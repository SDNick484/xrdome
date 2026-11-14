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
- CI (GitHub Actions, Ubuntu 24.04) builds with `-Dwerror=true` and runs the whole suite, failing if any of the three Xvfb smoke tests (`smoke`, `smoke_tracking`, `smoke_sbs`) skips.

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
- `packaging/gentoo/media-video/xrdome/` has `xrdome-9999.ebuild` (EAPI 8, `git-r3 meson`, no
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
- Not verified: the `B` key itself and the `I` line (the smoke test has no way to send keys); any
  effect on the glasses; GPU cost.
- Awaits the glasses: a timing pair, `--timing-log` each, `--deband off` against `on`, on the 4K
  film and on the 8K VR180 file (missed vsyncs; the integrated GPU is the constraint, and
  `fast` is the default profile for that reason); and the owner's judgement on a dark,
  low-bitrate file, toggling `B` on a still frame.

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

Verified in the sandbox (Ubuntu 24.04-like, mpv, software GL, Xvfb): unit tests for the record's
parse and format, the near-start and near-end rule, and the precedence; smoke cases M13 to M19
(no position without `--resume`; saved and restored with it, and playback begins at the saved
position; `--no-resume` and `--no-memory` leave the record byte-identical; an explicit start wins;
quitting in the first 30 s drops the position; a position past the end gives one note and starts at
0).

Awaits the glasses: resuming a real long film (hardware decode, seeking with `start` on a large
file, how long mpv takes to reach the position on an 8K file), and a real quit with `Q` or the
window close in the middle of a film (the sandbox quits by `--frames`).

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

Hardware check (not done; the owner, Viture Pro XR): play a flat film at zoom 1.5 and 2.5 and
press `C` to compare curved and flat: whether the curved screen is more comfortable to look
around, whether the bowed top and bottom edges look natural or distracting, whether R = 1 suits
or a larger `--screen-curve` (say 2) is better, and that `C`, `Z` and `L` print and act as the
README says. Unverified until then: everything about comfort; the look of a curved screen in 3D
(half SBS); frame timing with the curve on (the shader adds a square root and an arctangent per
pixel; not measured).

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

Awaits the glasses (hardware check, after merge; all of the real switch is unverified):
1. Reconnect the Viture Pro XR so it is in 1080p, start XRLinuxDriver, check
   `/dev/shm/xr_driver_state` has a current `heartbeat=`, `sbs_mode_supported=true` and
   `sbs_mode_enabled=false`.
2. `./build/xrdome --no-memory --frames 600 file.mp4`: expect
   `display: switched the glasses to SBS in N.N s` and normal playback; note N (is 8 s enough on
   Xorg/MATE?), and whether the picture lands on the glasses (MATE may place the new 3840x1080
   output elsewhere, or need a moment to settle).
3. At exit: `display: asked XRLinuxDriver to switch the glasses back to 1080p`, and the desktop
   mirror returns.
4. Run again with the glasses already in SBS: no request, plays at once.
5. Stop the driver and run with the glasses in 1080p: the old error, then
   `automatic SBS switch: XRLinuxDriver is not running (...)`.
6. `--sbs-switch off`: the old error only, nothing in `/dev/shm/xr_driver_control`.
7. Unplug during playback: no switch-back request (`ls /dev/shm/xr_driver_control` stays empty
   after exit).

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
  `Shift+R` (full recenter with the head level) corrects it: confirmed by the owner on
  2026-11-14 ("works great"). A gravity-level start or a "level" key would need the driver's world frame
  calibrated (see README, Tracking notes).

## Open
- Task 10: XREAL Air 2 Pro check (`--device xreal-air2pro`), yaw drift over 5 minutes on both
  glasses, EDID/`xrandr --props` capture, README hardware notes.
- Untested: unplug behaviour during playback (a disconnect between runs happened on 2026-11-13,
  see Findings), automatic 8-bit FBO fallback.
- Unanalysed: the remaining 0.6-1.3% late frames.
- Per-file settings: the items listed as not covered above, and resume (see its section).
- Recenter: `Shift+R` levels a tilted picture (2026-11-14). A gravity-level start or a
  "level" key is optional now.
- `no pose data` after a glasses reconnect: once (2026-11-13) it was XRLinuxDriver crashing at
  connect, and restarting the driver restored it; the earlier case was not checked.
- Automatic SBS switch (issue #18): the hardware check in its section above; the real switch,
  its timing and the switch back at exit are unverified on the glasses.
- Flat projection and format detection, after the hardware run: shimmer with and without
  mipmaps on content with fine detail; a real half-SBS film, and `M` into half SBS from mono;
  subtitles on the glasses; top-bottom and full SBS if such files turn up.
- Dimming and brightness keys wait on an XRLinuxDriver feature (control-file requests for
  brightness and the electrochromic film); the request is drafted in issue #22's PR for the owner
  to file. Keys `D` and `Shift+[` / `Shift+]` are reserved for it.
- Candidate next features, in suggested order: per-eye HUD (takes over `?` and `I`, which print
  to the terminal for now), Viture SDK backend (only in place of XRLinuxDriver, not beside it:
  see the dimming findings), GTK launcher.
