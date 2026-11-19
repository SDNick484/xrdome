"""Live video depth (spec 8, first half): the shared-memory ring's layout and sequence logic, the helper's
video loop with a fake depth maker, the "video" op of `depth serve`, and `depth bench-video`'s launcher.
Standard library only, except the numpy test of the real depth function, which skips without numpy.
src/video/framesink.h is the other side; tests/test_framesink.c runs this module against the C ring."""

import argparse
import json
import os
import shutil
import socket
import struct
import tempfile
import threading
import time
from pathlib import Path

import pytest

from xrdome_depth import serve, video
from xrdome_depth.serve import Server, VideoRequest
from xrdome_depth.video import Ring, RingError, Stats, VideoSession
from xrdome_tools import cli, config, depth, depthenv
from xrdome_tools.errors import EXIT_SETUP, ToolError

posixshm = pytest.importorskip("_posixshmem", reason="no POSIX shared memory in this Python")


# ---- layout ------------------------------------------------------------------------

def test_layout_matches_framesink_h():
    # The numbers in src/video/framesink.h's table.
    assert (video.WIDTH, video.HEIGHT, video.FRAME_SLOTS, video.DEPTH_SLOTS) == (322, 182, 4, 3)
    assert video.FRAME_BYTES == 322 * 182 * 4 and video.DEPTH_BYTES == 322 * 182 * 2
    assert video.frame_offset(0) == 4096
    assert video.FRAME_STRIDE == 237568 and video.DEPTH_STRIDE == 118784  # rounded up to 4096
    assert video.depth_offset(0) == 4096 + 4 * 237568
    assert video.SIZE == video.depth_offset(3) == 1310720
    assert all(o % 4096 == 0 for o in [video.frame_offset(i) for i in range(4)] +
               [video.depth_offset(i) for i in range(3)])
    assert video._HEAD.size == 80


def test_init_writes_the_header():
    buf = bytearray(video.SIZE)
    Ring(buf, init=True).release()
    assert buf[:8] == b"XRDVRNG1"
    assert struct.unpack_from("=IIIIII", buf, 8) == (1, 4096, 322, 182, 4, 3)
    assert struct.unpack_from("=QQQQQ", buf, 32) == (4096, 237568, 4096 + 4 * 237568, 118784, video.SIZE)
    assert struct.unpack_from("=II", buf, 72) == (1, 1)
    ring = Ring(buf)  # and it reads back
    assert (ring.frame_head, ring.depth_head, ring.closed) == (0, 0, False)


def test_refuses_what_is_not_a_ring():
    with pytest.raises(RingError, match="needs"):
        Ring(bytearray(100))
    buf = bytearray(video.SIZE)
    with pytest.raises(RingError, match="not a version 1 ring"):
        Ring(buf)
    Ring(buf, init=True).release()
    struct.pack_into("=I", buf, 16, 640)  # another width
    with pytest.raises(RingError):
        Ring(buf)


# ---- sequence logic ------------------------------------------------------------------

def _frame(k: int) -> bytes:
    return bytes([k % 256]) * video.FRAME_BYTES


def _depth(k: int) -> bytes:
    return bytes([k % 256, 0]) * (video.DEPTH_BYTES // 2)


def test_frames_newest_first_and_only_once():
    ring = Ring(bytearray(video.SIZE), init=True)
    assert ring.frame_read(0) is None
    ring.frame_write(1, _frame(1), t_ns=11)
    assert ring.frame_read(0) == (1, 11, _frame(1))
    assert ring.frame_read(1) is None  # nothing newer than what was seen
    for n in range(2, 7):  # the reader was away: it gets only the newest
        ring.frame_write(n, _frame(n), t_ns=n)
    assert ring.frame_read(1) == (6, 6, _frame(6))
    assert ring.frame_head == 6


def test_depth_carries_its_frame_number():
    ring = Ring(bytearray(video.SIZE), init=True)
    ring.depth_write(1, 7, _depth(1))
    ring.depth_write(2, 9, _depth(2))
    assert ring.depth_read(0) == (2, 9, _depth(2))
    assert ring.depth_read(2) is None
    with pytest.raises(ValueError):
        ring.depth_write(3, 10, b"short")


def test_a_slot_being_written_is_not_read():
    ring = Ring(bytearray(video.SIZE), init=True)
    ring.frame_write(1, _frame(1))
    ring.q[(video.OFF_FRAME_META + 16 * 1) // 8] = 0  # the player is writing slot 1 again
    assert ring.frame_read(0) is None


def test_a_lapped_slot_is_not_read():
    ring = Ring(bytearray(video.SIZE), init=True)
    ring.frame_write(1, _frame(1))
    ring.q[video.OFF_FRAME_HEAD // 8] = 5  # head says 5, slot 1 still holds 1
    assert ring.frame_read(0) is None


class TornWords:
    """The slot's number changes between the two checks: the writer came round during the copy."""

    def __init__(self, q, index, values):
        self.q, self.index, self.values = q, index, list(values)

    def __getitem__(self, i):
        if i == self.index and self.values:
            return self.values.pop(0)
        return self.q[i]

    def __setitem__(self, i, v):
        self.q[i] = v


def test_a_copy_torn_by_the_writer_is_thrown_away():
    ring = Ring(bytearray(video.SIZE), init=True)
    ring.frame_write(1, _frame(1))
    meta = (video.OFF_FRAME_META + 16) // 8
    ring.q = TornWords(ring.q, meta, [1, 0])
    assert ring.frame_read(0) is None
    ring.q = TornWords(ring.q.q, meta, [1, 1])
    assert ring.frame_read(0)[0] == 1


def test_closed():
    ring = Ring(bytearray(video.SIZE), init=True)
    assert not ring.closed
    ring.close()
    assert ring.closed


def test_valid_name():
    assert video.valid_name("/xrdome-123-1")
    for bad in ("xrdome", "/", "/a/b", "/" + "x" * 31, "/é", None, 3):
        assert not video.valid_name(bad)


def _make_shm(name: str) -> tuple[Ring, object]:
    import mmap

    fd = posixshm.shm_open(name, os.O_RDWR | os.O_CREAT | os.O_EXCL, mode=0o600)
    try:
        os.ftruncate(fd, video.SIZE)
        mm = mmap.mmap(fd, video.SIZE)
    finally:
        os.close(fd)
    return Ring(mm, init=True), mm


def _shm_name() -> str:
    return f"/xrdome-t{os.getpid()}-{time.monotonic_ns() % 100000}"


def test_open_shm_maps_and_removes_the_name():
    name = _shm_name()
    player, pmm = _make_shm(name)
    try:
        player.frame_write(1, _frame(3))
        ring, mm = video.open_shm(name)
        assert ring.frame_read(0)[2] == _frame(3)
        with pytest.raises(FileNotFoundError):
            posixshm.shm_open(name, os.O_RDWR, mode=0o600)  # the name is gone, the mapping is not
        ring.depth_write(1, 1, _depth(4))
        assert player.depth_read(0) == (1, 1, _depth(4))
        ring.release()
        mm.close()
    finally:
        player.release()
        pmm.close()
        try:
            posixshm.shm_unlink(name)
        except FileNotFoundError:
            pass


def test_open_shm_refuses_a_missing_name():
    with pytest.raises(FileNotFoundError):
        video.open_shm("/xrdome-no-such-ring")


# ---- the helper's loop ------------------------------------------------------------------

class Clock:
    def __init__(self):
        self.t = 100.0

    def __call__(self):
        return self.t


def _fake_depth(clock=None, cost=0.0):
    seen = []

    def fn(rgba, w, h):
        assert (w, h, len(rgba)) == (322, 182, video.FRAME_BYTES)
        seen.append(rgba[0])
        if clock is not None:
            clock.t += cost
        return bytes([rgba[0], 0]) * (w * h)

    return fn, seen


def test_session_depth_for_the_newest_frame_with_its_number():
    clock = Clock()
    ring = Ring(bytearray(video.SIZE), init=True)
    fn, seen = _fake_depth()
    s = VideoSession(ring, fn, clock=clock)
    assert not s.step()  # no frame yet
    for n in (1, 2, 3):
        ring.frame_write(n, _frame(n))
    assert s.step()
    assert seen == [3]
    assert ring.depth_read(0) == (1, 3, _depth(3))
    clock.t += 1
    assert not s.step()  # nothing new
    assert not s.done()
    ring.close()
    assert s.done()


def test_session_takes_at_most_30_a_second():
    clock = Clock()
    ring = Ring(bytearray(video.SIZE), init=True)
    fn, seen = _fake_depth()
    s = VideoSession(ring, fn, clock=clock)
    made = 0
    for n in range(1, 121):  # a 60 fps film for 2 s
        ring.frame_write(n, _frame(n))
        made += s.step()
        clock.t += 1 / 60
    assert made == 60  # every second frame: 30 Hz
    clock2 = Clock()
    ring2 = Ring(bytearray(video.SIZE), init=True)
    s2 = VideoSession(ring2, fn, clock=clock2)
    made = 0
    for n in range(1, 61):  # 30 fps, polled a little late each time: none held back
        ring2.frame_write(n, _frame(n))
        clock2.t += 0.003
        made += s2.step()
        clock2.t += 1 / 30 - 0.003
    assert made == 60


def test_session_logs_its_rate():
    clock = Clock()
    ring = Ring(bytearray(video.SIZE), init=True)
    fn, _ = _fake_depth(clock, cost=0.02)
    lines = []
    s = VideoSession(ring, fn, clock=clock, log=lines.append, log_every=1.0)
    for n in range(1, 40):
        ring.frame_write(n, _frame(n))
        s.step()
        clock.t += 1 / 30 - 0.02
    assert lines and lines[0].startswith("video: 30.0 Hz") and "inference median 20.0 ms" in lines[0]


def test_stats():
    st = Stats()
    assert st.hz() == 0.0 and st.worst_window_hz() is None
    for i in range(31):
        st.add(i / 30, 0.01)
    assert st.hz() == pytest.approx(30.0)
    assert st.worst_window_hz() == 30.0
    st.add(1.0 + 0.5, 0.03)  # a gap
    assert st.worst_window_hz() < 30.0
    assert "median 10.0 ms" in st.summary() and "32 depth maps" in st.summary()


def test_bench_reports_the_rate_it_holds():
    clock = Clock()
    fn, _ = _fake_depth(clock, cost=0.024)  # the spike's p95 at 322x182

    def sleep(s):
        clock.t += max(s, 0.0005)

    stats, fed = video.bench(3.0, 30.0, fn, clock=clock, sleep=sleep, frames=[_frame(1), _frame(2)])
    assert fed == pytest.approx(90, abs=2)
    assert stats.hz() == pytest.approx(30.0, rel=0.03)
    assert stats.worst_window_hz() >= 29.0


def test_bench_with_a_slow_model_falls_behind_gracefully():
    clock = Clock()
    fn, _ = _fake_depth(clock, cost=0.05)  # 20 Hz at most

    def sleep(s):
        clock.t += max(s, 0.0005)

    stats, _ = video.bench(3.0, 30.0, fn, clock=clock, sleep=sleep, frames=[_frame(1)])
    assert stats.hz() == pytest.approx(20.0, rel=0.05)


def test_make_depth_fn_normalises_like_photos():
    np = pytest.importorskip("numpy", reason="numpy is not installed (the depth venv has it)")
    from xrdome_depth.backend import Backend

    class Fake(Backend):
        def infer(self, chw, width, height):
            assert chw.shape == (3, height, width) and chw.dtype == np.float32
            return np.tile(np.linspace(0, 1, width, dtype=np.float32), (height, 1))

    fn = video.make_depth_fn(Fake())
    out = np.frombuffer(fn(_frame(9), 322, 182), dtype="=u2").reshape(182, 322)
    assert out[0, 0] == 0 and out[0, -1] == 65535 and (np.diff(out[0].astype(int)) >= 0).all()


# ---- the "video" op over the socket -----------------------------------------------------

@pytest.fixture
def short_dir():
    d = Path(tempfile.mkdtemp(prefix="xv", dir="/tmp"))  # Unix socket paths are short
    yield d
    shutil.rmtree(d, ignore_errors=True)


def _no_photos(p):
    raise AssertionError("no photo was asked for")


def _client(path):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(10)
    s.connect(str(path))
    return s


def _reply(s):
    buf = b""
    while not buf.endswith(b"\n"):
        data = s.recv(4096)
        assert data, "the server closed the connection"
        buf += data
    return json.loads(buf)


def _start(server):
    def run():
        try:
            server.serve()
        finally:
            server.close()

    t = threading.Thread(target=run, daemon=True)
    t.start()
    return t


def _wait(cond, timeout=5.0):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if cond():
            return True
        time.sleep(0.01)
    return False


def _mapped(shm, fn):
    ring, mm = video.open_shm(shm)
    return VideoSession(ring, fn, mm=mm)


def test_parse_video_request():
    assert serve.parse_request(b'{"op": "video", "shm": "/xrdome-4-1"}') == VideoRequest("/xrdome-4-1")


def test_video_over_the_socket(short_dir):
    fn, seen = _fake_depth()
    lines = []
    server = Server(short_dir / "s.sock", _no_photos, lambda p: None, idle_exit=1.0, log=lines.append,
                    open_video=lambda shm: _mapped(shm, fn))
    assert server.bind()
    thread = _start(server)
    name = _shm_name()
    player, pmm = _make_shm(name)
    try:
        s = _client(short_dir / "s.sock")
        s.sendall(json.dumps({"op": "video", "shm": name}).encode() + b"\n")
        assert _reply(s) == {"ok": True, "op": "video", "shm": name}
        player.frame_write(1, _frame(5))
        assert _wait(lambda: player.depth_head == 1)
        assert player.depth_read(0) == (1, 1, _depth(5))
        player.frame_write(2, _frame(6))
        assert _wait(lambda: player.depth_head == 2)
        assert player.depth_read(1) == (2, 2, _depth(6))
        player.close()  # 3D off: the helper lets the ring go, the connection stays
        assert _wait(lambda: "video: the player stopped sending" in lines)
        assert not server.videos()
        s.close()
    finally:
        player.release()
        pmm.close()
        thread.join(5)


def test_video_errors(short_dir):
    server = Server(short_dir / "s.sock", _no_photos, lambda p: None, idle_exit=1.0,
                    open_video=lambda shm: _mapped(shm, _fake_depth()[0]))
    assert server.bind()
    thread = _start(server)
    s = _client(short_dir / "s.sock")
    s.sendall(b'{"op": "video", "shm": "/xrdome-no-such-ring"}\n')
    got = _reply(s)
    assert got["ok"] is False and got["op"] == "video" and got["shm"] == "/xrdome-no-such-ring"
    assert got["error"].startswith("cannot map /xrdome-no-such-ring")
    s.sendall(b'{"op": "video"}\n')
    got = _reply(s)
    assert got["ok"] is False and "shm" in got["error"]
    s.close()
    thread.join(5)


def test_a_helper_without_video_says_so(short_dir):
    server = Server(short_dir / "s.sock", _no_photos, lambda p: None, idle_exit=1.0)
    assert server.bind()
    thread = _start(server)
    s = _client(short_dir / "s.sock")
    s.sendall(b'{"op": "video", "shm": "/xrdome-1-1"}\n')
    assert _reply(s) == {"ok": False, "op": "video", "shm": "/xrdome-1-1",
                         "error": "this helper does not make depth for video"}
    s.close()
    thread.join(5)


def test_a_client_that_leaves_lets_its_ring_go(short_dir):
    closed = []

    class Session(VideoSession):
        def close(self):
            closed.append(True)
            super().close()

    server = Server(short_dir / "s.sock", _no_photos, lambda p: None, idle_exit=1.0,
                    open_video=lambda shm: Session(Ring(bytearray(video.SIZE), init=True), _fake_depth()[0]))
    assert server.bind()
    thread = _start(server)
    s = _client(short_dir / "s.sock")
    s.sendall(b'{"op": "video", "shm": "/xrdome-1-1"}\n')
    assert _reply(s)["ok"] is True
    s.sendall(b'{"op": "video", "shm": "/xrdome-1-2"}\n')  # a new ring replaces the first
    assert _reply(s)["shm"] == "/xrdome-1-2"
    assert closed == [True]
    s.close()
    assert _wait(lambda: len(closed) == 2)
    thread.join(5)


# ---- the launcher ----------------------------------------------------------------------

@pytest.fixture
def paths(monkeypatch, tmp_path):
    for var in ("XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME"):
        monkeypatch.setenv(var, str(tmp_path / var.lower()))
    return config.paths()


def test_bench_video_is_a_subcommand():
    args = cli.build_parser().parse_args(["depth", "bench-video", "--seconds", "5", "--rate", "60"])
    assert (args.what, args.seconds, args.rate) == ("bench-video", 5.0, 60.0)
    args = cli.build_parser().parse_args(["depth", "bench-video"])
    assert (args.seconds, args.rate) == (30.0, 30.0)


def test_bench_video_argv(paths):
    assert depth.bench_video_argv(paths, 30, 30, cpu=False) == [
        str(depthenv.python(paths)), "-u", str(depth.VIDEO_SCRIPT), "--bench",
        "--models-dir", str(depthenv.ncnn_dir(paths)), "--seconds", "30", "--rate", "30"]
    assert depth.bench_video_argv(paths, 2.5, 60, cpu=True)[-5:] == ["--seconds", "2.5", "--rate", "60", "--cpu"]
    assert depth.VIDEO_SCRIPT.is_file()


def _bench(seconds=30.0, rate=30.0):
    return depth.run(argparse.Namespace(what="bench-video", seconds=seconds, rate=rate))


def test_bench_video_refuses_without_setup(paths):
    with pytest.raises(ToolError, match="setup depth") as e:
        _bench()
    assert e.value.code == EXIT_SETUP


def test_bench_video_runs_the_loop(paths, monkeypatch, capsys):
    monkeypatch.setattr(depthenv, "installed", lambda p: None)
    monkeypatch.setattr(depthenv, "variant", lambda p: "cpu")
    ran = []
    monkeypatch.setattr(depth.Runner, "run", lambda self, argv, **kw: ran.append(argv))
    with pytest.raises(ToolError, match="more than 0"):
        _bench(seconds=0)
    assert _bench(seconds=10) == 0
    assert ran == [depth.bench_video_argv(paths, 10, 30, cpu=True)]
    assert "CPU" in capsys.readouterr().out


def test_bench_main_reports_a_missing_backend(capsys):
    from xrdome_depth.backend import BackendError

    class Broken:
        def __init__(self, models_dir, vulkan):
            pass

        def infer(self, chw, w, h):
            raise BackendError("model file x is missing; run `xrdome-tools setup depth`")

    pytest.importorskip("numpy", reason="numpy is not installed (the depth venv has it)")
    assert video.main(["--bench", "--models-dir", "/nowhere", "--seconds", "1"], make_backend=Broken) == 2
    assert "model file x is missing" in capsys.readouterr().err
