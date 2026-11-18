import sys
import types

import pytest

from xrdome_tools import cli
from xrdome_tools.errors import ToolError


def _fake(monkeypatch, name, run):
    mod = types.ModuleType(f"xrdome_tools.{name}")
    mod.add_parser = lambda sub: sub.add_parser(name, help="fake")
    mod.run = run
    monkeypatch.setitem(sys.modules, f"xrdome_tools.{name}", mod)
    monkeypatch.setattr(cli, "SUBCOMMANDS", [name])


def test_help_lists_subcommands(monkeypatch, capsys):
    _fake(monkeypatch, "fakecmd", lambda a: 0)
    with pytest.raises(SystemExit) as e:
        cli.main(["--help"])
    assert e.value.code == 0
    assert "fakecmd" in capsys.readouterr().out


def test_toolerror_exit_code(monkeypatch, capsys):
    def boom(args):
        raise ToolError("nope", code=2)

    _fake(monkeypatch, "boomcmd", boom)
    assert cli.main(["boomcmd"]) == 2
    assert capsys.readouterr().err.startswith("xrdome-tools: nope")


def test_child_failed_exit_status(monkeypatch, capsys):
    from xrdome_tools.run import ChildFailed

    def boom(args):
        raise ChildFailed(["x"], 7, ["last line"])

    _fake(monkeypatch, "childcmd", boom)
    assert cli.main(["childcmd"]) == 7
    assert "last line" in capsys.readouterr().err


def test_interrupt_exit_130(monkeypatch):
    def boom(args):
        raise KeyboardInterrupt

    _fake(monkeypatch, "intcmd", boom)
    assert cli.main(["intcmd"]) == 130
