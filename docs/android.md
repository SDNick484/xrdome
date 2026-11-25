# Android

An Android front end for xrdome is being built, VR180 first: it plays a side-by-side VR180 file
from a phone onto USB glasses (VITURE Pro XR first, then XREAL Air 2 Pro), sharing the C core with
the Linux player. The design is in
[the spec](superpowers/specs/2026-11-25-android-vr180-design.md) and the order of work in
[the plan](superpowers/plans/2026-11-25-android-vr180.md).

## What works so far

Nothing you can watch yet. The app is a skeleton: it opens and shows "xrdome" and its version,
which comes from the native library (the shared core's build), so the version matches the Linux
player's `xrdome --version`. Playback, the glasses display, head tracking and the file picker come
in later steps; each adds its behaviour here.

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
