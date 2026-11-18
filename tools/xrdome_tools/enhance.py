"""`enhance`: frame interpolation (RIFE) and upscaling (Real-ESRGAN or Lanczos), 2D only.

The ncnn tools work on folders of PNG frames, so the video goes through them in chunks
(frames.py). Each chunk's result is encoded to a high-quality intermediate segment, the
segments are joined with the source's audio and subtitles, and the joined file goes
through `transcode` for the one final encode, the tags and the checks.
"""

import argparse
import os
import shutil
import subprocess
import tempfile
from fractions import Fraction
from pathlib import Path

from . import config, encode, formats, frames, models, transcode
from .errors import EXIT_SETUP, ToolError
from .run import ChildFailed, Runner, safe_arg, say_path

RIFE = "rife-ncnn-vulkan"
ESRGAN = "realesrgan-ncnn-vulkan"
RELEASES = {
    RIFE: "https://github.com/nihui/rife-ncnn-vulkan/releases",
    ESRGAN: "https://github.com/xinntao/Real-ESRGAN-ncnn-vulkan/releases",
}
RIFE_MODEL = ("rife-v4.6", "rife-v4.6.param")  # registry names: weights, then layout
ESRGAN_MODEL = "realesr-animevideov3"
ESRGAN_FILES = {2: ("realesr-animevideov3-x2", "realesr-animevideov3-x2.param")}
METHODS = ("lanczos", "model")
UPSCALES = (2, 3, 4)
# Intermediate segments: visually lossless and fast; transcode does the real encode.
INTERMEDIATE = ["-c:v", "libx264", "-preset", "veryfast", "-crf", "12"]

# ffprobe color_space -> (swscale matrix name, tag to write back)
_MATRIX = {"bt709": "bt709", "smpte170m": "bt601", "bt470bg": "bt601", "bt2020nc": "bt2020",
           "bt2020c": "bt2020", "smpte240m": "smpte240m", "fcc": "fcc"}


# ---- argv builders ----------------------------------------------------------------

def rife_argv(model_dir: Path, src: Path, dst: Path, target_count: int) -> list[str]:
    return [config.program(RIFE), "-m", safe_arg(model_dir), "-i", safe_arg(src),
            "-o", safe_arg(dst), "-n", str(target_count)]


def esrgan_argv(model: str, model_dir: Path, src: Path, dst: Path, scale: int) -> list[str]:
    return [config.program(ESRGAN), "-i", safe_arg(src), "-o", safe_arg(dst), "-n", model,
            "-s", str(scale), "-m", safe_arg(model_dir), "-f", "png"]


def lanczos_filter(has_libplacebo: bool, w: int, h: int) -> str:
    if has_libplacebo:
        return f"libplacebo=w={w}:h={h}:upscaler=ewa_lanczos"
    return f"scale={w}:{h}:flags=lanczos"


def parse_ffmpeg_filters(text: str) -> set[str]:
    """Filter names from `ffmpeg -hide_banner -filters`."""
    out: set[str] = set()
    for ln in text.splitlines():
        parts = ln.split()
        if len(parts) >= 3 and "->" in parts[2]:
            out.add(parts[1])
    return out


def libplacebo_works(runner: Runner) -> tuple[bool, str]:
    """(usable, reason). Being listed is not enough: it needs a Vulkan device at run time."""
    ff = config.program("ffmpeg")
    try:
        out = runner.run([ff, "-hide_banner", "-filters"], capture=True, timeout=20).stdout
    except (ChildFailed, OSError, subprocess.TimeoutExpired):
        out = ""
    if "libplacebo" not in parse_ffmpeg_filters(out):
        return False, "this ffmpeg has no libplacebo filter"
    test = [ff, "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=s=64x64:d=0.1",
            "-vf", lanczos_filter(True, 128, 128), "-f", "null", "-"]
    try:
        runner.run(test, capture=True, timeout=20)
    except ChildFailed as e:
        lines = [ln.strip() for ln in e.stderr_tail if ln.strip()]
        return False, "libplacebo is built in but its test run failed" + (f" ({lines[-1]})" if lines else "")
    except (OSError, subprocess.TimeoutExpired) as e:
        return False, f"libplacebo is built in but its test run failed ({e})"
    return True, "libplacebo test run passed"


def choose_lanczos(runner: Runner, w: int, h: int) -> str:
    ok, why = libplacebo_works(runner)
    if ok:
        say_path("upscale", "libplacebo", f"ewa_lanczos on Vulkan; {why}")
    else:
        say_path("upscale", "lanczos", f"{why}; scale=flags=lanczos on the CPU")
    return lanczos_filter(ok, w, h)


# ---- helpers ----------------------------------------------------------------------

def _rate_arg(r: Fraction) -> str:
    return str(r.numerator) if r.denominator == 1 else f"{r.numerator}/{r.denominator}"


def _rate_text(r: Fraction) -> str:
    return str(r.numerator) if r.denominator == 1 else f"{float(r):.3f}".rstrip("0").rstrip(".")


def _gb(n: float) -> str:
    return f"{n / 1e9:.1f} GB"


def parse_rate(text: str) -> Fraction:
    try:
        r = Fraction(str(text).strip())
    except (ValueError, ZeroDivisionError):
        raise ToolError(f"--fps {text}: not a frame rate (use e.g. 60 or 60000/1001)", EXIT_SETUP)
    if r <= 0 or r > 1000:
        raise ToolError(f"--fps {text}: out of range", EXIT_SETUP)
    return r


def _source_rate(info: formats.ProbeInfo, video: dict) -> tuple[Fraction, list[str]]:
    """The constant rate frames are taken at, and extraction args that make it constant."""
    measured = Fraction(*info.fps) if info.fps[0] else Fraction(0)
    if info.vfr:
        num, den = formats._ratio(video.get("r_frame_rate"), (0, 1))
        nominal = Fraction(num, den) if num else measured
        if nominal:
            say_path("fps", _rate_text(nominal),
                     f"variable frame rate source ({float(measured):.2f} fps measured); "
                     "made constant at its nominal rate")
            return nominal, ["-fps_mode", "cfr", "-r", _rate_arg(nominal)]
    if not measured:
        raise ToolError("the source has no usable frame rate", EXIT_SETUP)
    return measured, []


def _matrix(video: dict, height: int) -> tuple[str, str]:
    """(swscale matrix, colorspace tag) for RGB <-> YUV, as players guess for untagged video."""
    cs = str(video.get("color_space", ""))
    if cs in _MATRIX:
        return _MATRIX[cs], cs
    return ("bt709", "bt709") if height >= 720 else ("bt601", "smpte170m")


def _need_program(name: str) -> None:
    if shutil.which(config.program(name)) is None:
        raise ToolError(f"{name} not found. Download a release from {RELEASES[name]}, put the "
                        f"program on PATH, or set `{name} = /path/to/{name}` under [programs] in "
                        f"{config.paths().config_file}", EXIT_SETUP)


def _model_dir(names: tuple[str, ...], p: config.Paths) -> Path:
    paths = [models.require(n, p) for n in names]
    return paths[0].parent


def _free_bytes(d: Path) -> int:
    probe = d
    while not probe.exists() and probe != probe.parent:
        probe = probe.parent
    return shutil.disk_usage(probe).free


def _concat_list(segs: list[Path]) -> str:
    def q(p: Path) -> str:
        return "'" + os.fspath(p).replace("'", "'\\''") + "'"
    return "".join(f"file {q(s)}\n" for s in segs)


def _src_streams(info: formats.ProbeInfo, input_index: int) -> tuple[list[str], list[str]]:
    """Mapping for the source's audio, subtitles and attachments as input `input_index`."""
    smap, lines = encode.stream_args(info)
    smap = smap[2:]  # the video map: the video comes from elsewhere
    smap = [f"{input_index}:{a[2:]}" if a.startswith("0:") else a for a in smap]
    return smap, lines


# ---- the job ----------------------------------------------------------------------

def enhance(runner: Runner, src: Path, dst: Path, *, fps: Fraction | None = None,
            upscale: int | None = None, method: str = "lanczos", chunk: float = 10.0,
            tmp: Path | None = None, treat_as_2d: bool = False, codec: str = "hevc",
            quality: str = "high", encoder: str | None = None) -> Path:
    """Write dst from src. dst must already be checked with formats.output_target."""
    if fps is None and upscale is None:
        raise ToolError("nothing to do: give --fps N, --upscale 2|3|4, or both", EXIT_SETUP)
    if upscale is not None and upscale not in UPSCALES:
        raise ToolError(f"--upscale {upscale}: use 2, 3 or 4", EXIT_SETUP)
    if method not in METHODS:
        raise ToolError(f"--method {method}: use lanczos or model", EXIT_SETUP)
    if chunk <= 0:
        raise ToolError("--chunk must be more than 0 seconds", EXIT_SETUP)
    transcode._require_programs()

    reader = Runner(log=runner.log) if runner.dry_run else runner
    log = None
    if runner.log is None and not runner.dry_run:
        log = open(f"{dst}.log", "w", encoding="utf-8")
        runner = Runner(log=log)
        reader = runner
    try:
        return _enhance(runner, reader, src, dst, fps, upscale, method, chunk, tmp,
                        treat_as_2d, codec, quality, encoder)
    finally:
        if log is not None:
            log.close()


def _enhance(runner, reader, src, dst, fps, upscale, method, chunk, tmp, treat_as_2d,
             codec, quality, encoder) -> Path:
    info = formats.probe(reader, src)
    f, sources = formats.detect(info, src.name, "auto")
    what = ", ".join(f"{k} from {v}" for k, v in sources.items())
    if formats.is_stereo(f) or f.proj != "flat":
        name = f"{f.proj}/{f.layout}" + (f"/{f.packing}" if f.packing else "")
        if not treat_as_2d:
            raise ToolError(f"{src}: detected as {name} ({what}); enhance works on 2D video only. "
                            "If that detection is wrong, add --treat-as-2d", EXIT_SETUP)
        say_path("format", "flat/mono", f"--treat-as-2d; detected as {name}")
    else:
        say_path("format", "flat/mono", what)
    if encoder is not None:
        transcode._choose(reader, codec, encoder)  # refuse a broken --encoder before any work

    video = next((s for s in info.streams if s.get("index") == info.video_index), {})
    src_rate, cfr = _source_rate(info, video)
    interp = fps is not None
    if interp and fps <= src_rate:
        raise ToolError(f"--fps {_rate_text(fps)} is not above the source's {_rate_text(src_rate)} fps; "
                        "enhance only raises the frame rate", EXIT_SETUP)
    model_up = upscale is not None and method == "model"
    if model_up and upscale not in ESRGAN_FILES:
        raise ToolError(f"--upscale {upscale} --method model: the only allowed model is "
                        f"{ESRGAN_MODEL} x2; use --upscale 2, or --method lanczos", EXIT_SETUP)

    # Programs, then models: both before any work.
    if interp:
        _need_program(RIFE)
    if model_up:
        _need_program(ESRGAN)
    p = config.paths()
    rife_dir = _model_dir(RIFE_MODEL, p) if interp else None
    esr_dir = _model_dir(ESRGAN_FILES[upscale], p) if model_up else None

    scale = upscale or 1
    w, h = info.width * scale, info.height * scale
    lanczos = choose_lanczos(reader, w, h) if upscale and method == "lanczos" else None
    if model_up:
        say_path("upscale", "realesrgan", f"{ESRGAN_MODEL} x{upscale} with {ESRGAN} on Vulkan")
    ratio = fps / src_rate if interp else Fraction(1)
    out_rate = fps if interp else src_rate

    tokens = output_tokens(fps, upscale)
    use_frames = interp or model_up
    base = Path(os.path.abspath(tmp or p.cache_dir))

    if use_frames:
        total = round(info.duration * src_rate)
        if total < 2:
            raise ToolError(f"{src}: too short to interpolate ({total} frame(s))", EXIT_SETUP)
        chunks = frames.plan_chunks(total, (src_rate.numerator, src_rate.denominator), chunk, 1)
        est = frames.disk_estimate(info.width, info.height, chunks[0][1],
                                   scale if model_up else 1, float(ratio))
        free = _free_bytes(base)
        if free < est:
            raise ToolError(f"not enough free space in {base} for the temporary frames: about "
                            f"{_gb(est)} per {chunk:g} s chunk, {_gb(free)} free. Use --tmp DIR "
                            "on a larger disk, or a shorter --chunk", EXIT_SETUP)
        say_path("tmp", os.fspath(base), f"about {_gb(est)} of PNG frames per {chunk:g} s chunk "
                 f"({len(chunks)} chunk(s)), {_gb(free)} free")
        if interp:
            say_path("interpolate", f"rife-v4.6@{_rate_text(fps)}",
                     f"from {_rate_text(src_rate)} fps with {RIFE}; scene cuts are not detected, "
                     "so the frame across a cut is ghosted")
        if info.bit_depth > 8:
            say_path("bit-depth", "8", "the frame tools read and write 8-bit PNG")

    if not runner.dry_run:
        base.mkdir(parents=True, exist_ok=True)
        job = Path(tempfile.mkdtemp(prefix="enhance-", dir=base))
    else:
        job = base / "enhance-XXXXXX"
    joined = job / "joined.mkv"
    try:
        if use_frames:
            _frames_path(runner, src, info, video, job, joined, chunks, total, ratio, out_rate,
                         cfr, rife_dir, esr_dir, upscale if model_up else None, lanczos)
        else:
            _filter_path(runner, src, info, video, joined, lanczos)
        if runner.dry_run:
            print(f"# then: transcode {joined} -> {dst} (codec {codec}, quality {quality})")
            return dst
        return transcode.transcode(runner, joined, dst, as_="flat", codec=codec, quality=quality,
                                   encoder=encoder, reencode=True, extra_tokens=tokens)
    finally:
        if not runner.dry_run:
            shutil.rmtree(job, ignore_errors=True)


def _color_tags(video: dict, tag: str) -> list[str]:
    out = ["-colorspace", tag]
    for key, opt in (("color_primaries", "-color_primaries"), ("color_transfer", "-color_trc")):
        v = video.get(key)
        if v and v != "unknown":
            out += [opt, str(v)]
    return out


def _frames_path(runner, src, info, video, job, joined, chunks, total, ratio, out_rate, cfr,
                 rife_dir, esr_dir, esr_scale, lanczos) -> None:
    ff = config.program("ffmpeg")
    matrix, tag = _matrix(video, info.height)
    rng = "pc" if video.get("color_range") == "pc" else "tv"
    src_rate = out_rate / ratio

    def extract(s: Path, start: int, count: int, d: Path) -> list[str]:
        seek = ["-ss", f"{float((start - Fraction(1, 2)) / src_rate):.6f}"] if start else []
        return [ff, "-nostdin", "-hide_banner", "-loglevel", "error", *seek, "-i", safe_arg(s),
                "-map", f"0:{info.video_index}", *cfr, "-frames:v", str(count),
                "-vf", f"scale=in_color_matrix={matrix}:in_range={rng},format=rgb24",
                frames.pattern(d)]

    targets = frames.chunk_targets(chunks, total, ratio)
    by_in = {c.dir_in: targets[c.index][0] for c in frames.make_chunks(chunks, job)}
    steps: list[frames.Step] = []
    if rife_dir is not None:
        steps.append(lambda i, o: rife_argv(rife_dir, i, o, by_in[i]))
    if esr_dir is not None:
        steps.append(lambda i, o: esrgan_argv(ESRGAN_MODEL, esr_dir, i, o, esr_scale))

    vf = ([lanczos] if lanczos else []) + [f"scale=out_color_matrix={matrix}:out_range=tv",
                                           "format=yuv420p"]
    segs: list[Path] = []
    gen = frames.run_chunks(runner, src, chunks, steps, job, extract=extract)
    try:
        for c, d in enumerate(gen):
            seg = job / f"{c:05d}.mkv"
            runner.run([ff, "-nostdin", "-hide_banner", "-loglevel", "error", "-y",
                        "-framerate", _rate_arg(out_rate), "-i", frames.pattern(d),
                        "-frames:v", str(targets[c][1]), "-vf", ",".join(vf), *INTERMEDIATE,
                        "-pix_fmt", "yuv420p", *_color_tags(video, tag), "-an", safe_arg(seg)])
            segs.append(seg)
    finally:
        gen.close()

    lst = job / "segments.txt"
    if not runner.dry_run:
        lst.write_text(_concat_list(segs), encoding="utf-8")
    smap, lines = _src_streams(info, 1)
    for ln in lines:
        say_path("stream", ln.split(":")[0], ln)
    runner.run([ff, "-nostdin", "-hide_banner", "-loglevel", "error", "-y",
                "-f", "concat", "-safe", "0", "-i", safe_arg(lst), "-i", safe_arg(src),
                "-map", "0:v:0", *smap, "-c:v", "copy", "-f", "matroska", safe_arg(joined)])
    if not runner.dry_run:
        for s in segs:
            s.unlink(missing_ok=True)


def _filter_path(runner, src, info, video, joined, lanczos) -> None:
    """Lanczos upscaling only: one ffmpeg run, no frame folders."""
    ten = info.bit_depth > 8
    smap, lines = encode.stream_args(info)
    for ln in lines:
        say_path("stream", ln.split(":")[0], ln)
    pix = "yuv420p10le" if ten else "yuv420p"
    runner.run([config.program("ffmpeg"), "-nostdin", "-hide_banner", "-loglevel", "error", "-y",
                "-i", safe_arg(src), *smap, "-vf", f"{lanczos},format={pix}", *INTERMEDIATE,
                "-pix_fmt", pix, *encode.hdr_args(video), "-fps_mode", "passthrough",
                "-f", "matroska", safe_arg(joined)])


# ---- command line -----------------------------------------------------------------

def add_parser(sub: argparse._SubParsersAction) -> None:
    ap = sub.add_parser("enhance", help="raise the frame rate (RIFE) and/or upscale 2D video")
    ap.add_argument("input", type=Path)
    ap.add_argument("--fps", metavar="N", help="interpolate to N frames per second (e.g. 60)")
    ap.add_argument("--upscale", type=int, choices=UPSCALES, help="scale factor")
    ap.add_argument("--method", choices=METHODS, default="lanczos",
                    help="lanczos (ffmpeg, default) or model (Real-ESRGAN, x2 only)")
    ap.add_argument("--chunk", type=float, default=10.0, metavar="SECONDS",
                    help="seconds of frames on disk at once (default 10)")
    ap.add_argument("--tmp", type=Path, metavar="DIR", help="where the temporary frames go "
                    "(default ~/.cache/xrdome-tools)")
    ap.add_argument("--treat-as-2d", action="store_true",
                    help="enhance an input detected as stereo or VR180 anyway")
    ap.add_argument("--codec", choices=sorted(transcode.CODEC_NAMES), default=None, help="default hevc")
    ap.add_argument("--quality", choices=sorted(encode.QUALITY), default="high")
    ap.add_argument("--encoder", help="force an ffmpeg encoder for the final encode")
    ap.add_argument("--dry-run", action="store_true", help="print the commands, run nothing")
    out = ap.add_mutually_exclusive_group()
    out.add_argument("-o", dest="out", type=Path, metavar="PATH", help="output file")
    out.add_argument("--outdir", type=Path, metavar="DIR", help="output directory")
    ap.add_argument("--force", action="store_true", help="replace an existing output")


def output_tokens(fps: Fraction | None, upscale: int | None) -> list[str]:
    return (([f"{_rate_text(fps).replace('.', 'p')}fps"] if fps is not None else [])
            + ([f"x{upscale}"] if upscale else []))


def run(args: argparse.Namespace) -> int:
    if not args.input.is_file():
        raise ToolError(f"{args.input}: no such file", EXIT_SETUP)
    fps = parse_rate(args.fps) if args.fps is not None else None
    if fps is None and args.upscale is None:
        raise ToolError("nothing to do: give --fps N, --upscale 2|3|4, or both", EXIT_SETUP)
    codec = args.codec or (transcode._encoder_codec(args.encoder) if args.encoder else "hevc")
    name = formats.output_name(args.input.stem, formats.AS_CHOICES["flat"],
                               extra=output_tokens(fps, args.upscale))
    dst = formats.output_target(args.input, name, args.out, args.outdir, args.force)
    enhance(Runner(dry_run=args.dry_run), args.input, dst, fps=fps, upscale=args.upscale,
            method=args.method, chunk=args.chunk, tmp=args.tmp, treat_as_2d=args.treat_as_2d,
            codec=codec, quality=args.quality, encoder=args.encoder)
    if not args.dry_run:
        print(dst)
    return 0
