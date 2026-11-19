#!/usr/bin/env python3
"""video.py --bench --models-dir DIR [--seconds S] [--rate HZ] [--cpu]: depth for live video.

Spec 8 (first half) of docs/superpowers/specs/2026-11-19-depth-3d-design.md. xrdome copies each new frame
of a flat mono film with 3D on, scaled to 322x182, into a shared-memory ring; `depth serve` maps it (the
"video" op, serve.py) and runs the 322x182 model on the newest frame, up to 30 times a second, writing the
depth back into the ring with the number of the frame it was made from. src/video/framesink.h is the
other side and documents the ring; this module mirrors it:

  header (4096 bytes): magic "XRDVRNG1", version 1, sizes and offsets, the frame head (player), closed
  (player), the depth head (helper), and per slot its number (0 while being written) and a second word;
  then 4 frame slots (RGBA8, rows top first) and 3 depth slots (16-bit, native byte order, 65535 near),
  each on a 4096-byte boundary. Frame n is in slot n % 4; a reader checks the slot's number before and
  after copying (a seqlock), so nobody ever waits for the other side.

Spec 8 also asks for temporal smoothing: an exponential moving average of the depth (weight 0.5 for the
newest map) and of the percentile range it is normalised with, reset on a scene cut. The decisions (the
cut detector, the range average, the weight each map gets) are standard library only, in SceneCut and
Smoothing; DepthPipeline applies them to the model's output with numpy. tools/xrdome_depth/flicker.py
measures the result.

The ring, the pacing, the statistics and the smoothing decisions are standard library only (tested
without numpy); the depth itself needs numpy and the backend, imported where used. `--bench` is
`xrdome-tools depth bench-video`: it feeds the same loop synthetic frames at --rate and reports the Hz it
achieves (the spike's metrics).
"""

import argparse
import mmap
import os
import struct
import sys
import time
from typing import Callable

if __package__ in (None, ""):  # started as a script: make xrdome_depth importable, not this folder's files
    sys.path[0] = str(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

MAGIC = b"XRDVRNG1"
VERSION = 1
WIDTH, HEIGHT = 322, 182
FRAME_SLOTS = 4
DEPTH_SLOTS = 3
HEADER = 4096
PAGE = 4096
FRAME_BYTES = WIDTH * HEIGHT * 4
DEPTH_BYTES = WIDTH * HEIGHT * 2
FORMAT_RGBA8 = 1
FORMAT_U16 = 1
OFF_FRAME_HEAD = 128
OFF_CLOSED = 136
OFF_DEPTH_HEAD = 192
OFF_FRAME_META = 256
OFF_DEPTH_META = 512

MAX_HZ = 30.0
SMOOTH_WEIGHT = 0.5     # weight of the newest depth map (and percentile range) in the moving average
CUT_THRESHOLD = 0.15    # a scene cut: the mean luma change over the sample grid, of full scale, above this
CUT_GRID = (32, 18)     # the luma samples compared between two frames (columns, rows)
POLL_S = 0.004          # how often an idle helper looks at the ring for a new frame
LOG_EVERY_S = 10.0

_HEAD = struct.Struct("=8sIIIIIIQQQQQII")  # magic .. depth format (offsets 0 to 79)


def _round_page(n: int) -> int:
    return (n + PAGE - 1) // PAGE * PAGE


FRAME_STRIDE = _round_page(FRAME_BYTES)
DEPTH_STRIDE = _round_page(DEPTH_BYTES)


def frame_offset(slot: int) -> int:
    return HEADER + slot * FRAME_STRIDE


def depth_offset(slot: int) -> int:
    return frame_offset(FRAME_SLOTS) + slot * DEPTH_STRIDE


SIZE = depth_offset(DEPTH_SLOTS)


def header_fields() -> tuple:
    return (MAGIC, VERSION, HEADER, WIDTH, HEIGHT, FRAME_SLOTS, DEPTH_SLOTS, frame_offset(0), FRAME_STRIDE,
            depth_offset(0), DEPTH_STRIDE, SIZE, FORMAT_RGBA8, FORMAT_U16)


class RingError(Exception):
    """The shared memory is not a ring of this layout."""


class Ring:
    """A ring over a writable buffer of at least SIZE bytes (an mmap, or a bytearray in the tests).

    The 64-bit words both sides touch are read and written whole through a memoryview of unsigned
    64-bit items, aligned, which is one load or store on x86-64 and arm64. Python has no memory fences:
    on x86-64 (the platform this runs on) stores are not reordered with each other, so the seqlock holds;
    elsewhere it is unverified (spec 3: Linux first)."""

    def __init__(self, buf, init: bool = False):
        self.mv = memoryview(buf).cast("B")
        if len(self.mv) < SIZE:
            n = len(self.mv)
            self.mv.release()
            raise RingError(f"{n} bytes, a ring needs {SIZE}")
        self.q = self.mv[:SIZE].cast("Q")
        if init:
            self.mv[:HEADER] = bytes(HEADER)
            _HEAD.pack_into(self.mv, 0, *header_fields())
        else:
            got = _HEAD.unpack_from(self.mv, 0)
            if got != header_fields():
                self.release()
                raise RingError(f"not a version {VERSION} ring of {WIDTH}x{HEIGHT} (header {got[:7]})")

    def release(self) -> None:
        """Lets go of the buffer (an mmap cannot be closed while a view of it lives)."""
        self.q.release()
        self.mv.release()

    # ---- player side (the bench and the tests play it) ----------------------------

    def frame_write(self, n: int, rgba: bytes, t_ns: int = 0) -> None:
        meta = (OFF_FRAME_META + 16 * (n % FRAME_SLOTS)) // 8
        off = frame_offset(n % FRAME_SLOTS)
        self.q[meta] = 0
        self.mv[off:off + FRAME_BYTES] = rgba
        self.q[meta + 1] = t_ns
        self.q[meta] = n
        self.q[OFF_FRAME_HEAD // 8] = n

    def close(self) -> None:
        struct.pack_into("=I", self.mv, OFF_CLOSED, 1)

    # ---- helper side ------------------------------------------------------------------

    @property
    def closed(self) -> bool:
        return struct.unpack_from("=I", self.mv, OFF_CLOSED)[0] != 0

    @property
    def frame_head(self) -> int:
        return self.q[OFF_FRAME_HEAD // 8]

    @property
    def depth_head(self) -> int:
        return self.q[OFF_DEPTH_HEAD // 8]

    def frame_read(self, seen: int) -> tuple[int, int, bytes] | None:
        """(n, t_ns, RGBA bytes) of the newest frame when it is newer than seen; None when there is none
        or the player came round to its slot during the copy."""
        n = self.frame_head
        if n == 0 or n == seen:
            return None
        meta = (OFF_FRAME_META + 16 * (n % FRAME_SLOTS)) // 8
        if self.q[meta] != n:
            return None
        t_ns = self.q[meta + 1]
        off = frame_offset(n % FRAME_SLOTS)
        data = bytes(self.mv[off:off + FRAME_BYTES])
        if self.q[meta] != n:
            return None
        return n, t_ns, data

    def depth_write(self, m: int, frame: int, samples: bytes) -> None:
        """Depth map m (one more than the last) made from frame: WIDTH x HEIGHT native 16-bit samples."""
        if len(samples) != DEPTH_BYTES:
            raise ValueError(f"a depth map is {DEPTH_BYTES} bytes, got {len(samples)}")
        meta = (OFF_DEPTH_META + 16 * (m % DEPTH_SLOTS)) // 8
        off = depth_offset(m % DEPTH_SLOTS)
        self.q[meta] = 0
        self.mv[off:off + DEPTH_BYTES] = samples
        self.q[meta + 1] = frame
        self.q[meta] = m
        self.q[OFF_DEPTH_HEAD // 8] = m

    def depth_read(self, seen: int) -> tuple[int, int, bytes] | None:
        """(m, frame, samples) of the newest depth map when newer than seen (the player's side)."""
        m = self.depth_head
        if m == 0 or m == seen:
            return None
        meta = (OFF_DEPTH_META + 16 * (m % DEPTH_SLOTS)) // 8
        if self.q[meta] != m:
            return None
        frame = self.q[meta + 1]
        off = depth_offset(m % DEPTH_SLOTS)
        data = bytes(self.mv[off:off + DEPTH_BYTES])
        if self.q[meta] != m:
            return None
        return m, frame, data


def valid_name(name: str) -> bool:
    """A POSIX shared-memory name as xrdome makes them: "/" then up to 30 safe characters (macOS's limit
    is 31 in all)."""
    return (isinstance(name, str) and 2 <= len(name) <= 31 and name[0] == "/" and
            all(c.isascii() and (c.isalnum() or c in "-_.") for c in name[1:]))


def open_shm(name: str) -> tuple[Ring, mmap.mmap]:
    """Maps the player's ring and removes its name: the mapping lives on, and nothing is left behind when
    either side exits. OSError when it cannot be opened, RingError when it is not a ring."""
    import _posixshmem  # CPython's own shm_open (what multiprocessing.shared_memory uses), without the
    #                     resource tracker process multiprocessing would start

    fd = _posixshmem.shm_open(name, os.O_RDWR, mode=0o600)
    try:
        mm = mmap.mmap(fd, os.fstat(fd).st_size)
    finally:
        os.close(fd)
    try:
        _posixshmem.shm_unlink(name)
    except OSError:
        pass
    try:
        return Ring(mm), mm
    except RingError:
        mm.close()
        raise


class Stats:
    """Depth maps done: achieved Hz, the worst 1 s window, and inference times (the spike's metrics)."""

    def __init__(self):
        self.done: list[float] = []
        self.infer: list[float] = []

    def add(self, end: float, infer_s: float) -> None:
        self.done.append(end)
        self.infer.append(infer_s)

    def hz(self) -> float:
        if len(self.done) < 2 or self.done[-1] <= self.done[0]:
            return 0.0
        return (len(self.done) - 1) / (self.done[-1] - self.done[0])

    def worst_window_hz(self, window: float = 1.0) -> float | None:
        """The fewest maps finished in any window seconds (ending at a map, from window seconds after the
        first), per second; None for a run shorter than the window."""
        t = self.done
        worst = None
        lo = 0
        for hi, end in enumerate(t):
            if end < t[0] + window:
                continue
            while t[lo] <= end - window:
                lo += 1
            n = hi - lo + 1
            worst = n if worst is None else min(worst, n)
        return None if worst is None else worst / window

    @staticmethod
    def _pct(xs: list[float], p: float) -> float:
        s = sorted(xs)
        return s[min(len(s) - 1, int(round(p / 100.0 * (len(s) - 1))))] if s else 0.0

    def summary(self) -> str:
        worst = self.worst_window_hz()
        w = f"worst 1 s window {worst:.1f} Hz" if worst is not None else "under 1 s"
        return (f"{self.hz():.1f} Hz ({w}), {len(self.done)} depth maps, inference median "
                f"{self._pct(self.infer, 50) * 1e3:.1f} ms, p95 {self._pct(self.infer, 95) * 1e3:.1f} ms")


class VideoSession:
    """The helper's side of one ring: the newest frame through depth_fn, at most max_hz times a second.

    depth_fn(rgba, width, height) returns WIDTH x HEIGHT native 16-bit samples (bytes). The cap keeps 90%
    of the period between starts, so a film at exactly max_hz is never held back by polling jitter, and
    a 60 fps film gets every second frame."""

    def __init__(self, ring: Ring, depth_fn: Callable[[bytes, int, int], bytes], mm=None,
                 max_hz: float = MAX_HZ, clock: Callable[[], float] = time.monotonic,
                 log: Callable[[str], None] = lambda s: None, log_every: float = LOG_EVERY_S):
        self.ring = ring
        self.mm = mm
        self.depth_fn = depth_fn
        self.min_gap = 0.9 / max_hz
        self.clock = clock
        self.log = log
        self.log_every = log_every
        self.seen = 0
        self.written = 0
        self.next_start = 0.0
        self.stats = Stats()
        self.window = Stats()
        self.window_start = clock()

    def done(self) -> bool:
        """The player has stopped sending (3D off, or it is going)."""
        return self.ring.closed

    def step(self) -> bool:
        """Depth for the newest frame when one is new and the cap allows; True when one was made."""
        now = self.clock()
        if now < self.next_start:
            return False
        got = self.ring.frame_read(self.seen)
        if got is None:
            return False
        n, _, rgba = got
        self.seen = n
        self.next_start = now + self.min_gap
        depth = self.depth_fn(rgba, WIDTH, HEIGHT)
        end = self.clock()
        self.written += 1
        self.ring.depth_write(self.written, n, depth)
        self.stats.add(end, end - now)
        self.window.add(end, end - now)
        if end - self.window_start >= self.log_every:
            cuts = getattr(self.depth_fn, "cuts", None)
            more = "" if cuts is None else f", {cuts} scene cuts so far"
            self.log(f"video: {self.window.summary()}, newest frame {n}{more}")
            self.window = Stats()
            self.window_start = end
        return True

    def close(self) -> None:
        self.ring.release()
        if self.mm is not None:
            self.mm.close()
            self.mm = None


class SceneCut:
    """Tells a cut from a frame: the mean luma change, over a coarse grid of sample points, between this
    frame and the one before, as a fraction of full scale. A cut changes most of the picture (well over
    CUT_THRESHOLD); motion, a pan and a fade change little between two frames."""

    def __init__(self, threshold: float = CUT_THRESHOLD, grid: tuple[int, int] = CUT_GRID):
        self.threshold = threshold
        self.grid = grid
        self.prev: list[int] | None = None
        self._offsets: dict[tuple[int, int], list[int]] = {}
        self.last = 0.0  # the change measured by the latest update

    def reset(self) -> None:
        self.prev = None
        self.last = 0.0

    def sample(self, rgba: bytes, width: int, height: int) -> list[int]:
        """Luma (0 to 255) at the middle of each grid cell."""
        offs = self._offsets.get((width, height))
        if offs is None:
            gx, gy = self.grid
            offs = [(((2 * j + 1) * height // (2 * gy)) * width + (2 * i + 1) * width // (2 * gx)) * 4
                    for j in range(gy) for i in range(gx)]
            self._offsets[(width, height)] = offs
        return [(rgba[o] * 77 + rgba[o + 1] * 150 + rgba[o + 2] * 29) >> 8 for o in offs]

    def update(self, rgba: bytes, width: int, height: int) -> bool:
        """Takes the next frame; True when it is a cut from the one before (never for the first)."""
        cur = self.sample(rgba, width, height)
        prev, self.prev = self.prev, cur
        if prev is None:
            self.last = 0.0
            return False
        self.last = sum(abs(a - b) for a, b in zip(cur, prev)) / (255.0 * len(cur))
        return self.last > self.threshold


class Smoothing:
    """The temporal smoothing's decisions (spec 8): an exponential moving average of the depth, `weight`
    for the newest map, and the same of the percentile range (lo, hi) the maps are normalised with, both
    restarted on a scene cut and at the first frame. Numbers only; DepthPipeline applies them."""

    def __init__(self, weight: float = SMOOTH_WEIGHT, cut: SceneCut | None = None):
        if not 0.0 < weight <= 1.0:
            raise ValueError(f"the weight of the newest map is more than 0 and at most 1, got {weight}")
        self.weight = weight
        self.cut = cut if cut is not None else SceneCut()
        self.range: tuple[float, float] | None = None
        self.cuts = 0  # scene cuts seen

    def reset(self) -> None:
        self.range = None
        self.cut.reset()

    def step(self, rgba: bytes, width: int, height: int, lo: float, hi: float) -> tuple[float, float, float]:
        """The next frame, with the percentile range (lo, hi) of its raw depth. Returns the range to
        normalise it with and alpha, the weight of its normalised depth against the smoothed one before
        (1.0: take it as it is, after a cut or for the first)."""
        if self.cut.update(rgba, width, height):
            self.cuts += 1
            self.range = None
        if self.range is None:
            self.range = (lo, hi)
            return lo, hi, 1.0
        a = self.weight
        plo, phi = self.range
        self.range = (plo + a * (lo - plo), phi + a * (hi - phi))
        return self.range[0], self.range[1], a


class DepthPipeline:
    """depth_fn for VideoSession: the 322x182 model on an RGBA frame, normalised as photos are (1st and
    99th percentiles to 0 and 65535), smoothed over time by `smoothing` (None: not smoothed). One
    pipeline per film: it holds the previous map; reset() forgets it."""

    def __init__(self, backend, smoothing: Smoothing | None = None):
        import numpy as np

        from xrdome_depth import photo

        self.np = np
        self.photo = photo
        self.backend = backend
        self.smoothing = smoothing
        self.mean = np.array(photo.MEAN, dtype=np.float32)
        self.std = np.array(photo.STD, dtype=np.float32)
        self.prev = None  # the previous smoothed map (float32, 0 to 1)

    @property
    def cuts(self) -> int | None:
        return None if self.smoothing is None else self.smoothing.cuts

    def reset(self) -> None:
        self.prev = None
        if self.smoothing is not None:
            self.smoothing.reset()

    def infer(self, rgba: bytes, width: int, height: int):
        """The model's raw inverse depth for the frame (height x width float32, larger = nearer)."""
        np = self.np
        rgb = np.frombuffer(rgba, dtype=np.uint8).reshape(height, width, 4)[..., :3]
        x = (rgb.astype(np.float32) / 255.0 - self.mean) / self.std  # already the model size: no resize
        chw = np.ascontiguousarray(x.transpose(2, 0, 1), dtype=np.float32)
        return self.backend.infer(chw, width, height)

    def unit(self, raw, rgba: bytes, width: int, height: int):
        """The frame's depth in [0, 1] (float32): the raw depth normalised, then smoothed."""
        np = self.np
        if self.smoothing is None:
            return self.photo.normalise(raw)
        if not np.isfinite(raw).all():
            from xrdome_depth.backend import BackendError

            raise BackendError("the model returned a depth with NaN or infinite values")
        lo, hi = np.percentile(raw, self.photo.PERCENTILES)
        lo, hi, alpha = self.smoothing.step(rgba, width, height, float(lo), float(hi))
        if hi > lo:
            unit = np.clip((raw - lo) / (hi - lo), 0.0, 1.0).astype(np.float32)
        else:  # a flat depth has no relative information: on the default screen plane, as photos
            unit = np.full(raw.shape, 0.5, dtype=np.float32)
        if alpha < 1.0 and self.prev is not None and self.prev.shape == unit.shape:
            unit = (self.prev + alpha * (unit - self.prev)).astype(np.float32)
        self.prev = unit
        return unit

    def __call__(self, rgba: bytes, width: int, height: int) -> bytes:
        unit = self.unit(self.infer(rgba, width, height), rgba, width, height)
        return self.np.ascontiguousarray(self.photo.to_samples(unit), dtype="=u2").tobytes()


def make_depth_fn(backend, smooth: bool = True) -> DepthPipeline:
    """A DepthPipeline for one film; smooth=False is the unsmoothed depth of spec 8's first half."""
    return DepthPipeline(backend, Smoothing() if smooth else None)


def warm(depth_fn) -> None:
    """Loads the model and runs it once, so the first frame is not the slow one; the pipeline forgets it."""
    depth_fn(bytes(FRAME_BYTES), WIDTH, HEIGHT)
    reset = getattr(depth_fn, "reset", None)
    if reset is not None:
        reset()


def synthetic_frames(count: int = 8) -> list[bytes]:
    """A few different frames for the bench (content does not change the model's time)."""
    return [os.urandom(FRAME_BYTES) for _ in range(count)]


def bench(seconds: float, rate: float, depth_fn, clock: Callable[[], float] = time.monotonic,
          sleep: Callable[[float], None] = time.sleep, frames: list[bytes] | None = None) -> tuple[Stats, int]:
    """Feeds frames at rate into a ring of its own and runs the helper's loop on it for seconds.
    Returns the loop's statistics and how many frames were fed."""
    frames = frames or synthetic_frames()
    ring = Ring(bytearray(SIZE), init=True)
    session = VideoSession(ring, depth_fn, clock=clock)
    period = 1.0 / rate
    start = clock()
    next_frame = start
    fed = 0
    try:
        while clock() - start < seconds:
            now = clock()
            if now >= next_frame:
                fed += 1
                ring.frame_write(fed, frames[fed % len(frames)])
                next_frame += period
                if next_frame < now:  # the loop fell behind: the producer skips ticks, as a film would
                    next_frame = now + period
            if not session.step():
                sleep(max(0.0, min(next_frame - clock(), POLL_S)))
    finally:
        session.close()
    return session.stats, fed


def main(argv: list[str] | None = None, make_backend=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--bench", action="store_true", required=True,
                    help="run the video loop on synthetic frames and report the Hz it achieves")
    ap.add_argument("--models-dir", required=True)
    ap.add_argument("--seconds", type=float, default=30.0)
    ap.add_argument("--rate", type=float, default=30.0, help="frames fed per second")
    ap.add_argument("--cpu", action="store_true", help="run ncnn on the CPU")
    args = ap.parse_args(argv)
    if args.seconds <= 0 or args.rate <= 0:
        print("depth bench-video: --seconds and --rate must be more than 0", file=sys.stderr)
        return 2

    from xrdome_depth.backend import BackendError, NcnnBackend

    backend = (make_backend or NcnnBackend)(args.models_dir, vulkan=not args.cpu)
    try:
        depth_fn = make_depth_fn(backend)
        start = time.monotonic()
        warm(depth_fn)
        print(f"bench-video: model {WIDTH}x{HEIGHT} warm in {time.monotonic() - start:.2f} s; "
              f"{args.rate:g} frames a second for {args.seconds:g} s", flush=True)
        stats, fed = bench(args.seconds, args.rate, depth_fn)
    except ImportError as e:
        print(f"depth bench-video: cannot import numpy ({e}); run `xrdome-tools setup depth`", file=sys.stderr)
        return 2
    except BackendError as e:
        print(f"depth bench-video: {e}", file=sys.stderr)
        return 2
    print(f"bench-video: {stats.summary()}, {fed} frames fed", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
