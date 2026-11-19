#!/usr/bin/env python3
"""photo.py [--force] --cache-dir DIR --models-dir DIR [--cpu] FILE|DIR: write the depth cache for photos.

Runs in the depth venv, started by `xrdome-tools depth photo`. Spec 4 and 5 of
docs/superpowers/specs/2026-11-19-depth-3d-design.md are the contract:

  - EXIF orientation is applied first (the "displayed" photo); the model size follows the displayed
    orientation: 518x392 landscape (width >= height), 392x518 portrait; RGB, bicubic resize (aspect not
    kept), /255, ImageNet mean and std;
  - the model's inverse depth is normalised with its 1st and 99th percentiles to 0 and 1 (clipped), upsampled
    bilinearly to the displayed size capped at 2048 on the long side, and written as 16-bit PGM (cache.py).

numpy, PIL and OpenCV are imported inside the functions that need them, so the size and file rules can be
tested without them.
"""

import argparse
import sys
import time
from pathlib import Path

if __package__ in (None, ""):  # started as a script: make xrdome_depth importable, not this folder's files
    sys.path[0] = str(Path(__file__).resolve().parents[1])

from xrdome_depth import cache
from xrdome_depth.backend import Backend, BackendError, NcnnBackend

LANDSCAPE = (518, 392)
PORTRAIT = (392, 518)
MAX_LONG_SIDE = 2048
PERCENTILES = (1.0, 99.0)
MEAN = (0.485, 0.456, 0.406)
STD = (0.229, 0.224, 0.225)
EXTENSIONS = {".jpg", ".jpeg", ".png", ".webp", ".bmp", ".tif", ".tiff"}


def model_size(width: int, height: int) -> tuple[int, int]:
    """(W, H) of the model input for a photo as displayed. Square photos take the landscape model."""
    return LANDSCAPE if width >= height else PORTRAIT


def output_size(width: int, height: int, cap: int = MAX_LONG_SIDE) -> tuple[int, int]:
    """The size of the depth file: the displayed size, shrunk to `cap` on the long side keeping aspect."""
    long_side = max(width, height)
    if long_side <= cap:
        return width, height
    scale = cap / long_side
    return max(1, round(width * scale)), max(1, round(height * scale))


def list_photos(path: Path) -> list[Path]:
    """A file as it is; a folder's image files (by extension) in name order, not recursive."""
    if path.is_dir():
        return sorted((f for f in path.iterdir() if f.is_file() and f.suffix.lower() in EXTENSIONS),
                      key=lambda f: f.name)
    return [path]


def resize(img, width: int, height: int, cubic: bool):
    """Resize an H x W (x C) array to width x height with OpenCV: bicubic or bilinear."""
    import cv2

    return cv2.resize(img, (width, height), interpolation=cv2.INTER_CUBIC if cubic else cv2.INTER_LINEAR)


def load_displayed(path: Path):
    """The photo as an H x W x 3 uint8 RGB array, with its EXIF orientation applied."""
    import numpy as np
    from PIL import Image, ImageOps

    with Image.open(path) as im:
        im = ImageOps.exif_transpose(im)
        return np.asarray(im.convert("RGB"))


def preprocess(rgb, width: int, height: int):
    """Resize to the model size and normalise: float32, 3 x height x width."""
    import numpy as np

    x = resize(rgb, width, height, cubic=True).astype(np.float32) / 255.0
    x = (x - np.array(MEAN, dtype=np.float32)) / np.array(STD, dtype=np.float32)
    return np.ascontiguousarray(x.transpose(2, 0, 1), dtype=np.float32)


def normalise(depth):
    """Map the 1st and 99th percentiles to 0 and 1, clipped (float32). A flat depth has no relative
    information: it becomes 0.5, which sits on the default screen plane."""
    import numpy as np

    if not np.isfinite(depth).all():
        raise BackendError("the model returned a depth with NaN or infinite values")
    lo, hi = np.percentile(depth, PERCENTILES)
    if not hi > lo:
        return np.full(depth.shape, 0.5, dtype=np.float32)
    return np.clip((depth - lo) / (hi - lo), 0.0, 1.0).astype(np.float32)


def to_samples(unit):
    """A float array in [0, 1] as uint16 (0..65535)."""
    import numpy as np

    return np.rint(np.clip(unit, 0.0, 1.0) * cache.MAXVAL).astype(np.uint16)


def big_endian(samples) -> bytes:
    """uint16 samples as the bytes of the PGM body, whatever the machine's byte order."""
    import numpy as np

    return np.ascontiguousarray(samples, dtype=">u2").tobytes()


def depth_map(rgb, backend: Backend):
    """Depth for a displayed photo: uint16, output_size of the photo, larger = nearer."""
    height, width = rgb.shape[:2]
    mw, mh = model_size(width, height)
    unit = normalise(backend.infer(preprocess(rgb, mw, mh), mw, mh))
    ow, oh = output_size(width, height)
    if (ow, oh) != (mw, mh):
        unit = resize(unit, ow, oh, cubic=False)
    return to_samples(unit)


def process(path: Path, cache_dir: Path, backend: Backend, force: bool = False) -> tuple[Path, str]:
    """Make the cache file for one photo; returns (cache file, a line about how it went)."""
    start = time.monotonic()
    key = cache.cache_key(path)
    dest = cache.cache_path(cache_dir, key)
    if dest.is_file() and not force:
        return dest, "cached"
    rgb = load_displayed(path)
    decoded = time.monotonic()
    depth = depth_map(rgb, backend)
    done = time.monotonic()
    height, width = depth.shape
    cache.write_pgm(dest, width, height, big_endian(depth))
    end = time.monotonic()
    return dest, (f"{end - start:.3f} s (decode {decoded - start:.3f}, depth {done - decoded:.3f}, "
                  f"write {end - done:.3f}) {rgb.shape[1]}x{rgb.shape[0]} photo, {width}x{height} depth")


def main(argv: list[str] | None = None, make_backend=NcnnBackend) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("path", type=Path, help="a photo, or a folder of photos")
    ap.add_argument("--cache-dir", required=True, type=Path)
    ap.add_argument("--models-dir", required=True, type=Path)
    ap.add_argument("--cpu", action="store_true", help="run ncnn on the CPU")
    ap.add_argument("--force", action="store_true", help="recompute photos that are already cached")
    args = ap.parse_args(argv)

    photos = list_photos(args.path)
    if not photos:
        print(f"depth photo: no images in {args.path}", file=sys.stderr)
        return 1
    backend = make_backend(args.models_dir, vulkan=not args.cpu)
    failed = 0
    for photo in photos:
        try:
            dest, note = process(photo, args.cache_dir, backend, args.force)
        except BackendError as e:
            print(f"depth photo: {e}", file=sys.stderr)
            return 2
        except Exception as e:  # a bad photo must not stop the rest of the folder
            print(f"{photo.name}: failed: {e}", file=sys.stderr)
            failed += 1
            continue
        print(f"{photo.name}: {note} -> {dest}", flush=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
