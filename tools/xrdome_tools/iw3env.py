"""The managed iw3 environment: nunif and two torch hub repos at pinned commits, plus a venv.

Layout under Paths.iw3_dir:

    nunif/                                        nagadomi/nunif at PINS["nunif"]
    nunif/iw3/pretrained_models/hub/<owner>_<repo>_main/   the hub repos at their pins
    venv/                                         PyTorch (cuda, rocm or cpu) and nunif's requirements
    xrdome-setup.ini                              written last by `setup to3d`: variant and pins

The pins and steps follow the owner's sbs-test `scripts/setup.sh`.
"""

import configparser
import subprocess
from pathlib import Path

from . import config
from .config import Paths
from .errors import EXIT_SETUP, ToolError
from .hw import Probe
from .run import ChildFailed, Runner

PINS = {
    "nunif": "0f8b86b952d3f3e294c8d887fbf615a679e7f4ca",
    "Depth-Anything_iw3": "92eaac92bcc2c275f48c1f866ec708861055e435",
    "Video-Depth-Anything_iw3": "e9541668c380524f128a3a75886c5f9143214554",
}
REPOS = {
    "nunif": "https://github.com/nagadomi/nunif.git",
    "Depth-Anything_iw3": "https://github.com/nagadomi/Depth-Anything_iw3.git",
    "Video-Depth-Anything_iw3": "https://github.com/nagadomi/Video-Depth-Anything_iw3.git",
}
# PyTorch wheel indexes for the variants nunif's requirements-torch.txt does not cover.
# The ROCm one is unverified on hardware (spec 10); the CUDA build comes from nunif itself.
TORCH_INDEX = {
    "rocm": "https://download.pytorch.org/whl/rocm6.4",
    "cpu": "https://download.pytorch.org/whl/cpu",
}
# What `setup to3d` downloads, for the message before it starts. CUDA measured in sbs-test.
DOWNLOAD_SIZE = {
    "cuda": "about 4 to 5 GB (measured with the CUDA wheels in sbs-test)",
    "rocm": "about 5 GB or more (ROCm wheels; not measured)",
    "cpu": "about 1 GB (CPU wheels; not measured)",
}
MARKER = "xrdome-setup.ini"
NO_GPU = ("no NVIDIA (CUDA) or AMD (ROCm) GPU found. 2D-to-3D on the CPU takes hours "
          "for a few minutes of video (for comparison: about 60 fps on an RTX 5070 Ti). "
          "Add --cpu to use the CPU anyway.")
NO_COMPUTE = ("PyTorch in the iw3 environment sees no CUDA or ROCm GPU. 2D-to-3D on the CPU "
              "takes hours for a few minutes of video (for comparison: about 60 fps on an "
              "RTX 5070 Ti). Add --cpu to use the CPU anyway.")


def nunif_dir(p: Paths) -> Path:
    return p.iw3_dir / "nunif"


def hub_dir(p: Paths) -> Path:
    """Where iw3 points torch.hub; the checkpoints folder models.py uses is under it."""
    return nunif_dir(p) / "iw3" / "pretrained_models" / "hub"


def python(p: Paths) -> Path:
    return p.iw3_dir / "venv" / "bin" / "python"


def torch_variant(p: Probe, cpu: bool) -> str:
    """Which PyTorch build to install: a GPU always wins; the CPU only with --cpu."""
    if "nvidia" in p.gpus:
        return "cuda"
    if "amd" in p.gpus:
        return "rocm"
    if cpu:
        return "cpu"
    raise ToolError(NO_GPU, EXIT_SETUP)


def _checkout(git: str, url: str, dest: Path, pin: str) -> list[list[str]]:
    # init + fetch the pin works the same on a first run, a re-run, a changed pin, and a
    # folder that already holds model files placed by hand (where `git clone` would refuse).
    return [
        [git, "init", "--quiet", str(dest)],
        [git, "-C", str(dest), "fetch", "--quiet", "--depth", "1", url, pin],
        [git, "-C", str(dest), "checkout", "--quiet", "--detach", pin],
    ]


def setup_steps(paths: Paths, variant: str) -> list[list[str]]:
    """The argv of every setup step, in order. Running them again is harmless."""
    if variant not in ("cuda", "rocm", "cpu"):
        raise ToolError(f"unknown PyTorch variant {variant!r}", EXIT_SETUP)
    git = config.program("git", paths)
    nunif = nunif_dir(paths)
    py = str(python(paths))
    steps = _checkout(git, REPOS["nunif"], nunif, PINS["nunif"])
    for repo in ("Depth-Anything_iw3", "Video-Depth-Anything_iw3"):
        # torch.hub's cache name for nagadomi/<repo> on its default branch.
        steps += _checkout(git, REPOS[repo], hub_dir(paths) / f"nagadomi_{repo}_main", PINS[repo])
    steps.append([config.program("python3", paths), "-m", "venv", str(paths.iw3_dir / "venv")])
    pip = [py, "-m", "pip", "install", "--quiet"]
    steps.append(pip + ["--upgrade", "pip"])
    if variant == "cuda":
        steps.append(pip + ["-r", str(nunif / "requirements-torch.txt")])
    else:
        steps.append(pip + ["torch", "torchvision", "--index-url", TORCH_INDEX[variant]])
    steps.append(pip + ["-r", str(nunif / "requirements.txt")])
    # Put nunif on the venv's path so `python -m iw3.cli` works from any directory.
    steps.append([py, "-c", "import site, sys; "
                  "open(site.getsitepackages()[0] + '/xrdome-nunif.pth', 'w').write(sys.argv[1] + '\\n')",
                  str(nunif)])
    return steps


def compute(runner: Runner, paths: Paths) -> str:
    """"cuda", "rocm" or "none", as PyTorch in the iw3 venv sees it."""
    code = ("import torch;print(torch.cuda.is_available());"
            "print(torch.version.cuda);print(torch.version.hip)")
    try:
        out = runner.run([str(python(paths)), "-c", code], capture=True, timeout=120).stdout
    except (ChildFailed, OSError, subprocess.TimeoutExpired):
        return "none"
    words = out.split()
    if len(words) < 3 or words[0] != "True":
        return "none"
    return "rocm" if words[2] != "None" else "cuda"


def write_marker(paths: Paths, variant: str) -> None:
    cp = configparser.ConfigParser(interpolation=None)
    cp["setup"] = {"variant": variant}
    cp["pins"] = dict(PINS)
    with open(paths.iw3_dir / MARKER, "w", encoding="utf-8") as f:
        cp.write(f)


def installed(paths: Paths) -> str | None:
    """Why the environment is not usable, or None when setup finished with today's pins."""
    m = paths.iw3_dir / MARKER
    if not m.is_file() or not python(paths).exists():
        return f"iw3 is not set up in {paths.iw3_dir}"
    cp = configparser.ConfigParser(interpolation=None)
    try:
        cp.read(m, encoding="utf-8")
    except configparser.Error:
        return f"{m} is unreadable"
    have = dict(cp["pins"]) if cp.has_section("pins") else {}
    if have != {k.lower(): v for k, v in PINS.items()}:  # configparser lowercases keys
        return f"iw3 in {paths.iw3_dir} was set up with other pins"
    return None


def variant(paths: Paths) -> str:
    cp = configparser.ConfigParser(interpolation=None)
    cp.read(paths.iw3_dir / MARKER, encoding="utf-8")
    return cp.get("setup", "variant", fallback="")
