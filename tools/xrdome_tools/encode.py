"""ffmpeg arguments for `transcode`: video encode per encoder family, streams, HDR.

Pure functions that build argument lists, plus two small test encodes for the
VA-API options an ffmpeg build or driver may not accept (QVBR, HDR tags).
"""

import subprocess

from . import config
from .formats import Format, ProbeInfo
from .hw import DEFAULT_RENDER_NODE, EncoderChoice
from .run import ChildFailed, Runner

# Quality presets: {"high"|"normal"|"small": {family: [ffmpeg args]}}.
# "vaapi" is tried first; "vaapi-cqp" is the fallback where QVBR is missing.
# "software" is x265 (HEVC), "svtav1" is SVT-AV1.
QUALITY: dict[str, dict[str, list[str]]] = {
    "high": {
        "nvenc": ["-rc", "vbr", "-cq", "20", "-preset", "p5"],  # provisional, Task 8 settles
        "vaapi": ["-rc_mode", "QVBR", "-global_quality", "20"],  # provisional, Task 8 settles
        "vaapi-cqp": ["-rc_mode", "CQP", "-qp", "20"],  # provisional, Task 8 settles
        "software": ["-crf", "18", "-preset", "medium"],  # provisional, Task 8 settles
        "svtav1": ["-crf", "24", "-preset", "6"],  # provisional, Task 8 settles
    },
    "normal": {
        "nvenc": ["-rc", "vbr", "-cq", "24", "-preset", "p5"],  # provisional, Task 8 settles
        "vaapi": ["-rc_mode", "QVBR", "-global_quality", "24"],  # provisional, Task 8 settles
        "vaapi-cqp": ["-rc_mode", "CQP", "-qp", "24"],  # provisional, Task 8 settles
        "software": ["-crf", "22", "-preset", "medium"],  # provisional, Task 8 settles
        "svtav1": ["-crf", "30", "-preset", "6"],  # provisional, Task 8 settles
    },
    "small": {
        "nvenc": ["-rc", "vbr", "-cq", "28", "-preset", "p5"],  # provisional, Task 8 settles
        "vaapi": ["-rc_mode", "QVBR", "-global_quality", "28"],  # provisional, Task 8 settles
        "vaapi-cqp": ["-rc_mode", "CQP", "-qp", "28"],  # provisional, Task 8 settles
        "software": ["-crf", "26", "-preset", "medium"],  # provisional, Task 8 settles
        "svtav1": ["-crf", "36", "-preset", "6"],  # provisional, Task 8 settles
    },
}

HDR_TRANSFERS = {"smpte2084", "arib-std-b67"}
TEST_TIMEOUT = 20.0

# What Matroska holds. Audio outside this set is re-encoded to Opus.
_MKV_AUDIO = {"aac", "ac3", "eac3", "dts", "truehd", "mlp", "flac", "opus", "vorbis", "mp3", "mp2",
              "alac", "wavpack", "tta", "pcm_s16le", "pcm_s16be", "pcm_s24le", "pcm_s24be",
              "pcm_s32le", "pcm_f32le", "pcm_u8"}
_MKV_SUBS = {"subrip", "srt", "ass", "ssa", "webvtt", "hdmv_pgs_subtitle", "dvd_subtitle",
             "dvb_subtitle"}


def hdr_args(video: dict) -> list[str]:
    """Colour tags that mark HDR, copied from the source video stream, or []."""
    trc = video.get("color_transfer")
    if trc not in HDR_TRANSFERS:
        return []
    return ["-color_primaries", video.get("color_primaries") or "bt2020",
            "-color_trc", trc,
            "-colorspace", video.get("color_space") or "bt2020nc"]


def input_args(choice: EncoderChoice, vaapi_device: str | None) -> list[str]:
    """Arguments that go before `-i`."""
    if choice.family == "vaapi":
        return ["-vaapi_device", vaapi_device or DEFAULT_RENDER_NODE]
    return []


def scale_filter(max_width: int | None, f: Format, size: tuple[int, int] | None) -> str | None:
    """`scale=...` for --max-width, or None when the picture is already narrow enough.

    Side-by-side widths are kept a multiple of 4 so each eye gets a whole, even width.
    A half side-by-side picture keeps its height (so the shape each eye is squeezed
    to stays the same for both); everything else keeps the aspect ratio.
    """
    if max_width is None:
        return None
    if size is not None and size[0] <= max_width:
        return None
    w = max_width - max_width % (4 if f.layout.startswith("sbs") else 2)
    if w <= 0:
        w = max_width
    if f.packing == "half" and f.layout.startswith("sbs") and size is not None:
        return f"scale={w}:{size[1]}"
    return f"scale={w}:-2"


def video_args(choice: EncoderChoice, quality: str, ten_bit: bool, max_width: int | None,
               f: Format, vaapi_device: str | None, *, size: tuple[int, int] | None = None,
               qvbr: bool = True, hdr: list[str] = []) -> list[str]:
    """`-c:v ...` with quality, bit depth, scaling and HDR tags (output-side arguments).

    size is the source width and height (needed to keep half side-by-side shape);
    qvbr False selects the CQP fallback for VA-API; hdr is hdr_args() output.
    """
    if quality not in QUALITY:
        raise ValueError(f"unknown quality {quality!r}")
    q = QUALITY[quality]
    scale = scale_filter(max_width, f, size)
    av1 = choice.name.endswith("av1") or choice.name == "libsvtav1"
    args: list[str] = []
    filters = [scale] if scale else []
    if choice.family == "vaapi":
        filters += ["format=p010,hwupload" if ten_bit else "format=nv12,hwupload"]
        args += ["-vf", ",".join(filters), "-c:v", choice.name]
        args += q["vaapi" if qvbr else "vaapi-cqp"]
    else:
        if filters:
            args += ["-vf", ",".join(filters)]
        args += ["-c:v", choice.name]
        if choice.family == "nvenc":
            args += ["-pix_fmt", "p010le" if ten_bit else "yuv420p"]
            if not av1:
                args += ["-profile:v", "main10" if ten_bit else "main"]
            args += q["nvenc"]
        else:
            # libx265 and SVT-AV1 take yuv420p10le; p010le makes ffmpeg warn and convert.
            args += ["-pix_fmt", "yuv420p10le" if ten_bit else "yuv420p"]
            args += q["svtav1" if av1 else "software"]
            if not av1:
                args += ["-x265-params", "log-level=error"]
    return args + list(hdr)


# ---- VA-API options that may not work -----------------------------------------

def _vaapi_test(runner: Runner, choice: EncoderChoice, device: str | None, ten_bit: bool,
                extra: list[str]) -> bool:
    fmt = "format=p010,hwupload" if ten_bit else "format=nv12,hwupload"
    argv = [config.program("ffmpeg"), "-hide_banner", "-loglevel", "error",
            "-f", "lavfi", "-i", "testsrc2=s=256x256:d=1",
            "-vaapi_device", device or DEFAULT_RENDER_NODE, "-vf", fmt,
            "-c:v", choice.name, *extra, "-f", "null", "-"]
    try:
        runner.run(argv, capture=True, timeout=TEST_TIMEOUT)
    except (ChildFailed, OSError, subprocess.TimeoutExpired):
        return False
    return True


def vaapi_qvbr_ok(runner: Runner, choice: EncoderChoice, device: str | None,
                  quality: str, ten_bit: bool) -> bool:
    """Does this driver take -rc_mode QVBR? (Not all do; the fallback is CQP.)"""
    return _vaapi_test(runner, choice, device, ten_bit, QUALITY[quality]["vaapi"])


def vaapi_hdr_ok(runner: Runner, choice: EncoderChoice, device: str | None,
                 ten_bit: bool, hdr: list[str]) -> bool:
    return _vaapi_test(runner, choice, device, ten_bit, list(hdr))


# ---- streams ---------------------------------------------------------------------

def plan_streams(info: ProbeInfo) -> list[tuple[dict, str, str | None]]:
    """(stream, action, new codec) for every stream except the video used.

    action is "copy", "convert" (to new codec) or "drop".
    """
    out: list[tuple[dict, str, str | None]] = []
    for s in info.streams:
        if s.get("index") == info.video_index:
            continue
        kind = s.get("codec_type")
        codec = str(s.get("codec_name", ""))
        if kind == "audio":
            out.append((s, "copy", None) if codec in _MKV_AUDIO else (s, "convert", "libopus"))
        elif kind == "subtitle":
            if codec == "mov_text":
                out.append((s, "convert", "srt"))
            elif codec in _MKV_SUBS:
                out.append((s, "copy", None))
            else:
                out.append((s, "drop", None))
        elif kind == "attachment":
            out.append((s, "copy", None))
        else:  # cover art, data, timecode: not wanted in the output
            out.append((s, "drop", None))
    return out


def _describe(s: dict) -> str:
    lang = (s.get("tags") or {}).get("language")
    kind = s.get("codec_type", "stream")
    return f"{kind} stream {s.get('index')} ({s.get('codec_name', 'unknown')}{', ' + lang if lang else ''})"


def stream_args(info: ProbeInfo) -> tuple[list[str], list[str]]:
    """(ffmpeg stream mapping and copy arguments, human lines for what is not copied).

    The video stream is mapped first and its codec arguments come from video_args
    or `-c:v copy`. Audio and subtitles are copied; Matroska-incompatible audio is
    re-encoded to Opus, mov_text subtitles become srt, anything else is dropped.
    """
    args = ["-map", f"0:{info.video_index}"]
    lines: list[str] = []
    counts = {"audio": 0, "subtitle": 0}
    overrides: list[str] = []
    for s, action, new in plan_streams(info):
        kind = s.get("codec_type")
        if action == "drop":
            lines.append(f"dropped {_describe(s)}: Matroska output cannot use it")
            continue
        args += ["-map", f"0:{s['index']}"]
        if kind in counts:
            if action == "convert":
                letter = "a" if kind == "audio" else "s"
                overrides += [f"-c:{letter}:{counts[kind]}", new]
                what = "re-encoded to Opus" if kind == "audio" else f"converted to {new}"
                lines.append(f"{_describe(s)} {what}: Matroska cannot hold {s.get('codec_name')}")
            counts[kind] += 1
    args += ["-c:a", "copy", "-c:s", "copy", "-c:t", "copy"] + overrides
    return args, lines
