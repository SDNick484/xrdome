"""`setup to3d`: build the pinned iw3 environment that `to3d` runs in. `setup depth`: the depth venv and ncnn models."""

import argparse
import shutil
import sys

from . import config, depthenv, hw, iw3env, models
from .errors import EXIT_SETUP, ToolError
from .run import Runner, say_path

TO3D_MODELS = ("Any_V2_S", "VDA_S")
MODEL_SIZE = "about 0.2 GB more for the two depth models"


def add_parser(sub: argparse._SubParsersAction) -> None:
    ap = sub.add_parser("setup", help="build the environment a subcommand needs "
                         "(to3d: iw3 and PyTorch; depth: ncnn and the generated depth models)")
    ap.add_argument("what", choices=["to3d", "depth"], help="what to set up")
    ap.add_argument("--cpu", action="store_true",
                    help="to3d: install CPU-only PyTorch when no NVIDIA or AMD GPU is found; "
                         "depth: go on when ncnn finds no Vulkan GPU")
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
    if args.what == "depth":
        return run_depth(args)
    return run_to3d(args)


def run_to3d(args: argparse.Namespace) -> int:
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
            tc = iw3env.check(runner, p)
            say_path("compute", tc.compute, "a GPU test in the new environment"
                     + (f" on {tc.arch}" if tc.arch else ""))
            if tc.problem:
                raise ToolError(f"{tc.problem} Setup itself is complete; to3d will refuse to run "
                                "until this is solved or you add --cpu.", EXIT_SETUP)
            if tc.compute == "none":
                gpu = "ROCm" if variant == "rocm" else "CUDA"
                raise ToolError(
                    f"PyTorch ({gpu}) is installed but cannot see the GPU. "
                    + ("ROCm may not support this GPU yet; "
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


def run_depth(args: argparse.Namespace) -> int:
    if args.fetch_models:
        raise ToolError("--fetch-models is for setup to3d; setup depth generates its models", EXIT_SETUP)
    p = config.paths()
    if shutil.which(config.program("python3", p)) is None:
        raise ToolError("python3 not found; setup depth needs python3 (with venv)", EXIT_SETUP)
    dd = depthenv.depth_dir(p)
    message = f"setup depth downloads {depthenv.DOWNLOAD_SIZE} into {dd}"
    if not args.dry_run:
        _confirm(message, args.yes)
    else:
        print(message)

    log = None
    if not args.dry_run:
        dd.mkdir(parents=True, exist_ok=True)
        # The marker is written again only when every step has passed.
        (dd / depthenv.MARKER).unlink(missing_ok=True)
        log = open(dd / "setup.log", "w", encoding="utf-8")
    try:
        runner = Runner(dry_run=args.dry_run, log=log)
        for step in depthenv.venv_steps(p):
            runner.run(step)
        # Before the long export: it needs only ncnn, and a refusal here wastes nothing.
        gpus = depthenv.gpu_count(runner, p)
        variant = "vulkan"
        if not args.dry_run:
            say_path("compute", f"{gpus} Vulkan GPU(s)", "ncnn.get_gpu_count() in the new environment")
            if gpus == 0:
                if not args.cpu:
                    raise ToolError(depthenv.NO_VULKAN, EXIT_SETUP)
                variant = "cpu"
                print("warning: no Vulkan GPU: depth will run on the CPU, which costs xrdome frames while it renders")
        if not args.dry_run:
            # Nothing left over from an earlier run may pass for this run's output.
            shutil.rmtree(depthenv.export_out(p), ignore_errors=True)
            shutil.rmtree(depthenv.export_work(p), ignore_errors=True)
        try:
            for step in depthenv.export_steps(p):
                runner.run(step)
            if args.dry_run:
                print(f"would check the sha256 of the export in {depthenv.export_out(p)} against models.py, "
                      f"install it to {depthenv.ncnn_dir(p)} and {depthenv.source_dir(p)}")
            else:
                depthenv.verify(depthenv.export_out(p))
                depthenv.install(p, depthenv.export_out(p))
                shutil.rmtree(depthenv.export_out(p), ignore_errors=True)
        finally:
            if args.dry_run:
                print(f"would delete {dd / 'export-venv'} and {depthenv.export_work(p)}")
            else:
                depthenv.remove_export(p)
        if args.dry_run:
            print(f"would write {dd / depthenv.MARKER}")
            return 0
        depthenv.write_marker(p, variant)
    finally:
        if log is not None:
            log.close()
    print(f"setup depth: done ({variant}); log in {dd / 'setup.log'}")
    return 0
