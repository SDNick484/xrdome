"""The depth cache of spec 5 (docs/superpowers/specs/2026-11-19-depth-3d-design.md): key and PGM file.

Standard library only, so the contract can be tested without numpy. The player (C) computes the same key and
reads the same format:

  <cache dir>/<sha256 of the photo file's bytes>-dav2s-v1.pgm
  binary PGM: "P5\\n<W> <H>\\n65535\\n", then W*H big-endian 16-bit samples, row 0 = top.
"""

import hashlib
import os
from pathlib import Path

SUFFIX = "-dav2s-v1.pgm"
MAXVAL = 65535


def cache_key(path: Path | str) -> str:
    """sha256 of the photo file's bytes, as 64 lowercase hex digits."""
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def cache_path(cache_dir: Path, key: str) -> Path:
    return Path(cache_dir) / f"{key}{SUFFIX}"


def pgm_header(width: int, height: int) -> bytes:
    return f"P5\n{width} {height}\n{MAXVAL}\n".encode("ascii")


def write_pgm(path: Path, width: int, height: int, samples: bytes) -> None:
    """Write `samples` (width*height big-endian uint16, row 0 first) to path, through path + ".part".

    The rename is the last step, so a reader never sees a half-written file.
    """
    if width < 1 or height < 1:
        raise ValueError(f"bad size {width}x{height}")
    if len(samples) != 2 * width * height:
        raise ValueError(f"{len(samples)} bytes for {width}x{height} 16-bit samples "
                         f"(need {2 * width * height})")
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    part = path.with_name(path.name + ".part")
    try:
        with open(part, "wb") as f:
            f.write(pgm_header(width, height))
            f.write(samples)
        os.replace(part, path)
    except BaseException:
        part.unlink(missing_ok=True)
        raise
