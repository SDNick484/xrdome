# xrdome

A Linux player for VR180 video, ordinary and 3D films, and photos on XR glasses, with head tracking.

Version 0.1.0 (tagged `v0.1.0`; see [Releases](docs/releasing.md) and [CHANGELOG.md](CHANGELOG.md)).

One eye of a VR180 file as the glasses show it, taken from a desktop window with a scripted head
sweep and no glasses ([how](docs/setup.md#recording-a-demo-clip)).


https://github.com/user-attachments/assets/2784a7f1-cfec-43a9-bb9f-51663e81bfd9

## What it does

- Plays VR180 video with head tracking from XRLinuxDriver.
- Shows an ordinary film on a flat screen fixed in the room, larger than the glasses' view, that you
  explore by turning your head: zoom with `[` `]`, lock it to the head with `L`, curve it with `C`.
- Plays side-by-side 3D films in 3D (top-bottom 3D films: untested on the glasses).
- Opens photos and folders of photos (`PageDown` and `PageUp` step through them), and draws a
  photo in 3D from a depth model (`xrdome-tools depth`). Both untested on the glasses.
- Detects the format by itself, from metadata, the file name and the frame shape; `P` and `M` fix
  a wrong guess.
- Remembers the zoom for each file, also after a rename (the layout and the other remembered
  settings: untested on the glasses), and with `--resume` continues where you left off.
- Switches the glasses to side-by-side mode through XRLinuxDriver at start and back at exit
  (works on the XREAL Air 2 Pro; see below for the Viture).

The companion `xrdome-tools` command (preparing video for xrdome: transcoding, 2D to 3D, depth
for photos) is documented in [docs/tools.md](docs/tools.md).

## Glasses

| Glasses | Status |
|---|---|
| Viture Pro XR | Tested by the owner (the default device) |
| XREAL Air 2 Pro | Tested by the owner (`--device xreal-air2pro`) |
| Anything else | Untested: add a `[device.NAME]` section with its `fov_diag` (see [Config file](docs/usage.md#config-file)) |

- Viture Pro XR: XRLinuxDriver 2.11.9 hangs on 3 of 4 automatic SBS switch requests; switch the
  Viture to SBS with its own button (see [Switching to SBS](docs/setup.md#switching-to-sbs)).
- XREAL Air 2 Pro: keep it still for about 16 s while the driver calibrates it after a plug-in, or
  its yaw drifts (see [Tracking notes](docs/measurements.md#tracking-notes)).

## Requirements

- Linux with Xorg. XWayland is untested on hardware (see [Platforms](docs/platforms.md)).
- A GPU with VA-API decoding and OpenGL 3.3. NVIDIA through `--hwdec nvdec` is untested.
- mpv with libmpv, SDL2, libepoxy, and `ffprobe` from FFmpeg for format detection.
- [XRLinuxDriver](https://github.com/wheaney/XRLinuxDriver) for head tracking (never linked or
  shipped).

## Quick start

Get the release tarball, check it and build it:

```sh
curl -LO https://github.com/SDNick484/xrdome/releases/download/v0.1.0/xrdome-0.1.0.tar.xz
curl -LO https://github.com/SDNick484/xrdome/releases/download/v0.1.0/xrdome-0.1.0.tar.xz.sha256sum
sha256sum -c xrdome-0.1.0.tar.xz.sha256sum
tar -xf xrdome-0.1.0.tar.xz && cd xrdome-0.1.0
meson setup build
meson compile -C build
meson test -C build
```

Try it in a desktop window without glasses (drag with the left mouse button to look around):

```sh
./build/xrdome --windowed --pose null file.mp4
```

On the glasses, with the driver running:

```sh
./build/xrdome file.mp4
```

[docs/setup.md](docs/setup.md) has the build dependencies, the udev rules, how to start the
driver and the test patterns. Packages: [Gentoo](packaging/gentoo/README.md) (an ebuild for a
local overlay), [Arch](packaging/arch/README.md) (a `PKGBUILD` for the AUR) and
[Debian and Ubuntu](packaging/debian/README.md) (build a `.deb` from the tarball).

## Keys

| Key | Action |
|---|---|
| Space | Pause / resume |
| Left / Right | Seek -10 s / +10 s |
| Up / Down | Seek +60 s / -60 s |
| R | Recenter, yaw only |
| Shift+R | Recenter, full (also levels a tilted picture) |
| [ / ] | Zoom out / in |
| P | Switch projection, flat or VR180 |
| M | Cycle layout (2D, side by side, top-bottom) |
| S | Swap eyes |
| L | Lock the flat screen to the head |
| Q | Quit |

All keys: [docs/usage.md](docs/usage.md#keys).

## Status and roadmap

Release 0.1.0 is Linux only. "Tested" above means the owner ran it on those glasses;
[docs/STATUS.md](docs/STATUS.md) records what was seen on the glasses and what has only passed
the unit and Xvfb smoke tests, feature by feature.

Experimental, and not part of the release:

- Depth 3D for films (`D` on a flat film): built and tested with a fake helper, not run on glasses.
- The macOS and Windows 11 builds: built and unit tested in CI, never run on glasses.
- `xrdome-pose-viture`, head tracking through VITURE's own SDK: its hardware checklist has not
  been run.

Planned: a per-eye HUD (today `?` and `I` print to the terminal only), prebuilt packages.

Testers wanted: other glasses, and macOS and Windows with glasses. Please open an issue with what
you saw.

## Documentation

- [docs/usage.md](docs/usage.md): on the glasses, all keys, flags, the config file, diagnostics.
- [docs/flat-video.md](docs/flat-video.md): the flat screen, zoom modes, screen lock, curved screen, subtitles.
- [docs/photos.md](docs/photos.md): photos, depth 3D for photos, depth for films.
- [docs/detection.md](docs/detection.md): how the format is detected, and known wrong guesses.
- [docs/per-file-settings.md](docs/per-file-settings.md): per-file settings and resume.
- [docs/picture-quality.md](docs/picture-quality.md): viewing comfort, sharpness, debanding, mipmaps, mpv's scalers.
- [docs/platforms.md](docs/platforms.md): Wayland sessions, macOS and Windows 11 (experimental).
- [docs/setup.md](docs/setup.md): building, Gentoo, udev rules, the driver, the pose helper, switching to SBS.
- [docs/measurements.md](docs/measurements.md): performance, tracking and hardware notes, known limits.
- [docs/releasing.md](docs/releasing.md): releases and the maintainer's checklist.
- [docs/tools.md](docs/tools.md): `xrdome-tools`.
- [docs/STATUS.md](docs/STATUS.md): test status and hardware runs, with dates.
- [CHANGELOG.md](CHANGELOG.md): what is in each release.

## How this was built

<!-- DRAFT for the owner to edit. -->
The code was written with Claude Code, directed and reviewed by the owner, and tested by him on
his own glasses; what was seen on the glasses and what was only tested in CI is recorded per
feature in [docs/STATUS.md](docs/STATUS.md).
<!-- END DRAFT -->

## Licence

GPL-2.0-or-later (full text in `LICENSE`); xrdome links libmpv. The sharpening shader stays under
AMD's MIT licence, and `helpers/xrdome-pose-viture/` is a separate MIT program.
Details: [docs/licence.md](docs/licence.md).
