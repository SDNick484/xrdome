"""`depth`: depth maps for photos, for the player's 3D mode (spec docs/superpowers/specs/2026-11-19-depth-3d-design.md).

`depth photo FILE|DIR` writes the depth cache of spec 5 (`<sha256 of the file>-dav2s-v1.pgm` in the cache
folder) with ncnn on Vulkan. `depth serve` is the helper service of spec 7 that xrdome starts and asks for
the depth of each photo it opens, and that maps a film's frames for live depth (spec 8).
`depth bench-video` runs that video loop on synthetic frames and reports the Hz it achieves, and
`depth flicker` measures how much its (smoothed) depth flickers on a folder of consecutive frames. This module
is a standard-library launcher: the work is done by tools/xrdome_depth/photo.py, serve.py, video.py and
flicker.py, run with the depth venv's interpreter that `setup depth` built.
"""

import argparse
import os
from pathlib import Path
from typing import Mapping

from . import config, depthenv
from .errors import EXIT_SETUP, ToolError
from .run import Runner, safe_arg

PHOTO_SCRIPT = Path(__file__).resolve().parents[1] / "xrdome_depth" / "photo.py"
SERVE_SCRIPT = Path(__file__).resolve().parents[1] / "xrdome_depth" / "serve.py"
VIDEO_SCRIPT = Path(__file__).resolve().parents[1] / "xrdome_depth" / "video.py"
FLICKER_SCRIPT = Path(__file__).resolve().parents[1] / "xrdome_depth" / "flicker.py"
SOCKET_NAME = "xrdome-depth.sock"
IDLE_EXIT_S = 600
CPU_WARNING = ("warning: depth in this environment runs on the CPU (setup depth --cpu); that is slow and "
               "takes frames away from xrdome while it renders")


def cache_dir(p: config.Paths) -> Path:
    """Where the depth files go; the player computes the same path."""
    return p.cache_dir / "depth"


def socket_path(env: Mapping[str, str] = os.environ, uid: int | None = None) -> Path:
    """The helper's socket; the player (src/app/depthclient.c) computes the same path.

    $XDG_RUNTIME_DIR/xrdome-depth.sock; without an absolute XDG_RUNTIME_DIR (macOS has none),
    xrdome-depth-<uid>.sock in $TMPDIR when it is absolute, else in /tmp."""
    run = env.get("XDG_RUNTIME_DIR", "")
    if run and os.path.isabs(run):
        return Path(run) / SOCKET_NAME
    tmp = env.get("TMPDIR", "")
    base = tmp if tmp and os.path.isabs(tmp) else "/tmp"
    return Path(base) / f"xrdome-depth-{os.getuid() if uid is None else uid}.sock"


def serve_argv(p: config.Paths, sock: Path, idle_exit: float) -> list[str]:
    return [str(depthenv.python(p)), "-u", str(SERVE_SCRIPT), "--socket", str(sock),
            "--cache-dir", str(cache_dir(p)), "--models-dir", str(depthenv.ncnn_dir(p)),
            "--idle-exit", f"{idle_exit:g}"]


def bench_video_argv(p: config.Paths, seconds: float, rate: float, *, cpu: bool) -> list[str]:
    argv = [str(depthenv.python(p)), "-u", str(VIDEO_SCRIPT), "--bench", "--models-dir", str(depthenv.ncnn_dir(p)),
            "--seconds", f"{seconds:g}", "--rate", f"{rate:g}"]
    return argv + (["--cpu"] if cpu else [])


def flicker_argv(p: config.Paths, directory: Path, *, start: int, count: int, as_json: bool, cpu: bool) -> list[str]:
    argv = [str(depthenv.python(p)), "-u", str(FLICKER_SCRIPT), "frames", safe_arg(directory),
            "--models-dir", str(depthenv.ncnn_dir(p)), "--start", str(start), "--count", str(count)]
    return argv + (["--json"] if as_json else []) + (["--cpu"] if cpu else [])


def photo_argv(p: config.Paths, target: Path, *, cpu: bool, force: bool) -> list[str]:
    argv = [str(depthenv.python(p)), "-u", str(PHOTO_SCRIPT),
            "--cache-dir", str(cache_dir(p)), "--models-dir", str(depthenv.ncnn_dir(p))]
    if cpu:
        argv.append("--cpu")
    if force:
        argv.append("--force")
    return argv + [safe_arg(target)]


def add_parser(sub: argparse._SubParsersAction) -> None:
    ap = sub.add_parser("depth", help="depth maps for photos and films (3D in the player); needs setup depth")
    what = ap.add_subparsers(dest="what", metavar="WHAT", required=True)
    photo = what.add_parser("photo", help="write the depth cache file of a photo, or of every image in a folder")
    photo.add_argument("input", type=Path, metavar="FILE|DIR")
    photo.add_argument("--force", action="store_true", help="recompute photos that already have a cache file")
    serve = what.add_parser("serve", help="the helper service xrdome starts for 3D photos (GPU only)")
    serve.add_argument("--socket", type=Path, default=None,
                       help="the Unix socket to listen on (default $XDG_RUNTIME_DIR/xrdome-depth.sock)")
    serve.add_argument("--idle-exit", type=float, default=IDLE_EXIT_S, metavar="SECONDS",
                       help=f"exit after this long with no client and nothing to do (default {IDLE_EXIT_S})")
    bench = what.add_parser("bench-video", help="the helper's video loop on synthetic 322x182 frames: the Hz it "
                                                "achieves (run it beside xrdome to see the cost of both)")
    bench.add_argument("--seconds", type=float, default=30.0, help="how long to run (default 30)")
    bench.add_argument("--rate", type=float, default=30.0, metavar="HZ",
                       help="frames fed per second (default 30; the helper takes at most 30)")
    flicker = what.add_parser("flicker", help="how much the depth of consecutive frames flickers (the spike's "
                                              "metric, raw, unsmoothed and smoothed); use a near-static shot")
    flicker.add_argument("frames", type=Path, metavar="DIR", help="a folder of consecutive frame images")
    flicker.add_argument("--start", type=int, default=0, help="index of the first image in name order (default 0)")
    flicker.add_argument("--count", type=int, default=30, help="frames to use (default 30, as the spike)")
    flicker.add_argument("--json", action="store_true", help="print the result as JSON")


def run(args: argparse.Namespace) -> int:
    p = config.paths()
    if getattr(args, "what", "photo") == "serve":
        return run_serve(args, p)
    if getattr(args, "what", "photo") == "bench-video":
        return run_bench_video(args, p)
    if getattr(args, "what", "photo") == "flicker":
        return run_flicker(args, p)
    if not args.input.exists():
        raise ToolError(f"{args.input}: no such file or folder", EXIT_SETUP)
    why = depthenv.installed(p)
    if why:
        raise ToolError(f"{why}; run `xrdome-tools setup depth` first", EXIT_SETUP)
    cpu = depthenv.variant(p) == "cpu"
    if cpu:
        print(CPU_WARNING)
    Runner().run(photo_argv(p, args.input, cpu=cpu, force=args.force))
    return 0


def run_serve(args: argparse.Namespace, p: config.Paths) -> int:
    why = depthenv.installed(p)
    if why:
        raise ToolError(f"{why}; run `xrdome-tools setup depth` first", EXIT_SETUP)
    if depthenv.variant(p) == "cpu":
        raise ToolError("depth serve runs on the GPU only: depth on the CPU takes frames away from xrdome "
                        "while it renders (setup depth --cpu was used); `depth photo` still works", EXIT_SETUP)
    if args.idle_exit <= 0:
        raise ToolError("--idle-exit must be more than 0 seconds", EXIT_SETUP)
    sock = args.socket if args.socket is not None else socket_path()
    Runner().run(serve_argv(p, sock, args.idle_exit))
    return 0


def run_bench_video(args: argparse.Namespace, p: config.Paths) -> int:
    why = depthenv.installed(p)
    if why:
        raise ToolError(f"{why}; run `xrdome-tools setup depth` first", EXIT_SETUP)
    if args.seconds <= 0 or args.rate <= 0:
        raise ToolError("--seconds and --rate must be more than 0", EXIT_SETUP)
    cpu = depthenv.variant(p) == "cpu"
    if cpu:
        print(CPU_WARNING)
    Runner().run(bench_video_argv(p, args.seconds, args.rate, cpu=cpu))
    return 0


def run_flicker(args: argparse.Namespace, p: config.Paths) -> int:
    if not args.frames.is_dir():
        raise ToolError(f"{args.frames}: not a folder of frame images", EXIT_SETUP)
    why = depthenv.installed(p)
    if why:
        raise ToolError(f"{why}; run `xrdome-tools setup depth` first", EXIT_SETUP)
    if args.count < 2 or args.start < 0:
        raise ToolError("--count must be at least 2 and --start 0 or more", EXIT_SETUP)
    cpu = depthenv.variant(p) == "cpu"
    if cpu:
        print(CPU_WARNING)
    Runner().run(flicker_argv(p, args.frames, start=args.start, count=args.count, as_json=args.json, cpu=cpu))
    return 0
