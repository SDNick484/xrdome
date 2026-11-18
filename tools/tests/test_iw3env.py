import subprocess

import pytest

from xrdome_tools import cli, config, hw, iw3env, models
from xrdome_tools import setup as setup_cmd
from xrdome_tools.errors import ToolError
from xrdome_tools.run import Runner


def probe(gpus, platform="linux"):
    return hw.Probe(platform, gpus, set(), {}, None, [], "not set up")


@pytest.fixture
def paths(monkeypatch, tmp_path):
    for var in ("XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME"):
        monkeypatch.setenv(var, str(tmp_path / var.lower()))
    return config.paths()


def test_torch_variant():
    assert iw3env.torch_variant(probe(["nvidia"]), False) == "cuda"
    assert iw3env.torch_variant(probe(["nvidia"], "wsl2"), False) == "cuda"
    assert iw3env.torch_variant(probe(["amd"]), False) == "rocm"
    assert iw3env.torch_variant(probe(["intel", "amd"]), False) == "rocm"
    with pytest.raises(ToolError) as e:
        iw3env.torch_variant(probe([]), False)
    assert e.value.code == 2
    assert "--cpu" in str(e.value) and "about 60 fps on an RTX 5070 Ti" in str(e.value)
    assert iw3env.torch_variant(probe(["intel"]), True) == "cpu"


def test_setup_steps_pins(paths):
    steps = iw3env.setup_steps(paths, "cuda")
    for repo, pin in iw3env.PINS.items():
        assert any(s[-4:] == ["checkout", "--quiet", "--detach", pin] for s in steps), repo
        assert any(s[-2:] == [iw3env.REPOS[repo], pin] and "fetch" in s for s in steps), repo
    nunif = iw3env.nunif_dir(paths)
    assert any(s[-2:] == ["-r", str(nunif / "requirements-torch.txt")] for s in steps)
    assert not any("--index-url" in s for s in steps)
    assert all(isinstance(a, str) for s in steps for a in s)

    rocm = iw3env.setup_steps(paths, "rocm")
    assert any(s[-2:] == ["--index-url", iw3env.TORCH_INDEX["rocm"]] for s in rocm)
    assert not any("requirements-torch.txt" in " ".join(s) for s in rocm)
    cpu = iw3env.setup_steps(paths, "cpu")
    assert any(s[-2:] == ["--index-url", iw3env.TORCH_INDEX["cpu"]] for s in cpu)
    # The hub repos land where torch.hub looks, next to the checkpoints folder models.py uses.
    hub = iw3env.hub_dir(paths)
    assert (hub / "checkpoints") == (paths.data_dir / models.find("Any_V2_S").dest)
    assert any(s[-1] == str(hub / "nagadomi_Depth-Anything_iw3_main") and "init" in s for s in steps)


def test_setup_steps_idempotent(paths, tmp_path):
    # The same steps whatever is on disk: init and fetch work on a first run and a re-run.
    first = iw3env.setup_steps(paths, "cuda")
    (iw3env.nunif_dir(paths) / ".git").mkdir(parents=True)
    assert iw3env.setup_steps(paths, "cuda") == first
    assert not any("clone" in s for s in first)


def test_installed_marker(paths):
    assert "not set up" in iw3env.installed(paths)
    py = iw3env.python(paths)
    py.parent.mkdir(parents=True)
    py.write_text("")
    iw3env.write_marker(paths, "rocm")
    assert iw3env.installed(paths) is None
    assert iw3env.variant(paths) == "rocm"
    text = (paths.iw3_dir / iw3env.MARKER).read_text().replace(iw3env.PINS["nunif"], "0" * 40)
    (paths.iw3_dir / iw3env.MARKER).write_text(text)
    assert "other pins" in iw3env.installed(paths)


class Rec(Runner):
    def __init__(self, dry_run=False, log=None, out=""):
        super().__init__(dry_run=dry_run, log=log)
        self.calls = []
        self.out = out

    def run(self, argv, **kw):
        self.calls.append(argv)
        return subprocess.CompletedProcess(argv, 0, self.out, "")


@pytest.fixture
def setup_env(monkeypatch, paths):
    calls = []

    def make(dry_run=False, log=None):
        r = Rec(dry_run, log, out="True\n12.8\nNone\n")
        r.calls = calls
        return r

    monkeypatch.setattr(setup_cmd, "Runner", make)
    monkeypatch.setattr(setup_cmd.hw, "probe", lambda r, **kw: probe(["nvidia"]))
    monkeypatch.setattr(setup_cmd.shutil, "which", lambda prog: "/usr/bin/" + prog)
    return calls


def test_setup_prints_size_and_needs_yes(setup_env, monkeypatch, capsys):
    monkeypatch.setattr("sys.stdin.isatty", lambda: False)
    assert cli.main(["setup", "to3d"]) == 2
    err = capsys.readouterr().err
    assert "about 4 to 5 GB" in err and "--yes" in err
    assert setup_env == []


def test_setup_runs_steps_with_yes(setup_env, paths):
    assert cli.main(["setup", "to3d", "--yes"]) == 0
    steps = iw3env.setup_steps(paths, "cuda")
    assert setup_env[:len(steps)] == steps
    assert iw3env.variant(paths) == "cuda"


def test_setup_rocm_gpu_unseen(setup_env, monkeypatch, paths, capsys):
    monkeypatch.setattr(setup_cmd.hw, "probe", lambda r, **kw: probe(["amd"]))
    monkeypatch.setattr(iw3env, "compute", lambda r, p: "none")
    assert cli.main(["setup", "to3d", "--yes"]) == 2
    err = capsys.readouterr().err
    assert "cannot see the GPU" in err and "--cpu" in err


def test_setup_no_gpu_refused(setup_env, monkeypatch, capsys):
    monkeypatch.setattr(setup_cmd.hw, "probe", lambda r, **kw: probe([]))
    assert cli.main(["setup", "to3d", "--yes"]) == 2
    assert "--cpu" in capsys.readouterr().err
    assert setup_env == []


def test_setup_fetch_models(setup_env, monkeypatch, paths):
    fetched = []
    monkeypatch.setattr(models, "fetch", lambda r, m, p: fetched.append(m.name) or models.target(m, p))
    assert cli.main(["setup", "to3d", "--yes", "--fetch-models"]) == 0
    assert fetched == ["Any_V2_S", "VDA_S"]


def test_setup_dry_run_runs_nothing(setup_env, paths, capsys):
    assert cli.main(["setup", "to3d", "--dry-run"]) == 0
    assert not paths.iw3_dir.exists()
