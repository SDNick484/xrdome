# Setup

## Build and run

Dependencies: `media-video/mpv` (with `libmpv` and `vaapi` USE flags), `media-libs/libsdl2`
(with `X` and `opengl`), `media-libs/libepoxy`, `media-libs/mesa` (EGL) and `dev-build/meson`; `dev-util/cmocka` for the unit
tests (without it `meson setup` leaves them out; `-Dtests=enabled` makes it required, `-Dtests=disabled`
leaves them out even when it is installed). At run time xrdome also runs the `ffprobe` program from `media-video/ffmpeg`
(already a dependency of mpv) to detect the format; without it every file plays as VR180 side
by side unless flags say otherwise (one `xrdome: note:` line). Nothing from FFmpeg is linked.
The test patterns and the smoke tests also need `media-video/ffmpeg` (`x264`/`x265`) and
`x11-misc/xvfb-run`.

The USE flags that matter, as `/etc/portage/package.use` lines (checked with `equery uses` on the
owner's laptop, 2026-11-14: mpv 0.42.0, libsdl2 2.32.10, ffmpeg 9.0.2, mesa with `radeonsi`):

```
media-video/mpv libmpv vaapi
media-libs/libsdl2 X opengl
media-video/ffmpeg x264 x265
```

```sh
meson setup build
meson compile -C build
meson test -C build
```

CI builds with warnings as errors and runs the whole suite, including the three Xvfb smoke tests,
twice: on Ubuntu 24.04 (mpv 0.37) and in an Arch Linux container (`archlinux:latest`: current gcc,
mpv, SDL through `sdl2-compat`, and Mesa), and fails if a smoke test skips. Arch's `xvfb-run` sleeps 3 s
at every start (Debian's and Gentoo's wait for Xvfb to be ready), so the `smoke` test takes about
five minutes there; the Arch job gives the tests twice meson's time limit for that reason.

Make a test pattern (left eye red grid, right eye blue grid, yellow marks at the top):

```sh
tools/make_pattern.sh            # pattern_sbs.mp4, 60 s, libx265
```

`tools/make_pattern.sh --kind flat2d 60 flat.mp4` writes a flat 1920x1080 pattern instead (green,
white border, yellow box above the centre, a block of 1-pixel lines in the middle for judging
shimmer); `--kind flat-hsbs` a half side-by-side 3D one (left red, right blue) and
`--kind flat-tb` a full top-bottom one (1920x2160, top red, bottom blue).

Play it in a 1280x360 desktop window, without glasses or head tracking:

```sh
./build/xrdome --windowed --pose null pattern_sbs.mp4
```

With `--pose null` the orientation is fixed; drag with the left mouse button to look around.

Desktop head-tracking test without glasses: `tools/ot_sweep.py` stands in for the driver and
sends OpenTrack packets to `127.0.0.1:4242` (yaw sweeping -40 to 40 degrees every 4 s; `--yaw DEG`
for a constant angle, `--amplitude DEG` and `--period SEC` for the sweep, `--rate HZ`, `--count N`,
`--port N`). `--pitch-amplitude DEG` adds a sweep up and down with its own `--pitch-period SEC`
(default 6 s; no pitch sweep unless asked), and `--pitch DEG` holds a constant pitch:

```sh
./build/xrdome --windowed pattern_sbs.mp4      # terminal 1 (default pose: udp-opentrack:port=4242)
python3 tools/ot_sweep.py                      # terminal 2
```

The grid pans left and right. Stopping the sender prints `xrdome: pose stale` once and the view
holds; restarting it prints `xrdome: pose resumed`.

### Recording a demo clip

One eye at full size in a desktop window, a slow scripted head sweep, and `ffmpeg`'s screen grab
(needs `x11-apps/xwininfo`). The picture in the README was made this way (owner's laptop,
2026-11-21, a VR180 file).

```sh
./build/xrdome --window-eye left --window-size 1920x1080 video.mp4 &
python3 tools/ot_sweep.py --amplitude 22 --period 10 --pitch-amplitude 8 --pitch-period 15 &

# click the xrdome window when the pointer becomes a cross
eval "$(xwininfo | awk '
  /Absolute upper-left X/ {print "X="$4}
  /Absolute upper-left Y/ {print "Y="$4}
  /^ *Width:/  {print "W="$2}
  /^ *Height:/ {print "H="$2}')"

ffmpeg -y -f x11grab -framerate 30 -video_size "$((W-2))x$H" -i "$DISPLAY+$X,$Y" -t 30 \
  -c:v libx264 -crf 18 -pix_fmt yuv420p demo.mp4
kill %2 %1
```

- 30 seconds is one whole cycle of both sweeps, so the clip loops.
- The grab is 2 pixels narrower than the window because the window manager's border puts a
  screen-wide window 1 pixel in from the edge, and `x11grab` refuses an area that leaves the
  screen. Use a window smaller than the screen (`--window-size 1600x900`) to grab all of it.
- Run the `eval` and `ffmpeg` in the same shell: the variables do not carry to another terminal.
- A sweep wider than the picture's interesting part shows black or empty sky; narrow
  `--amplitude` until it stays on the subject. For a flat film use about `--amplitude 15
  --pitch-amplitude 8`.
- To shrink the file: `ffmpeg -i demo.mp4 -c:v libx264 -preset slow -crf 23 -pix_fmt yuv420p
  -movflags +faststart small.mp4` (the 30 s clip went from 26 MB to 13 MB).

## Install on Gentoo

`packaging/gentoo/` holds a live ebuild, `media-video/xrdome-9999`, that builds `main` from GitHub
with meson and installs `/usr/bin/xrdome`, and a versioned one, `xrdome-0.1.0.ebuild`, that builds
the release tarball once it is published (see [Releases](releasing.md#releases)). Neither is in an overlay yet;
add them to a local one (the versioned one needs a `Manifest` first, see
`packaging/gentoo/README.md`):

```
# as root
eselect repository create local            # skip if you already have a local overlay
mkdir -p /var/db/repos/local/media-video/xrdome
cp packaging/gentoo/media-video/xrdome/* /var/db/repos/local/media-video/xrdome/
echo 'media-video/xrdome **' >> /etc/portage/package.accept_keywords/xrdome
emerge --ask media-video/xrdome
```

`media-video/mpv` needs `libmpv`, `media-libs/libsdl2` needs `X` and `opengl`. Re-run `emerge` to
rebuild from the newest `main`. `packaging/gentoo/README.md` has the details and the test flag.
The live ebuild was parsed and its dependencies resolved with `emerge --pretend`; neither ebuild
has been built with `emerge` yet (the 0.1.0 one cannot be before the tarball exists).

## System setup

Head tracking comes from [XRLinuxDriver](https://github.com/wheaney/XRLinuxDriver), run by hand.
Its installer requires systemd, so on OpenRC it is unpacked and started manually.

### udev rules and group

`/etc/udev/rules.d/70-viture-xr.rules`:

```
SUBSYSTEM=="usb", ATTRS{idVendor}=="35ca", MODE="0660", GROUP="plugdev"
SUBSYSTEM=="usb", KERNEL=="hiddev[0-9]*", ATTRS{idVendor}=="35ca", MODE="0660", GROUP="plugdev"
SUBSYSTEM=="tty", KERNEL=="ttyACM[0-9]*", ATTRS{idVendor}=="35ca", MODE="0660", GROUP="plugdev"
SUBSYSTEM=="hidraw", KERNEL=="hidraw[0-9]*", ATTRS{idVendor}=="35ca", MODE="0660", GROUP="plugdev"
```

`/etc/udev/rules.d/70-xreal-xr.rules` (needed: on the owner's laptop it is the only rule that gives
the XREAL's hidraw and USB nodes to `plugdev`, which the driver needs; 2026-11-14):

```
SUBSYSTEM=="usb", ATTRS{idVendor}=="3318", MODE="0660", GROUP="plugdev"
SUBSYSTEM=="hidraw", KERNEL=="hidraw[0-9]*", ATTRS{idVendor}=="3318", MODE="0660", GROUP="plugdev"
```

```sh
getent group plugdev || groupadd plugdev
gpasswd -a "$USER" plugdev
udevadm control --reload-rules
```

Replug the glasses and log in again. Until the next login, `sg plugdev -c '<command>'` runs a command with the group active.

Without group access the driver still finds the glasses but fails with
`open_imu failed ... USB execution error`.

### Driver

Unpack `xrDriver-x86_64.tar.gz` and `xrDriver-libs-x86_64.tar.gz` from the XRLinuxDriver releases page.
Do not run `xr_driver_setup`.

`~/.config/xr_driver/config.ini`:

```
disabled=false
output_mode=external_only
external_mode=opentrack
opentrack_app_ip=127.0.0.1
opentrack_app_port=4242
metrics_disabled=true
```

`disabled=false` is required: the driver defaults to disabled.

```sh
LD_LIBRARY_PATH=/path/to/xr_driver_lib /path/to/xr_driver/bin/xrDriver
```

`LD_LIBRARY_PATH` must be the directory holding both `libglasses.so` and `libGlassSDK.so`.
The log is `~/.local/state/xr_driver/driver.log`.

Verify: `tcpdump -n -i lo udp port 4242 -c 20` shows `UDP, length 52`, and `tools/ot_dump.py` prints angles.

### Pose from VITURE's SDK (`xrdome-pose-viture`)

`helpers/xrdome-pose-viture/` is a small separate program that reads the Viture Pro XR's head pose
through VITURE's own Glasses SDK and sends it as OpenTrack UDP on 127.0.0.1:4242, so xrdome reads it
with the `udp-opentrack` source it already has. It works on Linux, macOS and Windows, which makes it
the head tracking on the last two, and an alternative to XRLinuxDriver on Linux. It is a separate
program, MIT licensed, because the SDK is proprietary and xrdome is GPL: the helper loads VITURE's
library at run time and the repository holds none of VITURE's files. Download the SDK (C) for your
platform from VITURE; the helper is built against its headers.

```sh
meson setup build -Dviture_sdk_include=/path/to/sdk/include     # headers only; builds the helper too
meson compile -C build
./build/helpers/xrdome-pose-viture/xrdome-pose-viture --lib /path/to/sdk/x86_64/libglasses.so --sbs
./build/xrdome video.mp4                                         # another terminal; the default pose
```

`--sbs` switches the glasses to 3840x1080 side by side through the SDK and back when the helper
ends (also on Ctrl+C), which is the switch XRLinuxDriver does on Linux and which Windows and macOS
otherwise leave to the glasses' button. Other options: `--port`, `--host`, `--rate`, `--pid`,
`--lib`; the helper's own [README](../helpers/xrdome-pose-viture/README.md) has them all.

Only one of the SDK and XRLinuxDriver can hold the glasses: they both open their USB interfaces.
On Linux the helper refuses to start while the driver's heartbeat in `/dev/shm/xr_driver_state` is
fresh; stop `xrDriver` first, and the udev rule and group from below apply to the SDK as well. If
a head turn goes the wrong way, `--pose udp-opentrack:yaw=-1` (or `pitch`, `roll`) flips it. Not
yet run on glasses; the hardware checklist is in `docs/STATUS.md` ("Pose helper for VITURE's SDK").

### Switching to SBS

The Viture Pro XR comes back in 1080p (mirror) mode after every reconnect. With
`--sbs-switch auto` (the default), when no 3840x1080 output is found at start (or the output named
with `--output` is 1920x1080), xrdome reads the driver's state file `/dev/shm/xr_driver_state`
and asks for the switch only when all of these hold:

- its `heartbeat=` is within 5 seconds of now (the driver rewrites the file every second; a
  crashed driver leaves a stale one behind),
- it lists glasses with `sbs_mode_supported=true`,
- and `sbs_mode_enabled=false`.

It then writes the one-line request `sbs_mode=enable` to `/dev/shm/xr_driver_control` (the
driver reads that file when it is closed, acts on it and deletes it; `xr_driver_cli` writes its
requests there the same way), and waits up to 8 seconds for a 3840x1080 output to appear,
listing the outputs again every 0.2 seconds and whenever the display changes. On success stderr
shows

```
display: switched the glasses to SBS in 1.3 s
```

and playback starts on that output (2.2 to 2.8 s on the XREAL Air 2 Pro, 3.3 s on the Viture Pro XR,
2026-11-14). At exit, if xrdome switched the glasses itself, it writes
`sbs_mode=disable` so the desktop mirror is usable again, and prints
`display: asked XRLinuxDriver to switch the glasses back to 1080p`. It does not switch back
glasses that disconnected during playback (they come back in 1080p anyway) or when the driver is
no longer running.

When it cannot switch, the usual error is followed by the reason, for example

```
xrdome: no 3840x1080 output was found; switch the glasses to SBS mode
xrdome: automatic SBS switch: XRLinuxDriver is not running (the heartbeat in /dev/shm/xr_driver_state is 412 s old)
xrdome: outputs: eDP-1 14" (1920x1200), VITURE 23" (1920x1080)
```

Other reasons: `XRLinuxDriver is not running (cannot read /dev/shm/xr_driver_state)`,
`XRLinuxDriver reports no glasses connected`, `XRLinuxDriver reports that these glasses cannot
switch to SBS`, `XRLinuxDriver reports the glasses already in SBS mode` (the driver has switched
but X shows no 3840x1080 output), and `asked XRLinuxDriver to switch the glasses to SBS, but no
3840x1080 output appeared within 8 s`. After a timeout xrdome does not ask to switch back: the
glasses may still switch, and the next run then finds them in SBS. `--sbs-switch off` (or
`sbs_switch = off`) restores the old behaviour exactly: no reading or writing of the driver's
files, and only the first and last of those lines.

The tests point xrdome at another directory with the environment variable
`XRDOME_XR_DRIVER_DIR` (default `/dev/shm`), so no test run writes to a real driver.

On the glasses (2026-11-14, XRLinuxDriver 2.11.9): on the XREAL Air 2 Pro the switch and the switch
back work. On the Viture Pro XR the driver hung on 3 of 4 switch requests, also with xrdome not
running: its heartbeat stops, so the next xrdome run reports it as not running, and only
restarting the driver helps. Until that is fixed in the driver, switch the Viture to SBS with
its own button (front left, hold 2 seconds; the XREAL's is volume up, hold 2 seconds): xrdome
then finds the glasses in SBS and asks the driver for nothing. A driver restarted while a request
was waiting deleted it without acting on it.
