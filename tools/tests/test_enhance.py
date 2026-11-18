import io
import shutil
import subprocess
from fractions import Fraction
from pathlib import Path

import pytest

from xrdome_tools import cli, enhance, formats, models, transcode
from xrdome_tools.errors import ToolError
from xrdome_tools.formats import ProbeInfo
from xrdome_tools.run import ChildFailed, Runner

REAL_REQUIRE = models.require

FILTERS = """Filters:
  T.. = Timeline support
  .S. = Slice threading
 ..C scale             V->V       Scale the input video size and/or convert the image format.
 ... fps               V->V       Force constant framerate.
"""
FILTERS_PL = FILTERS + " ... libplacebo        N->V       Apply various GPU filters from libplacebo\n"


def pinfo(w=1280, h=720, fps=(30, 1), dur=10.0, vfr=False, bit=8, video_extra=None, name_streams=True):
    video = {"index": 0, "codec_type": "video", "codec_name": "h264", **(video_extra or {})}
    audio = {"index": 1, "codec_type": "audio", "codec_name": "aac"}
    return ProbeInfo(w, h, (1, 1), "h264", "yuv420p", bit, dur, fps, vfr, None, False, None, 0,
                     [video, audio])


class Rec(Runner):
    def __init__(self, filters=FILTERS, fail=()):
        super().__init__(dry_run=False, log=io.StringIO())
        self.calls = []
        self.filters = filters
        self.fail = fail

    def run(self, argv, **kw):
        self.calls.append(argv)
        for needle in self.fail:
            if any(needle in a for a in argv):
                raise ChildFailed(argv, 1, ["it broke"])
        out = self.filters if "-filters" in argv else ""
        return subprocess.CompletedProcess(argv, 0, out, "")


@pytest.fixture
def env(monkeypatch, tmp_path):
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "cfg"))
    monkeypatch.setenv("XDG_CACHE_HOME", str(tmp_path / "cache"))
    monkeypatch.setenv("XDG_DATA_HOME", str(tmp_path / "data"))
    src = tmp_path / "clip.mp4"
    src.write_bytes(b"x")
    st = {"info": pinfo(), "missing": set(), "transcode": [], "free": 10**15}
    monkeypatch.setattr(formats, "probe", lambda r, p: st["info"])
    monkeypatch.setattr(transcode, "_require_programs", lambda: None)
    monkeypatch.setattr(enhance.shutil, "which",
                        lambda n: None if n in st["missing"] else f"/usr/bin/{n}")
    monkeypatch.setattr(enhance.models, "require", lambda n, p: tmp_path / "models" / n / "f")
    monkeypatch.setattr(shutil, "disk_usage", lambda p: shutil._ntuple_diskusage(0, 0, st["free"]))

    def fake_transcode(runner, s, d, **kw):
        st["transcode"].append((s, d, kw))
        return d
    monkeypatch.setattr(transcode, "transcode", fake_transcode)
    return src, st


def go(src, tmp_path, rec=None, **kw):
    rec = rec or Rec()
    kw.setdefault("tmp", tmp_path / "work")
    enhance.enhance(rec, src, tmp_path / "out.mkv", **kw)
    return rec


def ff(rec, needle):
    return [c for c in rec.calls if any(needle in a for a in c)]


def test_argv_builders():
    assert enhance.rife_argv(Path("/m/rife-v4.6"), Path("/t/in"), Path("/t/out"), 600) == \
        ["rife-ncnn-vulkan", "-m", "/m/rife-v4.6", "-i", "/t/in", "-o", "/t/out", "-n", "600"]
    a = enhance.esrgan_argv("realesr-animevideov3", Path("/m/esr"), Path("/t/in"), Path("/t/out"), 2)
    assert a[0] == "realesrgan-ncnn-vulkan"
    assert a[a.index("-n") + 1] == "realesr-animevideov3" and a[a.index("-s") + 1] == "2"
    assert a[a.index("-m") + 1] == "/m/esr"


def test_upscale_lanczos_without_libplacebo(env, tmp_path, capsys):
    src, st = env
    rec = go(src, tmp_path, Rec(FILTERS), upscale=2)
    vf = [c[c.index("-vf") + 1] for c in rec.calls if "-vf" in c and "joined.mkv" in c[-1]]
    assert vf == ["scale=2560:1440:flags=lanczos,format=yuv420p"]
    assert "path: upscale=lanczos (this ffmpeg has no libplacebo filter" in capsys.readouterr().out
    # Built in and working.
    rec = go(src, tmp_path, Rec(FILTERS_PL), upscale=2)
    vf = [c[c.index("-vf") + 1] for c in rec.calls if "-vf" in c and "joined.mkv" in c[-1]]
    assert vf == ["libplacebo=w=2560:h=1440:upscaler=ewa_lanczos,format=yuv420p"]
    assert "path: upscale=libplacebo" in capsys.readouterr().out
    # Listed but broken (no Vulkan device): falls through to the CPU scaler.
    rec = go(src, tmp_path, Rec(FILTERS_PL, fail=("libplacebo=w=128",)), upscale=2)
    vf = [c[c.index("-vf") + 1] for c in rec.calls if "-vf" in c and "joined.mkv" in c[-1]]
    assert vf == ["scale=2560:1440:flags=lanczos,format=yuv420p"]
    assert "libplacebo is built in but its test run failed (it broke)" in capsys.readouterr().out
    assert enhance.lanczos_filter(False, 10, 20) == "scale=10:20:flags=lanczos"


def test_lanczos_only_keeps_streams_and_encodes_once(env, tmp_path):
    src, st = env
    rec = go(src, tmp_path, upscale=2)
    (call,) = ff(rec, "joined.mkv")
    assert call[call.index("-map", call.index("-map") + 1) + 1] == "0:1"  # audio kept
    (s, d, kw) = st["transcode"][0]
    assert s.name == "joined.mkv" and kw["reencode"] and kw["as_"] == "flat"
    assert kw["extra_tokens"] == ["x2"]


def test_vfr_made_constant(env, tmp_path, capsys):
    src, st = env
    st["info"] = pinfo(fps=(5986, 100), vfr=True, video_extra={"r_frame_rate": "60/1"})
    rec = go(src, tmp_path, upscale=2, method="model")
    ext = [c for c in rec.calls if any("format=rgb24" in a for a in c)]
    assert ext
    for c in ext:
        i = c.index("-fps_mode")
        assert c[i:i + 4] == ["-fps_mode", "cfr", "-r", "60"]
    out = capsys.readouterr().out
    assert "path: fps=60 (variable frame rate source (59.86 fps measured); made constant" in out
    # Constant rate sources get no -fps_mode.
    st["info"] = pinfo()
    rec = go(src, tmp_path, upscale=2, method="model")
    assert not any("cfr" in c for c in rec.calls)


@pytest.mark.parametrize("name,w,h", [("clip_FSBS.mp4", 3840, 1080), ("clip_VR180.mp4", 4096, 2048)])
def test_refuse_stereo(env, tmp_path, name, w, h):
    src, st = env
    stereo = tmp_path / name
    stereo.write_bytes(b"x")
    st["info"] = pinfo(w=w, h=h)
    with pytest.raises(ToolError) as e:
        go(stereo, tmp_path, upscale=2)
    assert e.value.code == 2 and "--treat-as-2d" in str(e.value)
    assert st["transcode"] == []
    go(stereo, tmp_path, upscale=2, treat_as_2d=True)
    assert len(st["transcode"]) == 1


def test_disk_check(env, tmp_path):
    src, st = env
    st["free"] = 1000
    with pytest.raises(ToolError) as e:
        go(src, tmp_path, fps=Fraction(60))
    msg = str(e.value)
    assert e.value.code == 2 and "--tmp" in msg and "GB per 10 s chunk" in msg
    # 1280x720, 300 frames in, 600 out: 900 frames of 2.76 MB.
    assert "2.5 GB" in msg


def test_missing_binary(env, tmp_path):
    src, st = env
    st["missing"] = {"rife-ncnn-vulkan"}
    with pytest.raises(ToolError) as e:
        go(src, tmp_path, fps=Fraction(60))
    assert e.value.code == 2 and "https://github.com/nihui/rife-ncnn-vulkan/releases" in str(e.value)
    st["missing"] = {"realesrgan-ncnn-vulkan"}
    with pytest.raises(ToolError) as e:
        go(src, tmp_path, upscale=2, method="model")
    assert "https://github.com/xinntao/Real-ESRGAN-ncnn-vulkan/releases" in str(e.value)


def test_missing_model_stops_before_work(env, tmp_path, monkeypatch):
    src, st = env
    monkeypatch.setattr(enhance.models, "require", REAL_REQUIRE)  # the real one: nothing installed
    rec = Rec()
    with pytest.raises(ToolError) as e:
        go(src, tmp_path, rec, fps=Fraction(60))
    assert e.value.code == 2 and "rife-v4.6" in str(e.value)
    assert not [c for c in rec.calls if "rgb24" in " ".join(c)]


def test_combined_order(env, tmp_path):
    src, st = env
    rec = go(src, tmp_path, fps=Fraction(60), upscale=2, method="model", chunk=4)
    kinds = []
    for c in rec.calls:
        if c[0] == "rife-ncnn-vulkan":
            kinds.append("rife")
        elif c[0] == "realesrgan-ncnn-vulkan":
            kinds.append("esrgan")
        elif any("format=rgb24" in a for a in c):
            kinds.append("extract")
        elif "concat" in c:
            kinds.append("concat")
        elif "-framerate" in c:
            kinds.append("segment")
    # 300 frames, 4 s chunks at 30 fps: (0,120), (119,120), (238,62).
    assert kinds == ["extract", "rife", "esrgan", "segment"] * 3 + ["concat"]
    rife = [c for c in rec.calls if c[0] == "rife-ncnn-vulkan"]
    assert [c[c.index("-n") + 1] for c in rife] == ["240", "240", "124"]
    segs = [c for c in rec.calls if "-framerate" in c]
    assert [c[c.index("-frames:v") + 1] for c in segs] == ["238", "238", "124"]
    assert all(c[c.index("-framerate") + 1] == "60" for c in segs)
    (concat,) = [c for c in rec.calls if "concat" in c]
    assert concat[concat.index("-i", concat.index("-i") + 1) + 1] == str(src)
    assert "1:1" in concat  # the source's audio
    (s, d, kw) = st["transcode"][0]
    assert s.name == "joined.mkv" and kw["reencode"] and kw["extra_tokens"] == ["60fps", "x2"]
    # Nothing is left behind.
    assert list((tmp_path / "work").iterdir()) == []


def test_tool_failure_cleans_job(env, tmp_path):
    src, st = env
    with pytest.raises(ChildFailed):
        go(src, tmp_path, Rec(fail=("realesrgan",)), upscale=2, method="model")
    assert list((tmp_path / "work").iterdir()) == []
    assert st["transcode"] == []


def test_refusals(env, tmp_path):
    src, st = env
    for kw, needle in (({}, "nothing to do"), ({"fps": Fraction(30)}, "not above"),
                       ({"upscale": 4, "method": "model"}, "x2")):
        with pytest.raises(ToolError) as e:
            go(src, tmp_path, **kw)
        assert e.value.code == 2 and needle in str(e.value)


def test_dry_run_writes_nothing(env, tmp_path, capsys):
    src, st = env
    r = Runner(dry_run=True)
    enhance.enhance(r, src, tmp_path / "out.mkv", fps=Fraction(60), tmp=tmp_path / "work")
    out = capsys.readouterr().out
    assert "rife-ncnn-vulkan" in out and "then: transcode" in out
    assert not (tmp_path / "work").exists() and not (tmp_path / "out.mkv").exists()
    assert st["transcode"] == []


def test_cli_parses_and_names(env, tmp_path, monkeypatch):
    src, st = env
    seen = {}
    monkeypatch.setattr(enhance, "enhance", lambda r, s, d, **kw: seen.update(dst=d, **kw))
    assert cli.main(["enhance", str(src), "--fps", "60", "--upscale", "2", "--method", "model",
                     "--chunk", "5", "--tmp", str(tmp_path / "w")]) == 0
    assert seen["dst"].name == "clip_60fps_x2.mkv"
    assert seen["fps"] == 60 and seen["chunk"] == 5.0 and seen["method"] == "model"
    assert cli.main(["enhance", str(src)]) == 2
    assert cli.main(["enhance", str(src), "--fps", "abc"]) == 2
    assert enhance.output_tokens(Fraction(60000, 1001), None) == ["59p94fps"]
