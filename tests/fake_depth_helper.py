#!/usr/bin/env python3
"""A stand-in for `xrdome-tools depth serve` in the Xvfb smoke test (tests/smoke_depth_serve.sh).

    fake_depth_helper.py depth serve --socket PATH

The real socket loop and protocol (tools/xrdome_depth/serve.py), with a fake depth maker instead of the
model: for each photo (PNG) it writes the step depth smoke_depth.sh uses (0 left of the middle, 65535 from
it on) at the photo's size into $XDG_CACHE_HOME/xrdome-tools/depth/ (spec 5 name and format). Environment:

  FAKE_DEPTH_LOG    append "start" and one "<path> prefetch=<0|1>" line per request received
  FAKE_DEPTH_DELAY  seconds each photo takes (default 0.3), so the player shows 2D first
  FAKE_DEPTH_EXIT   exit with this status at once, saying so on stderr (a helper that cannot run)

A photo whose name starts with "bad" fails with an error reply.

Video (spec 8, tests/smoke_depth_video.sh): the real ring code (tools/xrdome_depth/video.py) with a fake
depth maker: it logs "video <shm>" per request and, for the first frame, "frame top=R,G,B bottom=R,G,B"
(the pixels at the middle of the top and bottom rows), and writes a flat depth (32768) for every frame.

  FAKE_DEPTH_NO_VIDEO  refuse video requests with this error
  FAKE_DEPTH_STEP      write the step depth of the photos instead (0 left of the middle, 65535 from it
                       on) for every frame: a film's bars then move as a photo's do
"""

import os
import signal
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from xrdome_depth import cache, serve, video  # noqa: E402


def log(line: str) -> None:
    path = os.environ.get("FAKE_DEPTH_LOG")
    if path:
        with open(path, "a", encoding="utf-8", errors="surrogateescape") as f:
            f.write(line + "\n")


def png_size(path: Path) -> tuple[int, int]:
    head = path.read_bytes()[:24]
    if head[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path.name}: not a PNG")
    return struct.unpack(">II", head[16:24])


def main() -> int:
    args = sys.argv[1:]
    if args[:2] != ["depth", "serve"] or len(args) != 4 or args[2] != "--socket":
        print(f"fake_depth_helper: unexpected arguments {args}", file=sys.stderr)
        return 64
    code = os.environ.get("FAKE_DEPTH_EXIT")
    if code:
        print("depth serve: fake failure", file=sys.stderr)
        return int(code)
    sock = Path(args[3])
    cache_dir = Path(os.environ["XDG_CACHE_HOME"]) / "xrdome-tools" / "depth"
    delay = float(os.environ.get("FAKE_DEPTH_DELAY", "0.3"))

    def process(p: Path) -> Path:
        time.sleep(delay)
        if p.name.startswith("bad"):
            raise ValueError(f"cannot decode {p.name}")
        w, h = png_size(p)
        row = b"".join((0 if x < w // 2 else 65535).to_bytes(2, "big") for x in range(w))
        dest = cache.cache_path(cache_dir, cache.cache_key(p))
        cache.write_pgm(dest, w, h, row * h)
        return dest

    def cached(p: Path):
        dest = cache.cache_path(cache_dir, cache.cache_key(p))
        return dest if dest.is_file() else None

    flat = (32768).to_bytes(2, sys.byteorder) * (video.WIDTH * video.HEIGHT)
    if os.environ.get("FAKE_DEPTH_STEP"):
        row = b"".join((0 if x < video.WIDTH // 2 else 65535).to_bytes(2, sys.byteorder)
                       for x in range(video.WIDTH))
        flat = row * video.HEIGHT
    frames = []

    def depth_fn(rgba: bytes, w: int, h: int) -> bytes:
        if not frames:
            def px(y):
                off = (y * w + w // 2) * 4
                return ",".join(str(v) for v in rgba[off:off + 3])
            log(f"frame top={px(0)} bottom={px(h - 1)}")
        frames.append(1)
        return flat

    def open_video(shm: str):
        no = os.environ.get("FAKE_DEPTH_NO_VIDEO")
        if no:
            raise RuntimeError(no)
        ring, mm = video.open_shm(shm)
        return video.VideoSession(ring, depth_fn, mm=mm)

    class Logged(serve.Server):
        def handle(self, c, line):
            try:
                req = serve.parse_request(line)
                if isinstance(req, serve.VideoRequest):
                    log(f"video {req.shm}")
                else:
                    log(f"{req.path} prefetch={int(req.prefetch)}")
            except serve.ProtocolError:
                log(f"bad request {line!r}")
            super().handle(c, line)

    server = Logged(sock, process, cached, idle_exit=float(os.environ.get("FAKE_DEPTH_IDLE", "5")),
                    open_video=open_video)
    if not server.bind():
        return 0
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))  # as serve.main: the socket is removed
    log("start")
    try:
        server.serve()
    finally:
        server.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
