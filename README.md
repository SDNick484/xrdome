# xrdome

A Linux player for VR180 video on XR glasses (Viture Pro XR, XREAL Air 2 Pro) with head tracking.

Status: plays a side-by-side VR180 file on the glasses with head tracking from
XRLinuxDriver (OpenTrack UDP), keyboard controls and a frame-timing log. Tested
on the desktop with synthetic poses; the hardware timing check on the Viture Pro XR
is still to do. See `docs/superpowers/specs/2026-11-11-xrdome-design.md` and
`docs/superpowers/plans/2026-11-11-xrdome.md`.

## Build and run

Dependencies: `media-video/mpv` (with `libmpv` and `vaapi` USE flags), `media-libs/libsdl2`
(with `X` and `opengl`), `media-libs/libepoxy`, `media-libs/mesa` (EGL), `dev-util/cmocka` and
`dev-build/meson`. The test pattern and the smoke test also need `media-video/ffmpeg`
(`x264`/`x265`) and `x11-misc/xvfb-run`.

```sh
meson setup build
meson compile -C build
meson test -C build
```

Make a test pattern (left eye red grid, right eye blue grid, yellow marks at the top):

```sh
tools/make_pattern.sh            # pattern_sbs.mp4, 60 s, libx265
```

Play it in a 1280x360 desktop window, without glasses or head tracking:

```sh
./build/xrdome --windowed --pose null pattern_sbs.mp4
```

With `--pose null` the orientation is fixed; drag with the left mouse button to look around.

Desktop head-tracking test without glasses: `tools/ot_sweep.py` stands in for the driver and
sends OpenTrack packets to `127.0.0.1:4242` (yaw sweeping -40 to 40 degrees every 4 s; `--yaw DEG`
for a constant angle, `--rate HZ`, `--count N`, `--port N`):

```sh
./build/xrdome --windowed pattern_sbs.mp4      # terminal 1 (default pose: udp-opentrack:port=4242)
python3 tools/ot_sweep.py                      # terminal 2
```

The grid pans left and right. Stopping the sender prints `xrdome: pose stale` once and the view
holds; restarting it prints `xrdome: pose resumed`.

### On the glasses

Switch the glasses to SBS (3840x1080), start the driver (see [Driver](#driver)), then:

```sh
./build/xrdome file.mp4
```

The first pose received becomes "straight ahead" (the driver's absolute frame is arbitrary), so
look straight ahead when starting, or press `R`. If head right does not move the image left, or
head up does not move it down, flip the axis in the pose spec, e.g.
`--pose udp-opentrack:port=4242,yaw=-1` (keys `yaw`, `pitch`, `roll`, each `1` or `-1`).

### Keys

| Key | Action |
|---|---|
| Space | Pause / resume |
| Left / Right | Seek -10 s / +10 s |
| R | Recenter, yaw only (keeps the horizon level) |
| Shift+R | Recenter, full |
| [ / ] | Zoom out / in (x1.1 per step, 0.25 to 4) |
| S | Swap eyes |
| M | Cycle layout: sbs-lr, sbs-rl, mono |
| Q | Quit |

### Flags

`--pose SPEC` (`null`, or `udp-opentrack` with `port`, `yaw`, `pitch`, `roll`), `--output NAME`,
`--device viture-pro|xreal-air2pro`, `--zoom F`, `--layout sbs-lr|sbs-rl|mono`, `--windowed`,
`--require-hwdec` (exit 1 unless mpv decodes with VA-API), `--timing-log PATH`.

`--timing-log PATH` writes one CSV row per presented frame:
`frame,interval_us,pose_to_swap_us,pose_age_us` (swap-to-swap interval, pose sample to swap call,
age of the pose at the swap; `0` for the first frame or before any pose). The log is line buffered,
so `tail -f` works. At exit stderr always shows a summary such as
`timing: 18000 frames, 0 missed vsyncs (swap interval > 20 ms), max pose-to-swap 1.234 ms` and,
with the UDP backend, `pose: 0 malformed UDP packets dropped`. Targets: 0 missed vsyncs over
5 minutes and max pose-to-swap under 4 ms.

stderr shows `hwdec: vaapi` when VA-API decoding is active; anything else prints one warning.
If the glasses' display disconnects during playback, the player pauses and exits with status 0.

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

`/etc/udev/rules.d/70-xreal-xr.rules` (not yet tested on hardware):

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

## Tracking notes

Measured 2026-11-11, Viture Pro XR (`35ca:101d`), XRLinuxDriver 2.11.9, Gentoo/OpenRC:

- About 365 packets per second, no malformed packets.
- The packet carries the driver's NWU Euler angles: `q = Rz_up(yaw) * Ry_left(pitch) * Rx_forward(roll)`.
- The pose is absolute in the driver's own world frame. Looking level and forward read roughly
  yaw -157, pitch 12, roll -176, so raw angles are not usable directly.
- Rotation relative to a reference pose, `conj(q_ref) * q`, has standard NWU body axes:
  head left is positive about up, looking up is negative about left, right ear down is positive about forward.
- Yaw drift over time: not measured yet.
