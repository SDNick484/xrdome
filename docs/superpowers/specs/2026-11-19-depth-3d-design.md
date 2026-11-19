# Depth-based 3D for photos and flat video: design

Status: approved by the owner on 2026-11-19. Plan: `docs/superpowers/plans/2026-11-19-depth-3d.md`.

## 1. Goal

Show ordinary photos, and later ordinary flat films, in 3D on the glasses, using depth estimated from the single
image. Photo mode comes first. `xrdome-tools to3d` already converts films to SBS offline with iw3; this is the live
path, in the player, with no conversion step.

## 2. What a depth spike measured (laptop: Ryzen AI 7 350, Radeon 860M, Mesa RADV, 2026-11-18/19)

A private spike measured Depth Anything V2 Small on every runtime the laptop could run, beside xrdome on the Viture
Pro XR. Numbers this design rests on:

- **Runtime: ncnn on Vulkan, fp16** (pip wheel `ncnn==1.0.20261020`). ROCm/MIGraphX could not be installed for
  gfx1152, the NPU has no usable path, onnxruntime's WebGPU plugin was slower, and int8 was slower than fp16 on
  Vulkan at every size.
- **Photo**: model input 518x392 (landscape) or 392x518 (portrait). Warm, end to end (decode, EXIF, resize, infer,
  upsample, 16-bit PNG): median about 400 ms, p95 831 ms per photo; inference itself about 190 ms. Cold start
  (process, imports, model load) a few seconds.
- **Video**: model input 322x182, inference p95 24.0 ms; a 30 Hz loop held 30.0 Hz (worst 1 s window 29.8 Hz) while
  xrdome rendered 3600 frames with 1 missed vsync (1 alone). Depth quality against fp32 518x294: 0.058 mean abs
  error vs 0.043 at 392x224 (which needs 43.8 ms); the owner accepted 322x182. Frame-to-frame flicker 0.015 vs
  0.011 at 392x224.
- **Contention**: depth inference on the GPU did not cost xrdome frames (3/3600 with one photo per 2 s); the same
  photo work on the CPU (onnxruntime) did (37/3600). Depth work must stay on the GPU while xrdome renders.
- Model: Depth Anything V2 Small, Apache-2.0 (Base and Large are CC-BY-NC and stay refused, as in `models`).
- Measured once each, on a re-encoded test file, 90 s runs; no long thermal run yet.

## 3. Architecture

```
xrdome (C)                                   xrdome-tools depth (Python, own venv)
  mpv -> video texture ----------------.       setup depth: venv + ncnn models (sha256-checked)
  depth texture (R16) <- cache file <--+---    depth photo FILE|DIR: writes cache files
  eye pass: flat branch + parallax     |       depth serve: Unix socket, photo requests (and video, later)
                                       '--->   frames (video, later): shared memory
```

- **Inference is a separate process**, never linked into xrdome: xrdome keeps no ML dependency, a crash in the
  helper leaves the player running in 2D, and the helper owns its own Vulkan device, which is the arrangement the
  spike measured.
- **The helper is part of xrdome-tools.** `xrdome_tools/` stays standard-library only (CLAUDE.md): the subcommands
  `setup depth` and `depth` are stdlib launchers; the code that needs numpy/ncnn lives in a separate package,
  `tools/xrdome_depth/`, run with the depth venv's interpreter (like `to3d` runs iw3 in its venv).
- **Linux first.** The macOS build must keep compiling and run in 2D; the depth path there is unverified.

## 4. Models and environment (`xrdome-tools setup depth`)

- Venv at `~/.local/share/xrdome-tools/depth/venv/` (`$XDG_DATA_HOME`), pinned: `ncnn==1.0.20261020`,
  `numpy==2.5.4`, `opencv-python-headless==5.0.0.93`, `pillow==13.0.0` (versions the spike ran).
- ncnn models are generated, not downloaded (nobody publishes them): a temporary export venv with
  `torch==2.11.0+cpu` (CPU wheel index), `transformers==5.23.0`, `pnnx==20261020`, `safetensors==0.8.0`,
  `huggingface_hub==2.4.0`, loads `depth-anything/Depth-Anything-V2-Small-hf` at revision
  `5426e4f0f36572d16453bbda7a8389317b1bef99` (`model.safetensors` sha256
  `3152477ce0d8d6978d76b995120de97cb5b928701fd0f817769f59e249a16b70`, Apache-2.0), traces it with
  `attn_implementation="eager"` and converts with pnnx to `dav2s_<W>x<H>.ncnn.{param,bin}` (input `in0`, output
  `out0`, fp32 weights) for 518x392, 392x518 and 322x182. The export venv is deleted afterwards (about 1 GB).
- Expected sha256 of the outputs (the spike's re-export on a second machine matched its first, so the export is
  deterministic with these pins):

  | file | sha256 |
  |---|---|
  | `dav2s_518x392.ncnn.param` | `d9d9a8eef2f63e8f3268184602dd79765e13b23acc4fb311c528c735fe3fa26c` |
  | `dav2s_518x392.ncnn.bin` | `f155d78193416c6c0e9f55dd0c36b5bd94d25410c23bbfb51439107d2a6d1846` |
  | `dav2s_392x518.ncnn.param` | `a9e2d29e96ce6b3d067c172665519eb7854e3216fd8348ec603b32008da6d839` |
  | `dav2s_392x518.ncnn.bin` | `282be6cdd0c2fd01eb11001a600d9945a04edd46f047bd2d83a34cc937bbfdc8` |
  | `dav2s_322x182.ncnn.param` | `b1373247d51fa69b49757559e549cfb2b00e9541c9a7fc90cfe308e4cf936741` |
  | `dav2s_322x182.ncnn.bin` | `e2d5048f550aadfa3f02ca0e935a9c00d580ad09f6f9d0aaa129a3f6cc68c62b` |

  If a hash differs, setup says which file and stops (exit 2); the param files are text and diffable. They land in
  `~/.local/share/xrdome-tools/models/dav2s-ncnn/` and appear in `models` (status, licence, "generated by setup
  depth", never `--fetch`).
- Pre-processing (must match the export): RGB, bicubic resize to the model size (aspect not kept), /255, ImageNet
  mean (0.485, 0.456, 0.406) and std (0.229, 0.224, 0.225). Output: relative inverse depth (larger = nearer), H x W.
- `setup depth` refuses to finish if ncnn finds no Vulkan GPU (`ncnn.get_gpu_count() == 0`), unless `--cpu`; even
  then `depth` warns that CPU depth costs xrdome frames.

## 5. The depth cache (the contract between helper and player)

- Directory `~/.cache/xrdome-tools/depth/` (`$XDG_CACHE_HOME`). One file per photo:
  `<sha256 of the photo file's bytes>-dav2s-v1.pgm`. The player computes the same key.
- Format: binary PGM, `P5\n<W> <H>\n65535\n`, big-endian 16-bit samples, row 0 = top. No new C dependency.
- Content: the depth in the photo's **displayed** orientation (EXIF applied), robustly normalised: the 1st and 99th
  percentiles map to 0 and 65535, clipped; larger = nearer. Size: the model output upsampled bilinearly to the
  displayed photo size, capped at 2048 on the long side (keeping aspect).
- Written to `<name>.part` and renamed, so the player never reads half a file.

## 6. Stereo in the eye pass

- Only the flat branch (`u_proj == 1`) and only for a mono picture with a depth texture bound; everything else
  (VR180, SBS films, 2D with 3D off) is drawn exactly as today.
- Depth texture: GL_R16, GL_LINEAR, clamp to edge, same orientation convention as the video texture.
- Disparity of a picture point with normalised depth `z` in [0,1]: `s * (z - plane)` in picture-width units, with
  `s` the strength (default 2% of the width, range 0 to 5%) and `plane` the depth that sits on the screen (default
  0.5; nearer than the plane comes out of the screen). The left eye shifts by `+disp/2`, the right by `-disp/2`;
  `S` (swap eyes) inverts it, as it does for SBS films.
- Lookup: for an output point `u`, search N picture positions across the maximum disparity range along the row
  (start N = 16, refine with a linear step between the two closest) for the first one whose shifted position covers
  `u`, nearest first (parallax-occlusion style). Uncovered gaps take the background next to them. At strength 0 the
  result is the 2D picture exactly.
- Cost limit: with a static 3D photo on the Viture at 3840x1080, `--timing-log` must show < 1% of swap intervals
  over 20 ms across 3600 frames. If the search cannot meet it, lower N; if that looks bad, the fallback is a
  displaced grid mesh drawn per eye (new geometry path), decided with the owner.

## 7. Photo mode in the player

- An image file (ffprobe: a single-frame stream from an image demuxer such as `image2`, `*_pipe`, or `png`/`mjpeg`/
  `webp`/`heic` codecs with one frame) opens as a flat mono screen held in the room, at its displayed aspect, with
  mpv's `image-display-duration=inf`. Resume, seek and other time-based settings are off for images.
- A directory argument opens its images in name order; PageDown/PageUp go to the next/previous.
- Orientation: mpv must show the photo the way the helper's EXIF transpose does; if it does not, the player rotates
  the depth to match (checked in plan Task 3).
- Keys (free on `main` today): `D` 3D on/off, `-` / `=` strength, `,` / `.` screen plane further/nearer. Strength and
  plane are remembered per photo in filestate, like zoom.
- Depth sources, in order: the cache file; else ask the helper (`depth serve`, started by xrdome when it opens an
  image or directory and the socket is not there); else 2D with one `xrdome: note:` line. The photo shows in 2D at
  once and switches to 3D when depth arrives; the next and previous photos are prefetched.
- Helper protocol: Unix socket `$XDG_RUNTIME_DIR/xrdome-depth.sock`, one JSON object per line. Request
  `{"op": "photo", "path": "...", "prefetch": false}`, reply `{"ok": true, "depth": "<cache path>"}` or
  `{"ok": false, "error": "..."}`. Requests are served newest-first among non-prefetch ones.

## 8. Live video (after photo mode)

- Only flat mono films with 3D on. Each new frame: the GPU scales mpv's texture to 322x182 RGB into a small FBO;
  an asynchronous readback (PBO, read one frame later) copies it into a shared-memory ring the helper reads
  (`depth serve` gains a `video` op that maps it). The render loop never waits for the helper.
- The helper runs the 322x182 model at the film's rate up to 30 Hz and writes depth with the frame number back
  into the ring; xrdome uploads the newest into the depth texture and reuses section 6 unchanged. Depth may be
  1-2 frames old.
- Temporal smoothing: exponential moving average of depth (weight about 0.5) and of the percentile range, reset on a
  scene cut. Target flicker at or below 0.011 (the spike's flicker metric at 392x224).

## 9. Not in scope

Head-motion parallax (6DoF), SBS output files from photos (to3d covers files), Base/Large models, the NPU, ROCm.
