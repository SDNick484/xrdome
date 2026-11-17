# xrdome-tools Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. In this repository the tasks are filed as `Prompt:` issues and run by the issue-queue skill; each task is one PR against `main`.

**Goal:** A `xrdome-tools` command with `doctor`, `transcode`, `to3d`, `enhance`, `setup` and `models` subcommands that turns any video into one xrdome plays in hardware and detects without flags, on NVIDIA, AMD or neither.

**Architecture:** A standard-library Python package in `tools/xrdome_tools/` that decides and orchestrates, and never touches pixels itself: ffmpeg/ffprobe for decode, encode and tags; iw3 in its own managed venv for 2D-to-3D; `rife-ncnn-vulkan` and `realesrgan-ncnn-vulkan` through chunked PNG folders for enhancement. Hardware detection is pure parsing of recorded tool output, so it is unit-tested without hardware.

**Tech Stack:** Python 3.11+ (stdlib only at run time), pytest (tests only), ffmpeg/ffprobe, iw3 (nagadomi/nunif), ncnn Vulkan binaries, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-11-17-xrdome-tools-design.md` (approved 2026-11-17). Section numbers below (6.3, 6.4, ...) refer to it.

## Global Constraints

- Python `>=3.11`; no third-party imports under `tools/xrdome_tools/`. pytest is the only test dependency.
- Licence GPL-2.0-or-later, as the rest of xrdome. No model weights, iw3 code or ncnn binaries in the repository.
- Allowed model licences: MIT, BSD-2-Clause, BSD-3-Clause, Apache-2.0. Anything else is refused, even if the file is present.
- Children run with `subprocess` and an argv list, never `shell=True`. A path argument that starts with `-` is passed as `./<path>`.
- Exit codes: 0 ok; 1 output exists (without `--force`); 2 setup/usage/refusal; 3 verify failed; 130 interrupted; a failing child's own status otherwise.
- Every decision line starts `path: ` and has the form `path: <key>=<value> (<reason>)`.
- Outputs never overwrite an existing file without `--force` and never overwrite the input, even with `--force`.
- Paths: config `~/.config/xrdome/tools.ini` (`$XDG_CONFIG_HOME`), data `$XDG_DATA_HOME/xrdome-tools/` (default `~/.local/share/xrdome-tools/`), cache `$XDG_CACHE_HOME/xrdome-tools/`.
- Defaults: codec `hevc`; quality `high`; container Matroska; `to3d` model `Any_V2_S`, divergence `2.0`, convergence `0.5`, foreground scale `0`; `enhance` chunk 10 s.
- Gate (CI and local): `meson setup build -Dwerror=true && meson compile -C build && meson test -C build --print-errorlogs` (unchanged) and `python3 -m pytest tools/tests -q` (new).
- Docs: every user-visible subcommand or option is documented in `docs/tools.md` in the same PR.

## Review Focus

1. **A file name with spaces, quotes, non-ASCII or a leading `-`** must work as input and produce a sensibly named output (Task 1, `test_runner_argv_safe`; Task 3, `test_output_name_odd_inputs`).
2. **A file whose first video stream is cover art** (`DISPOSITION:attached_pic=1`) must be probed by its real video stream, as xrdome's own probe does (Task 3, `test_probe_skips_attached_pic`).
3. **An ffmpeg built without an encoder or filter it lists elsewhere** (NVENC listed on an AMD machine, no `libplacebo`) must fall through to the next choice, not fail mid-job (Task 2, `test_choose_encoder_listed_but_broken`; Task 7, `test_upscale_lanczos_without_libplacebo`).
4. **Running out of disk or being interrupted mid-job** must leave no partial output and no temporary frames (Task 1, `test_runner_interrupt_cleans`; Task 7, `test_chunks_removed_on_failure`).
5. **`-o` pointing at the input file, or at an unwritable directory**, must be refused before any work starts (Task 3, `test_output_target_refuses_input`).

---

### Task 1: Package scaffold, runner, CI job, docs page

Commits the spec and this plan (they are not on `main` yet).

**Files:**
- Create: `tools/xrdome_tools/__init__.py`, `__main__.py`, `cli.py`, `config.py`, `run.py`, `errors.py`
- Create: `tools/xrdome-tools` (executable launcher: `exec python3 -m xrdome_tools "$@"` with `PYTHONPATH` set to its own directory)
- Create: `tools/tests/conftest.py`, `tools/tests/test_cli.py`, `tools/tests/test_run.py`, `tools/tests/test_config.py`, `tools/pytest.ini`
- Create: `docs/tools.md` (install, gate, one `## <subcommand>` heading per subcommand, each "not yet available")
- Create: `CLAUDE.md` (the gate commands above, the append points below, "update `docs/tools.md` with any user-visible change")
- Modify: `.github/workflows/ci.yml` (new job `tools`: ubuntu-24.04, `apt-get install python3-pytest ffmpeg`, run `python3 -m pytest tools/tests -q`)
- Modify: `README.md` (one line under the docs list linking `docs/tools.md`)
- Add: `docs/superpowers/specs/2026-11-17-xrdome-tools-design.md`, `docs/superpowers/plans/2026-11-17-xrdome-tools.md`

**Interfaces:**
- Produces:
  - `errors.ToolError(Exception)` with `.code: int`; constants `EXIT_EXISTS=1, EXIT_SETUP=2, EXIT_VERIFY=3, EXIT_INTERRUPTED=130`.
  - `config.Paths(config_file: Path, data_dir: Path, cache_dir: Path, iw3_dir: Path, models_dir: Path)`; `config.paths(env: Mapping[str,str] = os.environ) -> Paths` (`iw3_dir = data_dir/"iw3"`, `models_dir = data_dir/"models"`).
  - `config.program(name: str, paths: Paths | None = None) -> str`: `[programs] <name>` from `tools.ini`, else `name`.
  - `run.Runner(dry_run: bool = False, log: TextIO | None = None)` with `run(argv: list[str], *, capture: bool = False, timeout: float | None = None, cleanup: list[Path] = []) -> subprocess.CompletedProcess`; raises `run.ChildFailed(argv, returncode, stderr_tail: list[str])` (last 20 lines). In dry-run, prints `shlex.join(argv)` and returns a CompletedProcess with returncode 0 and empty output.
  - `run.say_path(key: str, value: str, reason: str) -> None`.
  - `run.safe_arg(p: Path) -> str` (prefixes `./` when the name starts with `-`).
  - `cli.SUBCOMMANDS: list[str]` of module names under `xrdome_tools`; each module defines `add_parser(sub: argparse._SubParsersAction) -> None` and `run(args: argparse.Namespace) -> int`. `cli.main(argv: list[str] | None = None) -> int` maps `ToolError` to its code, `ChildFailed` to its returncode, `KeyboardInterrupt` to 130.
- **Append points** (one line per later task): `cli.SUBCOMMANDS`, `docs/tools.md` sections, `CLAUDE.md` status line.

- [ ] **Step 1: Write the failing tests**
  - `test_cli.py::test_help_lists_subcommands`: with `SUBCOMMANDS` monkeypatched to a fake module, `main(["--help"])` exits 0 and prints the fake name.
  - `test_cli.py::test_toolerror_exit_code`: a fake subcommand raising `ToolError(..., code=2)` makes `main` return 2 and print the message to stderr prefixed `xrdome-tools: `.
  - `test_run.py::test_dry_run_prints_quoted`: `Runner(dry_run=True).run(["ffmpeg","-i","a b.mkv"])` prints `ffmpeg -i 'a b.mkv'` and starts no process.
  - `test_run.py::test_child_failed_tail`: running `python3 -c` that prints 30 lines to stderr and exits 4 raises `ChildFailed` with `returncode == 4` and 20 tail lines.
  - `test_run.py::test_runner_argv_safe`: `safe_arg(Path("-x.mkv")) == "./-x.mkv"`; a name with a space, a quote and `é` round-trips through `Runner.run(["python3","-c","import sys;print(sys.argv[1])", name], capture=True)`.
  - `test_run.py::test_runner_interrupt_cleans`: a child that sleeps, interrupted by sending `SIGINT` to the test process from a timer, raises `KeyboardInterrupt`, the child is gone, and every path in `cleanup` is deleted.
  - `test_run.py::test_log_records_argv`: with `log=io.StringIO()`, the log holds the joined argv and the wall time.
  - `test_config.py::test_xdg_defaults` and `test_xdg_overrides`: `paths({"HOME": "/h"}).data_dir == Path("/h/.local/share/xrdome-tools")`; with `XDG_DATA_HOME=/d`, `/d/xrdome-tools`.
  - `test_config.py::test_program_override`: a `tools.ini` with `[programs]\nffmpeg = /opt/ff/bin/ffmpeg` gives that path; a missing file gives `"ffmpeg"`.
- [ ] **Step 2: Run** `python3 -m pytest tools/tests -q`; expect failures (modules missing).
- [ ] **Step 3: Implement** the modules and launcher to the interfaces above. `Runner` kills the child's process group on `KeyboardInterrupt`, deletes `cleanup`, then re-raises.
- [ ] **Step 4: Run** `python3 -m pytest tools/tests -q`; expect PASS. Run `tools/xrdome-tools --help`; expect usage with no subcommands listed.
- [ ] **Step 5: Run the whole gate** (Global Constraints); expect PASS.
- [ ] **Step 6: Commit** `feat(tools): xrdome-tools scaffold, runner and CI job`.

### Task 2: Hardware detection and `doctor`

**Files:**
- Create: `tools/xrdome_tools/hw.py`, `tools/xrdome_tools/doctor.py`
- Create: `tools/tests/test_hw.py`, `tools/tests/test_doctor.py`, `tools/tests/fixtures/hw/` (text files, see Step 1)
- Modify (append): `cli.SUBCOMMANDS` (`"doctor"`), `docs/tools.md` (`## doctor`)

**Interfaces:**
- Consumes: `run.Runner`, `run.say_path`, `config.paths`, `config.program`.
- Produces:
  - `hw.VaapiInfo(device: str, encode: set[str], decode: set[str])` where profiles are vainfo names such as `VAProfileHEVCMain10`.
  - `hw.Probe(platform: str, gpus: list[str], encoders_built: set[str], encoders_ok: dict[str, str], vaapi: VaapiInfo | None, vulkan_devices: list[str], compute: str)`; `platform` is `"linux"` or `"wsl2"`; `gpus` items `"nvidia" | "amd" | "intel"`; `encoders_ok[name]` is `"ok"` or the error line; `compute` is `"cuda" | "rocm" | "none" | "not set up"`.
  - Parsers (pure): `parse_proc_version(text: str) -> bool` (True for WSL2), `parse_drm_vendors(vendor_ids: list[str]) -> list[str]`, `parse_ffmpeg_encoders(text: str) -> set[str]`, `parse_vainfo(text: str) -> VaapiInfo`, `parse_vulkaninfo_summary(text: str) -> list[str]`, `parse_nvidia_smi_list(text: str) -> list[str]`.
  - `hw.probe(runner: Runner, *, encoders: bool = True, compute: bool = True) -> Probe`.
  - `hw.EncoderChoice(name: str, family: str, reason: str)`, family `"nvenc" | "vaapi" | "software"`.
  - `hw.choose_encoder(p: Probe, codec: str, override: str | None = None) -> EncoderChoice`; codec `"hevc" | "av1"`; order and wording per spec 6.3; raises `ToolError(code=2)` when `override` names an encoder whose test failed or is not built.
  - `doctor.SECTIONS: list[Callable[[Probe], list[tuple[str, str]]]]` (append point: Task 5 adds models).

- [ ] **Step 1: Write fixtures and failing tests.** Fixtures, each with a first line `# source: <machine, tool version, date>` (synthetic ones say `# source: synthetic, from <doc URL>`; Task 8 replaces them with captures): `proc_version_wsl2.txt`, `proc_version_gentoo.txt`, `ffmpeg_encoders_full.txt` (lists `hevc_nvenc av1_nvenc hevc_vaapi av1_vaapi libx265 libsvtav1`), `ffmpeg_encoders_minimal.txt` (only `libx264`), `vainfo_radeon.txt` (HEVC Main/Main10 and AV1 with `VAEntrypointEncSlice`), `vainfo_decode_only.txt`, `vulkaninfo_summary_radeon.txt`, `nvidia_smi_L.txt`. Tests:
  - `test_parse_proc_version`: WSL2 fixture True, Gentoo False.
  - `test_parse_drm_vendors`: `["0x10de", "0x1002", "0x8086", "0x1234"] -> ["nvidia","amd","intel"]`.
  - `test_parse_ffmpeg_encoders`: full fixture contains all six; minimal contains none of them.
  - `test_parse_vainfo`: Radeon fixture `encode >= {"VAProfileHEVCMain","VAProfileHEVCMain10","VAProfileAV1Profile0"}`; decode-only fixture `encode == set()`.
  - `test_choose_encoder_order`: NVENC ok wins; NVENC failing, VA-API ok gives `hevc_vaapi`; neither gives `libx265` with a reason containing `slow`; `codec="av1"` with only software gives `libsvtav1`.
  - `test_choose_encoder_listed_but_broken` (Review Focus 3): `hevc_nvenc` built but `encoders_ok["hevc_nvenc"]` is an error line, VA-API ok: chooses `hevc_vaapi` and the reason names the NVENC error.
  - `test_choose_encoder_override_broken`: `override="hevc_nvenc"` when it failed raises `ToolError` with code 2 and the error line.
  - `test_test_encode_argv`: the argv `hw` builds for a VA-API test encode contains `-vaapi_device /dev/dri/renderD128` and `format=nv12,hwupload` and `-f null -`, for NVENC neither.
  - `test_doctor_json`: with `hw.probe` monkeypatched to a fixed `Probe`, `main(["doctor","--json"])` prints JSON with keys `platform, gpus, encoders, vaapi, vulkan, compute, paths` and a `paths` entry per subcommand (`transcode`, `to3d`, `enhance`).
- [ ] **Step 2: Run** `python3 -m pytest tools/tests/test_hw.py tools/tests/test_doctor.py -q`; expect FAIL.
- [ ] **Step 3: Implement.** Probes per spec 6.3 table. Test encode: `ffmpeg -hide_banner -loglevel error -f lavfi -i testsrc2=s=256x256:d=1 [-vaapi_device DEV -vf format=nv12,hwupload] -c:v ENC -f null -`, timeout 20 s. `nvidia-smi` is also tried at `/usr/lib/wsl/lib/nvidia-smi`. Compute: if `paths().iw3_dir/"venv/bin/python"` exists, run it with a one-liner printing `torch.cuda.is_available()`, `torch.version.cuda`, `torch.version.hip`; a HIP version with CUDA available is `rocm`.
- [ ] **Step 4: Run** the tests; expect PASS. Run `tools/xrdome-tools doctor` in the dev container; expect a table, `encode=libx265` or a hardware encoder, and exit 0.
- [ ] **Step 5: Gate, then commit** `feat(tools): hardware detection and doctor`.

### Task 3: Formats, detection rules shared with the player, naming and tags

**Files:**
- Create: `tools/xrdome_tools/formats.py`, `tools/tests/test_formats.py`, `tools/tests/fixtures/ffprobe/*.json`
- Create: `tests/data/detect_names.tsv`, `tests/data/detect_shapes.tsv`
- Modify: `tests/test_detect.c` (`name_table` and the `rows[]` of `shape_nothing_known` read the TSV files; path from env `XRD_TEST_DATA`), `meson.build` (pass `XRD_TEST_DATA=<source>/tests/data` to the detect test)

**Interfaces:**
- Consumes: `run.Runner`, `run.safe_arg`, `config.program`, `errors.ToolError`.
- Produces:
  - `formats.Format(proj: str, layout: str, packing: str | None)` (frozen); proj `"hequirect" | "flat"`; layout `"mono" | "sbs-lr" | "sbs-rl" | "tb-lr" | "tb-rl"`; packing `"full" | "half" | None`.
  - `formats.AS_CHOICES: dict[str, Format]` for `vr180, flat, hsbs, fsbs, htb, ftb` and `-rl` variants of the stereo ones.
  - `formats.ProbeInfo(width, height, sar: tuple[int,int], codec: str, pix_fmt: str, bit_depth: int, duration: float, fps: tuple[int,int], vfr: bool, stereo: str | None, stereo_inverted: bool, spherical: str | None, video_index: int, streams: list[dict])`.
  - `formats.probe(runner: Runner, path: Path) -> ProbeInfo` (runs `ffprobe -v error -show_streams -show_format -of json`); `formats.parse_ffprobe(data: dict) -> ProbeInfo`.
  - `formats.name_hints(basename: str) -> dict[str, str]` (keys among `proj, layout, packing`), a port of spec flat-projection 5.8's token table.
  - `formats.detect(info: ProbeInfo, basename: str, as_: str = "auto") -> tuple[Format, dict[str, str]]` (second value: field to source, `"flag" | "metadata" | "file name" | "frame shape"`).
  - `formats.output_tags(f: Format) -> dict[str, str]` (`{"stereo_mode": ...}`, values per spec 6.4 table).
  - `formats.output_name(stem: str, f: Format, extra: list[str] = []) -> str` (appends spec 6.4 tokens unless present, full top-bottom uses `_TB`; returns `stem + tokens + ".mkv"`).
  - `formats.output_target(input: Path, name: str, out: Path | None, outdir: Path | None, force: bool) -> Path`.
  - `formats.is_stereo(f: Format) -> bool`.

- [ ] **Step 1: Write the TSVs** from the current C arrays, one row per line: `detect_names.tsv` columns `name proj layout packing` (`-` for unset); `detect_shapes.tsv` columns `width height proj layout`.
- [ ] **Step 2: Change `tests/test_detect.c`** to read the TSVs (same assertions); run `meson test -C build detect`; expect PASS (no behaviour change).
- [ ] **Step 3: Write failing Python tests:**
  - `test_name_hints_table` and `test_shape_table`: every TSV row gives the same result in Python.
  - `test_name_examples`: the cases in `name_examples` of `test_detect.c` (`movie.HSBS.1080p.mkv`, `Routes.1800.mkv`, `ou.est.tb.mkv`, `A_OU.mkv`, `3D.film.OU.mkv`).
  - `test_parse_ffprobe_stereo`: a fixture with `side_data_list: [{"side_data_type": "Stereo 3D", "type": "side by side", "inverted": 1}]` gives `stereo == "side by side"`, `stereo_inverted`; detect gives layout `sbs-rl` from `metadata`. Fixtures are produced with `ffmpeg -f lavfi -i testsrc=s=1920x1080:d=1 -c:v libx264 -metadata:s:v stereo_mode=right_left x.mkv` and `ffprobe ... -of json`; the fixture's first key `"_source"` records the FFmpeg version.
  - `test_probe_skips_attached_pic` (Review Focus 2): stream 0 with `disposition.attached_pic == 1`, stream 1 video: `video_index == 1` and the size is stream 1's.
  - `test_detect_owner_vr180`: 2880x1440 and 4320x2160 with no metadata give `hequirect, sbs-lr` from `frame shape`.
  - `test_output_tags`: `fsbs -> left_right`, `sbs-rl -> right_left`, `ftb -> top_bottom`, flat 2D `-> mono`.
  - `test_output_name`: `("clip", fsbs) -> "clip_FSBS.mkv"`; `("clip_FSBS", fsbs) -> "clip_FSBS.mkv"`; `("x", vr180) -> "x_VR180_LR.mkv"`; `("x", ftb) -> "x_TB.mkv"`; right-left SBS adds `_RL`.
  - `test_output_name_odd_inputs` (Review Focus 1): stems `"-a"`, `"a b"`, `"é"` give names that `name_hints` reads back to the same format.
  - `test_output_target_refuses_input` (Review Focus 5): `out` equal to the input (also via a symlink) raises `ToolError` code 2 even with `force`; an existing output without `force` raises code 1; an unwritable `outdir` raises code 2.
- [ ] **Step 4: Implement** `formats.py` to pass them.
- [ ] **Step 5: Gate, then commit** `feat(tools): format detection, naming and tags; share detect cases with the player`.

### Task 4: `transcode`

**Files:**
- Create: `tools/xrdome_tools/encode.py`, `tools/xrdome_tools/transcode.py`, `tools/tests/test_encode.py`, `tools/tests/test_transcode.py`, `tools/tests/test_transcode_ffmpeg.py`
- Modify (append): `cli.SUBCOMMANDS` (`"transcode"`), `docs/tools.md` (`## transcode`)

**Interfaces:**
- Consumes: `hw.probe`, `hw.choose_encoder`, `hw.EncoderChoice`, `formats.*`, `run.*`.
- Produces:
  - `encode.QUALITY: dict[str, dict[str, list[str]]]`: `{"high"|"normal"|"small": {"nvenc"|"vaapi"|"software": [ffmpeg args]}}`. Provisional values, each marked `# provisional, Task 8 settles`: nvenc `-rc vbr -cq 20/24/28 -preset p5`, vaapi `-rc_mode QVBR -global_quality 20/24/28` with fallback `-rc_mode CQP -qp 20/24/28`, x265 `-crf 18/22/26 -preset medium`, SVT-AV1 `-crf 24/30/36 -preset 6`.
  - `encode.video_args(choice: EncoderChoice, quality: str, ten_bit: bool, max_width: int | None, f: Format, vaapi_device: str | None) -> list[str]` (input-side args returned separately: `encode.input_args(choice, vaapi_device) -> list[str]`).
  - `encode.stream_args(info: ProbeInfo) -> tuple[list[str], list[str]]` (ffmpeg args; human lines for every stream re-encoded or dropped).
  - `transcode.transcode(runner: Runner, src: Path, dst: Path, *, as_: str = "auto", codec: str = "hevc", quality: str = "high", max_width: int | None = None, encoder: str | None = None, reencode: bool = False, extra_tokens: list[str] = []) -> Path` (used by Tasks 6 and 7).
  - `transcode.verify(runner: Runner, src_info: ProbeInfo, dst: Path, f: Format, codec: str) -> list[str]` (empty when ok; reasons otherwise).

- [ ] **Step 1: Write failing unit tests** (`test_encode.py`, `test_transcode.py`, argv only, runner in dry-run or monkeypatched):
  - `test_ten_bit_follows_source`: a 10-bit source gives `-pix_fmt p010le` (software) / `format=p010,hwupload` (vaapi) / `-profile:v main10` (nvenc); 8-bit gives the 8-bit forms.
  - `test_max_width_keeps_eyes`: `max_width=3840` on 5760x2880 VR180 gives `scale=3840:-2`; on half SBS 3840x1080 with `max_width=1920` gives `scale=1920:1080` (keeps the shape so each eye scales the same).
  - `test_streams`: AAC audio and SRT subtitles get `-c:a copy -c:s copy`; a `mov_text` subtitle is converted to `srt` with a line; a stream Matroska cannot hold is dropped with a line naming it.
  - `test_remux_when_friendly`: an HEVC source within limits and no tags gives `-c copy` plus `-metadata:s:v:0 stereo_mode=...` and the line `path: transcode=remux (already HEVC ...)`; `reencode=True` gives a full encode.
  - `test_h264_too_wide_is_reencoded`: 4320x2160 H.264 is re-encoded (never remuxed), whatever its tags.
  - `test_vaapi_qvbr_fallback`: when the QVBR test encode fails, the args use `-rc_mode CQP`.
  - `test_hdr_passthrough`: a source with `color_transfer=smpte2084` gives `-color_primaries bt2020 -color_trc smpte2084 -colorspace bt2020nc` for software and NVENC; for VA-API, if the test encode with them fails, a line says HDR metadata is not passed through.
  - `test_verify_reasons`: a destination ProbeInfo with the wrong stereo side data, 1 s short, or missing an audio stream gives one reason each.
- [ ] **Step 2: Write failing integration tests** in `test_transcode_ffmpeg.py` (skipped with a reason when `ffmpeg` or `libx265` is missing; CI has both): generate 2 s clips with `tools/make_pattern.sh` kinds `sbs180`, `flat2d`, `flat-hsbs`, `flat-tb` using `libx264`, run `transcode(..., encoder="libx265", quality="small")`, then assert codec `hevc`, the expected `Stereo 3D` side data, the name token, duration within 0.5 s, and audio present when the source had it. `test_dry_run_runs_nothing`: `--dry-run` creates no file.
- [ ] **Step 3: Run**; expect FAIL. **Step 4: Implement** `encode.py`, `transcode.py` and the `transcode` subcommand (options per spec 6.4 plus `--encoder`, `--dry-run`, `-o`, `--outdir`, `--force`). The log goes to `<dst>.log`.
- [ ] **Step 5: Run** the tests; expect PASS. **Step 6: Gate, then commit** `feat(tools): transcode`.

### Task 5: Model registry, `models` and optional fetch

**Files:**
- Create: `tools/xrdome_tools/models.py`, `tools/tests/test_models.py`
- Modify (append): `cli.SUBCOMMANDS` (`"models"`), `doctor.SECTIONS` (one line), `docs/tools.md` (`## models`, with the full table of links and paths)

**Interfaces:**
- Consumes: `config.paths`, `run.Runner`, `errors.ToolError`.
- Produces:
  - `models.Model(name: str, used_by: str, licence: str, page: str, url: str, revision: str, file: str, sha256: str, dest: str)` where `dest` is relative to `Paths.data_dir`.
  - `models.REGISTRY: list[Model]`, initially: `Any_V2_S` (`depth_anything_v2_vits.pth`, Apache-2.0, HF `depth-anything/Depth-Anything-V2-Small` at `03876f8651c73a60fe4c2c48294e09fcb6838fcf`, sha256 `715fade13be8f229f8a70cc02066f656f2423a59effd0579197bbf57860e1378`); `VDA_S` (`video_depth_anything_vits.pth`, Apache-2.0, HF `depth-anything/Video-Depth-Anything-Small` at `256875362cff76724b920335dfb4b29dd611f66e`, sha256 `13379300b739e659f076a59d52e9801bd8d38c541a7e71f73bbca4dcfb013609`); both with `dest="iw3/nunif/iw3/pretrained_models/hub/checkpoints"`. The three iw3 support models with the sha256 values in sbs-test `NOTES.md` (`iw3_row_flow_v3_20250627.pth 2e4daa14...`, `iw3_depth_aa_20250530.pth 5a36c8eb...`, `transnetv2-pytorch-weights.pth 5e896c00...`), marked `fetched_by="iw3"`. RIFE and Real-ESRGAN ncnn entries are added by Task 7.
  - `models.ALLOWED_LICENCES = {"MIT","BSD-2-Clause","BSD-3-Clause","Apache-2.0"}`.
  - `models.status(m: Model, paths: Paths) -> str` (`"ok" | "missing" | "bad hash"`).
  - `models.require(name: str, paths: Paths) -> Path` (raises `ToolError(code=2)` with the instructions for that model, or "not an allowed model").
  - `models.fetch(runner: Runner, m: Model, paths: Paths) -> Path` (downloads with `urllib.request` to `<file>.part`, checks sha256, renames; deletes on mismatch and raises code 2).

- [ ] **Step 1: Write failing tests:**
  - `test_registry_licences`: every entry's licence is in `ALLOWED_LICENCES`; a registry entry added in the test with `CC-BY-NC-4.0` makes `require` raise "not an allowed model".
  - `test_status`: a temp `data_dir` with the right bytes for a test entry (sha256 of `b"x"`) is `ok`, wrong bytes `bad hash`, absent `missing`.
  - `test_instructions`: for a missing entry, `models` output names the page URL, the absolute destination path and `xrdome-tools models --check`.
  - `test_unknown_file_warned`: an extra `.pth` in a checkpoints folder is named in a warning.
  - `test_fetch_hash_mismatch`: `fetch` against a local `http.server` serving wrong bytes deletes `.part` and raises code 2; right bytes give the file in place.
  - `test_fetch_never_automatic`: `require` of a missing model never opens a URL (monkeypatch `urllib.request.urlopen` to fail the test).
- [ ] **Step 2: Run**; expect FAIL. **Step 3: Implement** `models.py` and the subcommand: `models` (list), `models --check`, `models --fetch [NAME...]`.
- [ ] **Step 4: Run**; expect PASS. **Step 5: Gate, then commit** `feat(tools): model registry with optional fetch`.

### Task 6: `setup to3d` and `to3d`

**Files:**
- Create: `tools/xrdome_tools/iw3env.py`, `tools/xrdome_tools/setup.py`, `tools/xrdome_tools/to3d.py`, `tools/tests/test_iw3env.py`, `tools/tests/test_to3d.py`
- Modify (append): `cli.SUBCOMMANDS` (`"setup"`, `"to3d"`), `docs/tools.md` (`## setup`, `## to3d`)

**Interfaces:**
- Consumes: `hw.probe`, `models.require`, `models.fetch`, `transcode.transcode`, `formats.*`, `run.*`.
- Produces:
  - `iw3env.PINS = {"nunif": "0f8b86b952d3f3e294c8d887fbf615a679e7f4ca", "Depth-Anything_iw3": "92eaac92bcc2c275f48c1f866ec708861055e435", "Video-Depth-Anything_iw3": "e9541668c380524f128a3a75886c5f9143214554"}` (from sbs-test).
  - `iw3env.torch_variant(p: Probe, cpu: bool) -> str` (`"cuda" | "rocm" | "cpu"`; raises `ToolError(code=2)` for no GPU without `cpu`).
  - `iw3env.setup_steps(paths: Paths, variant: str) -> list[list[str]]` (the argv of every step: clone, checkout pin, venv, pip installs, hub pins), idempotent when run again.
  - `iw3env.python(paths: Paths) -> Path`.
  - `to3d.iw3_argv(paths: Paths, src: Path, dst: Path, *, model: str, divergence: float, convergence: float, fg_scale: float, fps: tuple[int,int], half: bool, nvenc: bool) -> list[str]`.
  - `to3d.output_stem(stem: str, model: str, divergence: float, convergence: float, fg_scale: float) -> str` (`<stem>_<AnyV2S|VDAS>_div<D>[_c<C>][_fs<F>]`).

- [ ] **Step 1: Write failing tests:**
  - `test_torch_variant`: NVIDIA (native or WSL2) gives `cuda`; AMD gives `rocm`; neither raises code 2 whose message contains `--cpu` and `about 60 fps on an RTX 5070 Ti`; neither with `cpu=True` gives `cpu`.
  - `test_setup_steps_pins`: the steps check out each pin; a ROCm variant installs from the ROCm wheel index, the CUDA one uses nunif's `requirements-torch.txt`.
  - `test_setup_prints_size_and_needs_yes`: without a TTY and without `--yes`, `setup to3d` exits 2 before running anything, with the download size in the message.
  - `test_setup_fetch_models`: `--fetch-models` calls `models.fetch` for `Any_V2_S` and `VDA_S` only.
  - `test_iw3_argv`: defaults give `--depth-model Any_V2_S --divergence 2.0 --convergence 0.5`, `--max-fps` equal to the source rate rounded up (120 for a 119.88 source), no `--foreground-scale`; `VDA_S` adds `--ema-normalize --scene-detect`; `nvenc=True` gives `--video-codec hevc_nvenc`, otherwise `--video-codec libx264 --crf 12`.
  - `test_model_allow_list`: `--model Any_V2_L` exits 2 with "not an allowed model".
  - `test_output_stem`: matches sbs-test's names (`pan_AnyV2S_div2.0`, `pan_VDAS_div3.0_c0.65_fs1.0`).
  - `test_refuse_without_compute` and `test_refuse_before_setup`: exit 2 with the messages in spec 6.8.
  - `test_pipeline`: with the runner monkeypatched, a run calls iw3, then `transcode.transcode(..., as_="fsbs")` (or `"hsbs"` with `--half`) on the intermediate, and deletes the intermediate.
  - `test_hint_low_res_or_fps`: a 720p or 30 fps input prints the hint to run `enhance` first.
- [ ] **Step 2: Run**; expect FAIL. **Step 3: Implement.** Letterbox/pillarbox is iw3's own behaviour for full SBS at 1920x1080 per eye; if iw3 stretches a non-16:9 source, pad to 16:9 with ffmpeg first (`pad=...:(ow-iw)/2:(oh-ih)/2`) and say so in a `path:` line.
- [ ] **Step 4: Run**; expect PASS. **Step 5: Gate, then commit** `feat(tools): setup to3d and to3d`.

### Task 7: `enhance` (RIFE interpolation and upscaling, 2D only)

**Files:**
- Create: `tools/xrdome_tools/frames.py`, `tools/xrdome_tools/enhance.py`, `tools/tests/test_frames.py`, `tools/tests/test_enhance.py`
- Modify (append): `cli.SUBCOMMANDS` (`"enhance"`), `models.REGISTRY` (RIFE and Real-ESRGAN ncnn entries), `docs/tools.md` (`## enhance`)

**Interfaces:**
- Consumes: `transcode.transcode`, `encode.*`, `formats.*`, `models.require`, `config.program`, `run.*`.
- Produces:
  - `frames.Chunk(index: int, start_frame: int, count: int, dir_in: Path, dir_out: Path)`.
  - `frames.plan_chunks(total_frames: int, fps: tuple[int,int], seconds: float, overlap: int) -> list[tuple[int,int]]` (start, count; consecutive chunks share `overlap` frames).
  - `frames.disk_estimate(width: int, height: int, frames_per_chunk: int, scale: float, fps_factor: float) -> int` (bytes, PNG at 3 bytes per pixel as the upper bound).
  - `frames.run_chunks(runner: Runner, src: Path, chunks: list[tuple[int,int]], tool_argv: Callable[[Path, Path], list[str]], tmp: Path) -> Iterator[Path]` (yields each output folder; deletes input folders as it goes; deletes everything on failure or interrupt).
  - `enhance.rife_argv(model_dir: Path, src: Path, dst: Path, target_count: int) -> list[str]` (`rife-ncnn-vulkan -m <dir> -i <src> -o <dst> -n <count>`).
  - `enhance.esrgan_argv(model: str, model_dir: Path, src: Path, dst: Path, scale: int) -> list[str]`.
  - `enhance.lanczos_filter(has_libplacebo: bool, w: int, h: int) -> str`.

- [ ] **Step 1: Write failing tests:**
  - `test_plan_chunks`: 610 frames at 30 fps, 10 s, overlap 1 gives `[(0,300),(299,300),(598,12)]`; every frame pair `(i,i+1)` lies in one chunk.
  - `test_interp_frame_counts`: 24 to 60 fps over 240 source frames targets 600 output frames; the chunk targets sum to that after removing overlap duplicates.
  - `test_vfr_made_constant`: a VFR source (ProbeInfo `vfr=True`, 59.86 measured, 60 nominal) adds `-fps_mode cfr -r 60` to the extraction and prints a line.
  - `test_refuse_stereo`: a `fsbs` or VR180 input exits 2 naming `--treat-as-2d`; with it, the run proceeds.
  - `test_upscale_lanczos_without_libplacebo` (Review Focus 3): `ffmpeg -filters` without `libplacebo` gives `scale=W:H:flags=lanczos` and a `path:` line; with it, `libplacebo=w=W:h=H:upscaler=ewa_lanczos`.
  - `test_disk_check`: free space (monkeypatched `shutil.disk_usage`) below the estimate exits 2 with the estimate and `--tmp`.
  - `test_chunks_removed_on_failure` (Review Focus 4): a tool argv that fails on chunk 2 leaves `tmp` empty and raises `ChildFailed`.
  - `test_missing_binary`: no `rife-ncnn-vulkan` on PATH exits 2 with the upstream release page.
  - `test_combined_order`: `--fps 60 --upscale 2 --method model` runs interpolation then upscaling per chunk, then one final encode through `transcode` with the source audio.
- [ ] **Step 2: Add registry entries:** RIFE v4.6 (MIT, `nihui/rife-ncnn-vulkan` release `20221029`) and Real-ESRGAN `realesr-animevideov3` x2 (BSD-3-Clause, `xinntao/Real-ESRGAN-ncnn-vulkan` release `v0.2.0`), with `sha256` read from the release archives during this task and recorded with their source URL. If a hash cannot be obtained in the task's environment, leave the entry out and say so in the PR.
- [ ] **Step 3: Run**; expect FAIL. **Step 4: Implement** `frames.py`, `enhance.py` and the subcommand (`--fps N`, `--upscale 2|3|4`, `--method lanczos|model`, `--chunk SECONDS`, `--tmp DIR`, `--treat-as-2d`, plus the common options).
- [ ] **Step 5: Run**; expect PASS. **Step 6: Gate, then commit** `feat(tools): enhance (RIFE, upscaling)`.

### Task 8: Hardware run (owner at the machines)

Needs the owner on the laptop (Gentoo, Radeon 860M) and the desktop (WSL2, RTX 5070 Ti) with the Viture. Covers spec section 9 and the hardware checks in section 7.

**Files:**
- Modify: `tools/tests/fixtures/hw/*` (replace synthetic fixtures with captures from both machines), `tools/xrdome_tools/encode.py` (`QUALITY` values settled, `# provisional` comments removed), `docs/tools.md` (measured results), `docs/STATUS.md` (a `## xrdome-tools` section)

- [ ] **Step 1:** On each machine: `tools/xrdome-tools doctor --json > doctor-<machine>.json`, plus the raw outputs the parsers read (`ffmpeg -hide_banner -encoders`, `vainfo`, `vulkaninfo --summary`, `nvidia-smi -L`, `/proc/version`). Commit the raw outputs as fixtures; tests still pass.
- [ ] **Step 2:** Quality: encode one 60 s 1080p clip at three settings per encoder family available on that machine; the owner picks `high`, `normal`, `small` from size and a look on the glasses. Record in `encode.QUALITY` with the date and machine.
- [ ] **Step 3:** Laptop: does `-rc_mode QVBR` work with `hevc_vaapi`? Record the answer in a comment beside `QUALITY`.
- [ ] **Step 4:** `transcode test2.mp4` (4320x2160 H.264) on both machines; then on the laptop, on AC: `xrdome --require-hwdec --frames 3600 --timing-log t.csv <output>`. Expect `hwdec: vaapi`, `detect: ... (metadata)`, 0 missed of 3600.
- [ ] **Step 5:** Desktop: `setup to3d --fetch-models`, then `to3d pan.mp4`; compare size and frame count with sbs-test's `pan_AnyV2S_div2.0_FSBS.mkv`. Laptop: `setup to3d`; record whether ROCm PyTorch sees gfx1152. If it does not, `docs/tools.md` says so and that is accepted.
- [ ] **Step 6:** `enhance --fps 60` on a 30 fps clip on both machines; record frames per second and how scene cuts look.
- [ ] **Step 7:** Commit `docs(tools): hardware run`.

---

## Task dependencies (for the queue)

| Task | Builds on | Runs after | Parallel with |
|---|---|---|---|
| 1 scaffold | none | none | none (alone) |
| 2 detection, doctor | 1 | none | 3, 5 |
| 3 formats | 1 | none | 2, 5 |
| 4 transcode | 2, 3 | none | 5 |
| 5 models | 1 (and 2 for `doctor.SECTIONS`) | none | 3, 4 |
| 6 to3d | 4, 5 | none | 7 |
| 7 enhance | 4, 5 | none | 6 |
| 8 hardware run | 6, 7 | 6, 7 merged | none; needs the owner |

Append points shared by parallel tasks: `cli.SUBCOMMANDS`, `docs/tools.md` sections, `doctor.SECTIONS`, `models.REGISTRY`, `CLAUDE.md` status line.
