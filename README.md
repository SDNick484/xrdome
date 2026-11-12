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
| [ / ] | Zoom out / in (x1.1 per step, 0.25 to 4); prints `zoom: 1.10` |
| , / . | Convergence -0.1 / +0.1 degrees (-5 to 5); prints `convergence: 0.3 deg` |
| / | Convergence back to the `--convergence` value |
| F | Toggle bicubic / bilinear sampling; prints `filter: bicubic` or `filter: bilinear` |

The `zoom:`, `convergence:` and `filter:` lines are printed only when the value changes (not for
a press at a limit, or a reset to the value already set).
| S | Swap eyes |
| M | Cycle layout: sbs-lr, sbs-rl, mono |
| Q | Quit |

### Flags

`--pose SPEC` (`null`, or `udp-opentrack` with `port`, `yaw`, `pitch`, `roll`), `--output NAME`,
`--device viture-pro|xreal-air2pro`, `--zoom F`, `--layout sbs-lr|sbs-rl|mono`, `--windowed`,
`--require-hwdec` (exit 1 unless mpv decodes with VA-API), `--timing-log PATH`,
`--content-fov HxV`, `--proj hequirect` (the only projection so far, so optional),
`--sync-after-swap` (diagnostic, see [Diagnostics](#diagnostics)), `--mpv-profile NAME`,
`--mpv-opt KEY=VALUE` (repeatable, up to 16), `--mpv-log LEVEL`, `--convergence DEG`,
`--filter bilinear|bicubic`, `--fbo-format rgb10|rgba8`, `--dither on|off`.

`--convergence DEG` (default 0, -5 to 5) turns each eye's view by half the angle in opposite
directions; positive turns the eyes inwards. `--filter` chooses how the eye pass samples the
video: `bicubic` (Catmull-Rom, the default) or `bilinear`. Both are command-line flags only; see
[Viewing comfort and sharpness](#viewing-comfort-and-sharpness).

`--fbo-format rgb10|rgba8` (default `rgb10`) is the format of the texture mpv renders each
video frame into before the eye pass reads it. `rgb10` (`GL_RGB10_A2`, 10 bits per colour, the
same 4 bytes per pixel as 8-bit) keeps a 10-bit file's precision through the eye pass; `rgba8`
quantises to 8 bits first, which shows as banding in skies and smooth gradients. If the GPU
cannot render to `GL_RGB10_A2`, xrdome prints
`xrdome: note: 10-bit video texture unavailable, using 8-bit` and continues. `--dither on|off`
(default `on`) adds ordered dithering (an offset under half an 8-bit step per pixel) to the eye
pass's final write: the window is 8-bit, so without it the extra precision is rounded away again
at the very end. Black outside the video (behind you, or outside `--content-fov`) is never
dithered. After the first frame stderr shows which texture was used, e.g.
`video texture: rgb10_a2 dither=on`. Both are command-line flags only. To compare against the old
output, run the same file twice, the second time with `--fbo-format rgba8 --dither off` (which
reproduces the output from before these flags existed), and look at a sky or a slow gradient. 8-bit files
gain little from either flag.

`--mpv-log LEVEL` (default `warn`; one of `no`, `fatal`, `error`, `warn`, `info`, `v`, `debug`)
forwards mpv's own log messages at that level and above to stderr, one line each as
`mpv: [<module>] <text>`, for example
`mpv: [ao/alsa] Playback open error: Device or resource busy`. `no` turns them off. Use `info` or
`v` when audio or decoding fails without an obvious reason.

`--content-fov HxV` says how many degrees of longitude (H) and latitude (V) each eye of the file
covers, centred straight ahead. The default is `180x180`; each value is 10 to 180. For a
side-by-side file whose author gives 120 degrees horizontal by 90 vertical per eye:

```sh
xrdome --content-fov 120x90 clip.mp4
```

Directions outside the covered area, and behind you, are black. The player warns once if the
frame's aspect does not fit (per eye `H:V` for side-by-side, whole frame for mono). It assumes
an equirectangular mapping within the stated field of view, which may not hold for action-camera
footage (lens projections other than equirectangular will look distorted). It is a command-line
flag only, not an ini key, because it describes the file and not the glasses.

`--mpv-profile NAME` selects the mpv profile applied before mpv starts. The default is `fast`.
xrdome has mpv render the video at its native size into an offscreen buffer, with no scaling, so
mpv's default quality settings (high-quality chroma scaling, float intermediate buffers,
dithering) cost GPU time without a visible benefit; on a 7680x2880 file they saturated an
integrated GPU. `--mpv-profile default` applies no profile. `--mpv-opt KEY=VALUE` sets any mpv
option after the profile and xrdome's own fixed options (`vo`, `hwdec`, `osd-level`), so it can
override them; mpv rejecting an option or profile exits 1. After the first frame xrdome prints
`mpv: profile=NAME` and the `--mpv-opt` pairs to stderr, to match a timing log to its settings.
To diagnose a slow GPU, compare `--mpv-profile default` against the default run, and try
`--mpv-opt gpu-dumb-mode=yes` as the cheapest render path.

`--timing-log PATH` writes one CSV row per presented frame:
`frame,interval_us,pose_to_swap_us,pose_age_us,swap_us` (swap-to-swap interval, pose sample to swap
call, age of the pose at the swap, time spent inside the swap call; `0` for the first frame or
before any pose). The log is line buffered,
so `tail -f` works. At exit stderr always shows a summary such as
`timing: 18000 frames, 0 missed vsyncs (swap interval > 20 ms), pose-to-swap max 1.234 ms, median 0.812 ms (last 4096 frames)`
(the max covers every frame; the median is computed from the most recent 4096 frames) and,
with the UDP backend, `pose: 0 malformed UDP packets dropped`. Targets: 0 missed vsyncs over
5 minutes and max pose-to-swap under 4 ms.

stderr shows `hwdec: vaapi` when VA-API decoding is active; anything else prints one warning.
If the glasses' display disconnects during playback, the player pauses and exits with status 0.

### Config file

xrdome reads `$XDG_CONFIG_HOME/xrdome/config.ini`, or `~/.config/xrdome/config.ini` when
`XDG_CONFIG_HOME` is unset, empty or not an absolute path. A missing file is fine. Precedence, lowest
to highest: built-in defaults, built-in device values, the ini file, the command line.

`[general]` keys: `pose` (same spec as `--pose`, default `udp-opentrack:port=4242`) and `output`
(same as `--output`, default `auto`).

`[device.<name>]` keys, read for the device selected with `--device` (default `viture-pro`):
`fov_diag` (diagonal FOV in degrees, 10 to 120) and `zoom` (0.25 to 4). Built-in devices are
`viture-pro` (43) and `xreal-air2pro` (45); a section for any other name defines a new device and
must set `fov_diag`. Sections for other devices are ignored. Lines starting with `#` are comments;
unknown keys and bad values are errors that name the file and line.

```ini
[general]
pose = udp-opentrack:port=4242,yaw=-1
output = auto

[device.viture-pro]
fov_diag = 43
zoom = 1.0

[device.mydevice]
fov_diag = 50
```

## Viewing comfort and sharpness

**Convergence.** The glasses show both eyes' images on a virtual screen a few metres away, so
content that is identical in both eyes appears at that distance, not at infinity. A VR180 camera
was set up for objects at infinity to line up, so anything close to the camera needs more
eye-crossing on the glasses than it did in the scene, and a near subject can split into two
images that will not fuse. Press `.` a few times (0.1 degrees per step) until the near subject
becomes one image, `,` to go back, and `/` to return to the starting value. Each step prints the
current value; to start a file with it, pass it on the command line, e.g. `--convergence 1.5`.
Convergence is a display-side adjustment, not a property of the file: it shifts the left eye's
picture left and the right eye's picture right (by half the angle each), the same in every
layout, and does not change what the camera recorded. Positive values suit near subjects: they
move everything in the scene further away, so the larger the value, the more your eyes must
diverge for distant content; use the smallest value that fuses the subject. Negative values
bring the scene nearer.

**Sharpness.** The glasses show about 51 display pixels per degree, while a 4K VR180 file has
about 11 source pixels per degree, so every source pixel is enlarged about five times. Two things
help:

- Zoom out (`[`): a wider view puts fewer display pixels on each source pixel, so the enlargement
  and the blur that comes with it shrink. Zooming in (`]`) magnifies the softness.
- Bicubic sampling (the default; `F` toggles it): bilinear filtering, the GPU's built-in
  enlargement, turns every edge into a straight ramp a whole source pixel wide, which is the
  blurriest way to enlarge. Catmull-Rom keeps edges steeper, at the cost of a faint halo next to
  high-contrast edges. Press `F` on a still frame to compare.

Bicubic reads 9 texture samples per output pixel instead of 1. Its cost on the owner's GPU has
not been measured. To measure it, make two separate runs of the same file, one with
`--filter bilinear` and one with `--filter bicubic`, each with its own `--timing-log`, and
compare the logs; toggling `F` live mixes both filters in one log. `--mpv-log v` and
`--mpv-log debug` are for diagnosis only: they print a lot and affect timing, so leave them off
for measurements.

## Diagnostics

`--sync-after-swap` calls `glFinish()` right after `SDL_GL_SwapWindow`, before the frame is reported
to the player. On Mesa/X11 the swap can return at once and the driver may block on vsync only at the
next frame's first draw, which is after the pose sample, so each pose would be about one refresh
old. It exists to compare `pose_to_swap_us` in the timing log with and without the flag; it is
not meant for normal playback. `swap_us` in the log is the time spent in the swap call (plus the
`glFinish` when the flag is on).

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

Hardware-check notes for tracking:

- Look at `pose_to_swap_us` per frame, not only the max. If it often sits near 8-16 ms while
  `interval_us` is about 16.7 ms, re-run with `--sync-after-swap` and compare.
- Time how long the player takes to exit after the glasses are unplugged.
- Use a file the machine can decode at full rate.
