# xrdome-pose-viture

Reads the head pose of VITURE glasses through VITURE's own Glasses SDK and sends it as OpenTrack
UDP, which xrdome reads with `--pose udp-opentrack` (its default). It gives head tracking on
Windows and macOS, where XRLinuxDriver does not exist, and an alternative on Linux.

It is a separate program on purpose. VITURE's SDK is proprietary; xrdome is GPL and links libmpv
and FFmpeg. A program that loads the SDK is kept apart from xrdome and talks to it over a socket.
This helper is MIT licensed (`LICENSE` here), loads the SDK library at run time (`dlopen` or
`LoadLibraryW`, no link-time dependency), and neither contains nor ships any of VITURE's files:
download the SDK from VITURE.

## What you need

- VITURE's Glasses SDK (C), for your platform: Linux x86_64 or aarch64, macOS aarch64 or Windows
  x86_64. The library is `libglasses.so`, `libglasses.dylib` or `libglasses.dll`; keep it with the
  libraries it ships with (the Linux one finds its neighbours through `$ORIGIN`).
- The SDK's headers, only to build the helper: `-Dviture_sdk_include=PATH` to the folder holding
  `viture_glasses_provider.h`, `viture_device.h` and `viture_protocol_public.h`. The headers are not
  in this repository.
- Glasses VITURE reports through the IMU callback: the Pro, One, Lite, Luma, Luma Pro and the others
  of its Gen 1 and Gen 2 families. Luma Ultra (Carina) reports its pose through another call and is
  refused with a message.
- Linux: the udev rule that lets your user open the glasses ("udev rules and group" in
  [xrdome's setup notes](../../docs/setup.md#udev-rules-and-group)), and XRLinuxDriver stopped (see below).

## Build

```sh
meson setup build -Dviture_sdk_include=/path/to/sdk/include
meson compile -C build
./build/helpers/xrdome-pose-viture/xrdome-pose-viture --help
```

Without the option nothing here is configured. With it, `meson test` also builds a fake SDK
(`tests/fake_sdk.c`, which exports the same entry points and reports one fixed pose) and runs the
helper's tests against it, with VITURE's headers or the stand-in ones in `tests/fake_include/`
(written for the tests; CI configures with those).

## Run

```sh
xrdome-pose-viture --lib /path/to/sdk/x86_64/libglasses.so
xrdome video.mp4            # in another terminal; --pose udp-opentrack is the default
```

| Option | Meaning |
|---|---|
| `--lib PATH` | VITURE's library (default: `libglasses.so`, `.dylib` or `.dll` through the system's search path) |
| `--port N` | UDP port (default 4242, xrdome's) |
| `--host ADDR` | IPv4 address (default 127.0.0.1) |
| `--rate HZ` | at most HZ packets a second (default 250, 1 to 1000) |
| `--sbs` | switch the glasses to 3840x1080 side by side while running, and back when the helper ends |
| `--pid N` | USB product id (`0x1019` and `0x101D` are the Pro); by default the glasses are found on the USB bus (vendor `0x35CA`) |
| `--count N` | stop after N packets, a check |

Ctrl+C ends the session cleanly: the IMU is closed, the display mode put back if `--sbs` changed it,
and the SDK shut down. If the helper is killed without a chance to do that (SIGKILL, a crash, power),
the glasses stay in 3840x1080: use their button.

The SDK and XRLinuxDriver both open the glasses' USB interfaces, so only one of them can run. On
Linux the helper refuses to start while XRLinuxDriver's state file (`/dev/shm/xr_driver_state`)
holds a heartbeat from the last 5 seconds; stop `xrDriver` first.

## What it sends

The SDK's pose callback gives roll, pitch, yaw and a quaternion in a North-West-Up frame (x north,
y west, z up). The helper rebuilds yaw, pitch and roll in degrees from the quaternion, in the order
xrdome's reader expects, `q = Rz_up(yaw) * Ry_west(pitch) * Rx_north(roll)`, and sends them in an
OpenTrack packet (six little-endian doubles, x y z yaw pitch roll, and a uint32 frame number; the
position is zero). The callback's own roll, pitch and yaw are not used, because the header does not
say what unit they are in. The conversion is in `convert.c` and is unit tested; if a head turn goes
the wrong way on your glasses, `xrdome --pose udp-opentrack:yaw=-1` (or `pitch`, `roll`) flips it.

The IMU is opened in pose mode at the lowest report frequency the glasses support that is at least
twice `--rate` (500 Hz for the default), else the highest they support.

## Files

| File | Job |
|---|---|
| `main.c` | arguments, the XRLinuxDriver check, finding the glasses, Ctrl+C |
| `bridge.c` | one session: SDK lifecycle, `--sbs`, the rate cap, sending packets |
| `viture_loader.c` | loads the SDK library and its 14 entry points; compile-time check of each against the header |
| `convert.c` | quaternion to yaw, pitch, roll |
| `packet.c`, `udp_out.c` | the OpenTrack packet, the UDP socket |
| `usb_enum.c` | the attached VITURE product ids (sysfs, SetupAPI, IOKit) |
| `xrdriver_check.c` | is XRLinuxDriver running (Linux) |
| `options.c` | command line |
