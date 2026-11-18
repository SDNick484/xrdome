import io
from pathlib import Path

import pytest

from xrdome_tools import formats, hw, transcode
from xrdome_tools.errors import ToolError
from xrdome_tools.formats import ProbeInfo
from xrdome_tools.run import ChildFailed, Runner


def pinfo(w=1920, h=1080, codec="hevc", bit=8, streams=None, stereo=None, inv=False, dur=10.0, **vkw):
    video = {"index": 0, "codec_type": "video", "codec_name": codec, **vkw}
    return ProbeInfo(w, h, (1, 1), codec, "yuv420p", bit, dur, (25, 1), False, stereo, inv, None, 0,
                     [video] + (streams or []))


class Rec(Runner):
    """A runner that records argv instead of running (and prints nothing)."""

    def __init__(self, fail=()):
        super().__init__(dry_run=False)
        self.calls = []
        self.fail = fail

    def run(self, argv, **kw):
        self.calls.append(argv)
        for needle in self.fail:
            if needle in argv:
                raise ChildFailed(argv, 1, ["boom"])
        import subprocess
        out = " V....D libx265  x\n" if "-encoders" in argv else ""
        return subprocess.CompletedProcess(argv, 0, out, "")


@pytest.fixture
def setup(monkeypatch, tmp_path):
    src = tmp_path / "clip.mp4"
    src.write_bytes(b"x")
    state = {"info": pinfo(), "probe": None}
    monkeypatch.setattr(formats, "probe", lambda r, p: state["info"] if p == src else state.get("out", state["info"]))
    monkeypatch.setattr(transcode, "_require_programs", lambda: None)
    monkeypatch.setattr(transcode.os, "replace", lambda a, b: None)

    def fake_probe(runner, **kw):
        return state["probe"] or hw.Probe("linux", [], {"libx265", "hevc_vaapi", "hevc_nvenc"},
                                          {"hevc_nvenc": "no device", "hevc_vaapi": "ok"},
                                          hw.VaapiInfo("/dev/dri/renderD128", set(), set()), [], "none")
    monkeypatch.setattr(transcode.hw, "probe", fake_probe)
    return src, state


def ffmpeg_argv(rec):
    return next(c for c in rec.calls if "matroska" in c)


def run_t(src, tmp_path, **kw):
    rec = Rec(kw.pop("fail", ()))
    rec.dry_run = False
    dst = tmp_path / "out.mkv"
    rec.log = io.StringIO()
    # verify reads back through the monkeypatched probe
    try:
        transcode.transcode(rec, src, dst, **kw)
    except ToolError as e:
        return rec, e
    return rec, None


def test_remux_when_friendly(setup, tmp_path, capsys):
    src, st = setup
    rec, err = run_t(src, tmp_path, encoder="libx265")
    a = ffmpeg_argv(rec)
    assert "-c:v" in a and a[a.index("-c:v") + 1] == "copy"
    assert "stereo_mode=mono" in a
    out = capsys.readouterr().out
    assert "path: transcode=remux (already HEVC" in out
    rec, err = run_t(src, tmp_path, encoder="libx265", reencode=True)
    a = ffmpeg_argv(rec)
    assert a[a.index("-c:v") + 1] == "libx265"


def test_h264_too_wide_is_reencoded(setup, tmp_path, capsys):
    src, st = setup
    st["info"] = pinfo(4320, 2160, codec="h264", stereo="side by side")
    rec, err = run_t(src, tmp_path, encoder="libx265")
    a = ffmpeg_argv(rec)
    assert a[a.index("-c:v") + 1] == "libx265"
    assert "remux" not in capsys.readouterr().out


def test_max_width_forces_encode(setup, tmp_path):
    src, st = setup
    st["info"] = pinfo(3840, 1920, codec="hevc")
    rec, _ = run_t(src, tmp_path, encoder="libx265", max_width=1920)
    a = ffmpeg_argv(rec)
    assert a[a.index("-c:v") + 1] == "libx265"


def test_vaapi_qvbr_fallback(setup, tmp_path, capsys):
    src, st = setup
    st["info"] = pinfo(codec="h264")
    # the QVBR test encode contains this argument; the fallback must use CQP
    rec, _ = run_t(src, tmp_path, fail=("QVBR",))
    a = ffmpeg_argv(rec)
    assert "CQP" in a and "QVBR" not in a
    assert a[a.index("-c:v") + 1] == "hevc_vaapi"
    rec, _ = run_t(src, tmp_path)
    assert "QVBR" in ffmpeg_argv(rec)


def test_hdr_passthrough(setup, tmp_path, capsys):
    src, st = setup
    st["info"] = pinfo(codec="h264", bit=10, color_transfer="smpte2084", color_primaries="bt2020",
                       color_space="bt2020nc")
    rec, _ = run_t(src, tmp_path, encoder="libx265")
    a = " ".join(ffmpeg_argv(rec))
    assert "-color_primaries bt2020 -color_trc smpte2084 -colorspace bt2020nc" in a
    # VA-API: the HDR test encode fails, so no tags and a line says so
    rec, _ = run_t(src, tmp_path, fail=("-color_trc",))
    assert "-color_trc" not in ffmpeg_argv(rec)
    assert "HDR metadata is not passed through" in capsys.readouterr().out


def test_verify_reasons(monkeypatch):
    src = pinfo(streams=[{"index": 1, "codec_type": "audio", "codec_name": "aac"}], dur=10.0)
    f = formats.AS_CHOICES["fsbs"]
    good = pinfo(stereo="side by side", streams=[{"index": 1, "codec_type": "audio", "codec_name": "aac"}])
    monkeypatch.setattr(formats, "probe", lambda r, p: good)
    assert transcode.verify(Runner(), src, Path("x"), f, "hevc") == []

    bad = pinfo(stereo="top and bottom", streams=good.streams)
    monkeypatch.setattr(formats, "probe", lambda r, p: bad)
    r = transcode.verify(Runner(), src, Path("x"), f, "hevc")
    assert len(r) == 1 and "Stereo 3D" in r[0]

    short = pinfo(stereo="side by side", streams=good.streams, dur=9.0)
    monkeypatch.setattr(formats, "probe", lambda r, p: short)
    r = transcode.verify(Runner(), src, Path("x"), f, "hevc")
    assert len(r) == 1 and "duration" in r[0]

    noaudio = pinfo(stereo="side by side")
    monkeypatch.setattr(formats, "probe", lambda r, p: noaudio)
    r = transcode.verify(Runner(), src, Path("x"), f, "hevc")
    assert len(r) == 1 and "audio" in r[0]


def test_verify_failure_exits_3_and_keeps_output(setup, tmp_path):
    src, st = setup
    st["out"] = pinfo(stereo="top and bottom")
    st["info"] = pinfo(stereo="side by side")
    rec, err = run_t(src, tmp_path, encoder="libx265")
    assert err is not None and err.code == 3 and "kept" in str(err)


def test_refuses_input_as_output(setup, tmp_path):
    src, _ = setup
    with pytest.raises(ToolError) as e:
        transcode.transcode(Rec(), src, src)
    assert e.value.code == 2


def test_encoder_codec_mismatch(setup, tmp_path):
    src, _ = setup
    rec, err = run_t(src, tmp_path, encoder="libx265", codec="av1")
    assert err is not None and err.code == 2
