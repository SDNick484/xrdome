# CLAUDE.md

xrdome is a Linux player for VR180 and flat video on XR glasses (C, meson). `tools/` also holds
`xrdome-tools`, a standard-library Python package that prepares video for it.

## Gate

Run both before every commit:

    meson setup build -Dwerror=true && meson compile -C build && meson test -C build --print-errorlogs
    python3 -m pytest tools/tests -q

## xrdome-tools rules

- Python >= 3.11, standard library only under `tools/xrdome_tools/`; pytest is the only test dependency.
- Children run through `run.Runner` with an argv list, never `shell=True`.
- Update `docs/tools.md` with any user-visible change, in the same PR.
- Spec and plan: `docs/superpowers/specs/2026-11-17-xrdome-tools-design.md`,
  `docs/superpowers/plans/2026-11-17-xrdome-tools.md`.

## Append points (parallel work: one entry per line, touch only your own)

- `cli.SUBCOMMANDS` in `tools/xrdome_tools/cli.py`: one module name per line.
- `docs/tools.md`: one `## <subcommand>` section each.
- The status list below: one line per subcommand.

## xrdome-tools status

- doctor: available (`xrdome-tools doctor [--json]`)
- transcode: available (`xrdome-tools transcode INPUT`)
- models: available (`xrdome-tools models [--check] [--fetch]`)
- setup: available (`xrdome-tools setup to3d [--cpu] [--yes] [--fetch-models]`, `xrdome-tools setup depth [--cpu] [--yes]`)
- to3d: available (`xrdome-tools to3d INPUT`)
- enhance: available (`xrdome-tools enhance INPUT [--fps N] [--upscale 2|3|4]`)
- depth: available (`xrdome-tools depth photo FILE|DIR [--force]`, `xrdome-tools depth serve [--socket PATH] [--idle-exit S]`, `xrdome-tools depth bench-video [--seconds S] [--rate HZ]`, `xrdome-tools depth flicker DIR [--start N] [--count N]`; needs `setup depth`)
