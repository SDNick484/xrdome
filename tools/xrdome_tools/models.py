"""Model registry: where each weight file comes from, where it goes, and its hash.

No weights ship with xrdome. A download only ever happens through `models --fetch`,
never as a side effect of another command. Only permissively licensed models are
accepted, even if the file is already on disk.
"""

import argparse
import hashlib
import urllib.request
from dataclasses import dataclass
from pathlib import Path

from .config import Paths, paths
from .errors import ToolError
from .run import Runner

ALLOWED_LICENCES = {"MIT", "BSD-2-Clause", "BSD-3-Clause", "Apache-2.0"}

_CKPT = "iw3/nunif/iw3/pretrained_models/hub/checkpoints"
_IW3 = "iw3/nunif/iw3/pretrained_models"


@dataclass(frozen=True)
class Model:
    name: str
    used_by: str
    licence: str
    page: str
    url: str
    revision: str
    file: str
    sha256: str  # full hex digest; a shorter value is a prefix (see _hash_matches)
    dest: str  # relative to Paths.data_dir
    fetched_by: str = ""  # non-empty: another tool downloads it; we only check it


def _hf(repo: str, revision: str, file: str) -> tuple[str, str]:
    return (f"https://huggingface.co/{repo}/tree/{revision}",
            f"https://huggingface.co/{repo}/resolve/{revision}/{file}")


_ANY_PAGE, _ANY_URL = _hf("depth-anything/Depth-Anything-V2-Small",
                          "03876f8651c73a60fe4c2c48294e09fcb6838fcf", "depth_anything_v2_vits.pth")
_VDA_PAGE, _VDA_URL = _hf("depth-anything/Video-Depth-Anything-Small",
                          "256875362cff76724b920335dfb4b29dd611f66e", "video_depth_anything_vits.pth")
_NUNIF = "https://github.com/nagadomi/nunif"

# Append one Model(...) per entry at the end (keep the trailing comma).
REGISTRY: list[Model] = [
    Model(
        name="Any_V2_S", used_by="to3d", licence="Apache-2.0",
        page=_ANY_PAGE, url=_ANY_URL,
        revision="03876f8651c73a60fe4c2c48294e09fcb6838fcf",
        file="depth_anything_v2_vits.pth",
        sha256="715fade13be8f229f8a70cc02066f656f2423a59effd0579197bbf57860e1378",
        dest=_CKPT,
    ),
    Model(
        name="VDA_S", used_by="to3d", licence="Apache-2.0",
        page=_VDA_PAGE, url=_VDA_URL,
        revision="256875362cff76724b920335dfb4b29dd611f66e",
        file="video_depth_anything_vits.pth",
        sha256="13379300b739e659f076a59d52e9801bd8d38c541a7e71f73bbca4dcfb013609",
        dest=_CKPT,
    ),
    Model(
        name="iw3_row_flow_v3", used_by="to3d", licence="MIT",
        page=_NUNIF, url="", revision="",
        file="iw3_row_flow_v3_20250627.pth", sha256="2e4daa14",
        dest=_IW3, fetched_by="iw3",
    ),
    Model(
        name="iw3_depth_aa", used_by="to3d", licence="MIT",
        page=_NUNIF, url="", revision="",
        file="iw3_depth_aa_20250530.pth", sha256="5a36c8eb",
        dest=_IW3, fetched_by="iw3",
    ),
    Model(
        name="transnetv2", used_by="to3d", licence="MIT",
        page=_NUNIF, url="", revision="",
        file="transnetv2-pytorch-weights.pth", sha256="5e896c00",
        dest=_IW3, fetched_by="iw3",
    ),
]


def target(m: Model, p: Paths) -> Path:
    return p.data_dir / m.dest / m.file


def _sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def _hash_matches(m: Model, digest: str) -> bool:
    return bool(m.sha256) and digest.startswith(m.sha256.lower())


def status(m: Model, p: Paths) -> str:
    """"ok", "missing" or "bad hash"."""
    t = target(m, p)
    if not t.is_file():
        return "missing"
    return "ok" if _hash_matches(m, _sha256(t)) else "bad hash"


def instructions(m: Model, p: Paths) -> str:
    lines = [
        f"{m.name} ({m.licence}, used by {m.used_by}) is not installed or does not match.",
        f"  page:   {m.page}",
    ]
    if m.fetched_by:
        lines.append(f"  {m.fetched_by} downloads it on first use, to {target(m, p)}")
    else:
        lines += [f"  file:   {m.file}", f"  sha256: {m.sha256}",
                  f"  put it at: {target(m, p)}"]
    lines.append("  check with: xrdome-tools models --check"
                 + ("" if m.fetched_by else f"; or download it with: xrdome-tools models --fetch {m.name}"))
    return "\n".join(lines)


def find(name: str) -> Model | None:
    return next((m for m in REGISTRY if m.name == name), None)


def require(name: str, p: Paths) -> Path:
    """The path of an installed, allowed model, or a ToolError with instructions. Never downloads."""
    m = find(name)
    if m is None or m.licence not in ALLOWED_LICENCES:
        raise ToolError(f"{name}: not an allowed model (allowed licences: "
                        f"{', '.join(sorted(ALLOWED_LICENCES))})")
    if status(m, p) != "ok":
        raise ToolError(instructions(m, p))
    return target(m, p)


def fetch(runner: Runner, m: Model, p: Paths) -> Path:
    """Download one model, verify its sha256, and move it into place."""
    if m.licence not in ALLOWED_LICENCES:
        raise ToolError(f"{m.name}: not an allowed model")
    if m.fetched_by or not m.url:
        raise ToolError(f"{m.name}: downloaded by {m.fetched_by or 'its own tool'}, not by xrdome-tools\n"
                        + instructions(m, p))
    if len(m.sha256) != 64:
        raise ToolError(f"{m.name}: no full sha256 in the registry; refusing to download")
    t = target(m, p)
    if runner.dry_run:
        print(f"would download {m.url} to {t}")
        return t
    t.parent.mkdir(parents=True, exist_ok=True)
    part = t.with_name(t.name + ".part")
    h = hashlib.sha256()
    try:
        with urllib.request.urlopen(m.url, timeout=60) as r, open(part, "wb") as f:
            for block in iter(lambda: r.read(1 << 20), b""):
                h.update(block)
                f.write(block)
        if h.hexdigest() != m.sha256.lower():
            raise ToolError(f"{m.name}: sha256 mismatch (got {h.hexdigest()}); download discarded")
        part.replace(t)
    except BaseException as e:
        part.unlink(missing_ok=True)
        if isinstance(e, OSError):
            raise ToolError(f"{m.name}: download failed: {e}")
        raise
    return t


def unknown_files(p: Paths) -> list[Path]:
    """.pth files in a registry folder that no registry entry names."""
    known = {target(m, p) for m in REGISTRY}
    found: list[Path] = []
    for d in sorted({(p.data_dir / m.dest) for m in REGISTRY}):
        if d.is_dir():
            found += [f for f in sorted(d.glob("*.pth")) if f not in known]
    return found


def doctor_section(_probe) -> list[tuple[str, str]]:
    p = paths()
    return [(f"model {m.name}", status(m, p)) for m in REGISTRY]


def add_parser(sub: argparse._SubParsersAction) -> None:
    ap = sub.add_parser("models", help="list model weights, check them, optionally fetch them")
    ap.add_argument("--check", action="store_true", help="exit 2 if any model is missing or has a bad hash")
    ap.add_argument("--fetch", nargs="*", metavar="NAME",
                    help="download the named models (all fetchable ones if none named); never automatic")


def run(args: argparse.Namespace) -> int:
    p = paths()
    runner = Runner()
    if args.fetch is not None:
        names = args.fetch or [m.name for m in REGISTRY if not m.fetched_by]
        for n in names:
            m = find(n)
            if m is None:
                raise ToolError(f"{n}: not an allowed model")
            if status(m, p) == "ok":
                print(f"{m.name}: already ok")
                continue
            print(f"{m.name}: downloading {m.url}")
            print(f"{m.name}: ok, {fetch(runner, m, p)}")
        return 0
    bad = 0
    for m in REGISTRY:
        s = status(m, p)
        print(f"{m.name:<16} {s:<9} {m.licence:<11} {target(m, p)}")
        if s != "ok":
            bad += 1
            print(instructions(m, p))
    for f in unknown_files(p):
        print(f"warning: {f} is not a known model")
    return 2 if (args.check and bad) else 0
