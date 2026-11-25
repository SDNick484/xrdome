# Android front end, VR180 first: design

Status: approved by the owner on 2026-11-25. Plan:
`docs/superpowers/plans/2026-11-25-android-vr180.md`.

## 1. Intent

An Android app that plays VR180 side-by-side files from the phone on XR glasses with head tracking, so xrdome
works without the laptop. It is for the owner first, on their own phones and glasses; GitHub releases of the APK
come later. It shares the C core with the Linux player instead of being a second implementation.

**Success** (the owner's words: "a full film plays smoothly with head tracking"): on the Pixel 9 Pro XL with the
Viture Pro XR, a full-length VR180 film plays start to end with head tracking, with at most about 1% of
display frames late in the app's timing log (Linux manages 0.6%) and no visible freeze from pose gaps. The 1%
figure is a proposal the owner may change.

The films are the owner's own, **H.264 and AV1 at 4320x2160, played as they are** (no conversion step). 8K is
not a target. The Pixel 9 Pro XL is the acceptance phone; the Pixel 10 Pro XL and S24 Ultra run the hardware
checks and get fixes when cheap, but do not block a milestone.

## 2. Goals, non-goals, anti-goals

Goals:
- A `libxrdome-core` that the Linux player and the Android app both build from the same sources.
- An Android app (Kotlin + NDK/JNI) that plays a local VR180 SBS file on the glasses' external display at
  3840x1080 SBS, through the existing eye shader, with head tracking from the glasses over USB.
- Viture Pro XR first, then XREAL Air 2 Pro, both through open USB protocol code in the C core (no vendor SDK).
- Switching the glasses into and out of 3840x1080 SBS from the app.
- The phone as a touchpad remote with its screen black.

Non-goals (this spec):
- Flat video, photos, depth 3D, `S`/`P`/`M` layout and projection changes, subtitles, deband, mpv scaling,
  sharpening, curved screen, per-eye HUD. The core keeps them; the Android app does not expose them yet.
- Play Store or F-Droid distribution, release signing, a polished UI.
- Pose prediction (a separate planned feature) and latency work beyond measuring it.
- Running the new USB pose backends on Linux in place of XRLinuxDriver (possible later; not built here).
- Any vendor SDK in the APK (unless H5 fails; see 4.1).
- 8K files; a software decoder (see H1 for what happens if a phone has no hardware decoder for a target file).

Anti-goals (a failure even if it works):
- Any change in the Linux player's behaviour or frame timing from the core split.
- A second copy of the projection maths or the eye shader that can drift from the Linux one.
- A proprietary library in the APK or in the repository.

## 3. Constraints

- Same repository, new `android/` directory with its own Gradle build that compiles the C core through CMake
  (`externalNativeBuild`). The meson build stays the Linux, macOS and Windows build and must not change
  behaviour; its gate (`CLAUDE.md`) still passes on every commit.
- C11 for everything in the core; cmocka unit tests run by meson on Linux as now.
- A separate, path-filtered CI job for Android; existing CI jobs unchanged.
- Licence: xrdome is GPL-2.0-or-later. Media3 is Apache-2.0, which is compatible with GPLv3 but not GPLv2, so
  the APK as a whole is distributed under GPLv3 or later (the same reasoning the Windows zip already uses in
  `docs/platforms.md`). With no vendor SDK in it, nothing in the APK is non-free.
- Hardware: Pixel 9 Pro XL (Tensor G4, Mali GPU, primary), Pixel 10 Pro XL (Tensor G5, PowerVR DXT-48-1536
  per press reports), Samsung S24 Ultra (Snapdragon 8 Gen 3, Adreno). All three have USB-C DisplayPort alt mode
  (owner confirmed). Glasses: Viture Pro XR (primary), XREAL Air 2 Pro.

## 4. Approaches considered

### 4.1 Head tracking from the Viture on Android (the licensing question)

The project rule is "never ship `libglasses.so`, dlopen only", and since issue #64 the VITURE SDK lives in a
separate MIT program that sends OpenTrack UDP. On Android an SDK AAR would be packaged into an APK.

- **A. GPL additional permission for the VITURE AAR, one APK.** Lowest latency (JNI callback in process),
  fewest parts. Needs every copyright holder's consent (66 commits are authored as "Claude"; the copyright
  status of AI-written code is unsettled), binds future contributors, and VITURE's licence ("confidential trade
  secret", object code "as a component of Developed Programs"; its open-source notice lists a commercially
  licensed tracking library) is unverified for the Android AAR.
- **B. Separate MIT companion app holding the AAR**, sending OpenTrack UDP to `127.0.0.1:4242`; xrdome's existing
  `udp-opentrack` source needs no change. Mirrors issue #64. Two APKs, a foreground service, USB ownership in
  the companion; VITURE's redistribution terms still apply to the companion.
- **C. Open USB protocol, no SDK.** Fully open, F-Droid-eligible, reusable on Linux later. Unknown effort, and
  reverse engineering carries its own legal questions.

**Owner's choice: C.** Recon after the choice: `elasticjava/viture-v2` (MIT, active 2026-11-16) is a Rust driver
for VITURE's "Gen2" HID protocol with a written `PROTOCOL.md`, derived by its author from `usbmon` captures of
the official SDK. It reports pose at 119 Hz on a Pixel 9 under Android 17 through usbfs without root, the same
path an app gets from `UsbManager`. It was measured on a Pro 2 XR (`35CA:1301`); **whether the owner's Pro XR
speaks the same protocol is unverified** (hardware check H5, which gates milestone M4). xrdome writes its own C
backend from that document (crediting it; MIT), rather than linking the Rust crate, so the Android build needs
no Rust toolchain and the transport is shared with the XREAL backend.

Not legal advice: building from a third party's published protocol description keeps xrdome away from the
SDK's binary and its licence. **If H5 shows the Pro XR is not Gen2, the owner's decision is to explore using
VITURE's SDK instead**: M4 stops, and options A and B above (with H9, the SDK's terms) come back to the owner
for a choice; nobody captures the SDK's traffic, the step the VITURE licence question attaches to. H5 needs only
Linux, so it runs during M1, before any Android pose work is planned in detail. M6 (XREAL) does not depend on it.

**H5 result (owner, 2026-11-25): fail.** The Pro XR is `35ca:101d` and has two HID interfaces (0: endpoints
`0x81`/`0x01`; 1: `0x82`/`0x02`) plus a CDC serial pair; viture-v2 (its product id patched to `0x101D`, since its
command line only looks for `0x1301`) found the device over usbfs and timed out starting the IMU on interface 0.
The Pro XR speaks VITURE's older protocol, not Gen2.

**Owner's revised choice: C′, with B as the fallback.** Port `mgschwan/viture_virtual_display`'s
`viture_connection.c` (MIT, last push 2026-03-26), a reimplementation of the Pro XR's protocol whose comments say
it was taken from the **decompiled** SDK. The owner accepts that provenance for now; **if VITURE asks for its
removal, the project switches to option B** (a separate MIT companion app holding VITURE's AAR, sending OpenTrack
UDP to the unchanged `udp-opentrack` source). To keep that switch cheap, the Viture code stays in its own files
(`src/pose/viture_*`, `pose_viture_usb.c`) that nothing else depends on, and the app treats the pose source as a
spec string it can swap. Not legal advice; `docs/licence.md` states the provenance plainly.

### 4.2 How a decoded frame reaches the eye shader

`vr180.frag` samples the video with `textureLod(..., 0.0)` (implicit LOD is undefined inside its per-fragment
branches) and `textureSize`. External OES textures, per `GL_OES_EGL_image_external_essl3`, do not offer
`textureLod` and have no mipmaps (from the extension text; not tried on these GPUs).

- **A. Sample the OES texture directly** in a second variant of the eye shader using `texture()`. Zero-copy to
  the glasses, but a second shader to keep in step, a reliance on undefined LOD being harmless for a
  single-level texture on three GPU vendors, and no mipmaps ever.
- **B. Copy each new video frame once into an ordinary texture, then run the existing shader.** Matches Linux,
  where libmpv renders into a 10-bit texture the eye pass samples. One extra full-size write per video frame
  (24-30 per second, not 60): about 9 megapixels for 4320x2160, unmeasured on these phones.

**Owner's choice: B.** A stays available as an optimisation if the timing log asks for it.

### 4.3 Decode (agreed before this design)

Media3/ExoPlayer -> MediaCodec -> `SurfaceTexture` -> `GL_TEXTURE_EXTERNAL_OES` (no CPU copy), not libmpv on
Android. Metadata from `MediaExtractor` plus the existing frame-shape rules in `src/detect/detect.c`; no
`ffprobe` on Android.

## 5. Design

### 5.1 Milestone 1: `libxrdome-core` (Linux-only refactor)

`grep -rln -E 'X11|glX|Xlib|mpv' src/` matches 17 files, but the dependency is concentrated, not scattered:

| Where | Matches | What |
|---|---|---|
| `video/player_mpv.{c,h}`, `video/mpv_scale.{c,h}` | 184 | the mpv player; stays in the Linux front end |
| `main.c` | 49 | wiring; stays |
| `config.{c,h}` | 38 | `config.h` includes `player_mpv.h` for mpv option fields; the device table (`viture-pro` 43 deg, `xreal-air2pro` 45 deg) lives here |
| `platform/platform.h`, `display/output.h`, `app/session.*`, `app/imagedir.h`, `app/depthclient.h`, `render/depthmap.*`, `render/renderer_gl.c`, `video/framesink.frag` | 1-6 each | mostly comments; `depthmap` parses an mpv version string |

X11 and GLX appear almost only in comments and `platform.h`. The grep misses SDL, the other front-end
dependency: `app/controls.c` (`SDLK_*` in the key-repeat helper), `display/sbs.c`, `display/output.c`, `main.c`.

Already free of X11, GLX, mpv and SDL, and the core's content:
- `math/quat`, `render/projection`, `values`, `detect/detect.c` (the frame-shape and name rules; not
  `probe.c`, which runs `ffprobe`), `track/tracker`, `pose/pose.c`, `pose_null`, `pose_udp_opentrack`,
  `opentrack_packet`, `store/filestate`, `util/timing`.
- `render/renderer_gl` and the shaders. `xrd_renderer_draw` already takes a plain GL texture name, so it does
  not know about mpv. It uses libepoxy and GL 3.3 core; see 5.3 for GLES.

Work in M1:
- A meson static library for the core, linked by the existing `xrdcore` (or replacing part of it), built with
  no mpv, SDL or X11 dependency. Proposal (my call): files stay where they are; the core is a list, not a
  directory move, so history and the existing append points survive. The list lives in one plain text file
  that meson (`fs.read`) and CMake (`file(STRINGS)`) both read, so the two builds cannot drift.
- The device table moves out of `config.c` into the core so Android gets the same FOV per device.
- `filestate` reaches `platform.h` only for an absolute-path test; the core needs that small part of
  `platform` without the rest (Windows known folders, exe paths). Builder's call how to split it.
- `controls.c`: its view-state actions (zoom steps, clamps) are worth sharing; the SDL key-repeat helper is not.
  Builder's call whether to split it now or in M3.
- Tests: every existing unit and smoke test passes unchanged; a core-only test target proves the core links
  without mpv/SDL (for example a meson test that builds against the core library alone).

**M1 success:** the gate passes on Linux, macOS and Windows CI; and a timing comparison on the laptop. The
0-of-3600 baseline (`test2.mp4`, H.264 4320x2160, software decode, AC) is a single run, with no pose data, so it
is not enough alone. Proposal (my call): on AC, `balanced` profile, same file and zoom, alternate `main` and the
branch three times each with `--frames 3600 --timing-log`; pass if every branch run misses no more than
`main`'s worst run plus 4 vsyncs (0.1%). The owner runs it.

### 5.2 Milestone 2: Android project and CI

- `android/` holds a Gradle project (Kotlin, one `app` module) with `externalNativeBuild` CMake that compiles the
  core from the shared list plus a JNI layer under `android/app/src/main/cpp/`. Shaders are embedded by the
  same `tools/embed_shaders.py` (CMake runs it), with the GLES rewrite of 5.3.
- Proposal (my call): `minSdk` 33 (all three phones run far newer Android; it keeps `UsbManager`,
  `Presentation`, Media3 and GLES 3.0 well inside the supported range), `arm64-v8a` only, debug-signed APK.
- CI job `android-build` on `ubuntu-24.04`, run only when `android/**`, the core sources or list, the shaders,
  `tools/embed_shaders.py` or the workflow change: `./gradlew assembleDebug` (with lint and Kotlin unit tests),
  and the debug APK uploaded as an artifact for sideloading. Proposal: also compile the generated GLES shaders
  with `glslangValidator` (ES 300, and the OES copy shader with the `essl3` extension), the only shader check CI
  can do without a phone. Whether glslang accepts `samplerExternalOES` there is unverified.

### 5.3 Milestone 3: the smallest end-to-end slice (fixed orientation)

```
file picker (SAF) -> Media3 ExoPlayer -> MediaCodec -> SurfaceTexture (OES texture)
   -> copy pass (OES -> RGB10_A2 texture, once per new video frame)
   -> xrd_renderer_draw (shared vr180 shader, identity rotation)
   -> EGL window surface of a Presentation on the glasses' display, 3840x1080
```

- **Display.** The app finds the glasses through `DisplayManager` (presentation category), shows a
  `Presentation` on it, and requests the display's 3840x1080 mode. In M3 the owner switches the glasses to SBS
  with their own button. If no external display, or none at 3840x1080, the phone says so and does not play.
  Display added, removed or changed events end playback cleanly (the SBS switch in M5 causes them).
- **GL.** One EGL context (GLES 3.0) on the render thread, which owns the `SurfaceTexture`. The core renderer
  compiles against `<GLES3/gl3.h>` through a small header that picks epoxy on desktop and GLES on Android.
  `embed_shaders.py` gains an option that rewrites `#version 330 core` to `#version 300 es` and adds precision
  statements; the shader text is otherwise the same file. Depth (`GL_R16`) is not available in GLES 3.0 without
  an extension and depth is out of scope, so `xrd_renderer_set_depth` reports "not supported" on GLES. The copy
  pass is new, Android-only, and its shader is the only OES code.
- **Mipmaps** (my call): not in M3; the eye pass draws with `trilinear` off. A 4320x2160 VR180 picture is
  enlarged on a 1920x1080 eye at these FOVs, not shrunk, so Linux's eye pass does not use the chain for it
  either (`xrd_minify_ratio`). The H2 comparison with Linux is at zoom 1.
- **Frame pacing.** Draw once per display vsync (`Choreographer` on the render thread), latching a new video
  frame when `SurfaceTexture` has one, as Linux draws every vsync and uploads when mpv has a new frame.
- **Format check.** `MediaExtractor` gives width, height and the track's MIME; the core's frame-shape rules decide
  VR180 SBS. Anything else gets a message on the phone ("not a VR180 side-by-side file; flat video is not
  supported yet").
- **Phone screen.** After a file is picked: black, kept on, minimum window brightness, full-screen touchpad.
  In M3: tap play/pause, swipe left/right seek -10/+10 s, back stops. Volume keys work as normal.
- **Timing log.** The core's `util/timing` writes the same CSV as `--timing-log` to the app's external files
  directory, using the swap time after `eglSwapBuffers` and the same "late" rule (swap-to-swap > 20 ms at 60 Hz).
  The phone shows the missed count at the end. Builder's call whether `EGL_ANDROID_get_frame_timestamps` adds a
  present-time column; SurfaceFlinger adds about one frame that the swap time does not see.
- **Device report.** A screen (and a text file the owner can pull with `adb`) listing: decoders and their
  supported sizes and rates for H.264, HEVC and AV1 at 4320x2160 (`MediaCodecInfo`), displays and
  their modes, `GL_RENDERER`/`GL_VERSION`/extensions, USB devices (vendor, product). It is how the owner runs
  hardware checks H1-H3 and H5 without tools on the phone.

### 5.4 Milestone 4: Viture pose over USB

- New pose backend `viture-usb` in the core (`src/pose/pose_viture_usb.c`), behind the existing
  `xrd_pose_source` interface, so the tracker, recenter and staleness handling are unchanged. Ported from
  mgschwan's `viture_connection.c` (MIT notice and credit in the file and in `docs/licence.md`, with its
  provenance). The protocol as that file has it (from reading it; nothing here has been run):
  - Interface 1 (MCU, endpoints `0x82` IN / `0x02` OUT) carries commands and their replies; interface 0 (IMU,
    `0x81` IN) carries the pose stream. Reports are 64 bytes.
  - Command frame: `FF FE`, CRC-16 (2 bytes), length (2 bytes, = 12 + data length), 8 zero bytes, command id
    (2 bytes) at offset `0x0E`, 2 zero bytes, data from offset `0x12`; little-endian. CRC-16-CCITT, polynomial
    `0x1021`, initial value 0, over the length field and the `length` bytes after it. Always sent as 64 bytes.
  - Reply: the same frame; status in the first data byte (0 = success). mgschwan's code treats a header command
    id of 0 as "reply to the pending command" and notes the decompiled code compares it with the request's id
    instead; accept either.
  - IMU on/off: command `0x15`, one data byte, 1 on / 0 off.
  - IMU packet: starts `FF FC`, same layout; a 32-bit timestamp at offset 6; data at `0x12` holds roll, pitch,
    yaw as **big-endian** 32-bit floats, in degrees (mgschwan negates yaw). Euler angles, not a quaternion: the
    rotation order is assumed to be XRLinuxDriver's (`q = Rz(yaw) * Ry(pitch) * Rx(roll)`, NWU, as
    `pose_udp_opentrack.c` documents), since XRLinuxDriver reads the same glasses; the sign check confirms it.
- Spec string `viture-usb:fd=N` (Android: the app's descriptor) or `viture-usb:dev=/dev/bus/usb/BBB/DDD`
  (Linux: the backend opens the node itself), plus `yaw`, `pitch`, `roll` (±1). The Linux form exists so the
  owner can check the protocol on the laptop with the existing player before any Android pose work (H5b).
- Transport: on Android, Kotlin asks `UsbManager` for permission (device filter on vendor `0x35CA`), opens the
  device, claims interfaces 0 and 1 with force (detaching the kernel driver) and passes the descriptor down; on
  Linux the backend detaches and claims both itself. A reader thread in C does interrupt transfers on usbfs
  and keeps the newest pose; `latest()` stays non-blocking. The usbfs transport is its own small file so the
  XREAL backend reuses it.
- The sign check on glasses is the same as issue #64's checklist (look left, up, tilt).
- Touchpad gains double-tap (yaw recenter, `R`), long-press (full level recenter, `Shift+R`), swipe up/down
  (zoom, `[` `]`).
- Gate: H5b (the backend on Linux, above) before the Android wiring.

### 5.5 Milestone 5: SBS switching from the app

- At play, the app sets the glasses to 3840x1080 SBS through the protocol's display-mode message and restores
  the mode it read when playback ends or the app is backgrounded, like `xrdome-pose-viture --sbs`. A crash
  cannot restore it (as on Linux).
- The display re-enumerates on a mode switch; the app waits for the 3840x1080 display to appear (with a
  timeout and a message), then opens the Presentation.
- mgschwan's file has no display-mode command, so for the Pro XR the command is **unknown**. The builder looks
  for it in other open sources (licences checked; not XRLinuxDriver's source, project rule). If none documents
  it (my call): the glasses' own SBS button, already the path in M3, stays the Viture's way in; M5 is cut to the
  XREAL in M6 and comes back to the owner before anyone captures traffic.

### 5.6 Milestone 6: XREAL Air 2 Pro

- A second backend, `xreal-usb`, on the same usbfs transport, from open-source protocol work
  (`badicsalex/ar-drivers-rs`, MIT, active 2026-05; `enricoros/android-nreal`, BSD-3-Clause, last push 2023),
  credited the same way, plus its SBS switch. **Air 2 Pro support in that work is unverified.** If it is
  missing (my call): M6 may work from `usbmon` captures of the glasses with an open driver, because no vendor
  licence attaches to XREAL the way VITURE's does; XRLinuxDriver's source is still not copied (project rule).
- The XREAL must lie still while it calibrates after plug-in (about 16 s on Linux): the app shows "keep the
  glasses still" until the backend reports calibration done, if the protocol exposes it.

### 5.7 Per-file memory on Android

After M4 (the plan places it): `filestate` records in the app's files directory, keyed as on Linux by size
and the first and last 64 KiB, read through the descriptor the picker returns (the core gains a key function
taking an fd). Remembers what applies to VR180 here: zoom, swap-eyes, content FOV; restores at open, saves
at stop. The record's path label is the picker's display name.

## 6. Hardware checks (the owner runs these; results go in `docs/STATUS.md`)

- **H1 Decoders.** From the device report on each phone: are 4320x2160 H.264 and AV1 supported by a hardware
  decoder, at what frame rates (`areSizeAndRateSupported` 30 and 60); HEVC for reference. H.264 level 5.1/5.2
  allows 4320x2160 by macroblock count, but decoders often advertise a 4096 width limit (as the laptop's VA-API
  did). Run this first, during M2: if the Pixel 9 Pro XL has no hardware path for one of the two target
  formats, that comes back to the owner before M3 (a software decoder is a non-goal here). Until then the app
  tries to play and, on a decoder error, says which format failed.
- **H2 GLES drivers.** On Pixel 9 (Mali), Pixel 10 (PowerVR) and S24 Ultra (Adreno): `GL_RENDERER`, the `essl3`
  OES extension present, the copy pass and the eye shader compile, and a pattern file shows the same picture
  as on Linux (eye halves not mixed, no seam at the SBS split). The Pixel 10's launch GPU driver was reported
  to stay at idle clocks; note the driver version.
- **H3 External display and desktop modes.** With each pair of glasses on each phone: the glasses appear as a
  presentation display; their mode list includes 3840x1080@60 when they are in SBS (and what appears in
  1080p); whether Samsung DeX starts on plug-in and whether the Presentation still gets the display with DeX
  on or off; the same for the Pixels' desktop mode / connected-displays setting (Android 16+, developer
  options on some builds). The app needs a plain secondary display, not a desktop session.
- **H4 Phone screen.** Whether the Presentation keeps showing if the phone screen times out or is locked (my
  reading of the API is that it stops with the activity); battery drain with the black screen on.
- **H5 Viture protocol.** Done 2026-11-25: fail for the Gen2 protocol (4.1).
- **H5b Viture Gen1 backend on Linux.** After M4's C backend: XRLinuxDriver stopped, `xrdome --pose
  viture-usb:dev=/dev/bus/usb/BBB/DDD pattern_sbs.mp4`; the IMU starts, the pose follows the head with the
  right signs, about the rate XRLinuxDriver gives (365 packets per second measured). Then on Android: the USB
  permission flow, the forced claim, pose rate.
- **H6 XREAL protocol.** The same for the Air 2 Pro with ar-drivers-rs.
- **H7 Audio.** Where sound goes with the glasses connected (glasses speakers or phone), and whether it changes
  on the SBS switch.
- **H8 Full-film run.** The success test of section 1, with the timing log, and the phone's temperature and
  battery at the end (thermal throttling on a two-hour film is unknown).
- **H9 VITURE Android SDK terms.** Needed only if option B replaces C′ (4.1): whether the AAR exists, supports
  the Pro XR, and may be redistributed in a companion APK. Contact named on VITURE's licence page: bd@viture.com.

## 7. Unverified and unknown

- Whether mgschwan's protocol reading works (H5b), the Euler order, and the Pro XR's display-mode command; the
  XREAL Air 2 Pro in ar-drivers-rs.
- `textureLod` absence for OES textures and glslang's handling of `samplerExternalOES`: from documentation only.
- The cost of the copy pass on each GPU; the SurfaceFlinger frame of latency (stated, not measured here).
- Whether Android exposes 3840x1080 for these glasses on these phones (H3).
- Copyright status of AI-authored commits: moot for this design (no exception needed), noted for the record.

## 8. Left to the builder

The JNI surface and threading model beyond "render thread owns EGL and the `SurfaceTexture`"; Gradle and AGP
versions; how the core list file is laid out; the split of `platform` and `controls`; the device report's
layout; the touchpad gesture thresholds; whether the copy pass target is RGB10_A2 or RGBA8 (RGB10_A2 matches
Linux and is the default); file and test names, following the repository's conventions.
