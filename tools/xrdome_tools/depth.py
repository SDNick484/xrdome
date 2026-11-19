"""`depth`: depth maps for photos, for the player's 3D mode (spec docs/superpowers/specs/2026-11-19-depth-3d-design.md).

`depth photo FILE|DIR` writes the depth cache of spec 5 (`<sha256 of the file>-dav2s-v1.pgm` in the cache
folder) with ncnn on Vulkan. This module is a standard-library launcher: the work is done by
tools/xrdome_depth/photo.py, run with the depth venv's interpreter that `setup depth` built.
"""

import argparse
from pathlib import Path

from . import config, depthenv
from .errors import EXIT_SETUP, ToolError
from .run import Runner, safe_arg

PHOTO_SCRIPT = Path(__file__).resolve().parents[1] / "xrdome_depth" / "photo.py"
CPU_WARNING = ("warning: depth in this environment runs on the CPU (setup depth --cpu); that is slow and "
               "takes frames away from xrdome while it renders")


def cache_dir(p: config.Paths) -> Path:
    """Where the depth files go; the player computes the same path."""
    return p.cache_dir / "depth"


def photo_argv(p: config.Paths, target: Path, *, cpu: bool, force: bool) -> list[str]:
    argv = [str(depthenv.python(p)), "-u", str(PHOTO_SCRIPT),
            "--cache-dir", str(cache_dir(p)), "--models-dir", str(depthenv.ncnn_dir(p))]
    if cpu:
        argv.append("--cpu")
    if force:
        argv.append("--force")
    return argv + [safe_arg(target)]


def add_parser(sub: argparse._SubParsersAction) -> None:
    ap = sub.add_parser("depth", help="depth maps for photos (3D in the player); needs setup depth")
    what = ap.add_subparsers(dest="what", metavar="WHAT", required=True)
    photo = what.add_parser("photo", help="write the depth cache file of a photo, or of every image in a folder")
    photo.add_argument("input", type=Path, metavar="FILE|DIR")
    photo.add_argument("--force", action="store_true", help="recompute photos that already have a cache file")


def run(args: argparse.Namespace) -> int:
    p = config.paths()
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
