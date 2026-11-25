# Android

An Android front end for xrdome is being built, VR180 first: it plays a side-by-side VR180 file
from a phone onto USB glasses (VITURE Pro XR first, then XREAL Air 2 Pro), sharing the C core with
the Linux player. The design is in
[the spec](superpowers/specs/2026-11-25-android-vr180-design.md) and the order of work in
[the plan](superpowers/plans/2026-11-25-android-vr180.md).

## What works so far

Nothing you can watch yet. The app opens and shows "xrdome" and its version, which comes from the
native library (the shared core's build), so the version matches the Linux player's
`xrdome --version`. It can put a test pattern on the glasses (below). Playback, head tracking and
the file picker come in later steps; each adds its behaviour here.

The "Device report" button lists what the phone offers: its decoders (H.264, HEVC, AV1) with whether each
takes 4320x2160 at 30 and 60 fps, its displays and their modes, the GL driver, and attached USB devices.
It also writes `device-report.txt`; read it from the computer with

    adb pull /sdcard/Android/data/io.github.sdnick484.xrdome/files/device-report.txt

### Test pattern on the glasses

"Show test pattern" puts a still picture on the glasses, the first thing the app draws there. It
checks the GPU path the films will take (the shared eye shader, on the glasses' display) before
there is a film to play.

1. Connect the glasses to the phone and switch them to 3D (SBS) mode with their own button (VITURE:
   the button at the front left, hold 2 seconds; the app does not switch them yet).
2. Press "Show test pattern". Each eye shows four solid colours meeting in the middle (left eye:
   red over blue on the left, orange over violet on the right; right eye: yellow over cyan, magenta
   over white), the same quadrant picture the Linux renderer's tests draw. The two eyes must not
   mix, and there must be no seam down the middle of the glasses' picture.
3. Press "Stop test pattern" to take it down. Leaving the app (Home, a call, the screen locking)
   takes it down too.

What the phone says instead:

- "Connect the glasses": no external (presentation) display was found.
- "Switch the glasses to 3D (SBS) mode: they are at 1920x1080": the glasses have no 3840x1080 mode,
  or did not reach it within 3 seconds of the app asking for it. Nothing is drawn on the glasses
  in 1920x1080, so you never see a squashed picture.
- "Glasses disconnected": the glasses' display went away while the pattern was up.
- "The glasses cannot show the picture: ...": the GL setup failed (the reason follows); please
  report it with the device report.

The field of view is the VITURE Pro XR's (43 degrees, from the shared core's device table); there
is no head tracking yet, so the picture stays fixed straight ahead.

Requirements: Android 13 or later (`minSdk 33`), a 64-bit ARM phone (`arm64-v8a` only).

## Install a CI build

Every pull request and every push to `main` that touches `android/`, the shared core or its shader
generator runs the `android` workflow. It builds a debug-signed APK and uploads it as the artifact
`xrdome-android-debug`.

1. Download the artifact from the workflow run on GitHub and unzip it: it holds `app-debug.apk`.
2. On the phone, enable Developer options and USB debugging, connect it to the computer.
3. `adb install -r app-debug.apk`
4. Open "xrdome". It shows `xrdome 0.1.0` (or the current version).

The APK is signed with the Android SDK's debug key, so it is for sideloading only.

## Building it yourself

You need JDK 17 and the Android SDK with NDK 27.2.12479018 and CMake 3.22.1 (the versions the
workflow installs), and Python 3 on the build host for the shader generator.

    cd android && ./gradlew assembleDebug lintDebug testDebugUnitTest

The native part is `android/app/src/main/cpp/CMakeLists.txt`. It reads the same
`src/core_sources.txt` as the meson build, runs `tools/embed_shaders.py` for the shaders and the
version comes from `meson.build`. To add a core source file, add it to the list, not to CMake.
