"""Video formats: ffprobe parsing, xrdome's detection rules, output names and tags.

The detection rules (`name_hints`, the shape rules in `detect`) are a port of
src/detect/detect.c. Their test cases live in tests/data/detect_names.tsv and
tests/data/detect_shapes.tsv, read by both tests/test_detect.c and
tools/tests/test_formats.py, so the two copies cannot drift apart silently.
"""

import json
import os
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path

from . import config
from .errors import EXIT_EXISTS, EXIT_SETUP, ToolError
from .run import ChildFailed, Runner, safe_arg


@dataclass(frozen=True)
class Format:
    proj: str  # "hequirect" | "flat"
    layout: str  # "mono" | "sbs-lr" | "sbs-rl" | "tb-lr" | "tb-rl"
    packing: str | None  # "full" | "half" | None


def _choices() -> dict[str, Format]:
    c = {
        "vr180": Format("hequirect", "sbs-lr", None),
        "flat": Format("flat", "mono", None),
        "hsbs": Format("flat", "sbs-lr", "half"),
        "fsbs": Format("flat", "sbs-lr", "full"),
        "htb": Format("flat", "tb-lr", "half"),
        "ftb": Format("flat", "tb-lr", "full"),
    }
    for name in ("vr180", "hsbs", "fsbs", "htb", "ftb"):
        f = c[name]
        c[name + "-rl"] = Format(f.proj, f.layout.replace("-lr", "-rl"), f.packing)
    return c


AS_CHOICES: dict[str, Format] = _choices()


def is_stereo(f: Format) -> bool:
    return f.layout != "mono"


# ---- ffprobe ---------------------------------------------------------------


@dataclass(frozen=True)
class ProbeInfo:
    width: int
    height: int
    sar: tuple[int, int]
    codec: str
    pix_fmt: str
    bit_depth: int
    duration: float
    fps: tuple[int, int]
    vfr: bool
    stereo: str | None  # the Stereo 3D "type" string, e.g. "side by side"
    stereo_inverted: bool
    spherical: str | None  # the Spherical Mapping "projection" string
    video_index: int  # ffprobe's stream index of the video stream used
    streams: list[dict]  # every stream, as ffprobe printed it


def _ratio(text, default: tuple[int, int]) -> tuple[int, int]:
    m = re.fullmatch(r"\s*(\d+)\s*[:/]\s*(\d+)\s*", str(text or ""))
    if not m or int(m[1]) <= 0 or int(m[2]) <= 0:
        return default
    return int(m[1]), int(m[2])


def _float(v, default: float = 0.0) -> float:
    try:
        f = float(v)
    except (TypeError, ValueError):
        return default
    return f if f == f and f not in (float("inf"), float("-inf")) else default


def _bit_depth(stream: dict) -> int:
    try:
        n = int(stream.get("bits_per_raw_sample"))
        if n > 0:
            return n
    except (TypeError, ValueError):
        pass
    m = re.search(r"(?:p|le|be)(\d\d)(?:le|be)?$", str(stream.get("pix_fmt", "")))
    return int(m[1]) if m and int(m[1]) > 8 else 8


def parse_ffprobe(data: dict) -> ProbeInfo:
    """Read `ffprobe -show_streams -show_format -of json` output.

    The video stream used is the first one that is not an attached picture, as
    xrdome's own probe does (cover art is often listed first).
    """
    streams = [s for s in data.get("streams", []) if isinstance(s, dict)]
    video = None
    for s in streams:
        attached = (s.get("disposition") or {}).get("attached_pic") == 1
        if s.get("codec_type") == "video" and not attached:
            video = s
            break
    if video is None:
        raise ToolError("no video stream found", EXIT_SETUP)

    stereo = None
    inverted = False
    spherical = None
    for sd in video.get("side_data_list") or []:
        kind = sd.get("side_data_type")
        if kind == "Stereo 3D" and stereo is None:
            stereo = str(sd.get("type", "")) or None
            inverted = sd.get("inverted") in (1, "1", True)
        elif kind == "Spherical Mapping" and spherical is None:
            spherical = str(sd.get("projection", "")) or "unknown"

    avg = _ratio(video.get("avg_frame_rate"), (0, 1))
    real = _ratio(video.get("r_frame_rate"), (0, 1))
    fps = avg if avg[0] else real
    vfr = bool(avg[0] and real[0] and abs(avg[0] / avg[1] / (real[0] / real[1]) - 1) > 0.001)

    fmt = data.get("format") or {}
    duration = _float(fmt.get("duration")) or _float(video.get("duration"))

    return ProbeInfo(
        width=int(video.get("width") or 0),
        height=int(video.get("height") or 0),
        sar=_ratio(video.get("sample_aspect_ratio"), (1, 1)),
        codec=str(video.get("codec_name", "")),
        pix_fmt=str(video.get("pix_fmt", "")),
        bit_depth=_bit_depth(video),
        duration=duration,
        fps=fps,
        vfr=vfr,
        stereo=stereo,
        stereo_inverted=inverted,
        spherical=spherical,
        video_index=int(video.get("index", 0)),
        streams=streams,
    )


def probe(runner: Runner, path: Path) -> ProbeInfo:
    argv = [config.program("ffprobe"), "-v", "error", "-show_streams", "-show_format",
            "-of", "json", "-i", safe_arg(path)]
    try:
        cp = runner.run(argv, capture=True, timeout=60)
    except FileNotFoundError:
        raise ToolError("ffprobe not found; install FFmpeg (it ships ffprobe)", EXIT_SETUP)
    except subprocess.TimeoutExpired:
        raise ToolError(f"{path}: ffprobe did not finish in 60 s", EXIT_SETUP)
    except ChildFailed as e:
        why = e.stderr_tail[-1] if e.stderr_tail else f"status {e.returncode}"
        raise ToolError(f"{path}: ffprobe failed: {why}", EXIT_SETUP)
    try:
        data = json.loads(cp.stdout)
    except ValueError:
        raise ToolError(f"{path}: ffprobe printed no usable output", EXIT_SETUP)
    try:
        return parse_ffprobe(data)
    except ToolError as e:
        raise ToolError(f"{path}: {e}", e.code)


# ---- file name hints (port of xrd_detect_name_hints) -------------------------


def name_hints(basename: str) -> dict[str, str]:
    """Hints from the tokens of a file name: keys among proj, layout, packing.

    Tokens are runs of ASCII letters and digits. Matching is case-insensitive
    except for LR, RL, TB and OU, which must be upper case. The first token that
    gives a key wins; a bare `3D` means sbs-lr only if no other token gave a layout.
    """
    basename = basename.rsplit("/", 1)[-1]
    out: dict[str, str] = {}
    three_d = False
    for t in re.findall(r"[A-Za-z0-9]+", basename):
        u = t.upper()
        if "proj" not in out and u in ("VR180", "180", "180X180", "VR"):
            out["proj"] = "hequirect"
        sbs = u in ("SBS", "3DH") or t == "LR"
        hsbs = u == "HSBS"
        fsbs = u in ("FSBS", "FULLSBS")
        rl = t == "RL"
        tb = t in ("TB", "OU") or u == "3DV"
        htb = u in ("HTB", "HOU")
        mono = u in ("MONO", "2D")
        if u == "3D":
            three_d = True
        if "layout" not in out and (sbs or hsbs or fsbs or rl or tb or htb or mono):
            out["layout"] = ("sbs-lr" if sbs or hsbs or fsbs else
                             "sbs-rl" if rl else
                             "tb-lr" if tb or htb else "mono")
        if "packing" not in out and (hsbs or fsbs or htb):
            out["packing"] = "half" if hsbs or htb else "full"
    if "layout" not in out and three_d:
        out["layout"] = "sbs-lr"
    return out


# ---- shape rules (port of shape_layout, shape_proj in detect.c) ---------------


def _within(f: float, x: float, frac: float) -> bool:
    return abs(f / x - 1.0) <= frac + 1e-9


def _shape_layout(f: float, proj: str) -> str:
    if proj == "hequirect":
        if f > 0 and _within(f, 1.0, 0.02):
            return "mono"
        if f > 0 and _within(f, 0.5, 0.02):
            return "tb-lr"
        return "sbs-lr"
    if f >= 3.0:
        return "sbs-lr"
    if f > 0 and _within(f, 0.889, 0.03):
        return "tb-lr"
    return "mono"


def _shape_proj(f: float, layout: str) -> str:
    x = 2.0 if layout.startswith("sbs") else 0.5 if layout.startswith("tb") else 1.0
    return "hequirect" if _within(f, x, 0.02) else "flat"


def _metadata_hints(info: ProbeInfo) -> dict[str, str]:
    out: dict[str, str] = {}
    if info.spherical is not None:
        out["proj"] = "hequirect"
    if info.stereo == "side by side":
        out["layout"] = "sbs-rl" if info.stereo_inverted else "sbs-lr"
    elif info.stereo == "top and bottom":
        out["layout"] = "tb-rl" if info.stereo_inverted else "tb-lr"
    elif info.stereo == "2D":
        out["layout"] = "mono"
    return out


def detect(info: ProbeInfo, basename: str, as_: str = "auto") -> tuple[Format, dict[str, str]]:
    """The format xrdome would play `info` as, and where each field came from.

    Order: `as_` (an explicit choice, source "flag"), container metadata, file
    name tokens, frame shape. The second value maps proj, layout and (for flat
    stereo) packing to "flag", "metadata", "file name", "frame shape" or "fallback".
    """
    if as_ != "auto":
        f = AS_CHOICES.get(as_)
        if f is None:
            raise ToolError(f"unknown --as value {as_!r}; use auto or one of "
                            f"{', '.join(AS_CHOICES)}", EXIT_SETUP)
        src = {"proj": "flag", "layout": "flag"}
        if f.packing:
            src["packing"] = "flag"
        return f, src

    sources: dict[str, str] = {}
    got: dict[str, str] = {}
    for label, hints in (("metadata", _metadata_hints(info)), ("file name", name_hints(basename))):
        for key, value in hints.items():
            if key not in got:
                got[key] = value
                sources[key] = label

    # Pixel aspect is taken as 1:1 when the container carries stereo metadata.
    f = 0.0
    if info.width > 0 and info.height > 0:
        sn, sd = info.sar if info.stereo is None else (1, 1)
        f = info.width * sn / (info.height * sd)

    proj, layout = got.get("proj"), got.get("layout")
    if proj is None or layout is None:
        if f > 0:
            if proj is None and layout is None:
                if _within(f, 2.0, 0.02):
                    proj, layout = "hequirect", "sbs-lr"
                else:
                    proj, layout = "flat", _shape_layout(f, "flat")
            elif proj is None:
                proj = _shape_proj(f, layout)
            else:
                layout = _shape_layout(f, proj)
            label = "frame shape"
        else:
            # No frame size: what xrdome did before it detected anything.
            if proj is None:
                proj = "hequirect"
            if layout is None:
                layout = "mono" if proj == "flat" else "sbs-lr"
            label = "fallback"
        if "proj" not in sources:
            sources["proj"] = label
        if "layout" not in sources:
            sources["layout"] = label

    packing = None
    if proj == "flat" and layout != "mono":
        packing = got.get("packing")
        if packing is not None:
            sources["packing"] = "file name"
        else:
            if layout.startswith("sbs"):
                full = f >= 3.0
            else:
                full = 0 < f < 1.5
            packing = "full" if full else "half"
            sources["packing"] = "frame shape"
    return Format(proj, layout, packing), sources


# ---- output tags and names -----------------------------------------------------

_STEREO_MODE = {
    "mono": "mono",
    "sbs-lr": "left_right",
    "sbs-rl": "right_left",
    "tb-lr": "top_bottom",
    "tb-rl": "bottom_top",
}


def output_tags(f: Format) -> dict[str, str]:
    """Matroska tags for the output video stream."""
    return {"stereo_mode": _STEREO_MODE[f.layout]}


def _tokens(stem: str) -> list[str]:
    return re.findall(r"[A-Za-z0-9]+", stem)


def output_name(stem: str, f: Format, extra: list[str] = []) -> str:
    """`stem` plus the tokens xrdome reads the format from, plus extra, plus .mkv.

    A token the name already carries is not added again. For right-eye-first
    side by side the RL token goes before the packing token, because the first
    layout token wins in name_hints (`x_FSBS_RL` would read back as left-first).
    Top-bottom has no right-first token, so `_RL` is only a label there and the
    Stereo 3D tag carries the order. Full top-bottom uses `_TB`, half `_HTB`.
    """
    have = name_hints(stem)
    rl = f.layout.endswith("-rl")
    sbs = f.layout.startswith("sbs")
    tokens: list[str] = []
    if f.proj == "hequirect":
        if have.get("proj") != "hequirect":
            tokens.append("VR180")
        if f.layout != "mono" and have.get("layout") != f.layout:
            if sbs:
                tokens.append("RL" if rl else "LR")
            else:
                tokens.append("TB")
    elif f.layout != "mono":
        if sbs:
            word = {"full": "FSBS", "half": "HSBS"}.get(f.packing or "", "SBS")
            ok = have.get("layout") == f.layout and (f.packing is None or have.get("packing") == f.packing)
            if not ok:
                tokens += (["RL"] if rl else []) + [word]
        else:
            word = "HTB" if f.packing == "half" else "TB"
            ok = have.get("layout") == "tb-lr" and (f.packing != "half" or have.get("packing") == "half")
            if not ok:
                tokens.append(word)
    if rl and not sbs and "RL" not in _tokens(stem) + tokens:
        tokens.append("RL")
    present = {t.upper() for t in _tokens(stem)} | {t.upper() for t in tokens}
    for e in extra:
        if e.upper() not in present:
            tokens.append(e)
            present.add(e.upper())
    return stem + "".join("_" + t for t in tokens) + ".mkv"


def output_target(input: Path, name: str, out: Path | None, outdir: Path | None,
                  force: bool) -> Path:
    """Where to write, refusing before any work starts.

    Exit 2: the target is the input (also through a symlink or hard link, even
    with force), is a directory, or its directory is missing or not writable.
    Exit 1: the target exists and force is not set.
    """
    if out is not None:
        target = out
    elif outdir is not None:
        target = outdir / name
    else:
        target = input.parent / name

    def same(a: Path, b: Path) -> bool:
        try:
            return os.path.samefile(a, b)
        except OSError:
            return os.path.realpath(a) == os.path.realpath(b)

    if same(target, input):
        raise ToolError(f"{target}: that is the input file; refusing to overwrite it", EXIT_SETUP)
    if target.is_dir():
        raise ToolError(f"{target}: is a directory", EXIT_SETUP)
    parent = target.parent if str(target.parent) else Path(".")
    if not parent.is_dir():
        raise ToolError(f"{parent}: no such directory", EXIT_SETUP)
    if not os.access(parent, os.W_OK | os.X_OK):
        raise ToolError(f"{parent}: not writable", EXIT_SETUP)
    if target.exists() or target.is_symlink():
        if not force:
            raise ToolError(f"{target}: exists; use --force to replace it", EXIT_EXISTS)
        if target.exists() and not os.access(target, os.W_OK):
            raise ToolError(f"{target}: not writable", EXIT_SETUP)
    return target
