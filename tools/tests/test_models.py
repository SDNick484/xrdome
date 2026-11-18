import hashlib
import http.server
import threading
import urllib.request

import pytest

from xrdome_tools import cli, config, models
from xrdome_tools.errors import ToolError
from xrdome_tools.run import Runner


def _paths(tmp_path):
    return config.paths({"HOME": str(tmp_path), "XDG_DATA_HOME": str(tmp_path / "d")})


def _entry(**kw):
    base = dict(name="T", used_by="to3d", licence="MIT", page="https://example.org/p",
                url="", revision="r", file="t.pth", sha256=hashlib.sha256(b"x").hexdigest(),
                dest="iw3/t")
    base.update(kw)
    return models.Model(**base)


def test_registry_licences(tmp_path, monkeypatch):
    assert all(m.licence in models.ALLOWED_LICENCES for m in models.REGISTRY)
    bad = _entry(name="NC", licence="CC-BY-NC-4.0")
    monkeypatch.setattr(models, "REGISTRY", [*models.REGISTRY, bad])
    p = _paths(tmp_path)
    t = models.target(bad, p)
    t.parent.mkdir(parents=True)
    t.write_bytes(b"x")  # present on disk, still refused
    with pytest.raises(ToolError, match="not an allowed model") as e:
        models.require("NC", p)
    assert e.value.code == 2


def test_status(tmp_path):
    p, m = _paths(tmp_path), _entry()
    assert models.status(m, p) == "missing"
    t = models.target(m, p)
    t.parent.mkdir(parents=True)
    t.write_bytes(b"y")
    assert models.status(m, p) == "bad hash"
    t.write_bytes(b"x")
    assert models.status(m, p) == "ok"


def test_instructions(tmp_path, monkeypatch, capsys):
    monkeypatch.setenv("XDG_DATA_HOME", str(tmp_path / "d"))
    monkeypatch.setattr(models, "REGISTRY", [_entry()])
    assert cli.main(["models"]) == 0
    out = capsys.readouterr().out
    assert "https://example.org/p" in out
    assert str(tmp_path / "d" / "xrdome-tools" / "iw3" / "t" / "t.pth") in out
    assert "xrdome-tools models --check" in out


def test_check_exit_code(tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_DATA_HOME", str(tmp_path / "d"))
    monkeypatch.setattr(models, "REGISTRY", [_entry()])
    assert cli.main(["models", "--check"]) == 2


def test_unknown_file_warned(tmp_path, monkeypatch, capsys):
    monkeypatch.setenv("XDG_DATA_HOME", str(tmp_path / "d"))
    monkeypatch.setattr(models, "REGISTRY", [_entry()])
    d = _paths(tmp_path).data_dir / "iw3" / "t"
    d.mkdir(parents=True)
    (d / "stray.pth").write_bytes(b"?")
    cli.main(["models"])
    assert "stray.pth" in capsys.readouterr().out


def _serve(body: bytes):
    class H(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *a):
            pass

    srv = http.server.HTTPServer(("127.0.0.1", 0), H)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv, f"http://127.0.0.1:{srv.server_port}/t.pth"


def test_fetch_hash_mismatch(tmp_path):
    p = _paths(tmp_path)
    srv, url = _serve(b"wrong")
    try:
        m = _entry(url=url)
        with pytest.raises(ToolError, match="mismatch") as e:
            models.fetch(Runner(), m, p)
        assert e.value.code == 2
        t = models.target(m, p)
        assert not t.exists() and not t.with_name(t.name + ".part").exists()
    finally:
        srv.shutdown()


def test_fetch_ok(tmp_path):
    p = _paths(tmp_path)
    srv, url = _serve(b"x")
    try:
        m = _entry(url=url)
        t = models.fetch(Runner(), m, p)
        assert t.read_bytes() == b"x" and models.status(m, p) == "ok"
        assert not t.with_name(t.name + ".part").exists()
    finally:
        srv.shutdown()


def test_fetch_never_automatic(tmp_path, monkeypatch):
    def boom(*a, **k):
        raise AssertionError("network used")

    monkeypatch.setattr(urllib.request, "urlopen", boom)
    p = _paths(tmp_path)
    with pytest.raises(ToolError):
        models.require("Any_V2_S", p)


def test_fetch_subcommand_monkeypatched(tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_DATA_HOME", str(tmp_path / "d"))
    m = _entry(url="https://example.invalid/t.pth")
    monkeypatch.setattr(models, "REGISTRY", [m])
    calls = []

    def fake(runner, mod, p):
        calls.append(mod.name)
        return models.target(mod, p)

    monkeypatch.setattr(models, "fetch", fake)
    assert cli.main(["models", "--fetch", "T"]) == 0
    assert calls == ["T"]


def test_iw3_entries_not_fetched(tmp_path):
    m = next(m for m in models.REGISTRY if m.fetched_by == "iw3")
    with pytest.raises(ToolError, match="iw3"):
        models.fetch(Runner(), m, _paths(tmp_path))
