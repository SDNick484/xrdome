"""Run child programs: argv lists only, never a shell."""

import codecs
import os
import shlex
import shutil
import signal
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import TextIO


class ChildFailed(Exception):
    def __init__(self, argv: list[str], returncode: int, stderr_tail: list[str], shown: bool = False):
        super().__init__(f"{shlex.join(argv)} exited with status {returncode}")
        self.argv = argv
        self.returncode = returncode
        self.stderr_tail = stderr_tail
        self.shown = shown  # the tail is already on the terminal (stderr was streamed)


def _lines(text: str) -> list[str]:
    """Lines as a terminal shows them: of each line's carriage-return rewrites, only the last."""
    out = []
    for raw in text.split("\n"):
        parts = [part for part in raw.split("\r") if part.strip()]
        if parts:
            out.append(parts[-1])
    return out


def safe_arg(p: Path) -> str:
    """A path as an argument: a leading '-' would read as an option, so use ./name."""
    s = os.fspath(p)
    return "./" + s if s.startswith("-") else s


def say_path(key: str, value: str, reason: str) -> None:
    """A decision line: 'path: <key>=<value> (<reason>)'."""
    print(f"path: {key}={value} ({reason})")


def _remove(p: Path) -> None:
    try:
        if p.is_dir() and not p.is_symlink():
            shutil.rmtree(p, ignore_errors=True)
        else:
            p.unlink(missing_ok=True)
    except OSError:
        pass


def _kill_group(proc: subprocess.Popen) -> None:
    try:
        os.killpg(proc.pid, signal.SIGKILL)
    except (ProcessLookupError, PermissionError):
        pass
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        pass


class Runner:
    def __init__(self, dry_run: bool = False, log: TextIO | None = None):
        self.dry_run = dry_run
        self.log = log

    def run(
        self,
        argv: list[str],
        *,
        capture: bool = False,
        timeout: float | None = None,
        cleanup: list[Path] = [],
    ) -> subprocess.CompletedProcess:
        line = shlex.join(argv)
        if self.dry_run:
            print(line)
            return subprocess.CompletedProcess(argv, 0, "", "")
        start = time.monotonic()
        if self.log is not None:
            self.log.write(f"$ {line}\n")
            self.log.flush()
        # The child gets its own process group so an interrupt can take its
        # helpers down with it. stderr is always piped: its tail is the error.
        # Without capture it is also copied to our stderr as it arrives, so a
        # long child's progress bar shows while it runs.
        stream = not capture
        proc = subprocess.Popen(
            argv,
            stdout=subprocess.PIPE if capture else None,
            stderr=subprocess.PIPE,
            text=not stream,
            errors=None if stream else "replace",
            start_new_session=True,
        )
        chunks: list[str] = []
        reader = threading.Thread(target=_copy_stderr, args=(proc.stderr, chunks), daemon=True) \
            if stream else None
        try:
            if reader is not None:
                reader.start()
                proc.wait(timeout=timeout)
                reader.join()
                out, err = None, "".join(chunks)
            else:
                out, err = proc.communicate(timeout=timeout)
        except BaseException:  # timeout, KeyboardInterrupt and anything else
            _kill_group(proc)
            if reader is not None:
                reader.join(timeout=5)
            for p in cleanup:
                _remove(p)
            raise
        elapsed = time.monotonic() - start
        if self.log is not None:
            for ln in _lines(err or ""):
                self.log.write(ln + "\n")
            self.log.write(f"# exit {proc.returncode} after {elapsed:.2f} s\n")
            self.log.flush()
        if proc.returncode != 0:
            tail = _lines(err or "")[-20:]
            for p in cleanup:
                _remove(p)
            raise ChildFailed(argv, proc.returncode, tail, shown=stream)
        return subprocess.CompletedProcess(argv, 0, out or "", err or "")


def _copy_stderr(pipe, chunks: list[str]) -> None:
    """Copy a child's stderr to ours as it arrives, keeping the text."""
    dec = codecs.getincrementaldecoder("utf-8")(errors="replace")
    while True:
        data = pipe.read1(65536)
        text = dec.decode(data, final=not data)
        if text:
            chunks.append(text)
            try:
                sys.stderr.write(text)
                sys.stderr.flush()
            except (OSError, ValueError):
                pass
        if not data:
            break
    pipe.close()
