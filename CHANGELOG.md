# Changelog

All notable changes to xrdome are listed here, in the format of
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Versions follow `meson.build`.

## [Unreleased]

### Added
- `debian/` packaging (`packaging/debian/`): builds a `.deb` from the release tarball; built and
  installed on Ubuntu 24.04 and Debian 13, and by a new CI job on Ubuntu 24.04.
- `packaging/arch/.SRCINFO` and instructions for publishing the `PKGBUILD` on the AUR; CI checks
  that `.SRCINFO` matches the `PKGBUILD`.
- meson option `tests` (`auto` by default): cmocka is needed only for the unit tests, which are
  left out when it is missing (`-Dtests=enabled` requires it). The live Gentoo ebuild uses it.
- `--window-size WxH` sets the size of the `--windowed` desktop window, and
  `--window-eye left|right` fills it with one eye instead of both side by side, for demos and
  screen recordings without glasses. Either one implies `--windowed`.
- `tools/ot_sweep.py` can sweep pitch as well as yaw: `--pitch-amplitude DEG`,
  `--pitch-period SEC` and a constant `--pitch DEG`. The default is unchanged (no pitch).

### Fixed
- `xrdome-tools to3d` on a Radeon PyTorch's ROCm build has no code for (the Radeon 860M,
  gfx1152) no longer says `compute=rocm` and then crashes inside iw3 with `HIP error: invalid
  device function`. `to3d`, `setup to3d` and `doctor` now run a few real GPU kernels, and on a
  failure name the chip, the error and the fix: on the 860M,
  `HSA_OVERRIDE_GFX_VERSION=11.0.2` (verified; about 2.4 frames per second) or `--cpu`.

## [0.1.0] - 2026-11-19

First release: Linux only, built from the source tarball. "Verified" means the owner ran it on the
glasses named; everything else passed the unit and Xvfb smoke tests in CI and nothing more.
`docs/STATUS.md` has the numbers and the dates.

### Added

Playback and head tracking
- A player for VR180 (half-equirectangular, side by side) and for ordinary flat video on the
  Viture Pro XR and the XREAL Air 2 Pro (`--device xreal-air2pro`). Decode through libmpv with
  VA-API, a per-eye OpenGL pass, 3840x1080 at 60 Hz side-by-side output.
- Head tracking from XRLinuxDriver over OpenTrack UDP (port 4242). Verified on both glasses.
  Keys: `R` recentres yaw only, `Shift+R` recentres fully and levels a tilted picture.
- Frame timing on the Viture Pro XR with the defaults: 0.6% of swap intervals late on AC power.
  `--timing-log PATH` writes the log; `--require-hwdec` exits when decode is not zero-copy.
- Pose sources can be swapped; `--pose null` and `tools/ot_sweep.py` allow testing without glasses.

Flat video and format detection
- An ordinary film opens as a flat screen fixed in the room, bigger than the display, that you
  explore by turning your head. `[` `]` zoom, `Z` switches between enlarging the screen and
  magnifying, `L` locks the screen to the head, `C` curves it (`--screen-curve`).
- The format is detected with `ffprobe` (run as a child process; FFmpeg is never linked): flags,
  then the saved record, container metadata, file name, frame shape. `P` switches the projection
  and `M` the layout (2D, half and full side by side, half and full top-bottom).
  Verified: flat 1080p and 4K films, VR180 files, a full side-by-side 3D film.

Picture quality
- Enlarging filters (`F`), contrast-adaptive sharpening (`Shift+F`, `--sharpen`), debanding (`B`),
  mipmaps when the picture is shrunk, and mpv's own scalers for an enlarged flat screen
  (`--mpv-scale`).

Per-file settings and resume
- Zoom, layout, swap-eyes and the other settings are remembered for each file, found again after a
  rename (`--no-memory`, `--forget`). `--resume` continues from the last position.
  Verified on the Viture Pro XR for zoom; the other remembered settings are untested on glasses.

Glasses and displays
- `--sbs-switch auto` switches the glasses to side-by-side mode through XRLinuxDriver and back at
  exit. Verified on the XREAL Air 2 Pro. See the known limits for the Viture Pro XR.
- Running under XWayland (not measured on hardware: frame timing against Xorg is unknown).
- `--hwdec NAME` chooses the decoder (`vaapi` on Linux, `nvdec` or `auto-safe` for NVIDIA).
- `xrdome --version`.

Photos
- Image files and directories of images open as a flat screen; `PageDown` and `PageUp` step
  through a directory. EXIF orientation is applied by mpv.
- Depth 3D for photos: `xrdome-tools depth photo FILE|DIR` makes a depth map, or xrdome asks a
  helper (`xrdome-tools depth serve`) when it opens a photo that has none. `D` toggles it, `-` `=`
  set the strength and `,` `.` the screen plane. Needs `xrdome-tools setup depth` and a Vulkan GPU.

`xrdome-tools` (Python 3.11 or newer, standard library only; run from the source tree, it is not
installed by `meson install`)
- `doctor`, `transcode`, `models`, `setup`, `to3d` (2D to 3D, offline), `enhance` (frame
  interpolation and upscaling, 2D only) and `depth`. See `docs/tools.md`.

Packaging
- A Gentoo ebuild (`media-video/xrdome`, live and 0.1.0) and an Arch `PKGBUILD` in `packaging/`.
- A source tarball, `xrdome-0.1.0.tar.xz`, built by CI with `meson dist`.

### Experimental, not part of the release

- **macOS** and **Windows 11** builds, and the zips CI makes of them. They build and pass the unit
  tests in CI. Neither has run on glasses: the macOS bundle waits for a tester with an Apple
  Silicon Mac (issue #65), the Windows build for the owner's hardware run (issue #82).
- **Depth 3D for films** (`D` on a flat film). Built and tested with a fake helper; the
  hardware run (issue #74) has not been done, and the real model's flicker, helper rate and cost on
  the glasses are unmeasured.
- **`xrdome-pose-viture`** (`helpers/xrdome-pose-viture/`, MIT): head tracking and the SBS switch
  through VITURE's own SDK instead of XRLinuxDriver. Built only against the SDK headers, which
  are not in the repository. Its hardware checklist has not been run.

### Known limits

- **XRLinuxDriver 2.11.9 hangs on the Viture Pro XR's SBS switch** in 3 of 4 requests, with or
  without xrdome. The Viture also comes back in 1080p after every reconnect and then presents no
  frames. Until the driver is fixed, switch the Viture to SBS with its button.
- **`no pose data`** after a reconnect, seen after the driver crashed on connect. Restart the
  driver. The driver also aborted on two XREAL unplugs.
- **The XREAL Air 2 Pro must lie still while the driver calibrates** after a plug-in (about 16 s);
  moved during it, its yaw slides 3 to 17 degrees over the next half minute.
- **H.264 wider than 4096 pixels decodes in software on the AMD GPU** the player was developed on
  (VA-API stops at 4096). On battery that missed most vsyncs; measure on AC. HEVC and AV1 are not
  limited this way.
- 8K AV1 VR180 misses 2 to 3% of vsyncs at `--zoom 0.5`, and debanding saturates the GPU at 8K.
- Not seen on the glasses: subtitles, shimmer with and without mipmaps, a real half side-by-side
  film, top-bottom files, saved layout, swap-eyes, content FOV and convergence records, `--forget`,
  the Viture unplugged during playback, and the automatic 8-bit fallback.
- Photo depth: the default strength (2%) and plane (0.5) are provisional and have not been judged
  by eye on the glasses, nor has doubling at the edges of near objects. HEIC photos are not
  opened. The photo-mode polish still open (issue #71) is not in this release.
- Dimming and brightness keys wait for an XRLinuxDriver feature.
- The per-eye HUD is not built: `?` and `I` print to the terminal only.

### Requirements

- Linux with Xorg (XWayland works but is unmeasured), OpenGL 3.3, and glasses in side-by-side
  3840x1080 at 60 Hz mode (`--sbs-switch` or the glasses' own button).
- libmpv with VA-API, SDL2 (`X`, `opengl`), libepoxy, `ffprobe`. Tests need cmocka; the smoke
  tests need `xvfb-run`.
- XRLinuxDriver for head tracking; it is never linked or shipped.
- Licence: GPL-2.0-or-later, plus AMD's MIT licence for `sharpen.frag`.
