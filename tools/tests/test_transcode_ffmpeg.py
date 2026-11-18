"""transcode on real short clips with software encoders (runs in CI)."""

import json
import shutil
import subprocess
from pathlib import Path

import pytest

from xrdome_tools import cli, formats
from xrdome_tools.run import Runner
from xrdome_tools.transcode import transcode

ROOT = Path(__file__).resolve().parents[2]
PATTERN = ROOT / "tools" / "make_pattern.sh"


def _have_x265() -> bool:
    if not shutil.which("ffmpeg"):
        return False
    out = subprocess.run(["ffmpeg", "-hide_banner", "-encoders"], capture_output=True, text=True).stdout
    return "libx265" in out


pytestmark = pytest.mark.skipif(not _have_x265(), reason="needs ffmpeg with libx265")


def make(tmp_path, kind, name, audio=True, dur="2"):
    raw = tmp_path / ("raw_" + name)
    subprocess.run(["sh", str(PATTERN), "--kind", kind, dur, str(raw), "libx264",
                    *(["512x512"] if kind == "sbs180" else [])], check=True)
    if not audio:
        return raw
    out = tmp_path / name
    subprocess.run(["ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-y", "-i", str(raw),
                    "-f", "lavfi", "-i", "sine=d=2", "-map", "0:v", "-map", "1:a", "-c:v", "copy",
                    "-c:a", "aac", "-shortest", str(out)], check=True)
    return out


CASES = [
    ("sbs180", "dome.mp4", "side by side", "_VR180_LR"),
    ("flat2d", "plain.mp4", None, ""),
    ("flat-hsbs", "show_HSBS.mp4", "side by side", "_HSBS"),
    ("flat-tb", "stack.mp4", "top and bottom", "_TB"),
]


@pytest.mark.parametrize("kind,name,stereo,token", CASES)
def test_transcode_formats(tmp_path, kind, name, stereo, token, capsys):
    src = make(tmp_path, kind, name)
    sinfo = formats.probe(Runner(), src)
    f, _ = formats.detect(sinfo, src.name)
    dst = tmp_path / formats.output_name(src.stem, f)
    assert token in dst.name
    transcode(Runner(), src, dst, encoder="libx265", quality="small")
    out = formats.probe(Runner(), dst)
    assert out.codec == "hevc"
    assert (out.stereo if out.stereo != "2D" else None) == stereo
    assert abs(out.duration - sinfo.duration) < 0.5
    assert any(s["codec_type"] == "audio" for s in out.streams)
    assert "path: encode=libx265" in capsys.readouterr().out
    assert Path(str(dst) + ".log").exists()
    assert not Path(str(dst) + ".part").exists()


def test_remux_hevc(tmp_path, capsys):
    src = tmp_path / "pre.mkv"
    subprocess.run(["ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-y",
                    "-f", "lavfi", "-i", "testsrc2=s=320x240:d=1", "-c:v", "libx265", str(src)], check=True)
    dst = tmp_path / "remuxed.mkv"
    transcode(Runner(), src, dst, as_="hsbs")
    assert "transcode=remux" in capsys.readouterr().out
    out = formats.probe(Runner(), dst)
    assert out.codec == "hevc" and out.stereo == "side by side"


def test_dry_run_runs_nothing(tmp_path, capsys):
    src = make(tmp_path, "flat2d", "plain.mp4")
    before = set(tmp_path.iterdir())
    rc = cli.main(["transcode", str(src), "--dry-run", "--encoder", "libx265"])
    assert rc == 0
    assert set(tmp_path.iterdir()) == before
    assert "-f matroska" in capsys.readouterr().out


def test_cli_end_to_end_and_exists(tmp_path, capsys):
    src = make(tmp_path, "flat-hsbs", "my clip 'q' é.mp4", audio=False)
    assert cli.main(["transcode", str(src), "--encoder", "libx265", "--quality", "small",
                     "--as", "hsbs"]) == 0
    dst = tmp_path / "raw_my clip 'q' é_HSBS.mkv"
    assert dst.exists()
    assert cli.main(["transcode", str(src), "--encoder", "libx265", "--as", "hsbs"]) == 1
    assert cli.main(["transcode", str(src), "-o", str(src), "--force"]) == 2


def test_failed_encode_leaves_no_partial(tmp_path):
    bad = tmp_path / "bad.mp4"
    bad.write_bytes(b"not a video")
    assert cli.main(["transcode", str(bad), "--encoder", "libx265"]) == 2
    assert sorted(p.name for p in tmp_path.iterdir()) == ["bad.mp4"]
