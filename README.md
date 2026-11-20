# xrdome

A Linux player for VR180 video and ordinary flat video on XR glasses (Viture Pro XR, XREAL Air 2
Pro) with head tracking.

Version 0.1.0 (tagged `v0.1.0`; see [Releases](#releases) and [CHANGELOG.md](CHANGELOG.md)).

Status: plays a side-by-side VR180 file on the glasses with head tracking from
XRLinuxDriver (OpenTrack UDP), keyboard controls, a frame-timing log and per-file settings
memory. On the Viture Pro XR, with the default mpv profile, bicubic sampling and a 10-bit
texture, 0.6% of swap intervals were late (see [Performance notes](#performance-notes)). Per-file
settings memory was run on the Viture Pro XR on 2026-11-13: saving a zoom change, restoring it,
and keeping it across a rename were seen to work; the other remembered settings and `--forget`
have only passed the unit and Xvfb smoke tests. On the XREAL Air 2 Pro (2026-11-14) VR180 and flat
files played with head tracking much as on the Viture, with and without `--device xreal-air2pro`. See `docs/STATUS.md`, `docs/superpowers/specs/2026-11-11-xrdome-design.md` and
`docs/superpowers/plans/2026-11-11-xrdome.md`.

The companion `xrdome-tools` command (preparing video for xrdome) is documented in [docs/tools.md](docs/tools.md).

Flat video and format detection (an ordinary film opens as a flat screen fixed in the room, a
VR180 file opens as before, both without flags; see [Flat video](#flat-video) and
[Format detection](#format-detection)) were run on the Viture Pro XR on 2026-11-14: flat 1080p
and 4K films opened flat at 1.5 by frame shape, and the owner's VR180 files as VR180; the screen
stayed put when the head turned, `[` `]` `Z` `L` `R` `?` `I` and Up/Down worked; half SBS (a test
clip) showed the right depth and `S` inverted it; flat and VR180 records restored; mipmaps cost no
more than 0.14 percentage points of missed vsyncs. Not seen on the glasses: whether mipmaps reduce
shimmer, a real half-SBS film, subtitles, top-bottom and full SBS. `docs/STATUS.md` has the
details.

A second run on both pairs of glasses (2026-11-14, afternoon and evening) covered the newer
features. The automatic switch to SBS works on the XREAL Air 2 Pro (about 2.5 s, and back at exit);
on the Viture Pro XR it worked once, then XRLinuxDriver 2.11.9 hung on the switch request (see
[Switching to SBS](#switching-to-sbs)). Resume worked on an 81-minute film and an 8K file;
debanding costs little on a 4K film but saturates the GPU at 8K; mpv's scalers cost almost nothing
and look slightly better at most; the curved screen was liked, at R = 1; a full side-by-side 3D
film played in 3D without flags. Tracking: the Viture drifted 0.02 degrees in 5 minutes; the XREAL
must lie still while the driver calibrates it (see [Tracking notes](#tracking-notes)). Still not
seen on the glasses: subtitles, shimmer, a real half-SBS film. `docs/STATUS.md` has the numbers.

## Build and run

Dependencies: `media-video/mpv` (with `libmpv` and `vaapi` USE flags), `media-libs/libsdl2`
(with `X` and `opengl`), `media-libs/libepoxy`, `media-libs/mesa` (EGL), `dev-util/cmocka` and
`dev-build/meson`. At run time xrdome also runs the `ffprobe` program from `media-video/ffmpeg`
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
for a constant angle, `--rate HZ`, `--count N`, `--port N`):

```sh
./build/xrdome --windowed pattern_sbs.mp4      # terminal 1 (default pose: udp-opentrack:port=4242)
python3 tools/ot_sweep.py                      # terminal 2
```

The grid pans left and right. Stopping the sender prints `xrdome: pose stale` once and the view
holds; restarting it prints `xrdome: pose resumed`.

### On the glasses

Start the driver (see [Driver](#driver)), then:

```sh
./build/xrdome file.mp4
```

Glasses in 1080p (mirror) mode are switched to SBS (3840x1080) through the driver and back
at exit (see [Switching to SBS](#switching-to-sbs)); without the driver, switch them to SBS by hand
first.

The first pose received becomes "straight ahead" (the driver's absolute frame is arbitrary), so
look straight ahead when starting, or press `R`. If head right does not move the image left, or
head up does not move it down, flip the axis in the pose spec, e.g.
`--pose udp-opentrack:port=4242,yaw=-1` (keys `yaw`, `pitch`, `roll`, each `1` or `-1`).

### Keys

| Key | Action |
|---|---|
| Space | Pause / resume |
| Left / Right | Seek -10 s / +10 s |
| Up / Down | Seek +60 s / -60 s |
| PageDown / PageUp | Next / previous photo, when a directory was opened (see [Photos](#photos)); prints `photo: 2 of 5: name.jpg` |
| R | Recenter, yaw only (keeps the current pitch and roll reference) |
| Shift+R | Recenter, full |
| [ / ] | Zoom out / in (x1.1 per step, 0.25 to 4); prints `zoom: 1.10` |
| F | Cycle the enlarging filter: bilinear, bicubic, bicubic-ar, mitchell, then bilinear again; prints e.g. `filter: bicubic-ar`. See [Sharpness](#viewing-comfort-and-sharpness) |
| Shift+F | Sharpening strength: off, 0.25, 0.5, 0.75, 1, then off again (from a `--sharpen` value in between, the next step above it); prints `sharpen: 0.25` or `sharpen: off`. Not remembered per file |
| S | Swap eyes |
| M | Cycle layout. Flat: 2D, half SBS, full SBS, half top-bottom, full top-bottom; prints e.g. `layout: sbs-lr half` or `layout: mono`. VR180: sbs-lr, sbs-rl, tb-lr, mono; prints e.g. `layout: sbs-rl` |
| P | Switch projection, flat or VR180; prints `proj: flat` or `proj: hequirect` |
| Z | Zoom mode, flat only; prints `zoom mode: screen` or `zoom mode: magnify` |
| L | Screen lock, flat only; prints `screen: head-locked` or `screen: fixed` |
| B | Debanding on / off (mpv `deband`); prints `deband: on` or `deband: off`. Not remembered per file |
| C | Curved screen on or off, flat only; prints `screen curve: 1.00` or `screen curve: off` |
| D | Depth 3D on / off, for a photo with depth (see [Depth 3D for photos](#depth-3d-for-photos)); prints `depth: off, strength 2.0%, plane 0.50`. On at start; carries over from one photo to the next. On a flat 2D film: off at start; on sends the film's frames to the depth helper (see [Depth for films](#depth-for-films)) |
| - / = | Depth strength down / up (0.5% of the picture width per step, 0 to 5%); prints the `depth:` line. Remembered per photo |
| , / . | Screen plane further / nearer (0.05 per step, 0 to 1): further brings the picture forward; prints the `depth:` line. Remembered per photo |
| ? | Print the key list to stderr (`keys:` lines) |
| I | Print playback information to stderr (`info:` lines) |
| Q | Quit |

The `zoom:`, `filter:`, `sharpen:` and `depth:` lines are printed only when the value changes
(not for a press at a limit). `P` also prints the other projection's zoom, since each projection keeps its
own. In VR180, `Z` prints `zoom mode: magnify (VR180 has no screen mode)`, `L` prints
`screen: fixed (VR180 cannot be head-locked)` and `C` prints
`screen curve: none (VR180 has no flat screen)`, and none of them changes anything. `C` works
the same in both zoom modes and prints the same lines in both; `Z` leaves the curve as it is.
`?` is Shift+/ on a US keyboard. Key repeat is ignored except for zoom, seek, and the depth
strength and plane. For a film, `D`, `-`, `=`, `,` and `.` print `depth: photos only` and change
nothing; for a photo without depth they also print why (`depth: none for this photo: ...`).

`I` prints, one item per line: the file name; codec, coded frame size and frame rate; `hwdec`;
projection, layout and (flat 3D) packing, each with where it came from (the sources of the
`detect:` line, or `key`); zoom, and for flat video the zoom mode and the screen size in degrees
(for a curved screen, the angle between its ends as seen from the eye; in magnify mode, the size
of the zoom-1 screen zoom magnifies, the same at every zoom, not the part in view); screen fixed or
head-locked and the screen curve; the filter in use (`trilinear` while mipmaps are used) and whether
mipmaps are on; the sharpening strength (`sharpen: off` or e.g. `sharpen: 0.50`); mpv's scaler (`--mpv-scale`: `off`, the scaler and the sizes it scales between, or
why it is not in use); the video texture; `deband` on or off; frames presented and missed vsyncs
so far. For example:

```
info: projection: flat (frame shape)
info: layout: mono (frame shape)
info: zoom: 1.50, zoom mode screen, screen 54.5 x 32.3 degrees
info: screen: fixed
info: screen curve: off
info: filter: trilinear, mipmaps on
info: sharpen: off
info: mpv scale: spline36, not in use (picture shrunk)
```

These go to the terminal only; the per-eye HUD planned next will show them on the glasses.

The head pose at the first frame becomes the reference on all three axes, so a head tilted at
start gives a tilted picture, and `R` does not correct that. Hold your head level and press
`Shift+R`.

### Flags

`--pose SPEC` (`null`, or `udp-opentrack` with `port`, `yaw`, `pitch`, `roll`), `--output NAME`,
`--device viture-pro|xreal-air2pro`, `--zoom F`, `--proj auto|hequirect|flat`,
`--layout auto|sbs-lr|sbs-rl|tb-lr|tb-rl|mono`, `--packing auto|half|full`,
`--zoom-mode screen|magnify`, `--screen fixed|locked`, `--screen-curve off|R`, `--mipmap on|off`,
`--windowed`, `--sbs-switch auto|off`,
`--hwdec NAME` (mpv's hwdec; default `vaapi` on Linux, `videotoolbox` on macOS, `d3d11va-copy`
on Windows; for example
`nvdec` or `auto-safe` on an NVIDIA machine), `--require-hwdec` (exit 1 unless mpv decodes
zero-copy: any decoder but `no` and the `-copy` ones), `--timing-log PATH`,
`--content-fov HxV`, `--sync-after-swap` (diagnostic, see [Diagnostics](#diagnostics)),
`--mpv-profile NAME`, `--mpv-opt KEY=VALUE` (repeatable, up to 16), `--mpv-log LEVEL`,
`--mpv-scale off|NAME`, `--convergence DEG`, `--filter bilinear|bicubic|bicubic-ar|mitchell`,
`--sharpen S`, `--fbo-format rgb10|rgba8`, `--dither on|off`, `--deband on|off`,
`--depth-helper PROGRAM|off` (see [Depth 3D for photos](#depth-3d-for-photos)), `--no-memory`, `--forget`,
`--resume`, `--no-resume` (the last four are described under
[Per-file settings](#per-file-settings)).

`--proj auto|hequirect|flat` (default `auto`) chooses the projection: `hequirect` is VR180
(half-equirectangular), `flat` an ordinary flat screen (see [Flat video](#flat-video)), `auto`
detects it (see [Format detection](#format-detection)). `--layout` (default `auto`, detected)
says how the eyes are packed into the frame: `sbs-lr` and `sbs-rl` side by side, `tb-lr` and
`tb-rl` top and bottom (the first-named eye is the left half or the top half), `mono` one picture
for both eyes. `--zoom F` applies to whichever projection the file opens in. Each of these, when
given, wins over everything detection finds.

`--packing auto|half|full` (default `auto`, flat 3D only): `full` means each eye's half of the
frame already has the picture's shape, `half` that it was squeezed to half size along the split
and is stretched back. `auto` takes side by side as full when the frame is at least 3:1 and top
and bottom as full when it is narrower than 1.5:1, otherwise half (so 1920x1080 is half and
3840x1080 full side by side; 1920x2160 is full top-bottom).

`--zoom-mode screen|magnify` (default `screen`, flat only) chooses what zoom does to a flat
picture; see [Flat video](#flat-video). `--screen fixed|locked` (default `fixed`, flat only)
keeps the screen fixed in the room or locks it to the head. `--screen-curve off|R` (default `off`,
flat only; R from 1 to 10) bends the screen into part of a cylinder; see
[Flat video](#flat-video). `--mipmap on|off` (default `on`) uses
mipmaps while the picture is shrunk; see [Viewing comfort and sharpness](#viewing-comfort-and-sharpness).

`--convergence DEG` (default 0, -5 to 5) turns each eye's view by half the angle in opposite
directions; positive turns the eyes inwards. `--filter` chooses how the eye pass enlarges the
video: `bicubic` (Catmull-Rom, the default), `bicubic-ar` (Catmull-Rom without its halo),
`mitchell` (softer, a much fainter halo) or `bilinear`. Neither is an ini key.
`--convergence` is also remembered per file (see [Per-file settings](#per-file-settings)), and
`--filter` is not. See [Viewing comfort and sharpness](#viewing-comfort-and-sharpness).

`--sharpen S` (default 0, off; 0 to 1; ini key `sharpen`) sharpens each eye's picture after it
has been drawn, with contrast-adaptive sharpening (AMD FidelityFX CAS); 1 is the strongest. At 0
the eye pass is exactly what it is without the flag, at no cost. `Shift+F` steps it during
playback. See [Viewing comfort and sharpness](#viewing-comfort-and-sharpness).

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
`video texture: rgb10_a2 1920x1080 dither=on mipmap=off` (the size is the coded frame size, and
`mipmap` says whether mipmaps were used for the first frame). With `--mpv-scale` the line ends in
`mpv-scale=NAME` while mpv enlarges into the texture (the size is then the enlarged size) or
`mpv-scale=unused`, and it is printed again whenever the texture's size changes. Both are command-line flags only. To compare against the old
output, run the same file twice, the second time with `--fbo-format rgba8 --dither off` (which
reproduces the output from before these flags existed), and look at a sky or a slow gradient. 8-bit files
gain little from either flag.

`--mpv-log LEVEL` (default `warn`; one of `no`, `fatal`, `error`, `warn`, `info`, `v`, `debug`)
forwards mpv's own log messages at that level and above to stderr, one line each as
`mpv: [<module>] <text>`, for example
`mpv: [ao/alsa] Playback open error: Device or resource busy`. `no` turns them off. Use `info` or
`v` when audio or decoding fails without an obvious reason.

`--content-fov HxV` (also remembered per file; VR180 only) says how many degrees of longitude (H) and latitude (V) each eye of the file
covers, centred straight ahead. The default is `180x180`; each value is 10 to 180. For a
side-by-side file whose author gives 120 degrees horizontal by 90 vertical per eye:

```sh
xrdome --content-fov 120x90 clip.mp4
```

Directions outside the covered area, and behind you, are black. When playing as VR180 the player
warns once if the frame's aspect does not fit (per eye `H:V` for side by side and top-bottom,
whole frame for mono), for example
`xrdome: warning: video is 1920x1080, which is not the aspect of a side-by-side VR video covering 180x180 degrees. If it is an ordinary flat video, press P or use --proj flat. If it is VR video covering less than 180 degrees, set --content-fov. Playing it as given.`
Given for a file that opens flat, `--content-fov` is ignored with
`xrdome: note: --content-fov applies to VR180 only; ignored for this flat video`. It assumes
an equirectangular mapping within the stated field of view, which may not hold for action-camera
footage (lens projections other than equirectangular will look distorted). It is a command-line
flag only, not an ini key, because it describes the file and not the glasses.

`--mpv-scale off|NAME` (default `off`; ini key `mpv_scale`) lets mpv enlarge a flat picture
with its own scaler `NAME` (mpv's `scale` option, e.g. `spline36` or `ewa_lanczossharp`; mpv's
chroma scaler follows it unless `--mpv-opt cscale=...` sets one). See
[Viewing comfort and sharpness](#viewing-comfort-and-sharpness). A name mpv does not know exits 1
with mpv's own reason (its wording depends on the mpv version; libmpv 2.6 prints
`mpv: [cplayer] Invalid value for option scale: bogus`) followed by
`xrdome: --mpv-scale bogus: mpv rejects it as a scaler (mpv --scale=help lists them)`. `--mpv-opt scale=...` wins over it.

`--mpv-profile NAME` selects the mpv profile applied before mpv starts. The default is `fast`.
xrdome has mpv render the video at its native size into an offscreen buffer, with no scaling
(unless `--mpv-scale` is on), so mpv's default quality settings (high-quality chroma scaling, float intermediate buffers,
dithering) cost GPU time without a visible benefit; on a 7680x2880 file they saturated an
integrated GPU. `--mpv-profile default` applies no profile. `--mpv-opt KEY=VALUE` sets any mpv
option after the profile and xrdome's own fixed options (`vo`, `hwdec`, `osd-level`), so it can
override them; mpv rejecting an option or profile exits 1. After the first frame xrdome prints
`mpv: profile=NAME` and the `--mpv-opt` pairs to stderr, to match a timing log to its settings.
To diagnose a slow GPU, compare `--mpv-profile default` against the default run, and try
`--mpv-opt gpu-dumb-mode=yes` as the cheapest render path.

`--timing-log PATH` writes one CSV row per presented frame:
`frame,interval_us,pose_to_swap_us,pose_age_us,swap_us,frames_out_us` (swap-to-swap interval, pose
sample to swap call, age of the pose at the swap, time spent inside the swap call, CPU time spent
sending video frames to the depth helper; `0` for the first frame, before any pose, or when no
frames go out). The log is line buffered,
so `tail -f` works. At exit stderr always shows a summary such as
`timing: 18000 frames, 0 missed vsyncs (swap interval > 20 ms), pose-to-swap max 1.234 ms, median 0.812 ms (last 4096 frames)`
(the max covers every frame; the median is computed from the most recent 4096 frames) and,
with the UDP backend, `pose: 0 malformed UDP packets dropped`. Targets: 0 missed vsyncs over
5 minutes and max pose-to-swap under 4 ms.

stderr shows `hwdec: vaapi` when VA-API decoding is active; anything else prints one warning.
If the glasses' display disconnects during playback, the player pauses and exits with status 0.

`--sbs-switch auto|off` (default `auto`; ini key `sbs_switch`): with `auto`, when no 3840x1080
output is found at start, xrdome asks XRLinuxDriver to switch the glasses to side-by-side, waits
for the output, and switches them back at exit; see [Switching to SBS](#switching-to-sbs). `off`
never asks and fails as before. `--windowed` never looks for the glasses' output, so never asks.

### Config file

xrdome reads `$XDG_CONFIG_HOME/xrdome/config.ini`, or `~/.config/xrdome/config.ini` when
`XDG_CONFIG_HOME` is unset, empty or not an absolute path (Windows: `%APPDATA%\xrdome\config.ini`,
unless `XDG_CONFIG_HOME` is set and absolute). A missing file is fine. Precedence, lowest
to highest: built-in defaults, built-in device values, the ini file, the command line.

`[general]` keys: `pose` (same spec as `--pose`, default `udp-opentrack:port=4242`), `output`
(same as `--output`, default `auto`), `screen` (`fixed` or `locked`, as `--screen`, default
`fixed`), `resume` (`yes` or `no`, as `--resume`, default `no`; see
[Resume](#resume-where-you-left-off)), `screen_curve` (`off` or a radius from 1 to 10, as
`--screen-curve`, default `off`), `sbs_switch` (`auto` or `off`, as `--sbs-switch`, default
`auto`), `ffprobe` (the program run for format detection, default `ffprobe` looked up on
`PATH`), `deband` (`yes` or `no`, also `on` or `off`, as `--deband`, default `no`),
`mpv_scale` (`off` or an mpv scaler name, as `--mpv-scale`, default `off`), `sharpen` (0 to 1,
as `--sharpen`, default 0, off), `depth_helper` (as `--depth-helper`, default `xrdome-tools`) and
`hwdec` (as `--hwdec`, default by platform).

`[device.<name>]` keys, read for the device selected with `--device` (default `viture-pro`):
`fov_diag` (diagonal FOV in degrees, 10 to 120), `zoom` (the VR180 zoom, 0.25 to 4, default 1)
and `flat_zoom` (the flat zoom, 0.25 to 4, default 1.5 for both built-in devices). Built-in devices are
`viture-pro` (43) and `xreal-air2pro` (45); a section for any other name defines a new device and
must set `fov_diag`. Sections for other devices are ignored. Lines starting with `#` are comments;
unknown keys and bad values are errors that name the file and line.

```ini
[general]
pose = udp-opentrack:port=4242,yaw=-1
output = auto
screen = fixed
resume = no
screen_curve = off
sbs_switch = auto
ffprobe = /usr/bin/ffprobe
deband = no
mpv_scale = off
sharpen = 0
depth_helper = xrdome-tools

[device.viture-pro]
fov_diag = 43
zoom = 1.0
flat_zoom = 1.5

[device.mydevice]
fov_diag = 50
```

## Flat video

An ordinary film (2D, or 3D packed side by side or top and bottom) is shown as a flat
rectangular screen in front of you, fixed in the room: turn your head and the screen stays where
it is, so a screen larger than the glasses' view can be explored by looking around, "almost pan
and scan". Straight lines in the video stay straight at every head angle. The screen sits at the
glasses' own optical distance; there is no distance control. 3D films use the depth in the file
as it is.

At zoom 1 the whole picture just fits the display, as in plain mpv (a 2.39:1 film is
letterboxed, a 4:3 picture pillarboxed). The default flat zoom is 1.5 (`flat_zoom` in
`config.ini`, separate from the VR180 `zoom`), so the screen is larger than the view. `[` and `]`
change it, x1.1 per step from 0.25 to 4. Each projection keeps its own zoom during a run, so `P`
switches to the other projection's zoom rather than carrying the number over.

Two zoom modes (`Z`, or `--zoom-mode`, remembered per file):

- `screen` (default): zoom sets the size of the screen. The view keeps the display's true field
  of view, so the picture moves exactly as much as your head turns.
- `magnify`: the screen stays at the size of zoom 1 and zoom narrows the view instead, as zoom
  does for VR180. The picture moves zoom times faster than your head.

At the same zoom both modes show the same point of the picture at the centre of the view, at the
same scale, so switching does not jump. On a flat screen they show the same picture across the
whole view straight ahead; on a curved one they differ away from the centre (see below).

Screen size of a 16:9 picture on the Viture Pro XR (display 37.9 x 21.9 degrees), in screen mode,
as `I` prints it:

| Zoom | Screen (degrees) |
|---|---|
| 1.0 | 37.9 x 21.9 |
| 1.5 (default) | 54.5 x 32.3 |
| 2.0 | 69.0 x 42.2 |
| 3.0 | 91.7 x 60.2 |

Screen lock (`L`, `--screen fixed|locked`, `[general] screen`): locked, the screen follows your
head like the glasses' own mode (head tracking and mouse look are ignored; convergence still
applies). Turning the lock off recentres fully, so the screen reappears straight ahead; a tilted
head at that moment gives a tilted screen, as with `Shift+R`. The lock is not saved per file,
since it depends on where you sit, not on the film. It does nothing for VR180.

Curved screen (`C`, `--screen-curve off|R`, `[general] screen_curve`, default `off`): the screen
is bent into part of an upright cylinder, so the edges of a large screen are as far from you as
its centre and face you instead of being seen at a slant. R is the cylinder's radius in screen
distances, from 1 to 10. The screen's centre stays where the flat screen's is and the cylinder
touches the flat screen there, so R = 1 puts the cylinder's axis through your head and every
column of the screen at the same distance; a larger R bends it less and approaches the flat
screen. The screen keeps its width measured along the curve, and its height, so a curved screen
at a given zoom covers the same picture as a flat one but its ends come round towards you: at
zoom 1 with R = 1, a 16:9 screen's left and right edges sit just outside the view (39.3 degrees
apart instead of 37.9). Columns stay straight and upright; the top and bottom edges bow outwards
towards the sides, as the top and bottom of a cinema's curved screen do from its centre. `C`
switches between flat and curved; if `--screen-curve` was `off`, `C` curves with R = 1. Like the
lock, the curve is not saved per file. Magnify mode curves too: its zoom-1 screen is bent with
the same R (the arc keeps the zoom-1 screen's width, the height is unchanged) and zoom narrows the
view onto it, so `Z` never changes the curve. Straight ahead the two modes show the same point of
the picture at the view's centre, at the same scale; away from it they differ. In screen mode the
arc is zoom times longer and bends more across the view; in magnify mode the view lies on the
middle of the zoom-1 arc, which bends little there, so across the view the picture is nearer the
flat screen's: at zoom 2.5 with R = 1 the view's left and right edges show about 3% less of the
picture in screen mode than in magnify mode. Above zoom 1 the magnified view of a 16:9 picture
straight ahead lies wholly on the screen, so the arc's ends and the bowed top and bottom come into
view only when you turn towards them. A head-locked screen curves the same way. Nothing beside or
behind you is drawn, so an arc that would reach past 90 degrees to either side stops there (at
zoom 4 with R = 1 a 16:9 screen reaches 79 degrees).

Subtitles: for flat 2D, mpv's own subtitle rendering is left on, so a subtitle track (or an
`.srt` next to the file) appears on the screen, at the bottom of the picture. In 3D layouts and in
VR180 subtitles are turned off, because mpv would draw them once across the whole frame and split
them between the eyes; `M` and `P` switch them with the layout. `--mpv-opt sub-visibility=no`
turns them off for flat 2D too.

## Photos

An image file is shown as a flat mono screen, held in the room like a flat film and at the
photo's own shape, until you quit. An image is a file that `ffprobe` reports as coming from an
image demuxer (`image2`, `jpeg_pipe`, `png_pipe`, `webp_pipe`, ...; not an animated GIF); with no
working `ffprobe`, a file name ending in `.jpg`, `.jpeg`, `.png`, `.webp`, `.bmp`, `.tif` or
`.tiff`. The usual flags, keys and per-file settings work as for a flat film; a photo is `flat`
and `mono` unless a flag, the photo's saved record, or a file name word (`SBS`, `TB`, ...) says
otherwise (the `detect:` line says `image file`). There is no time to pause or seek in: Space and
the seek keys do nothing, and `--resume` is ignored.

```
xrdome photo.jpg
xrdome ~/Pictures/holiday/
```

A directory argument opens its photos in name order (the bytes of the file names, so
`IMG_10.jpg` comes before `IMG_2.jpg`; names starting with `.`, other file types and
subdirectories are skipped) and shows the first. `PageDown` and `PageUp` go to the next and the
previous; they stop at the ends and do not wrap. Each press prints `photo: 2 of 5: name.jpg`
(`photo: 5 of 5 (end of the directory)` at a limit), then the usual `detect:` and `settings:
restored` lines for that photo. The old photo stays up until the new one is drawn. Zoom,
projection, layout and the other per-file settings are remembered for each photo (see
[Per-file settings](#per-file-settings)); the filter, sharpening, curve, screen lock and mouse look
carry over from one photo to the next. `I` also prints `info: photo: 2 of 5`. A single photo has no
neighbours: the keys do nothing. A photo mpv cannot decode ends xrdome with an error, as a film
would.

Orientation: mpv turns a JPEG upright by its EXIF Orientation tag, and xrdome shows the picture
mpv draws, at the displayed shape (a portrait photo is a tall screen). Measured on 2026-11-19 with
a test picture of four coloured quadrants and each of the eight tags, against what Pillow's
`ImageOps.exif_transpose` (the EXIF transpose the planned depth helper will apply) makes of the
same file:

| mpv, FFmpeg | tags 1, 3, 6, 8 (rotations; all a camera writes) | tags 2, 4, 5, 7 (mirrored) |
|---|---|---|
| 0.42.0, 9.0.2 (Gentoo, Arch) | as the transpose | as the transpose |
| 0.37.0, 6.1.1 (Ubuntu 24.04) | as the transpose | turned, not mirrored: 2, 4, 5, 7 are shown as 3, 1, 6, 8 |

### Depth 3D for photos

A photo whose depth is in the cache is drawn in 3D: each eye sees the picture's points shifted
sideways by their depth, nearer points out of the screen and further ones into it. Make the depth
with [`xrdome-tools depth photo FILE|DIR`](docs/tools.md#depth) (it needs `xrdome-tools setup
depth`); xrdome finds it by the sha256 of the photo's bytes, in
`$XDG_CACHE_HOME/xrdome-tools/depth/` (default `~/.cache/xrdome-tools/depth/`), and prints
`depth: 1365x2048 from the cache` when it opens the photo.

Without a cached depth, xrdome asks the depth helper for it and shows the photo in 2D meanwhile:
`depth: none cached; asking the depth helper`, then, about a second later (a few seconds for the
first photo, while the helper starts), `depth: 1365x2048 from the depth helper` and the photo turns
3D. In a directory the previous and next photos are asked for too, so PageDown usually lands on a
photo whose depth is ready. The helper is
[`xrdome-tools depth serve`](docs/tools.md#depth-serve), which needs `xrdome-tools setup depth`
(and a Vulkan GPU: it refuses to run on the CPU, which would cost xrdome frames). xrdome starts it
the first time it needs it, unless one is already running; it keeps its model loaded between
photos and runs, and exits by itself after 10 minutes with no xrdome connected. Its output goes to
`xrdome-depth.log` beside its socket.

- Socket: `$XDG_RUNTIME_DIR/xrdome-depth.sock`; without `XDG_RUNTIME_DIR` (macOS),
  `xrdome-depth-<uid>.sock` in `$TMPDIR`, or in `/tmp`.
- `--depth-helper PROGRAM|off` (ini `depth_helper`, default `xrdome-tools` on `PATH`): the
  program started as `PROGRAM depth serve --socket SOCKET`. `off` neither starts nor asks a helper:
  photos without cached depth stay 2D with `depth: none cached; 2D (xrdome-tools depth photo makes
  it)`.
- A helper that cannot be started, exits (`setup depth` not done, no GPU) or stops answering gives
  one `xrdome: note: depth helper: ...; photos without cached depth stay 2D` for the run, naming its
  log; xrdome goes on in 2D. A photo the helper cannot read gives a note for that photo.
- The socket is only read and written without waiting, once per frame; reading the finished depth
  file and uploading it happens in the frame its reply arrives.

```
xrdome ~/Pictures/holiday/                        # depth made on the fly
xrdome-tools depth photo ~/Pictures/holiday/      # or all of it ahead of time
```

- Disparity: a point of depth `z` (0 furthest, 1 nearest in the photo) is shifted by
  `strength * (z - plane)` picture widths between the eyes, half each way. The strength (default
  2% of the width, `-` / `=`, 0 to 5%) sets how deep the photo looks; the plane (default 0.5,
  `,` / `.`) is the depth that sits on the screen. Both defaults are provisional, to be chosen by
  eye on the glasses. Strength 0 is exactly the 2D picture. `S` swaps the eyes, which inverts the
  depth.
- Only a flat 2D (mono) photo is drawn in 3D; after `M` or `P` it is drawn as before. `I` prints
  `info: depth: on, strength 2.0%, plane 0.50` or why there is none.
- Each pixel searches 16 positions along its row for the nearest point that lands on it; the
  edges of near objects uncover a little of what is behind them, which shows the background beside
  the gap.
- A photo mirrored by its EXIF tag (2, 4, 5 or 7; cameras do not write these) is shown in 2D, with a
  `xrdome: note:` line, when mpv is older than 0.42: older mpv turns it without mirroring it (table
  above), so the depth would not line up. A depth whose shape does not fit the picture mpv shows
  (EXIF that mpv does not apply, outside JPEG) is dropped with a warning.

### Depth for films

Experimental, and not part of release 0.1.0: it is built and tested against a fake helper, but the
glasses run (the real model's rate, flicker and cost) has not been done.

A flat 2D film is drawn in 3D live from its depth (spec section 8 of
`docs/superpowers/specs/2026-11-19-depth-3d-design.md`), with the same stereo pass as a photo's, so
`-` / `=` (strength) and `,` / `.` (screen plane) work on it too. 3D is off at the start of a film;
`D` turns it on. Then each new frame is scaled on the GPU to 322x182 and read back a frame later,
never waiting, into a shared-memory ring that [`xrdome-tools depth serve`](docs/tools.md#depth-serve)
reads (xrdome starts it as for photos). The helper runs the 322x182 model at the film's rate up to
30 times a second and writes the depth back with the number of the frame it was made from; each
time a newer depth map is there, xrdome uploads it into the depth texture (the render loop never
waits for the helper). The film is in 2D until the first map arrives, and its depth is one or two
frames old, which is not noticeable in slow scenes. At 60 fps, every second frame has a new depth.
The helper smooths the depth over time (an exponential moving average of the depth and of the
range it is scaled with, weight 0.5 for the newest) and starts afresh at a scene cut, so the
picture does not shimmer; `xrdome-tools depth flicker` measures it.

```
depth: video frames to the depth helper (322x182 through /xrdome-4242-1)
depth: the depth helper reads /xrdome-4242-1
...
depth: video stopped: 900 frames out, 0 skipped (readbacks busy), 899 depth maps back; CPU 0.086 ms a frame on average, 4.425 ms max; 880 depth maps drawn, 1.4 frames old on average, 3 at most
```

The last line comes when 3D goes off or xrdome exits (the depth of the film goes with it). Only a flat mono film sends frames; a stereo
film, VR180 and photos never read anything back. With `--depth-helper off`, `D` says the film has
no depth source. The CPU time of each frame's sending and of uploading its depth is the
`frames_out_us` column of `--timing-log`. A helper that cannot take the frames gives one `xrdome: note:` and the film plays on
in 2D. `xrdome-tools depth bench-video` reports the rate the helper reaches.

## Format detection

Each file is opened as flat or VR180, with its layout and packing, without flags. Before mpv
opens the file, xrdome runs `ffprobe -v error -show_streams -show_format -i FILE` (no shell; killed
after 3 seconds; skipped when the argument is not a regular file). For each of projection, layout and
packing, the first of these that has a value wins:

1. A flag: `--proj`, `--layout`, `--packing`, when not `auto`.
2. The file's saved record (see [Per-file settings](#per-file-settings)).
3. Container metadata read by `ffprobe`.
4. File name hints.
5. The frame shape.
6. Fallback: VR180 side by side, what xrdome did before detection.

An image file (see [Photos](#photos)) skips the last two: what 1 to 4 leave open is flat and
`mono`.

Container metadata:

| ffprobe side data | Result |
|---|---|
| Stereo 3D `side by side` | layout `sbs-lr`; `sbs-rl` when inverted |
| Stereo 3D `top and bottom` | layout `tb-lr`; `tb-rl` when inverted |
| Stereo 3D `2D` | layout `mono` |
| Any other Stereo 3D type | ignored, one note naming it |
| Spherical Mapping, equirectangular | projection `hequirect` |
| Spherical Mapping, any other kind | projection `hequirect`, one note that it is not supported and may look distorted |

File name hints: the name is split into words at every character that is not a letter or digit;
matching ignores case, except for the two-letter words marked "capitals only", which occur in
ordinary titles.

| Word | Result |
|---|---|
| `VR180`, `180`, `180x180`, `VR` | projection `hequirect` |
| `SBS`, `3DH`, `LR` (capitals only) | layout `sbs-lr` |
| `RL` (capitals only) | layout `sbs-rl` |
| `HSBS` | layout `sbs-lr`, packing `half` |
| `FSBS`, `FULLSBS` | layout `sbs-lr`, packing `full` |
| `TB`, `OU` (capitals only), `3DV` | layout `tb-lr` |
| `HTB`, `HOU` | layout `tb-lr`, packing `half` |
| `MONO`, `2D` | layout `mono` |
| `3D` with no layout word | layout `sbs-lr` |

Frame shape, with `F` the frame's width over its height times the pixel aspect (taken as 1:1 when
the file has stereo metadata). It fills only what is still open:

| Still open | Shape | Result |
|---|---|---|
| projection and layout | `F` within 2% of 2.0 | VR180, `sbs-lr` |
| projection and layout | `F` 3.0 or more | flat, `sbs-lr` (full) |
| projection and layout | `F` within 3% of 0.889 (16:9 doubled in height) | flat, `tb-lr` (full) |
| projection and layout | anything else | flat, `mono` |
| projection only, layout side by side | `F` within 2% of 2.0 | VR180; otherwise flat |
| projection only, layout top-bottom | `F` within 2% of 0.5 | VR180; otherwise flat |
| projection only, layout mono | `F` within 2% of 1.0 | VR180; otherwise flat |
| layout only, VR180 | `F` within 2% of 1.0 | `mono`; within 2% of 0.5 `tb-lr`; otherwise `sbs-lr` |
| layout only, flat | as the flat rows of the first block | |

One line at open says what was decided and why:

```
detect: proj=hequirect (frame shape) layout=sbs-lr (frame shape)
detect: proj=flat (frame shape) layout=sbs-lr (file name "HSBS") packing=half (file name "HSBS")
detect: proj=flat (flag) layout=tb-lr (metadata) packing=full (auto)
```

The sources are `flag`, `record`, `metadata`, `file name "<word>"`, `frame shape`, `auto`
(packing only), `fallback` and `image file`; `packing=` is shown for flat 3D only. A failed or missing `ffprobe`
prints one `xrdome: note:` line and detection goes on without it; it never stops a file from
playing.

Known wrong guesses, each fixed with one key:

- A half side-by-side or half top-bottom 3D film with no tag and no name hint opens as flat 2D.
  Press `M` (half SBS is the next step).
- A flat video that is exactly 2:1 (some streaming series, phone cameras) opens as VR180. Press
  `P`.
- A side-by-side VR180 file that is not 2:1 (for example squeezed into 3840x2160), with no tag
  and no name hint, opens as flat 2D. Press `P`.
- A full top-bottom 3D film that is not 16:9 per eye opens as flat 2D. Press `M`.

`P` keeps a layout that came from a flag, the record, metadata, a file name or an `M` or `S` press;
a layout that was only a frame-shape or fallback guess is worked out again for the new projection
(the "layout only" rows above), so a 2:1 flat video that opened as VR180 becomes flat 2D with the
one press. `P` and `M` count as changes and are remembered for the file; a layout worked out again
by `P` is not.

## Per-file settings

xrdome remembers these settings for each video file and restores them the next time you open the
same file: the projection, the VR180 zoom and the flat zoom, layout (`sbs-lr`, `sbs-rl`, `tb-lr`,
`tb-rl`, `mono`), packing, zoom mode, swap-eyes, content FOV and convergence, and for a photo
the depth strength and screen plane (`-` `=` `,` `.`). The screen lock is not remembered. The playback position is remembered only when you ask for it with `--resume`
(see [Resume](#resume-where-you-left-off)).

Only settings you changed are stored. A setting counts as changed for a file when a key changed it
during the run (`[`, `]`, `S`, `M`, `P`, `Z`), when you gave its flag (`--zoom`, `--proj`,
`--layout`, `--packing`, `--zoom-mode`, `--content-fov`, `--convergence`; `auto` is not a flag
value that counts) on the command line, or when the file's record already held it. `--zoom` and
`[` `]` count for the projection in effect: on a flat file they set the flat zoom, on a VR180 file
the VR180 zoom. Settings you
never touched are left out of the record and keep following the defaults, so if you later change
`zoom` in `config.ini`, files whose zoom you never adjusted pick up the new value. A flag given
once is therefore remembered. This is the only way content FOV is remembered, since it has no key.
Zooming out and back in to the starting value does not count as a change.

Precedence, lowest to highest: built-in defaults, built-in device values, `config.ini`, the
file's record, command-line flags. A record value is used only where the matching flag was not
given. There is no flag for swap-eyes, so a recorded swap is always restored.

Records are one small file per video, in
`$XDG_STATE_HOME/xrdome/files/`, or `~/.local/state/xrdome/files/` when `XDG_STATE_HOME` is unset,
empty or not an absolute path (Windows: `%LOCALAPPDATA%\xrdome\files\`, unless `XDG_STATE_HOME` is
set and absolute). The directories are created on first save with mode 0700. The file
name is the video's key, `<size>-<hash>.ini`: the size in bytes, then a 64-bit FNV-1a hash (16
lowercase hex digits) over the first 64 KiB and the last 64 KiB of the file, for example
`8123456789-9f3c0a1b22d4e6f7.ini`. The key comes from the file's content, not its name, so
renaming or moving a file keeps its settings. A re-encode or a trim is a new file with no record.
If the argument is not a regular file you can read (a URL, a pipe, a missing file), memory is off
for that run and nothing is printed about it.

A record looks like this:

```ini
# xrdome per-file settings
version = 1
path = /home/nick/vr/coast-walk.mp4
saved = 2026-11-12
proj = hequirect
zoom = 0.7513
content_fov = 120x90
```

`path` and `saved` are labels for you (and for `grep`); xrdome never uses them to find or check a
record. The settings keys are `zoom` (the VR180 zoom, 0.25 to 4), `flat_zoom` (the flat zoom, 0.25
to 4), `layout`, `packing` (`half` or `full`), `zoom_mode` (`screen` or `magnify`), `swap` (`yes`
or `no`), `content_fov` (`HxV`, each 10 to 180), `convergence` (-5 to 5), `depth_strength` (0
to 0.05, picture widths) and `depth_plane` (0 to 1); a missing key means
"follow the defaults" (or, for `layout` and `packing`, detection). `position` (seconds, three
decimals) is only written with `--resume`.

`proj` (`hequirect` or `flat`) is written in every record: it is the projection in effect when
the player quit, and on the next open it decides the projection (`detect: proj=flat (record)`).
So once anything has been saved for a file, its projection is pinned to what you were looking
at; `P` changes it, `--forget` removes it. A flat record looks like this:

```ini
# xrdome per-file settings
version = 1
path = /home/nick/films/coast.mkv
saved = 2026-11-13
proj = flat
flat_zoom = 2.0000
```

Films saved under the old curved-screen workaround (`--layout mono --content-fov 60x34`, from
before xrdome had a flat projection) have records with `proj = hequirect`, so they keep opening as
VR180 with that layout. Press `P` once (it is saved), or run once with `--forget`.

The record is saved once, when the player exits for any reason (`Q`, closing the window, end of
file, the glasses being unplugged, an error), as long as at least one video frame was shown. A file
that never showed a frame (audio only, a text file, a truncated download) leaves no record, even
with `--content-fov` given. Nothing is written if nothing is set and no record existed, or if the
record would be the same as the one loaded. A failed save prints one
`xrdome: warning: cannot save settings: ...` line and does not change the exit status.

If `--forget` cannot delete the record, it prints one
`xrdome: warning: cannot forget settings record <path>: ...` line and carries on. The record that
could not be removed is then neither restored nor overwritten in that run.

stderr shows what happened, in lines that start with `settings:`:

```
settings: restored zoom=0.75 content-fov=120x90 proj=hequirect
settings: saved zoom=0.68 layout=sbs-rl proj=hequirect
settings: saved proj=flat flat-zoom=2.00
settings: forgot 8123456789-9f3c0a1b22d4e6f7
```

The words are `zoom`, `layout`, `swap`, `content-fov`, `convergence`, `proj`, `flat-zoom`,
`packing`, `zoom-mode` and `position` (`H:MM:SS`, only with `--resume`), in that order. `restored`
is printed at open, after the `detect:` line,
and lists only the values actually applied (not ones a flag overrode). `saved` is printed at exit when a record was written. `forgot` is printed by `--forget`
when it deleted a record. A file with no record prints nothing.

- `--no-memory`: do not read or write the record for this run. Use it for timing runs and
  comparisons.
- `--forget`: delete the file's record at startup (including an unreadable or newer one), then
  continue from the defaults. The run saves at exit as usual. With `--no-memory` as well, the
  record is still deleted and then nothing is read or written.

Both are command-line only.

### Resume (where you left off)

Off by default. With `--resume` (or `resume = yes` under `[general]` in `config.ini`) a file
reopens where it was left; `--no-resume` turns it off again for one run. If both flags are given,
the last one wins, and either flag beats the ini file.

While resume is on, the record gets a `position = SECONDS` line (three decimals, for example
`position = 3723.457`) when the player exits, and the next `--resume` run opens the file there
through mpv's `start` option. The restored line shows it, for example
`settings: restored zoom=0.75 proj=hequirect position=1:02:03`, and the saved line shows the
new one. The position is mpv's playback position at the moment of exit.

A position is saved and resumed only inside this part of the file: not in the first 30 seconds,
and not in the last 5% of the file or the last 60 seconds, whichever is shorter. So a position t
in a file of length d is used when `t >= 30` and `t < d - min(0.05 * d, 60)`. A finished film
therefore starts from the beginning next time; playing to the end of the file counts as
finishing it. Quitting outside that range removes the record's
`position` line. A file of 30 seconds or less never resumes.

- Without `--resume` a record is exactly what it was before this feature, and a `position` line
  already in it is kept as it is, neither used nor changed.
- `--no-memory` turns resume off as well: nothing is read or written.
- `--no-resume` (or no `--resume`) reads and writes the other settings as usual and leaves the
  `position` line alone.
- An explicit start, `--mpv-opt start=TIME`, wins over the record: the file opens at TIME, the
  record's position is not printed as restored, and the position at exit is saved as usual.
- A position past the end of the file (the file was replaced by a shorter one with the same key) is
  ignored: one `xrdome: note: saved position H:MM:SS is past the end of this file (H:MM:SS); starting
  from the beginning` line is printed and the file plays from the start. The `restored` line has
  already been printed by then, and shows the position. The duration is only known once mpv has
  opened the file, so xrdome gives mpv the start option first and seeks back to 0 when the
  position turns out to be unusable.
- A `position` that is not a number from 0 to 10000000 makes the record unusable, like any other bad
  value (see below).

To find the record for a video, search the `path` labels, then open it in an editor:

```sh
grep -l coast-walk ~/.local/state/xrdome/files/*
vim ~/.local/state/xrdome/files/8123456789-9f3c0a1b22d4e6f7.ini
```

The `path` label is the absolute path at the time of the last save, so after a rename it names the
old file. Unknown keys are ignored. A record that has a malformed line, no `version` or a value out
of range is not used: xrdome prints one `xrdome: warning: settings record PATH: ...` line, plays
the file from the defaults and does not overwrite the record at exit. A record with a `version`
above 1 or a `proj` other than `hequirect` or `flat` (written by a newer xrdome) is treated the same way,
with an `xrdome: note:` line instead. Fix the line with `vim`, or run once with `--forget` to
delete it. Records for deleted videos are not cleaned up; remove them by hand.

Verification: the feature passes the unit tests and the Xvfb smoke tests, run in a sandbox (Ubuntu
24.04, mpv 0.37, software GL). The flat keys (`proj` other than `hequirect`, `flat_zoom`, `packing`,
`zoom_mode`, `tb-lr` and `tb-rl`) have been run there and in Xvfb; on the glasses (2026-11-14) a
flat film's `proj = flat` and flat zoom were restored, and a VR180 record written before them still
restored. On the Viture Pro XR (2026-11-13) saving a zoom change at quit,
restoring it on reopen and keeping it across a rename were seen to work, and playing with the
record in use missed no more vsyncs than playing with `--no-memory`. Layout, swap-eyes, content
FOV and convergence records, `--forget` and the bad-record paths have not been run on the glasses.
`docs/STATUS.md` has the details.

## Viewing comfort and sharpness

**Close subjects.** The glasses show both eyes' images on a virtual screen a few metres away, so
content that is identical in both eyes appears at that distance, not at infinity. A VR180 camera
was set up for objects at infinity to line up, so a subject close to the camera is hard to fuse
at zoom 1 and above: it can split into two images. Zoom out (`[`) until the near subject becomes
one image. Each press prints the new zoom, and the value is remembered for the file (see
[Per-file settings](#per-file-settings)).

`--convergence DEG` (default 0, -5 to 5) is an experimental flag, not a recommended fix. It is a
display-side adjustment, not a property of the file: it shifts the left eye's picture left and the
right eye's picture right (by turning each eye's view inwards by half the angle), the same in
every layout, and does not change
what the camera recorded. Positive values move everything in the scene further away, so your eyes
must diverge more for distant content; negative values bring the scene nearer. On the Viture Pro
XR (tested 2026-11-12), values up to 2 degrees made no significant difference, and 5 degrees put
the scene out of focus until zoomed far out. Convergence was not tried on the XREAL Air 2 Pro. There are no
keys for it; it can be given on the command line and is remembered per file like the other
settings.

**Sharpness.** The glasses show about 51 display pixels per degree, while a 4K VR180 file has
about 11 source pixels per degree, so every source pixel is enlarged about five times. Two things
help:

- Zoom out (`[`): a wider view puts fewer display pixels on each source pixel, so the enlargement
  and the blur that comes with it shrink. Zooming in (`]`) magnifies the softness.
- The enlarging filter (`--filter`, `F` cycles through them; prints `filter: NAME`). Press `F` on
  a still frame to compare; a VR180 file, enlarged two to six times, shows the difference most.

| Filter | Texture reads per pixel | What it does to an enlarged picture |
|---|---|---|
| `bilinear` | 1 | The GPU's own enlargement: every edge becomes a straight ramp a whole source pixel wide, the blurriest way to enlarge; diagonals show a beaded pattern |
| `bicubic` (default) | 9 | Catmull-Rom: steeper edges and more fine detail, with a faint halo (a dark and a light fringe of up to 7% of the step) beside high-contrast edges |
| `bicubic-ar` | 13 | Catmull-Rom with anti-ringing: each pixel is held within the darkest and brightest of the 2 x 2 source pixels around it, so the edges are as steep and the halo is gone |
| `mitchell` | 9 | Mitchell-Netravali (B = C = 1/3): softer than Catmull-Rom (fine detail close to bilinear), smooth diagonals, a halo about half as strong |

`bicubic` stays the default until the filters have been compared on the glasses. Lanczos-2 was
also tried and dropped: it looked and measured the same as Catmull-Rom (within one level), at
more arithmetic. EWA (jinc) filters were not tried: they are not separable, so they need about 16
or more separate reads per pixel. Comparison pictures and numbers are in `docs/STATUS.md`.

The filters cost GPU time on every display pixel while the picture is enlarged (while it is
shrunk, mipmaps take over). On the owner's Radeon bicubic made no measurable difference against
bilinear (see "Performance notes"); `bicubic-ar` and `mitchell` have not been timed on the glasses
yet. To measure a filter, make separate runs of the same file, one per `--filter`, each with its
own `--timing-log`, and compare the logs; pressing `F` during a run mixes filters in one log.
`--mpv-log v` and `--mpv-log debug` are for diagnosis only: they print a lot and affect timing,
so leave them off for measurements.

**Sharpening (`--sharpen`, `Shift+F`).** After the eye pass has drawn both eyes, a second pass
can sharpen them with contrast-adaptive sharpening (CAS, from AMD FidelityFX): each display pixel
is pushed away from its four neighbours by an amount that shrinks where the local contrast is
already high or the pixel is near black or white, so flat areas and noise gain little and hard
edges get a smaller halo than with a plain sharpening filter. `--sharpen S` sets the strength
from 0 (off, the default) to 1 (CAS at its own maximum, `sharpness = 1`); `0.625` is CAS's own
default (`sharpness = 0`). `Shift+F` steps through off, 0.25, 0.5, 0.75 and 1 and prints
`sharpen: 0.50`; `I` shows it; `sharpen = 0.5` in `[general]` sets it at start. It is a viewing
preference, so it is not saved per file.

- It works in display pixels (3 x 3 around each pixel), after the enlarging filter, mipmaps and
  `--mpv-scale`, and before dithering. So it sharpens detail at the size of one or two display
  pixels: most on a picture shown about 1:1 (flat video near zoom 1, or with `--mpv-scale`), and
  less the more the picture is enlarged. In the sandbox a hard edge drawn 1:1 became a third
  steeper at strength 1, one enlarged 2x a sixth steeper, and one enlarged 4x a tenth; the soft
  edges of a VR180 picture enlarged 2 to 6 times change by a few levels. Try it on a low-bitrate
  or VR180 file, where softness shows most.
- Strength 1 on a hard edge between levels 50 and 200 leaves a dark and a light fringe of about
  25 levels on each side; at 0.5 about 9. Lower strengths are the safer default when you try it.
- Nothing crosses between the eyes, and the black around the picture (behind you, outside
  `--content-fov`, around the flat screen) stays black and is never sharpened into the picture's
  edge: the picture's border pixels are sharpened as if the picture continued.
- Cost: one more pass over the whole 3840x1080 output, 9 reads per pixel from a 10-bit
  intermediate texture (16 MB written and read again every frame). It has not been timed on the
  glasses yet; time it with a pair of runs, `--sharpen 0` against `--sharpen 0.5`, each with its
  own `--timing-log` (see [Performance notes](#performance-notes)). At 0 there is no extra pass.

**Banding: debanding.** Dark gradients and skies in a low-bitrate file (a 2.5 Mbit/s AV1 file
looked far worse than 20 Mbit/s H.264) show visible steps that the 10-bit texture and dithering
cannot remove, because the steps are in the decoded picture. mpv's `deband` filter smooths them
inside its render pass, before xrdome's texture and dithering. It is off by default: `--deband on`
(or `deband = yes` in `[general]`) turns it on at start, and `B` switches it during playback so
you can compare on the glasses; the key prints `deband: on` or `deband: off` and `I` shows it.
It is a viewing preference, not a property of the file, so it is not saved in the per-file record.
It costs GPU time in mpv's render pass: on the owner's integrated Radeon a 4K film went from 0.03%
to 0.75% missed vsyncs and an 8K VR180 file from 1.6% to 28% (see
[Performance notes](#performance-notes)); on a dark low-bitrate film the owner saw little
difference.
The strength is left to mpv: `--mpv-opt deband-iterations=N` (mpv default 1),
`--mpv-opt deband-threshold=N` (32), `--mpv-opt deband-range=N` (16) and
`--mpv-opt deband-grain=N` (48), for example `--deband on --mpv-opt deband-iterations=2`.
`--mpv-opt deband=yes` does the same as `--deband on`; an explicit `--mpv-opt deband=...` wins
over `--deband`, and the key then toggles whatever mpv currently has. mpv's `gpu-dumb-mode`
(which xrdome uses only on software GL) ignores it.

**Shrinking: mipmaps.** Flat video often has more pixels than the area it is drawn into, and a
picture drawn smaller than its source shimmers on fine detail and edges when the head moves.
While the picture is shrunk (at the centre of the view, with the current zoom, one display pixel
covers more than one source pixel), the video texture gets mipmaps (smaller copies, levels 0 to
3, regenerated only when mpv delivers a new frame) and the eye pass samples them trilinearly
instead of with the `F` filter. While the picture is enlarged nothing changes: no
mipmaps are made and `F` chooses the filter as before. This applies to both projections; VR180
is almost always enlarged. `--mipmap off` turns it off (command line only). `I` shows the filter
in use (`trilinear` while mipmaps are used) and the `video texture:` line shows `mipmap=on` or
`mipmap=off` for the first frame. On the Viture Pro XR (1920x1080 per eye), flat 2D in screen
mode:

| Source | Zoom 0.5 | Zoom 1.0 | Zoom 1.5 (default) | Zoom 2.0 |
|---|---|---|---|---|
| 1920x1080 | shrunk 2x, mipmaps | 1:1, none | enlarged 1.5x, none | enlarged 2x, none |
| 3840x2160 | shrunk 4x, mipmaps | shrunk 2x, mipmaps | shrunk 1.33x, mipmaps | 1:1, none |

On the owner's Radeon (2026-11-14, AC power) a 4K film at zoom 1.0 missed 8 of 3600 vsyncs with
`--mipmap off` and 13 with `on`; whether mipmaps visibly reduce shimmer was not settled (no clear
difference seen on one film at zoom 0.5).

**Enlarging flat video: mpv's scalers (`--mpv-scale`).** Normally mpv draws each frame into a
texture of the coded size and the eye pass enlarges it with the `F` filter. With
`--mpv-scale NAME`, while a flat picture is enlarged (the opposite of the mipmap case above: at
the centre of the view one source pixel covers more than one display pixel), mpv draws each
frame into a texture of the size the picture has on the glasses, enlarging it with its own scaler
(`spline36`, `ewa_lanczossharp`, ...), and the eye pass samples that texture at about 1:1 with
the `F` filter. Side-by-side and top-bottom frames keep their layout, each eye at its display
size. On the Viture Pro XR a 1080p film at the default zoom 1.5 is drawn into 2880x1620 instead
of 1920x1080. Because mpv then draws at the display's size, mpv shader options
(`--mpv-opt glsl-shaders=...`, e.g. FSRCNNX) act at that size too. Subtitles are drawn at the
larger size as well.

- Only flat video, and only while enlarged. A shrunk picture keeps the coded texture and
  mipmaps; VR180 always keeps the coded texture (to be 1:1 at the centre a VR180 texture would
  need about 17600x8800 texels for any file, 150 million). `I` says which applies:
  `info: mpv scale: spline36, 1920x1080 to 2880x1620` or
  `info: mpv scale: spline36, not in use (picture shrunk)`.
- The texture holds at most 3840x2160 texels (both eyes together), the frame size mpv already
  draws for a 4K film, and fits the GPU's largest texture; beyond that it is scaled down with
  its shape kept, and the eye pass enlarges the rest. 1080p mono reaches the cap only above zoom
  2; a 1080p half side-by-side film at zoom 1.5 (two 2880x1620 eyes) is capped to two
  2715x1527 eyes.
- Zooming resizes the texture once the zoom has stayed the same for a quarter of a second, so a
  held `[` or `]` does not reallocate on every step; until then the old texture is drawn larger
  or smaller. Each resize has mpv draw the current frame again at once (also while paused), so
  no black or half-drawn frame is shown.
- In side-by-side and top-bottom frames mpv's scaler sees both eyes as one picture, so within
  2.5 source pixels of the eyes' shared edge (for `spline36`) its kernel mixes in a little of the
  other eye's edge (4:2:0 chroma is already shared there without `--mpv-scale`). For ordinary 3D
  films both edges show the same part of the scene.
- Cost: mpv scales into more pixels with a heavier kernel every frame; see
  [Performance notes](#performance-notes). On the owner's Radeon it costs almost nothing for a
  1080p film; the look was "hard to tell, maybe slight improvements". Off by default.

### Wayland sessions (XWayland)

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

### macOS (experimental)

xrdome builds on macOS (Apple Silicon; CI builds it and runs the unit tests on `macos-latest`), but it
has not been run on glasses there. The platform is detected at build time; on Linux nothing
changes. On macOS: SDL uses Cocoa with a CGL GL 3.3 core context (forward-compatible, high-DPI
drawable), mpv gets no X11 display and decodes with `videotoolbox`, the glasses' window is
fullscreen in its own Space, and the automatic SBS switch is off (it needs XRLinuxDriver, which
is Linux only): switch the glasses to SBS with their own button. Head tracking comes over the same
OpenTrack UDP port from any sender, such as `xrdome-pose-viture` (see "Pose from VITURE's SDK",
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

### Windows 11 (experimental)

xrdome builds on Windows 11 x64 with MSYS2's UCRT64 toolchain (CI builds it and runs the unit
tests on `windows-latest`), but it has not been run on glasses there. On Windows: SDL uses its
windows driver with a WGL GL 3.3 core context, mpv gets no X11 display and decodes with
`d3d11va-copy` (libmpv's GL render API maps D3D11 frames into GL without a copy only through
ANGLE, so with WGL the decoded frame is copied; `--require-hwdec` refuses a `-copy` decoder, so do
not pass it there), the glasses' window is desktop fullscreen on their display, and the automatic
SBS switch is off (XRLinuxDriver is Linux only): switch the glasses to SBS with their own button.
Head tracking comes over the same OpenTrack UDP port (from `xrdome-pose-viture`, see "Pose from
VITURE's SDK", or from opentrack itself); without one use `--pose null`. Config is in `%APPDATA%\xrdome\config.ini` and per-file records in
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

## Diagnostics

`--sync-after-swap` calls `glFinish()` right after `SDL_GL_SwapWindow`, before the frame is reported
to the player. On Mesa/X11 the swap can return at once and the driver may block on vsync only at the
next frame's first draw, which is after the pose sample, so each pose would be about one refresh
old. It exists to compare `pose_to_swap_us` in the timing log with and without the flag; it is
not meant for normal playback. `swap_us` in the log is the time spent in the swap call (plus the
`glFinish` when the flag is on).

## Install on Gentoo

`packaging/gentoo/` holds a live ebuild, `media-video/xrdome-9999`, that builds `main` from GitHub
with meson and installs `/usr/bin/xrdome`, and a versioned one, `xrdome-0.1.0.ebuild`, that builds
the release tarball once it is published (see [Releases](#releases)). Neither is in an overlay yet;
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
`--lib`; the helper's own [README](helpers/xrdome-pose-viture/README.md) has them all.

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

## Performance notes

Measured 2026-11-12 on a ThinkPad P14s Gen 6 AMD (integrated Radeon, Mesa, Xorg/MATE) driving
Viture Pro XR glasses at 3840x1080 60 Hz, with a 4320x2160 10-bit AV1 60 fps file, VA-API decode,
10-bit video texture and dithering on:

| Settings | Frames | Swap intervals over 20 ms |
|---|---|---|
| mpv default render path (before `--mpv-profile fast` became the default), 8-bit texture | 456 | 251 (55%) |
| Defaults: profile `fast`, bicubic (filter toggled during the run) | 15345 | 96 (0.6%) |
| `--filter bilinear` | 4547 | 29 (0.6%) |
| `--mpv-opt gpu-dumb-mode=yes` | 2482 | 13 (0.5%) |

- mpv's `fast` profile is what fixed the frame rate. Bicubic sampling and mpv's dumb mode make no
  measurable difference on top of it, so the defaults stay `fast` and `bicubic`.
- Median time from pose sample to swap call: 0.003 ms. The swap, not the draw, is where the loop
  waits for vsync on this driver, so `--sync-after-swap` is not needed here.
- What the remaining 0.5 to 0.6% of late frames are has not been analysed.
- Decode ceilings on this GPU: 7680x2880 AV1 at 47 fps, the same frame size in HEVC at 132 fps.
- H.264 hardware decode on this GPU is limited to 4096x4096, so a 4320x2160 H.264 file falls back
  to software decode (`hwdec: no`; mpv prints the reason). Software decode of that file at 60 fps
  and 20 Mbit/s still kept up: 446 of 34376 swap intervals (1.3%) exceeded 20 ms. HEVC and AV1 do
  not have this size limit here.
- That holds on AC power. On battery with the `balanced` platform profile (2026-11-13) the same
  kind of file missed 57 to 74% of swap intervals over 3600 frames; plugged in, 0 of 3600. If
  software-decoded playback stutters, check the charger first.
- Source bitrate matters more than anything the player does: the same resolution looked much
  better at 20 Mbit/s H.264 than at 2.5 Mbit/s AV1.
- Mipmaps (2026-11-14, AC power, 3600 frames per run, no keys): a 3840x2160 AV1 flat film at
  `--zoom 1.0` missed 8 vsyncs (0.22%) with `--mipmap off` and 13 (0.36%) with `on`. A 7680x3840
  AV1 VR180 file at `--zoom 0.5` missed 113 (3.1%) without mipmaps and 85 (2.4%) with
  `--mipmap-force`, which makes the chain every frame. One pair each; the differences are within
  run-to-run noise. A flat 1080p AV1 film missed 0 of 10754.

- Debanding (2026-11-14, AC power, 3600 frames per run, no keys): a 3840x2160 AV1 flat film at
  zoom 1.5 missed 1 vsync with `--deband off` and 27 (0.75%) with `on`; a 7680x3840 AV1 VR180 file
  at zoom 1.0 missed 59 (1.6%) and 1015 (28%). At 8K mpv's deband saturates the GPU.
- `--mpv-scale` (2026-11-14, same conditions), a 1080p AV1 film: at zoom 1.5 (texture 2880x1620)
  `off` 0 missed, `spline36` 2, `ewa_lanczossharp` 2; at zoom 2.0 (texture 3840x2160, the cap) 1, 1
  and 7. Cheap on this GPU.
- The 4K AV1 film's late frames fall on a spike every 32 display frames (16 source frames),
  present with and without deband; not explained.

## Tracking notes

Measured 2026-11-11, Viture Pro XR (`35ca:101d`), XRLinuxDriver 2.11.9, Gentoo/OpenRC:

- About 365 packets per second, no malformed packets.
- The packet carries the driver's NWU Euler angles: `q = Rz_up(yaw) * Ry_left(pitch) * Rx_forward(roll)`.
- The pose is absolute in the driver's own world frame. Looking level and forward read roughly
  yaw -157, pitch 12, roll -176, so raw angles are not usable directly.
- Rotation relative to a reference pose, `conj(q_ref) * q`, has standard NWU body axes:
  head left is positive about up, looking up is negative about left, right ear down is positive about forward.
- Yaw drift at rest (2026-11-14): 0.02 degrees in 5 minutes, glasses lying still, hours after
  they connected.

XREAL Air 2 Pro (`3318:0432`), same driver, 2026-11-14: about 1000 packets per second, no malformed
packets; head tracking works with `--device xreal-air2pro` much as on the Viture (owner; the axis
signs were not checked one by one).

- **Keep the XREAL still while the driver calibrates it**, about 16 seconds after it is plugged in
  (or the driver started), until its log says `Device calibration complete`. Calibrated while
  being handled or worn, its yaw slid 3 to 17 degrees over the next half minute, which shows in
  xrdome as the picture drifting sideways after a recenter; calibrated lying still, it moved 0.06
  degrees in a minute. Worn and still long after calibration it drifted 0.08 degrees per minute.
  The plan's Task 10 already asked for 15 still seconds; this is the measurement.
- Whether xrdome's automatic SBS switch (which restarts the glasses' IMU stream in the driver)
  has the same effect is not known: the picture drifted after it twice, six minutes after
  calibration.

Hardware-check notes for tracking:

- Look at `pose_to_swap_us` per frame, not only the max. If it often sits near 8-16 ms while
  `interval_us` is about 16.7 ms, re-run with `--sync-after-swap` and compare.
- Time how long the player takes to exit after the glasses are unplugged (XREAL Air 2 Pro,
  2026-11-14: about 0.3 s; the display goes before the USB device does).
- Use a file the machine can decode at full rate.

## Hardware notes

From `xrandr --props` and `edid-decode` on the owner's laptop, 2026-11-14 (full captures in
`docs/hardware/`):

| Glasses | USB ID | EDID vendor, product name | 1080p mode | SBS mode |
|---|---|---|---|---|
| Viture Pro XR | `35ca:101d` | `CVT`, `VITURE` | 1920x1080 at 60 (preferred), 90, 120 Hz | 3840x1080 at 60 Hz only |
| XREAL Air 2 Pro | `3318:0432` | `MRG`, `Air 2 Pro` | 1920x1080 at 60 (preferred), 90, 120 Hz | 3840x1080 at 60 Hz only |

- Both report the same placeholder EDID serial number (0x88888800), so the serial cannot tell
  two pairs apart.
- After a replug the glasses' output can come back under another name (`DisplayPort-1`, then
  `DisplayPort-0`); xrdome finds the glasses' output by its size, not its name.
- Each pair has a button that switches between 1080p and SBS: Viture, the small button at the
  front left, held 2 seconds; XREAL, volume up on the right arm, held 2 seconds.

Known limits:

- The driver sends Euler angles, so the pose is ambiguous near 90 degrees of pitch (looking
  straight up or down).
- Yaw can drift slowly over a long session; `R` recentres it. See
  [Tracking notes](#tracking-notes) for the XREAL's calibration.
- A 4K VR180 file has about 11 source pixels per degree against the glasses' 51, so it looks soft
  however it is scaled; see [Viewing comfort and sharpness](#viewing-comfort-and-sharpness).

## Releases

A release is a source tarball, `xrdome-X.Y.Z.tar.xz`, made with `meson dist` by the `dist` job in
CI, with its `.sha256sum`. It holds the sources, tests, docs and `xrdome-tools`; it ships no mpv
or FFmpeg. Build it as in [Build and run](#build-and-run). Release 0.1.0 is Linux only; what is in
it, what is experimental (macOS, Windows, depth for films, the VITURE pose helper) and the known
limits are in [CHANGELOG.md](CHANGELOG.md). The macOS and Windows zips CI makes are test builds,
not releases.

On a pushed `v*` tag CI builds the tarball again and attaches it to a **draft** GitHub release,
with the matching CHANGELOG section as its notes; the maintainer publishes it by hand.

### Release checklist (maintainer)

1. Bump `version` in `meson.build`.
2. In `CHANGELOG.md`, move `[Unreleased]` into the new version's section and give it the date.
3. Merge to `main` through a pull request with CI green, including `dist`.
4. Tag and push: `git tag -a vX.Y.Z -m "xrdome vX.Y.Z"` (or `-s` with a signing key), then
   `git push origin vX.Y.Z`.
5. Wait for CI to finish the draft release, check its notes and the two attached files, publish it.
6. Generate the Gentoo `Manifest` (`ebuild xrdome-X.Y.Z.ebuild manifest`) and put the tarball's
   sha256 in `packaging/arch/PKGBUILD` in a follow-up pull request.

## Licence

GPL-2.0-or-later, the same terms as mpv built with its GPL files (see `Copyright` in the
[mpv repository](https://github.com/mpv-player/mpv)); xrdome links libmpv. The full text is in
`LICENSE`. The sharpening filter in `src/render/shaders/sharpen.frag` is a port of AMD FidelityFX
CAS and stays under AMD's MIT licence, whose notice is in that file (and in the shader text
compiled into the program); MIT is compatible with the GPL. `helpers/xrdome-pose-viture/` is a separate
program under the MIT licence (its own `LICENSE`); it shares no source with xrdome and never contains
VITURE's SDK, which is proprietary and is loaded from the user's own copy.
