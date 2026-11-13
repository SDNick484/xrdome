# xrdome status and handoff (2026-11-12)

Linux VR180 (side-by-side half-equirectangular) player for XR glasses with head tracking.
Repo: https://github.com/SDNick484/xrdome, `main` only. Design: `docs/superpowers/specs/2026-11-11-xrdome-design.md`.
Plan: `docs/superpowers/plans/2026-11-11-xrdome.md` (Tasks 0-9 done, Task 10 open).

## What works (verified on Viture Pro XR, ThinkPad P14s Gen 6 AMD, Gentoo, MATE/Xorg)
- Playback through libmpv (VA-API decode) into a per-eye OpenGL 3.3 pass, 3840x1080@60 SBS output.
- Head tracking from XRLinuxDriver over OpenTrack UDP (port 4242); recenter keys; seek; audio.
- Frame timing: 0.6% late frames with defaults (mpv profile `fast`, bicubic, 10-bit FBO + dither).
- Options and keys: see README.

## Decisions
- Architecture B: one C11 process, libmpv render API + own GL projection pass. meson, cmocka.
- Pose sources are pluggable; XRLinuxDriver is reached over UDP only. Never link or ship
  `libglasses.so` or XRLinuxDriver source. A Viture SDK backend, if added, uses dlopen.
- mpv default profile is `fast`; the default mpv render path saturated the integrated GPU.
- Source bitrate dominates picture quality (20 Mbit/s H.264 looked far better than 2.5 Mbit/s AV1).

## Findings from hardware testing
- Close subjects are hard to fuse at zoom 1 and above. Zooming out fixes it. Convergence does not:
  up to 2.0 deg made no significant difference, and 5 deg put the scene out of focus until zoomed
  far out. Zoom is the working control for close scenes; convergence is not the fix.
- H.264 hardware decode on this GPU stops at 4096 wide; 4320x2160 H.264 decodes in software
  (1.3% late frames). HEVC and AV1 are not limited this way.
- Decode ceilings: 7680x2880 AV1 47 fps, HEVC 132 fps.

## Open
- Task 10: XREAL Air 2 Pro check (`--device xreal-air2pro`), yaw drift over 5 minutes on both
  glasses, EDID/`xrandr --props` capture, README hardware notes.
- Untested: unplug behaviour, automatic 8-bit FBO fallback.
- Unanalysed: the remaining 0.6-1.3% late frames.
- LICENSE not chosen. Depends on whether the installed mpv is a GPL build:
  `grep -n -i gpl "$(equery which media-video/mpv)"`.
- Candidate next features, in suggested order: per-file settings memory (zoom, content FOV,
  convergence), format auto-detection, per-eye HUD, Viture SDK backend, GTK launcher,
  Gentoo overlay ebuild.
- Convergence keys: decide whether to keep, widen, or demote given the finding above.
