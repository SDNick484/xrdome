# xrdome status and handoff (2026-11-14)

Linux player for VR180 (half-equirectangular) and ordinary flat video on XR glasses with head
tracking. Repo: https://github.com/SDNick484/xrdome; work lands on `main` through pull requests.
Design: `docs/superpowers/specs/2026-11-11-xrdome-design.md`.
Plan: `docs/superpowers/plans/2026-11-11-xrdome.md` (Tasks 0-9 done, Task 10 open).

## What works (verified on Viture Pro XR, ThinkPad P14s Gen 6 AMD, Gentoo, MATE/Xorg)
- Playback through libmpv (VA-API decode) into a per-eye OpenGL 3.3 pass, 3840x1080@60 SBS output.
- Head tracking from XRLinuxDriver over OpenTrack UDP (port 4242); recenter keys; seek; audio.
- Frame timing: 0.6% late frames with defaults (mpv profile `fast`, bicubic, 10-bit FBO + dither).
- Options and keys: see README.
- CI (GitHub Actions, Ubuntu 24.04) builds with `-Dwerror=true` and runs the whole suite, failing if either Xvfb smoke test skips.

## Per-file settings memory (merged in PR #1, `main` at `3358de5`)

Remembers zoom, layout, swap-eyes, content FOV and convergence per file, keyed by file size and
a hash of its first and last 64 KiB; `--no-memory`, `--forget`.

Hardware run 2026-11-13: Viture Pro XR, ThinkPad P14s Gen 6 AMD, Gentoo (gcc 16.2.1, cmocka
2.0.2, SDL 2.32.10, libmpv 2.6.0), MATE/Xorg. The owner ran every command by hand and reported
what he saw; file `test2.mp4` (H.264 4320x2160, software decode).

Seen to work:
- Build with 0 compiler warnings; 13 of 13 unit tests pass as a normal user. Both smoke tests
  skipped on this machine (no Xvfb); they pass in the sandbox (Ubuntu 24.04, mpv 0.37).
- A key press is saved at quit: six presses of `[` gave `settings: saved zoom=0.56` and a record
  with `zoom = 0.5645` and the file's absolute path.
- Restored on reopen: the owner saw the video open at the zoom he had left; record unchanged.
- Survives a rename: `settings: restored zoom=0.56` under the new name, no save, one record.
- A flag overrides the record and is saved: `--zoom 1.0` gave `settings: saved zoom=1.00`.
- `--no-memory` printed no `settings:` line.
- `,` `.` `/` : no significant change seen.
- Timing, memory off against memory on, same file and zoom: 2105 against 2061 missed vsyncs of
  3600, then 2111 against 2070 (both pairs on battery, see Findings). On AC, with memory on:
  0 missed of 3600 (one run; that run had no pose data).

Not covered by this run: saving layout, swap-eyes, content FOV or convergence on the glasses
(only zoom was exercised); `--forget`; corrupt and newer records; the unwritable-state-directory
warning; save on unplug; the flat-video warning; the XREAL Air 2 Pro.

## Flat projection and format detection (PRs #11, #12, #13, #15 and #16)

Spec `docs/superpowers/specs/2026-11-13-flat-projection-design.md`, plan
`docs/superpowers/plans/2026-11-13-flat-projection.md` (Tasks 1-9 done; hardware run done,
below). An ordinary film opens as a flat screen fixed in the room at 1.5 times the display, a
VR180 file opens as before, both without flags; flat 2D, half and full SBS and top-bottom; zoom
modes, screen lock, mipmaps while shrinking, subtitles in flat 2D, keys `P` `Z` `L` `?` `I` and
Up/Down, the new `M` cycles; projection, flat zoom, packing and zoom mode remembered per file.

**Run on the glasses on 2026-11-14 (spec 8.4, all eight steps); see the hardware run below.**

Decisions (beyond the spec's own calls):
- `ffprobe` runs once before the window opens; a failed probe and the fallback it causes are one
  `xrdome: note:` line.
- Every record states the projection in effect at quit, so `proj` is always written and is
  always listed in `settings: saved` and `settings: restored` (an existing VR180 record now
  restores as `... proj=hequirect`).
- A layout that `P` works out again from the frame shape is not saved as a change; one chosen
  with `M` or `S`, or from a flag, record, metadata or file name, is kept by `P`.
- The aspect warning is checked when the video size becomes known and when `P` switches to
  VR180, not on `M`. It names top-bottom as well as side by side and mono.
- Subtitles follow mpv's own `sub-visibility` (so `--mpv-opt sub-visibility=no` still hides
  them); xrdome only hides them in 3D layouts and VR180, which changes VR180 behaviour: an
  `.srt` next to a VR180 file is no longer drawn.
- `--mipmap-check` (development) now fails a draw that wants mipmaps and finds no chain for its
  frame; `--mipmap-force` stays as a development flag.
- `?` is read as Shift+/ (and SDL's `?` key where a layout has one).

What the sandbox verified (unit tests and both Xvfb smoke tests on the owner's laptop, by the
implementing session: Gentoo, libmpv 2.6.0 / mpv 0.42, FFmpeg 9.0.2, software GL; CI on Ubuntu
24.04 with mpv 0.37 runs the same suite):
- The 2:1 VR180 pattern with no flags: `detect: proj=hequirect (frame shape) layout=sbs-lr
  (frame shape)`, and the existing frame checks pass unchanged.
- A 16:9 pattern opens flat 2D; at zoom 1.0 it fills the eye edge to edge, at 0.5 the corners are
  black; no `xrdome:` line other than the headless hwdec warning.
- Half SBS by file name (`clip.HSBS.mp4`) and full top-bottom by frame shape: each eye shows only
  its own half. A stereo-tagged Matroska file: `layout=sbs-lr (metadata)` (FFmpeg 9.0.2).
- Subtitles from an `.srt` are drawn in flat 2D and not in half SBS.
- `ffprobe` missing: one note, the 2:1 pattern still plays as VR180 by fallback.
- A file named `-weird name.mp4` is probed and plays.
- A flat record round trip: `settings: saved proj=flat flat-zoom=2.00`, then
  `settings: restored proj=flat flat-zoom=2.00` and `detect: proj=flat (record)`.
- Screen lock under the tracking sweep: head-locked, a 20 degree head turn leaves the frame
  unchanged; fixed, it changes it.
- Mipmaps are used for a shrunk flat picture, and `--mipmap-check` passes, including across a
  mid-file size change.
- Keys `P` `M` `Z` `L` `?` `I` were driven by hand under Xvfb with synthetic key events (not in
  the test suite): their output lines as in the README; `P` on a 2:1 file with no hint gave
  `layout: mono` and saved only `proj=flat`.

Hardware run 2026-11-13 evening to 2026-11-14 (issue #9): Viture Pro XR in SBS, ThinkPad P14s
Gen 6 AMD on AC power (governor `performance`), Gentoo, libmpv 2.6.0, FFmpeg 9.0.2, XRLinuxDriver
over OpenTrack UDP. Mipmap cost was measured on `main` at `8c20f05` (PRs #14 and #15); the rest on
`main` at `53ab2c1` (PR #16). The owner ran the commands and reported what was seen; `detect:` lines
for `test.mkv` and `test2.mp4` with `--no-memory` were captured by the session on the same laptop.

Seen to work (spec 8.4 step numbers):
1. Build with 0 compiler warnings (owner). The `meson test` summary line was not reported.
2. VR180 regression: `test.mkv` (AV1 2880x1440) and `test2.mp4` (H.264 4320x2160) with
   `--no-memory` print `detect: proj=hequirect (frame shape) layout=sbs-lr (frame shape)`. On
   `8c20f05` the owner saw an 8K VR180 file (AV1 7680x3840) look right: eyes, centre seam, depth
   and head tracking ("looked pretty good"). The look of `test.mkv` and `test2.mp4` on `53ab2c1`
   was not asked about.
3. Flat films with no flags open flat at 1.5: a 1080p AV1 film printed `projection: flat (frame
   shape)`, `layout: mono (frame shape)`, VA-API decode, 0 missed vsyncs in 10754 frames with keys
   pressed; a 4K AV1 film printed `detect: proj=flat (frame shape) layout=mono (frame shape)` and
   `zoom: 1.50 ... screen 54.5 x 32.3 degrees`. On the 4K film (owner): edges straight; the screen
   stays put when the head turns; `[` `]` resize it; `L` locks it to the head and unlocks; `R`
   recentres; `Z` switches mode, with a bigger difference when already zoomed in than zoomed out.
   The owner found bicubic "does seem to help a bit" on the 1080p film.
4. Mipmap cost, 4K AV1 film, `--no-memory --zoom 1.0 --frames 3600`, no keys: `--mipmap off` 8
   missed vsyncs of 3600 (0.22%), `--mipmap on` 13 (0.36%): within 0.5 percentage points; the
   0-missed target was met by neither. On `8c20f05` with the 8K VR180 file (`--zoom 0.5`,
   `--mipmap-force` against none): 113 (3.1%, one `Audio device underrun detected`) against 85
   (2.4%), so making the chain every frame costs nothing measurable at 7680x3840. Mipmaps came on
   by themselves for the 4K film at its default zoom (`filter: trilinear, mipmaps on`) and stayed
   off for the 1080p film at zoom 1.40.
5. Half SBS: a synthetic clip (1920x1080, two 960-wide eyes, boxes labelled NEAR, SCREEN and FAR at
   crossed, zero and uncrossed disparity, each half labelled with its eye). Its name,
   `depth_test_3d.mp4`, matched the `3d` hint: `detect: proj=flat (frame shape) layout=sbs-lr (file
   name "3d") packing=half (auto)`, so it opened in 3D without `M`. Owner: NEAR closest and FAR
   furthest; each eye sees its own label; `S` inverts the depth; `M` switches layouts.
6. Records: the 4K film reopened with `detect: proj=flat (record)` and its zoom restored;
   `test.mkv`, whose record was written before PR #16, reopened with `detect: proj=hequirect
   (record)` and its zoom restored (owner: `settings: restored` lines correct).
7. `ffprobe -show_streams` with the laptop's FFmpeg 9.0.2 on files made again from the fixture
   README's recipes (Matroska `stereo_mode=left_right`; MP4 with an injected `sv3d` box): byte for
   byte the same as `sbs_mkv.txt` and `spherical_mp4.txt`. The owner's own VR180 files (`test.mkv`,
   `test2.mp4` and four downloaded 8K files) carry no stereo or spherical side data, so no file from a
   camera or a site was compared.
8. `?` and `I` print their lines; Up and Down skip forward and back (owner).

Not covered by this run: shimmer (step 4: at `--zoom 0.5` the owner saw no clear difference between
`--mipmap off` and `on` on the 4K film, which may lack fine high-contrast detail; neither confirmed
nor contradicted); a real half-SBS film (only the synthetic clip); `M` cycling into half SBS from
mono (the clip opened in 3D by name); subtitles on the glasses; top-bottom and full SBS; rotated
video; the XREAL Air 2 Pro.

Still unverified (spec section 11):
- What `ffprobe` prints for Spherical Mapping side data from a real camera or site file, and on
  versions other than 9.0.2 (9.0.2 on a synthetic `sv3d` box matches the fixtures); MP4 stereo
  metadata on FFmpeg 9.0.2 beyond the remux in the fixtures.
- Subtitles drawn by mpv's normal render path on the Radeon (seen only with software GL and
  `gpu-dumb-mode`).
- That FFmpeg does not decode both eyes of MVC (not relied on; MVC is a non-goal).
- Rotated video (phone footage with a rotation tag): expected to work through mpv, not tested.

## Decisions
- Architecture B: one C11 process, libmpv render API + own GL projection pass. meson, cmocka.
- Pose sources are pluggable; XRLinuxDriver is reached over UDP only. Never link or ship
  `libglasses.so` or XRLinuxDriver source. A Viture SDK backend, if added, uses dlopen.
- mpv default profile is `fast`; the default mpv render path saturated the integrated GPU.
- Convergence is demoted to the `--convergence` flag (still remembered per file); the `,` `.` and
  `/` keys are removed. Reason: on the Viture Pro XR up to 2 deg made no significant difference
  and 5 deg put the scene out of focus, while zooming out is what fuses close subjects. The flag
  stays because the finding is one viewer on one pair of glasses and the XREAL Air 2 Pro is untested.
- Source bitrate dominates picture quality (20 Mbit/s H.264 looked far better than 2.5 Mbit/s AV1).

## Findings from hardware testing
- Close subjects are hard to fuse at zoom 1 and above. Zooming out fixes it. Convergence does not:
  up to 2.0 deg made no significant difference, and 5 deg put the scene out of focus until zoomed
  far out. Zoom is the working control for close scenes; convergence is not the fix.
- H.264 hardware decode on this GPU stops at 4096 wide; 4320x2160 H.264 decodes in software
  (1.3% late frames). HEVC and AV1 are not limited this way.
- Decode ceilings: 7680x2880 AV1 47 fps, HEVC 132 fps.
- Power matters for software decode (2026-11-13): on battery with the `balanced` platform
  profile, 4320x2160 H.264 missed 57 to 74% of vsyncs over 3600 frames, on `main` (2670) and on
  the per-file branch (2061 to 2111) alike. On AC, same profile, same file: 0 missed of 3600.
  The `performance` profile and battery-with-performance were not tried.
- The Viture Pro XR does not keep side-by-side mode across a reconnect; it comes back in 1080p
  every time (owner's report). With the glasses in 1080p the player presents 0 frames.
- After that reconnect xrdome printed `no pose data` on every run. Seen, not explained: the
  driver was not restarted to test whether that brings the pose back.
- 2026-11-13 evening, `no pose data` again: no `xrDriver` process was running, and its
  `driver.log` ended at the glasses' connect with `[E][libglasses] m_StateCallback is not
  initialized` and `Segmentation fault occurred`. The build from before the flat feature
  (`505aa1f`) showed the same, so it is not xrdome. Restarting the driver brought the pose back.
  Seen once; whether the earlier case was the same crash is not known.
- 8K AV1 VR180 (7680x3840, VA-API) on AC misses 2 to 3% of vsyncs at `--zoom 0.5`, with or
  without mipmaps (one pair each, 2026-11-13). 4K AV1 flat at `--zoom 1.0`: 0.2 to 0.4%.
- Recentering and head tilt (owner's report): the video plays at whatever angle the head had at
  start, and `R` centres it without correcting the tilt. This matches the code: the first pose
  becomes the full reference, and `R` is yaw only, so it keeps that reference's roll.
  `Shift+R` (full recenter with the head level) should correct it; the owner did not report
  trying it. A gravity-level start or a "level" key would need the driver's world frame
  calibrated (see README, Tracking notes).

## Open
- Task 10: XREAL Air 2 Pro check (`--device xreal-air2pro`), yaw drift over 5 minutes on both
  glasses, EDID/`xrandr --props` capture, README hardware notes.
- Untested: unplug behaviour during playback (a disconnect between runs happened on 2026-11-13,
  see Findings), automatic 8-bit FBO fallback.
- Unanalysed: the remaining 0.6-1.3% late frames.
- LICENSE not chosen. Depends on whether the installed mpv is a GPL build:
  `grep -n -i gpl "$(equery which media-video/mpv)"`.
- Per-file settings: the items listed as not covered above.
- Recenter: confirm `Shift+R` levels a tilted picture; decide on a gravity-level start or a
  "level" key.
- `no pose data` after a glasses reconnect: once (2026-11-13) it was XRLinuxDriver crashing at
  connect, and restarting the driver restored it; the earlier case was not checked.
- The player does not switch the glasses to side-by-side mode itself; after a reconnect the
  owner has to do it by hand.
- Flat projection and format detection, after the hardware run: shimmer with and without
  mipmaps on content with fine detail; a real half-SBS film, and `M` into half SBS from mono;
  subtitles on the glasses; top-bottom and full SBS if such files turn up.
- Candidate next features, in suggested order: per-eye HUD (takes over `?` and `I`, which print
  to the terminal for now), Viture SDK backend, GTK launcher, Gentoo overlay ebuild.
