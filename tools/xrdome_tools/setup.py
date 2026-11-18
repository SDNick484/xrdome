"""`setup to3d`: build the pinned iw3 environment that `to3d` runs in."""

import argparse
import shutil
import sys

from . import config, hw, iw3env, models
from .errors import EXIT_SETUP, ToolError
from .run import Runner, say_path

TO3D_MODELS = ("Any_V2_S", "VDA_S")
MODEL_SIZE = "about 0.2 GB more for the two depth models"


def add_parser(sub: argparse._SubParsersAction) -> None:
    ap = sub.add_parser("setup", help="build the environment a subcommand needs (to3d: iw3 and PyTorch)")
    ap.add_argument("what", choices=["to3d"], help="what to set up")
    ap.add_argument("--cpu", action="store_true",
                    help="install CPU-only PyTorch when no NVIDIA or AMD GPU is found")
    ap.add_argument("--yes", action="store_true", help="do not ask before downloading")
    ap.add_argument("--fetch-models", action="store_true",
                    help="also download the Any_V2_S and VDA_S depth models (checked by sha256)")
    ap.add_argument("--dry-run", action="store_true", help="print the commands, run nothing")


def _reason(p: hw.Probe, variant: str) -> str:
    where = " under WSL2" if p.platform == "wsl2" else ""
    return {"cuda": f"NVIDIA GPU{where}; CUDA wheels from nunif's requirements-torch.txt",
            "rocm": f"AMD GPU{where}; ROCm wheels from {iw3env.TORCH_INDEX['rocm']}",
            "cpu": "no NVIDIA or AMD GPU, --cpu given; CPU wheels"}[variant]


def _confirm(message: str, yes: bool) -> None:
    if yes:
        return
    if not sys.stdin.isatty():
        raise ToolError(f"{message}\nNot asking without a terminal; add --yes to go ahead.", EXIT_SETUP)
    print(message)
    if input("Continue? [y/N] ").strip().lower() not in ("y", "yes"):
        raise ToolError("setup cancelled", EXIT_SETUP)


def run(args: argparse.Namespace) -> int:
    p = config.paths()
    for prog in ("git", "python3"):
        if shutil.which(config.program(prog, p)) is None:
            raise ToolError(f"{prog} not found; setup to3d needs git and python3 (with venv)", EXIT_SETUP)
    probe = hw.probe(Runner(), encoders=False, compute=False)
    variant = iw3env.torch_variant(probe, args.cpu)
    say_path("torch", variant, _reason(probe, variant))

    size = iw3env.DOWNLOAD_SIZE[variant] + (f", plus {MODEL_SIZE}" if args.fetch_models else "")
    message = f"setup to3d downloads {size} into {p.iw3_dir}"
    if not args.dry_run:
        _confirm(message, args.yes)
    else:
        print(message)

    log = None
    if not args.dry_run:
        p.iw3_dir.mkdir(parents=True, exist_ok=True)
        # The marker is written again only when every step has passed.
        (p.iw3_dir / iw3env.MARKER).unlink(missing_ok=True)
        log = open(p.iw3_dir / "setup.log", "w", encoding="utf-8")
    try:
        runner = Runner(dry_run=args.dry_run, log=log)
        for step in iw3env.setup_steps(p, variant):
            runner.run(step)
        if args.dry_run:
            return 0
        iw3env.write_marker(p, variant)
        if variant != "cpu":
            seen = iw3env.compute(runner, p)
            say_path("compute", seen, "torch.cuda.is_available() in the new environment")
            if seen == "none":
                gpu = "ROCm" if variant == "rocm" else "CUDA"
                raise ToolError(
                    f"PyTorch ({gpu}) is installed but cannot see the GPU. "
                    + ("ROCm may not support this GPU yet (for example the Radeon 860M, gfx1152); "
                       "HSA_OVERRIDE_GFX_VERSION is not set for you. " if variant == "rocm" else
                       "Check the NVIDIA driver (nvidia-smi). ")
                    + "to3d will refuse to run unless you add --cpu.", EXIT_SETUP)
        if args.fetch_models:
            for name in TO3D_MODELS:
                m = models.find(name)
                if m is None:
                    raise ToolError(f"{name}: not in the model registry")
                if models.status(m, p) == "ok":
                    print(f"{name}: already ok")
                    continue
                print(f"{name}: downloading {m.url}")
                print(f"{name}: ok, {models.fetch(runner, m, p)}")
    finally:
        if log is not None:
            log.close()
    print(f"setup to3d: done ({variant}); log in {p.iw3_dir / 'setup.log'}")
    return 0
