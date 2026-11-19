"""The `depth` launcher: refusals and the child's argv. Standard library only."""

import argparse
from pathlib import Path

import pytest

from xrdome_tools import cli, config, depth, depthenv
from xrdome_tools.errors import EXIT_SETUP, ToolError


@pytest.fixture
def paths(monkeypatch, tmp_path):
    for var in ("XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME"):
        monkeypatch.setenv(var, str(tmp_path / var.lower()))
    return config.paths()


def test_depth_is_a_subcommand():
    assert "depth" in cli.SUBCOMMANDS
    args = cli.build_parser().parse_args(["depth", "photo", "a.jpg", "--force"])
    assert (args.subcommand, args.what, args.input, args.force) == ("depth", "photo", Path("a.jpg"), True)


def test_cache_dir_is_the_specs(paths, tmp_path):
    assert depth.cache_dir(paths) == tmp_path / "xdg_cache_home" / "xrdome-tools" / "depth"


def test_photo_argv(paths):
    argv = depth.photo_argv(paths, Path("-odd.jpg"), cpu=False, force=False)
    assert argv[:3] == [str(depthenv.python(paths)), "-u", str(depth.PHOTO_SCRIPT)]
    assert argv[3:7] == ["--cache-dir", str(depth.cache_dir(paths)), "--models-dir", str(depthenv.ncnn_dir(paths))]
    assert argv[7:] == ["./-odd.jpg"]  # not read as an option
    full = depth.photo_argv(paths, Path("d"), cpu=True, force=True)
    assert full[-3:] == ["--cpu", "--force", "d"]
    assert depth.PHOTO_SCRIPT.is_file()


def _run(path):
    return depth.run(argparse.Namespace(input=Path(path), force=False))


def test_refuses_without_setup(paths, tmp_path):
    photo = tmp_path / "a.jpg"
    photo.write_bytes(b"x")
    with pytest.raises(ToolError, match="setup depth") as e:
        _run(photo)
    assert e.value.code == EXIT_SETUP


def test_refuses_a_missing_input(paths, tmp_path):
    with pytest.raises(ToolError, match="no such file"):
        _run(tmp_path / "nope.jpg")


def test_runs_the_helper(paths, tmp_path, monkeypatch, capsys):
    photo = tmp_path / "a.jpg"
    photo.write_bytes(b"x")
    monkeypatch.setattr(depthenv, "installed", lambda p: None)
    monkeypatch.setattr(depthenv, "variant", lambda p: "cpu")
    ran = []
    monkeypatch.setattr(depth.Runner, "run", lambda self, argv, **kw: ran.append(argv))
    assert _run(photo) == 0
    assert ran == [depth.photo_argv(paths, photo, cpu=True, force=False)]
    assert "CPU" in capsys.readouterr().out  # the warning of spec 4
