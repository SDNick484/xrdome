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
- Options and keys: see README.
- CI (GitHub Actions) builds with `-Dwerror=true` and runs the whole suite, failing if any of the three
  Xvfb smoke tests (`smoke`, `smoke_tracking`, `smoke_sbs`) skips, in two jobs: Ubuntu 24.04 (mpv 0.37)
  and an `archlinux:latest` container (gcc 16.2.1, mpv 0.41 / libmpv 2.5.0, `sdl2-compat` 2.32.74 on
  SDL3, Mesa 26.2 on 2026-11-14). Arch's SDL adds `-D_GNU_SOURCE=1` to the cflags, and its
  `xvfb-run` (T2 SDE's copy of an old Debian script) sleeps 3 s per start, so `smoke` takes about 315 s there; that job
  runs with `--timeout-multiplier 2`.

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
- Magnify mode draws the screen flat: there the screen stays at its zoom-1 size, which fits the
  display, and zoom narrows the view, so there is no large screen to bend. With a curve on, `Z`
  therefore also switches between curved and flat, and the two modes no longer show the same
  picture away from the centre.
- Head-locked screens curve the same way (the mapping is in head space when locked).
- Nothing beside or behind the viewer is drawn, as for the plane: an arc reaching past 90 degrees
  stops there (the widest, zoom 4 with R = 1, reaches 79 degrees).
- `xrd_minify_ratio` is unchanged: the cylinder touches the plane at the centre, where u and v
  change at the plane's rates (a unit test measures them on the curved mapping), and they only
  fall away from the centre, so the centre stays the worst case for the mipmap decision.
- The eye-edge and mipmap clamp margins act on uv after the mapping, so the curved mapping needs
  none of its own; the seam and trilinear eye-separation render tests run again on a curved
  screen and pass.
- `C` is not remembered per file, like the screen lock: it depends on the viewer, not the film.
  With `--screen-curve off`, `C` curves with R = 1.

What the sandbox verified (unit tests and both Xvfb smoke tests, software GL, this worktree):
- `xrd_flat_curve_dir_to_uv` (the C mirror): centre, the arc's ends, top and bottom, beside and
  behind, junk input; `off` gives the plane's result bit for bit.
- The shader against the C mirror on every pixel of both eyes, for radii 0, 1, 2.5 and 10 at two
  zooms and four head poses (a texture that encodes its own uv).
- A dumped flat2d frame at `--zoom 0.9 --screen-curve 1.0`: the side borders each in one column,
  the top and bottom borders 6 rows nearer the frame's edge at the sides than at the centre;
  `off` and magnify mode give straight borders and the frame of a run without the flag.

Run on the glasses on 2026-11-14 (see Hardware run 2026-11-14): worked well at 1.5 and about 2.5;
the owner prefers R = 1 to R = 2; `C`, `Z`, `L` and `I` as the README says; `C` in 3D (full SBS)
fine. The owner found `Z` flattening the curve did not fit: issue #41 reverses that decision. Still
unverified: a timing pair with the curve on (0 missed in one run with keys pressed), half SBS.

## Automatic SBS switch (issue #18)

When no 3840x1080 output is found at start, xrdome asks XRLinuxDriver to switch the glasses to
side-by-side, waits for the output and plays; at exit it switches them back. `--sbs-switch
auto|off`, ini `[general] sbs_switch`, default `auto`. Code: `src/driver/xrdriver.c` (the driver's
files: state parsing, request writing; nothing SBS-specific beyond the request text, so other
requests can reuse it), `src/display/sbs.c` (the decision, the wait, switching back), wired in
`src/main.c` before the window opens. README: Flags, Config file, Switching to SBS.

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
- The README's XREAL udev rule is what gives the driver access: the XREAL's hidraw and USB nodes
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

## Decisions
- Architecture B: one C11 process, libmpv render API + own GL projection pass. meson, cmocka.
- Pose sources are pluggable; XRLinuxDriver is reached over UDP only. Never link or ship
  `libglasses.so` or XRLinuxDriver source. A Viture SDK backend, if added, uses dlopen.
- No second process opens the glasses while XRLinuxDriver runs (it holds their USB interfaces).
  Device controls such as dimming and brightness go through the driver (issue #22).
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
- Enlarging and sharpening: issues #25 (a better eye-pass filter) and #26 (`--sharpen`, after #25);
  the run's mpv-scaling and debanding numbers are in their bodies.
- Dimming and brightness keys wait on an XRLinuxDriver feature (control-file requests for
  brightness and the electrochromic film); the request is drafted in issue #22's PR for the owner
  to file. Keys `D` and `Shift+[` / `Shift+]` are reserved for it.
- Candidate next features, in suggested order: per-eye HUD (takes over `?` and `I`, which print
  to the terminal for now), Viture SDK backend (only in place of XRLinuxDriver, not beside it:
  see the dimming findings), GTK launcher.
