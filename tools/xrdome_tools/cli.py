"""Command line entry point. Each subcommand is a module under xrdome_tools."""

import argparse
import importlib
import sys

from . import __version__
from .errors import EXIT_INTERRUPTED, ToolError
from .run import ChildFailed

# Append one module name per line (keep the trailing comma).
SUBCOMMANDS: list[str] = [
    "doctor",
    "transcode",
]


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="xrdome-tools",
        description="Prepare video for xrdome.",
    )
    parser.add_argument("--version", action="version", version=f"xrdome-tools {__version__}")
    sub = parser.add_subparsers(dest="subcommand", metavar="SUBCOMMAND")
    for name in SUBCOMMANDS:
        importlib.import_module(f"{__package__}.{name}").add_parser(sub)
    return parser


def main(argv: list[str] | None = None) -> int:
    try:
        parser = build_parser()
        args = parser.parse_args(argv)
        if not args.subcommand:
            parser.print_usage(sys.stderr)
            return 2
        module = importlib.import_module(f"{__package__}.{args.subcommand}")
        return int(module.run(args) or 0)
    except ToolError as e:
        print(f"xrdome-tools: {e}", file=sys.stderr)
        return e.code
    except ChildFailed as e:
        print(f"xrdome-tools: {e}", file=sys.stderr)
        for line in e.stderr_tail:
            print(line, file=sys.stderr)
        return e.returncode
    except KeyboardInterrupt:
        print("xrdome-tools: interrupted", file=sys.stderr)
        return EXIT_INTERRUPTED
