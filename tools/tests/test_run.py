import io
import os
import signal
import sys
import threading
import time
from pathlib import Path

import pytest

from xrdome_tools.run import ChildFailed, Runner, safe_arg, say_path


def test_dry_run_prints_quoted(capsys, monkeypatch):
    def no_popen(*a, **k):
        raise AssertionError("process started")

    monkeypatch.setattr("subprocess.Popen", no_popen)
    r = Runner(dry_run=True).run(["ffmpeg", "-i", "a b.mkv"])
    assert capsys.readouterr().out == "ffmpeg -i 'a b.mkv'\n"
    assert r.returncode == 0 and r.stdout == ""


def test_child_failed_tail():
    code = "import sys\nfor i in range(30): print('l%d' % i, file=sys.stderr)\nsys.exit(4)"
    with pytest.raises(ChildFailed) as e:
        Runner().run([sys.executable, "-c", code])
    assert e.value.returncode == 4
    assert e.value.stderr_tail == [f"l{i}" for i in range(10, 30)]


def test_runner_argv_safe():
    assert safe_arg(Path("-x.mkv")) == "./-x.mkv"
    assert safe_arg(Path("a.mkv")) == "a.mkv"
    name = "it's a \"film\" é-ü.mkv"
    r = Runner().run([sys.executable, "-c", "import sys;print(sys.argv[1])", name], capture=True)
    assert r.stdout.strip() == name


def test_runner_interrupt_cleans(tmp_path):
    f = tmp_path / "partial.mkv"
    f.write_text("x")
    d = tmp_path / "frames"
    d.mkdir()
    (d / "0001.png").write_text("x")
    pidfile = tmp_path / "pid"
    code = f"import os,time;open({str(pidfile)!r},'w').write(str(os.getpid()));time.sleep(60)"

    def interrupt():
        for _ in range(100):
            if pidfile.exists() and pidfile.read_text():
                break
            time.sleep(0.05)
        os.kill(os.getpid(), signal.SIGINT)

    t = threading.Thread(target=interrupt)
    t.start()
    try:
        with pytest.raises(KeyboardInterrupt):
            Runner().run([sys.executable, "-c", code], cleanup=[f, d])
    finally:
        t.join()
    assert not f.exists() and not d.exists()
    pid = int(pidfile.read_text())
    for _ in range(50):
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            break
        time.sleep(0.05)
    else:
        pytest.fail("child still running")


def test_log_records_argv():
    log = io.StringIO()
    Runner(log=log).run([sys.executable, "-c", "pass"])
    text = log.getvalue()
    assert "-c pass" in text and " s" in text


def test_say_path(capsys):
    say_path("encoder", "hevc_nvenc", "NVIDIA present")
    assert capsys.readouterr().out == "path: encoder=hevc_nvenc (NVIDIA present)\n"


class _Recorder(io.StringIO):
    """A stand-in stderr that runs a callback on each write."""

    def __init__(self, on_write):
        super().__init__()
        self.on_write = on_write

    def write(self, s):
        n = super().write(s)
        self.on_write(self.getvalue())
        return n


def test_stderr_streams_while_child_runs(tmp_path, monkeypatch):
    # The child prints progress, then waits for proof that it was seen. If stderr were
    # only read at exit, the child would give up and fail.
    go = tmp_path / "seen"
    code = (
        "import os,sys,time\n"
        "sys.stderr.write('frames: 10%\\r'); sys.stderr.flush()\n"
        f"for _ in range(200):\n    if os.path.exists({str(go)!r}): sys.exit(0)\n    time.sleep(0.05)\n"
        "sys.exit(7)"
    )
    log = io.StringIO()
    seen_log: list[str] = []

    def on_write(text):
        if "frames: 10%" in text and not go.exists():
            seen_log.append(log.getvalue())
            go.write_text("y")

    monkeypatch.setattr(sys, "stderr", _Recorder(on_write))
    r = Runner(log=log).run([sys.executable, "-c", code])
    assert r.returncode == 0
    assert go.exists()
    assert "$ " in seen_log[0]  # the command is in the log before the child ends


def test_carriage_returns_collapse_in_tail_and_log(monkeypatch):
    monkeypatch.setattr(sys, "stderr", io.StringIO())
    code = "import sys; sys.stderr.write('a 1%\\ra 50%\\ra 100%\\nboom\\n'); sys.exit(3)"
    log = io.StringIO()
    with pytest.raises(ChildFailed) as e:
        Runner(log=log).run([sys.executable, "-c", code])
    assert e.value.stderr_tail == ["a 100%", "boom"]
    assert e.value.shown is True
    logged = log.getvalue().splitlines()
    assert logged[0].startswith("$ ") and logged[-1].startswith("# exit 3")
    assert logged[1:-1] == ["a 100%", "boom"]


def test_captured_child_is_not_streamed(capsys):
    code = "import sys\nprint('out'); sys.stderr.write('quiet\\n'); sys.exit(2)"
    with pytest.raises(ChildFailed) as e:
        Runner().run([sys.executable, "-c", code], capture=True)
    assert e.value.shown is False and e.value.stderr_tail == ["quiet"]
    assert "quiet" not in capsys.readouterr().err
