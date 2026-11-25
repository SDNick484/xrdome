# xrdome status and handoff (2026-11-14)

Linux player for VR180 (half-equirectangular) and ordinary flat video on XR glasses with head
tracking. Repo: https://github.com/SDNick484/xrdome; work lands on `main` through pull requests.
Design: `docs/superpowers/specs/2026-11-11-xrdome-design.md`.
Plan: `docs/superpowers/plans/2026-11-11-xrdome.md` (Tasks 0-10 done; Task 10's hardware part on
2026-11-14, see Hardware run 2026-11-14 below).

## What works (verified on Viture Pro XR, ThinkPad P14s Gen 6 AMD, Gentoo, MATE/Xorg)
- Playback through libmpv (VA-API decode) into a per-eye OpenGL 3.3 pass, 3840x1080@60 SBS output.
- Head tracking from XRLinuxDriver over OpenTrack UDP (port 4242); recenter keys; seek; audio.
- Frame timing: 0.6% late frames with defaults (mpv profile `fast`, bicubic, 10-bit FBO + dither).
- Options and keys: see `docs/usage.md`.
- CI (GitHub Actions) builds with `-Dwerror=true` and runs the whole suite, failing if any of the seven
  Xvfb smoke tests (`smoke`, `smoke_tracking`, `smoke_sbs`, `smoke_photos`, `smoke_depth`,
  `smoke_depth_serve`, `smoke_depth_video`) skips, in two jobs: Ubuntu 24.04 (mpv 0.37)
  and an `archlinux:latest` container (gcc 16.2.1, mpv 0.41 / libmpv 2.5.0, `sdl2-compat` 2.32.74 on
  SDL3, Mesa 26.2 on 2026-11-14). Arch's SDL adds `-D_GNU_SOURCE=1` to the cflags, and its
  `xvfb-run` (T2 SDE's copy of an old Debian script) sleeps 3 s per start, so `smoke` takes about 315 s there; that job
  runs with `--timeout-multiplier 2`.

## Summary of the hardware runs (moved from the README, 2026-11-21)

Status: plays a side-by-side VR180 file on the glasses with head tracking from
XRLinuxDriver (OpenTrack UDP), keyboard controls, a frame-timing log and per-file settings
memory. On the Viture Pro XR, with the default mpv profile, bicubic sampling and a 10-bit
texture, 0.6% of swap intervals were late (see [Performance notes](measurements.md#performance-notes)). Per-file
settings memory was run on the Viture Pro XR on 2026-11-13: saving a zoom change, restoring it,
and keeping it across a rename were seen to work; the other remembered settings and `--forget`
have only passed the unit and Xvfb smoke tests. On the XREAL Air 2 Pro (2026-11-14) VR180 and flat
files played with head tracking much as on the Viture, with and without `--device xreal-air2pro`. See the sections below, `docs/superpowers/specs/2026-11-11-xrdome-design.md` and
`docs/superpowers/plans/2026-11-11-xrdome.md`.

Flat video and format detection (an ordinary film opens as a flat screen fixed in the room, a
VR180 file opens as before, both without flags; see [Flat video](flat-video.md#flat-video) and
[Format detection](detection.md#format-detection)) were run on the Viture Pro XR on 2026-11-14: flat 1080p
and 4K films opened flat at 1.5 by frame shape, and the owner's VR180 files as VR180; the screen
stayed put when the head turned, `[` `]` `Z` `L` `R` `?` `I` and Up/Down worked; half SBS (a test
clip) showed the right depth and `S` inverted it; flat and VR180 records restored; mipmaps cost no
more than 0.14 percentage points of missed vsyncs. Not seen on the glasses: whether mipmaps reduce
shimmer, a real half-SBS film, subtitles, top-bottom and full SBS. The sections below have the
details.

A second run on both pairs of glasses (2026-11-14, afternoon and evening) covered the newer
features. The automatic switch to SBS works on the XREAL Air 2 Pro (about 2.5 s, and back at exit);
on the Viture Pro XR it worked once, then XRLinuxDriver 2.11.9 hung on the switch request (see
[Switching to SBS](setup.md#switching-to-sbs)). Resume worked on an 81-minute film and an 8K file;
debanding costs little on a 4K film but saturates the GPU at 8K; mpv's scalers cost almost nothing
and look slightly better at most; the curved screen was liked, at R = 1; a full side-by-side 3D
film played in 3D without flags. Tracking: the Viture drifted 0.02 degrees in 5 minutes; the XREAL
must lie still while the driver calibrates it (see [Tracking notes](measurements.md#tracking-notes)). Still not
seen on the glasses: subtitles, shimmer, a real half-SBS film. The sections below have the numbers.

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

## Packaging and licence (issue #21)

- Licence: GPL-2.0-or-later (owner: "match mpv", 2026-11-14). mpv's `Copyright` file says mpv as a
  whole is GPLv2+ by default and LGPLv2.1+ only when built with `-Dgpl=false`; its `meson.build`
  has `license: ['GPL2+', 'LGPL2.1+']`. The owner's mpv is a GPL build and xrdome links libmpv,
  so xrdome is GPLv2+. `LICENSE` is the GPLv2 text (Gentoo's `licenses/GPL-2`, which differs from
  mpv's `LICENSE.GPL` only in form-feed characters; gnu.org was unreachable from the sandbox).
  `license : 'GPL-2.0-or-later'` is in `project()`; no per-file headers.
- `packaging/gentoo/media-video/xrdome/` has `xrdome-9999.ebuild` (EAPI 8, `git-r3 meson python-any-r1`, no
  KEYWORDS, `IUSE=test`) and `metadata.xml`; `packaging/gentoo/README.md` says how to add it to a
  local overlay. In-repo because the ebuild is tiny (owner, 2026-11-14). cmocka is a build
  dependency even without the `test` flag because `meson.build` requires it.
- Verified on the laptop: `ebuild manifest` parses it in a throwaway overlay under the scratchpad,
  `emerge --pretend media-video/xrdome::local` resolves it, `meson install --prefix=/usr --destdir` (what the meson eclass uses) puts
  `usr/bin/xrdome` and nothing else in the destdir.
- Not verified: an actual `emerge` build (needs root), `pkgcheck scan` (not installed). The owner
  runs the install commands from the README.

## Debanding (`--deband`, `B`)
- `--deband on|off` and `[general] deband = yes|no` (default off) set mpv's `deband` at open; `B`
  toggles it through `mpv_set_property` and prints `deband: on|off`; `I` prints
  `info: deband: on|off`, read back from mpv.
- Decisions: not remembered per file (a viewing preference, like `F` and the sharpness choices,
  not a property of the file); strength is left to `--mpv-opt deband-*`; `--mpv-opt deband=...`
  is applied after `--deband` and wins; the key reads mpv's live value, so it follows it. The ini
  also takes `on|off` and the flag only `on|off` (the issue writes `deband = yes`).
- Verified in the sandbox: unit tests for flag, ini, precedence and errors; smoke B1 to B4 on
  Mesa llvmpipe: a banded 8-bit gradient (32 bands, 4 levels apart) shows about 90 distinct
  levels across a row with `--deband on` against 32 with `off` (mpv's deband adds its grain; the
  smoothing of the steps is small at this size). llvmpipe only honours `deband` outside
  `gpu-dumb-mode`, so B1 to B3 pass `--mpv-opt gpu-dumb-mode=no`; with xrdome's usual
  `gpu-dumb-mode` on llvmpipe the frames with deband on and off are identical.
- Run on the glasses on 2026-11-14 (see Hardware run 2026-11-14): `B` and `I` work; 4K film
  0.03% -> 0.75% missed with deband on, 8K VR180 1.6% -> 28%; the owner saw little difference on a
  dark low-bitrate frame. Off by default stays.

## Resume from the last position (`--resume`, issue #19)

Opt-in: `--resume` / `--no-resume` and `[general] resume = yes|no` (default no). New record key
`position` (seconds, three decimals), written at quit only while resume is on; without resume a
record is what it was before (and a `position` already in it is kept, not used).

Decisions:
- Not saved or resumed in the first 30 s, nor in the last `min(5% of the file, 60 s)`
  (`t >= 30 and t < d - min(0.05 d, 60)`, `xrd_resume_check`). Quitting outside that range removes
  the record's `position`, so a finished film starts over.
- Precedence: `--no-memory` beats everything (nothing read or written); `--no-resume` beats an ini
  `resume = yes`; of `--resume` and `--no-resume` the last wins; an explicit
  `--mpv-opt start=...` wins over the record's position (which is then not printed as restored),
  and the position at exit is still saved.
- `position` is not in the table that `apply`, `collect` and `equal` walk, so `settings: restored`
  cannot mention it unless resume used it; `main.c` adds it by hand.
- The start is mpv's `start` option, put before the user's `--mpv-opt`s. mpv only knows the duration
  after the file loads, so for the past-the-end case the player checks on `FILE_LOADED`, prints one
  `xrdome: note:` and seeks to 0. The `restored` line is printed before that, so it still shows
  the position.
- Duration or position unknown at quit (mpv returns none): the record's position is left as loaded.
- Played to the end of the file: mpv unloads the file at its end, so position and duration are
  gone at quit. The player keeps the duration from `FILE_LOADED` and, after an end-of-file
  `END_FILE`, reports it as the position, so the near-end rule removes the position. Found on
  the glasses on 2026-11-14 (a film fast-forwarded to its end kept its old position); smoke M20.

Verified in the sandbox (Ubuntu 24.04-like, mpv, software GL, Xvfb): unit tests for the record's
parse and format, the near-start and near-end rule, and the precedence; smoke cases M13 to M19
(no position without `--resume`; saved and restored with it, and playback begins at the saved
position; `--no-resume` and `--no-memory` leave the record byte-identical; an explicit start wins;
quitting in the first 30 s drops the position; a position past the end gives one note and starts at
0).

Run on the glasses on 2026-11-14 (see Hardware run 2026-11-14): saved at `Q` and resumed on an
81-minute film and an 8K VR180 file (first frame 0.6 to 1.0 s after start, 0.3 to 0.5 s more than
from the start of the file); the near-end rule held for a `Q` in the last minute. A film played to
its end kept its old position: fixed in PR #39. Not tried: the near-start rule, the window close.

## Dimming and brightness on the Viture Pro XR (issue #22, research only, 2026-11-14)
Question: can xrdome set the electrochromic dimming and the display brightness while XRLinuxDriver
runs? Answer: not today, and not safely from a second process. Decision (b): the controls belong in
the driver; an upstream feature request is drafted in the description of the PR that closes
issue #22, for the owner to file. Nothing was sent to the glasses or the driver; all of this is
from reading source, headers, docs and the installed binaries.

Findings (XRLinuxDriver at `da303d2`, 2026-11-05, the commit the spec was checked against; the
Viture SDK 2.4.0 headers it bundles, `include/sdks/viture_version.h`, brought in by its PR #138):
- The SDK has both controls. `viture_protocol_public.h`: `xr_device_provider_set_film_mode(handle,
  float voltage)` is the electrochromic film ("controls the tint level of the glasses lenses"),
  binary on Gen1 (0.0 off, any non-zero on; One and Pro: range [0, 1]; Pro 2 has no film; Beast
  [0, 8]). `xr_device_provider_set_brightness_level(handle, int level)` is display brightness,
  [0, 8] on the Pro ([0, 6] on the One). `set_duty_cycle` (0 to 100) is a lower-level brightness
  that Viture's page says most applications should not use. Viture's SDK page
  (https://www.viture.com/developer/glasses-sdk/glasses) gives the same ranges. That the Pro XR
  is the "Pro" row is an inference from the driver's market names, not checked on the glasses.
- The driver uses neither. `src/devices/viture.c` calls no brightness, film or duty-cycle
  function, and the installed `~/xr_driver/bin/xrDriver` imports none of them (`strings`).
  `read_control_flags` in `src/state.c` knows `recenter_screen`, `recalibrate`, `sbs_mode`,
  `force_quit` and `request_features`; plugins add `refresh_device_license` and the smooth-follow
  keys. `device_driver_type` (`include/devices.h`) has SBS get/set only. So option (a), a control
  file request the driver already supports, does not exist. The SDK's state-change callback
  (which reports brightness and film changes) is not registered: the call is commented out
  (`viture.c`, `// viture_register_state_callback_locked();`).
- `libGlassSDK.so` is Rokid's SDK, not Viture's (`CMakeLists.txt` pairs it with
  `src/devices/rokid.c`; `include/sdks/rokid.h` declares its `SetBrightness(handle, channel,
  level)`). Its brightness calls do not apply to the Viture.
- Controls and the IMU share one connection. `libglasses.so` (installed copy) has a statically
  built libusb and opens the glasses' "MCU" and "IMU" USB interfaces through usbfs (its strings:
  `No MCU device found`, `No IMU device found`, `libusb_claim_interface`). Viture's page says one
  pair of glasses exposes several USB interfaces (IMU, MCU, audio, camera) and that a handle
  "represents one open connection to one physical device". Display mode, brightness and film are
  MCU commands sent through the provider the driver already holds.
- A second process cannot get those interfaces. While one process has claimed an interface
  through usbfs, another's `libusb_claim_interface` gets `LIBUSB_ERROR_BUSY`, and libusb refuses
  to detach an interface whose driver is `usbfs` (libusb `libusb/os/linux_usbfs.c`,
  `claim_interface` and `op_detach_kernel_driver`, master `d3937ff`). What `create` and
  `initialize` do on the device before that fails is not documented (`initialize` can fail with
  `VITURE_GLASSES_ERROR_SERIAL_FETCH`, so it talks to the glasses), and the SDK documents no
  shared access. So a `dlopen` backend in xrdome (option c) would at best fail with BUSY and at
  worst disturb the driver's IMU stream. This confirms the spec's note (section 4.1) that only
  one process can own the glasses.
- The driver's SBS switch is the model for an upstream change: `handle_control_flags_update`
  (`src/driver.c`) calls `device_set_sbs_mode_func`, which takes `viture_connection_mutex` and
  calls the SDK on the driver's own provider. It stops the IMU stream first because
  `switch_dimension` "fails if we're still streaming IMU data"; whether brightness and film
  commands need the same is not documented.
- No request for this exists upstream: searching wheaney/XRLinuxDriver issues and PRs for
  brightness, dimming, dim, film mode and electrochromic found nothing relevant (2026-11-14).

Why (b), not (c) or (d): the safe path is one owner of the USB connection, and the driver is that
owner. The change upstream is small and follows an existing pattern (the SBS request). Option (d)
would leave a reachable fix unasked. If the request lands, xrdome's side is a few one-shot control
file writes through issue #18's writer: `D` toggles dimming, `Shift+[` / `Shift+]` step brightness
(keys reserved, not bound), stepping from the level the driver reports in its state file.

Not checked, and needs the glasses or the maintainer: which SDK device type the Pro XR reports
(Gen1 is assumed from the market name); whether film and brightness commands work while the IMU
streams; whether the glasses keep the levels across a reconnect.

## Curved screen (issue #20)

`--screen-curve off|R`, `[general] screen_curve` and the `C` key bend the flat screen into part
of an upright cylinder, so the edges of a large screen are as far away as its centre (like
XRLinuxDriver's `--curved-display`). Default `off`, which draws exactly the plane as before.

Decisions:
- R is the cylinder's radius in screen distances, 1 to 10. The cylinder touches the flat screen
  along its centre column, so its axis is on the line through the eye and the screen's centre,
  R - 1 behind the eye: R = 1 puts the axis through the eye and every column at the same
  distance, and a larger R bends less, towards the plane. A radius rather than an arc angle
  because the arc's angle then follows from the zoom (the arc keeps the flat width), and one
  number keeps the same shape at every zoom; radii under 1 (the eye outside the cylinder) are
  refused.
- The arc is as long as the flat screen is wide, and the height is unchanged: the same picture
  at the same zoom, with u along the arc and v by height. Columns stay straight; the top and
  bottom edges bow outwards. Seen from the eye a curved screen is a little wider than the flat
  one: at zoom 1 and R = 1 a 16:9 screen spans 39.3 degrees, not 37.9, so its sides sit just
  outside the view (`I` prints the angle between the arc's ends).
- REVERSED 2026-11-14 (issue #41), kept for the record: "Magnify mode draws the screen flat:
  there the screen stays at its zoom-1 size, which fits the display, and zoom narrows the view, so
  there is no large screen to bend. With a curve on, `Z` therefore also switches between curved
  and flat, and the two modes no longer show the same picture away from the centre." On the
  hardware run of 2026-11-14 (Viture Pro XR, 1080p film, zoom 1.5 and 2.5) the owner found the
  curved screen worked well, preferred R = 1 to R = 2, and named one thing that did not fit: "the
  only behavior that didn't fit well was Z which flattened the curved image". The owner decided to
  reverse the decision.
- Since then (issue #41) magnify mode curves its zoom-1 screen with the same R: the arc keeps the
  zoom-1 screen's width, the height is unchanged, and zoom narrows the view onto it as before.
  `C` toggles the curve in both modes and prints the same in both (the `(flat in magnify mode)`
  suffix is gone); `Z` never changes the curve. Only `xrd_controls_screen_curve` changed: the
  mapping (`xrd_flat_curve_dir_to_uv`, the shader) already takes the screen's half-extents, which
  in magnify mode are the zoom-1 screen's. At the view centre, straight ahead, both modes show the
  picture's centre at the same scale, so `Z` does not jump; away from it they differ: screen
  mode's arc is zoom times longer and bends more across the view, so at the view's edges it shows
  a little less of the picture (at zoom 2.5 with R = 1, about 3% less than magnify mode, whose
  view lies on the nearly flat middle of the zoom-1 arc). `I` prints, in magnify mode, the
  zoom-1 screen's angle, the same at every zoom.
- Head-locked screens curve the same way (the mapping is in head space when locked).
- Nothing beside or behind the viewer is drawn, as for the plane: an arc reaching past 90 degrees
  stops there (the widest, zoom 4 with R = 1, reaches 79 degrees).
- `xrd_minify_ratio` is unchanged: the cylinder touches the plane at the centre, where u and v
  change at the plane's rates (a unit test measures them on the curved mapping), and they only
  fall away from the centre, so the centre stays the worst case for the mipmap decision.
  Re-checked for the curved zoom-1 screen of magnify mode (issue #41): the same holds there (a
  unit test measures the ratio at the centre on the curved mapping and compares it with
  `xrd_minify_ratio`'s, radii 1, 2.5 and 10, zooms 0.5 and 2.5), so it stays unchanged.
- The eye-edge and mipmap clamp margins act on uv after the mapping, so the curved mapping needs
  none of its own; the seam and trilinear eye-separation render tests run again on a curved
  screen and pass. The seam test looks at a 20x magnified view, so it is the curved magnify
  screen's check too; since issue #41 it runs at radii 1, 2.5 and 10.
- `C` is not remembered per file, like the screen lock: it depends on the viewer, not the film.
  With `--screen-curve off`, `C` curves with R = 1.

What the sandbox verified (unit tests and both Xvfb smoke tests, software GL, this worktree):
- `xrd_flat_curve_dir_to_uv` (the C mirror): centre, the arc's ends, top and bottom, beside and
  behind, junk input; `off` gives the plane's result bit for bit.
- The shader against the C mirror on every pixel of both eyes, for radii 0, 1, 2.5 and 10 at two
  zooms and four head poses (a texture that encodes its own uv).
- A dumped flat2d frame at `--zoom 0.9 --screen-curve 1.0`: the side borders each in one column,
  the top and bottom borders 6 rows nearer the frame's edge at the sides than at the centre;
  `off` gives straight borders and the frame of a run without the flag.
- Issue #41, magnify mode curved: `xrd_controls_screen_curve` returns the radius in magnify mode
  (also with `--zoom-mode magnify` from the start and head-locked); `Z` keeps the curve and `C`
  flips it in both modes. The C mirror on the magnify geometry (zooms 1.5 and 2.5, radii 1, 2.5 and
  10): centre, the arc's ends, top and bottom, the narrowed view's corners on the screen, beside and
  behind, junk input; magnify with the curve off is the plane bit for bit; at the view centre both
  modes give the picture's centre at the same scale (zooms 0.5 to 4, three picture shapes), and
  above zoom 1 the view's edge shows less of the picture in screen mode than in magnify mode, and
  less in magnify mode than on the plane. The shader against the mirror on every pixel for the
  curved magnify mapping (radii 0, 1, 2.5 and 10, zooms 0.8 and 2.5, four head poses). The smoke
  frame at `--zoom 0.9 --zoom-mode magnify --screen-curve 1.0` now has bowed borders (side borders
  in one column, top and bottom 7.5 rows nearer the edge at the sides); this case expected straight
  borders before, and the changed expectation follows the reversed decision, it is not a loosened
  test. Magnify with `--screen-curve off` keeps straight borders and the frame of a run without the
  flag.

Run on the glasses on 2026-11-14 (see Hardware run 2026-11-14): worked well at 1.5 and about 2.5;
the owner prefers R = 1 to R = 2; `C`, `Z`, `L` and `I` as the README says; `C` in 3D (full SBS)
fine. The owner found `Z` flattening the curve did not fit: issue #41 reverses that decision. Still
unverified: a timing pair with the curve on (0 missed in one run with keys pressed), half SBS.

Hardware check for issue #41 (not done; the owner, Viture Pro XR): with a curve on (R = 1), play a
flat film at zoom 1.5 and 2.5 and compare `Z` back and forth: the screen stays curved in magnify
mode and `C` still switches it there, the picture straight ahead does not jump, and whether
magnify mode's curve (its bowed edges and ends come into view only when turning towards them)
feels right next to screen mode's. Unverified until then: how the curved magnify screen looks and
feels on the glasses.

## Automatic SBS switch (issue #18)

When no 3840x1080 output is found at start, xrdome asks XRLinuxDriver to switch the glasses to
side-by-side, waits for the output and plays; at exit it switches them back. `--sbs-switch
auto|off`, ini `[general] sbs_switch`, default `auto`. Code: `src/driver/xrdriver.c` (the driver's
files: state parsing, request writing; nothing SBS-specific beyond the request text, so other
requests can reuse it), `src/display/sbs.c` (the decision, the wait, switching back), wired in
`src/main.c` before the window opens. `docs/usage.md`: Flags, Config file; `docs/setup.md`: Switching to SBS.

Request format, from XRLinuxDriver's source at `da303d2` (2026-11-05), read, not copied:
`sbs_mode=enable` or `sbs_mode=disable`, one `key=value` line, written to
`/dev/shm/xr_driver_control` (file names `src/state.c` lines 31-33; parsing in
`read_control_flags`, lines 73-107, the `sbs_mode` values `unset`, `enable` and `disable` at
83-92). The driver reads the file on inotify `IN_CLOSE_WRITE` of that name, acts on
it only while glasses are connected and only when the mode differs, then deletes it
(`src/driver.c` `handle_control_flags_update` 561-590 and `monitor_control_flags_file_thread_func`
593-649). State: `heartbeat=` (epoch seconds, rewritten every second by `manage_state_thread_func`,
`src/driver.c` 541-559) and, only while glasses are connected, `sbs_mode_supported=` and
`sbs_mode_enabled=` (`src/state.c` `write_state` 43-71).

Decisions:
- Ask only when the state file's heartbeat is within 5 s of now (either way), it lists glasses,
  `sbs_mode_supported=true` and `sbs_mode_enabled=false`. A stale file (the driver crashed, as on
  2026-11-13) never gets a request, which would otherwise wait in `/dev/shm` for the driver's next
  start.
- The request is written in place (`O_CREAT|O_TRUNC|O_NOFOLLOW`), not written to a temporary file
  and renamed: the driver watches for the close of a write to that name, and a rename is a
  different inotify event.
- The wait lists the outputs again by re-initialising SDL's video subsystem (before any window
  exists), at least every 0.2 s and at every SDL event, for up to 8 s. SDL 2 keeps a display's old
  mode when an existing output changes mode ("FIXME" in its XRandR output-change handler), so
  pumping events alone is not enough on hardware; there is no fixed sleep.
- Success prints `display: switched the glasses to SBS in N.N s` (the time, to tune the 8 s on
  hardware). Failure keeps the old error line, then `xrdome: automatic SBS switch: <reason>`, then
  the `outputs:` line.
- Switch back at exit only when xrdome switched the glasses itself, so the desktop mirror works
  again; not after a disconnect during playback (the glasses come back in 1080p, and a request
  the driver cannot act on would stay pending in it), not when the driver has stopped. The window
  is destroyed first.
- After a timeout (or quitting during the wait) xrdome does not ask to switch back: if the
  glasses switch late, the next run finds them in SBS.
- `--output NAME` also qualifies when the named output is 1920x1080; a named output of another
  size (the laptop panel) does not.
- `--sbs-switch off` neither reads nor writes the driver's files and prints exactly what xrdome
  printed before. `--windowed` never asks.
- Tests never reach `/dev/shm`: the directory comes from `XRDOME_XR_DRIVER_DIR` (default
  `/dev/shm`), which the unit tests and all three smoke scripts set to a temporary directory.

What the sandbox verified (owner's laptop, software GL, Xvfb, and CI's Ubuntu 24.04 runs the
same): unit tests `test_xrdriver` (state parsing: connected, no glasses, enabled, unsupported,
bad values, a long license line; heartbeat window; request text; the written file, its
truncation, refusal of a symlink) and `test_sbs` (the decision for each state, the reasons, which
selection failures qualify, and the wait with fake outputs and clock: found after a few polls,
timeout exactly at the deadline, a failed listing retried, quit while waiting, nothing written in
each refusing state; switching back only to a running driver). Smoke `smoke_sbs` (S1-S5), with
`tests/fake_xr_driver.py` standing in for the driver and Xvfb started at 3840x1080 with its
output turned off at 1920x1080 (`-noreset`; Xvfb's RandR allows growing back only up to the
starting size): S1 the request is written, the fake grows the screen to 3840x1080, playback runs
full screen there (the dumped frame is 3840x1080) and `sbs_mode=disable` is written at exit; S2
`--sbs-switch off` writes nothing and prints only the old two lines; S3 a stale heartbeat writes
nothing and says why; S4 a driver that never switches gives the 8 s timeout line and no switch
back; S5 `--windowed` writes nothing. `smoke` case 3 (no driver at all) checks the reason line.

Run on the glasses on 2026-11-14 (see Hardware run 2026-11-14). XREAL Air 2 Pro: the whole
cycle works (switch in 2.2 to 2.8 s, switch back at exit, no switch back after an unplug). Viture
Pro XR: the switch worked once (3.3 s), then XRLinuxDriver 2.11.9 hung on 3 of 4 `sbs_mode`
requests (also with xrdome not running), so the switch back is unverified there; the glasses'
own button switches reliably. Already in SBS, driver not running and `--sbs-switch off` behaved as
designed.

## mpv scaling of enlarged flat video (`--mpv-scale`, issue #23)

`--mpv-scale off|NAME` (ini `mpv_scale`, default `off`, which keeps the previous behaviour): while a
flat picture is enlarged (`xrd_minify_ratio` < 1) and mipmaps are not in use, mpv draws each frame
into a texture of the picture's size on the display, with its own scaler `NAME` and
`keepaspect=no`, and the eye pass samples it at about 1:1. Shrunk pictures (mipmaps) and VR180 keep
the coded texture. `I` prints `info: mpv scale: ...`; the `video texture:` line ends in
`mpv-scale=NAME` or `mpv-scale=unused` and is printed again on each resize.

Decisions:
- Texture size (`xrd_mpv_scale_size`, `src/video/mpv_scale.c`, a pure function of the flat
  geometry, the layout, the coded size and the eye's viewport): each eye gets the screen's size in
  display pixels at the view centre, `half_w / tan_h * eye_w` by `half_h / tan_v * eye_h`, rounded;
  side by side and top-bottom keep two such eyes on the split axis (so even there). Used only
  when that is larger than the coded eye on both axes. Cap: at most 3840x2160 texels for the whole
  texture (what mpv already draws for a 4K film) and the GPU's largest texture, scaling both axes
  by the same factor; if the cap leaves an axis at or below the coded size, the coded texture is
  kept. There is no cap on the enlargement factor itself: the texel count is what costs.
- Debounce (`xrd_size_debounce`): the first size applies at once (so the first frame and the
  `video texture:` line already have it); after that a new size takes effect once it has been
  asked for unchanged for 250 ms, so a held `]` reallocates once.
- No black or half-drawn frame on a resize: the player reallocates the texture in place and has
  mpv draw the current frame again (`mpv_render_context_render` with no new frame redraws the
  current one, also on mpv 0.37 per its `vo_libmpv.c`) before the eye pass reads it; a coded-size
  change keeps a requested size, since mpv scales into any size.
- Eye pass at about 1:1: the `F` filter as before (Catmull-Rom by default). At 1:1 with the
  picture off the texel grid by a fraction of a pixel, which head motion makes the normal case,
  bilinear keeps 0.71 of the contrast of a 4-pixel period at half a pixel's offset and 0.50 of a
  3-pixel period; Catmull-Rom keeps 0.88 and 0.69 (computed from the kernels, not seen on the
  glasses). It costs no more than today since the eye pass runs per display pixel.
- `scale` is set after `mpv_initialize` with `mpv_set_property_string`: before it, mpv rejects a
  bad name without logging why. `cscale` is left unset, so it follows `scale` (mpv 0.37's default,
  checked in its `video.c`; the `fast` profile sets `scale` and `dscale` only). An `--mpv-opt
  scale=` wins and the flag's scale is then not set.
- VR180 is left alone. With the Viture Pro's 1920x1080 eyes over 37.9 x 21.9 degrees, any VR180
  file would need about 17600x8800 texels (154 million) to be 1:1 at the centre: 2.3x the coded
  size for 7680x3840, 4.1x for 4320x2160, 6.1x for 2880x1440. The 3840x2160 cap allows 0.53x,
  0.94x and 1.41x of the coded size, so a fixed oversize would buy nothing for 4K and 8K files and
  little for smaller ones, while the visible area is about 2.6% of each eye. Not worth a
  follow-up; a better eye-pass filter (#25) is the way for VR180.
- Side by side and top-bottom go through mpv as one picture, so mpv's kernel reaches across
  the eyes' shared edge by its radius less half a pixel (2.5 source pixels for `spline36`,
  0.5 for the bilinear of `gpu-dumb-mode`). The eye pass's clamp keeps its own filter on its
  side, but not mpv's. Today's path already shares the 4:2:0 chroma at that edge (the encode
  and mpv's chroma upsampling): a 480x270 half-SBS pattern with a 1-pixel white border shows
  the other eye in its last two columns without `--mpv-scale` and three with it. The smoke
  patterns of a given size therefore get a 4-pixel white border (`tools/make_pattern.sh`; the
  default patterns are byte for byte as before), as F4's 960-wide halves already have. Not
  guarded further: in a 3D film both edges show the same part of the scene. If the owner sees a
  seam, the eye pass can clamp each eye that many texels (times the enlargement) away from it.
- What xrdome's own filter still enlarges once this lands: all of VR180 (about 2 to 6x); flat video
  with `--mpv-scale off` (the default); flat video beyond the cap (the part above it, e.g. 1080p
  half SBS at zoom 1.5); pictures enlarged on one axis and shrunk on the other (mipmaps); and the
  250 ms after a zoom change before the new size settles. These are what #25 and #26 still act on.

Verified in the sandbox (llvmpipe, mpv in `gpu-dumb-mode`, which ignores the scaler's name and
enlarges bilinearly, so the look of `spline36` was not seen): unit tests for the size (mono, half
and full SBS, top-bottom, odd sizes, both caps, VR180 and bad input, agreement with
`xrd_minify_ratio`) and the debounce; smoke cases V1 to V7 (a 480x270 picture at zoom 1.5 gets a
960x540 texture and the same picture as without the flag; zoom 1.0 from the ini key fills the eye
exactly; zoom 0.5 keeps the coded texture with every trilinear draw checked; half SBS gets
1280x360 and each viewport shows only its own eye; subtitles appear where F6 has them; an unknown name exits 1
with mpv's reason; VR180 keeps the coded texture). By hand with XTest key presses under Xvfb
(not in the suite): `]` three times gave one resize to 1278x719 after the presses, zoom changes
while paused resized with the frame redrawn, zooming out to 0.58 went back to the coded texture
with trilinear filtering, zoom 4 gave 2560x1440 (the 640x360 window's eye), `P` to VR180 went
back to the coded texture, and a probe of every drawn frame found no black one. mpv's message
for an unknown name was seen on libmpv 2.6 only; on 0.37 its source logs `No scaler named
'...' found!` on the same path.

Run on the glasses on 2026-11-14 (see Hardware run 2026-11-14): cheap on this GPU (1080p film:
0 to 2 missed of 3600 at zoom 1.5, 1 to 7 at zoom 2.0 with the 3840x2160 texture); the owner saw
at most a slight improvement over `off`; no flash on resize; no fringe at the eyes' shared edge in
a full SBS film. Not checked: subtitles with it, the `I` line at zoom 1.5. Off by default stays.

## Enlarging filter in the eye pass (`--filter`, `F`, issue #25)

`--filter bilinear|bicubic|bicubic-ar|mitchell` (default `bicubic`, command line only as before);
`F` cycles bilinear, bicubic, bicubic-ar, mitchell and back, printing `filter: NAME`; `I` prints
the name. The filter acts wherever the eye pass enlarges: all of VR180 (about 2 to 6x) and flat
video that is enlarged without `--mpv-scale` or beyond its cap. Mipmaps still take over while
shrinking.

Decisions:
- Kept `bicubic-ar` (Catmull-Rom with anti-ringing: the result held, per channel, within the
  lowest and highest of the 2 x 2 texels around the sample point) and `mitchell`
  (Mitchell-Netravali, B = C = 1/3). Texture reads per pixel: bilinear 1, bicubic 9, bicubic-ar
  13 (the 9 plus the 4 nearest texels, each read at its centre), mitchell 9. Mitchell uses the same
  9-tap merge as Catmull-Rom: its two middle weights are positive for every fraction.
- Dropped Lanczos-2 after trying it: it can use the same 9 taps (its middle weights are positive
  too), but in the comparison below it was within one level of Catmull-Rom on every measure, at
  8 more sines per pixel. EWA / jinc was not tried: it is not separable, so it needs about 16 or
  more single reads per pixel plus a Bessel-like weight each, while the 8K VR180 file already
  misses 1.6% of vsyncs with the defaults.
- `bicubic` stays the default and keeps its meaning (exactly the previous Catmull-Rom, same
  shader arithmetic). `bicubic-ar` looks strictly better in the sandbox (same steepness, no halo)
  for 4 more reads, but it has not been seen or timed on the glasses, so the evidence is not
  enough to change the default.
- No ini key: `filter` has not been one (`docs/usage.md`, Flags; `ini_new_flags_are_not_keys` checks it).
  The issue assumed an ini key existed; adding one is a separate decision.
- All filters share one clamp: every tap goes through `tap()`, so no read reaches the other eye's
  pixels or the shared middle texel of an odd split, for the new 4 reads as for the 9.

Comparison in the sandbox (flat, `--zoom 1.0 --dither off`, a synthetic 16:9 picture with text,
1-pixel diagonals, black/white and 100/160 grey steps and 1-pixel gratings, lossless RGB, dumped
with `--dump-frame` from the 640x360 eye; crops in the PR description):

| 4x enlargement | ring at the grey step (levels) | halo pixels around text (white on 20, black on 235) | grating contrast, 2-pixel period |
|---|---|---|---|
| bilinear | 0 | 0, 0 | 0.75 |
| bicubic | 4 | 1708, 1116 | 0.91 |
| bicubic-ar | 0 | 0, 0 | 0.91 |
| mitchell | 2 | 265, 104 | 0.71 |
| lanczos2 (dropped) | 5 | 1796, 1170 | 0.92 |

At 2x the order is the same (bicubic 4, mitchell 1, bicubic-ar 0 levels of ring; contrast 0.50,
0.69, 0.69, 0.54 for bilinear, bicubic, bicubic-ar, mitchell). A thin bright line peaks slightly
lower with bicubic-ar than with bicubic (194 against 202 at 4x). Rough relative cost on Mesa
llvmpipe (CPU, a 7680x3840 texture drawn into 3840x1080, three noisy runs): bilinear 1, bicubic
about 3, mitchell about 3.3, bicubic-ar about 4 (lanczos2 about 4). That is not the GPU.

Verified in the sandbox (llvmpipe): render tests for each new filter against a double-precision
reference on a horizontal and a vertical step (`bicubic_ar_matches_reference`,
`mitchell_matches_reference`, `new_filters_vertical_match_reference`); bicubic-ar against a 2D
reference and within the 2 x 2 range on a noise texture at two views, where Catmull-Rom leaves
that range (`bicubic_ar_stays_in_2x2_range`); constant input stays constant at three views and
dithering stays within one level, for every filter; and every filter passes the existing eye-edge
checks (VR180 seam, flat stereo colours, seam edge at 20x magnify for even and odd sizes, odd
sizes straight ahead, and the shrinking cases with trilinear off). Mutations that drop the
anti-ringing clamp, move one of its reads off its texel or change a Mitchell weight each fail
these tests. Smoke E1 to E3: a 480x270 picture enlarged 4/3 with `bicubic`, `bicubic-ar` and
`mitchell` fills the eye, shows the same picture as `bicubic` but a different frame, and half SBS
keeps each viewport to its own eye. Not verified: the `F` key and its `filter:` line (the smoke
test sends no keys); the look on the glasses; GPU cost.

Hardware check (owner, after merge):
1. On a VR180 file (the enlargement is largest there) and then a 1080p film, on a still frame,
   `F` through all four filters: is `bicubic-ar` better than `bicubic` (no halo at high-contrast
   edges, no loss of detail), and is `mitchell` worth keeping? If `bicubic-ar` wins, it can become
   the default.
2. Timing pair on the 8K AV1 VR180 file at zoom 1.0, AC power, `--no-memory --frames 3600`, a
   `--timing-log` each, no keys: `--filter bicubic` against `--filter bicubic-ar` (the most
   expensive), missed vsyncs from the `timing:` line. The defaults missed 1.6% there on 2026-11-14.

## Sharpening after the eye pass (`--sharpen`, `Shift+F`, issue #26)

`--sharpen S` (0 to 1, default 0 = off) and `[general] sharpen` (same range; the flag wins);
`Shift+F` steps off, 0.25, 0.5, 0.75, 1 and back to off, printing `sharpen: 0.50` or
`sharpen: off`; `I` prints `info: sharpen: ...`. Not remembered per file (a viewing preference,
like `--filter` and `--deband`).

Decisions:
- Design (a), a second pass: with sharpening on, the eye pass draws both eyes into an
  intermediate texture of the output's size (`GL_RGB10_A2`, the same 10 bits as the video
  texture, alpha used as the picture mask) without dithering, and `sharpen.frag` draws each eye
  from it into the window: 9 `texelFetch` reads per pixel (the 3 x 3), at 3840x1080, plus the
  intermediate written and read once (16.6 MB each way per frame). At 0 there is no second pass
  and no intermediate: the draw is the old one-pass draw, byte for byte.
- Not (b), sharpening in source space inside the eye pass: CAS needs the 3 x 3 around every
  texel it sharpens, so sharpening the texels a filter reads means a 4 x 4 texel neighbourhood
  for bilinear (16 reads instead of 1) and 6 x 6 for the 4 x 4 bicubic/Mitchell kernels (36
  instead of 9; 40 for bicubic-ar), per display pixel, every texel's sharpening recomputed for
  every display pixel that covers it (2 to 6 times over in VR180). Not built or measured: at 2
  to 4 times the reads of today's filters, against 9 cache-friendly reads in (a), it is the
  more expensive design on this GPU, and it would differ per filter and change again with
  `--mpv-scale` and mipmaps. Its advantage: it sharpens at the source's scale (see the next
  point).
- What display-space CAS does to an enlarged picture: its 3 x 3 sees one to two display pixels,
  so it acts on edges one or two pixels wide. Measured on a hard 50/200 step drawn flat at
  several enlargements, strength 1, steepest step in levels per pixel: 1:1 150 -> 200 (+33%);
  2x 74 -> 88 bilinear, 90 -> 104 bicubic (+16 to +19%); 3x 50 -> 66 bilinear, 61 -> 67 bicubic;
  4x 38 -> 42, 47 -> 51 (+9 to +10%). A VR180 step enlarged far more (a 32-texel test eye)
  changed by under 4 levels. So it helps most at about 1:1 (flat near zoom 1, or with
  `--mpv-scale`) and on VR180's 2x (8K) more than its 5x (4K). Whether that is visible on the
  glasses is the owner's call.
- The filter is CAS's sharpen-only path ported from AMD's `ffx_cas.h` 1.20190610 (soft min and
  max over the cross and the 3 x 3, `CAS_BETTER_DIAGONALS`; per-channel weights, `CAS_SLOW`;
  exact reciprocal and square root, `CAS_GO_SLOWER`), so the MIT notice is in `sharpen.frag`,
  the README's Licence section names it and the ebuild says `GPL-2+ MIT`. The shader's text,
  notice included, is compiled into the program by `embed_shaders.py`. One deviation: the
  amount divides by `max(mx, 1e-5)`, so an all-black neighbourhood gives 0, not NaN. It runs on
  the display-encoded (gamma) values, as most CAS ports do; AMD suggests linear input.
- Strength: CAS's own `sharpness` 0..1 maps to a lobe of -1/8..-1/5, and its 0 still sharpens.
  Ours is linear from 0 (off) to 1 = CAS at `sharpness 1` (-1/5); 0.625 is CAS's `sharpness 0`.
  That makes 0 off with no jump to the first step. Fringe on a hard 1:1 50/200 edge: about 25
  levels at strength 1, about 9 at 0.5.
- Eye edges: every neighbour read is clamped into the eye's own viewport (nothing crosses
  between eyes), and a neighbour the eye pass drew black (behind, outside `--content-fov`,
  around the flat or curved screen) is read as the centre pixel, so black is never sharpened
  into the picture and the picture's border pixels are sharpened as if it continued. Black
  itself stays exactly black and undithered. (Pure black alone would already give CAS an amount
  of 0 next to it; the mask also keeps the border rows sharpened along the edge and does not
  depend on the outside being exactly 0.)
- Dithering moves to the second pass, so it still comes last; the intermediate keeps 10 bits.
- `Shift+F`: `F` ignored Shift before, so the combination was free; it steps rather than cycles
  a list so a `--sharpen` value in between steps to the next quarter. `sharpen` is an ini key as
  the issue asked (unlike `filter`, nothing ruled it out); it is in `[general]` only.
- If the GPU cannot render to the intermediate, one warning
  (`xrdome: warning: sharpening is off: ...`) and drawing goes on without it.

Rough relative cost on Mesa llvmpipe (CPU, not the GPU): a 7680x3840 10-bit texture drawn as
VR180 into 3840x1080, `glFinish` after every frame, 10 frames, three runs each (within 0.1 ms):
bilinear 4.5 ms, bilinear + sharpen 15.0 ms; bicubic 14.9 ms, bicubic + sharpen 25.0 ms. The
second pass costs about 10.3 ms there, of which a plain copy pass through the intermediate (1
read) is 3.6 ms. On a GPU the ratio will differ (the eye pass's 8K reads miss the cache, the
second pass's mostly hit); only the timing pair below decides.

Verified in the sandbox (llvmpipe): render tests `sharpen_matches_cas_reference` (every pixel
against a double-precision CAS reference computed from the unsharpened draw read back from an
`RGB10_A2` target, within 1e-3, at strengths 1 and 0.5: a 1:1 flat step, a VR180 step across
the eye boundary, VR180 next to the rolled content edge of a 90x90 file and the rolled "behind"
edge, the rolled flat screen's border and the curved screen's border),
`sharpen_zero_is_todays_output` (strength 0 after 0.5, and -1, byte-identical to the one-pass
draw for all four filters in both projections with dithering), `sharpen_keeps_constant`
(constant colour stays within 2 levels with dithering, black stays exactly black, at the test
size and then 3840x1080, which remakes the intermediate), `sharpen_keeps_eyes_apart` (a
different grey per eye, each viewport constant to the boundary, swapped and not),
`sharpen_steepens_a_step` (darker dark side, brighter bright side, steeper edge, within 0..1)
and `sharpen_ignores_caller_state`; unit tests for the flag, the ini key and their precedence
and errors, `Shift+F`'s steps, the session copying `--sharpen` into the controls, and the key
list against README. Mutations that drop the eye clamp, drop the mask, use green-only weights,
drop the diagonals, dither in the first pass, dither the black, scale the lobe by 1/8 or draw
the second pass into framebuffer 0 each fail at least one test. Smoke SH1 to SH5: flat 4/3
enlargement with `--sharpen 0.5` fills the eye and shows E1's picture but not its frame; half
SBS at 1 keeps each viewport to its eye; a shrunk picture with mipmaps keeps black corners and
every trilinear draw finds its chain; VR180 with `sharpen = 0.5` from the ini passes the frame
check and differs from the unsharpened frame, and `--sharpen 0` beats the ini; `--sharpen 2`
exits 1 naming the flag and range. Not verified: `Shift+F` and its `sharpen:` line, the `I`
line (the smoke test sends no keys); the look on the glasses; GPU cost.

Hardware check (owner, after merge):
1. Timing pair first, on the 8K AV1 VR180 file at zoom 1.0 (the defaults missed 1.6% there):
   AC power, `--no-memory --frames 3600`, a `--timing-log` each, no keys, `--sharpen 0` against
   `--sharpen 0.5`; missed vsyncs from the `timing:` line. Then the same pair on the 4K AV1 flat
   film at zoom 1.5.
2. Look: on a still frame of the VR180 file and of a low-bitrate flat file, `Shift+F` from off
   to 1 and back: is there a visible gain at 0.5 or 0.75, and are halos or noise visible at 1?
   With `I`, note the strength judged best; it can become the default only after that.

## XREAL Air 2 Pro (owner, 2026-11-14, `main` at `bb95090`)
- VR180 and flat files played on the XREAL Air 2 Pro with head tracking, "pretty much the same" as
  on the Viture Pro XR, both with `--device xreal-air2pro` and without it.
- Without `--device` xrdome uses the Viture's diagonal FOV (43 degrees) on the XREAL's 45, so the
  picture is drawn about 4% smaller than intended and moves slightly less than the head turns;
  nothing was reported about that difference. `--device xreal-air2pro` is the correct setting.
- Not covered by this run: yaw drift over 5 minutes, EDID/`xrandr --props` capture, convergence,
  per-file settings, and the features in the open PRs #28-#34 (this was `main`). All but
  convergence and per-file settings were run that evening: see Hardware run 2026-11-14.

## Hardware run 2026-11-14, afternoon and evening (Viture Pro XR, XREAL Air 2 Pro)

Setup: ThinkPad P14s Gen 6 AMD, Gentoo (gcc 16.2.1, mpv 0.42.0 / libmpv 2.6.0, FFmpeg 9.0.2,
SDL 2.32.10), MATE/Xorg, XRLinuxDriver 2.11.9 run by hand; `main` at `e8803c5` (PR #36). The owner
wore the glasses, pressed the keys and said what was seen; the session ran each command after the
owner's go-ahead for that step and read the logs itself (stderr, `--timing-log`, the driver's state
file and log, dmesg). Timing runs: AC power, governor `performance` (platform profile `balanced`),
`--no-memory --frames 3600`, a `--timing-log` each, no keys; "missed" is swap intervals over 20 ms.
Files, by format: a 1080p AV1 film (10-bit, 24 fps, 1.1 Mbit/s), a 4K AV1 film (10-bit, 30 fps,
3.1 Mbit/s), an 8K AV1 VR180 file (7680x3840, 24 fps, 15 Mbit/s), a dark 1080p AV1 film
(1.2 Mbit/s), an 81-minute 720p AV1 film, a full side-by-side HEVC film (3840x1080, 30 fps). The
gate on `main` passed first: 0 warnings, 23/23 OK (xvfb-run 21.1.21.1 installed for it).

### Automatic SBS switch (#18)
Viture Pro XR, from 1080p mirror mode (its output came up at 120 Hz; SBS mode offers 60 Hz only):
- The switch: `display: switched the glasses to SBS in 3.3 s`; the picture landed on the glasses,
  full screen, with head tracking (owner); 600 frames, 4 missed.
- The switch back was not seen. XRLinuxDriver hung during that run: its heartbeat stopped at
  14:50:26, a second before xrdome wrote `sbs_mode=disable` at exit, the request was never read and
  the glasses stayed in SBS. The driver hung again on the next request (14:59, after a restart and
  a replug) and on a bare `printf 'sbs_mode=enable\n' > /dev/shm/xr_driver_control` with xrdome not
  running (15:02): 3 hangs in 4 requests, each with `get_display_mode ... returned -7` (a USB
  timeout) in its log and the glasses' HID interface re-enumerating about 3 s later. It is the
  driver, not xrdome; written up for upstream by the owner. In the run that timed out, xrdome
  waited 8 s, printed the reason and asked for no switch back, as designed.
- Started with the glasses already in SBS: no request, no `display:` line at start or exit.
- With the driver hung (heartbeat 91 s old): the old error, then `automatic SBS switch:
  XRLinuxDriver is not running (the heartbeat in /dev/shm/xr_driver_state is 91 s old)`, then the
  outputs line; nothing written. `--sbs-switch off`: the old two lines only; nothing written.
- A restarted driver deleted a request still waiting in the control file without acting on it
  (twice): it reads the file before the glasses have connected.
- The Viture's own button (front left, hold 2 s) switched between 1080p and SBS every time.

XREAL Air 2 Pro:
- The whole cycle: `switched the glasses to SBS in 2.8 s` (2.2 s in a later run), 600 frames,
  0 missed, and at exit the driver switched back at once; its heartbeat stayed current.
- Once (16:22:57) the driver switched the glasses to SBS and back to 1080p a second later with no
  request from xrdome; xrdome timed out after 8 s, as designed. The next try worked. Seen once.
- Unplug during playback: `xrdome: warning: glasses disconnected, exiting` about 0.3 s after the
  unplug (the display went 2.4 s before the kernel logged the USB disconnect), exit 0, no switch
  back asked, nothing left pending. The driver itself crashed on that unplug (glibc `corrupted
  size vs. prev_size while consolidating`), and again on an unplug at 23:35 with xrdome not running.
- Once the glasses came up in SBS at plug-in and showed nothing at all while X listed a 3840x1080
  output and xrdome drew 2654 frames to it; resetting the glasses brought the picture back. Seen
  once.
- The volume-up button (right arm, hold 2 s) switches the XREAL between 1080p and SBS.

### Debanding (#24)
| File | `--deband off` | `--deband on` |
|---|---|---|
| 4K AV1 flat film, zoom 1.5 (mipmaps on) | 1 missed (0.03%) | 27 (0.75%) |
| 8K AV1 VR180, zoom 1.0 | 59 (1.64%) | 1015 (28.2%); swap p99 37.6 ms against 16.9 |

- At 8K, mpv's deband over 7680x3840 saturates the integrated GPU. Off by default stays.
- `B` on a paused dark, low-bitrate 1080p frame, zoomed to 4: "Smoothness difference was
  minimal" (owner). `B` printed `deband: on` / `off` alternately and `I` printed the state of the
  last press.
- The 4K runs have a load spike every 32 display frames (16 source frames of the 30 fps film),
  with or without deband: on those frames the mean interval is 17.2 ms against 16.66; deband pushes
  them over 20 ms (all 27 misses fall on them). Not explained; an AV1 mini-GOP of 16 frames is a
  guess, not checked.

### mpv scaling (#23)
1080p AV1 film:

| `--mpv-scale` | zoom 1.5 (texture 2880x1620) | zoom 2.0 (texture 3840x2160, the cap) |
|---|---|---|
| off | 0 missed | 1 (0.03%) |
| spline36 | 2 (0.06%) | 1 (0.03%) |
| ewa_lanczossharp | 2 (0.06%) | 7 (0.19%) |

- The look, still and in motion: "Hard to tell, maybe slight improvements across scalers." `F`:
  "helped a little, but again, it was minor." (owner)
- Resizing with `[` `]`: one `video texture:` line per settled zoom (e.g. 3168x1782 at 1.65,
  3840x2159 at 2.66, capped), the coded texture with mipmaps again below zoom 1 (`I`:
  `mpv scale: spline36, not in use (picture shrunk)`), and "No flash noticed" (owner).
- Full SBS 3D film on the XREAL with `spline36`: texture capped to two 2715x1527 eyes, no fringe
  from the other eye at the screen's borders (owner).

### Curved screen (#20)
- 1080p film at 1.5 and about 2.5, `C` against flat, R = 1 and `--screen-curve 2`: "Everything
  worked well in both; I preferred the stronger curve" (R = 1). `C` printed `screen curve: 1.00` /
  `off` (`2.00` / `off` with R = 2), `I` gave the arc's angle (64.9 x 35.3 degrees at 1.65 with
  R = 1, 88.3 x 50.0 at 2.42 with R = 2), `Z` and `L` printed their lines.
- What did not fit: "The only behavior that didn't fit well was Z which flattened the curved
  image" (owner). That reverses a decision of this section; issue #41.
- 3D: `C` on the full SBS film on the XREAL worked as expected (owner).
- Timing: 0 missed in 4751 frames with the curve on for much of the run (keys pressed; not a
  controlled pair).

### Resume (#19)
| Run | Result |
|---|---|
| 81-min film, `--resume --mpv-opt start=2400`, `Q` | `saved proj=flat position=0:40:17`; first frame 0.60 s after start |
| the same, `--resume` | `restored ... position=0:40:17`, playback continued there (owner); first frame 0.82 s |
| 8K VR180 from 0:00, no resume | first frame 0.58 s |
| 8K VR180, `--resume --mpv-opt start=240`, then `--resume` | saved `0:04:28`, restored there (owner); first frame 0.86 s and 1.04 s |
| 81-min film from 1:20:40, `Q` at about 1:21:01 | `settings: saved proj=flat`, position removed (near-end rule) |
| the same, `--resume` | no position restored, playback from the start (owner) |

- Bug: a film that played to its end (the owner fast-forwarded past it) kept its old position,
  and the next `--resume` opened it there. mpv unloads the file at its end, so position and
  duration are both gone when the record is saved. Reproduced under Xvfb; fixed in PR #39 (smoke
  M20).
- Not tried: the near-start rule (a quit in the first 30 s).

### 3D on the XREAL (flat projection)
The full side-by-side film opened in 3D with no flags: `detect: proj=flat (frame shape)
layout=sbs-lr (file name "SBS") packing=full (auto)`. Depth right, each eye its own picture; with
`--layout mono`, `M` gave `sbs-lr half` (squeezed, as expected) and then `sbs-lr full` (right);
0 missed in all three runs. "Everything worked as expected" (owner).

### Tracking, drift and EDID (Task 10)
- Packet rate: Viture about 350 per second, XREAL about 1000.
- Viture on the desk for 5 minutes, hours after it connected: yaw -0.02 degrees, pitch -0.06,
  roll +0.06.
- XREAL: a yaw slide of 3 to 17 degrees over the first ~30 s after a calibration done while the
  glasses were handled or worn (worn and still: -11.6 degrees in 30 s from 8 s after calibration,
  +17 degrees in 29 s from calibration; on the desk from 21 s after calibration: -2.7 in the first
  minute, -2.83 after 5 minutes). Lying still through the calibration: 0.00 degrees in 21 s and
  +0.06 in 60 s. Worn and still long after calibration: -0.08 degrees per minute in 1080p mode,
  back within 0.1 degree after 2 minutes in SBS mode. In xrdome this showed as the picture
  drifting sideways a few seconds after `Shift+R` or unlocking with `L` (owner). Plan Task 10
  already said to keep the glasses still for the first 15 s; that is now measured. Open: the
  playback drift at 16:21 and 16:23 came right after xrdome's SBS switch, six minutes after
  calibration; whether a switch (it restarts the IMU stream) needs stillness too is not known.
- Holding `Shift+R` recentred on every key repeat, which hid the slide in one run; fixed in PR #42.
- Pose gaps: `pose stale` then `pose resumed` (about 0.6 s without packets) in 6 of the 10 timing
  runs on the Viture. Not explained.
- EDID and `xrandr --props` for both glasses in both modes: `docs/hardware/`. Viture: `CVT`,
  `VITURE`; 1080p mode 60, 90 and 120 Hz, SBS mode 3840x1080 at 60 Hz only. XREAL: `MRG`,
  `Air 2 Pro`; the same modes. Both report the placeholder serial 0x88888800.
- The XREAL udev rule (now in `docs/setup.md`) is what gives the driver access: the XREAL's hidraw and USB nodes
  are `root:plugdev 0660` and no other rule names its vendor ID 3318 (inferred; the rule was not
  removed to show it).
- After a replug the glasses' output name changed (`DisplayPort-1` to `DisplayPort-0`); xrdome
  finds it by size, so nothing changed for it. SDL names the laptop panel `0` in xrdome's
  `outputs:` line (the README's example says `eDP-1 14"`).

### Not done in this run
Subtitles on the glasses (no subtitled film to hand); shimmer with and without mipmaps; a real
half side-by-side film, and `M` into half SBS on one; top-bottom; the Viture's switch back at exit
(the driver hung); the Viture unplugged during playback; the near-start resume rule; the `I` line
of `--mpv-scale` at zoom 1.5 and subtitles with it; a timing pair for the curved screen;
convergence on the XREAL.

## Platform module and macOS build (2026-11-19)
- `src/platform/`: the platform is fixed at build time (`xrd_platform_current`); the choices that
  follow from it are pure functions of the platform, so both are unit tested on Linux
  (`tests/test_platform.c`). On Linux every choice is what xrdome did before: SDL X11 driver with
  EGL, mpv gets the X11 display, hwdec `vaapi`, borderless window on Xorg, automatic SBS switch.
- `--hwdec NAME` and `[general] hwdec` (default by platform) replace the hard-coded `vaapi`; for
  the NVIDIA desktop use `nvdec` or `auto-safe`. `--require-hwdec` now accepts any zero-copy
  decoder (not `no`, not `-copy`). The warning text keeps its shape, "not <hwdec>", so it is
  unchanged on Linux defaults.
- macOS: Cocoa/CGL forward-compatible GL 3.3 core context, high-DPI drawable, no X11 display for
  mpv, hwdec `videotoolbox`, fullscreen window, no SBS switch; BSD `getopt` reset (`optreset`);
  `_DARWIN_C_SOURCE`; EGL optional, so `test_render` (surfaceless Mesa EGL) is Linux only.
- CI `macos-build-test` (`macos-latest`, Apple Silicon): build with `-Dwerror=true` and unit tests;
  the smoke tests skip there (no xvfb-run). Not run on glasses: there is no owner Mac. Unknowns:
  an unscaled 3840x1080 mode on the glasses, compositor frame timing, VideoToolbox limits.
- Not done: a Viture SDK pose backend. Viture's C SDK (2026) lists macOS aarch64 and the
  Viture Pro (PIDs 0x1019, 0x101D), but its signatures are only in the SDK download; the backend
  waits for the headers (follow-up issue).

## macOS test bundle from CI (issue #83, 2026-11-19)
- `packaging/macos/bundle.sh` (after the CI build) runs `bundle.py`, standard library only: copies
  `xrdome` and Homebrew's `ffprobe` to `dist/xrdome/`, every non-system dylib they load
  (transitively, through `otool -L`; `@rpath` names resolved through the rpaths) to `lib/`, rewrites
  load paths to `@executable_path/lib/<name>` with `install_name_tool` (and drops absolute rpaths),
  signs each file ad hoc (`codesign --force -s -`), adds `COPYING`, `LICENSES/<formula>/` (licence
  files from each formula's keg), `SOURCES.md` (formula, version, licence and source URL from
  `brew info --json=v2 --installed`, plus ffprobe's build configuration) and `README-macos.txt`,
  then zips `xrdome-<version>-macos-arm64.zip`. It fails on `--enable-nonfree` in
  `ffprobe -buildconf`. Homebrew's FFmpeg is built `--enable-gpl --enable-version3`, so the bundle
  as a whole is under GPL 3 or later (xrdome is GPL-2.0-or-later); SOURCES.md says so.
- CI `macos-build-test`, after the unit tests: bundle; `bundle.py verify` and `otool -L` on every
  file (no `/opt/homebrew` or `/usr/local` path); then with `/opt/homebrew` moved away and
  `env -i PATH=/usr/bin:/bin`, `xrdome --version` and `ffprobe -show_streams` on a sample; then the
  zip is uploaded as artifact `xrdome-macos-arm64` (the download is a zip holding the zip, because
  upload-artifact drops file modes).
- Run-time library: Homebrew's `sdl2` is sdl2-compat, whose load-time constructor `dlopen()`s SDL3
  (tries `@loader_path/libSDL3.dylib` first); nothing links it, so `otool` never lists it, and a
  failed load is a modal error dialog that hangs a headless runner. `bundle.py` (`sdl3_extra`) adds
  `$(brew --prefix sdl3)/lib/libSDL3.dylib` to `lib/` under that name, with its own dependencies,
  whenever the bundled libSDL2's formula depends on `sdl3`. CI fails if libSDL3 is not loaded from
  `dist/xrdome/lib` (dyld trace).
- `ffprobe` lookup (`src/platform/exepath.c`): with `[general] ffprobe` unset, `<dir of xrdome>/ffprobe`
  if it runs, else `ffprobe` on `PATH`; the directory comes from `_NSGetExecutablePath` (macOS) or
  `/proc/self/exe` (Linux), unknown elsewhere. A directory that is itself on `PATH` keeps `PATH`'s order,
  so a normal install behaves as before. The pure choice is unit tested (`tests/test_exepath.c`).
- New: `xrdome --version` (prints `xrdome <meson version>`), used by the smoke check.
- Not verified: no Mac here, so the script and the CI steps are written from the documentation and
  only the pure parts (otool parsing, resolution, the walk, SOURCES.md) are tested
  (`tools/tests/test_macos_bundle.py`); whether the bundle runs on a Mac with glasses is #65.

## Photos in 2D (issue #68, Task 3 of the depth 3D plan, 2026-11-19)

Spec `docs/superpowers/specs/2026-11-19-depth-3d-design.md` section 7 (first half), plan
`docs/superpowers/plans/2026-11-19-depth-3d.md` Task 3. No depth yet: an image or a directory opens
as a flat mono screen. `docs/photos.md`.
- Detection: `ffprobe` now also runs with `-show_format`; `format_name` of `image2`, `image2pipe`
  or `*_pipe` makes `xrd_probe.image` and `xrd_detect.image`. An image is flat mono (source
  `image file`) wherever a flag, the record, metadata or the file name leave it open. No probe
  data: the extension decides. `frame_aspect` is 0 for an image: ffprobe reports the stored size,
  which EXIF may turn (a 96x48 JPEG with Orientation 6 probes as 96x48 and is shown 48x96).
- Player: `image-display-duration=inf` at open; `xrd_player_load` replaces the file for
  PageDown/PageUp. mpv's libmpv output does not flag a frame for the new file: after the video
  reconfig for it xrdome must call render itself, or mpv waits for it ("mpv_render_context_render()
  not being called or stuck") and the old picture stays. `xrd_player_file_shown` is false from the
  load until that frame is drawn, and `--frames` counts only frames of the photo now open.
- Directory: `src/app/imagedir.c` lists regular files with an image extension, byte order of the
  name, no recursion, no dot files. No wrap at the ends. The per-photo record, `detect:` line and
  controls are redone for each photo (`switch_photo` in `src/main.c`); the old photo's record is
  saved first, only if it was drawn.
- `--press KEYS` (development, with `--frames`): SDL key names pushed as key presses, one after
  every 3 frames of the open photo; it is how `tests/smoke_photos.sh` presses PageDown under Xvfb.

EXIF orientation, measured (2026-11-19). A 96x48 JPEG of four coloured quadrants, each of the eight
Orientation tags written, shown by xrdome (`--frames`, `--dump-frame`), compared with Pillow 13
`ImageOps.exif_transpose` on the same files (the helper's transpose, spec section 5):
- mpv 0.42.0 / FFmpeg 9.0.2 (Gentoo and an `archlinux:latest` container): all eight tags are shown
  as the transpose (size and quadrants), including the mirrored tags 2, 4, 5 and 7.
- mpv 0.37.0 / FFmpeg 6.1.1 (`ubuntu:24.04` container, `--vo=image` frames): tags 1, 3, 6 and 8
  as the transpose; 2, 4, 5 and 7 are turned but not mirrored (shown as 3, 1, 6 and 8). mpv
  reports `video-params/rotate` 180, 0, 90 and 270 for them.
- mpv reports the size already turned (`video-out-params` w x h with `rotate` 0), so the existing
  `xrd_video_texture_size` needed no change. ffprobe 9.0.2 and 6.1 do not print the orientation.
- So the spec's contract ("mpv must show the photo the way the helper's EXIF transpose does")
  holds for every camera photo (tags 1, 3, 6, 8) on both mpv versions and for all eight tags on
  0.42. On older mpv the depth of a mirrored photo would be flipped against the picture: Task 4 or
  5 must either read the tag itself and mirror the depth, or let the helper skip mirrored tags.
  Nothing is rotated in the player today. `tests/smoke_photos.sh` asserts the transpose for 1, 3, 6,
  8 and the shape for all eight, and prints an `info:` line for the mirrored ones saying which
  behaviour this mpv has.

Tests: `test_probe` (the format block, fixtures `image_*.txt`), `test_detect` (photo rows),
`test_imagedir`, `test_config` (`--press`), `test_controls` (the key list in `docs/usage.md`), and the Xvfb smoke test
`smoke_photos` (registered in `meson.build` and in the CI "Require the smoke tests to have run"
lists): eight orientations, a directory of a landscape and a portrait JPEG with PageDown and PageUp
and the ends, a single file, an empty directory, per-photo zoom records.

Not seen on the glasses: a portrait and a landscape photo upright and the right shape (owner).
Limits: a photo mpv cannot decode ends the run, as a film does; HEIC is not opened (plan Task 6);
`ffprobe` runs once per photo on PageDown, blocking the loop for its few tens of ms.

## Depth 3D for photos (issue #69, Task 4 of the depth 3D plan, 2026-11-19)

Spec `docs/superpowers/specs/2026-11-19-depth-3d-design.md` section 6 and the keys of section 7.
A photo whose depth `xrdome-tools depth photo` has cached is drawn in 3D. `docs/photos.md`, Depth 3D for photos.
- Cache (read side, `src/render/depthmap.c`): key = sha256 of the photo's bytes (own FIPS 180-4
  code), `$XDG_CACHE_HOME/xrdome-tools/depth/<key>-dav2s-v1.pgm`, PGM `P5`/65535/big-endian with an
  exact body length. `test_depthmap` runs `tools/xrdome_depth/cache.py` to write a file and reads it
  back by the C key. Hashing blocks the loop at open and on PageDown (tens of ms for a large JPEG).
- Shader (`depth_u()` in `vr180.frag`, C mirror `xrd_depth_source_u`): parallax search, N = 16
  depths from near to far, linear refinement, a wall hit (more than two steps in front) takes the
  previous try, so gaps show the background beside them. Only flat mono with strength > 0; every
  other draw is byte-identical to before (`test_render`).
- Keys `D`, `-` `=` (0.5% steps, 0 to 5%), `,` `.` (0.05 steps); strength and plane in filestate
  (`depth_strength`, `depth_plane`). Defaults 2% and 0.5 are provisional.
- Orientation: for EXIF tags 2, 4, 5, 7 on mpv older than 0.42 the photo is shown in 2D with a note
  (mpv reports no flip; 0.38 to 0.41 untested). A depth whose aspect does not fit the shown picture is
  dropped with a warning.

Timing, measured 2026-11-19 on the Viture Pro XR at 3840x1080@60 (Radeon 860M, Mesa RADV GL), a
4032x3024 synthetic JPEG at the default flat zoom 1.5 (picture fills both eyes, trilinear), 2048x1536
depth, `--frames 3600 --timing-log`: 0 swap intervals over 20 ms in each of 3600 frames for no depth,
a scene-like depth and an all-far depth (the worst case: all 16 tries per pixel); p99 interval
17.1 to 17.6 ms. GPU time of the eye pass (a temporary GL_TIME_ELAPSED query, not committed), mean /
max over 2900 frames: 2D 1.12 / 1.61 ms; scene depth N=16 2.10 / 3.00 ms; all-far N=8 1.97 / 3.02,
N=16 2.65 / 3.66, N=32 3.39 / 4.52, N=64 4.20 / 5.54 ms. N = 16 leaves wide headroom; no lower N or
mesh fallback needed.

Not seen by eye on the glasses: default strength and plane, doubled edges (owner).

## Depth helper service (issue #70, Task 5 of the depth 3D plan, 2026-11-19)

Spec `docs/superpowers/specs/2026-11-19-depth-3d-design.md` section 7. A photo without cached depth
opens in 2D and turns 3D when `xrdome-tools depth serve` has made its depth. `docs/photos.md`, Depth 3D for
photos; docs/tools.md, depth serve.
- Helper (`tools/xrdome_depth/serve.py`, launcher `depth serve`): one thread, `selectors` loop,
  Unix socket mode 0600, JSON lines. Cached photos are answered at once; the queue gives the newest
  non-prefetch photo first, then the newest prefetch; input is drained before each photo, so the
  newest request is known when the next one is chosen. The 518x392 model is loaded and run once
  before the socket opens; the 392x518 one on the first portrait photo. Refuses `setup depth --cpu`.
  Exits after 10 minutes idle (no client, nothing queued).
- Replies carry `path` (an addition to the spec's example), since they come back out of order.
- Player (`src/app/depthclient.c`): non-blocking `AF_UNIX` socket (`fcntl`, no `SOCK_NONBLOCK`;
  `MSG_NOSIGNAL` on Linux, `SO_NOSIGPIPE` on macOS), polled once per frame. The helper is started
  with `posix_spawnp` (own process group, stdin `/dev/null`, stdout and stderr to
  `xrdome-depth.log` beside the socket, other descriptors closed on glibc 2.34+ and macOS) only
  when a request needs it and the socket does not answer: a single cached photo starts nothing; a
  directory starts it for the prefetches. It is waited for up to 30 s (it loads and warms the
  model first); its exit, a failed start, a timeout or a lost connection is one note for the run.
  xrdome does not stop the helper at exit.
- Socket path: `$XDG_RUNTIME_DIR/xrdome-depth.sock`, else `xrdome-depth-<uid>.sock` in `$TMPDIR`
  or `/tmp` (macOS has no `XDG_RUNTIME_DIR`); C and Python compute it the same way.
- `--depth-helper PROGRAM|off`, ini `depth_helper`. `smoke_photos` and `smoke_depth` run with
  `depth_helper = off` and their own `XDG_RUNTIME_DIR`, so their output is as before.
- Tests: `test_depthclient` (paths, request escaping, reply parsing, against an in-test server, a
  helper that is missing, exits, starts late or never answers, writes to a closed peer),
  `tools/tests/test_depth_serve.py` (protocol, queue order, the socket loop with a fake depth maker,
  the launcher), `smoke_depth_serve` under Xvfb with `tests/fake_depth_helper.py` (serve.py's loop
  and a fake step depth): 2D then 3D, a cached photo starting nothing, a running helper reused,
  directory prefetch and PageDown, a missing, exiting and failing helper, `off`.

Not run: the real model behind the socket (no depth venv on the development machine) and the
owner's check: browse 20 photos on the glasses, each 3D within about 1 s, `--timing-log` < 1%
missed. Reading the depth file (up to 2048x1536, 6 MB) and the texture upload happen in the frame
the reply lands, as with a cached depth at PageDown; their cost on the glasses is unmeasured.

## Video frames out to the depth helper (issue #72, Task 7 of the depth 3D plan, 2026-11-19)

Spec `docs/superpowers/specs/2026-11-19-depth-3d-design.md` section 8, first half. `D` on a flat mono
film sends its frames to `depth serve`, which makes their depth live and writes it back; the film is
still drawn in 2D (uploading that depth is Task 8). `docs/photos.md`, Depth for films; docs/tools.md, depth
serve and depth bench-video.
- Player (`src/video/framesink.c`): only for a film, flat, mono, 3D on and a helper configured
  (`xrd_framesink_wanted`); otherwise no FBO, PBO or ring exists. Films start with 3D off (photos
  still start on). Each new mpv frame (`xrd_player_frame_serial`): a box filter (4 x 4 bilinear
  taps, level 0) into a 322x182 RGBA8 FBO with the rows flipped so the readback is top row first,
  `glReadPixels` into one of 3 `GL_STREAM_READ` PBOs, a fence, `glFlush`. Each loop iteration first
  copies every PBO whose fence `glClientWaitSync(..., 0)` reports done into the ring (so one frame
  late at the earliest); with all 3 in flight a new frame is skipped and counted. The step runs
  before the pose sample; its CPU time is the new `frames_out_us` column of `--timing-log`, and a
  `depth: video stopped:` line gives the totals when 3D goes off or xrdome exits.
- Ring: POSIX `shm_open` `/xrdome-<pid>-<n>` (fits macOS's 31 bytes), mode 0600, 1.25 MB: a
  4096-byte header, 4 RGBA8 frame slots, 3 16-bit depth slots, native byte order. Per slot a seqlock
  (number 0 while written, C11 atomics in C, aligned 64-bit memoryview words in Python). The helper
  removes the name as soon as it has mapped it; xrdome removes it too when it stops (ENOENT
  ignored) and replaces a stale one of the same name at start.
- Helper: `serve.py` gains `{"op": "video", "shm": ...}` per connection (a new ring replaces the
  old; closed flag or disconnect ends it); video comes before photos in the loop, which polls the
  ring every 4 ms while one is mapped. `video.py` holds the ring, a 30 Hz cap (90% of the period
  between starts, so a 30 fps film is never held back by polling), the 322x182 model call
  (normalised per frame like photos; smoothing is Task 8), statistics in the spike's terms, and
  `--bench` (`depth bench-video`).
- RGBA, not RGB, in the ring: 4-byte pixels are GL's fast readback path; the helper drops alpha.
- Tests: `test_framesink` (layout numbers, seqlock reads that are lapped or torn, depth back with its
  frame number, closed flag, shm names, `xrd_framesink_wanted`, and the C ring against
  `tools/xrdome_depth/video.py` through real shared memory via `tests/vring_peer.py`),
  `test_framesink_gl` (EGL surfaceless: scaled, top row first, never in the same step, numbering,
  no texture), `test_depthclient` (video line and replies), `test_timing` (`frames_out_us`),
  `tools/tests/test_depth_video.py` (ring, torn copies, shm, the 30 Hz cap, statistics, the bench
  with a fake clock, the video op over the socket, the launcher; the numpy depth function with a fake
  backend skips without numpy), `smoke_depth_video` under Xvfb with the fake helper: frames out and
  depth back with red at the top and blue at the bottom, 3D off reads nothing back and starts no
  helper, on then off, a helper refusing video, `--depth-helper off`, a stereo film.

Measured on the development laptop's desktop (Radeon 860M, RADV/radeonsi, Xorg, windowed 1280x360,
not the glasses; fake helper), a 1920x1080 30 fps H.264 film (vaapi), 1800 frames, twice each with
`--timing-log`: 3D off 2 and 0 missed vsyncs; 3D on 2 and 0, 900 frames out, 0 skipped, 899 depth
maps back. `frames_out_us` median 56 and 63 us, p99 293 and 308 us, max 4.4 and 1.4 ms: the max is
the frame `D` is pressed (the ring, FBO, PBOs and shader are made then; that frame was late once).
pose-to-swap is unchanged (the step runs before the pose sample).

Not run: the real 322x182 model behind the ring (no depth venv on the development machine), so no
helper Hz; the owner's check: `--timing-log` with 3D on against 3D off on the glasses, and the
helper's `video:` lines (or `depth bench-video` beside xrdome) on the laptop.

## Video depth in the shader, smoothing (issue #73, Task 8 of the depth 3D plan, 2026-11-19)

Spec `docs/superpowers/specs/2026-11-19-depth-3d-design.md` section 8, second half. A flat mono film
is now drawn in 3D from the helper's depth with section 6's pass unchanged (strength and plane keys
included). `docs/photos.md`, Depth for films; docs/tools.md, depth serve and depth flicker.
- Player (`src/main.c`, `video_depth`): each loop iteration, after the frame sink's step, the newest
  depth map in the ring that is newer than the last taken (`xrd_vring_depth_read`, the seqlock: a map
  being overwritten is left for the next frame) goes into the depth texture through
  `xrd_renderer_set_depth`, which now writes a map of the size the texture has with `glTexSubImage2D`
  (one 322x182 R16 copy, 116 KB, row flip on the CPU; no GPU wait, no new texture per map). The
  film is 2D until the first map; 3D off or a failed ring drops the depth with the ring. The depth's
  aspect check is skipped for films: the frame is scaled over the whole picture, so it lines up in
  picture coordinates whatever the film's aspect. The CPU time of the upload is in `frames_out_us`
  with the sending; the `depth: video stopped:` line gains the maps drawn and their age in frames
  (frame head of the ring at upload minus the frame the map was made from).
- Helper (`tools/xrdome_depth/video.py`): `SceneCut` (mean luma change over a 32x18 grid of sample
  points, threshold 0.15 of full scale), `Smoothing` (EMA of the percentile range with weight 0.5 for
  the newest; alpha for the depth's EMA: 0.5, or 1 at a cut and for the first map), both standard
  library only; `DepthPipeline` applies them with numpy (percentiles of the raw output, range
  smoothed, normalise and clip, blend with the previous smoothed map). One pipeline per film
  (`depth serve` makes a new one for each ring; the model stays loaded and warmed once).
- `tools/xrdome_depth/flicker.py` (`xrdome-tools depth flicker DIR`): the spike's metric
  (`scripts/flicker.py` of the depth spike: per-map min-max to [0,1], mean absolute per-pixel change
  between consecutive maps, 29 deltas over 30 frames, mean and max), over the raw model output, the
  helper's unsmoothed depth and its smoothed depth; `maps FILE.pgm...` measures finished maps.
- Tests: `tools/tests/test_depth_smoothing.py` (cut detector, range average, cut restart, the
  metric, the PGM reader, the `maps` command on a synthetic noisy sequence, the launcher: all
  standard library; the numpy pipeline tests (EMA weights, restart at a cut, range smoothing, flat
  depth, NaN, warm-up leaves no state, smoothing lowering flicker on noisy maps, the `frames` path
  with a fake backend) skip without numpy and OpenCV); `test_render` (`depth_rewrite_and_resize`:
  same-size uploads and size changes); `smoke_depth_video` cases V7 to V9: the fake helper's step
  depth draws a flat film's bars shifted by the same amount as a photo's (red -5.00 px, white +6.00
  px against the expected 5.12 at 2%), 3D off and 3D turned off again are the 2D picture.

Not measured: flicker on the real model (no depth venv on the development machine), so the target of
0.011 is unverified, as are the cut threshold and the 0.5 weight on real films (a threshold too low
resets the smoothing on fast action; too high lets a cut blend the old scene's depth for a few
frames). The owner's check (plan Task 9): a flat film in 3D on the glasses, `xrdome-tools depth
flicker` on a near-static shot, `--timing-log` with 3D on against off.

## Windows build (issue #80, 2026-11-19)
- `XRD_PLATFORM_WINDOWS` (`_WIN32`, name `windows`) in `src/platform/`: no X11 (SDL's windows
  driver, WGL, no X11 display for mpv), GL 3.3 core not forward-compatible, desktop fullscreen on
  the glasses, no XRLinuxDriver, hwdec `d3d11va-copy` (libmpv's GL render API is zero-copy on
  Windows only through ANGLE; xrdome uses WGL). `--require-hwdec` keeps its meaning and refuses the
  copy; its message and the warning say Windows copies by default. Unit tested on Linux with every
  other platform (`tests/test_platform.c`).
- Paths: `xrd_platform_user_path` (pure, tested for every platform): an absolute `XDG_CONFIG_HOME` /
  `XDG_STATE_HOME` wins everywhere; else `~/.config`, `~/.local/state`, or on Windows the known
  folders from `SHGetKnownFolderPath` (`%APPDATA%\xrdome\config.ini`, `%LOCALAPPDATA%\xrdome\files`).
  Records: `mkdir` without a mode, `_commit` for `fsync`, and the atomic replace through
  `MoveFileExW(MOVEFILE_REPLACE_EXISTING)` (`xrd_replace_file`).
- ffprobe: `src/detect/probe_win.c` runs it with `CreateProcessW` (only the pipe and NUL handles
  inherited), stdout read through a pipe with `PeekNamedPipe` and 1 ms waits on the process, the
  timeout by `TerminateProcess`. The command line is quoted by the C runtime's documented rules
  (`xrd_win_quote_arg`, `xrd_win_command_line`: pure, tested on every platform against a reference
  splitter, 3000 random argument lists). The POSIX runner is unchanged. `tests/fake_ffprobe.c`
  replaces the shell fake on every platform.
- UDP pose: Winsock (`WSAStartup` once, `closesocket`, `SO_RCVTIMEO` as a DWORD of ms). Timing:
  `QueryPerformanceCounter`; sleeps on a high-resolution waitable timer. `xrdriver.c` compiles and
  stays inert. Depth: the helper client is a failed one on Windows ("not available on Windows") and
  the frame ring cannot be created, so photos without cached depth show in 2D and films get no 3D.
- `packaging/windows/xrdome.manifest` (through `xrdome.rc`): UTF-8 active code page, long-path aware,
  per-monitor DPI aware (the window is placed in pixels, not scaled), Windows 10/11.
- CI `windows-build-test` (`windows-latest`, MSYS2 UCRT64): build with `-Dwerror=true` and unit tests;
  the smoke tests skip (no xvfb-run). Not run on Windows hardware: window placement on the glasses,
  the WGL context, `d3d11va-copy` at 60 fps, frame timing.

## Windows zip (issue #81, 2026-11-19)
- CI `windows-build-test` ends with `packaging/windows/build.sh --package-only` (the build above is
  packaged, not rebuilt), a smoke check from a plain `cmd` shell with only Windows' directories on
  `PATH` (`xrdome.exe --version`, `ffprobe.exe -version`, `ffprobe.exe` reading a sample), and an
  upload of `dist/xrdome-<version>-win64.zip` as the artifact `xrdome-win64`. Artifact only: no
  release, no tag. `ci.yml` also runs on `workflow_dispatch`.
- `packaging/windows/collect.py` (standard library; tests in `tools/tests/test_windows_collect.py`):
  `ntldd -R` on `xrdome.exe` and `ffprobe.exe`, keeps the DLLs in `/ucrt64/bin` (never Windows' own),
  plus `SDL3.dll`, which sdl2-compat's `SDL2.dll` loads at run time and no import table shows. Each
  file is mapped to its package through pacman's local database (what `pacman -Qo` reads; pacman is
  an MSYS program and would be given Windows paths). `LICENSES/` gets each package's files from
  `share/licenses/` (and COPYING-like ones from `share/doc/`); MSYS2's FFmpeg and mpv ship none, so
  the FSF texts of GPL-3.0, LGPL-2.1 and LGPL-3.0 (`packaging/windows/licenses/`) are added when a
  bundled package's licence names them. `SOURCES.md`: xrdome's commit, each package's version, source
  archive (`mirror.msys2.org/mingw/sources/<pkgbase>-<version>.src.tar.zst`) and the ffprobe
  configure line.
- Guards: `--enable-nonfree` in `ffprobe -hide_banner -buildconf` stops the build (the configure line
  is printed); so does a bundled package whose licence is not on the open-source list. A bare
  `custom` licence passes only for the packages in `REVIEWED_CUSTOM` (someone read their text), so a
  new one stops the build until it is reviewed.
- Shared with the macOS bundle (`packaging/bundle_common.py`): the meson version, the non-free check,
  `ffprobe -buildconf`, COPYING and the sharpen shader's licence, the xrdome and ffprobe sections of
  SOURCES.md. The dependency walks, package managers and SOURCES.md tables differ and stay apart.
- `ffprobe` lookup (`src/platform/exepath.c`, the macOS helper extended): on Windows
  `GetModuleFileNameW` gives the executable's directory, then `<dir>\ffprobe.exe` is used when it
  exists and the directory is not one of the `;`-separated `PATH` entries (names compared without
  case, either slash, quotes allowed). The pick takes the platform as an argument, so Windows and
  POSIX rules are both unit tested on Linux; the Windows branch also ran in a Wine test.
- Zip size: about 75 MB (140 DLLs, 190 MB unpacked), measured on a local MSYS2 package set; the CI
  zip's size is in the PR description. Most of it is FFmpeg's own dependencies (ffprobe imports them all).
- Not run on Windows hardware: the zip on a clean machine with the glasses (window placement, 100%
  scale, SBS mode, d3d11va-copy, whether mpv needs anything loaded at run time that the DLL walk cannot
  see), `build.ps1` (winget, the first MSYS2 update) and the CI step's real `ntldd` output.

## Pose helper for VITURE's SDK (issue #64, 2026-11-19)
- `helpers/xrdome-pose-viture/`: a separate C11 program, MIT licensed (its own `LICENSE`), that reads
  the head pose through VITURE's Glasses SDK (2.4.2 read: `viture_glasses_provider.h`,
  `viture_device.h`, `viture_protocol_public.h`) and sends it as OpenTrack UDP to 127.0.0.1:4242.
  xrdome is unchanged and keeps its `udp-opentrack` source on every platform. Built only with
  `-Dviture_sdk_include=PATH` (new `meson.options`); the SDK library is loaded at run time
  (`dlopen`, `LoadLibraryW`), never linked, never in the repository, and neither are its headers
  (they say "all rights reserved" and the package has no redistribution licence): CI configures
  with `helpers/xrdome-pose-viture/tests/fake_include/`, stand-ins written for the tests, and a fake
  SDK (`tests/fake_sdk.c`) that exports the same 14 entry points.
- Calls used: `xr_device_provider_create(pid)`, `_initialize(h, NULL, NULL)`, `_start`,
  `register_imu_pose_callback`, `open_imu(VITURE_IMU_MODE_POSE, f)`, and on exit `close_imu`,
  `_stop`, `_shutdown`, `_destroy`; for `--sbs` `get_display_mode` and `set_display_mode`. Each is
  declared in `viture_loader.h` and checked against the header's prototype at compile time with
  `_Generic`, so a signature that drifts fails the build. All 14 symbols are in the 2.4.2
  `libglasses.so` (`nm -D`).
- Pose: the callback's `data` is `[roll, pitch, yaw, qw, qx, qy, qz]`, North-West-Up (x north, y
  west, z up). The helper uses only the quaternion and rebuilds yaw, pitch, roll in degrees with
  `q = Rz(yaw) * Ry(pitch) * Rx(roll)`, the order `pose_udp_opentrack.c` documents for the driver's
  angles: the header does not say what unit the callback's own angles are in. A change of basis
  check (NWU to GL) in `test_viture_bridge` reads the packets back through `pose_udp_opentrack`.
  The packet encoder is the helper's own (the parser in `src/pose/opentrack_packet.c` is GPL, and
  the helper must stay MIT); `test_viture_packet` round-trips it through that parser. Same for the
  XRLinuxDriver check (`xrdriver_check.c`, checked against `xrd_xrdriver_fresh` on the same inputs).
- `--sbs` reads the current display mode, sets `VITURE_DISPLAY_MODE_3840_1080_60HZ` (0x32), and
  restores the mode it read at exit, on Ctrl+C or SIGTERM too (the signal sets a flag; the session
  puts the mode back while the provider still runs). A glasses already in 3840x1080 (0x32, 0x35)
  are left alone. A SIGKILL or crash cannot restore it. Gen 2 glasses in native mode answer
  `NOT_SUPPORTED`; the helper warns and carries on tracking.
- Luma Ultra (Carina) is refused: its pose is polled with `get_gl_pose_carina`, not delivered by the
  IMU callback. The Beast's native mode is not handled either.
- The glasses are found by vendor 0x35CA on the USB bus (sysfs on Linux, SetupAPI on Windows, IOKit
  on macOS), filtered by `xr_device_provider_is_product_id_valid`; `--pid` overrides. SetupAPI and
  IOKit code is written from the platform documentation and built by CI only; the hardware-id parser
  and the sysfs scan are unit tested on Linux.
- The IMU frequency is the lowest supported that is at least twice `--rate` (default 250, so 500 Hz);
  the rate cap is a schedule in the callback, so a 500 Hz stream capped at 250 gives 250.
- Hardware checklist (owner): (1) Linux, XRLinuxDriver stopped (the helper refuses otherwise):
  `xrdome-pose-viture --lib .../libglasses.so`, then `xrdome --windowed pattern_sbs.mp4`; look left,
  up and tilt: the picture must follow with the right sign (else `yaw=-1`, `pitch=-1` or `roll=-1`
  on the `--pose` spec, and tell the helper's author, because the quaternion's meaning is then
  different from the one assumed). (2) `tools/ot_dump.py` while it runs: about 250 packets a second,
  no malformed. (3) Yaw drift over 5 minutes, glasses still, the same way as the Task 10 numbers
  (0.02 degrees in 5 minutes with XRLinuxDriver, "Tracking, drift and EDID" above). (4) `--sbs`:
  the glasses switch to 3840x1080 and come back on Ctrl+C; whether they re-enumerate their display
  and how long that takes. (5) The Windows desktop: the SDK's `libglasses.dll` and helper built from
  the same sources, head tracking into `xrdome.exe`, `--sbs`. (6) macOS if it is at hand. (7) Whether
  `xr_device_provider_create` needs the USB permissions XRLinuxDriver's udev rule gives.
- Not known: the callback's own angle units (unused); whether the quaternion is body-in-world as
  assumed (checklist 1); whether the SDK's yaw is anchored or free (xrdome's recenter handles an
  offset either way); the default library name on Windows and macOS (`libglasses.dll`, `.dylib`,
  from the SDK documentation's text).

## Releases (2026-11-19)
- Releases: v0.1.0 published 2026-11-21 (https://github.com/SDNick484/xrdome/releases/tag/v0.1.0),
  from the draft the tag's CI run made; the tarball was checked, built and tested from its unpacked copy first.
- v0.1.0, Linux only: merged in PR #91 (`6f7740d`) and tagged `v0.1.0` on that commit; the tag's CI run
  made the draft release (tarball and `.sha256sum`). `CHANGELOG.md` holds the release notes (its 0.1.0
  heading said `unreleased` until 2026-11-21, when it was dated 2026-11-19); `docs/releasing.md` has the
  Releases section and the maintainer checklist.
- CI `dist` (ubuntu-24.04): `meson dist --no-tests`, checks of the tarball's contents (build files
  present; no build output, libraries, VITURE SDK headers or FFmpeg/mpv), then the tarball is
  unpacked elsewhere and built and tested with the seven smoke tests required to pass; uploads
  `xrdome-dist` (`.tar.xz` and `.sha256sum`). CI `release` (only for a pushed `v*` tag, the only
  job with `contents: write`): checks the tag against `meson.build` and that CHANGELOG has a section for
  it (a warning if it is still undated), then `gh release create --draft` with that section as the notes. Draft only.
- Packaging: `packaging/gentoo/media-video/xrdome/xrdome-0.1.0.ebuild` (`~amd64`) with a `Manifest`
  and `packaging/arch/PKGBUILD` with the tarball's sha256 (`b895efed...d658e`, 500220 bytes), both
  from the release tarball. The Arch CI job only checks the PKGBUILD's syntax and that its version
  and the ebuild's name match `meson.build`.
- Not verified: the ebuild has not been built with `emerge` and its `Manifest` line was computed by
  a script, not `ebuild manifest`. The PKGBUILD was first built with `makepkg` on 2026-11-21 (see
  Packages below).
  The tag first pointed at `897d254` (before #91) and was moved to `6f7740d` before the release was made.
- Not in the release: the macOS and Windows builds, depth 3D for films (issue #74, the glasses run),
  the VITURE pose helper (its hardware checklist), and depth 3D Task 6 (issue #71).

## Packages: Debian/Ubuntu, AUR, optional cmocka (2026-11-21)
- meson option `tests` (feature, `auto`): cmocka only for the unit tests. Checked: `disabled` leaves
  9 tests (`version`, the seven smoke tests, `viture_process`), `enabled` and `auto` with cmocka 43;
  in an Ubuntu 24.04 container without cmocka, `auto` configures with 8 (no helper) and `enabled`
  fails with "Dependency "cmocka" not found". The live ebuild passes `$(meson_feature test tests)`;
  `pkgcheck scan` is clean for both ebuilds. Neither ebuild has been built with `emerge`.
- `packaging/debian/` (0.1.0-1), built from the v0.1.0 tarball with `dpkg-buildpackage` in
  `ubuntu:24.04` and `debian:trixie` containers: 28 unit tests OK, the 7 smoke tests skip (no
  xvfb-run, on purpose), 0 failures; installed with apt, `xrdome --version` ran. Depends:
  `libmpv2`, `libsdl2-2.0-0`, `libepoxy0`, `ffmpeg`. Lintian: no errors; warnings `no-manual-page`
  and `initial-upload-closes-no-bugs` (Debian ITP only). CI job `deb` repeats this on Ubuntu 24.04.
  No PPA upload yet (needs the owner's Launchpad account and key).
- `packaging/arch/PKGBUILD` built with `makepkg -s` as a normal user in `archlinux:latest`: all 35
  tests OK in `check()`, including the Xvfb smoke tests; installed with pacman, `xrdome --version`
  ran. namcap's licence error (MIT needs its text) fixed by installing AMD's notice as
  `/usr/share/licenses/xrdome/LICENSE-MIT-AMD`, and `glibc` added; left: `ffmpeg` "may not be
  needed" (it is, for ffprobe at run time) and the debug package's build-id symlink (a namcap false
  positive). `.SRCINFO` generated by `makepkg --printsrcinfo`; CI checks it. Not on the AUR yet
  (needs the owner's account); the name `xrdome` was free on 2026-11-21.
- Not run on glasses from either package.

## Android: VITURE protocol check H5 (2026-11-25)
- Spec `docs/superpowers/specs/2026-11-25-android-vr180-design.md` 4.1, plan Task 7. Owner's run on the laptop,
  XRLinuxDriver stopped (`rc-service --user xr-driver stop`), elasticjava/viture-v2 at its 2026-11-16 head.
- `lsusb -d 35ca:`: `35ca:101d VITURE Pro VITURE Pro XR Glasses`. Interfaces: 0 HID (`0x81` IN, `0x01` OUT, 64 B),
  1 HID (`0x82` IN, `0x02` OUT, 64 B), 2 CDC (`0x86`), 3 CDC data (`0x03`, `0x85`). The Pro 2 XR viture-v2 was
  measured on has one HID interface.
- viture-v2's command line only looks for product `0x1301` (`PID_PRO2` in `src/main.rs`), whatever its README
  says; with it patched to `0x101D`, `probe` found the usbfs node but no hidraw node (the only hidraw node was the
  touchpad; XRLinuxDriver had probably detached the kernel's HID driver), and `pose 5 usbfs` (interface 0)
  ended `could not start the IMU: timed out`.
- Result: fail. The Pro XR does not speak the Gen2 protocol. Owner's decision: port mgschwan's MIT
  `viture_connection.c` (Gen1, derived from a decompiled SDK), with a companion app holding VITURE's AAR as the
  fallback if VITURE objects (spec 4.1). Next check: H5b after Task 7 (the backend on Linux).

## Android Task 1: `libxrdome-core` (issue #102, 2026-11-25)
- Spec 5.1, plan Task 1. A Linux-only refactor: no file moved, no behaviour meant to change.
  `src/core_sources.txt` lists the 16 core files; `meson.build` reads it (`fs.read`) into the static
  library `xrdome_core` (m, threads, epoxy, rt, win_deps; no SDL, mpv or EGL) and `xrdome_core_dep`;
  `xrdcore` keeps the front end (config, display, driver, video, probe, session, imagedir, platform,
  exepath, depthmap, depthclient, framesink, the SDL key-repeat helper) and links the core.
- Split out for the core: `platform/paths.{c,h}` (the platform, absolute paths, per-user paths,
  `xrd_mkdir`, `xrd_replace_file`; `platform.h` includes it), `detect/probe_info.{c,h}` (`xrd_probe`
  and `xrd_probe_zero`; `detect.h` includes it, not `probe.h`), `devices.{c,h}` (the built-in device
  table, `viture-pro` 43 deg and `xreal-air2pro` 45 deg, out of `config.c`) and
  `app/controls_keys.{c,h}` (`xrd_key_repeats`, SDL, front end only).
- New tests: `devices` (cmocka), `core_link` (cmocka; built against `xrdome_core_dep` and cmocka
  only), `core_clean` (`tools/check_core.py`: no `mpv/`, `SDL`, `X11/`, `GL/glx` or `epoxy/glx`
  include in a core file or a `src/` header it reaches) and `tools/tests/test_check_core.py`.
- Laptop (no cmocka, no xvfb-run): `-Dwerror=true` build clean, also with the CI's
  `-Dviture_sdk_include` stand-in headers; `version`, `core_clean` and `viture_process` pass, the
  seven smoke tests skip. The cmocka tests and smoke tests are proven by CI only. Outside the gate, a
  local cmocka stand-in ran `test_core_link` linked against `libxrdome_core.a` with only epoxy, libm
  and pthread (8/8), and `devices`, `controls`, `config`, `platform`, `filestate`, `probe`,
  `detect`, `probe_run`, `values`, `projection`, `quat`, `tracker`, `timing`, `opentrack`, all
  passing.
- Step 6, `grep -rln -E 'X11|glX|Xlib|mpv'` over the 16 listed files: `src/render/renderer_gl.c`
  only, a comment (line 225, "The unpack state is mpv's as much as ours"). Their headers: no match.
- Step 8, the timing comparison (owner, before merge): **not yet run**. On AC, `balanced`,
  `test2.mp4`, same zoom, alternate `main` and this branch three times each with
  `./build/xrdome --frames 3600 --timing-log run-N.csv test2.mp4`; pass when every branch run's
  missed vsyncs are at most `main`'s worst plus 4. The six numbers go here.
- Step 9, H5: recorded above (PR #114).

## Android Task 2: shared shader and renderer on GLES 3.0 (issue #103, 2026-11-25)
- Spec 5.3 GL, plan Task 2. `tools/embed_shaders.py` now emits, for every `NAME=file`, the unchanged
  `XRD_SHADER_NAME` and `XRD_SHADER_NAME_ES` (`#version 330 core` becomes `#version 300 es`, then
  `precision highp float/int/sampler2D`); a source not starting with `#version 330 core` exits 1
  naming the file. `src/render/gl.h` picks `<GLES3/gl3.h>` on Android, libepoxy elsewhere, and
  `xrd_gl_is_es()` (Android: true; else `!epoxy_is_desktop_gl()`).
- `xrd_renderer_new` compiles the `_ES` sources (eye and sharpen programs, the vertex shader too) when
  `xrd_gl_is_es()`. On ES `xrd_renderer_set_depth` returns false with "depth 3D needs desktop GL"
  (a NULL map, which only drops, still returns true), `has_depth` stays false and the
  `GL_FRAMEBUFFER_SRGB` disable is skipped. The desktop path is unchanged: same shader text, same calls.
- New tests: `tools/tests/test_embed_shaders.py` (`test_es_rewrite`, `test_bad_version_fails`) and
  `test_render_gles` (cmocka; surfaceless EGL, `EGL_OPENGL_ES_API`, an ES 3.0 context; exit 77 when
  Mesa cannot make one): `hequirect_centre_colour`, `sbs_eyes_not_mixed`, `flat_corners_black`,
  `depth_refused_on_es`, plus `is_es` and a sharpen/dither draw (compiles the ES sharpen program).
  The expected colours are the desktop test's, within +-2 per channel.
- Laptop (no cmocka): gate builds clean with `-Dwerror=true`, pytest passes. A local cmocka stand-in
  ran `test_render_gles` on this machine's Mesa llvmpipe (ES 3.2): all 6 pass, no skip. The cmocka
  test as built by meson, and its running in the Ubuntu and Arch jobs, are proven by CI only. No Android
  device was involved: the Adreno/Mali driver's shader compiler is untested (owner steps H2 onward,
  not yet run).

## Android Task 3: Android project, CMake core build, CI (issue #104, 2026-11-25)
- Spec 5.2, plan Task 3. `android/` is a Gradle project (AGP 8.7.3, Kotlin 2.0.21, Gradle 8.9 wrapper
  with the distribution's SHA-256 pinned; `minSdk 33`, `arm64-v8a` only, debug-signed, package
  `io.github.sdnick484.xrdome`). `app/src/main/cpp/CMakeLists.txt` reads `src/core_sources.txt` with
  `file(STRINGS)` (same filtering as meson), runs `tools/embed_shaders.py` for `shaders_gen.h`, builds
  `xrdome_core` static and links `libxrdome_jni.so` with `GLESv3 EGL android log`. `XRD_VERSION` is
  parsed from `meson.build`; `NativeBridge.version()` returns it and `MainActivity` shows it.
- `oes_copy.frag` (ESSL 3.00, `samplerExternalOES`, `u_st`) is added but not yet compiled into the app
  (the copy pass is Task 5/6). `embed_shaders.py --dump-es DIR` writes the `_ES` text of each source
  for glslang; `android.yml` runs `glslangValidator` on the ES vr180 vertex and fragment, sharpen and
  `oes_copy.frag`. On this laptop glslang's `-S frag` accepted `oes_copy.frag` including
  `samplerExternalOES`; whether the CI's glslang does is shown by the workflow.
- Found while checking the CMake build with host gcc and `-D__ANDROID__` against Mesa's GLES3
  headers: `renderer_gl.c` names `GL_R16` and `GL_FRAMEBUFFER_SRGB`, which `<GLES3/gl3.h>` lacks.
  `src/render/gl.h` now defines them under `__ANDROID__` (code the ES path never runs).
  The core then compiled with `-Wall -Wextra` and no warnings. The NDK's clang and bionic are untested.
- Gradle wrapper: `gradle-wrapper.jar` is the official one from the `v8.9.0` tag of `gradle/gradle`
  (SHA-256 498495120a03b9a6ab5d155f5de3c8f0d986a449153702fb80fc80e134484f17); the workflow also
  runs `gradle/actions/wrapper-validation`.
- Laptop: pytest passes (including `test_dump_es`); the gradle build, lint, the Kotlin test and the
  APK are proven by the `android` workflow only (no JDK or Android SDK here).
- Owner steps, not yet run: install the CI APK with `adb install` on the Pixel 9 Pro XL and confirm it
  opens and shows the version.

## Android Task 4: device report (issue #105, 2026-11-25)
- Spec 5.3 Device report, plan Task 4. `MainActivity` has a "Device report" button that opens
  `ReportActivity`, which shows `DeviceReport.collect(context).toText()` and writes it to
  `getExternalFilesDir(null)/device-report.txt`. Sections in order: `[build]`, `[decoders]` (H.264, HEVC,
  AV1 decoders: name, hardware or software, `isSizeSupported(4320,2160)`, `areSizeAndRateSupported` at 30
  and 60, as yes/no), `[displays]` (id, name, flags, presentation category, every mode), `[gl]`
  (`GL_RENDERER`, `GL_VERSION`, `GL_SHADING_LANGUAGE_VERSION`, the essl3 OES extension; from a pbuffer
  ES 3.0 context in `gl_info.c` via `NativeBridge.glInfo()`), `[usb]` (vendor:product, product name; no
  serial, no permission asked).
- Checked here: `gl_info.c` compiles with host `gcc -std=c11 -Wall -Wextra -Werror -fsyntax-only` against
  the EGL and GLES3 headers. The Kotlin, `DeviceReportFormatTest`, lint and the NDK build are proven by the
  `android` workflow only.
- Owner steps, not yet run: H1 on the Pixel 9 Pro XL, Pixel 10 Pro XL and S24 Ultra, each with the Viture
  and then the XREAL attached (also the `[displays]` half of H3 and the `[usb]` half of H5): open
  "Device report", `adb pull` the file. Look at: `[decoders]` for a hardware `video/avc` and `video/av01`
  with `size4320x2160=yes` and `rate30`/`rate60`; `[displays]` for a presentation display with a
  `3840x1080@60` mode (and what shows in 1080p); `[gl]` for the renderer and `essl3 ... yes`; `[usb]` for
  the glasses' ids. If the Pixel 9 Pro XL has no hardware decoder for 4320x2160 H.264 or AV1, stop before
  Task 6 (#107 is rewritten first). Results go here.

## Android Task 5: presentation on the glasses, render loop, test pattern (issue #106, 2026-11-25)
- Spec 5.3 Display, GL, Frame pacing; plan Task 5. `play/DisplayPicker.kt` picks a presentation display
  with a mode of exactly 3840x1080 at 59-61 Hz (nearest 60 if several): `Ok(displayId, modeId)`,
  `NoExternal`, or `NotSbs(currentW, currentH)`. `MainActivity` has "Show test pattern": it shows a
  `GlassesPresentation` (asks for that mode with `preferredDisplayModeId`, a full-window `SurfaceView`)
  whose `RenderThread` (a `HandlerThread` driven by `Choreographer`) owns the EGL context and the
  `SurfaceTexture`. Phone messages: "Connect the glasses", "Switch the glasses to 3D (SBS) mode: they
  are at WxH", "Glasses disconnected" (display removed), and the GL error if setup fails.
- Native: `render_loop.c` (EGL ES 3.0 context and window surface, swap interval 1, the shared
  renderer, the OES texture, a tracker on the `null` pose source: identity view) and `oes_copy.c` (the
  copy pass: OES -> `GL_RGB10_A2` of the video size, `GL_LINEAR`, clamp to edge, one level).
  `oes_copy.frag` is now compiled in: CMake writes it into `oes_copy_frag_gen.h` at configure time
  (`embed_shaders.py` only rewrites 330 sources, so it is left alone); the vertex stage is
  `XRD_SHADER_VR180_VERT_ES`. The eye pass: half-equirect, SBS left-right, zoom 1, no trilinear, the
  device FOV from the core's table (`NativeBridge.deviceFov("viture-pro")`, 43 degrees). The test
  pattern is a 512x256 RGBA8 texture with `test_render.c`'s quadrant colours.
- Decisions (beyond the plan's interface, which is kept): `draw` returns `Boolean` (false on a failed
  copy or swap, reason in `lastError()`, and the thread stops drawing); `setVideoSize(h, w, height)` and
  `deviceFov(name)` are added, because an OES texture's size cannot be queried in GLES 3.0 (Task 6
  calls `setVideoSize` from the player's video-size callback); `lastError()` is per thread (read on the
  render thread). Nothing is drawn unless the surface is 3840x1080, and if the display has not reached
  the SBS mode 3 s after the Presentation asked, the pattern is taken down with the "Switch the
  glasses to 3D" message: no squashed picture (Review Focus). A display change away from SBS after it
  was reached gives that message too, not "Glasses disconnected". No `eglTerminate` on teardown (the
  default display is shared with the device report). `onStop` dismisses the Presentation, which
  releases the SurfaceTexture, EGL and the thread before its surface goes (waits up to 2 s).
- Frame pacing: Choreographer ticks at the phone screen's vsync, not the glasses'; the swap (interval
  1) blocks on the glasses' queue, so a 120 Hz phone screen does not draw the glasses faster. Whether
  that leaves the frames evenly paced is for the timing log (Task 6).
- Checked here: the four C files compile with host gcc and clang `-std=c11 -Wall -Wextra -Werror
  -fsyntax-only -D__ANDROID__` against the EGL/GLES3 headers (stub `jni.h` and `android/*.h`, written
  for the check only). The copy pass's program compiles, links and draws (no GL error, framebuffer
  complete) on Mesa llvmpipe in a surfaceless ES 3.0 context; glslang accepts `oes_copy.frag`. The
  Kotlin, `DisplayPickerTest`, lint and the NDK build are proven by the `android` workflow only (no JDK
  or Android SDK here). Nothing has run on a phone.
- Owner steps, not yet run:
  - H2 (GLES on the three GPUs): the test pattern on the Pixel 9 Pro XL (Mali), Pixel 10 Pro XL
    (PowerVR) and S24 Ultra (Adreno) with the Viture, then the XREAL. Check: the pattern shows (no
    "cannot show the picture" error, i.e. the copy pass and eye shader compile), each eye shows its
    own four colours (docs/android.md), the eye halves are not mixed, no seam at the SBS split, and it
    matches the Linux renderer's picture of the same quadrants. Note `GL_RENDERER` and the driver
    version from the device report (the Pixel 10's launch driver was reported to stay at idle clocks).
  - H3 (DeX and desktop mode): with each pair on each phone, whether Samsung DeX starts on plug-in and
    whether the pattern still reaches the glasses with DeX on and off; the same for the Pixels'
    desktop mode / connected-displays setting. Also: whether `preferredDisplayModeId` switches the
    glasses from 1920x1080 to 3840x1080 on its own (if 1080p mode lists a 3840x1080 mode at all), and
    what the phone says when the glasses are in 1080p; unplugging with the pattern up shows "Glasses
    disconnected" without a crash; Home and back returns to the main screen with the pattern down.

## Decisions
- Architecture B: one C11 process, libmpv render API + own GL projection pass. meson, cmocka.
- Pose sources are pluggable; XRLinuxDriver is reached over UDP only. Never link or ship
  `libglasses.so` or XRLinuxDriver source. The Viture SDK is not a backend inside xrdome: it is a
  separate MIT program, `xrdome-pose-viture`, that sends OpenTrack UDP (issue #64), because loading
  a proprietary library into the process that holds GPL libmpv and FFmpeg is a licence problem that
  `dlopen` does not clearly avoid, while separate programs over a socket are separate works.
- No second process opens the glasses while XRLinuxDriver runs (it holds their USB interfaces). The
  Viture SDK (`xrdome-pose-viture`) runs only in place of XRLinuxDriver, never beside it: both open
  the same USB interfaces, so the helper refuses to start on Linux while the driver's heartbeat is
  fresh. Device controls such as dimming and brightness go through the driver (issue #22), or
  through the SDK when it is the one running (not built).
- Platform: one code base; the platform is chosen at build time and Linux behaviour is the
  reference. macOS is experimental (build and unit tests in CI only).
- mpv default profile is `fast`; the default mpv render path saturated the integrated GPU.
- Convergence is demoted to the `--convergence` flag (still remembered per file); the `,` `.` and
  `/` keys are removed. Reason: on the Viture Pro XR up to 2 deg made no significant difference
  and 5 deg put the scene out of focus, while zooming out is what fuses close subjects. The flag
  stays because the finding is one viewer on one pair of glasses and convergence was not tried on
  the XREAL Air 2 Pro.
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
  `Shift+R` (full recenter with the head level) corrects it: confirmed by the owner on
  2026-11-14 ("works great"). A gravity-level start or a "level" key would need the driver's world frame
  calibrated (see README, Tracking notes).
- XRLinuxDriver 2.11.9 (2026-11-14): on the Viture Pro XR it hangs on 3 of 4 `sbs_mode` requests
  (also without xrdome); it aborted with glibc heap corruption on two XREAL unplugs and, per its
  log, once after a Viture reconnect (2026-11-13) and once when a second instance was started.
  These are the driver's; the owner is taking them upstream.
- The XREAL Air 2 Pro must lie still while the driver calibrates after a plug-in (about 16 s,
  until "Device calibration complete"): moved during it, its yaw slides 3 to 17 degrees over the
  next half minute. The Viture's yaw drifted 0.02 degrees in 5 minutes at rest.
- A 4K AV1 film has a load spike every 32 display frames (16 source frames) with and without
  deband; it is where deband's misses fall. Not explained.
- Pose gaps of about 0.6 s (`pose stale`, `pose resumed`) in 6 of 10 timing runs on the Viture.

## Open
- Untested: automatic 8-bit FBO fallback; the Viture unplugged during playback (the XREAL's unplug
  works, see Hardware run 2026-11-14).
- Unanalysed: the remaining 0.6-1.3% late frames. A clue (2026-11-14): on a 4K AV1 film the late
  frames fall on a spike every 32 display frames.
- Per-file settings: layout, swap-eyes, content FOV and convergence records, `--forget` and the
  bad-record paths on the glasses; resume's near-start rule and the window close.
- Recenter: `Shift+R` levels a tilted picture (2026-11-14). A gravity-level start or a
  "level" key is optional now.
- XRLinuxDriver 2.11.9: the Viture `sbs_mode` hang, the crashes on unplug and connect, the XREAL's
  slide after a calibration done while moving, a pending request dropped at start (all in Hardware
  run 2026-11-14 and Findings); the owner is taking them upstream. Until the hang is fixed, switch
  the Viture to SBS with its button. Open on xrdome's side: whether its SBS switch (an IMU restart)
  causes the XREAL's slide too.
- Flat projection and format detection: shimmer with and without mipmaps on content with fine
  detail; a real half-SBS film, and `M` into half SBS on one; subtitles on the glasses (also with
  `--mpv-scale`); top-bottom if such a file turns up. Full SBS works (2026-11-14).
- Curved screen: issue #41 (`Z` keeps the curve, the owner's call from the run); a timing pair.
- Enlarging and sharpening: issue #25 (a better eye-pass filter); `--sharpen` (issue #26) is
  built, and its timing pair on the 8K VR180 file and the owner's look at 0.5 to 1 are open (see
  its section). The run's mpv-scaling and debanding numbers are in #25's and #26's bodies.
- Dimming and brightness keys wait on an XRLinuxDriver feature (control-file requests for
  brightness and the electrochromic film); the request is drafted in issue #22's PR for the owner
  to file. Keys `D` and `Shift+[` / `Shift+]` are reserved for it.
- Candidate next features, in suggested order: per-eye HUD (takes over `?` and `I`, which print
  to the terminal for now), GTK launcher. The Viture SDK pose helper is built (issue #64) and
  waits for its hardware checklist.
