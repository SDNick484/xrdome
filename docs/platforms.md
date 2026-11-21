# Platforms

## Wayland sessions (XWayland)

xrdome is an X11 client: it forces SDL's X11 driver, so under a Wayland compositor it runs through
XWayland (`SDL_VIDEODRIVER` still overrides). When `WAYLAND_DISPLAY` is set or `XDG_SESSION_TYPE` is
`wayland`, the glasses' window is a real fullscreen window on the glasses' output, because a
compositor may ignore or tile a borderless window placed at the output's position; on Xorg it stays
borderless at the output's position. Set the glasses' output to 3840x1080 at 100% scale in the
compositor (for example `wlr-randr --output NAME --mode 3840x1080@60`), because XWayland cannot
change modes with `xrandr` and a scaled output shows a size other than 3840x1080 to xrdome. Not
yet run on hardware: frame timing (compare `--timing-log` against Xorg), `--require-hwdec`, output
names, and the SBS switch under XWayland. Notes: the project's
`xrdome-xwayland-and-steamdeck-notes.md`.

## macOS (experimental)

xrdome builds on macOS (Apple Silicon; CI builds it and runs the unit tests on `macos-latest`), but it
has not been run on glasses there. The platform is detected at build time; on Linux nothing
changes. On macOS: SDL uses Cocoa with a CGL GL 3.3 core context (forward-compatible, high-DPI
drawable), mpv gets no X11 display and decodes with `videotoolbox`, the glasses' window is
fullscreen in its own Space, and the automatic SBS switch is off (it needs XRLinuxDriver, which
is Linux only): switch the glasses to SBS with their own button. Head tracking comes over the same
OpenTrack UDP port from any sender, such as `xrdome-pose-viture` (see [Pose from VITURE's SDK](setup.md#pose-from-vitures-sdk-xrdome-pose-viture),
which also has `--sbs` for the switch); without one use `--pose null` (drag with the mouse). Config
and per-file state keep their XDG paths (`~/.config/xrdome`, `~/.local/state/xrdome`).

```sh
brew install meson ninja pkgconf mpv sdl2 libepoxy cmocka
meson setup build && meson compile -C build && meson test -C build
./build/xrdome --pose null --require-hwdec --timing-log mac.csv --frames 3600 video.mp4
```

To skip building, download the CI artifact: on the repository's Actions page open a recent run of the
`ci` workflow and take `xrdome-macos-arm64` from its Artifacts. GitHub wraps it, so unzip once to get
`xrdome-<version>-macos-arm64.zip`, then unzip that. It is built on GitHub's Apple Silicon runner, needs
no Homebrew (the libraries, and an `ffprobe` that xrdome finds next to itself, are inside), and is not
notarised: run `xattr -dr com.apple.quarantine xrdome` in the unzipped folder once, then
`./xrdome --version`. Its `README-macos.txt` has the commands for the hardware check (issue #65),
`COPYING`, `LICENSES/` and `SOURCES.md` list the licences and where each library's source is (FFmpeg
and mpv are Homebrew's GPL builds, without non-free parts; the script refuses a non-free `ffprobe`).
`ffprobe` is looked up next to the program, then on `PATH`, only when `[general] ffprobe` is not set.

Unknown until tried on hardware: whether macOS offers the glasses an unscaled 3840x1080 mode
(a scaled one shows xrdome a smaller size and output detection fails; `--windowed` still plays),
frame timing through the compositor, and VideoToolbox limits (no AV1 hardware decode before M3).
For glasses not built in, add a `[device.NAME]` section, for example `fov_diag = 57` for an XREAL
One Pro (XREAL's figure, not measured), and keep their own screen stabilisation off (head-locked),
or head turns are corrected twice.

## Windows 11 (experimental)

xrdome builds on Windows 11 x64 with MSYS2's UCRT64 toolchain (CI builds it and runs the unit
tests on `windows-latest`), but it has not been run on glasses there. On Windows: SDL uses its
windows driver with a WGL GL 3.3 core context, mpv gets no X11 display and decodes with
`d3d11va-copy` (libmpv's GL render API maps D3D11 frames into GL without a copy only through
ANGLE, so with WGL the decoded frame is copied; `--require-hwdec` refuses a `-copy` decoder, so do
not pass it there), the glasses' window is desktop fullscreen on their display, and the automatic
SBS switch is off (XRLinuxDriver is Linux only): switch the glasses to SBS with their own button.
Head tracking comes over the same OpenTrack UDP port (from `xrdome-pose-viture`, see [Pose from
VITURE's SDK](setup.md#pose-from-vitures-sdk-xrdome-pose-viture), or from opentrack itself); without one use `--pose null`. Config is in `%APPDATA%\xrdome\config.ini` and per-file records in
`%LOCALAPPDATA%\xrdome\files\`. The executable's manifest sets the UTF-8 code page, so file names
with any characters work as on Linux. The depth helper is not available: photos without cached
depth show in 2D and films get no 3D.

In the "MSYS2 UCRT64" shell:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,meson,ninja,pkgconf,sdl2-compat,libepoxy,mpv,cmocka,ffmpeg}
meson setup build && meson compile -C build && meson test -C build
./build/xrdome.exe --pose null --timing-log win.csv --frames 3600 video.mp4
```

Run it from that shell, or use the zip below. Unknown until tried on hardware: window placement on
the glasses, the WGL context, whether `d3d11va-copy` holds 60 fps at 3840x1080, and frame timing.

**Download the CI artifact.** Open the latest run of the `ci` workflow (any push or pull request, or
Actions, `ci`, Run workflow) and take `xrdome-win64` from its Artifacts. GitHub wraps it, so unzip
once to get `xrdome-<version>-win64.zip`, then unzip that anywhere. It needs no MSYS2: `xrdome.exe`,
`ffprobe.exe` and every DLL they load are in the folder, and `ffprobe.exe` beside `xrdome.exe` is
used before one on `PATH` (and only when `[general] ffprobe` is not set). Check it with
`.\xrdome.exe --version`; `README-windows.txt` in the zip has the rest. The glasses must be at 100%
display scale and in their 3840x1080 SBS mode. The build is not signed, so Windows SmartScreen asks
once ("More info", "Run anyway").

**Build it yourself.** On a Windows 11 x64 machine without MSYS2, in PowerShell from a checkout:
`powershell -ExecutionPolicy Bypass -File packaging\windows\build.ps1`. It installs MSYS2 with
`winget install MSYS2.MSYS2` if `C:\msys64` is missing, updates it, then runs
`packaging/windows/build.sh` in the UCRT64 environment: dependencies with `pacman`, the build and
the tests, then `dist\xrdome-<version>-win64.zip`. It needs no administrator rights beyond
whatever winget asks for when it installs MSYS2. In an MSYS2 UCRT64 shell run
`packaging/windows/build.sh` directly (`--package-only` zips an existing `build/` without
building again, as CI does).

**Licence.** xrdome is GPL-2.0-or-later. MSYS2's FFmpeg is GPL-3.0-or-later, so the zip as a whole is
distributed under the GNU GPL version 3 or later, which xrdome's "or later" permits. Nothing in it is
non-free: `build.sh` stops if `ffprobe -buildconf` shows `--enable-nonfree`, or if a bundled package's
licence is not open source. `COPYING` is xrdome's licence, `LICENSES/` holds every bundled package's
licence files, and `SOURCES.md` names each package's exact version and its source archive on
`mirror.msys2.org` (the offer of corresponding source), with xrdome's own commit.
