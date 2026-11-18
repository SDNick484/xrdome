"""Chunked PNG frame folders for the ncnn tools (rife-ncnn-vulkan, realesrgan-ncnn-vulkan).

The ncnn programs read and write folders of images, so a video goes through them a
chunk at a time: decode a few seconds to PNG, run the tool(s), hand the result on,
delete the folders. Only one chunk's frames are on disk at once, and a failure or
an interrupt removes every folder this module made.
"""

import os
import shutil
from dataclasses import dataclass
from fractions import Fraction
from pathlib import Path
from typing import Callable, Iterator, Sequence

from . import config
from .run import Runner, safe_arg

BYTES_PER_PIXEL = 3  # PNG rgb24, uncompressed: an upper bound for the file size

Step = Callable[[Path, Path], list[str]]
Extract = Callable[[Path, int, int, Path], list[str]]


@dataclass(frozen=True)
class Chunk:
    index: int
    start_frame: int
    count: int
    dir_in: Path
    dir_out: Path


def plan_chunks(total_frames: int, fps: tuple[int, int], seconds: float,
                overlap: int) -> list[tuple[int, int]]:
    """(start, count) per chunk; consecutive chunks share `overlap` frames.

    With overlap 1 every pair of neighbouring frames (i, i+1) lies inside one chunk,
    so the interpolation between them is never lost at a chunk boundary.
    """
    if total_frames <= 0:
        return []
    per = max(1, round(seconds * fps[0] / fps[1]))
    if per <= overlap:
        per = overlap + 1
    out: list[tuple[int, int]] = []
    start = 0
    while True:
        count = min(per, total_frames - start)
        out.append((start, count))
        if start + count >= total_frames:
            return out
        start += count - overlap


def chunk_targets(chunks: list[tuple[int, int]], total_frames: int,
                  ratio: Fraction) -> list[tuple[int, int]]:
    """(frames to ask the tool for, frames to keep) per chunk, for a rate change by `ratio`.

    Output frame j sits at source position j / ratio. Chunk c keeps the output frames
    from round(start_c * ratio) up to the next chunk's first, so the frames at a
    shared (overlap) position are kept once. The last chunk keeps up to
    round(total_frames * ratio). The tool is asked for round(count * ratio) frames
    (rife-ncnn-vulkan spreads them evenly over the chunk), and the few beyond `keep`
    are dropped when the chunk is encoded.
    """
    first = [round(s * ratio) for s, _ in chunks] + [round(total_frames * ratio)]
    out = []
    for c, (_, count) in enumerate(chunks):
        keep = first[c + 1] - first[c]
        out.append((max(round(count * ratio), keep), keep))
    return out


def disk_estimate(width: int, height: int, frames_per_chunk: int, scale: float,
                  fps_factor: float) -> int:
    """Bytes of PNG on disk for one chunk at most: input, interpolated and upscaled frames."""
    frame = width * height * BYTES_PER_PIXEL
    n_out = frames_per_chunk * max(fps_factor, 1.0)
    total = frame * frames_per_chunk
    if fps_factor > 1 and scale > 1:
        total += frame * n_out  # interpolated, before upscaling
    total += frame * scale * scale * n_out
    return int(total)


def pattern(d: Path) -> str:
    """An image2 file pattern in folder d (a '%' in the folder name is escaped)."""
    return os.fspath(d).replace("%", "%%") + "/%08d.png"


def extract_argv(src: Path, start: int, count: int, dst: Path) -> list[str]:
    """Frames start..start+count-1 of src's first video stream as PNG files (simple form).

    `enhance` passes its own extraction that seeks and keeps colours right; this one
    selects by frame number and is used when no other is given.
    """
    return [config.program("ffmpeg"), "-nostdin", "-hide_banner", "-loglevel", "error",
            "-i", safe_arg(src), "-map", "0:v:0",
            "-vf", f"select=between(n\\,{start}\\,{start + count - 1})", "-fps_mode", "passthrough",
            "-frames:v", str(count), "-pix_fmt", "rgb24", pattern(dst)]


def make_chunks(chunks: list[tuple[int, int]], tmp: Path) -> list[Chunk]:
    return [Chunk(i, s, n, tmp / f"{i:05d}.in", tmp / f"{i:05d}.out")
            for i, (s, n) in enumerate(chunks)]


def _rm(p: Path) -> None:
    shutil.rmtree(p, ignore_errors=True)


def run_chunks(runner: Runner, src: Path, chunks: list[tuple[int, int]],
               tool_argv: Step | Sequence[Step], tmp: Path, *,
               extract: Extract = extract_argv) -> Iterator[Path]:
    """Yield each chunk's output folder after the tool(s) ran on it.

    tool_argv(in_dir, out_dir) gives one tool's argv; a sequence of them runs in order,
    each on the previous one's output. Input folders are deleted as soon as they are
    used, the yielded folder when the caller asks for the next. On a failure, an
    interrupt or the caller stopping early, every folder made here is deleted.
    """
    steps: list[Step] = list(tool_argv) if isinstance(tool_argv, (list, tuple)) else [tool_argv]
    made: list[Path] = []

    def mkdir(d: Path) -> Path:
        if not runner.dry_run:
            d.mkdir(parents=True)
        made.append(d)
        return d

    try:
        for ch in make_chunks(chunks, tmp):
            cur = mkdir(ch.dir_in)
            runner.run(extract(src, ch.start_frame, ch.count, cur))
            for k, step in enumerate(steps):
                nxt = mkdir(ch.dir_out if k == len(steps) - 1 else tmp / f"{ch.index:05d}.s{k + 1}")
                runner.run(step(cur, nxt))
                _rm(cur)
                cur = nxt
            yield cur
            _rm(cur)
    finally:
        # Normal end: everything is already gone. Failure, interrupt or close(): clean up.
        for d in made:
            _rm(d)
