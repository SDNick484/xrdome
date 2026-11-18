# xrdome-tools

`xrdome-tools` prepares video for xrdome. Design: `docs/superpowers/specs/2026-11-17-xrdome-tools-design.md`.
Plan: `docs/superpowers/plans/2026-11-17-xrdome-tools.md`.

## Install

Needs Python 3.11 or newer and nothing from pip (standard library only). Run it from the
repository:

    tools/xrdome-tools --help

The launcher works from any directory. Symlink it onto your `PATH` if you want it everywhere.

Files it uses:

- Config: `~/.config/xrdome/tools.ini` (`$XDG_CONFIG_HOME/xrdome/tools.ini`). A `[programs]`
  section overrides where an external program is found, e.g. `ffmpeg = /opt/ff/bin/ffmpeg`.
- Data: `~/.local/share/xrdome-tools/` (`$XDG_DATA_HOME/xrdome-tools/`).
- Cache: `~/.cache/xrdome-tools/` (`$XDG_CACHE_HOME/xrdome-tools/`).

Exit codes: 0 ok; 1 output exists (without `--force`); 2 setup, usage or refusal; 3 verify
failed; 130 interrupted; a failing child program's own status otherwise.

## Tests

    python3 -m pytest tools/tests -q

CI runs this in the `tools` job, beside the existing meson job.

## doctor

Not yet available.

## transcode

Not yet available.

## models

Not yet available.

## setup

Not yet available.

## to3d

Not yet available.

## enhance

Not yet available.
