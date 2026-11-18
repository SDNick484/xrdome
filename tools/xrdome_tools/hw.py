"""Hardware detection: pure parsers over recorded tool output, probes, encoder choice."""

import dataclasses
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


def _probe_compute(runner: Runner) -> str:
    py = config.paths().iw3_dir / "venv" / "bin" / "python"
    if not py.exists():
        return "not set up"
    code = ("import torch;print(torch.cuda.is_available());"
            "print(torch.version.cuda);print(torch.version.hip)")
    out = _try(runner, [str(py), "-c", code], timeout=60.0)
    lines = (out or "").split()
    if len(lines) < 3 or lines[0] != "True":
        return "none"
    return "rocm" if lines[2] != "None" else "cuda"


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

    return Probe(platform, gpus, built, ok, vaapi, vulkan,
                 _probe_compute(runner) if compute else "not set up")


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
