# xrdome status and handoff (2026-11-13)

Linux VR180 (side-by-side half-equirectangular) player for XR glasses with head tracking.
Repo: https://github.com/SDNick484/xrdome, `main` only. Design: `docs/superpowers/specs/2026-11-11-xrdome-design.md`.
Plan: `docs/superpowers/plans/2026-11-11-xrdome.md` (Tasks 0-9 done, Task 10 open).

## What works (verified on Viture Pro XR, ThinkPad P14s Gen 6 AMD, Gentoo, MATE/Xorg)
- Playback through libmpv (VA-API decode) into a per-eye OpenGL 3.3 pass, 3840x1080@60 SBS output.
- Head tracking from XRLinuxDriver over OpenTrack UDP (port 4242); recenter keys; seek; audio.
- Frame timing: 0.6% late frames with defaults (mpv profile `fast`, bicubic, 10-bit FBO + dither).
- Options and keys: see README.
- CI (GitHub Actions, Ubuntu 24.04) builds with `-Dwerror=true` and runs the whole suite, failing if either Xvfb smoke test skips.

## Per-file settings memory (branch `feature/per-file-settings`, head `6d93d87`)

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
- `no pose data` after a glasses reconnect: find out what restores it.
- The player does not switch the glasses to side-by-side mode itself; after a reconnect the
  owner has to do it by hand.
- Candidate next features, in suggested order: format auto-detection and flat projection with
  head tracking and zoom, per-eye HUD, Viture SDK backend, GTK launcher, Gentoo overlay ebuild.
