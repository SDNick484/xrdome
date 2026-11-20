"""Hardware detection: pure parsers over recorded tool output, probes, encoder choice."""

import dataclasses
import os
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path

from . import config
from .errors import EXIT_SETUP, ToolError
from .run import ChildFailed, Runner

HW_ENCODERS = ("hevc_nvenc", "av1_nvenc", "hevc_vaapi", "av1_vaapi")
KNOWN_ENCODERS = HW_ENCODERS + ("libx265", "libsvtav1")
DEFAULT_RENDER_NODE = "/dev/dri/renderD128"
NVIDIA_SMI_WSL = "/usr/lib/wsl/lib/nvidia-smi"
TEST_TIMEOUT = 20.0
TORCH_TEST_TIMEOUT = 120.0

# What PyTorch sees: available, CUDA version, HIP version, the ROCm chip name ("-" otherwise).
TORCH_SEES = ("import torch;print(torch.cuda.is_available());"
              "print(torch.version.cuda);print(torch.version.hip);"
              "print(torch.cuda.get_device_properties(0).gcnArchName "
              "if torch.version.hip and torch.cuda.is_available() else '-')")
# A few real kernels of the kinds iw3's depth models run (fp16 conversion, convolution,
# attention). Seeing the GPU is not enough: a ROCm wheel built without code for the chip
# fails here with "invalid device function", or aborts with HSA_STATUS_ERROR_INVALID_ISA
# under an override for the wrong chip.
TORCH_KERNELS = ("import torch;import torch.nn.functional as F;d='cuda';"
                 "x=torch.randn(1,3,64,64,device=d).half();"
                 "y=F.conv2d(x,torch.randn(8,3,3,3,device=d).half(),padding=1);"
                 "q=torch.randn(1,6,1024,64,device=d,dtype=torch.half);"
                 "z=F.scaled_dot_product_attention(q,q,q);"
                 "torch.cuda.synchronize();print('ok',float(y.float().mean()+z.float().mean()))")
# ROCm chips PyTorch's ROCm wheels have no code for, and the HSA_OVERRIDE_GFX_VERSION that
# made iw3 work on them. gfx1152 (Radeon 860M): 11.0.0 and 11.0.1 abort with
# HSA_STATUS_ERROR_INVALID_ISA, 11.0.2 works (torch 2.9.1+rocm6.4, measured 2026-11-20,
# about 2.4 frames/s with Any_V2_S on a 608x1080 source).
ROCM_OVERRIDES = {"gfx1152": "11.0.2"}

_VENDORS = {"0x10de": "nvidia", "0x1002": "amd", "0x8086": "intel"}


@dataclass(frozen=True)
class VaapiInfo:
    device: str
    encode: set[str]
    decode: set[str]


@dataclass(frozen=True)
class Probe:
    platform: str
    gpus: list[str]
    encoders_built: set[str]
    encoders_ok: dict[str, str]
    vaapi: VaapiInfo | None
    vulkan_devices: list[str]
    compute: str
    compute_problem: str = ""


@dataclass(frozen=True)
class TorchCheck:
    compute: str  # "cuda", "rocm" or "none": a GPU PyTorch can run iw3 on
    seen: str  # what torch.cuda.is_available() reports: "cuda", "rocm" or "none"
    arch: str  # the ROCm chip (gfx1152), "" otherwise
    problem: str = ""  # why a GPU PyTorch sees cannot be used; "" when it can


@dataclass(frozen=True)
class EncoderChoice:
    name: str
    family: str
    reason: str


# ---- parsers (pure) ----

def _lines(text: str) -> list[str]:
    return [ln for ln in text.splitlines() if not ln.lstrip().startswith("#")]


def parse_proc_version(text: str) -> bool:
    return "microsoft" in text.lower()


def parse_drm_vendors(vendor_ids: list[str]) -> list[str]:
    out: list[str] = []
    for v in vendor_ids:
        name = _VENDORS.get(v.strip().lower())
        if name and name not in out:
            out.append(name)
    return out


def parse_ffmpeg_encoders(text: str) -> set[str]:
    found = set()
    for ln in _lines(text):
        m = re.match(r"^\s*[VAS][.A-Z]{5}\s+(\S+)", ln)
        if m:
            found.add(m.group(1))
    return found


def parse_vainfo(text: str) -> VaapiInfo:
    """Profiles by entrypoint. The device is the caller's to fill in."""
    encode: set[str] = set()
    decode: set[str] = set()
    for ln in _lines(text):
        m = re.match(r"^\s*(VAProfile\w+)\s*:\s*(VAEntrypoint\w+)", ln)
        if not m:
            continue
        profile, entry = m.groups()
        if entry == "VAEntrypointVLD":
            decode.add(profile)
        elif entry.startswith("VAEntrypointEnc"):
            encode.add(profile)
    return VaapiInfo("", encode, decode)


def parse_vulkaninfo_summary(text: str) -> list[str]:
    return [m.group(1).strip() for ln in _lines(text)
            if (m := re.match(r"^\s*deviceName\s*=\s*(.+)$", ln))]


def parse_nvidia_smi_list(text: str) -> list[str]:
    return [m.group(1).strip() for ln in _lines(text)
            if (m := re.match(r"^GPU \d+:\s*(.+?)\s*\(UUID:", ln))]


# ---- probes ----

def encode_test_argv(ffmpeg: str, encoder: str, device: str = DEFAULT_RENDER_NODE) -> list[str]:
    """The 1-second 256x256 test encode that shows whether an encoder really works."""
    argv = [ffmpeg, "-hide_banner", "-loglevel", "error",
            "-f", "lavfi", "-i", "testsrc2=s=256x256:d=1"]
    if encoder.endswith("_vaapi"):
        argv += ["-vaapi_device", device, "-vf", "format=nv12,hwupload"]
    return argv + ["-c:v", encoder, "-f", "null", "-"]


def _try(runner: Runner, argv: list[str], timeout: float = 10.0) -> str | None:
    """stdout of a probe, or None when the program is missing or fails."""
    try:
        return runner.run(argv, capture=True, timeout=timeout).stdout
    except (ChildFailed, OSError, subprocess.TimeoutExpired):
        return None


def _read(path: str) -> str:
    try:
        return Path(path).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def _test_encoder(runner: Runner, ffmpeg: str, encoder: str, device: str) -> str:
    try:
        runner.run(encode_test_argv(ffmpeg, encoder, device), capture=True, timeout=TEST_TIMEOUT)
    except ChildFailed as e:
        lines = [ln.strip() for ln in e.stderr_tail if ln.strip()]
        return lines[-1] if lines else f"exited with status {e.returncode}"
    except subprocess.TimeoutExpired:
        return f"timed out after {TEST_TIMEOUT:g} s"
    except OSError as e:
        return str(e)
    return "ok"


def parse_torch_sees(out: str) -> tuple[str, str]:
    """("cuda" | "rocm" | "none", ROCm chip or "") from TORCH_SEES's output."""
    words = out.split()
    if len(words) < 3 or words[0] != "True":
        return "none", ""
    if words[2] == "None":
        return "cuda", ""
    arch = words[3].split(":")[0] if len(words) > 3 and words[3] != "-" else ""
    return "rocm", arch


def parse_kfd_target(properties: str) -> str:
    """The chip name ("gfx1152") from a KFD topology node's properties, or "" for a CPU node.

    The kernel reports the real chip here, also when HSA_OVERRIDE_GFX_VERSION makes ROCm (and
    PyTorch) report another one: gfx_target_version 110502 is gfx1152, 90010 is gfx90a.
    """
    m = re.search(r"^gfx_target_version\s+(\d+)$", properties, re.M)
    v = int(m.group(1)) if m else 0
    if not v:
        return ""
    return f"gfx{v // 10000}{(v // 100) % 100:x}{v % 100:x}"


def _kfd_arch() -> str:
    for node in sorted(Path("/sys/class/kfd/kfd/topology/nodes").glob("*/properties")):
        arch = parse_kfd_target(_read(str(node)))
        if arch:
            return arch
    return ""


def torch_error_line(stderr_tail: list[str], returncode: int) -> str:
    """The line of a failed kernel test that says what went wrong."""
    lines = [ln.strip() for ln in stderr_tail if ln.strip()]
    for ln in lines:
        if "Error" in ln or "error" in ln:
            return ln.split("aborting with error : ", 1)[-1][:200]  # drop ROCm's log prefix
    return lines[-1][:200] if lines else f"exited with status {returncode}"


def torch_problem(seen: str, arch: str, error: str, override: str) -> str:
    """Why a GPU PyTorch sees failed its kernel test, and what to do about it."""
    if seen == "cuda":
        return (f"PyTorch sees the NVIDIA GPU but cannot run on it ({error}). Check the driver "
                "(nvidia-smi), or add --cpu.")
    chip = arch or "this GPU"
    known = ROCM_OVERRIDES.get(arch)
    if override:
        hint = (f"; {chip} works with HSA_OVERRIDE_GFX_VERSION={known}" if known and known != override
                else "")
        return (f"PyTorch's ROCm build cannot run on {chip} with HSA_OVERRIDE_GFX_VERSION="
                f"{override} ({error}){hint}. Or add --cpu.")
    if known:
        return (f"PyTorch's ROCm build has no GPU code for {chip} ({error}). Put "
                f"HSA_OVERRIDE_GFX_VERSION={known} in front of the command to run it as a chip "
                "the build has code for, or add --cpu.")
    return (f"PyTorch sees {chip} but cannot run on it ({error}). Its ROCm build may have no GPU "
            "code for this chip; HSA_OVERRIDE_GFX_VERSION set to a supported chip of the same "
            "family sometimes works. Or add --cpu.")


def check_torch(runner: Runner, python: Path) -> TorchCheck:
    """Whether PyTorch in the given Python can run on the GPU, by running a few kernels."""
    out = _try(runner, [str(python), "-c", TORCH_SEES], timeout=TORCH_TEST_TIMEOUT)
    seen, arch = parse_torch_sees(out or "")
    if seen == "rocm":
        arch = _kfd_arch() or arch  # PyTorch names the overridden chip, the kernel the real one
    if seen == "none":
        return TorchCheck("none", "none", "")
    try:
        runner.run([str(python), "-c", TORCH_KERNELS], capture=True, timeout=TORCH_TEST_TIMEOUT)
    except ChildFailed as e:
        error = torch_error_line(e.stderr_tail, e.returncode)
    except subprocess.TimeoutExpired:
        error = f"the GPU test timed out after {TORCH_TEST_TIMEOUT:g} s"
    except OSError as e:
        error = str(e)
    else:
        return TorchCheck(seen, seen, arch)
    override = os.environ.get("HSA_OVERRIDE_GFX_VERSION", "")
    return TorchCheck("none", seen, arch, torch_problem(seen, arch, error, override))


def _probe_compute(runner: Runner) -> TorchCheck | None:
    py = config.paths().iw3_dir / "venv" / "bin" / "python"
    if not py.exists():
        return None
    return check_torch(runner, py)


def probe(runner: Runner, *, encoders: bool = True, compute: bool = True) -> Probe:
    wsl = parse_proc_version(_read("/proc/version")) or Path("/dev/dxg").exists()
    platform = "wsl2" if wsl else "linux"

    vendor_ids = [_read(str(p)) for p in sorted(Path("/sys/class/drm").glob("card*/device/vendor"))]
    gpus = parse_drm_vendors(vendor_ids)
    if wsl and "nvidia" not in gpus:
        for exe in (config.program("nvidia-smi"), NVIDIA_SMI_WSL):
            out = _try(runner, [exe, "-L"])
            if out and parse_nvidia_smi_list(out):
                gpus.append("nvidia")
                break

    nodes = sorted(str(p) for p in Path("/dev/dri").glob("renderD*"))
    vaapi: VaapiInfo | None = None
    if nodes:
        dev = nodes[0]
        out = _try(runner, [config.program("vainfo"), "--display", "drm", "--device", dev])
        info = parse_vainfo(out or "")
        vaapi = dataclasses.replace(info, device=dev)

    out = _try(runner, [config.program("vulkaninfo"), "--summary"])
    vulkan = parse_vulkaninfo_summary(out or "")

    built: set[str] = set()
    ok: dict[str, str] = {}
    if encoders:
        ffmpeg = config.program("ffmpeg")
        out = _try(runner, [ffmpeg, "-hide_banner", "-encoders"])
        built = parse_ffmpeg_encoders(out or "") & set(KNOWN_ENCODERS)
        for enc in HW_ENCODERS:
            if enc not in built:
                continue
            if enc.endswith("_vaapi") and vaapi is None:
                ok[enc] = "no /dev/dri render node"
                continue
            ok[enc] = _test_encoder(runner, ffmpeg, enc, vaapi.device if vaapi else DEFAULT_RENDER_NODE)

    tc = _probe_compute(runner) if compute else None
    return Probe(platform, gpus, built, ok, vaapi, vulkan,
                 tc.compute if tc else "not set up", tc.problem if tc else "")


# ---- encoder choice ----

def choose_encoder(p: Probe, codec: str, override: str | None = None) -> EncoderChoice:
    if codec not in ("hevc", "av1"):
        raise ToolError(f"unknown codec {codec!r}", EXIT_SETUP)
    nv, va = f"{codec}_nvenc", f"{codec}_vaapi"
    sw = "libx265" if codec == "hevc" else "libsvtav1"

    def family(name: str) -> str:
        return "nvenc" if name.endswith("_nvenc") else "vaapi" if name.endswith("_vaapi") else "software"

    if override is not None:
        if override not in p.encoders_built:
            raise ToolError(f"--encoder {override}: not built into this ffmpeg", EXIT_SETUP)
        status = p.encoders_ok.get(override, "ok")
        if status != "ok":
            raise ToolError(f"--encoder {override}: its test encode failed: {status}", EXIT_SETUP)
        return EncoderChoice(override, family(override), "requested with --encoder")

    skipped: list[str] = []
    for name, label in ((nv, "NVENC"), (va, "VA-API")):
        if name not in p.encoders_built:
            continue
        status = p.encoders_ok.get(name, "not tested")
        if status == "ok":
            prefix = "; ".join(skipped) + "; " if skipped else ""
            return EncoderChoice(name, family(name), f"{prefix}{label} test encode passed")
        skipped.append(f"{name} is built in but its test encode failed ({status})")
    if sw not in p.encoders_built:
        raise ToolError(f"no usable {codec} encoder: ffmpeg has neither a working hardware "
                        f"encoder nor {sw}", EXIT_SETUP)
    prefix = "; ".join(skipped) + "; " if skipped else ""
    return EncoderChoice(sw, "software", f"{prefix}no working hardware encoder, software encoding is slow")
