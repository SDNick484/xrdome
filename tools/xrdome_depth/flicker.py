#!/usr/bin/env python3
"""flicker.py maps FILE.pgm... | frames DIR [--models-dir DIR]: how much a depth sequence flickers.

Spec 8 of docs/superpowers/specs/2026-11-19-depth-3d-design.md asks for flicker at or below 0.011, "the
spike's flicker metric at 392x224". The spike's scripts/flicker.py defines it (its figures are in the spec,
section 2), and this module measures the same thing:

  - each depth map is min-max normalised to [0, 1] on its own, at the resolution it has;
  - the metric is the mean absolute per-pixel change between consecutive maps (for 30 maps, 29 deltas),
    reported as the mean and the maximum of the deltas;
  - use a near-static shot: motion in the picture is real depth change, not flicker.

`maps` measures depth maps already made (binary 16-bit PGM files, in order: the player's cache format).
`frames` runs the helper's depth path, `xrdome-tools depth flicker`: the model on COUNT consecutive frame
images of DIR (name order, from --start) scaled to 322x182, and reports three sequences, so that the
smoothing's effect is in front of you: the raw model output (the spike's own number), the helper's depth
without smoothing (percentile normalisation only) and with it (what depth serve sends).

The metric itself (measure, minmax) is standard library only, so it is tested without numpy.
"""

import argparse
import json
import os
import sys
from array import array
from dataclasses import dataclass
from typing import Iterable, Sequence

if __package__ in (None, ""):  # started as a script: make xrdome_depth importable, not this folder's files
    sys.path[0] = str(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from xrdome_depth.backend import BackendError

TARGET = 0.011  # spec 8: at or below the spike's number at 392x224
SPIKE_COUNT = 30  # the spike measured 30 consecutive frames


@dataclass
class Flicker:
    deltas: list[float]

    @property
    def mean(self) -> float:
        return sum(self.deltas) / len(self.deltas)

    @property
    def max(self) -> float:
        return max(self.deltas)

    def line(self, name: str) -> str:
        return f"{name}: mean {self.mean:.4f} max {self.max:.4f} over {len(self.deltas)} deltas (0-1 scale)"


def minmax(samples: Sequence[float]) -> list[float]:
    """The samples scaled so the smallest is 0 and the largest 1; all 0 for a flat map (no depth to flicker)."""
    lo, hi = min(samples), max(samples)
    if not hi > lo:
        return [0.0] * len(samples)
    span = hi - lo
    return [(v - lo) / span for v in samples]


def measure(maps: Iterable[Sequence[float]]) -> Flicker:
    """The spike's metric over maps (each a flat sequence of numbers, all the same length, in time order):
    min-max normalise each, then the mean absolute change between consecutive ones. ValueError for fewer
    than two maps or maps of different sizes."""
    deltas: list[float] = []
    prev = None
    for m in maps:
        cur = minmax(m)
        if prev is not None:
            if len(cur) != len(prev):
                raise ValueError(f"maps of different sizes ({len(prev)} and {len(cur)} samples)")
            deltas.append(sum(abs(a - b) for a, b in zip(cur, prev)) / len(cur))
        prev = cur
    if not deltas:
        raise ValueError("flicker needs at least two depth maps")
    return Flicker(deltas)


def read_pgm(path: str) -> array:
    """The samples of a 16-bit binary PGM (the cache format, cache.py) as an array of numbers."""
    with open(path, "rb") as f:
        data = f.read()
    fields: list[bytes] = []
    pos = 0
    while len(fields) < 4:  # "P5", width, height, maxval, each ended by whitespace
        while pos < len(data) and data[pos:pos + 1].isspace():
            pos += 1
        start = pos
        while pos < len(data) and not data[pos:pos + 1].isspace():
            pos += 1
        if start == pos:
            raise ValueError(f"{path}: not a PGM")
        fields.append(data[start:pos])
    pos += 1  # the one whitespace byte after maxval
    if fields[0] != b"P5" or fields[3] != b"65535":
        raise ValueError(f"{path}: not a 16-bit binary PGM")
    width, height = int(fields[1]), int(fields[2])
    body = data[pos:]
    if len(body) != width * height * 2:
        raise ValueError(f"{path}: {len(body)} bytes of samples for {width}x{height}")
    samples = array("H")
    samples.frombytes(body)
    if sys.byteorder == "little":  # the file is big-endian
        samples.byteswap()
    return samples


def report(results: dict[str, Flicker], as_json: bool = False) -> str:
    if as_json:
        return json.dumps({name: {"mean": r.mean, "max": r.max, "deltas": r.deltas}
                           for name, r in results.items()}, indent=1)
    lines = [r.line(name) for name, r in results.items()]
    last = list(results.values())[-1]
    lines.append(f"target at or below {TARGET}: {'met' if last.mean <= TARGET else 'NOT met'} "
                 f"by the last line")
    return "\n".join(lines)


# ---- the helper's depth path over frame images (numpy, OpenCV and the model) -------------------------

def frame_files(directory: str, start: int, count: int) -> list[str]:
    names = sorted(n for n in os.listdir(directory)
                   if os.path.splitext(n)[1].lower() in (".png", ".jpg", ".jpeg", ".bmp", ".webp"))
    picked = names[start:start + count]
    if len(picked) < count:
        raise ValueError(f"{directory}: {len(names)} images, {count} needed from index {start}")
    return [os.path.join(directory, n) for n in picked]


def load_rgba(path: str, width: int, height: int) -> bytes:
    """The image as the player sends a frame: width x height RGBA8, alpha unused."""
    import cv2
    import numpy as np

    bgr = cv2.imread(path)
    if bgr is None:
        raise ValueError(f"{path}: cannot read the image")
    rgb = cv2.cvtColor(cv2.resize(bgr, (width, height), interpolation=cv2.INTER_AREA), cv2.COLOR_BGR2RGB)
    rgba = np.full((height, width, 4), 255, dtype=np.uint8)
    rgba[..., :3] = rgb
    return rgba.tobytes()


def measure_frames(paths: list[str], backend) -> dict[str, Flicker]:
    """Runs the model once per frame and feeds its output to the three measurements."""
    import numpy as np

    from xrdome_depth import video

    plain = video.make_depth_fn(backend, smooth=False)
    smooth = video.make_depth_fn(backend, smooth=True)
    raw_maps, plain_maps, smooth_maps = [], [], []
    for path in paths:
        rgba = load_rgba(path, video.WIDTH, video.HEIGHT)
        raw = plain.infer(rgba, video.WIDTH, video.HEIGHT)
        raw_maps.append(np.asarray(raw, dtype=np.float32).ravel().tolist())
        plain_maps.append(plain.unit(raw, rgba, video.WIDTH, video.HEIGHT).ravel().tolist())
        smooth_maps.append(smooth.unit(raw, rgba, video.WIDTH, video.HEIGHT).ravel().tolist())
    return {"raw model (the spike's number)": measure(raw_maps),
            "helper, not smoothed": measure(plain_maps),
            "helper, smoothed": measure(smooth_maps)}


def main(argv: list[str] | None = None, make_backend=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    what = ap.add_subparsers(dest="what", required=True)
    maps = what.add_parser("maps", help="measure depth maps already made (16-bit PGM files, in time order)")
    maps.add_argument("files", nargs="+", metavar="FILE.pgm")
    frames = what.add_parser("frames", help="run the model on consecutive frame images and measure it")
    frames.add_argument("directory", metavar="DIR")
    frames.add_argument("--models-dir", required=True)
    frames.add_argument("--start", type=int, default=0, help="index of the first image in name order")
    frames.add_argument("--count", type=int, default=SPIKE_COUNT, help=f"frames to use (default {SPIKE_COUNT})")
    frames.add_argument("--cpu", action="store_true", help="run ncnn on the CPU")
    for p in (maps, frames):
        p.add_argument("--json", action="store_true", help="print the result as JSON")
    args = ap.parse_args(argv)

    try:
        if args.what == "maps":
            results = {"maps": measure(read_pgm(f) for f in args.files)}
        else:
            if args.count < 2 or args.start < 0:
                print("flicker: --count must be at least 2 and --start 0 or more", file=sys.stderr)
                return 2
            from xrdome_depth.backend import NcnnBackend

            backend = (make_backend or NcnnBackend)(args.models_dir, vulkan=not args.cpu)
            results = measure_frames(frame_files(args.directory, args.start, args.count), backend)
    except ImportError as e:
        print(f"flicker: cannot import {e.name or e} (the depth venv has numpy and OpenCV; run "
              f"`xrdome-tools setup depth`)", file=sys.stderr)
        return 2
    except (OSError, ValueError) as e:
        print(f"flicker: {e}", file=sys.stderr)
        return 2
    except BackendError as e:
        print(f"flicker: {e}", file=sys.stderr)
        return 2
    print(report(results, args.json))
    return 0


if __name__ == "__main__":
    sys.exit(main())
