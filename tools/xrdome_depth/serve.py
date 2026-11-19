#!/usr/bin/env python3
"""serve.py --socket PATH --cache-dir DIR --models-dir DIR [--idle-exit S]: the depth helper service.

Runs in the depth venv, started by `xrdome-tools depth serve`, which xrdome starts when it opens a photo
whose depth is not in the cache. Spec 7 of docs/superpowers/specs/2026-11-19-depth-3d-design.md is the
contract:

  - a Unix stream socket; one JSON object per line each way, UTF-8;
  - request {"op": "photo", "path": "<absolute path>", "prefetch": false};
  - reply {"ok": true, "path": "<as asked>", "depth": "<cache file>"} or
    {"ok": false, "path": "<as asked>", "error": "..."}. "path" (not in the spec's example) says which
    request a reply answers, since replies do not come back in the order asked;
  - requests are served newest first among the ones that are not prefetches; prefetches come after them,
    newest first too. A photo already in the cache is answered at once.

Spec 8 adds video: request {"op": "video", "shm": "/xrdome-<pid>-<n>"}, the name of the shared-memory
ring xrdome copies a film's frames into (video.py and src/video/framesink.h). The helper maps it, removes
the name, and replies {"ok": true, "op": "video", "shm": "<as asked>"} or {"ok": false, "op": "video",
"shm": "<as asked>", "error": "..."}. From then on it makes depth for the ring's newest frame, before any
photo, until xrdome marks the ring closed, sends another video request on the same connection, or
disconnects.

Paths travel as their bytes: a file name that is not UTF-8 goes through Python's surrogateescape both
ways, so the reply's "path" is byte for byte what the player sent.

One model is loaded at start and kept warm; the process exits after --idle-exit seconds with no client
connected and nothing to do. This module's protocol and queue are standard library only, so they can be
tested without numpy; the inference comes from photo.py, imported in main().
"""

import argparse
import errno
import json
import os
import selectors
import signal
import socket
import stat
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

if __package__ in (None, ""):  # started as a script: make xrdome_depth importable, not this folder's files
    sys.path[0] = str(Path(__file__).resolve().parents[1])

from xrdome_depth import cache, video

MAX_LINE = 64 * 1024     # a request line longer than this closes the connection
MAX_JOBS = 256           # queued photos; past this the oldest prefetch (else the oldest) is dropped
IDLE_EXIT_S = 600.0


class ProtocolError(Exception):
    def __init__(self, message: str, path: str | None = None):
        super().__init__(message)
        self.path = path


@dataclass(frozen=True)
class Request:
    path: str
    prefetch: bool


@dataclass(frozen=True)
class VideoRequest:
    shm: str


def parse_request(line: bytes) -> Request | VideoRequest:
    """One request line (without its newline). ProtocolError for anything but a photo or video request."""
    try:
        obj = json.loads(line.decode("utf-8", "surrogateescape"))
    except ValueError as e:
        raise ProtocolError(f"not JSON: {e}") from None
    if not isinstance(obj, dict):
        raise ProtocolError("not a JSON object")
    path = obj.get("path")
    path = path if isinstance(path, str) else None
    if obj.get("op") == "video":
        shm = obj.get("shm")
        if not video.valid_name(shm):
            raise ProtocolError("a video request needs a \"shm\" name such as \"/xrdome-1-1\"")
        return VideoRequest(shm)
    if obj.get("op") != "photo":
        raise ProtocolError(f"unknown op {obj.get('op')!r} (this helper serves \"photo\" and \"video\")", path)
    if path is None or not path:
        raise ProtocolError("a photo request needs a \"path\" string")
    if not os.path.isabs(path):
        raise ProtocolError("the path must be absolute", path)
    prefetch = obj.get("prefetch", False)
    if not isinstance(prefetch, bool):
        raise ProtocolError("\"prefetch\" must be true or false", path)
    return Request(path, prefetch)


def encode_reply(reply: dict) -> bytes:
    """A reply as its line: UTF-8, the path's undecodable bytes restored (surrogateescape)."""
    return json.dumps(reply, ensure_ascii=False).encode("utf-8", "surrogateescape") + b"\n"


def ok_reply(path: str, depth: Path) -> dict:
    return {"ok": True, "path": path, "depth": os.fsdecode(depth)}


def video_reply(shm: str, error: str | None = None) -> dict:
    if error is None:
        return {"ok": True, "op": "video", "shm": shm}
    return {"ok": False, "op": "video", "shm": shm, "error": error}


def error_reply(path: str | None, error: str) -> dict:
    reply: dict = {"ok": False}
    if path is not None:
        reply["path"] = path
    reply["error"] = error
    return reply


@dataclass
class Job:
    path: str
    prefetch: bool
    seq: int
    waiters: list = field(default_factory=list)


class Queue:
    """Photos waiting for depth: one job per path, each with the connections waiting for it.

    pop() gives the newest job that is not a prefetch, else the newest prefetch. Asking again for a
    queued photo makes it the newest, and a request that is not a prefetch makes the job not one."""

    def __init__(self, max_jobs: int = MAX_JOBS):
        self.max_jobs = max_jobs
        self._jobs: dict[str, Job] = {}
        self._seq = 0

    def __len__(self) -> int:
        return len(self._jobs)

    def add(self, req: Request, waiter) -> list[Job]:
        """Queues req for waiter; returns the jobs dropped to make room (their waiters get an error)."""
        self._seq += 1
        job = self._jobs.get(req.path)
        if job is None:
            job = self._jobs[req.path] = Job(req.path, req.prefetch, self._seq)
        else:
            job.prefetch = job.prefetch and req.prefetch
            job.seq = self._seq
        if waiter not in job.waiters:
            job.waiters.append(waiter)
        dropped = []
        while len(self._jobs) > self.max_jobs:
            jobs = [j for j in self._jobs.values() if j is not job]
            pre = [j for j in jobs if j.prefetch]
            old = min(pre or jobs, key=lambda j: j.seq)
            dropped.append(self._jobs.pop(old.path))
        return dropped

    def pop(self) -> Job | None:
        if not self._jobs:
            return None
        now = [j for j in self._jobs.values() if not j.prefetch]
        job = max(now or self._jobs.values(), key=lambda j: j.seq)
        return self._jobs.pop(job.path)

    def forget(self, waiter) -> None:
        """A connection went away: it waits for nothing; jobs nobody waits for are dropped."""
        for path in list(self._jobs):
            job = self._jobs[path]
            if waiter in job.waiters:
                job.waiters.remove(waiter)
                if not job.waiters:
                    del self._jobs[path]


class Conn:
    def __init__(self, sock: socket.socket):
        self.sock = sock
        self.inbuf = b""
        self.outbuf = b""
        self.closed = False
        self.video = None  # its VideoSession while it sends video


def socket_in_use(path: Path) -> bool:
    """Whether a server answers on path (another helper already serves)."""
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        s.connect(os.fspath(path))
        return True
    except OSError:
        return False
    finally:
        s.close()


class Server:
    """The socket loop. process(path) makes the depth of one photo (slow) and returns its cache file;
    cached(path) returns the cache file when it already exists, else None (both may raise).
    open_video(shm) maps a video ring and returns its video.VideoSession (may raise); None: this helper
    refuses video."""

    def __init__(self, path: Path, process: Callable[[Path], Path], cached: Callable[[Path], Path | None],
                 idle_exit: float = IDLE_EXIT_S, clock: Callable[[], float] = time.monotonic,
                 log: Callable[[str], None] = lambda s: None,
                 open_video: Callable[[str], "video.VideoSession"] | None = None):
        self.path = Path(path)
        self.process = process
        self.cached = cached
        self.open_video = open_video
        self.idle_exit = idle_exit
        self.clock = clock
        self.log = log
        self.queue = Queue()
        self.sel = selectors.DefaultSelector()
        self.listener: socket.socket | None = None
        self.conns: list[Conn] = []
        self.last_busy = clock()
        self._ino: tuple[int, int] | None = None

    # ---- socket ---------------------------------------------------------

    def bind(self) -> bool:
        """Listens on the socket path. False when another helper already answers there; a stale socket
        file (nobody listening) is replaced."""
        self.path.parent.mkdir(parents=True, exist_ok=True)
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        old_umask = os.umask(0o177)  # the socket is the user's alone
        try:
            try:
                s.bind(os.fspath(self.path))
            except OSError as e:
                if e.errno != errno.EADDRINUSE:
                    raise
                if socket_in_use(self.path):
                    s.close()
                    return False
                self.path.unlink(missing_ok=True)
                s.bind(os.fspath(self.path))
        except BaseException:
            s.close()
            raise
        finally:
            os.umask(old_umask)
        st = os.stat(self.path)
        self._ino = (st.st_dev, st.st_ino)
        s.listen(16)
        s.setblocking(False)
        self.listener = s
        self.sel.register(s, selectors.EVENT_READ, None)
        return True

    def close(self) -> None:
        for c in list(self.conns):
            self._drop(c)
        if self.listener is not None:
            self.sel.unregister(self.listener)
            self.listener.close()
            self.listener = None
            try:  # remove the socket file only if it is still ours
                st = os.stat(self.path)
                if stat.S_ISSOCK(st.st_mode) and (st.st_dev, st.st_ino) == self._ino:
                    self.path.unlink()
            except OSError:
                pass
        self.sel.close()

    # ---- connections ------------------------------------------------------

    def _accept(self) -> None:
        while True:
            try:
                s, _ = self.listener.accept()
            except (BlockingIOError, InterruptedError):
                return
            s.setblocking(False)
            c = Conn(s)
            self.conns.append(c)
            self.sel.register(s, selectors.EVENT_READ, c)

    def _drop(self, c: Conn) -> None:
        if c.closed:
            return
        c.closed = True
        self._end_video(c)
        self.queue.forget(c)
        self.conns.remove(c)
        self.sel.unregister(c.sock)
        c.sock.close()

    def _watch(self, c: Conn) -> None:
        if not c.closed:
            events = selectors.EVENT_READ | (selectors.EVENT_WRITE if c.outbuf else 0)
            self.sel.modify(c.sock, events, c)

    def send(self, c: Conn, reply: dict) -> None:
        if c.closed:
            return
        c.outbuf += encode_reply(reply)
        self._flush(c)

    def _flush(self, c: Conn) -> None:
        while c.outbuf and not c.closed:
            try:
                n = c.sock.send(c.outbuf)
            except (BlockingIOError, InterruptedError):
                break
            except OSError:
                self._drop(c)
                return
            c.outbuf = c.outbuf[n:]
        self._watch(c)

    def _read(self, c: Conn) -> None:
        while not c.closed:
            try:
                data = c.sock.recv(65536)
            except (BlockingIOError, InterruptedError):
                break
            except OSError:
                self._drop(c)
                return
            if not data:
                self._drop(c)
                return
            c.inbuf += data
        while not c.closed and b"\n" in c.inbuf:
            line, c.inbuf = c.inbuf.split(b"\n", 1)
            if line.strip():
                self.handle(c, line)
        if len(c.inbuf) > MAX_LINE:
            self.send(c, error_reply(None, f"request line longer than {MAX_LINE} bytes"))
            self._drop(c)

    # ---- requests ---------------------------------------------------------

    def handle(self, c: Conn, line: bytes) -> None:
        """One request line from c: answered at once when it is bad or cached, else queued."""
        try:
            req = parse_request(line)
        except ProtocolError as e:
            self.send(c, error_reply(e.path, str(e)))
            return
        if isinstance(req, VideoRequest):
            self.start_video(c, req.shm)
            return
        try:
            hit = self.cached(Path(req.path))
        except OSError as e:
            self.send(c, error_reply(req.path, f"cannot read the photo: {e.strerror or e}"))
            return
        if hit is not None:
            self.send(c, ok_reply(req.path, hit))
            return
        for job in self.queue.add(req, c):
            for w in job.waiters:
                self.send(w, error_reply(job.path, "dropped: too many photos queued"))

    # ---- video ---------------------------------------------------------------

    def start_video(self, c: Conn, shm: str) -> None:
        """Maps c's ring (replacing a ring c sent before) and answers."""
        self._end_video(c)
        if self.open_video is None:
            self.send(c, video_reply(shm, "this helper does not make depth for video"))
            return
        try:
            c.video = self.open_video(shm)
        except Exception as e:  # a bad ring must not stop the service
            self.log(f"video {shm}: {e}")
            self.send(c, video_reply(shm, f"cannot map {shm}: {e}"))
            return
        self.log(f"video {shm}: mapped")
        self.send(c, video_reply(shm))

    def _end_video(self, c: Conn) -> None:
        if c.video is not None:
            c.video.close()
            c.video = None

    def videos(self) -> list[Conn]:
        return [c for c in self.conns if c.video is not None]

    def run_video(self) -> bool:
        """Depth for each ring's newest frame that is due; rings the player closed are let go. True when
        any depth was made."""
        busy = False
        for c in self.videos():
            if c.video.done():
                self.log("video: the player stopped sending")
                self._end_video(c)
                continue
            try:
                busy = c.video.step() or busy
            except Exception as e:  # e.g. the GPU fails: say so once, drop the ring, keep serving photos
                self.log(f"video: failed: {e}")
                self._end_video(c)
                continue
            if busy:
                self.last_busy = self.clock()
        return busy

    def run_one(self) -> bool:
        """Makes the depth of the next queued photo and answers everyone waiting for it."""
        job = self.queue.pop()
        if job is None:
            return False
        try:
            reply = ok_reply(job.path, self.process(Path(job.path)))
        except Exception as e:  # one bad photo must not stop the service
            reply = error_reply(job.path, str(e) or type(e).__name__)
            self.log(f"{job.path}: failed: {reply['error']}")
        for w in job.waiters:
            self.send(w, reply)
        self.last_busy = self.clock()
        return True

    # ---- the loop -----------------------------------------------------------

    def step(self, timeout: float | None) -> None:
        """Waits up to timeout for I/O and handles all of it (no photo work)."""
        for key, events in self.sel.select(timeout):
            c = key.data
            if c is None:
                self._accept()
                continue
            if events & selectors.EVENT_WRITE:
                self._flush(c)
            if events & selectors.EVENT_READ:
                self._read(c)

    def idle_for(self) -> float | None:
        """Seconds of idleness left before exiting; None while a client is connected or work is queued."""
        if self.conns or len(self.queue):
            self.last_busy = self.clock()
            return None
        return self.idle_exit - (self.clock() - self.last_busy)

    def serve(self) -> None:
        """Until idle for idle_exit seconds. Input is drained before each photo, so the newest request
        is known when the next photo is chosen."""
        while True:
            self.step(0)
            if self.run_video() or self.run_one():
                continue
            left = self.idle_for()
            if left is not None and left <= 0:
                self.log(f"idle for {self.idle_exit:g} s, exiting")
                return
            if self.videos():  # a ring has no wake-up: look at it again soon
                left = video.POLL_S if left is None else min(left, video.POLL_S)
            self.step(left)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--socket", required=True, type=Path)
    ap.add_argument("--cache-dir", required=True, type=Path)
    ap.add_argument("--models-dir", required=True, type=Path)
    ap.add_argument("--idle-exit", type=float, default=IDLE_EXIT_S,
                    help="exit after this many seconds with no client and nothing queued")
    args = ap.parse_args(argv)

    def log(s: str) -> None:
        print(f"depth serve: {s}", flush=True)

    # The model and numpy before the socket: a helper that cannot run never takes the socket, and a
    # player waiting for it sees the process exit with the reason in its log.
    from xrdome_depth import photo
    from xrdome_depth.backend import BackendError, NcnnBackend

    start = time.monotonic()
    backend = NcnnBackend(args.models_dir, vulkan=True)  # the GPU only: CPU depth costs xrdome frames
    try:
        import numpy as np

        w, h = photo.LANDSCAPE
        backend.infer(np.zeros((3, h, w), dtype=np.float32), w, h)  # load and warm the photo model
    except ImportError as e:
        print(f"depth serve: cannot import numpy ({e}); run `xrdome-tools setup depth`", file=sys.stderr)
        return 2
    except BackendError as e:
        print(f"depth serve: {e}", file=sys.stderr)
        return 2
    log(f"model {w}x{h} warm in {time.monotonic() - start:.2f} s")

    def process(p: Path) -> Path:
        dest, note = photo.process(p, args.cache_dir, backend)
        log(f"{p}: {note}")
        return dest

    def cached(p: Path) -> Path | None:
        dest = cache.cache_path(args.cache_dir, cache.cache_key(p))
        return dest if dest.is_file() else None

    video_warm = False

    def open_video(shm: str) -> video.VideoSession:
        nonlocal video_warm
        fn = video.make_depth_fn(backend)  # its own smoothing state for each film
        if not video_warm:  # the 322x182 model, loaded and warmed on the first film
            start = time.monotonic()
            video.warm(fn)
            log(f"model {video.WIDTH}x{video.HEIGHT} warm in {time.monotonic() - start:.2f} s")
            video_warm = True
        ring, mm = video.open_shm(shm)
        return video.VideoSession(ring, fn, mm=mm, log=log)

    server = Server(args.socket, process, cached, args.idle_exit, log=log, open_video=open_video)
    if not server.bind():
        log(f"another helper already serves {args.socket}")
        return 0
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
    log(f"listening on {args.socket}")
    try:
        server.serve()
    finally:
        server.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
