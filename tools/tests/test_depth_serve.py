"""`depth serve`: the protocol, the queue and the socket loop with a fake depth maker (no numpy), and the
launcher's refusals and argv. Standard library only."""

import argparse
import json
import os
import shutil
import socket
import tempfile
import threading
import time
from pathlib import Path

import pytest

from xrdome_depth import serve
from xrdome_depth.serve import ProtocolError, Queue, Request, Server
from xrdome_tools import cli, config, depth, depthenv
from xrdome_tools.errors import EXIT_SETUP, ToolError


# ---- protocol ----------------------------------------------------------------

def test_parse_request():
    r = serve.parse_request(b'{"op": "photo", "path": "/p/a.jpg", "prefetch": true}')
    assert r == Request("/p/a.jpg", True)
    assert serve.parse_request(b'{"op":"photo","path":"/a"}') == Request("/a", False)


@pytest.mark.parametrize("line, msg, path", [
    (b"nope", "not JSON", None),
    (b"[1]", "not a JSON object", None),
    (b'{"op": "video", "path": "/a"}', "unknown op", "/a"),
    (b'{"op": "photo"}', "needs a \"path\"", None),
    (b'{"op": "photo", "path": 3}', "needs a \"path\"", None),
    (b'{"op": "photo", "path": "a.jpg"}', "absolute", "a.jpg"),
    (b'{"op": "photo", "path": "/a", "prefetch": 1}', "prefetch", "/a"),
])
def test_parse_request_refuses(line, msg, path):
    with pytest.raises(ProtocolError, match=msg) as e:
        serve.parse_request(line)
    assert e.value.path == path


def test_paths_travel_as_bytes():
    raw = b"/p/caf\xe9 \xc3\xa9\"\\.jpg"  # Latin-1 byte, real UTF-8, quote, backslash
    line = b'{"op": "photo", "path": "' + raw.replace(b"\\", b"\\\\").replace(b'"', b'\\"') + b'"}'
    req = serve.parse_request(line)
    assert os.fsencode(req.path) == raw
    out = serve.encode_reply(serve.ok_reply(req.path, Path("/c/k.pgm")))
    assert out.endswith(b"\n") and out.count(b"\n") == 1
    assert b"caf\xe9 \xc3\xa9" in out  # bytes back as they came, not \u escapes
    back = json.loads(out.decode("utf-8", "surrogateescape"))
    assert back == {"ok": True, "path": req.path, "depth": "/c/k.pgm"}


def test_error_reply():
    assert serve.error_reply("/a", "boom") == {"ok": False, "path": "/a", "error": "boom"}
    assert serve.error_reply(None, "bad") == {"ok": False, "error": "bad"}


# ---- queue -------------------------------------------------------------------

def _order(q):
    out = []
    while (job := q.pop()) is not None:
        out.append(job.path)
    return out


def test_newest_first_prefetches_last():
    q = Queue()
    for path, pre in [("/a", False), ("/n1", True), ("/b", False), ("/n2", True), ("/c", False)]:
        q.add(Request(path, pre), "conn")
    assert _order(q) == ["/c", "/b", "/a", "/n2", "/n1"]


def test_asking_again_makes_it_newest_and_not_a_prefetch():
    q = Queue()
    q.add(Request("/a", True), "x")
    q.add(Request("/b", False), "x")
    q.add(Request("/a", False), "y")
    q.add(Request("/a", False), "y")  # the same waiter once
    job = q.pop()
    assert (job.path, job.prefetch, job.waiters) == ("/a", False, ["x", "y"])
    q.add(Request("/c", False), "x")
    q.add(Request("/c", True), "x")  # a prefetch does not demote it
    assert [q.pop().path, q.pop().path] == ["/c", "/b"]


def test_full_queue_drops_the_oldest_prefetch():
    q = Queue(max_jobs=3)
    q.add(Request("/a", False), "x")
    q.add(Request("/p", True), "x")
    q.add(Request("/q", True), "x")
    dropped = q.add(Request("/b", False), "x")
    assert [j.path for j in dropped] == ["/p"]
    dropped = q.add(Request("/c", False), "x")
    assert [j.path for j in dropped] == ["/q"]
    dropped = q.add(Request("/d", False), "x")
    assert [j.path for j in dropped] == ["/a"]
    assert _order(q) == ["/d", "/c", "/b"]


def test_forget_drops_jobs_nobody_waits_for():
    q = Queue()
    q.add(Request("/a", False), "x")
    q.add(Request("/b", False), "x")
    q.add(Request("/b", False), "y")
    q.forget("x")
    assert _order(q) == ["/b"]


# ---- the socket loop -----------------------------------------------------------

@pytest.fixture
def short_dir():
    # A Unix socket path must fit in about 100 bytes: pytest's tmp_path can be longer.
    d = Path(tempfile.mkdtemp(prefix="xrd-", dir="/tmp"))
    yield d
    shutil.rmtree(d, ignore_errors=True)


class Fake:
    """Writes <name>.pgm for each photo; blocks while the gate is closed; records the order."""

    def __init__(self, out: Path, fail: set[str] = frozenset()):
        self.out = out
        self.order = []
        self.gate = threading.Event()
        self.gate.set()
        self.fail = fail

    def process(self, p: Path) -> Path:
        self.gate.wait(10)
        self.order.append(p.name)
        if p.name in self.fail:
            raise ValueError(f"cannot decode {p.name}")
        dest = self.out / (p.name + ".pgm")
        dest.write_bytes(b"P5\n1 1\n65535\n\0\0")
        return dest

    def cached(self, p: Path):
        if not p.exists():
            raise FileNotFoundError(2, "No such file or directory")
        dest = self.out / (p.name + ".pgm")
        return dest if dest.exists() else None


class Running:
    def __init__(self, server: Server):
        self.server = server
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def _run(self):
        try:
            self.server.serve()
        finally:
            self.server.close()


def _client(path: Path) -> socket.socket:
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(10)
    s.connect(str(path))
    return s


def _ask(s, path, prefetch=False):
    s.sendall(json.dumps({"op": "photo", "path": str(path), "prefetch": prefetch}).encode() + b"\n")


def _replies(s, n):
    buf = b""
    while buf.count(b"\n") < n:
        data = s.recv(4096)
        assert data, "the server closed the connection"
        buf += data
    return [json.loads(line) for line in buf.decode().splitlines()]


def _photos(d: Path, *names):
    for n in names:
        (d / n).write_bytes(n.encode())
    return [d / n for n in names]


def test_serves_newest_first_over_the_socket(short_dir):
    fake = Fake(short_dir)
    a, b, c, n = _photos(short_dir, "a.jpg", "b.jpg", "c.jpg", "n.jpg")
    server = Server(short_dir / "s.sock", fake.process, fake.cached, idle_exit=30)
    assert server.bind()
    assert (os.stat(short_dir / "s.sock").st_mode & 0o777) == 0o600
    Running(server)
    s = _client(short_dir / "s.sock")
    fake.gate.clear()
    _ask(s, a)
    time.sleep(0.3)  # a is being made; the rest wait in the socket
    _ask(s, b)
    _ask(s, n, prefetch=True)
    _ask(s, c)
    time.sleep(0.3)
    fake.gate.set()
    got = _replies(s, 4)
    assert fake.order == ["a.jpg", "c.jpg", "b.jpg", "n.jpg"]
    assert [r["path"] for r in got] == [str(a), str(c), str(b), str(n)]
    assert all(r["ok"] and r["depth"] == str(short_dir / (Path(r["path"]).name + ".pgm")) for r in got)
    # Cached now: answered at once, nothing made again.
    _ask(s, b)
    assert _replies(s, 1) == [{"ok": True, "path": str(b), "depth": str(short_dir / "b.jpg.pgm")}]
    assert fake.order == ["a.jpg", "c.jpg", "b.jpg", "n.jpg"]
    s.close()


def test_errors_come_back_and_the_service_goes_on(short_dir):
    fake = Fake(short_dir, fail={"bad.jpg"})
    bad, good = _photos(short_dir, "bad.jpg", "good.jpg")
    server = Server(short_dir / "s.sock", fake.process, fake.cached, idle_exit=30)
    assert server.bind()
    Running(server)
    s = _client(short_dir / "s.sock")
    s.sendall(b"garbage\n")
    _ask(s, short_dir / "missing.jpg")
    _ask(s, bad)
    got = _replies(s, 3)
    assert got[0]["ok"] is False and "not JSON" in got[0]["error"] and "path" not in got[0]
    assert got[1]["ok"] is False and got[1]["path"] == str(short_dir / "missing.jpg")
    assert "No such file" in got[1]["error"]
    assert got[2] == {"ok": False, "path": str(bad), "error": "cannot decode bad.jpg"}
    _ask(s, good)
    assert _replies(s, 1)[0]["ok"] is True
    s.close()


def test_a_second_helper_steps_aside_and_a_stale_socket_is_replaced(short_dir):
    fake = Fake(short_dir)
    sock = short_dir / "s.sock"
    stale = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    stale.bind(str(sock))
    stale.close()  # the file stays, nobody listens
    first = Server(sock, fake.process, fake.cached, idle_exit=30)
    assert first.bind()
    second = Server(sock, fake.process, fake.cached, idle_exit=30)
    assert not second.bind()
    first.close()
    assert not sock.exists()


def test_exits_when_idle_and_removes_its_socket(short_dir):
    fake = Fake(short_dir)
    server = Server(short_dir / "s.sock", fake.process, fake.cached, idle_exit=0.3)
    assert server.bind()
    run = Running(server)
    s = _client(short_dir / "s.sock")
    time.sleep(0.6)
    assert run.thread.is_alive()  # a client is connected
    s.close()
    run.thread.join(5)
    assert not run.thread.is_alive()
    assert not (short_dir / "s.sock").exists()


def test_a_client_that_leaves_cancels_its_jobs(short_dir):
    fake = Fake(short_dir)
    a, b = _photos(short_dir, "a.jpg", "b.jpg")
    server = Server(short_dir / "s.sock", fake.process, fake.cached, idle_exit=30)
    assert server.bind()
    Running(server)
    s = _client(short_dir / "s.sock")
    fake.gate.clear()
    _ask(s, a)
    time.sleep(0.3)
    _ask(s, b)
    time.sleep(0.1)
    s.close()
    time.sleep(0.1)
    fake.gate.set()
    s2 = _client(short_dir / "s.sock")  # the loop is past a and has read the close
    _ask(s2, a)
    assert _replies(s2, 1)[0]["ok"] is True
    assert fake.order == ["a.jpg"]
    s2.close()


# ---- the launcher ----------------------------------------------------------------

@pytest.fixture
def paths(monkeypatch, tmp_path):
    for var in ("XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME"):
        monkeypatch.setenv(var, str(tmp_path / var.lower()))
    return config.paths()


def test_socket_path():
    assert depth.socket_path({"XDG_RUNTIME_DIR": "/run/user/7"}) == Path("/run/user/7/xrdome-depth.sock")
    assert depth.socket_path({"XDG_RUNTIME_DIR": "rel", "TMPDIR": "/var/t/"}, uid=7) == \
        Path("/var/t/xrdome-depth-7.sock")
    assert depth.socket_path({"TMPDIR": "rel"}, uid=7) == Path("/tmp/xrdome-depth-7.sock")
    assert depth.socket_path({}, uid=7) == Path("/tmp/xrdome-depth-7.sock")


def test_serve_is_a_subcommand():
    args = cli.build_parser().parse_args(["depth", "serve", "--socket", "/s", "--idle-exit", "5"])
    assert (args.what, args.socket, args.idle_exit) == ("serve", Path("/s"), 5.0)
    args = cli.build_parser().parse_args(["depth", "serve"])
    assert (args.socket, args.idle_exit) == (None, depth.IDLE_EXIT_S)


def test_serve_argv(paths):
    argv = depth.serve_argv(paths, Path("/run/s.sock"), 600)
    assert argv == [str(depthenv.python(paths)), "-u", str(depth.SERVE_SCRIPT), "--socket", "/run/s.sock",
                    "--cache-dir", str(depth.cache_dir(paths)), "--models-dir", str(depthenv.ncnn_dir(paths)),
                    "--idle-exit", "600"]
    assert depth.SERVE_SCRIPT.is_file()


def _serve(socket_=None, idle=600.0):
    return depth.run(argparse.Namespace(what="serve", socket=socket_, idle_exit=idle))


def test_serve_refuses_without_setup(paths):
    with pytest.raises(ToolError, match="setup depth") as e:
        _serve()
    assert e.value.code == EXIT_SETUP


def test_serve_refuses_the_cpu(paths, monkeypatch):
    monkeypatch.setattr(depthenv, "installed", lambda p: None)
    monkeypatch.setattr(depthenv, "variant", lambda p: "cpu")
    with pytest.raises(ToolError, match="GPU only") as e:
        _serve()
    assert e.value.code == EXIT_SETUP


def test_serve_runs_the_helper(paths, monkeypatch):
    monkeypatch.setattr(depthenv, "installed", lambda p: None)
    monkeypatch.setattr(depthenv, "variant", lambda p: "vulkan")
    monkeypatch.setenv("XDG_RUNTIME_DIR", "/run/user/9")
    ran = []
    monkeypatch.setattr(depth.Runner, "run", lambda self, argv, **kw: ran.append(argv))
    assert _serve(idle=30) == 0
    assert ran == [depth.serve_argv(paths, Path("/run/user/9/xrdome-depth.sock"), 30)]
    assert _serve(socket_=Path("/x.sock")) == 0
    assert ran[1] == depth.serve_argv(paths, Path("/x.sock"), 600)
