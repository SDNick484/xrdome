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
