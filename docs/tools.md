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

    xrdome-tools transcode INPUT [--as FORMAT] [--codec hevc|av1] [--quality high|normal|small]
        [--max-width W] [--encoder NAME] [--reencode] [-o PATH | --outdir DIR] [--force] [--dry-run]

Writes a Matroska file (`.mkv`) that xrdome decodes in hardware and detects without flags: HEVC
(default) or AV1, tagged and named by the rules in "Output names and tags" below. Name, tags and
refusals are checked before any work starts. Needs `ffmpeg` and `ffprobe`.

- `--as auto|vr180|flat|hsbs|fsbs|htb|ftb` (and `-rl` variants): how to read the input. `auto`
  uses container metadata, then file name tokens, then frame shape.
- `--codec hevc|av1`: default `hevc`. If `--encoder` names an encoder and `--codec` is not given,
  the codec follows the encoder.
- `--quality`: `high` (default), `normal`, `small`. One setting per encoder family: NVENC
  `-rc vbr -cq`, VA-API `-rc_mode QVBR -global_quality` (falls back to `-rc_mode CQP -qp` when a
  test encode shows the driver rejects QVBR), x265 and SVT-AV1 `-crf`. The values are provisional
  until the hardware run (plan Task 8) settles them; see `QUALITY` in `encode.py`.
- `--max-width W`: scale down to at most W pixels wide, keeping the aspect ratio and the stereo
  layout. Side-by-side widths stay a multiple of 4; a half side-by-side picture keeps its height.
- `--encoder NAME`: use this ffmpeg encoder (`libx265`, `hevc_vaapi`, `hevc_nvenc`, ...). Refused
  with exit 2 if it is not built in or its test encode failed.
- `--reencode`: never remux.
- `-o PATH` or `--outdir DIR`: where to write; default is next to the input. `--force` replaces an
  existing output (never the input).
- `--dry-run`: prints the ffmpeg command, quoted, and writes nothing. Probes still run.

Encoder choice is the one `doctor` shows: NVENC, then VA-API, then software (slow), each only if
its test encode passed.

**Remux when friendly.** A source that is already the wanted codec (HEVC by default) and within
`--max-width` is copied (`-c copy`) with the tag and name added, and a line says so. Anything else is
re-encoded: H.264 wider than 4096 (which decodes in software on AMD) is never remuxed.

**What is kept.** Bit depth follows the source (10-bit stays 10-bit). Resolution and frame rate are
unchanged, timestamps pass through (variable frame rate stays variable). Audio and subtitles are
copied; audio Matroska cannot hold is re-encoded to Opus, `mov_text` subtitles become `srt`, and
other streams (cover art, data, unsupported subtitles) are dropped, each with a `path: stream=` line.
HDR (PQ or HLG) colour primaries, transfer and matrix are copied for software and NVENC; for VA-API
only if a test encode with them works, otherwise a line says HDR metadata is not passed through.
Mastering display metadata is not copied.

**Verify.** After writing, `ffprobe` checks the codec, that the size is not larger than the source,
the `Stereo 3D` side data against the format, the duration within 0.5 s, and that every audio and
subtitle stream that was meant to be kept is present. A failure exits 3, keeps the output and prints
the reasons.

**Log.** Every ffmpeg command, its stderr and the decisions go to `<output>.log`. The video is
written to `<output>.part` and renamed when ffmpeg succeeds; an interrupt or failure deletes it.

## models

    xrdome-tools models [--check] [--fetch [NAME ...]]

No model weights ship with xrdome. `models` lists every model with its status (`ok`,
`missing` or `bad hash`), licence and destination, and for each one that is not `ok` prints
the page to get it from, the file name, the sha256 and the exact path to put it at.
`--check` does the same and exits 2 if any model is not `ok`. Files in a model folder that
the registry does not know are named in a warning.

Nothing is downloaded unless you ask: `--fetch NAME ...` (or `--fetch` alone for every
fetchable model) downloads to `<file>.part`, checks the sha256, and only then renames it into
place; on a mismatch the download is deleted and the exit status is 2. No other subcommand
ever downloads a model, it stops and prints these instructions instead.

Only models under MIT, BSD-2-Clause, BSD-3-Clause or Apache-2.0 are accepted. Any other
licence is refused as "not an allowed model", even if the file is already present.

Paths are relative to the data directory (`~/.local/share/xrdome-tools/`):

| Model | Used by | Licence | Page | File | Destination |
|---|---|---|---|---|---|
| `Any_V2_S` | to3d | Apache-2.0 | https://huggingface.co/depth-anything/Depth-Anything-V2-Small/tree/03876f8651c73a60fe4c2c48294e09fcb6838fcf | `depth_anything_v2_vits.pth` | `iw3/nunif/iw3/pretrained_models/hub/checkpoints/` |
| `VDA_S` | to3d | Apache-2.0 | https://huggingface.co/depth-anything/Video-Depth-Anything-Small/tree/256875362cff76724b920335dfb4b29dd611f66e | `video_depth_anything_vits.pth` | `iw3/nunif/iw3/pretrained_models/hub/checkpoints/` |
| `iw3_row_flow_v3` | to3d | MIT | https://github.com/nagadomi/nunif | `iw3_row_flow_v3_20250627.pth` | `iw3/nunif/iw3/pretrained_models/` |
| `iw3_depth_aa` | to3d | MIT | https://github.com/nagadomi/nunif | `iw3_depth_aa_20250530.pth` | `iw3/nunif/iw3/pretrained_models/` |
| `transnetv2` | to3d | MIT | https://github.com/nagadomi/nunif | `transnetv2-pytorch-weights.pth` | `iw3/nunif/iw3/pretrained_models/` |

The last three are downloaded by iw3 itself on first use, so `--fetch` refuses them; `models`
only checks them. `xrdome-tools doctor` also lists each model's status.

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
