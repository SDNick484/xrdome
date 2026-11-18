"""`doctor`: what hardware was found and which path each subcommand would take."""

import argparse
import json
from typing import Callable

from . import hw
from .errors import ToolError
from .run import Runner, say_path

Section = Callable[[hw.Probe], list[tuple[str, str]]]


def _platform(p: hw.Probe) -> list[tuple[str, str]]:
    return [("platform", p.platform), ("gpus", ", ".join(p.gpus) or "none found")]


def _encoders(p: hw.Probe) -> list[tuple[str, str]]:
    rows = []
    for name in hw.KNOWN_ENCODERS:
        if name not in p.encoders_built:
            state = "not built"
        else:
            state = p.encoders_ok.get(name, "built")
        rows.append((f"encoder {name}", state))
    return rows


def _vaapi(p: hw.Probe) -> list[tuple[str, str]]:
    if p.vaapi is None:
        return [("vaapi", "no render node")]
    v = p.vaapi
    return [("vaapi", v.device),
            ("vaapi encode", ", ".join(sorted(v.encode)) or "none"),
            ("vaapi decode", ", ".join(sorted(v.decode)) or "none")]


def _vulkan(p: hw.Probe) -> list[tuple[str, str]]:
    return [("vulkan", ", ".join(p.vulkan_devices) or "none found")]


def _compute(p: hw.Probe) -> list[tuple[str, str]]:
    return [("to3d compute", p.compute)]


# Append one entry per line (keep the trailing comma).
SECTIONS: list[Section] = [
    _platform,
    _encoders,
    _vaapi,
    _vulkan,
    _compute,
]


def decisions(p: hw.Probe) -> dict[str, dict[str, str]]:
    """The path each subcommand would take, as {subcommand: {"value", "reason"}}."""
    out: dict[str, dict[str, str]] = {}
    try:
        c = hw.choose_encoder(p, "hevc")
        out["transcode"] = {"value": f"encode={c.name}", "reason": c.reason}
    except ToolError as e:
        out["transcode"] = {"value": "encode=none", "reason": str(e)}
    if p.compute in ("cuda", "rocm"):
        out["to3d"] = {"value": f"compute={p.compute}", "reason": "found in the iw3 environment"}
    elif p.compute == "not set up":
        out["to3d"] = {"value": "compute=none", "reason": "iw3 is not set up; run `xrdome-tools setup to3d`"}
    else:
        out["to3d"] = {"value": "compute=none", "reason": "no CUDA or ROCm in the iw3 environment; needs --cpu"}
    real = [d for d in p.vulkan_devices if "llvmpipe" not in d.lower()]
    if real:
        out["enhance"] = {"value": f"vulkan={real[0]}", "reason": "first Vulkan GPU"}
    else:
        out["enhance"] = {"value": "vulkan=none", "reason": "no Vulkan GPU found"}
    return out


def to_json(p: hw.Probe) -> dict:
    return {
        "platform": p.platform,
        "gpus": p.gpus,
        "encoders": {"built": sorted(p.encoders_built), "tested": dict(sorted(p.encoders_ok.items()))},
        "vaapi": None if p.vaapi is None else {
            "device": p.vaapi.device,
            "encode": sorted(p.vaapi.encode),
            "decode": sorted(p.vaapi.decode),
        },
        "vulkan": p.vulkan_devices,
        "compute": p.compute,
        "paths": decisions(p),
    }


def add_parser(sub: argparse._SubParsersAction) -> None:
    ap = sub.add_parser("doctor", help="show detected hardware and the path each command would take")
    ap.add_argument("--json", action="store_true", help="print JSON, for bug reports")


def run(args: argparse.Namespace) -> int:
    p = hw.probe(Runner())
    if args.json:
        print(json.dumps(to_json(p), indent=2))
        return 0
    rows = [row for section in SECTIONS for row in section(p)]
    width = max(len(k) for k, _ in rows)
    for k, v in rows:
        print(f"{k:<{width}}  {v}")
    print()
    for key, d in decisions(p).items():
        k, _, v = d["value"].partition("=")
        say_path(k, v, f"{key}: {d['reason']}")
    return 0
