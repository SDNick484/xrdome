"""The managed depth environment: a venv for the helper and a temporary one that exports the ncnn models.

Layout under depth_dir(paths):

    venv/                    ncnn, numpy, OpenCV, Pillow (what `depth` runs in)
    export-venv/             PyTorch CPU, transformers, pnnx, huggingface_hub; deleted when the export is done
    export-work/             Hugging Face cache and pnnx scratch files; deleted with export-venv
    export-out/              the export's output until its sha256 are checked; kept when one is wrong
    xrdome-setup.ini         written last by `setup depth`: variant (vulkan or cpu) and pins

The generated models go to models.DAV2S_NCNN_DIR, the verified source weights to models.DAV2S_SOURCE_DIR.
The pins are spec 4 of docs/superpowers/specs/2026-11-19-depth-3d-design.md. The code that runs in the
export venv is tools/xrdome_depth/export.py.
"""

import configparser
import os
import shutil
import subprocess
from pathlib import Path

from . import config, models
from .config import Paths
from .errors import EXIT_SETUP, ToolError
from .run import ChildFailed, Runner

# pip requirements of the depth venv (the versions the spike ran).
VENV_PINS = {
    "ncnn": "1.0.20261020",
    "numpy": "2.5.4",
    "opencv-python-headless": "5.0.0.93",
    "pillow": "13.0.0",
}
# pip requirements of the export venv. torch comes first, from the CPU wheel index.
TORCH_REQUIREMENT = "torch==2.11.0+cpu"
TORCH_INDEX = "https://download.pytorch.org/whl/cpu"
EXPORT_PINS = {
    "transformers": "5.23.0",
    "pnnx": "20261020",
    "safetensors": "0.8.0",
    "huggingface_hub": "2.4.0",
}
MARKER = "xrdome-setup.ini"
EXPORT_SCRIPT = Path(__file__).resolve().parents[1] / "xrdome_depth" / "export.py"
# What `setup depth` downloads, for the message before it starts. Not measured.
DOWNLOAD_SIZE = ("about 1.3 GB, not measured (the temporary export venv with PyTorch about 1 GB, "
                 "deleted afterwards; the depth venv about 0.2 GB; the source model 0.1 GB)")
NO_VULKAN = ("ncnn in the depth environment finds no Vulkan GPU (ncnn.get_gpu_count() is 0). Depth on the "
             "CPU is much slower and takes frames away from xrdome while it renders. "
             "Add --cpu to use the CPU anyway.")


def depth_dir(p: Paths) -> Path:
    return p.data_dir / "depth"


def python(p: Paths) -> Path:
    return depth_dir(p) / "venv" / "bin" / "python"


def export_python(p: Paths) -> Path:
    return depth_dir(p) / "export-venv" / "bin" / "python"


def export_work(p: Paths) -> Path:
    return depth_dir(p) / "export-work"


def export_out(p: Paths) -> Path:
    return depth_dir(p) / "export-out"


def ncnn_dir(p: Paths) -> Path:
    return p.data_dir / models.DAV2S_NCNN_DIR


def source_dir(p: Paths) -> Path:
    return p.data_dir / models.DAV2S_SOURCE_DIR


def _requirements(pins: dict[str, str]) -> list[str]:
    return [f"{name}=={version}" for name, version in pins.items()]


def venv_steps(p: Paths) -> list[list[str]]:
    """The argv of the steps that make the depth venv. Running them again is harmless."""
    venv = depth_dir(p) / "venv"
    pip = [str(python(p)), "-m", "pip", "install", "--quiet"]
    return [
        [config.program("python3", p), "-m", "venv", str(venv)],
        pip + ["--upgrade", "pip"],
        pip + _requirements(VENV_PINS),
    ]


def gpu_step(p: Paths) -> list[str]:
    """Prints how many Vulkan devices ncnn finds."""
    return [str(python(p)), "-c", "import ncnn; print(ncnn.get_gpu_count())"]


def export_steps(p: Paths) -> list[list[str]]:
    """The argv of the steps that make the export venv and run the export in it."""
    venv = depth_dir(p) / "export-venv"
    pip = [str(export_python(p)), "-m", "pip", "install", "--quiet"]
    return [
        [config.program("python3", p), "-m", "venv", str(venv)],
        pip + ["--upgrade", "pip"],
        pip + [TORCH_REQUIREMENT, "--index-url", TORCH_INDEX],
        pip + _requirements(EXPORT_PINS),
        [str(export_python(p)), str(EXPORT_SCRIPT), "--out", str(export_out(p)), "--work", str(export_work(p))],
    ]


def gpu_count(runner: Runner, p: Paths) -> int:
    """How many Vulkan GPUs ncnn in the depth venv finds; 0 when it cannot say."""
    try:
        out = runner.run(gpu_step(p), capture=True, timeout=120).stdout
    except (ChildFailed, OSError, subprocess.TimeoutExpired):
        return 0
    try:
        return int(out.split()[-1])
    except (IndexError, ValueError):
        return 0


def expected() -> dict[str, str]:
    """File name -> sha256 of everything the export must produce (the generated registry entries)."""
    return {m.file: m.sha256 for m in models.REGISTRY if m.generated_by == "setup depth"}


def verify(out: Path) -> None:
    """Check the export's output against the registry's hashes; name every file that is wrong."""
    wrong = []
    for name, digest in expected().items():
        f = out / name
        got = models.sha256_file(f) if f.is_file() else "missing"
        if got != digest:
            wrong.append(f"  {name}: expected {digest}, got {got}")
    src = models.find("Any_V2_S_hf")
    f = out / src.file
    got = models.sha256_file(f) if f.is_file() else "missing"
    if got != src.sha256:
        wrong.append(f"  {src.file} (source weights): expected {src.sha256}, got {got}")
    if wrong:
        raise ToolError("the export does not match the expected sha256 (spec 4):\n" + "\n".join(wrong)
                        + f"\nThe files stay in {out} (the param files are text: diff them against a known good "
                        "export); nothing was installed.", EXIT_SETUP)


def install(p: Paths, out: Path) -> None:
    """Move the verified export into the model folders."""
    for dest, names in ((ncnn_dir(p), list(expected())), (source_dir(p), [models.find("Any_V2_S_hf").file])):
        dest.mkdir(parents=True, exist_ok=True)
        for name in names:
            os.replace(out / name, dest / name)


def remove_export(p: Paths) -> None:
    for d in (depth_dir(p) / "export-venv", export_work(p)):
        shutil.rmtree(d, ignore_errors=True)


def pins() -> dict[str, str]:
    return {**VENV_PINS, **EXPORT_PINS, "torch": TORCH_REQUIREMENT.split("==")[1], "model": models.DAV2S_REVISION}


def write_marker(p: Paths, variant: str) -> None:
    cp = configparser.ConfigParser(interpolation=None)
    cp["setup"] = {"variant": variant}
    cp["pins"] = pins()
    with open(depth_dir(p) / MARKER, "w", encoding="utf-8") as f:
        cp.write(f)


def installed(p: Paths) -> str | None:
    """Why the environment is not usable, or None when setup finished with today's pins."""
    m = depth_dir(p) / MARKER
    if not m.is_file() or not python(p).exists():
        return f"depth is not set up in {depth_dir(p)}"
    cp = configparser.ConfigParser(interpolation=None)
    try:
        cp.read(m, encoding="utf-8")
    except configparser.Error:
        return f"{m} is unreadable"
    have = dict(cp["pins"]) if cp.has_section("pins") else {}
    if have != {k.lower(): v for k, v in pins().items()}:  # configparser lowercases keys
        return f"depth in {depth_dir(p)} was set up with other pins"
    missing = [m.name for m in models.REGISTRY if m.generated_by == "setup depth" and models.status(m, p) != "ok"]
    if missing:
        return f"depth models missing or changed: {', '.join(missing)}"
    return None


def variant(p: Paths) -> str:
    cp = configparser.ConfigParser(interpolation=None)
    cp.read(depth_dir(p) / MARKER, encoding="utf-8")
    return cp.get("setup", "variant", fallback="")
