import json
import os
import subprocess
from pathlib import Path

import pytest

from xrdome_tools import formats
from xrdome_tools.errors import ToolError
from xrdome_tools.formats import AS_CHOICES, Format, ProbeInfo

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / "tests" / "data"
FIXTURES = Path(__file__).parent / "fixtures" / "ffprobe"


def tsv(name):
    rows = []
    for line in (DATA / name).read_text(encoding="utf-8").splitlines():
        if line and not line.startswith("#"):
            rows.append(line.split("\t"))
    return rows


def info(w, h, **kw):
    base = dict(width=w, height=h, sar=(1, 1), codec="h264", pix_fmt="yuv420p", bit_depth=8,
                duration=1.0, fps=(25, 1), vfr=False, stereo=None, stereo_inverted=False,
                spherical=None, video_index=0, streams=[])
    base.update(kw)
    return ProbeInfo(**base)


def load(name):
    return json.loads((FIXTURES / name).read_text(encoding="utf-8"))


def test_name_hints_table():
    rows = tsv("detect_names.tsv")
    assert len(rows) == 22
    for name, proj, layout, packing in rows:
        want = {k: v for k, v in (("proj", proj), ("layout", layout), ("packing", packing))
                if v != "-"}
        assert formats.name_hints(name) == want, name


def test_shape_table():
    rows = tsv("detect_shapes.tsv")
    assert len(rows) == 9
    for w, h, proj, layout in rows:
        f, src = formats.detect(info(int(w), int(h)), "x.mkv")
        assert (f.proj, f.layout) == (proj, layout), (w, h)
        assert src["proj"] == src["layout"] == "frame shape"


def test_name_examples():
    assert formats.name_hints("/films/movie.HSBS.1080p.mkv") == {"layout": "sbs-lr", "packing": "half"}
    assert formats.name_hints("Routes.1800.mkv") == {}
    assert formats.name_hints("ou.est.tb.mkv") == {}
    assert "layout" not in formats.name_hints("lr.rl.mkv")
    assert formats.name_hints("A_OU.mkv") == {"layout": "tb-lr"}
    assert formats.name_hints("clip_3D.mp4") == {"layout": "sbs-lr"}
    assert formats.name_hints("x_180_LR.mp4") == {"proj": "hequirect", "layout": "sbs-lr"}
    assert formats.name_hints("3D.film.OU.mkv")["layout"] == "tb-lr"
    assert formats.name_hints("/home/SBS/movie.mkv") == {}


def test_parse_ffprobe_stereo():
    i = formats.parse_ffprobe(load("sbs_rl.json"))
    assert i.stereo == "side by side"
    assert i.stereo_inverted
    assert (i.width, i.height, i.codec, i.video_index) == (1920, 1080, "h264", 0)
    f, src = formats.detect(i, "plain.mkv")
    assert f.layout == "sbs-rl"
    assert src["layout"] == "metadata"
    assert f.proj == "flat"


def test_parse_ffprobe_fields():
    i = formats.parse_ffprobe(load("plain_10bit.json"))
    assert i.bit_depth == 10
    assert i.pix_fmt == "yuv420p10le"
    assert i.fps == (25, 1)
    assert not i.vfr
    assert i.stereo is None and i.spherical is None
    assert i.duration == pytest.approx(1.0, abs=0.1)
    assert [s["codec_type"] for s in i.streams] == ["video", "audio"]


def test_parse_ffprobe_spherical():
    data = load("plain_10bit.json")
    data["streams"][0]["side_data_list"] = [
        {"side_data_type": "Spherical Mapping", "projection": "equirectangular"}]
    i = formats.parse_ffprobe(data)
    assert i.spherical == "equirectangular"
    f, src = formats.detect(i, "x.mkv")
    assert (f.proj, src["proj"]) == ("hequirect", "metadata")
    assert src["layout"] == "frame shape"


def test_parse_ffprobe_vfr_and_sar():
    data = load("plain_10bit.json")
    v = data["streams"][0]
    v["r_frame_rate"], v["avg_frame_rate"] = "60/1", "5986/100"
    v["sample_aspect_ratio"] = "16:15"
    i = formats.parse_ffprobe(data)
    assert i.vfr and i.fps == (5986, 100) and i.sar == (16, 15)
    v["sample_aspect_ratio"] = "0:1"
    assert formats.parse_ffprobe(data).sar == (1, 1)


def test_probe_skips_attached_pic():
    data = load("coverart_first.json")
    assert data["streams"][0]["disposition"]["attached_pic"] == 1
    i = formats.parse_ffprobe(data)
    assert i.video_index == 1
    assert (i.width, i.height) == (1280, 720)
    assert i.codec == "h264"


def test_parse_ffprobe_no_video():
    data = load("plain_10bit.json")
    data["streams"] = [s for s in data["streams"] if s["codec_type"] != "video"]
    with pytest.raises(ToolError) as e:
        formats.parse_ffprobe(data)
    assert e.value.code == 2


class FakeRunner:
    def __init__(self, out="", fail=None):
        self.out, self.fail, self.argv = out, fail, None

    def run(self, argv, **kw):
        self.argv = argv
        if self.fail:
            raise self.fail
        return subprocess.CompletedProcess(argv, 0, self.out, "")


def test_probe_runs_ffprobe_with_safe_argv():
    r = FakeRunner(json.dumps(load("sbs_rl.json")))
    i = formats.probe(r, Path("-odd name.mkv"))
    assert i.stereo_inverted
    assert r.argv[-2:] == ["-i", "./-odd name.mkv"]
    assert "json" in r.argv and "-show_format" in r.argv


def test_probe_errors_are_tool_errors():
    from xrdome_tools.run import ChildFailed
    for r in (FakeRunner("not json"), FakeRunner(fail=FileNotFoundError()),
              FakeRunner(fail=ChildFailed(["ffprobe"], 1, ["x: Invalid data"]))):
        with pytest.raises(ToolError) as e:
            formats.probe(r, Path("a.mkv"))
        assert e.value.code == 2


def test_detect_owner_vr180():
    for w, h in ((2880, 1440), (4320, 2160)):
        f, src = formats.detect(info(w, h), "x.mkv")
        assert (f.proj, f.layout) == ("hequirect", "sbs-lr")
        assert src == {"proj": "frame shape", "layout": "frame shape"}


def test_detect_precedence():
    i = info(2880, 1440, stereo="top and bottom", spherical="equirectangular")
    f, src = formats.detect(i, "x.FSBS.flat.mkv")
    assert (f.proj, f.layout) == ("hequirect", "tb-lr")
    assert (src["proj"], src["layout"]) == ("metadata", "metadata")
    # name beats shape; packing from the name
    f, src = formats.detect(info(2880, 1440), "x.FSBS.flat.mkv")
    assert (f.proj, f.layout, f.packing) == ("hequirect", "sbs-lr", None)
    f, src = formats.detect(info(1920, 1080), "x.FSBS.mkv")
    assert f == Format("flat", "sbs-lr", "full")
    assert src == {"proj": "frame shape", "layout": "file name", "packing": "file name"}
    # an explicit choice beats everything
    f, src = formats.detect(i, "x.mkv", "htb")
    assert f == AS_CHOICES["htb"]
    assert set(src.values()) == {"flag"}
    with pytest.raises(ToolError):
        formats.detect(i, "x.mkv", "nonsense")


def test_detect_packing_from_shape_and_sar():
    f, src = formats.detect(info(3840, 1080), "x.mkv")
    assert f == Format("flat", "sbs-lr", "full") and src["packing"] == "frame shape"
    f, _ = formats.detect(info(1920, 1080), "x.sbs.mkv")
    assert f.packing == "half"
    # SAR counts without stereo metadata, and is ignored with it
    assert formats.detect(info(1920, 1080, sar=(1, 2)), "x.mkv")[0].layout == "tb-lr"
    assert formats.detect(info(1920, 1080, sar=(1, 2), stereo="side by side"), "x.mkv")[0].layout == "sbs-lr"


def test_detect_no_size_falls_back():
    f, src = formats.detect(info(0, 0), "x.mkv")
    assert (f.proj, f.layout) == ("hequirect", "sbs-lr")
    assert src["proj"] == "fallback"


def test_output_tags():
    assert formats.output_tags(AS_CHOICES["fsbs"]) == {"stereo_mode": "left_right"}
    assert formats.output_tags(AS_CHOICES["fsbs-rl"]) == {"stereo_mode": "right_left"}
    assert formats.output_tags(AS_CHOICES["ftb"]) == {"stereo_mode": "top_bottom"}
    assert formats.output_tags(AS_CHOICES["ftb-rl"]) == {"stereo_mode": "bottom_top"}
    assert formats.output_tags(AS_CHOICES["flat"]) == {"stereo_mode": "mono"}
    assert formats.output_tags(AS_CHOICES["vr180"]) == {"stereo_mode": "left_right"}


def test_output_name():
    n = formats.output_name
    assert n("clip", AS_CHOICES["fsbs"]) == "clip_FSBS.mkv"
    assert n("clip_FSBS", AS_CHOICES["fsbs"]) == "clip_FSBS.mkv"
    assert n("x", AS_CHOICES["vr180"]) == "x_VR180_LR.mkv"
    assert n("x_VR180_LR", AS_CHOICES["vr180"]) == "x_VR180_LR.mkv"
    assert n("x", AS_CHOICES["ftb"]) == "x_TB.mkv"
    assert n("x", AS_CHOICES["htb"]) == "x_HTB.mkv"
    assert n("x", AS_CHOICES["hsbs"]) == "x_HSBS.mkv"
    assert n("x", AS_CHOICES["flat"]) == "x.mkv"
    assert n("x", AS_CHOICES["fsbs-rl"]) == "x_RL_FSBS.mkv"
    assert n("x", AS_CHOICES["vr180-rl"]) == "x_VR180_RL.mkv"
    assert n("x", AS_CHOICES["ftb-rl"]) == "x_TB_RL.mkv"
    assert n("x", AS_CHOICES["fsbs"], ["to3d", "FSBS"]) == "x_FSBS_to3d.mkv"
    assert n("x", Format("flat", "sbs-lr", None)) == "x_SBS.mkv"


@pytest.mark.parametrize("stem", ["-a", "a b", "é", "it's \"q\"", "日本語", "a.b.c", "180p"])
@pytest.mark.parametrize("choice", ["vr180", "hsbs", "fsbs", "htb", "fsbs-rl", "vr180-rl"])
def test_output_name_odd_inputs(stem, choice):
    f = AS_CHOICES[choice]
    name = formats.output_name(stem, f)
    assert name.startswith(stem) and name.endswith(".mkv")
    hints = formats.name_hints(name)
    # "180p" is not the 180 token; "a.b.c" has no hint tokens.
    assert hints.get("layout") == f.layout
    if f.proj == "hequirect":
        assert hints.get("proj") == "hequirect"
    if f.proj == "flat" and f.layout.startswith("sbs"):
        assert hints.get("packing") == f.packing
    # naming an already-named output again changes nothing
    assert formats.output_name(name[:-4], f) == name


def test_output_target_defaults(tmp_path):
    src = tmp_path / "in.mp4"
    src.write_bytes(b"x")
    assert formats.output_target(src, "in_FSBS.mkv", None, None, False) == tmp_path / "in_FSBS.mkv"
    d = tmp_path / "d"
    d.mkdir()
    assert formats.output_target(src, "n.mkv", None, d, False) == d / "n.mkv"
    assert formats.output_target(src, "n.mkv", tmp_path / "o.mkv", d, False) == tmp_path / "o.mkv"


def test_output_target_refuses_input(tmp_path):
    src = tmp_path / "in.mkv"
    src.write_bytes(b"x")
    link = tmp_path / "link.mkv"
    link.symlink_to(src)
    hard = tmp_path / "hard.mkv"
    os.link(src, hard)
    for force in (False, True):
        for target in (src, link, hard):
            with pytest.raises(ToolError) as e:
                formats.output_target(src, "n.mkv", target, None, force)
            assert e.value.code == 2
    # same name through the default directory
    with pytest.raises(ToolError) as e:
        formats.output_target(src, "in.mkv", None, None, True)
    assert e.value.code == 2

    out = tmp_path / "out.mkv"
    out.write_bytes(b"y")
    with pytest.raises(ToolError) as e:
        formats.output_target(src, "n.mkv", out, None, False)
    assert e.value.code == 1
    assert formats.output_target(src, "n.mkv", out, None, True) == out

    with pytest.raises(ToolError) as e:
        formats.output_target(src, "n.mkv", None, tmp_path / "missing", False)
    assert e.value.code == 2


@pytest.mark.skipif(os.geteuid() == 0, reason="root can write anywhere")
def test_output_target_unwritable_outdir(tmp_path):
    src = tmp_path / "in.mkv"
    src.write_bytes(b"x")
    ro = tmp_path / "ro"
    ro.mkdir()
    ro.chmod(0o500)
    try:
        with pytest.raises(ToolError) as e:
            formats.output_target(src, "n.mkv", None, ro, False)
        assert e.value.code == 2
    finally:
        ro.chmod(0o700)


def test_is_stereo():
    assert formats.is_stereo(AS_CHOICES["fsbs"])
    assert formats.is_stereo(AS_CHOICES["vr180"])
    assert not formats.is_stereo(AS_CHOICES["flat"])
