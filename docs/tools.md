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
| `rife-v4.6` | enhance | MIT | https://github.com/nihui/rife-ncnn-vulkan/releases/tag/20221029 | `flownet.bin` | `models/rife-v4.6/` |
| `rife-v4.6.param` | enhance | MIT | https://github.com/nihui/rife-ncnn-vulkan/releases/tag/20221029 | `flownet.param` | `models/rife-v4.6/` |
| `realesr-animevideov3-x2` | enhance | BSD-3-Clause | https://github.com/xinntao/Real-ESRGAN/releases/tag/v0.2.5.0 (`realesrgan-ncnn-vulkan-20220424-ubuntu.zip`, `models/`) | `realesr-animevideov3-x2.bin` | `models/realesrgan/` |
| `realesr-animevideov3-x2.param` | enhance | BSD-3-Clause | as above | `realesr-animevideov3-x2.param` | `models/realesrgan/` |

The last three are downloaded by iw3 itself on first use, so `--fetch` refuses them; `models`
only checks them. `xrdome-tools doctor` also lists each model's status.

## setup

    xrdome-tools setup to3d [--cpu] [--yes] [--fetch-models] [--dry-run]

Builds the environment `to3d` runs in, under `~/.local/share/xrdome-tools/iw3/`
(`$XDG_DATA_HOME/xrdome-tools/iw3/`). Needs `git` and `python3` with `venv`; both can be
overridden in the `[programs]` section of `tools.ini`. The steps, each printed by `--dry-run`:

- [nunif](https://github.com/nagadomi/nunif) (which contains iw3) at commit
  `0f8b86b952d3f3e294c8d887fbf615a679e7f4ca`, in `iw3/nunif/`.
- The two torch hub repos iw3 loads, `nagadomi/Depth-Anything_iw3` at
  `92eaac92bcc2c275f48c1f866ec708861055e435` and `nagadomi/Video-Depth-Anything_iw3` at
  `e9541668c380524f128a3a75886c5f9143214554`, in `iw3/nunif/iw3/pretrained_models/hub/`.
- A venv in `iw3/venv/` with PyTorch and nunif's `requirements.txt`. The PyTorch build follows
  the GPU found (a `path: torch=` line says which and why):
  - NVIDIA, native or under WSL2: the CUDA wheels nunif's `requirements-torch.txt` names.
  - AMD: ROCm wheels from `https://download.pytorch.org/whl/rocm6.4`. If PyTorch then cannot
    see the GPU (ROCm does not support every Radeon yet; the Radeon 860M, gfx1152, is not
    verified), setup says so and exits 2. It never sets `HSA_OVERRIDE_GFX_VERSION` for you.
  - Neither: refused unless `--cpu`, which installs the CPU wheels. With a GPU present,
    `--cpu` changes nothing.

The fetches are pinned by commit, so running `setup to3d` again is harmless and picks up new
pins after an xrdome update. Before downloading it prints the size (about 4 to 5 GB for CUDA,
measured; ROCm and CPU not measured) and asks; without a terminal it exits 2 unless `--yes`.
The log goes to `iw3/setup.log`. Setup writes `iw3/xrdome-setup.ini` last, once every step
has passed; `to3d` refuses to run without it.

`--fetch-models` also downloads the `Any_V2_S` and `VDA_S` depth models, as
`xrdome-tools models --fetch` does (sha256 checked). Without it, place them by hand as
`xrdome-tools models` explains. iw3 downloads its three small support models itself on first
use.

## to3d

    xrdome-tools to3d INPUT [--model Any_V2_S|VDA_S] [--divergence D] [--convergence C]
        [--foreground-scale F] [--half] [--cpu] [--quality high|normal|small] [--tmp DIR]
        [-o PATH | --outdir DIR] [--force] [--dry-run]

Converts 2D video to side-by-side 3D with iw3, then finishes it through `transcode`, so the
result is HEVC Matroska tagged and named for xrdome. Needs `setup to3d` first, and the chosen
depth model in place (see `models`).

- `--model`: `Any_V2_S` (default, Depth Anything V2 Small) or `VDA_S` (Video Depth Anything
  Small, run with `--ema-normalize --scene-detect`). Any other name, including the
  non-commercial Base and Large models, is refused as "not an allowed model".
- `--divergence` (default 2.0), `--convergence` (default 0.5), `--foreground-scale` (default
  0, not passed to iw3): iw3's own options, defaults from the owner's sbs-test.
- `--half`: half SBS instead of full SBS.
- `--cpu`: run without CUDA or ROCm. Without it, `to3d` refuses (exit 2): on the CPU a few
  minutes of video takes hours, against about 60 fps on an RTX 5070 Ti.
- `--quality`: the final encode, as for `transcode`.
- `--tmp DIR`: where the intermediate file goes (default `~/.cache/xrdome-tools/`). It is
  deleted when the run ends, also on failure or Ctrl-C.
- `-o`, `--outdir`, `--force`, `--dry-run`: as for `transcode`. `--dry-run` prints the iw3
  command and the transcode that would follow.

The frame rate is kept: iw3's `--max-fps` is set to the source rate rounded up (120 for
119.88). On NVIDIA, iw3 encodes HEVC with NVENC itself and `transcode` only remuxes and tags
it; elsewhere iw3 writes a libx264 CRF 12 intermediate that `transcode` encodes with VA-API or
in software. The output name is `<name>_<AnyV2S|VDAS>_div<D>[_c<C>][_fs<F>]_FSBS.mkv`
(`_HSBS` with `--half`), with `_c` and `_fs` only when not the default, for example
`pan_AnyV2S_div2.0_FSBS.mkv` or `pan_VDAS_div3.0_c0.65_fs1.0_FSBS.mkv`. iw3's command and the
decisions go to `<output>.log`, with the transcode's.

Enhance first, then convert: below 1080p or below 50 fps, `to3d` prints a hint to run
`enhance` first. Interpolating or upscaling before the conversion is half the work and keeps
both eyes made from the same frames.

Not yet verified on hardware: ROCm, conversion speed on AMD, and how iw3 fits a source that is
not 16:9 into the 1920x1080 eye (letterbox expected, not checked).

## enhance

    xrdome-tools enhance INPUT [--fps N] [--upscale 2|3|4] [--method lanczos|model]
        [--chunk SECONDS] [--tmp DIR] [--treat-as-2d] [--codec hevc|av1]
        [--quality high|normal|small] [--encoder NAME] [-o PATH | --outdir DIR] [--force] [--dry-run]

Raises the frame rate with RIFE, upscales, or both in one pass, for 2D video. The result goes
through `transcode` for the final encode, tags and checks, so it opens in xrdome like any
`transcode` output. The name gets `_<N>fps` and/or `_x<S>` (`clip_60fps_x2.mkv`; 59.94 is
written `59p94fps`).

**Enhance first, then `to3d`.** Converting the enhanced 2D file halves the work and makes both
eyes from the same frames.

- `--fps N`: interpolate to N frames per second (`60`, or `60000/1001`). It must be above the
  source's rate. Uses `rife-ncnn-vulkan` with the RIFE v4.6 model, which runs through Vulkan on
  AMD, Intel and NVIDIA and handles rates that are not a whole multiple (24 to 60). A variable
  frame rate source (phone video) is first made constant at its nominal rate, with a
  `path: fps=` line. Scene cuts are not detected: the frame interpolated across a cut is
  ghosted.
- `--upscale 2|3|4` with `--method`:
  - `lanczos` (default): ffmpeg's `libplacebo` filter (`ewa_lanczos` on Vulkan) when it is built
    in and a short test run works, otherwise `scale=...:flags=lanczos` on the CPU; a
    `path: upscale=` line says which and why. This adds no detail: mpv already scales at
    playback. Its only use is to save GPU work at playback, for example on the Steam Deck.
  - `model`: `realesrgan-ncnn-vulkan` with `realesr-animevideov3`, x2 only (the one model in
    the registry). `--upscale 3` or `4` with `--method model` is refused.
- `--fps` and `--upscale` together run interpolation, then upscaling, on each chunk.
- `--treat-as-2d`: input detected as stereo or VR180 (same rules as `transcode`) is refused
  with exit 2; this enhances it anyway, for a file that is wrongly detected.
- `--chunk SECONDS` (default 10) and `--tmp DIR` (default `~/.cache/xrdome-tools/`): see below.
- `--codec`, `--quality`, `--encoder`, `-o`, `--outdir`, `--force`, `--dry-run`: as for
  `transcode`. A broken `--encoder` is refused before any work starts.

**Programs and models.** `rife-ncnn-vulkan` and `realesrgan-ncnn-vulkan` are not shipped. Get a
release from https://github.com/nihui/rife-ncnn-vulkan/releases (tested against `20221029`) and
https://github.com/xinntao/Real-ESRGAN-ncnn-vulkan/releases (`v0.2.0`), and put the program on
`PATH` or name it in `tools.ini` (`[programs]`). Only the program is needed from those archives;
the models come from the registry (see `## models`): `rife-v4.6` and `rife-v4.6.param` can be
fetched with `xrdome-tools models --fetch rife-v4.6 rife-v4.6.param`; the two
`realesr-animevideov3-x2` files only ship inside an archive and are placed by hand. A missing
program or model stops the run with exit 2 before any work, naming where to get it.

**How it runs.** The ncnn programs read and write folders of images, so the video goes through
them a chunk at a time (default 10 s, one frame of overlap so no frame pair is missed at a
chunk boundary): frames are decoded to PNG in a job folder under `--tmp`, the tool(s) run, the
result is encoded to a high-quality intermediate segment (`libx264 -crf 12`), and the PNG
folders are deleted. The segments are joined with the source's audio and subtitles, and that
file goes through `transcode` for the one final encode. Lanczos upscaling alone needs no frame
folders: one ffmpeg run writes the intermediate.

**Disk.** Before starting, the PNG space for one chunk is estimated (3 bytes per pixel, input
plus output frames) and printed in a `path: tmp=` line. Less free space than that in `--tmp`
stops the run with exit 2, naming the estimate; use `--tmp` on a larger disk or a shorter
`--chunk`. The intermediate segments also go to `--tmp`. A failure, Ctrl-C or a full disk
removes every temporary frame and segment; nothing is resumed.

**Colour.** Frames go through 8-bit RGB PNG, so a 10-bit source comes out 8-bit (with a
`path: bit-depth=8` line). The RGB conversion uses the source's matrix (BT.709 for untagged HD,
BT.601 below 720 lines) and the intermediate is tagged with it. HDR sources are not verified.

**Unverified on hardware:** speed of both ncnn programs on AMD and NVIDIA, and the real
programs' output against these command lines (the tests use stand-ins).

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
