# Usage

## On the glasses

Start the driver (see [Driver](setup.md#driver)), then:

```sh
./build/xrdome file.mp4
```

Glasses in 1080p (mirror) mode are switched to SBS (3840x1080) through the driver and back
at exit (see [Switching to SBS](setup.md#switching-to-sbs)); without the driver, switch them to SBS by hand
first.

The first pose received becomes "straight ahead" (the driver's absolute frame is arbitrary), so
look straight ahead when starting, or press `R`. If head right does not move the image left, or
head up does not move it down, flip the axis in the pose spec, e.g.
`--pose udp-opentrack:port=4242,yaw=-1` (keys `yaw`, `pitch`, `roll`, each `1` or `-1`).

## Keys

| Key | Action |
|---|---|
| Space | Pause / resume |
| Left / Right | Seek -10 s / +10 s |
| Up / Down | Seek +60 s / -60 s |
| PageDown / PageUp | Next / previous photo, when a directory was opened (see [Photos](photos.md#photos)); prints `photo: 2 of 5: name.jpg` |
| R | Recenter, yaw only (keeps the current pitch and roll reference) |
| Shift+R | Recenter, full |
| [ / ] | Zoom out / in (x1.1 per step, 0.25 to 4); prints `zoom: 1.10` |
| F | Cycle the enlarging filter: bilinear, bicubic, bicubic-ar, mitchell, then bilinear again; prints e.g. `filter: bicubic-ar`. See [Sharpness](picture-quality.md#viewing-comfort-and-sharpness) |
| Shift+F | Sharpening strength: off, 0.25, 0.5, 0.75, 1, then off again (from a `--sharpen` value in between, the next step above it); prints `sharpen: 0.25` or `sharpen: off`. Not remembered per file |
| S | Swap eyes |
| M | Cycle layout. Flat: 2D, half SBS, full SBS, half top-bottom, full top-bottom; prints e.g. `layout: sbs-lr half` or `layout: mono`. VR180: sbs-lr, sbs-rl, tb-lr, mono; prints e.g. `layout: sbs-rl` |
| P | Switch projection, flat or VR180; prints `proj: flat` or `proj: hequirect` |
| Z | Zoom mode, flat only; prints `zoom mode: screen` or `zoom mode: magnify` |
| L | Screen lock, flat only; prints `screen: head-locked` or `screen: fixed` |
| B | Debanding on / off (mpv `deband`); prints `deband: on` or `deband: off`. Not remembered per file |
| C | Curved screen on or off, flat only; prints `screen curve: 1.00` or `screen curve: off` |
| D | Depth 3D on / off, for a photo with depth (see [Depth 3D for photos](photos.md#depth-3d-for-photos)); prints `depth: off, strength 2.0%, plane 0.50`. On at start; carries over from one photo to the next. On a flat 2D film: off at start; on sends the film's frames to the depth helper (see [Depth for films](photos.md#depth-for-films)) |
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

## Flags

`--pose SPEC` (`null`, or `udp-opentrack` with `port`, `yaw`, `pitch`, `roll`), `--output NAME`,
`--device viture-pro|xreal-air2pro`, `--zoom F`, `--proj auto|hequirect|flat`,
`--layout auto|sbs-lr|sbs-rl|tb-lr|tb-rl|mono`, `--packing auto|half|full`,
`--zoom-mode screen|magnify`, `--screen fixed|locked`, `--screen-curve off|R`, `--mipmap on|off`,
`--windowed`, `--window-size WxH`, `--window-eye both|left|right`, `--sbs-switch auto|off`,
`--hwdec NAME` (mpv's hwdec; default `vaapi` on Linux, `videotoolbox` on macOS, `d3d11va-copy`
on Windows; for example
`nvdec` or `auto-safe` on an NVIDIA machine), `--require-hwdec` (exit 1 unless mpv decodes
zero-copy: any decoder but `no` and the `-copy` ones), `--timing-log PATH`,
`--content-fov HxV`, `--sync-after-swap` (diagnostic, see [Diagnostics](#diagnostics)),
`--mpv-profile NAME`, `--mpv-opt KEY=VALUE` (repeatable, up to 16), `--mpv-log LEVEL`,
`--mpv-scale off|NAME`, `--convergence DEG`, `--filter bilinear|bicubic|bicubic-ar|mitchell`,
`--sharpen S`, `--fbo-format rgb10|rgba8`, `--dither on|off`, `--deband on|off`,
`--depth-helper PROGRAM|off` (see [Depth 3D for photos](photos.md#depth-3d-for-photos)), `--no-memory`, `--forget`,
`--resume`, `--no-resume` (the last four are described under
[Per-file settings](per-file-settings.md#per-file-settings)).

`--window-size WxH` sets the size of the `--windowed` desktop window in pixels (default
1280x360; each value 16 to 8192) and `--window-eye left|right` fills the window with one eye
instead of both side by side (default `both`); either one implies `--windowed`. With both eyes
each gets half the width, so `--window-size 1920x540` gives 960x540 an eye; with one eye the
window is that eye, so `--window-eye left --window-size 1920x1080` is the left eye as the glasses
show it (without a size, one eye is 640x360). They are for demos, screenshots and screen
recordings without glasses (see [Recording a demo clip](setup.md#recording-a-demo-clip)); the
one-eye window is drawn through an offscreen target twice its width and has not been timed.

`--proj auto|hequirect|flat` (default `auto`) chooses the projection: `hequirect` is VR180
(half-equirectangular), `flat` an ordinary flat screen (see [Flat video](flat-video.md#flat-video)), `auto`
detects it (see [Format detection](detection.md#format-detection)). `--layout` (default `auto`, detected)
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
picture; see [Flat video](flat-video.md#flat-video). `--screen fixed|locked` (default `fixed`, flat only)
keeps the screen fixed in the room or locks it to the head. `--screen-curve off|R` (default `off`,
flat only; R from 1 to 10) bends the screen into part of a cylinder; see
[Flat video](flat-video.md#flat-video). `--mipmap on|off` (default `on`) uses
mipmaps while the picture is shrunk; see [Viewing comfort and sharpness](picture-quality.md#viewing-comfort-and-sharpness).

`--convergence DEG` (default 0, -5 to 5) turns each eye's view by half the angle in opposite
directions; positive turns the eyes inwards. `--filter` chooses how the eye pass enlarges the
video: `bicubic` (Catmull-Rom, the default), `bicubic-ar` (Catmull-Rom without its halo),
`mitchell` (softer, a much fainter halo) or `bilinear`. Neither is an ini key.
`--convergence` is also remembered per file (see [Per-file settings](per-file-settings.md#per-file-settings)), and
`--filter` is not. See [Viewing comfort and sharpness](picture-quality.md#viewing-comfort-and-sharpness).

`--sharpen S` (default 0, off; 0 to 1; ini key `sharpen`) sharpens each eye's picture after it
has been drawn, with contrast-adaptive sharpening (AMD FidelityFX CAS); 1 is the strongest. At 0
the eye pass is exactly what it is without the flag, at no cost. `Shift+F` steps it during
playback. See [Viewing comfort and sharpness](picture-quality.md#viewing-comfort-and-sharpness).

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
[Viewing comfort and sharpness](picture-quality.md#viewing-comfort-and-sharpness). A name mpv does not know exits 1
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
for the output, and switches them back at exit; see [Switching to SBS](setup.md#switching-to-sbs). `off`
never asks and fails as before. `--windowed` never looks for the glasses' output, so never asks.

## Config file

xrdome reads `$XDG_CONFIG_HOME/xrdome/config.ini`, or `~/.config/xrdome/config.ini` when
`XDG_CONFIG_HOME` is unset, empty or not an absolute path (Windows: `%APPDATA%\xrdome\config.ini`,
unless `XDG_CONFIG_HOME` is set and absolute). A missing file is fine. Precedence, lowest
to highest: built-in defaults, built-in device values, the ini file, the command line.

`[general]` keys: `pose` (same spec as `--pose`, default `udp-opentrack:port=4242`), `output`
(same as `--output`, default `auto`), `screen` (`fixed` or `locked`, as `--screen`, default
`fixed`), `resume` (`yes` or `no`, as `--resume`, default `no`; see
[Resume](per-file-settings.md#resume-where-you-left-off)), `screen_curve` (`off` or a radius from 1 to 10, as
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

## Diagnostics

`--sync-after-swap` calls `glFinish()` right after `SDL_GL_SwapWindow`, before the frame is reported
to the player. On Mesa/X11 the swap can return at once and the driver may block on vsync only at the
next frame's first draw, which is after the pose sample, so each pose would be about one refresh
old. It exists to compare `pose_to_swap_us` in the timing log with and without the flag; it is
not meant for normal playback. `swap_us` in the log is the time spent in the swap call (plus the
`glFinish` when the flag is on).
