"""`transcode`: re-encode (or remux) to HEVC/AV1 in Matroska that xrdome detects without flags."""

import argparse
import os
import shutil
import subprocess
from pathlib import Path

from . import config, encode, formats, hw
from .errors import EXIT_SETUP, EXIT_VERIFY, ToolError
from .run import Runner, safe_arg, say_path

CODEC_NAMES = {"hevc": "hevc", "av1": "av1"}
SOFTWARE = {"libx265": "hevc", "libsvtav1": "av1"}
STEREO_TYPE = {"sbs": "side by side", "tb": "top and bottom"}


def _require_programs() -> None:
    for prog, pkgs in (("ffmpeg", "ffmpeg"), ("ffprobe", "ffmpeg")):
        if shutil.which(config.program(prog)) is None:
            raise ToolError(f"{prog} not found. Install FFmpeg: Gentoo media-video/ffmpeg, "
                            f"Debian/Ubuntu {pkgs}, Arch {pkgs}", EXIT_SETUP)


def _encoder_codec(name: str) -> str:
    return SOFTWARE.get(name) or ("hevc" if name.startswith("hevc") else "av1")


def _choose(runner: Runner, codec: str, encoder: str | None) -> tuple[hw.EncoderChoice, hw.Probe | None]:
    if encoder is not None and _encoder_codec(encoder) != codec:
        raise ToolError(f"--encoder {encoder} makes {_encoder_codec(encoder)}, not {codec}", EXIT_SETUP)
    if encoder in SOFTWARE:
        # A software encoder needs no hardware test encode; only check it is built in.
        out = runner.run([config.program("ffmpeg"), "-hide_banner", "-encoders"], capture=True,
                         timeout=20).stdout
        if encoder not in hw.parse_ffmpeg_encoders(out):
            raise ToolError(f"--encoder {encoder}: not built into this ffmpeg", EXIT_SETUP)
        return hw.EncoderChoice(encoder, "software", "requested with --encoder"), None
    p = hw.probe(runner, compute=False)
    return hw.choose_encoder(p, codec, encoder), p


def _expected_stereo(f: formats.Format) -> tuple[str | None, bool]:
    if f.layout == "mono":
        return None, False
    kind = "sbs" if f.layout.startswith("sbs") else "tb"
    return STEREO_TYPE[kind], f.layout.endswith("-rl")


def verify(runner: Runner, src_info: formats.ProbeInfo, dst: Path, f: formats.Format,
           codec: str) -> list[str]:
    """Reasons the output is not what was asked for; empty when it is."""
    out = formats.probe(runner, dst)
    reasons: list[str] = []
    if out.codec != CODEC_NAMES[codec]:
        reasons.append(f"video codec is {out.codec}, expected {CODEC_NAMES[codec]}")
    if out.width > src_info.width or out.height > src_info.height:
        reasons.append(f"size {out.width}x{out.height} is larger than the source "
                       f"{src_info.width}x{src_info.height}")
    want, inverted = _expected_stereo(f)
    got = None if out.stereo in (None, "2D") else out.stereo
    if got != want or (want is not None and out.stereo_inverted != inverted):
        reasons.append(f"Stereo 3D side data is {out.stereo or 'absent'}"
                       f"{' (inverted)' if out.stereo_inverted else ''}, expected {want or 'none'}"
                       f"{' (inverted)' if inverted else ''}")
    if src_info.duration > 0 and abs(out.duration - src_info.duration) > 0.5:
        reasons.append(f"duration is {out.duration:.2f} s, the source is {src_info.duration:.2f} s")
    for kind in ("audio", "subtitle"):
        want_n = sum(1 for s, a, _ in encode.plan_streams(src_info)
                     if a != "drop" and s.get("codec_type") == kind)
        have_n = sum(1 for s in out.streams if s.get("codec_type") == kind)
        if have_n < want_n:
            reasons.append(f"{kind}: {have_n} stream(s) in the output, expected {want_n}")
    return reasons


def _friendly(info: formats.ProbeInfo, codec: str, max_width: int | None) -> bool:
    return info.codec == CODEC_NAMES[codec] and (max_width is None or info.width <= max_width)


def transcode(runner: Runner, src: Path, dst: Path, *, as_: str = "auto", codec: str = "hevc",
              quality: str = "high", max_width: int | None = None, encoder: str | None = None,
              reencode: bool = False, extra_tokens: list[str] = []) -> Path:
    """Write dst from src. dst must already be checked with formats.output_target.

    extra_tokens are the tokens the caller put in dst's name; they are only recorded.
    """
    if codec not in CODEC_NAMES:
        raise ToolError(f"unknown codec {codec!r}; use hevc or av1", EXIT_SETUP)
    if quality not in encode.QUALITY:
        raise ToolError(f"unknown quality {quality!r}; use high, normal or small", EXIT_SETUP)
    if os.path.exists(dst) and os.path.exists(src) and os.path.samefile(src, dst):
        raise ToolError(f"{dst}: that is the input file; refusing to overwrite it", EXIT_SETUP)
    _require_programs()

    # Probing changes nothing, so it runs even in a dry run.
    reader = Runner(log=runner.log) if runner.dry_run else runner
    log = None
    if runner.log is None and not runner.dry_run:
        log = open(f"{dst}.log", "w", encoding="utf-8")
        runner = Runner(log=log)
        reader = runner
    try:
        return _transcode(runner, reader, src, dst, as_, codec, quality, max_width, encoder,
                          reencode, extra_tokens, log)
    finally:
        if log is not None:
            log.close()


def _say(log, key: str, value: str, reason: str) -> None:
    say_path(key, value, reason)
    if log is not None:
        log.write(f"path: {key}={value} ({reason})\n")
        log.flush()


def _transcode(runner, reader, src, dst, as_, codec, quality, max_width, encoder, reencode,
               extra_tokens, log) -> Path:
    info = formats.probe(reader, src)
    f, sources = formats.detect(info, src.name, as_)
    _say(log, "format", f"{f.proj}/{f.layout}" + (f"/{f.packing}" if f.packing else ""),
         ", ".join(f"{k} from {v}" for k, v in sources.items()))
    if extra_tokens:
        _say(log, "name-tokens", ",".join(extra_tokens), "added by the caller")
    video = next((s for s in info.streams if s.get("index") == info.video_index), {})
    smap, lines = encode.stream_args(info)
    for ln in lines:
        _say(log, "stream", ln.split(":")[0], ln)
    tag = ["-metadata:s:v:0", f"stereo_mode={formats.output_tags(f)['stereo_mode']}"]

    pre: list[str] = []
    if _friendly(info, codec, max_width) and not reencode:
        _say(log, "transcode", "remux",
             f"already {codec.upper()} {info.width}x{info.height}, within limits; only tags and name added")
        vargs = ["-c:v", "copy"]
    else:
        ten = info.bit_depth > 8
        choice, _p = _choose(reader, codec, encoder)
        _say(log, "encode", choice.name, choice.reason)
        if info.codec == "h264" and info.width > 4096:
            _say(log, "transcode", "reencode", f"H.264 {info.width} wide decodes in software on AMD")
        elif reencode:
            _say(log, "transcode", "reencode", "--reencode")
        else:
            _say(log, "transcode", "reencode", f"source is {info.codec}, wanted {codec}"
                 if info.codec != codec else f"wider than --max-width {max_width}")
        device = None
        qvbr = True
        hdr = encode.hdr_args(video)
        if choice.family == "vaapi":
            p = _p
            device = p.vaapi.device if p and p.vaapi else None
            qvbr = encode.vaapi_qvbr_ok(reader, choice, device, quality, ten)
            if not qvbr:
                _say(log, "rate-control", "CQP", "this VA-API driver rejected -rc_mode QVBR")
            if hdr and not encode.vaapi_hdr_ok(reader, choice, device, ten, hdr):
                _say(log, "hdr", "not passed", "HDR metadata is not passed through by this VA-API encoder")
                hdr = []
        if hdr:
            _say(log, "hdr", "passed", "colour primaries, transfer and matrix copied; "
                 "mastering display metadata is not")
        if ten:
            _say(log, "bit-depth", "10", "source is 10-bit")
        pre = encode.input_args(choice, device)
        vargs = encode.video_args(choice, quality, ten, max_width, f, device,
                                  size=(info.width, info.height), qvbr=qvbr, hdr=hdr)
        if max_width and info.width > max_width:
            _say(log, "scale", f"<={max_width}", "--max-width")

    part = dst.with_name(dst.name + ".part")
    argv = [config.program("ffmpeg"), "-nostdin", "-hide_banner", "-loglevel", "warning", "-y",
            *pre, "-i", safe_arg(src), *smap, *vargs, "-fps_mode", "passthrough", *tag,
            "-f", "matroska", safe_arg(part)]
    runner.run(argv, cleanup=[part])
    if runner.dry_run:
        return dst
    os.replace(part, dst)
    reasons = verify(reader, info, dst, f, codec)
    if reasons:
        raise ToolError(f"{dst}: written, but verification failed (output kept): "
                        + "; ".join(reasons), EXIT_VERIFY)
    return dst


# ---- command line -----------------------------------------------------------------

def add_parser(sub: argparse._SubParsersAction) -> None:
    ap = sub.add_parser("transcode", help="re-encode (or remux) to HEVC/AV1 Matroska that xrdome opens")
    ap.add_argument("input", type=Path)
    ap.add_argument("--as", dest="as_", default="auto",
                    help="input format: auto, " + ", ".join(formats.AS_CHOICES))
    ap.add_argument("--codec", choices=sorted(CODEC_NAMES), default=None, help="default hevc")
    ap.add_argument("--quality", choices=sorted(encode.QUALITY), default="high")
    ap.add_argument("--max-width", type=int, metavar="W", help="scale down to at most W pixels wide")
    ap.add_argument("--encoder", help="force an ffmpeg encoder, e.g. libx265 or hevc_vaapi")
    ap.add_argument("--reencode", action="store_true", help="never remux, even when already friendly")
    ap.add_argument("--dry-run", action="store_true", help="print the commands, run nothing")
    out = ap.add_mutually_exclusive_group()
    out.add_argument("-o", dest="out", type=Path, metavar="PATH", help="output file")
    out.add_argument("--outdir", type=Path, metavar="DIR", help="output directory")
    ap.add_argument("--force", action="store_true", help="replace an existing output")


def run(args: argparse.Namespace) -> int:
    if not args.input.is_file():
        raise ToolError(f"{args.input}: no such file", EXIT_SETUP)
    if args.max_width is not None and args.max_width < 16:
        raise ToolError("--max-width must be at least 16", EXIT_SETUP)
    codec = args.codec or (_encoder_codec(args.encoder) if args.encoder else "hevc")
    _require_programs()
    info = formats.probe(Runner(), args.input)
    f, _ = formats.detect(info, args.input.name, args.as_)
    name = formats.output_name(args.input.stem, f)
    dst = formats.output_target(args.input, name, args.out, args.outdir, args.force)
    transcode(Runner(dry_run=args.dry_run), args.input, dst, as_=args.as_, codec=codec,
              quality=args.quality, max_width=args.max_width, encoder=args.encoder,
              reencode=args.reencode)
    if not args.dry_run:
        print(dst)
    return 0
