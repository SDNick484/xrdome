from xrdome_tools import encode
from xrdome_tools.formats import AS_CHOICES, Format, ProbeInfo
from xrdome_tools.hw import EncoderChoice

SW = EncoderChoice("libx265", "software", "t")
NV = EncoderChoice("hevc_nvenc", "nvenc", "t")
VA = EncoderChoice("hevc_vaapi", "vaapi", "t")
VR = AS_CHOICES["vr180"]
FLAT = AS_CHOICES["flat"]


def pairs(args):
    return " ".join(args)


def test_ten_bit_follows_source():
    assert "-pix_fmt yuv420p10le" in pairs(encode.video_args(SW, "high", True, None, FLAT, None))
    assert "-pix_fmt yuv420p " in pairs(encode.video_args(SW, "high", False, None, FLAT, None)) + " "
    assert "format=p010,hwupload" in pairs(encode.video_args(VA, "high", True, None, FLAT, "/d"))
    assert "format=nv12,hwupload" in pairs(encode.video_args(VA, "high", False, None, FLAT, "/d"))
    ten = pairs(encode.video_args(NV, "high", True, None, FLAT, None))
    assert "-profile:v main10" in ten and "-pix_fmt p010le" in ten
    assert "-profile:v main " in pairs(encode.video_args(NV, "high", False, None, FLAT, None)) + " "


def test_max_width_keeps_eyes():
    a = encode.video_args(SW, "high", False, 3840, VR, None, size=(5760, 2880))
    assert a[a.index("-vf") + 1] == "scale=3840:-2"
    half = AS_CHOICES["hsbs"]
    a = encode.video_args(SW, "high", False, 1920, half, None, size=(3840, 1080))
    assert a[a.index("-vf") + 1] == "scale=1920:1080"
    a = encode.video_args(SW, "high", False, 1920, half, None, size=(1920, 1080))
    assert "-vf" not in a


def test_quality_values():
    assert encode.QUALITY["small"]["nvenc"][:4] == ["-rc", "vbr", "-cq", "28"]
    assert encode.QUALITY["high"]["software"][:2] == ["-crf", "18"]
    assert encode.QUALITY["normal"]["svtav1"][:2] == ["-crf", "30"]


def stream(i, kind, codec, **kw):
    return {"index": i, "codec_type": kind, "codec_name": codec, **kw}


def pinfo(streams):
    return ProbeInfo(1920, 1080, (1, 1), "h264", "yuv420p", 8, 10.0, (25, 1), False, None, False,
                     None, 0, streams)


def test_streams():
    args, lines = encode.stream_args(pinfo([stream(0, "video", "h264"), stream(1, "audio", "aac"),
                                            stream(2, "subtitle", "subrip")]))
    assert lines == []
    s = pairs(args)
    assert "-c:a copy -c:s copy" in s and "-map 0:1" in s and "-map 0:2" in s

    args, lines = encode.stream_args(pinfo([stream(0, "video", "h264"),
                                            stream(1, "subtitle", "mov_text")]))
    assert "-c:s:0 srt" in pairs(args)
    assert len(lines) == 1 and "mov_text" in lines[0]

    args, lines = encode.stream_args(pinfo([stream(0, "video", "h264"), stream(1, "data", "bin_data"),
                                            stream(2, "subtitle", "eia_608")]))
    assert "-map 0:1" not in pairs(args) and "-map 0:2" not in pairs(args)
    assert len(lines) == 2 and "bin_data" in lines[0] and "eia_608" in lines[1]


def test_unholdable_audio_goes_to_opus():
    args, lines = encode.stream_args(pinfo([stream(0, "video", "h264"), stream(1, "audio", "pcm_dvd")]))
    assert "-c:a:0 libopus" in pairs(args) and "Opus" in lines[0]


def test_hdr_args():
    v = {"color_transfer": "smpte2084", "color_primaries": "bt2020", "color_space": "bt2020nc"}
    assert pairs(encode.hdr_args(v)) == "-color_primaries bt2020 -color_trc smpte2084 -colorspace bt2020nc"
    assert encode.hdr_args({"color_transfer": "bt709"}) == []
    a = encode.video_args(SW, "high", True, None, FLAT, None, hdr=encode.hdr_args(v))
    assert "-color_trc smpte2084" in pairs(a)


def test_vaapi_cqp_args():
    a = pairs(encode.video_args(VA, "normal", False, None, FLAT, "/d", qvbr=False))
    assert "-rc_mode CQP -qp 24" in a and "QVBR" not in a
    assert "-rc_mode QVBR -global_quality 24" in pairs(encode.video_args(VA, "normal", False, None, FLAT, "/d"))


def test_input_args():
    assert encode.input_args(VA, "/dev/dri/renderD129") == ["-vaapi_device", "/dev/dri/renderD129"]
    assert encode.input_args(NV, None) == []
