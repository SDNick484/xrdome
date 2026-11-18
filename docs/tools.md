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

    xrdome-tools doctor [--json]

Probes the machine and prints what it found, then the path each subcommand would take. Paste
the output (or `--json`) into bug reports. Nothing is cached between runs.

Probes: platform (`linux` or `wsl2`), GPU vendors (`/sys/class/drm`, and `nvidia-smi`, also
tried at `/usr/lib/wsl/lib/nvidia-smi`, on WSL2), the ffmpeg encoders built in, a 1-second
256x256 test encode with each built-in hardware encoder (`hevc_nvenc`, `av1_nvenc`,
`hevc_vaapi`, `av1_vaapi`), VA-API profiles from `vainfo`, Vulkan devices from
`vulkaninfo --summary`, and compute (`cuda`, `rocm`, `none` or `not set up`) from the iw3
environment. An encoder that ffmpeg lists but whose test encode fails is reported with its
error line and is not chosen.

Encoder choice: NVENC if its test passed, else VA-API if its test passed, else software
(`libx265` for HEVC, `libsvtav1` for AV1), which is slow. Missing programs are not errors;
the matching rows say so. Programs can be overridden in `tools.ini` (`[programs]`).

The last lines are decision lines, `path: <key>=<value> (<reason>)`, one each for
`transcode`, `to3d` and `enhance`. `--json` prints the keys `platform`, `gpus`, `encoders`,
`vaapi`, `vulkan`, `compute` and `paths` (one entry per subcommand). Exit status 0.

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

## Output names and tags

Every subcommand that writes a video (`transcode`, `to3d`, `enhance`) names and tags it so it opens
in xrdome with no flags. The rules live in `tools/xrdome_tools/formats.py`.

**Detecting the input.** With `--as auto` the format is read the way xrdome reads it: container
metadata (`Stereo 3D` and `Spherical Mapping` side data), then tokens in the file name, then the
frame shape (2:1 is VR180 side by side; 3:1 or wider is flat side by side; and so on). The video stream
used is the first one that is not cover art. The Python copy of the name-token and frame-shape
rules is checked against the same cases as the player's C code: `tests/data/detect_names.tsv`
and `tests/data/detect_shapes.tsv`. Add a case there and both test suites pick it up.

**Names.** The output is Matroska (`.mkv`), named from the input's stem plus tokens, unless a
token is already in the name:

| Format | Tokens added | Matroska `stereo_mode` |
|---|---|---|
| VR180 side by side | `_VR180_LR` | `left_right` |
| Full / half side by side | `_FSBS` / `_HSBS` | `left_right` |
| Full / half top-bottom | `_TB` / `_HTB` | `top_bottom` |
| Flat 2D | none | `mono` |
| Right-eye-first | `_RL` | `right_left` / `bottom_top` |

For right-eye-first side by side the `RL` token goes before the others (`clip_RL_FSBS.mkv`,
`clip_VR180_RL.mkv`), because xrdome reads the first layout token it finds. Top-bottom has no
right-first name token, so `clip_TB_RL.mkv` is a label only; the `stereo_mode` tag carries the
order and xrdome reads the tag before the name.

**Refusals.** An output path is checked before any work starts. It is never the input file (also
through a symlink or hard link, even with `--force`): exit 2. Its directory must exist and be
writable: exit 2. An existing output without `--force`: exit 1.
