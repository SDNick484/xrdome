"""`to3d`: 2D video to side-by-side 3D with iw3, finished through `transcode`.

iw3 runs in the environment `setup to3d` built. On NVIDIA it encodes HEVC with NVENC itself
and `transcode` only remuxes and tags; elsewhere it writes a libx264 CRF 12 intermediate
(PyAV's bundled ffmpeg has no VA-API) that `transcode` encodes with VA-API or software.
"""

import argparse
import os
from pathlib import Path

from . import config, formats, iw3env, models, transcode
from .errors import EXIT_SETUP, ToolError
from .run import Runner, safe_arg, say_path

MODELS = ("Any_V2_S", "VDA_S")
MODEL_TOKEN = {"Any_V2_S": "AnyV2S", "VDA_S": "VDAS"}
DEFAULT_DIVERGENCE = 2.0
DEFAULT_CONVERGENCE = 0.5
DEFAULT_FG_SCALE = 0.0
INTERMEDIATE_CRF = "12"
# iw3's depth models are vision transformers. On Radeons PyTorch keeps its fast attention
# kernels (AOTriton) switched off as "experimental" unless this is set, and every attention
# layer falls back to the slow math path. Radeon 860M, Any_V2_S, 608x1080 source: 2.46 -> 5.34
# frames/s, output PSNR 48.7 dB / SSIM 0.994 against the default path (measured 2026-11-20).
# Ignored on CUDA and the CPU. An explicit value in the environment (for example 0) wins.
AOTRITON_VAR = "TORCH_ROCM_AOTRITON_ENABLE_EXPERIMENTAL"
MIN_DEPTH_RESOLUTION = 224  # iw3's MIN_RESOLUTION for Depth Anything
IW3_DEPTH_RESOLUTION = 392  # iw3's own default for both models
# On ROCm the GPU is usually an iGPU, and the depth model is most of the time per frame.
# Radeon 860M, fast attention, 608x1080 source: 280 ran 9.9 frames/s against 5.3 at 392, and
# the owner saw no difference on the glasses (2026-11-20). NVIDIA keeps iw3's default.
ROCM_DEPTH_RESOLUTION = 280


def _num(x: float) -> str:
    return str(float(x))  # 2 -> "2.0", 0.65 -> "0.65", as sbs-test names them


def output_stem(stem: str, model: str, divergence: float, convergence: float, fg_scale: float,
                depth_resolution: int | None = None) -> str:
    """`<stem>_<AnyV2S|VDAS>_div<D>[_c<C>][_fs<F>][_dr<N>]`; c, fs and dr only when not the default."""
    out = f"{stem}_{MODEL_TOKEN.get(model, model.replace('_', ''))}_div{_num(divergence)}"
    if float(convergence) != DEFAULT_CONVERGENCE:
        out += f"_c{_num(convergence)}"
    if float(fg_scale) != DEFAULT_FG_SCALE:
        out += f"_fs{_num(fg_scale)}"
    if depth_resolution is not None:
        out += f"_dr{int(depth_resolution)}"
    return out


def max_fps(fps: tuple[int, int]) -> int:
    """The source rate rounded up (119.88 -> 120), so iw3 keeps every frame."""
    num, den = fps
    if num <= 0 or den <= 0:
        return 60
    return -(-num // den)


def iw3_argv(paths: config.Paths, src: Path, dst: Path, *, model: str, divergence: float,
             convergence: float, fg_scale: float, fps: tuple[int, int], half: bool, nvenc: bool,
             cpu: bool = False, depth_resolution: int | None = None) -> list[str]:
    argv = [str(iw3env.python(paths)), "-m", "iw3.cli", "-i", safe_arg(src), "-o", safe_arg(dst),
            "--depth-model", model, "--divergence", _num(divergence),
            "--convergence", _num(convergence), "--max-fps", str(max_fps(fps))]
    if float(fg_scale) != DEFAULT_FG_SCALE:
        argv += ["--foreground-scale", _num(fg_scale)]
    if depth_resolution is not None:
        argv += ["--resolution", str(int(depth_resolution))]
    if model == "VDA_S":
        argv += ["--ema-normalize", "--scene-detect"]
    if half:
        argv += ["--half-sbs"]
    if cpu:
        argv += ["--gpu", "-1"]
    if nvenc:
        argv += ["--video-codec", "hevc_nvenc"]
    else:
        argv += ["--video-codec", "libx264", "--crf", INTERMEDIATE_CRF]
    return argv


def add_parser(sub: argparse._SubParsersAction) -> None:
    ap = sub.add_parser("to3d", help="convert 2D video to side-by-side 3D with iw3")
    ap.add_argument("input", type=Path)
    ap.add_argument("--model", default="Any_V2_S", help="depth model: " + " or ".join(MODELS))
    ap.add_argument("--divergence", type=float, default=DEFAULT_DIVERGENCE, metavar="D")
    ap.add_argument("--convergence", type=float, default=DEFAULT_CONVERGENCE, metavar="C")
    ap.add_argument("--foreground-scale", type=float, default=DEFAULT_FG_SCALE, metavar="F")
    ap.add_argument("--depth-resolution", type=int, metavar="N",
                    help=f"depth model input, short side (default {ROCM_DEPTH_RESOLUTION} on ROCm, "
                         f"{IW3_DEPTH_RESOLUTION} otherwise; at least {MIN_DEPTH_RESOLUTION}); "
                         "smaller is faster and softer")
    ap.add_argument("--half", action="store_true", help="half SBS (1920x1080 in all) instead of full")
    ap.add_argument("--cpu", action="store_true", help="run on the CPU when there is no CUDA or ROCm (slow)")
    ap.add_argument("--quality", choices=["high", "normal", "small"], default="high",
                    help="final encode quality, as for transcode")
    ap.add_argument("--tmp", type=Path, metavar="DIR", help="folder for the intermediate file")
    ap.add_argument("--dry-run", action="store_true", help="print the commands, run nothing")
    out = ap.add_mutually_exclusive_group()
    out.add_argument("-o", dest="out", type=Path, metavar="PATH", help="output file")
    out.add_argument("--outdir", type=Path, metavar="DIR", help="output directory")
    ap.add_argument("--force", action="store_true", help="replace an existing output")


def run(args: argparse.Namespace) -> int:
    if args.model not in MODELS:
        m = models.find(args.model)
        known = f" ({m.licence})" if m else ""
        raise ToolError(f"{args.model}{known}: not an allowed model for to3d; use "
                        + " or ".join(MODELS), EXIT_SETUP)
    if args.depth_resolution is not None and args.depth_resolution < MIN_DEPTH_RESOLUTION:
        raise ToolError(f"--depth-resolution {args.depth_resolution}: at least {MIN_DEPTH_RESOLUTION}",
                        EXIT_SETUP)
    if not args.input.is_file():
        raise ToolError(f"{args.input}: no such file", EXIT_SETUP)
    p = config.paths()
    said: list[str] = []

    def say(key: str, value: str, reason: str) -> None:
        say_path(key, value, reason)
        said.append(f"path: {key}={value} ({reason})\n")

    why = iw3env.installed(p)
    if why:
        raise ToolError(f"{why}; run `xrdome-tools setup to3d` first", EXIT_SETUP)
    # Set before the GPU test, so the test's attention kernel runs the way iw3's will.
    aot_given = os.environ.get(AOTRITON_VAR)
    os.environ.setdefault(AOTRITON_VAR, "1")
    tc = iw3env.check(Runner(), p)
    compute = tc.compute
    if compute == "none":
        if not args.cpu:
            raise ToolError(tc.problem or iw3env.NO_COMPUTE, EXIT_SETUP)
        why = (f"the {tc.seen} GPU failed PyTorch's test" if tc.problem
               else "no CUDA or ROCm in the iw3 environment")
        say("compute", "cpu", f"--cpu given; {why}, this is slow")
    else:
        say("compute", compute, "PyTorch in the iw3 environment ran its GPU test"
            + (f" on {tc.arch}" if tc.arch else ""))
    if compute == "rocm":
        if aot_given is None:
            say("attention", "aotriton", f"{AOTRITON_VAR}=1: PyTorch's fast attention kernels on "
                "Radeon, about twice as fast as the default path")
        else:
            say("attention", "aotriton" if aot_given == "1" else "default",
                f"{AOTRITON_VAR}={aot_given} from the environment")
    if args.depth_resolution is not None:
        depth_res, why_res = args.depth_resolution, "--depth-resolution given"
    elif compute == "rocm":
        depth_res, why_res = ROCM_DEPTH_RESOLUTION, (
            f"ROCm default: about twice as fast as {IW3_DEPTH_RESOLUTION}, no visible difference on "
            f"a Radeon 860M test; --depth-resolution {IW3_DEPTH_RESOLUTION} for iw3's default")
    else:
        depth_res, why_res = IW3_DEPTH_RESOLUTION, "iw3's default"
    say("depth-resolution", str(depth_res), why_res)
    # iw3's default is not passed and not in the name, so names match runs without the option.
    passed_res = None if depth_res == IW3_DEPTH_RESOLUTION else depth_res
    models.require(args.model, p)
    transcode._require_programs()

    info = formats.probe(Runner(), args.input)
    rate = info.fps[0] / info.fps[1] if info.fps[1] else 0.0
    if (info.width < 1920 and info.height < 1080) or 0 < rate < 50:
        print(f"hint: the input is {info.width}x{info.height} at {rate:.4g} fps; for interpolation "
              "to 60 fps or upscaling, run `xrdome-tools enhance` first, then to3d on its output")

    stem = output_stem(args.input.stem, args.model, args.divergence, args.convergence,
                       args.foreground_scale, passed_res)
    fmt = formats.Format("flat", "sbs-lr", "half" if args.half else "full")
    dst = formats.output_target(args.input, formats.output_name(stem, fmt), args.out, args.outdir,
                                args.force)
    nvenc = compute == "cuda"
    say("depth", args.model, f"divergence {_num(args.divergence)}, convergence "
             f"{_num(args.convergence)}, foreground scale {_num(args.foreground_scale)}")
    say("max-fps", str(max_fps(info.fps)), "the source rate rounded up, so no frame is dropped")
    if nvenc:
        say("iw3-encode", "hevc_nvenc", "CUDA: iw3 encodes HEVC itself; transcode only remuxes and tags")
    else:
        say("iw3-encode", "libx264", f"CRF {INTERMEDIATE_CRF} intermediate; transcode makes the final encode")

    tmp = args.tmp or p.cache_dir
    inter = tmp / f"to3d-{os.getpid()}.mp4"
    log = None
    if not args.dry_run:
        tmp.mkdir(parents=True, exist_ok=True)
        log = open(f"{dst}.log", "w", encoding="utf-8")
        log.writelines(said)
        log.flush()
    try:
        runner = Runner(dry_run=args.dry_run, log=log)
        runner.run(iw3_argv(p, args.input, inter, model=args.model, divergence=args.divergence,
                            convergence=args.convergence, fg_scale=args.foreground_scale,
                            fps=info.fps, half=args.half, nvenc=nvenc, cpu=compute == "none",
                            depth_resolution=passed_res),
                   cleanup=[inter])
        as_ = "hsbs" if args.half else "fsbs"
        if args.dry_run:
            print(f"then: xrdome-tools transcode {safe_arg(inter)} --as {as_} "
                  f"--quality {args.quality} -o {safe_arg(dst)}; the intermediate is deleted")
            return 0
        transcode.transcode(runner, inter, dst, as_=as_, quality=args.quality,
                            extra_tokens=stem[len(args.input.stem) + 1:].split("_"))
    finally:
        inter.unlink(missing_ok=True)
        if log is not None:
            log.close()
    print(dst)
    return 0
