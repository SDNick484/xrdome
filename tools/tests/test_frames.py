import subprocess
from fractions import Fraction
from pathlib import Path

import pytest

from xrdome_tools import frames
from xrdome_tools.run import ChildFailed, Runner


def test_plan_chunks():
    chunks = frames.plan_chunks(610, (30, 1), 10, 1)
    assert chunks == [(0, 300), (299, 300), (598, 12)]
    for i in range(609):
        assert any(s <= i and i + 1 < s + n for s, n in chunks), i
    assert frames.plan_chunks(0, (30, 1), 10, 1) == []
    assert frames.plan_chunks(5, (30, 1), 10, 1) == [(0, 5)]


def test_interp_frame_counts():
    ratio = Fraction(60) / Fraction(24)
    chunks = frames.plan_chunks(240, (24, 1), 2, 1)
    assert len(chunks) > 1
    targets = frames.chunk_targets(chunks, 240, ratio)
    assert round(240 * ratio) == 600
    assert sum(keep for _, keep in targets) == 600
    assert all(ask >= keep for ask, keep in targets)
    # One chunk: the tool is asked for exactly the target.
    assert frames.chunk_targets([(0, 240)], 240, ratio) == [(600, 600)]
    # No rate change: the shared frame is kept once.
    same = frames.chunk_targets(frames.plan_chunks(610, (30, 1), 10, 1), 610, Fraction(1))
    assert [k for _, k in same] == [299, 299, 12]


def test_disk_estimate():
    # 1920x1080, 300 frames: input only.
    one = 1920 * 1080 * 3 * 300
    assert frames.disk_estimate(1920, 1080, 300, 1, 1) == 2 * one  # in + out
    assert frames.disk_estimate(1920, 1080, 300, 2, 1) == one + 4 * one
    # interpolate 2x and upscale 2x: in + interpolated + upscaled interpolated
    assert frames.disk_estimate(1920, 1080, 300, 2, 2) == one + 2 * one + 8 * one


class Fake(Runner):
    """Extract writes two PNGs; a tool copies them; fails where asked."""

    def __init__(self, fail_tool_on=None, interrupt_on=None):
        super().__init__()
        self.calls = []
        self.fail_tool_on = fail_tool_on
        self.interrupt_on = interrupt_on

    def run(self, argv, **kw):
        self.calls.append(argv)
        if argv[0] == "extract":
            d = Path(argv[1])
            for i in (1, 2):
                (d / f"{i:08d}.png").write_bytes(b"p")
        elif argv[0] == "tool":
            src, dst = Path(argv[1]), Path(argv[2])
            if self.interrupt_on and self.interrupt_on in src.name:
                raise KeyboardInterrupt
            if self.fail_tool_on and self.fail_tool_on in src.name:
                raise ChildFailed(argv, 3, ["tool broke"])
            for f in src.iterdir():
                (dst / f.name).write_bytes(f.read_bytes())
        return subprocess.CompletedProcess(argv, 0, "", "")


def _extract(src, start, count, d):
    return ["extract", str(d)]


def _tool(i, o):
    return ["tool", str(i), str(o)]


def test_run_chunks_yields_and_cleans(tmp_path):
    tmp = tmp_path / "t"
    tmp.mkdir()
    seen = []
    for d in frames.run_chunks(Fake(), tmp_path / "src.mp4", [(0, 2), (1, 2)], _tool, tmp,
                               extract=_extract):
        seen.append(sorted(p.name for p in d.iterdir()))
        # Only the chunk being handed out is on disk.
        assert list(tmp.iterdir()) == [d]
    assert seen == [["00000001.png", "00000002.png"]] * 2
    assert list(tmp.iterdir()) == []


def test_run_chunks_two_steps(tmp_path):
    tmp = tmp_path / "t"
    tmp.mkdir()
    r = Fake()
    outs = list(d.name for d in frames.run_chunks(r, Path("s"), [(0, 2)], [_tool, _tool], tmp,
                                                   extract=_extract))
    assert outs == ["00000.out"]
    tools = [c for c in r.calls if c[0] == "tool"]
    assert [Path(c[1]).name for c in tools] == ["00000.in", "00000.s1"]
    assert [Path(c[2]).name for c in tools] == ["00000.s1", "00000.out"]
    assert list(tmp.iterdir()) == []


def test_chunks_removed_on_failure(tmp_path):
    tmp = tmp_path / "t"
    tmp.mkdir()
    gen = frames.run_chunks(Fake(fail_tool_on="00001"), Path("s"), [(0, 2), (1, 2), (2, 2)],
                            _tool, tmp, extract=_extract)
    with pytest.raises(ChildFailed):
        for _ in gen:
            pass
    assert list(tmp.iterdir()) == []


def test_chunks_removed_on_interrupt_and_early_stop(tmp_path):
    tmp = tmp_path / "t"
    tmp.mkdir()
    with pytest.raises(KeyboardInterrupt):
        for _ in frames.run_chunks(Fake(interrupt_on="00001"), Path("s"), [(0, 2), (1, 2)],
                                   _tool, tmp, extract=_extract):
            pass
    assert list(tmp.iterdir()) == []
    gen = frames.run_chunks(Fake(), Path("s"), [(0, 2), (1, 2)], _tool, tmp, extract=_extract)
    next(gen)
    gen.close()  # the caller failed while encoding the first chunk
    assert list(tmp.iterdir()) == []


def test_default_extract_and_pattern(tmp_path):
    a = frames.extract_argv(Path("-odd.mp4"), 10, 5, tmp_path / "100%")
    assert "./-odd.mp4" in a
    assert a[-1].endswith("100%%/%08d.png")
    assert "select=between(n\\,10\\,14)" in a
