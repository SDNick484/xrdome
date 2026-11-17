# xrdome-tools: transcoding, 2D-to-3D and enhancement (design spec)

Date: 2026-11-17
Status: draft, for owner review
Builds on: `docs/superpowers/specs/2026-11-13-flat-projection-design.md` (format detection, 5.8)
and the owner's 2D-to-3D quality test in `~/src/sbs-test` on the desktop (iw3, `NOTES.md`,
`scripts/sbs-convert.sh`, `scripts/setup.sh`).

Lines marked **(owner)** were decided by the owner in the design conversation. Lines marked
**(call)** are the author's proposals; the owner can change them at review.

## 1. Intent

xrdome plays files; it does not prepare them. The owner wants offline tools, shipped with
xrdome, that turn a video into one xrdome plays well:

1. **Transcode** into a format the player decodes in hardware and detects correctly.
2. **Convert 2D to side-by-side 3D**, building on the iw3 test in sbs-test.
3. **Enhance**, optionally: frame interpolation to 60 fps (RIFE) and better upscaling,
   conventional or model-based.

They are for the owner and, once xrdome is public, for anyone running it on Linux.
**(owner)** So the tools cannot assume the owner's two machines. Each run works out what
hardware it has, picks a path, and says which path it picked and why. **(owner)**

The owner's measured findings set the targets: source bitrate matters more than anything
else for picture quality on the glasses; H.264 wider than 4096 decodes in software on the
laptop and misses vsyncs on battery; HEVC and AV1 decode in hardware up to 8K.

## 2. Goals

- One command, `xrdome-tools`, with subcommands `doctor`, `transcode`, `to3d`, `enhance`,
  `setup` and `models`. **(owner: approach B)**
- Works on NVIDIA and AMD and picks the right tools by itself. **(owner)** Intel comes free
  through VA-API. **(call)**
- `doctor` prints what was found and which path each subcommand would take, in a form a
  user can paste into a bug report.
- Every output carries tags and a name that xrdome's detection (flat projection spec 5.8)
  reads correctly, so the file opens right with no flags and no `P`/`M` press. **(call)**
- No model weights in the repository or in any release. The docs link to each model's
  upstream source and say where to place the file. **(owner)**
- Only models whose code and weights are under permissive licences (MIT, BSD, Apache-2.0).
  Non-commercial licences (for example Depth Anything V2 Base/Large, CC-BY-NC-4.0) are
  refused even if a user places the file by hand. **(owner: "same limits as earlier")**
- Enhancement for 2D sources first. **(owner)**

## 3. Non-goals

- Enhancing VR180 or other video that is already stereo. Later, as its own design.
  **(owner: "start with 2D, explore VR later")**
- Real-time 2D-to-3D, interpolation or upscaling inside the player.
- Native Windows. WSL2 is supported as a place to run conversions, since that is how the
  owner's desktop runs them. **(call)**
- A GUI.
- Other RIFE pipelines (VapourSynth, PyTorch RIFE, TensorRT). Possible later behind the same
  commands. **(owner)**
- Shipping or bundling iw3, RIFE, Real-ESRGAN or any binary built from them.
- Resuming an interrupted job. A failed or killed job starts again. **(call)**

## 4. Anti-goals

These make the tools a failure even if they technically work:

- A 2D-to-3D job that silently runs on the CPU for hours. Without CUDA or ROCm, `to3d`
  refuses unless `--cpu` is given. **(call, agreed in the conversation)**
- An output that xrdome opens the wrong way.
- Overwriting or modifying the input file. Outputs never replace an existing file unless
  `--force`.
- A result that is worse than the source without saying so: a lower-quality encode than the
  user asked for, a dropped audio or subtitle stream, a changed frame rate. Every such change
  is printed.
- Fetching or using a model the licence rules exclude.

## 5. Approaches (decided)

| | Approach | Trade-off |
|---|---|---|
| A | Bash scripts, extending sbs-test | Familiar and readable; the hardware decision logic (NVENC / VA-API / software, CUDA / ROCm / none, Vulkan, WSL2) is fragile and hard to test in shell, and public users will hit its edge cases. |
| **B** | **One Python command, standard library only, that runs ffmpeg, ffprobe, iw3 and the ncnn binaries** | Detection and fallbacks are testable with pytest against recorded tool output; `doctor` gives useful bug reports; iw3 is Python already. Cost: the ncnn tools read and write image folders, so enhancement goes through PNG frames on disk. |
| C | VapourSynth pipeline | Fastest, no image folders; a heavy dependency that is awkward on many distros and patchy on AMD. Too much to ask for an optional feature. |

The owner picked B. C, or another RIFE pipeline, can come back later as a faster engine
behind `enhance`. **(owner)**

## 6. Design

### 6.1 Layout, language, licence

- Python 3.11 or newer, standard library only. No pip install is needed to run `doctor`,
  `transcode` or `enhance`. **(call)** Ubuntu 24.04 has 3.12; Gentoo has 3.13.
- Source in `tools/xrdome_tools/` (a package), with `tools/xrdome-tools` as the launcher
  script. **(call)** It lives beside the existing `tools/` helpers rather than in `src/`,
  which is the C player.
- GPL-2.0-or-later, the same as the rest of xrdome.
- External programs, all looked up on `PATH` and overridable in
  `~/.config/xrdome/tools.ini` (`[programs]` section): `ffmpeg`, `ffprobe`, `vainfo`
  (optional), `nvidia-smi` (optional), `vulkaninfo` (optional), `rife-ncnn-vulkan`,
  `realesrgan-ncnn-vulkan`. **(call)**
- Data in `$XDG_DATA_HOME/xrdome-tools/` (default `~/.local/share/xrdome-tools/`): the iw3
  environment (6.5) and models (6.7). Temporary frames in `$XDG_CACHE_HOME/xrdome-tools/`
  or `--tmp DIR`. **(call)**

### 6.2 Common behaviour

- Usage: `xrdome-tools SUBCOMMAND INPUT [options]`. One input file per run; batch use is a
  shell loop. **(call)**
- Output goes next to the input unless `-o PATH` (a file) or `--outdir DIR` is given.
- `--dry-run` prints every command that would run, quoted so it can be pasted, and runs
  nothing.
- Each run starts with one `path:` line per decision, for example:
  `path: encode=hevc_vaapi (AMD, VA-API HEVC Main10 encode on /dev/dri/renderD128)`.
- Every ffmpeg command is logged in full to `<output>.log` next to the output, with the
  tool's own decisions and the wall time. **(call)** sbs-test's per-run logs showed their
  worth.
- Children are run without a shell; file names are passed as single arguments.
- Ctrl-C stops the child, removes partial output and temporary frames, and exits 130.

### 6.3 `doctor`: hardware detection

Runs every probe and prints a table, then the path each subcommand would take.
`doctor --json` prints the same as JSON for bug reports. Other subcommands run the same
probes for what they need, and cache nothing between runs. **(call)**

| Probe | How | Result |
|---|---|---|
| Platform | `/proc/version` contains `microsoft`, `/dev/dxg` exists | `wsl2` or `linux` |
| GPUs | `/sys/class/drm/card*/device/vendor` (0x10de NVIDIA, 0x1002 AMD, 0x8086 Intel); on WSL2, `nvidia-smi` (also tried at `/usr/lib/wsl/lib/nvidia-smi`, which the owner's desktop needed) | vendor list |
| ffmpeg encoders | `ffmpeg -hide_banner -encoders` | which of `hevc_nvenc`, `av1_nvenc`, `hevc_vaapi`, `av1_vaapi`, `libx265`, `libsvtav1` are built in |
| Encoders that really work | a 1-second, 256x256 lavfi test encode with each built-in hardware encoder | `ok` or the error line. Being listed by ffmpeg is not enough: the owner's FFmpeg lists NVENC on an AMD laptop. |
| VA-API | render nodes `/dev/dri/renderD*`; `vainfo` entrypoints if installed | device, HEVC/AV1 encode and decode profiles |
| Vulkan | `vulkaninfo --summary` if installed, otherwise the ncnn tool's own device list | devices, for `enhance` |
| Compute for `to3d` | inside the iw3 environment if it exists: `torch.cuda.is_available()`, `torch.version.cuda`, `torch.version.hip` | `cuda`, `rocm`, `none`, or `not set up` |
| Models and binaries | 6.7 | present, missing, or bad hash |

Encoder choice, unless `--encoder` overrides it **(call)**:

1. NVENC, if the test encode passed.
2. VA-API, if the test encode passed.
3. Software (`libx265` for HEVC, `libsvtav1` for AV1), with a line saying it will be slow.

### 6.4 `transcode`

Re-encodes to a format xrdome decodes in hardware and detects without flags.

**What "friendly" means** **(call)**:

- Video HEVC (default) or AV1 (`--codec av1`). HEVC is the default because it decodes in
  hardware on the laptop, the desktop and, as far as is known, the Steam Deck; AV1 does not
  on the Deck.
- 10-bit output if the source is 10-bit, otherwise 8-bit. HDR metadata is passed through
  when the encoder supports it and a line says so when it cannot. **(call)**
- Resolution unchanged by default. `--max-width W` scales down, keeping the aspect ratio and
  the stereo layout (each eye scaled the same).
- Frame rate unchanged. Variable frame rate sources stay variable. **(call)**
- Matroska container. Audio and subtitle streams are copied; a stream Matroska cannot hold
  is re-encoded (audio to Opus) or dropped, with a line saying which. **(call)**
- Quality: `--quality high|normal|small`, default `high`, because bitrate dominates what the
  glasses show. Each maps to one setting per encoder family (NVENC `-rc vbr -cq`, VA-API
  `-rc_mode QVBR` or `CQP` where QVBR is missing, x265 and SVT-AV1 `-crf`). The exact values
  are chosen by a test in the first build task (section 9) and recorded in the code with
  their source. **(call)**

**Input format.** `--as auto|vr180|flat|hsbs|fsbs|htb|ftb` (with `-rl` variants for
right-eye-first). `auto` uses the same rules as xrdome's detection: container metadata,
then the file name token table, then frame shape (flat projection spec 5.8). The Python
copy of those rules and the C code read their test cases from one shared file
(`tests/data/detect_cases.tsv`, moved out of `tests/test_detect.c`), so the two cannot drift
apart silently. **(call)**

**Tagging the output** **(call)**:

| Format | Matroska tag | Name token appended |
|---|---|---|
| VR180 side-by-side | `stereo_mode=left_right` | `_VR180_LR` |
| Full SBS | `stereo_mode=left_right` | `_FSBS` |
| Half SBS | `stereo_mode=left_right` | `_HSBS` |
| Full / half top-bottom | `stereo_mode=top_bottom` | `_FTB` / `_HTB`, see below |
| Flat 2D | `stereo_mode=mono` | none |
| Right-eye-first variants | `right_left` / `bottom_top` | `_RL` added |

A token already in the name is not added again. Two gaps, both to settle in the first build
task:
- `FTB` is not in xrdome's token table today; `TB` is. Either add `FTB` to the player's table
  or use `_TB`. **(call: use `_TB` for full top-bottom; no player change)**
- Writing a Spherical Mapping tag into Matroska with ffmpeg is not verified. VR180 output
  relies on the name token and the 2:1 frame shape, which already open as VR180 today.

**Skip when already friendly.** If the video is already HEVC or AV1, within `--max-width`,
and only the tags or name are missing, the file is remuxed (`-c copy`) with the tags, not
re-encoded, and a line says so. `--reencode` forces a full encode. **(call)**

**Verify.** After writing, `ffprobe` checks: the codec, the size, the `Stereo 3D` side data
matching the format, the duration within 0.5 s of the source, and every stream that was
supposed to be copied is present. A failed check makes the run exit non-zero and keeps the
output, with the reason. **(call)**

### 6.5 `to3d`: 2D to side-by-side 3D

Wraps iw3 the way `sbs-test/scripts/sbs-convert.sh` does, carrying over its pins, model
allow-list and hash checks.

**Setup.** `xrdome-tools setup to3d` builds the iw3 environment in
`$XDG_DATA_HOME/xrdome-tools/iw3/`: nunif cloned at a pinned commit, a venv, PyTorch, and
the two hub repos pinned (the sbs-test `setup.sh` steps). The PyTorch build follows
detection **(call)**:

- NVIDIA (native or WSL2): the CUDA wheels nunif's `requirements-torch.txt` names.
- AMD: ROCm wheels for the detected GPU. If ROCm PyTorch cannot see the GPU (the laptop's
  Radeon 860M, gfx1152, has official ROCm support only in the 7.13 technology preview, and
  PyTorch wheels for it are not verified), setup says so and stops. No
  `HSA_OVERRIDE_GFX_VERSION` tricks by default.
- Neither: setup offers the CPU wheels only with `--cpu`.

Setup prints the size of the download before starting (about 4 to 5 GB for CUDA in
sbs-test) and asks for `--yes` in non-interactive use. **(call)**

**Conversion.** `xrdome-tools to3d INPUT [--model Any_V2_S|VDA_S] [--divergence D]
[--convergence C] [--foreground-scale F] [--half] [--cpu]`. Defaults from the owner's test:
`Any_V2_S`, divergence 2.0, convergence 0.5, foreground scale 0. **(call: the owner picked
1.0 and 2.0 to compare and has not settled one)**

- Output: full SBS, 1920x1080 per eye (3840x1080 total), letterboxed or pillarboxed when the
  source is not 16:9, never stretched. `--half` gives half SBS at 1920x1080. **(call)**
- Frame rate kept, including 60 and above (iw3's `--max-fps` raised to the source rate; the
  sbs-test default of 60 halved a 120 fps source).
- iw3 writes its output with its own encoder, then the file goes through `transcode` (6.4)
  for the final encode and the tags. On NVIDIA iw3 encodes with `hevc_nvenc` directly and
  the file is only remuxed with tags; elsewhere iw3 writes a high-quality intermediate
  (`libx264` at a low CRF, which PyAV's bundled ffmpeg has) that `transcode` then encodes
  with VA-API or software. **(call)** iw3's PyAV build has no VA-API.
- Output name: `<name>_<model>_div<D>[_c<C>][_fs<F>]_FSBS.mkv`, as in sbs-test.
- Without CUDA or ROCm: refuse, with the measured rate for comparison (about 60 fps on the
  owner's RTX 5070 Ti), unless `--cpu`.

### 6.6 `enhance`: frame interpolation and upscaling (2D only)

Two operations, run separately or together in one pass: `xrdome-tools enhance INPUT
[--fps 60] [--upscale 2 --method lanczos|model]`. **(owner: RIFE to 60 fps; upscaling
conventional or model-based)**

- **2D only.** Input detected as stereo or VR180 (6.4 rules) is refused; `--treat-as-2d`
  overrides for a file that is wrongly detected. **(owner: 2D first)**
- **Order with `to3d`.** Enhance first, then convert to 3D. That halves the work and keeps
  both eyes made from the same frames. The docs say so, and `to3d` prints a hint when its
  input is below 1080p or below 50 fps. **(call)**

**Interpolation.** `rife-ncnn-vulkan` (MIT), which runs on AMD, Intel and NVIDIA through
Vulkan. **(call)** The input is decoded to PNG frames in chunks (default 10 s, with one frame
of overlap so no pair is missed at a chunk boundary), interpolated to the target frame
count, and encoded by the 6.4 pipeline with the source audio. Target rates that are not a
whole multiple (24 to 60) use the RIFE v4 models' arbitrary time steps. A variable frame
rate source (phone video, such as the sbs-test clips at 59.86 fps measured) is first made
constant at its nominal rate, with a line saying so. **(call)** Interpolation
across a scene cut produces a ghosted frame; v1 does not detect cuts and the docs say so.
**(call)** Disk use per chunk is printed before starting.

**Upscaling.**

- `--method model`: `realesrgan-ncnn-vulkan` (BSD-3-Clause) with an allowed model, through
  the same chunked frame folders. **(call)**
- `--method lanczos`: ffmpeg's `libplacebo` filter on Vulkan if available, otherwise
  `scale=...:flags=lanczos` on the CPU. Note for the docs: this adds no detail, since mpv
  already scales at playback (`--mpv-scale`). Its only use is to save GPU work at playback,
  for example on the Steam Deck. **(call)**

### 6.7 Models and binaries

Nothing is shipped. **(owner)** A registry in the code lists each allowed model:

| Field | Example |
|---|---|
| name | `Any_V2_S` |
| used by | `to3d` |
| licence (code and weights) | Apache-2.0 |
| upstream page and download URL, pinned to a revision | Hugging Face `depth-anything/Depth-Anything-V2-Small` at `03876f8...` |
| file name and sha256 | `depth_anything_v2_vits.pth`, `715fade1...` |
| where it goes | `$XDG_DATA_HOME/xrdome-tools/iw3/.../checkpoints/` |

`xrdome-tools models` lists every entry as `ok`, `missing` or `bad hash`, and for each
missing one prints the upstream link, the exact path to put it at, and the command to check
it. A file that is present but not in the registry is ignored and named in a warning.

Initial registry: Depth Anything V2 Small and Video Depth Anything Small (Apache-2.0; hashes
from sbs-test `NOTES.md`); RIFE v4 models as packaged for rife-ncnn-vulkan (MIT, per
Practical-RIFE); Real-ESRGAN `realesr-animevideov3` and any other ncnn-format model whose
licence is confirmed (BSD-3-Clause; the project gives no separate statement for weights, so
the registry records "BSD-3-Clause, per project licence"). Exact files and hashes are filled
in by the first build task. **(call)**

iw3 also downloads three support models on first use (warp, depth anti-aliasing,
TransNetV2; hashes in sbs-test `NOTES.md`). The registry lists them and `models` checks them,
though iw3 fetches them itself. **(call)**

The ncnn programs are found on `PATH` (or `tools.ini`). Missing ones are reported with the
upstream release page.

### 6.8 Errors

| Condition | Behaviour |
|---|---|
| Output exists | exit 1 naming it, unless `--force` |
| ffmpeg or ffprobe missing | exit 2 with the package name for Gentoo, Debian/Ubuntu and Arch |
| Requested `--encoder` fails its test encode | exit 2 with the encoder's error line |
| No working hardware encoder | continue in software, one line saying so |
| `to3d` with no CUDA or ROCm | exit 2, unless `--cpu` |
| `to3d` before `setup to3d` | exit 2 naming the command |
| Model missing or bad hash | exit 2 with the `models` instructions for that model |
| Model not in the registry requested by name | exit 2: "not an allowed model" with its licence if known |
| `enhance` on stereo input | exit 2, unless `--treat-as-2d` |
| Disk space below the estimate for temporary frames | exit 2 with the estimate and `--tmp` |
| Child fails | exit with its status; its last 20 lines of stderr are printed; the log keeps all of it |
| Verify fails (6.4) | exit 3; output kept, reason printed |

## 7. Testing

- **Unit (pytest, no hardware, no video):** detection from recorded `nvidia-smi`, `vainfo`,
  `ffmpeg -encoders`, `vulkaninfo` and `/proc/version` output captured on the laptop (Gentoo,
  AMD) and the desktop (WSL2, NVIDIA), plus a machine with neither; encoder choice; the
  Python copy of the detection rules against the same cases as `tests/test_detect.c`;
  output naming and token rules; command building for each encoder family; the model
  registry and licence filter.
- **Integration (CI, Ubuntu, software encoders):** `transcode` on short lavfi clips of each
  format (`tools/make_pattern.sh` kinds), then `ffprobe` checks the tags, codec and streams;
  remux-only path; `--dry-run` output. A new CI job; the C job is unchanged. **(call)**
- **Hardware, manual, per machine:** `doctor` output on the laptop and the desktop;
  `transcode` of `test2.mp4` (4320x2160 H.264) to HEVC with VA-API on the laptop and NVENC on
  the desktop, then `xrdome --require-hwdec` plays it with `hwdec: vaapi` and 0 of 3600
  missed vsyncs on AC; `to3d` of sbs-test's `pan.mp4` on the desktop matches the sbs-test
  output's size and frame count; `enhance --fps 60` of a 30 fps clip on both machines; every
  output opens in xrdome with a `detect:` line naming `metadata` as the source.

## 8. Documentation

A `docs/tools.md` page: install per distro, `doctor`, each subcommand with examples, the
model table with links and paths, the order "enhance, then to3d", and what is measured
versus expected. The README links to it.

## 9. First build task, before the code that depends on it

Capture and record (as test fixtures and in the code with their source):

1. Tool outputs for detection on both machines (section 7).
2. The quality settings per encoder family: encode a 60 s clip at a few settings, compare
   size and a quick look on the glasses, and pick the three `--quality` values.
3. Whether `rc_mode QVBR` works with `hevc_vaapi` on the Radeon 860M.
4. The rife-ncnn-vulkan and realesrgan-ncnn-vulkan model files to list, with hashes.
5. Whether ROCm PyTorch sees the laptop's GPU (`setup to3d` on the laptop). If not, the AMD
   `to3d` path is documented as "not working on this GPU" and that is accepted.

## 10. Not verified

- ROCm PyTorch on gfx1152.
- iw3 conversion speed on AMD; rife-ncnn-vulkan and realesrgan-ncnn-vulkan speed on either
  machine.
- Writing Spherical Mapping into Matroska with ffmpeg.
- Whether NVENC test encodes behave the same under WSL2 as natively.
- How visible per-frame RIFE ghosting at scene cuts is on real films.

## 11. Left to the builder

- Module boundaries inside `tools/xrdome_tools/`, argument parsing details, and exact
  message wording beyond the examples here.
- Chunk size tuning and parallelism between decode, model and encode.
- The CI job's exact steps.
