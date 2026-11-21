import os
import subprocess
from pathlib import Path

import pytest

from xrdome_tools import cli, config, formats, hw, iw3env, models, to3d
from xrdome_tools.formats import ProbeInfo
from xrdome_tools.run import Runner


def pinfo(w=1920, h=1080, fps=(30, 1)):
    video = {"index": 0, "codec_type": "video", "codec_name": "h264"}
    return ProbeInfo(w, h, (1, 1), "h264", "yuv420p", 8, 10.0, fps, False, None, False, None, 0, [video])


def argv(**kw):
    p = config.paths({"HOME": "/home/u"})
    base = dict(model="Any_V2_S", divergence=2.0, convergence=0.5, fg_scale=0.0, fps=(60, 1),
                half=False, nvenc=False)
    return to3d.iw3_argv(p, Path("in.mp4"), Path("out.mp4"), **{**base, **kw})


def opt(a, name):
    return a[a.index(name) + 1]


def test_iw3_argv():
    a = argv(fps=(120000, 1001))
    assert a[:3] == ["/home/u/.local/share/xrdome-tools/iw3/venv/bin/python", "-m", "iw3.cli"]
    assert opt(a, "--depth-model") == "Any_V2_S"
    assert opt(a, "--divergence") == "2.0"
    assert opt(a, "--convergence") == "0.5"
    assert opt(a, "--max-fps") == "120"
    assert "--foreground-scale" not in a and "--ema-normalize" not in a
    assert opt(a, "--video-codec") == "libx264" and opt(a, "--crf") == "12"
    assert opt(argv(fps=(30000, 1001)), "--max-fps") == "30"
    assert opt(argv(fps=(60, 1)), "--max-fps") == "60"

    v = argv(model="VDA_S", fg_scale=1.0)
    assert "--ema-normalize" in v and "--scene-detect" in v
    assert opt(v, "--foreground-scale") == "1.0"
    n = argv(nvenc=True)
    assert opt(n, "--video-codec") == "hevc_nvenc" and "--crf" not in n
    assert "--half-sbs" in argv(half=True)
    assert opt(argv(cpu=True), "--gpu") == "-1"


def test_iw3_argv_leading_dash():
    p = config.paths({"HOME": "/home/u"})
    a = to3d.iw3_argv(p, Path("-x.mp4"), Path("out.mp4"), model="Any_V2_S", divergence=2.0,
                      convergence=0.5, fg_scale=0.0, fps=(25, 1), half=False, nvenc=False)
    assert opt(a, "-i") == "./-x.mp4"


def test_iw3_argv_depth_resolution():
    assert "--resolution" not in argv()
    assert opt(argv(depth_resolution=280), "--resolution") == "280"


def test_output_stem():
    assert to3d.output_stem("pan", "Any_V2_S", 2.0, 0.5, 0.0, 280) == "pan_AnyV2S_div2.0_dr280"
    assert to3d.output_stem("pan", "Any_V2_S", 2.0, 0.5, 0.0) == "pan_AnyV2S_div2.0"
    assert to3d.output_stem("pan", "VDA_S", 3.0, 0.65, 1.0) == "pan_VDAS_div3.0_c0.65_fs1.0"
    assert to3d.output_stem("pan", "Any_V2_S", 2, 0.5, 0.5) == "pan_AnyV2S_div2.0_fs0.5"


# ---- the subcommand, with every child monkeypatched ------------------------------------

class Rec(Runner):
    def __init__(self, calls, dry_run=False, log=None):
        super().__init__(dry_run=dry_run, log=log)
        self.calls = calls

    def run(self, argv, **kw):
        self.calls.append(argv)
        if not self.dry_run and "iw3.cli" in argv:
            Path(argv[argv.index("-o") + 1]).write_bytes(b"sbs")
        return subprocess.CompletedProcess(argv, 0, "", "")


@pytest.fixture
def env(monkeypatch, tmp_path):
    for var in ("XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME"):
        monkeypatch.setenv(var, str(tmp_path / var.lower()))
    monkeypatch.setenv(to3d.AOTRITON_VAR, "")  # recorded, so the test's own change is undone
    monkeypatch.delenv(to3d.AOTRITON_VAR)
    p = config.paths()
    src = tmp_path / "pan.mp4"
    src.write_bytes(b"x")
    st = {"info": pinfo(), "compute": "cuda", "calls": [], "transcode": [], "installed": None}
    monkeypatch.setattr(to3d, "Runner", lambda dry_run=False, log=None: Rec(st["calls"], dry_run, log))
    monkeypatch.setattr(iw3env, "installed", lambda p: st["installed"])
    monkeypatch.setattr(iw3env, "check", lambda r, p: st.get("check") or hw.TorchCheck(st["compute"], st["compute"], ""))
    monkeypatch.setattr(models, "require", lambda name, p: Path("/x") / name)
    monkeypatch.setattr(formats, "probe", lambda r, path: st["info"])
    monkeypatch.setattr(to3d.transcode, "_require_programs", lambda: None)

    def fake_transcode(runner, src, dst, **kw):
        st["transcode"].append((src, dst, kw, src.exists()))
        return dst

    monkeypatch.setattr(to3d.transcode, "transcode", fake_transcode)
    st["src"], st["paths"] = src, p
    return st


def test_pipeline(env, capsys):
    assert cli.main(["to3d", str(env["src"])]) == 0
    (iw3,) = env["calls"]
    assert "iw3.cli" in iw3 and opt(iw3, "--video-codec") == "hevc_nvenc"
    inter = Path(opt(iw3, "-o"))
    (src, dst, kw, existed), = env["transcode"]
    assert src == inter and existed
    assert kw["as_"] == "fsbs"
    assert dst == env["src"].parent / "pan_AnyV2S_div2.0_FSBS.mkv"
    assert not inter.exists()
    out = capsys.readouterr().out
    assert "path: iw3-encode=hevc_nvenc" in out
    assert (env["src"].parent / "pan_AnyV2S_div2.0_FSBS.mkv.log").read_text().startswith("path: compute=cuda")


def test_pipeline_half_rocm(env):
    env["compute"] = "rocm"
    assert cli.main(["to3d", str(env["src"]), "--half", "--model", "VDA_S"]) == 0
    (iw3,) = env["calls"]
    assert opt(iw3, "--video-codec") == "libx264" and "--half-sbs" in iw3
    (_, dst, kw, _), = env["transcode"]
    assert kw["as_"] == "hsbs"
    assert dst.name == "pan_VDAS_div2.0_HSBS.mkv"


def test_intermediate_removed_on_failure(env, monkeypatch):
    def boom(runner, src, dst, **kw):
        raise KeyboardInterrupt

    monkeypatch.setattr(to3d.transcode, "transcode", boom)
    assert cli.main(["to3d", str(env["src"])]) == 130
    assert not list(env["paths"].cache_dir.glob("to3d-*"))


def test_model_allow_list(env, capsys):
    assert cli.main(["to3d", str(env["src"]), "--model", "Any_V2_L"]) == 2
    assert "not an allowed model" in capsys.readouterr().err
    assert cli.main(["to3d", str(env["src"]), "--model", "iw3_depth_aa"]) == 2
    assert env["calls"] == []


def test_refuse_without_compute(env, capsys):
    env["compute"] = "none"
    assert cli.main(["to3d", str(env["src"])]) == 2
    err = capsys.readouterr().err
    assert "--cpu" in err and "about 60 fps on an RTX 5070 Ti" in err
    assert env["calls"] == []
    assert cli.main(["to3d", str(env["src"]), "--cpu"]) == 0
    assert opt(env["calls"][0], "--gpu") == "-1"
    assert opt(env["calls"][0], "--video-codec") == "libx264"


def test_refuse_gpu_that_fails_its_test(env, capsys):
    problem = hw.torch_problem("rocm", "gfx1152", "HIP error: invalid device function", "")
    env["check"] = hw.TorchCheck("none", "rocm", "gfx1152", problem)
    assert cli.main(["to3d", str(env["src"])]) == 2
    err = capsys.readouterr().err
    assert "no GPU code for gfx1152" in err and "HSA_OVERRIDE_GFX_VERSION=11.0.2" in err
    assert env["calls"] == []
    assert cli.main(["to3d", str(env["src"]), "--cpu"]) == 0
    assert opt(env["calls"][0], "--gpu") == "-1"
    assert "the rocm GPU failed PyTorch's test" in capsys.readouterr().out


def test_rocm_arch_in_path_line(env, capsys):
    env["check"] = hw.TorchCheck("rocm", "rocm", "gfx1152")
    assert cli.main(["to3d", str(env["src"])]) == 0
    assert "path: compute=rocm (PyTorch in the iw3 environment ran its GPU test on gfx1152)" in capsys.readouterr().out


def test_refuse_before_setup(env, capsys):
    env["installed"] = "iw3 is not set up in /somewhere"
    assert cli.main(["to3d", str(env["src"])]) == 2
    assert "xrdome-tools setup to3d" in capsys.readouterr().err
    assert env["calls"] == []


def test_refuse_missing_model(env, monkeypatch, capsys):
    monkeypatch.setattr(models, "require", lambda name, p: (_ for _ in ()).throw(
        models.ToolError(models.instructions(models.find(name), p))))
    assert cli.main(["to3d", str(env["src"])]) == 2
    assert "models --check" in capsys.readouterr().err
    assert env["calls"] == []


def test_output_exists(env):
    (env["src"].parent / "pan_AnyV2S_div2.0_FSBS.mkv").write_bytes(b"")
    assert cli.main(["to3d", str(env["src"])]) == 1
    assert env["calls"] == []


@pytest.mark.parametrize("size,fps,hint", [
    ((1280, 720), (60, 1), True),
    ((1920, 1080), (30, 1), True),
    ((1920, 1080), (60000, 1001), False),
])
def test_hint_low_res_or_fps(env, capsys, size, fps, hint):
    env["info"] = pinfo(*size, fps=fps)
    assert cli.main(["to3d", str(env["src"])]) == 0
    assert ("xrdome-tools enhance" in capsys.readouterr().out) == hint


def test_dry_run_runs_nothing(env, capsys):
    assert cli.main(["to3d", str(env["src"]), "--dry-run"]) == 0
    assert env["transcode"] == []
    assert not (env["src"].parent / "pan_AnyV2S_div2.0_FSBS.mkv.log").exists()
    assert "then: xrdome-tools transcode" in capsys.readouterr().out


def test_rocm_attention_on_by_default(env, capsys, monkeypatch):
    seen = {}

    def check(r, p):
        seen["at_check"] = os.environ.get(to3d.AOTRITON_VAR)
        return hw.TorchCheck("rocm", "rocm", "gfx1152")

    monkeypatch.setattr(iw3env, "check", check)
    assert cli.main(["to3d", str(env["src"])]) == 0
    assert seen["at_check"] == "1"  # the GPU test runs the attention iw3 will use
    assert os.environ[to3d.AOTRITON_VAR] == "1"  # and iw3 inherits it
    assert "path: attention=aotriton" in capsys.readouterr().out


def test_rocm_attention_explicit_value_wins(env, capsys, monkeypatch):
    monkeypatch.setenv(to3d.AOTRITON_VAR, "0")
    env["compute"] = "rocm"
    assert cli.main(["to3d", str(env["src"])]) == 0
    assert os.environ[to3d.AOTRITON_VAR] == "0"
    assert f"path: attention=default ({to3d.AOTRITON_VAR}=0 from the environment)" in capsys.readouterr().out


def test_no_attention_line_on_cuda(env, capsys):
    assert cli.main(["to3d", str(env["src"])]) == 0
    assert "path: attention=" not in capsys.readouterr().out


def test_depth_resolution(env, capsys):
    assert cli.main(["to3d", str(env["src"]), "--depth-resolution", "280"]) == 0
    (iw3,) = env["calls"]
    assert opt(iw3, "--resolution") == "280"
    (_, dst, _, _), = env["transcode"]
    assert dst.name == "pan_AnyV2S_div2.0_dr280_FSBS.mkv"
    assert "depth input 280" in capsys.readouterr().out


def test_depth_resolution_too_small(env, capsys):
    assert cli.main(["to3d", str(env["src"]), "--depth-resolution", "100"]) == 2
    assert "at least 224" in capsys.readouterr().err
    assert env["calls"] == []
