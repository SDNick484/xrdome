import dataclasses
import hashlib
import subprocess
from pathlib import Path

import pytest

from xrdome_depth import export
from xrdome_tools import cli, config, depthenv, models
from xrdome_tools import setup as setup_cmd
from xrdome_tools.errors import ToolError
from xrdome_tools.run import Runner

# Spec 4 (docs/superpowers/specs/2026-11-19-depth-3d-design.md), copied here so an edit of models.py
# that changes a hash or a pin fails a test rather than passing silently.
SPEC_HASHES = {
    "dav2s_518x392.ncnn.param": "d9d9a8eef2f63e8f3268184602dd79765e13b23acc4fb311c528c735fe3fa26c",
    "dav2s_518x392.ncnn.bin": "f155d78193416c6c0e9f55dd0c36b5bd94d25410c23bbfb51439107d2a6d1846",
    "dav2s_392x518.ncnn.param": "a9e2d29e96ce6b3d067c172665519eb7854e3216fd8348ec603b32008da6d839",
    "dav2s_392x518.ncnn.bin": "282be6cdd0c2fd01eb11001a600d9945a04edd46f047bd2d83a34cc937bbfdc8",
    "dav2s_322x182.ncnn.param": "b1373247d51fa69b49757559e549cfb2b00e9541c9a7fc90cfe308e4cf936741",
    "dav2s_322x182.ncnn.bin": "e2d5048f550aadfa3f02ca0e935a9c00d580ad09f6f9d0aaa129a3f6cc68c62b",
}
REVISION = "5426e4f0f36572d16453bbda7a8389317b1bef99"
SAFETENSORS = "3152477ce0d8d6978d76b995120de97cb5b928701fd0f817769f59e249a16b70"


@pytest.fixture
def paths(monkeypatch, tmp_path):
    for var in ("XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME"):
        monkeypatch.setenv(var, str(tmp_path / var.lower()))
    return config.paths()


def test_pins():
    assert depthenv.VENV_PINS == {"ncnn": "1.0.20261020", "numpy": "2.5.4",
                                  "opencv-python-headless": "5.0.0.93", "pillow": "13.0.0"}
    assert depthenv.EXPORT_PINS == {"transformers": "5.23.0", "pnnx": "20261020",
                                    "safetensors": "0.8.0", "huggingface_hub": "2.4.0"}
    assert depthenv.TORCH_REQUIREMENT == "torch==2.11.0+cpu"
    assert depthenv.TORCH_INDEX == "https://download.pytorch.org/whl/cpu"
    assert models.DAV2S_REVISION == REVISION


def test_expected_hashes_are_the_specs():
    assert depthenv.expected() == SPEC_HASHES
    src = models.find("Any_V2_S_hf")
    assert src.sha256 == SAFETENSORS and src.revision == REVISION and src.file == "model.safetensors"


def test_export_script_matches_registry():
    assert export.REVISION == REVISION and export.SAFETENSORS_SHA256 == SAFETENSORS
    assert export.REPO == "depth-anything/Depth-Anything-V2-Small-hf"
    assert export.SIZES == ((518, 392), (392, 518), (322, 182))
    names = {export.output_name(w, h, ext) for w, h in export.SIZES for ext in ("param", "bin")}
    assert names == set(SPEC_HASHES)
    assert depthenv.EXPORT_SCRIPT.is_file()


def test_registry_entries():
    gen = [m for m in models.REGISTRY if m.generated_by]
    assert len(gen) == 6
    assert all(m.used_by == "depth" and m.licence == "Apache-2.0" and m.dest == "models/dav2s-ncnn"
               and m.generated_by == "setup depth" and not m.url and m.revision == REVISION
               and len(m.sha256) == 64 for m in gen)
    assert len({m.name for m in models.REGISTRY}) == len(models.REGISTRY)
    assert models.find("dav2s-ncnn-518x392").file == "dav2s_518x392.ncnn.bin"
    assert models.find("dav2s-ncnn-518x392.param").file == "dav2s_518x392.ncnn.param"
    src = models.find("Any_V2_S_hf")
    assert src.licence == "Apache-2.0" and src.used_by == "depth" and src.dest == "models/dav2s-hf"


def test_generated_never_fetched(paths):
    m = models.find("dav2s-ncnn-322x182")
    with pytest.raises(ToolError, match="never downloaded") as e:
        models.fetch(Runner(), m, paths)
    assert e.value.code == 2 and "xrdome-tools setup depth" in str(e.value)
    with pytest.raises(ToolError, match="setup depth"):
        models.require("dav2s-ncnn-322x182", paths)


def test_models_lists_generated(paths, capsys):
    assert cli.main(["models"]) == 0
    out = capsys.readouterr().out
    assert "dav2s-ncnn-518x392 " in out and "Any_V2_S_hf" in out
    assert "generated, never downloaded: run `xrdome-tools setup depth`" in out
    assert "--fetch dav2s" not in out and "--fetch Any_V2_S_hf" not in out
    assert cli.main(["models", "--check"]) == 2


def test_models_fetch_all_skips_generated(paths, monkeypatch):
    calls = []
    monkeypatch.setattr(models, "fetch", lambda r, m, p: calls.append(m.name) or models.target(m, p))
    assert cli.main(["models", "--fetch"]) == 0
    assert calls == ["Any_V2_S", "VDA_S", "rife-v4.6", "rife-v4.6.param"]


def test_steps(paths):
    d = depthenv.depth_dir(paths)
    assert d == paths.data_dir / "depth"
    venv = depthenv.venv_steps(paths)
    assert venv[0] == ["python3", "-m", "venv", str(d / "venv")]
    assert venv[-1][-4:] == ["ncnn==1.0.20261020", "numpy==2.5.4", "opencv-python-headless==5.0.0.93",
                            "pillow==13.0.0"]
    assert depthenv.gpu_step(paths)[-1] == "import ncnn; print(ncnn.get_gpu_count())"
    exp = depthenv.export_steps(paths)
    assert exp[0] == ["python3", "-m", "venv", str(d / "export-venv")]
    assert exp[2][-3:] == ["torch==2.11.0+cpu", "--index-url", "https://download.pytorch.org/whl/cpu"]
    assert exp[3][-4:] == ["transformers==5.23.0", "pnnx==20261020", "safetensors==0.8.0",
                          "huggingface_hub==2.4.0"]
    assert exp[4][:2] == [str(d / "export-venv" / "bin" / "python"), str(depthenv.EXPORT_SCRIPT)]
    assert all(isinstance(a, str) for s in venv + exp for a in s)


def test_python3_override(paths):
    paths.config_file.parent.mkdir(parents=True)
    paths.config_file.write_text("[programs]\npython3 = /opt/py/bin/python3\n")
    assert depthenv.venv_steps(paths)[0][0] == "/opt/py/bin/python3"


class Rec(Runner):
    """Records every step; a step running the export script writes `files` into its --out."""

    def __init__(self, dry_run=False, log=None, gpus="1", files=None, calls=None):
        super().__init__(dry_run=dry_run, log=log)
        self.gpus, self.files, self.calls = gpus, files or {}, calls

    def run(self, argv, **kw):
        self.calls.append(argv)
        if self.dry_run:
            print(" ".join(argv))
        if "--out" in argv and not self.dry_run:
            out = Path(argv[argv.index("--out") + 1])
            out.mkdir(parents=True)
            for name, data in self.files.items():
                (out / name).write_bytes(data)
        return subprocess.CompletedProcess(argv, 0, self.gpus + "\n", "")


@pytest.fixture
def env(monkeypatch, paths):
    state = {"calls": [], "gpus": "1", "files": {}}
    monkeypatch.setattr(setup_cmd, "Runner", lambda dry_run=False, log=None: Rec(
        dry_run, log, state["gpus"], state["files"], state["calls"]))
    monkeypatch.setattr(setup_cmd.shutil, "which", lambda prog: "/usr/bin/" + prog)
    return state


def fake_export(monkeypatch):
    """Registry entries whose hashes are those of small fake files; returns the files."""
    files = {name: name.encode() for name in SPEC_HASHES}
    files["model.safetensors"] = b"source"
    reg = []
    for m in models.REGISTRY:
        if m.file in files:
            m = dataclasses.replace(m, sha256=hashlib.sha256(files[m.file]).hexdigest())
        reg.append(m)
    monkeypatch.setattr(models, "REGISTRY", reg)
    return files


def test_dry_run_lists_steps_and_runs_nothing(env, paths, capsys):
    assert cli.main(["setup", "depth", "--dry-run"]) == 0
    out = capsys.readouterr().out
    steps = (depthenv.venv_steps(paths) + [depthenv.gpu_step(paths)] + depthenv.export_steps(paths))
    lines = [ln for ln in out.splitlines() if not ln.startswith(("setup depth downloads", "would"))]
    assert lines == [" ".join(s) for s in steps]
    assert "would check the sha256" in out and "would delete" in out and "would write" in out
    assert "about 1.3 GB" in out
    assert not paths.data_dir.exists()


def test_needs_yes_without_terminal(env, monkeypatch, capsys):
    monkeypatch.setattr("sys.stdin.isatty", lambda: False)
    assert cli.main(["setup", "depth"]) == 2
    err = capsys.readouterr().err
    assert "about 1.3 GB" in err and "--yes" in err
    assert env["calls"] == []


def test_fetch_models_refused(env, capsys):
    assert cli.main(["setup", "depth", "--yes", "--fetch-models"]) == 2
    assert env["calls"] == []


def test_setup_ok(env, monkeypatch, paths, capsys):
    env["files"] = fake_export(monkeypatch)
    assert cli.main(["setup", "depth", "--yes"]) == 0
    steps = depthenv.venv_steps(paths) + [depthenv.gpu_step(paths)] + depthenv.export_steps(paths)
    assert env["calls"] == steps
    d = depthenv.depth_dir(paths)
    for name in SPEC_HASHES:
        assert (depthenv.ncnn_dir(paths) / name).read_bytes() == name.encode()
    assert (depthenv.source_dir(paths) / "model.safetensors").read_bytes() == b"source"
    assert not (d / "export-venv").exists() and not depthenv.export_out(paths).exists()
    assert not depthenv.export_work(paths).exists()
    assert depthenv.variant(paths) == "vulkan"
    assert [models.status(m, paths) for m in models.REGISTRY if m.used_by == "depth"] == ["ok"] * 7
    assert (d / "setup.log").exists()


def test_marker_written_last_and_installed(env, monkeypatch, paths):
    assert "not set up" in depthenv.installed(paths)
    env["files"] = fake_export(monkeypatch)
    assert cli.main(["setup", "depth", "--yes"]) == 0
    py = depthenv.python(paths)
    py.parent.mkdir(parents=True)
    py.write_text("")
    assert depthenv.installed(paths) is None
    f = depthenv.ncnn_dir(paths) / "dav2s_322x182.ncnn.bin"
    f.write_bytes(b"changed")
    assert "dav2s-ncnn-322x182" in depthenv.installed(paths)
    f.write_bytes(b"dav2s_322x182.ncnn.bin")
    text = (depthenv.depth_dir(paths) / depthenv.MARKER).read_text().replace("1.0.20261020", "1.0.0")
    (depthenv.depth_dir(paths) / depthenv.MARKER).write_text(text)
    assert "other pins" in depthenv.installed(paths)


def test_no_gpu_refused_before_export(env, paths, capsys):
    env["gpus"] = "0"
    assert cli.main(["setup", "depth", "--yes"]) == 2
    assert "--cpu" in capsys.readouterr().err
    assert env["calls"] == depthenv.venv_steps(paths) + [depthenv.gpu_step(paths)]
    assert not (depthenv.depth_dir(paths) / depthenv.MARKER).exists()


def test_cpu_flag_goes_on_and_records_variant(env, monkeypatch, paths, capsys):
    env["gpus"] = "0"
    env["files"] = fake_export(monkeypatch)
    assert cli.main(["setup", "depth", "--yes", "--cpu"]) == 0
    assert "costs xrdome frames" in capsys.readouterr().out
    assert depthenv.variant(paths) == "cpu"


def test_gpu_present_cpu_flag_changes_nothing(env, monkeypatch, paths):
    env["files"] = fake_export(monkeypatch)
    assert cli.main(["setup", "depth", "--yes", "--cpu"]) == 0
    assert depthenv.variant(paths) == "vulkan"


def test_gpu_check_that_fails_counts_as_none(paths):
    class Boom(Runner):
        def run(self, argv, **kw):
            raise OSError("no such file")

    assert depthenv.gpu_count(Boom(), paths) == 0


def test_hash_mismatch_names_file_and_stops(env, monkeypatch, paths, capsys):
    files = fake_export(monkeypatch)
    files["dav2s_392x518.ncnn.param"] = b"different"
    files["dav2s_322x182.ncnn.bin"] = b"also different"
    env["files"] = files
    assert cli.main(["setup", "depth", "--yes"]) == 2
    err = capsys.readouterr().err
    assert "dav2s_392x518.ncnn.param" in err and "dav2s_322x182.ncnn.bin" in err
    assert "dav2s_518x392" not in err
    assert str(depthenv.export_out(paths)) in err
    assert not depthenv.ncnn_dir(paths).exists()  # nothing installed
    assert (depthenv.export_out(paths) / "dav2s_392x518.ncnn.param").read_bytes() == b"different"
    assert not (depthenv.depth_dir(paths) / depthenv.MARKER).exists()
    assert not (depthenv.depth_dir(paths) / "export-venv").exists()


def test_missing_export_file_reported(env, monkeypatch, paths, capsys):
    files = fake_export(monkeypatch)
    del files["dav2s_322x182.ncnn.bin"]
    env["files"] = files
    assert cli.main(["setup", "depth", "--yes"]) == 2
    err = capsys.readouterr().err
    assert "dav2s_322x182.ncnn.bin: expected" in err and "got missing" in err


def test_stale_output_does_not_pass(env, monkeypatch, paths, capsys):
    files = fake_export(monkeypatch)
    out = depthenv.export_out(paths)
    out.mkdir(parents=True)
    for name, data in files.items():  # a good earlier export left behind
        (out / name).write_bytes(data)
    env["files"] = {}  # this run's export produces nothing
    assert cli.main(["setup", "depth", "--yes"]) == 2


def test_verify_directly(tmp_path, monkeypatch):
    files = fake_export(monkeypatch)
    for name, data in files.items():
        (tmp_path / name).write_bytes(data)
    depthenv.verify(tmp_path)
    (tmp_path / "model.safetensors").write_bytes(b"x")
    with pytest.raises(ToolError, match="source weights") as e:
        depthenv.verify(tmp_path)
    assert e.value.code == 2
