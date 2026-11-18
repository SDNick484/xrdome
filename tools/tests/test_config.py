from pathlib import Path

from xrdome_tools.config import Paths, paths, program


def test_xdg_defaults():
    p = paths({"HOME": "/h"})
    assert p.config_file == Path("/h/.config/xrdome/tools.ini")
    assert p.data_dir == Path("/h/.local/share/xrdome-tools")
    assert p.cache_dir == Path("/h/.cache/xrdome-tools")
    assert p.iw3_dir == p.data_dir / "iw3"
    assert p.models_dir == p.data_dir / "models"


def test_xdg_overrides():
    p = paths({"HOME": "/h", "XDG_DATA_HOME": "/d", "XDG_CONFIG_HOME": "/c", "XDG_CACHE_HOME": "/k"})
    assert p.data_dir == Path("/d/xrdome-tools")
    assert p.config_file == Path("/c/xrdome/tools.ini")
    assert p.cache_dir == Path("/k/xrdome-tools")


def test_relative_xdg_ignored():
    assert paths({"HOME": "/h", "XDG_DATA_HOME": "rel"}).data_dir == Path("/h/.local/share/xrdome-tools")


def _paths_for(tmp_path):
    return Paths(tmp_path / "tools.ini", tmp_path, tmp_path, tmp_path, tmp_path)


def test_program_override(tmp_path):
    p = _paths_for(tmp_path)
    p.config_file.write_text("[programs]\nffmpeg = /opt/ff/bin/ffmpeg\n")
    assert program("ffmpeg", p) == "/opt/ff/bin/ffmpeg"
    assert program("ffprobe", p) == "ffprobe"


def test_program_missing_file(tmp_path):
    assert program("ffmpeg", _paths_for(tmp_path)) == "ffmpeg"
