# xrdome

A Linux player for VR180 video and ordinary flat video on XR glasses (Viture Pro XR, XREAL Air 2
Pro) with head tracking.

Status: plays a side-by-side VR180 file on the glasses with head tracking from
XRLinuxDriver (OpenTrack UDP), keyboard controls, a frame-timing log and per-file settings
memory. On the Viture Pro XR, with the default mpv profile, bicubic sampling and a 10-bit
texture, 0.6% of swap intervals were late (see [Performance notes](#performance-notes)). Per-file
settings memory was run on the Viture Pro XR on 2026-11-13: saving a zoom change, restoring it,
and keeping it across a rename were seen to work; the other remembered settings and `--forget`
have only passed the unit and Xvfb smoke tests. The XREAL Air 2 Pro is untested. See `docs/STATUS.md`, `docs/superpowers/specs/2026-11-11-xrdome-design.md` and
`docs/superpowers/plans/2026-11-11-xrdome.md`.

Flat video and format detection (an ordinary film opens as a flat screen fixed in the room, a
VR180 file opens as before, both without flags; see [Flat video](#flat-video) and
[Format detection](#format-detection)) were run on the Viture Pro XR on 2026-11-14: flat 1080p
and 4K films opened flat at 1.5 by frame shape, and the owner's VR180 files as VR180; the screen
stayed put when the head turned, `[` `]` `Z` `L` `R` `?` `I` and Up/Down worked; half SBS (a test
clip) showed the right depth and `S` inverted it; flat and VR180 records restored; mipmaps cost no
more than 0.14 percentage points of missed vsyncs. Not seen on the glasses: whether mipmaps reduce
shimmer, a real half-SBS film, subtitles, top-bottom and full SBS. `docs/STATUS.md` has the
details.

## Build and run

Dependencies: `media-video/mpv` (with `libmpv` and `vaapi` USE flags), `media-libs/libsdl2`
(with `X` and `opengl`), `media-libs/libepoxy`, `media-libs/mesa` (EGL), `dev-util/cmocka` and
`dev-build/meson`. At run time xrdome also runs the `ffprobe` program from `media-video/ffmpeg`
(already a dependency of mpv) to detect the format; without it every file plays as VR180 side
by side unless flags say otherwise (one `xrdome: note:` line). Nothing from FFmpeg is linked.
The test patterns and the smoke tests also need `media-video/ffmpeg` (`x264`/`x265`) and
`x11-misc/xvfb-run`.

```sh
meson setup build
meson compile -C build
meson test -C build
```

CI builds with warnings as errors and runs the whole suite, including both Xvfb smoke tests, on Ubuntu 24.04 (mpv 0.37).

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
| Up / Down | Seek +60 s / -60 s |
| R | Recenter, yaw only (keeps the current pitch and roll reference) |
| Shift+R | Recenter, full |
| [ / ] | Zoom out / in (x1.1 per step, 0.25 to 4); prints `zoom: 1.10` |
| F | Toggle bicubic / bilinear sampling; prints `filter: bicubic` or `filter: bilinear` |
| S | Swap eyes |
| M | Cycle layout. Flat: 2D, half SBS, full SBS, half top-bottom, full top-bottom; prints e.g. `layout: sbs-lr half` or `layout: mono`. VR180: sbs-lr, sbs-rl, tb-lr, mono; prints e.g. `layout: sbs-rl` |
| P | Switch projection, flat or VR180; prints `proj: flat` or `proj: hequirect` |
| Z | Zoom mode, flat only; prints `zoom mode: screen` or `zoom mode: magnify` |
| L | Screen lock, flat only; prints `screen: head-locked` or `screen: fixed` |
| B | Debanding on / off (mpv `deband`); prints `deband: on` or `deband: off`. Not remembered per file |
| ? | Print the key list to stderr (`keys:` lines) |
| I | Print playback information to stderr (`info:` lines) |
| Q | Quit |

The `zoom:` and `filter:` lines are printed only when the value changes (not for
a press at a limit). `P` also prints the other projection's zoom, since each projection keeps its
own. In VR180, `Z` prints `zoom mode: magnify (VR180 has no screen mode)` and `L` prints
`screen: fixed (VR180 cannot be head-locked)`, and neither changes anything. `?` is Shift+/ on a
US keyboard. Key repeat is ignored except for zoom and seek.

`I` prints, one item per line: the file name; codec, coded frame size and frame rate; `hwdec`;
projection, layout and (flat 3D) packing, each with where it came from (the sources of the
`detect:` line, or `key`); zoom, and for flat video the zoom mode and the screen size in degrees;
screen fixed or head-locked; the filter in use (`trilinear` while mipmaps are used) and whether
mipmaps are on; the video texture; `deband` on or off; frames presented and missed vsyncs so far. For example:

```
info: projection: flat (frame shape)
info: layout: mono (frame shape)
info: zoom: 1.50, zoom mode screen, screen 54.5 x 32.3 degrees
info: screen: fixed
info: filter: trilinear, mipmaps on
```

These go to the terminal only; the per-eye HUD planned next will show them on the glasses.

The head pose at the first frame becomes the reference on all three axes, so a head tilted at
start gives a tilted picture, and `R` does not correct that. Hold your head level and press
`Shift+R`.

### Flags

`--pose SPEC` (`null`, or `udp-opentrack` with `port`, `yaw`, `pitch`, `roll`), `--output NAME`,
`--device viture-pro|xreal-air2pro`, `--zoom F`, `--proj auto|hequirect|flat`,
`--layout auto|sbs-lr|sbs-rl|tb-lr|tb-rl|mono`, `--packing auto|half|full`,
`--zoom-mode screen|magnify`, `--screen fixed|locked`, `--mipmap on|off`, `--windowed`,
`--require-hwdec` (exit 1 unless mpv decodes with VA-API), `--timing-log PATH`,
`--content-fov HxV`, `--sync-after-swap` (diagnostic, see [Diagnostics](#diagnostics)),
`--mpv-profile NAME`, `--mpv-opt KEY=VALUE` (repeatable, up to 16), `--mpv-log LEVEL`,
`--convergence DEG`, `--filter bilinear|bicubic`, `--fbo-format rgb10|rgba8`, `--dither on|off`, `--deband on|off`,
`--no-memory`, `--forget` (the last two are described under [Per-file settings](#per-file-settings)).

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
keeps the screen fixed in the room or locks it to the head. `--mipmap on|off` (default `on`) uses
mipmaps while the picture is shrunk; see [Viewing comfort and sharpness](#viewing-comfort-and-sharpness).

`--convergence DEG` (default 0, -5 to 5) turns each eye's view by half the angle in opposite
directions; positive turns the eyes inwards. `--filter` chooses how the eye pass samples the
video: `bicubic` (Catmull-Rom, the default) or `bilinear`. Neither is an ini key.
`--convergence` is also remembered per file (see [Per-file settings](#per-file-settings)), and
`--filter` is not. See [Viewing comfort and sharpness](#viewing-comfort-and-sharpness).

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
`mipmap` says whether mipmaps were used for the first frame). Both are command-line flags only. To compare against the old
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

`[general]` keys: `pose` (same spec as `--pose`, default `udp-opentrack:port=4242`), `output`
(same as `--output`, default `auto`), `screen` (`fixed` or `locked`, as `--screen`, default
`fixed`) and `ffprobe` (the program run for format detection, default `ffprobe` looked up on
`PATH`) and `deband` (`yes` or `no`, also `on` or `off`, as `--deband`, default `no`).

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
ffprobe = /usr/bin/ffprobe
deband = no

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

At the same zoom both modes show the same picture straight ahead, so switching does not jump.

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

Subtitles: for flat 2D, mpv's own subtitle rendering is left on, so a subtitle track (or an
`.srt` next to the file) appears on the screen, at the bottom of the picture. In 3D layouts and in
VR180 subtitles are turned off, because mpv would draw them once across the whole frame and split
them between the eyes; `M` and `P` switch them with the layout. `--mpv-opt sub-visibility=no`
turns them off for flat 2D too.

## Format detection

Each file is opened as flat or VR180, with its layout and packing, without flags. Before mpv
opens the file, xrdome runs `ffprobe -v error -show_streams -i FILE` (no shell; killed after 3
seconds; skipped when the argument is not a regular file). For each of projection, layout and
packing, the first of these that has a value wins:

1. A flag: `--proj`, `--layout`, `--packing`, when not `auto`.
2. The file's saved record (see [Per-file settings](#per-file-settings)).
3. Container metadata read by `ffprobe`.
4. File name hints.
5. The frame shape.
6. Fallback: VR180 side by side, what xrdome did before detection.

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
(packing only) and `fallback`; `packing=` is shown for flat 3D only. A failed or missing `ffprobe`
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
`tb-rl`, `mono`), packing, zoom mode, swap-eyes, content FOV and convergence. The screen lock is
not remembered.

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
empty or not an absolute path. The directories are created on first save with mode 0700. The file
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
or `no`), `content_fov` (`HxV`, each 10 to 180) and `convergence` (-5 to 5); a missing key means
"follow the defaults" (or, for `layout` and `packing`, detection).

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
`packing` and `zoom-mode`, in that order. `restored` is printed at open, after the `detect:` line,
and lists only the values actually applied (not ones a flag overrode). `saved` is printed at exit when a record was written. `forgot` is printed by `--forget`
when it deleted a record. A file with no record prints nothing.

- `--no-memory`: do not read or write the record for this run. Use it for timing runs and
  comparisons.
- `--forget`: delete the file's record at startup (including an unreadable or newer one), then
  continue from the defaults. The run saves at exit as usual. With `--no-memory` as well, the
  record is still deleted and then nothing is read or written.

Both are command-line only.

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
the scene out of focus until zoomed far out. It is untested on the XREAL Air 2 Pro. There are no
keys for it; it can be given on the command line and is remembered per file like the other
settings.

**Sharpness.** The glasses show about 51 display pixels per degree, while a 4K VR180 file has
about 11 source pixels per degree, so every source pixel is enlarged about five times. Two things
help:

- Zoom out (`[`): a wider view puts fewer display pixels on each source pixel, so the enlargement
  and the blur that comes with it shrink. Zooming in (`]`) magnifies the softness.
- Bicubic sampling (the default; `F` toggles it): bilinear filtering, the GPU's built-in
  enlargement, turns every edge into a straight ramp a whole source pixel wide, which is the
  blurriest way to enlarge. Catmull-Rom keeps edges steeper, at the cost of a faint halo next to
  high-contrast edges. Press `F` on a still frame to compare.

Bicubic reads 9 texture samples per output pixel instead of 1. On the owner's Radeon it made
no measurable difference (see "Performance notes"). To measure it yourself, make two separate runs of the same file, one with
`--filter bilinear` and one with `--filter bicubic`, each with its own `--timing-log`, and
compare the logs; toggling `F` live mixes both filters in one log. `--mpv-log v` and
`--mpv-log debug` are for diagnosis only: they print a lot and affect timing, so leave them off
for measurements.

**Banding: debanding.** Dark gradients and skies in a low-bitrate file (a 2.5 Mbit/s AV1 file
looked far worse than 20 Mbit/s H.264) show visible steps that the 10-bit texture and dithering
cannot remove, because the steps are in the decoded picture. mpv's `deband` filter smooths them
inside its render pass, before xrdome's texture and dithering. It is off by default: `--deband on`
(or `deband = yes` in `[general]`) turns it on at start, and `B` switches it during playback so
you can compare on the glasses; the key prints `deband: on` or `deband: off` and `I` shows it.
It is a viewing preference, not a property of the file, so it is not saved in the per-file record.
It costs GPU time in mpv's render pass (not measured on the glasses yet; see `docs/STATUS.md`).
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
instead of with Catmull-Rom or bilinear. While the picture is enlarged nothing changes: no
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

## Diagnostics

`--sync-after-swap` calls `glFinish()` right after `SDL_GL_SwapWindow`, before the frame is reported
to the player. On Mesa/X11 the swap can return at once and the driver may block on vsync only at the
next frame's first draw, which is after the pose sample, so each pose would be about one refresh
old. It exists to compare `pose_to_swap_us` in the timing log with and without the flag; it is
not meant for normal playback. `swap_us` in the log is the time spent in the swap call (plus the
`glFinish` when the flag is on).

## Install on Gentoo

`packaging/gentoo/` holds a live ebuild, `media-video/xrdome-9999`, that builds `main` from GitHub
with meson and installs `/usr/bin/xrdome`. It is not in any overlay yet; add it to a local one:

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
The ebuild was parsed and its dependencies resolved with `emerge --pretend`; it has not been
built with `emerge` yet.

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

## Licence

GPL-2.0-or-later, the same terms as mpv built with its GPL files (see `Copyright` in the
[mpv repository](https://github.com/mpv-player/mpv)); xrdome links libmpv. The full text is in
`LICENSE`.
