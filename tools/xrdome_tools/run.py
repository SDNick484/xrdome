"""Run child programs: argv lists only, never a shell."""

import os
import shlex
import shutil
import signal
import subprocess
import sys
import time
from collections import deque
from pathlib import Path
from typing import TextIO


class ChildFailed(Exception):
    def __init__(self, argv: list[str], returncode: int, stderr_tail: list[str]):
        super().__init__(f"{shlex.join(argv)} exited with status {returncode}")
        self.argv = argv
        self.returncode = returncode
        self.stderr_tail = stderr_tail


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
        # The child gets its own process group so an interrupt can take its
        # helpers down with it. stderr is always piped: its tail is the error.
        proc = subprocess.Popen(
            argv,
            stdout=subprocess.PIPE if capture else None,
            stderr=subprocess.PIPE,
            text=True,
            errors="replace",
            start_new_session=True,
        )
        tail: deque[str] = deque(maxlen=20)
        try:
            out, err = proc.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            _kill_group(proc)
            for p in cleanup:
                _remove(p)
            raise
        except BaseException:  # KeyboardInterrupt and anything else
            _kill_group(proc)
            for p in cleanup:
                _remove(p)
            raise
        elapsed = time.monotonic() - start
        if self.log is not None:
            self.log.write(f"$ {line}\n")
            if err:
                self.log.write(err if err.endswith("\n") else err + "\n")
            self.log.write(f"# exit {proc.returncode} after {elapsed:.2f} s\n")
            self.log.flush()
        if proc.returncode != 0:
            tail.extend(err.splitlines())
            for p in cleanup:
                _remove(p)
            raise ChildFailed(argv, proc.returncode, list(tail))
        if err and not capture:
            sys.stderr.write(err)
        return subprocess.CompletedProcess(argv, 0, out or "", err or "")
