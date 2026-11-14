# xrdome: flat projection and format auto-detection (design spec)

Date: 2026-11-13
Status: approved 2026-11-13 (reviewed once by a fresh reader; three points settled as calls in 5.7, 5.8 and 5.9)
Branch: `feature/flat-projection` (from `main` at `3358de5`)
Builds on: `docs/superpowers/specs/2026-11-11-xrdome-design.md` and
`docs/superpowers/specs/2026-11-12-per-file-settings-design.md`

Lines marked **(owner)** were decided by the owner in the design conversation. Lines marked
**(call)** are the author's proposals; the owner can change them at review.

## 1. Intent

The owner wants to watch ordinary films on the glasses and have it feel more immersive than
the glasses' own mode, where plain mpv shows a 1080p picture locked to the head. In his
words: the picture should appear closer (zoomed), and he should be able to look left or
right to see the parts that fall outside the display, "almost pan and scan". **(owner)**

So the screen is a flat rectangle that is larger than the display can show at once, stays
where it is in the room, and is explored by turning the head.

Because the player then handles two kinds of file, it also has to open each the right way
without flags. **(owner)**

## 2. Goals

- A flat (rectilinear) projection: straight lines in the video are straight on the display,
  at every head angle. The 2 degree bow of today's `--layout mono --content-fov 60x34`
  workaround is gone.
- Flat video in 2D and in 3D: half SBS, full SBS, half top-bottom, full top-bottom, each in
  either eye order. **(owner)**
- Head tracking and zoom for flat video, with the screen fixed in space by default and a way
  to lock it to the head. **(owner)**
- Format auto-detection: a flat file and a VR180 file each open correctly with no flags, and
  one key press corrects a wrong guess. **(owner)**
- No shimmer when the source has more pixels than the area it is drawn into (mipmaps).
  **(owner)**
- `?` and `i` print key help and playback information to the terminal. **(owner: stopgap)**
- VR180 playback is unchanged for a file that is detected as VR180.

## 3. Non-goals

- An on-glasses HUD. `?` and `i` print to stderr only in this feature. The per-eye HUD is
  the next feature and will move those two keys onto the glasses. **(owner)**
- A distance control for 2D. The screen sits at the glasses' own optical distance.
  **(owner)**
- 360 video, fisheye VR180, cubemap. Unchanged from the first spec.
- Frame-packed Blu-ray 3D (MVC), anaglyph, interlaced or checkerboard 3D. **(owner)**
- A curved screen.
- Subtitles in 3D layouts and in VR180 (see 5.11).
- Switching the glasses to SBS mode from the player; plan Task 10; LICENSE; the
  tilt-at-start recenter behaviour; `no pose data` after a reconnect. **(owner)**

## 4. Anti-goals

- A side-by-side VR180 file whose frame is 2:1 must open exactly as it does today, with no
  flags. The owner's two VR180 test files (fixtures, 8.1) are the reference. VR180 files of
  other shapes are covered by the known wrong guesses in 5.8.
- Detection must never stop a file from playing. A missing `ffprobe`, a failed or slow probe
  and unknown metadata all fall through to the next step with at most one `xrdome: note:`
  line.
- No process spawn, file I/O or metadata work inside the render loop.
- Nothing from FFmpeg is linked. The owner's FFmpeg is built `--enable-gpl --enable-nonfree`,
  so a binary linked against its libavformat could not be redistributed.
- The mipmap work must not cost frames: see the timing target in 8.4.

## 5. Behaviour

### 5.1 Projections

`--proj auto|hequirect|flat`, default `auto` (detected, 5.8). `hequirect` is the existing
half-equirectangular VR180 mapping. `flat` is new.

Flat geometry. The screen is a rectangle in the plane one unit in front of the reference
pose (the pose captured at start or at the last recenter), centred straight ahead and
perpendicular to that direction. For a view ray `d` (after the tracking rotation, GL axes,
`-z` forward):

- `d.z >= 0` (the screen is behind): black.
- Otherwise `p = d.xy / -d.z`, and `u = p.x / (2 * half_w) + 0.5`,
  `v = p.y / (2 * half_h) + 0.5`. Outside [0,1] on either axis: black.

`half_w` and `half_h` come from the picture aspect `A` (5.3), the display's half-FOV
tangents at zoom 1 (`tan_h`, `tan_v`, from `xrd_fov_tan_half` with zoom 1) and the screen
size `S`:

```
half_w = S * min(tan_h, tan_v * A)
half_h = half_w / A
```

So at `S = 1` the whole picture just fits the display (a 2.39:1 film is letterboxed, a 4:3
picture is pillarboxed), which matches what plain mpv shows. **(call)**

`--content-fov` and the aspect warning apply to `hequirect` only. Giving `--content-fov`
with a file that ends up flat prints one `xrdome: note:` line and is otherwise ignored.
`--convergence` works in both projections, unchanged. **(owner)**

### 5.2 Zoom

Flat video has two zoom modes. **(owner: screen by default, with a way to switch)**

- `screen` (default): zoom sets the screen size, `S = zoom`. The view keeps the display's
  true FOV, so the picture moves on the display exactly as much as the head turns.
- `magnify`: the screen stays at `S = 1` and zoom narrows the view FOV, exactly as zoom does
  for VR180 today. The picture moves `zoom` times faster than the head.

At the same zoom value both modes show the same picture when looking straight ahead, so
switching does not jump.

- Key `Z` toggles the mode and prints `zoom mode: screen` or `zoom mode: magnify`.
  **(call: the key)** In `hequirect` it prints `zoom mode: magnify (VR180 has no screen mode)`
  and changes nothing.
- Flag `--zoom-mode screen|magnify`. Remembered per file (5.9).
- `[` and `]` are unchanged: x1.1 per step, 0.25 to 4.
- The default flat zoom is 1.5. **(owner: "try 1.5 for now")** It is separate from the
  VR180 default: a new `[device.<name>]` ini key `flat_zoom` (0.25 to 4, built-in 1.5 for
  both devices). `zoom` in the ini keeps meaning the VR180 default.
- `--zoom F` applies to whichever projection the file opens in.
- Each projection keeps its own zoom during a run. Pressing `P` (5.7) switches to the other
  projection's zoom (its default, or the value last used in this run), not the current
  number, because 1.5 means different things in the two. **(call)**

For reference, on the Viture Pro XR (37.9 x 21.9 degrees) a 16:9 screen is 54.5 x 32.4
degrees at 1.5 and 69.0 x 42.3 at 2.0.

### 5.3 Layouts and picture aspect

Layouts: `mono`, `sbs-lr`, `sbs-rl`, and new `tb-lr`, `tb-rl` (the first-named eye is the
top half). `--layout auto|mono|sbs-lr|sbs-rl|tb-lr|tb-rl`, default `auto`.

Packing, flat only: `--packing auto|half|full`, default `auto`. `full` means each eye's
sub-image already has the picture's shape; `half` means it was squeezed to half size along
the split axis and is stretched back.

With `F` = frame width / frame height in pixels (coded size, 5.10) times the pixel aspect:

| Layout | Full | Half |
|---|---|---|
| mono | `A = F` | n/a |
| sbs | `A = F / 2` | `A = F` |
| tb | `A = 2 * F` | `A = F` |

`auto` packing: SBS is `full` when `F >= 3.0`, else `half`; top-bottom is `full` when
`F < 1.5`, else `half`. **(call)** (1920x1080 SBS is half, 3840x1080 is full; 1920x1080
top-bottom is half, 1920x2160 is full.)

Pixel aspect comes from the probe (5.8). When the container carries stereo metadata the
pixel aspect is taken as 1:1, because Matroska stores a per-eye display size for stereo
tracks and the resulting pixel aspect does not describe the pixels. **(call)**

The eye split now works on either axis. Every guarantee the horizontal split has today
holds for the vertical one: no sample, including every Catmull-Rom tap and every mipmap
level used, reads the other eye's pixels.

Top-bottom is also accepted for `hequirect` since it is the same code path. It is listed as
untested on real content. **(call)**

Key `M` cycles: **(owner agreed the flat cycle)**

- flat: 2D, half SBS, full SBS, half top-bottom, full top-bottom. It sets layout and packing
  together and prints, for example, `layout: sbs-lr half`. Eye order is kept through `S`.
- hequirect: `sbs-lr`, `sbs-rl`, `tb-lr`, `mono`. **(call)**

`S` swaps eyes, as today.

### 5.4 Screen lock

**(owner: fixed by default; lock available, set mainly by parameter)**

- `--screen fixed|locked`, and `[general] screen = fixed|locked` in `config.ini`. Default
  `fixed`.
- Key `L` toggles and prints `screen: fixed` or `screen: head-locked`.
- Locked: the eye pass gets the identity rotation in place of the tracker's (mouse look is
  ignored too). Convergence still applies.
- Turning the lock off does a full recenter, so the screen reappears straight ahead.
  **(call)** A tilted head at that moment gives a tilted screen, as with `Shift+R` today;
  that is the open tilt question and is not solved here.
- Flat only. In `hequirect` the flag and ini key are ignored and `L` prints
  `screen: fixed (VR180 cannot be head-locked)`.
- Not saved per file: it depends on where the viewer is sitting, not on the film.

### 5.5 Depth

A 2D flat screen has no per-eye offset and no new control. 3D flat video uses the disparity
in the file as it is, with the screen plane at the glasses' own distance. **(owner)**

### 5.6 Minification: mipmaps

**(owner: do it in this feature)**

- `--mipmap on|off`, default `on`. Command line only.
- The video texture gets a mipmap chain (levels 0 to 3) only while the picture is being
  shrunk: when, at the centre of the screen and with the current zoom, one display pixel
  covers more than one source pixel. The chain is regenerated only after mpv has rendered a
  new video frame, not on every display frame.
- While shrinking, the eye pass samples trilinearly. While enlarging it samples as today
  (Catmull-Rom or bilinear per `--filter`), and no mipmaps are generated.
- This applies to both projections. In practice VR180 is almost always enlarged.
- After the first frame the existing `video texture:` line also shows `mipmap=on|off`.
- For reference: 1080p at flat zoom 1.5 is enlarged 1.5x; 4K at 1.5 is shrunk 1.3x, at 1.0
  2x, at 0.5 4x.

### 5.7 Keys added

| Key | Action |
|---|---|
| P | Switch projection, flat or VR180; prints `proj: flat` or `proj: hequirect` |
| Z | Zoom mode (5.2) |
| L | Screen lock (5.4) |
| Up / Down | Seek +60 s / -60 s, as in mpv **(owner)** |
| ? | Print the key list to stderr |
| I | Print playback information to stderr |

`P`, like `M` and `S`, counts as a change and is remembered for the file.

What `P` does to the layout depends on where the layout came from. If it came from a flag,
the record, metadata, a file name hint, or an `M` or `S` press in this run, it is kept. If
it was only a frame-shape or fallback guess, it is worked out again for the new projection
from the "layout only" rows of the shape table in 5.8 (packing likewise). So a 2:1 flat
video that opened as VR180 side by side becomes flat 2D with the one press, and a 16:9
squeezed VR180 file that opened as flat 2D becomes VR180 side by side. **(call)**

`?` prints one line per key, each starting with `keys:`. `I` prints lines starting with
`info:` : file name; codec, coded size and frame rate; `hwdec`; projection, layout and
packing with where each came from (5.8); zoom, zoom mode and, for flat, the screen size in
degrees; screen fixed or locked; filter and whether mipmaps are in use; texture format;
frames presented and missed vsyncs so far. Neither prefix is `xrdome:`, which the smoke
tests count. **(call: contents)**

### 5.8 Detection

Detection runs once, before mpv opens the file. It fills three fields: projection, layout
and packing. For each field the first source in this list that has a value wins:
**(owner: metadata above name; flags and record above both; a key to correct)**

1. An explicit flag (`--proj`, `--layout`, `--packing`, when not `auto`).
2. The file's saved record (5.9).
3. Container metadata, read with `ffprobe`.
4. File name hints.
5. Frame shape.
6. Fallback.

**Probe.** xrdome runs `ffprobe` as a child process (no shell; the file name is passed as
one argument) on the first video stream that is not an attached picture, and reads from its
`key=value` output: `width`, `height`, `sample_aspect_ratio`, and the `Stereo 3D` and
`Spherical Mapping` side data if present. **(owner: approach 1)**

- The program is `ffprobe` looked up on `PATH`; `[general] ffprobe = PATH` in `config.ini`
  overrides it. **(call)**
- A probe that does not finish in 3 seconds is killed. **(call)**
- If the argument is not a regular file, the probe is skipped.
- Any failure prints one `xrdome: note:` line and detection continues without probe data.

**Metadata mapping.**

| ffprobe side data | Result |
|---|---|
| Stereo 3D `side by side` | layout `sbs-lr`; `sbs-rl` when `inverted=1` |
| Stereo 3D `top and bottom` | layout `tb-lr`; `tb-rl` when `inverted=1` |
| Stereo 3D `2D` | layout `mono` |
| Any other Stereo 3D type | ignored, one note naming it |
| Spherical Mapping, any equirectangular kind | projection `hequirect` |
| Spherical Mapping, any other kind (cubemap, fisheye, ...) | projection `hequirect`, one note that the kind is not supported and may look distorted |

The exact strings `ffprobe` prints for these are confirmed for Stereo 3D on FFmpeg 6.1 and
not confirmed for Spherical Mapping or on the owner's FFmpeg 9.0.2 (section 11). The parser
is written against captured fixtures; the first build task captures them.

**File name hints.** The base name is split into tokens at every character that is not a
letter or digit. Matching is case-insensitive except where stated.

| Token | Result |
|---|---|
| `VR180`, `180`, `180x180`, `VR` | projection `hequirect` |
| `SBS`, `3DH`, `LR` (capitals only) | layout `sbs-lr` |
| `RL` (capitals only) | layout `sbs-rl` |
| `HSBS` | layout `sbs-lr`, packing `half` |
| `FSBS`, `FULLSBS` | layout `sbs-lr`, packing `full` |
| `TB`, `OU` (capitals only), `3DV` | layout `tb-lr` |
| `HTB`, `HOU` | layout `tb-lr`, packing `half` |
| `MONO`, `2D` | layout `mono` |
| `3D` with no layout token | layout `sbs-lr` |

The two-letter tokens count only in capitals because `ou`, `tb`, `lr` and `rl` occur in
ordinary titles. **(call: the whole table)**

**Frame shape**, with `F` as in 5.3. It fills only what is still open.

| Still open | Shape | Result |
|---|---|---|
| projection and layout | `F` within 2% of 2.0 | `hequirect`, `sbs-lr` |
| projection and layout | `F >= 3.0` | `flat`, `sbs-lr` (packing auto gives full) |
| projection and layout | `F` within 3% of 0.889 (16:9 doubled in height) | `flat`, `tb-lr` (packing auto gives full) |
| projection and layout | anything else | `flat`, `mono` |
| projection only, layout is sbs | `F` within 2% of 2.0 | `hequirect`; otherwise `flat` |
| projection only, layout is tb | `F` within 2% of 0.5 | `hequirect`; otherwise `flat` |
| projection only, layout is mono | `F` within 2% of 1.0 | `hequirect`; otherwise `flat` |
| layout only, projection is hequirect | `F` within 2% of 1.0 | `mono`; within 2% of 0.5 `tb-lr`; otherwise `sbs-lr` |
| layout only, projection is flat | as the first block's flat rows | |

**(call: the table)**

**Fallback.** If the frame size is not known before opening (no probe), anything still open
becomes `hequirect` and `sbs-lr`, which is what xrdome did before this feature, with one
note that the format could not be detected. **(call)**

**Known wrong guesses, accepted:**

- A half-SBS or half-top-bottom 3D film with no tag and no name hint opens as flat 2D. `M`
  fixes it once. **(owner)**
- A flat video that is exactly 2:1 (some streaming series, phone cameras) opens as VR180.
  `P` fixes it once. **(owner informed)**
- A side-by-side VR180 file that is not 2:1 (for example squeezed into 3840x2160), with no
  tag and no name hint, opens as flat 2D. Today it plays as VR180 with the aspect warning.
  `P` fixes it once. **(call; not discussed with the owner)**
- A full top-bottom 3D film that is not 16:9 per eye opens as flat 2D. `M` fixes it.

**Output.** One line at open, always:

```
detect: proj=hequirect (frame shape) layout=sbs-lr (frame shape)
detect: proj=flat (frame shape) layout=sbs-lr (file name "HSBS") packing=half (file name "HSBS")
detect: proj=flat (flag) layout=tb-lr (metadata) packing=full (auto)
```

Sources are `flag`, `record`, `metadata`, `file name "<token>"`, `frame shape`, `auto`
(packing only), `fallback`. `packing=` is shown for flat stereo layouts only.

### 5.9 Per-file record

The record format stays `version = 1`.

- `proj` is written in every record, as today, and is now the projection in effect at quit:
  `hequirect` or `flat`. On load it is used as the file's projection (source `record`).
  **(call)** So once any setting has been saved for a file, its projection is pinned to what
  the owner was looking at.
- `zoom` is the VR180 zoom, as in every existing record. A new optional key `flat_zoom`
  (0.25 to 4) is the flat zoom. Each is applied only to its own projection and is "set" only
  by a change, a `--zoom` flag or a loaded value that belongs to that projection: `--zoom`
  counts for the projection the file opened in, and a key press for the projection in
  effect when it was pressed. Switching projection with `P` therefore neither loses the
  other projection's saved zoom nor saves a zoom the owner never chose. **(call)**
- `layout` accepts `tb-lr` and `tb-rl`.
- New keys, written only when set (same three rules as spec 5.5 of the per-file design):
  `flat_zoom`, `packing = half|full`, `zoom_mode = screen|magnify`.
- `P` and `--proj` (not `auto`) make the projection count as changed, so a record is written
  even if nothing else was touched.
- New `settings:` words: `proj=flat`, `flat-zoom=1.65`, `packing=half`, `zoom-mode=magnify`.

Compatibility:

- An existing record has `proj = hequirect` and keeps working unchanged. A flat film that
  was once saved under the old curved-screen workaround therefore keeps opening as VR180
  until `P` is pressed or `--forget` is used. **(call)**
- A binary from before this feature that meets `proj = flat` treats the record as
  unsupported and does not overwrite it (existing behaviour). One that meets `tb-lr` in a
  `hequirect` record treats the record as bad, plays from defaults and does not overwrite it.

### 5.10 Video texture size

Today the texture is sized from mpv's `dwidth` and `dheight`. For a Matroska file with a
stereo tag those describe one eye's shape (a 1920x1080 SBS file reported 1920x2160 on mpv
0.37), which would upscale the frame for nothing. The texture must be the coded frame size
(after any rotation mpv applies), with the video filling it exactly, whatever aspect the
container declares. xrdome works out shapes itself (5.3). How that is achieved with mpv is
left to the builder; a fixture test with a stereo-tagged Matroska file checks it.

### 5.11 Subtitles

**(call; not discussed with the owner)**

- Flat 2D: mpv's own subtitle rendering is left on, so subtitles are drawn into the video
  texture and appear on the screen.
- Any stereo layout, and VR180: subtitles are turned off, because mpv would draw them once
  across the whole frame and split them between the eyes. Changing layout with `M` updates
  this.

Whether libmpv draws subtitles into the texture with `osd-level=0` is not verified (11); a
smoke case checks it. If it does not, flat 2D subtitles become a non-goal and the README
says so.

### 5.12 Aspect warning

Printed only when the projection is `hequirect` and the frame does not fit the layout and
content FOV. New text, one physical line:

```
xrdome: warning: video is 1920x1080, which is not the aspect of a side-by-side VR video covering 180x180 degrees. If it is an ordinary flat video, press P or use --proj flat. If it is VR video covering less than 180 degrees, set --content-fov. Playing it as given.
```

## 6. Approaches considered

How to read container metadata, since mpv does not supply it (mpv 0.37 reports Matroska
stereo mode only, nothing for MP4, and has no projection property):

| Approach | Trade-off | Outcome |
|---|---|---|
| Run `ffprobe` as a child process and parse its text | Links nothing; parser testable from captured output; 50 to 100 ms at startup; needs the `ffprobe` binary, which ships with the FFmpeg that mpv already requires | Chosen **(owner)** |
| Link libavformat | In-process and faster; licence follows the FFmpeg build (the owner's is GPL and nonfree); side-data API differs between major versions | Rejected |
| Own MP4 and Matroska reader | No dependency; most code and most risk on odd files | Rejected |
| mpv properties only | No new code path; covers Matroska stereo only | Rejected: insufficient |

Zoom for flat video: enlarging the screen (motion one to one) against magnifying the view
(motion amplified). The owner chose the first as default with a switch (5.2).

Minification: mipmaps used only when shrinking (chosen), leaving it, or having mpv deliver
a smaller frame (rejected: texture reallocation on every zoom step, and the `fast`
profile's downscaler is no better).

## 7. Structure

Language, build and test framework are unchanged (C11, meson, cmocka). No new libraries.
Runtime: `ffprobe` from `media-video/ffmpeg`, already a dependency of `media-video/mpv`.

- New `src/detect/probe.c`, `.h`: run `ffprobe` (fork, exec, pipe, timeout) and a pure
  function that parses its text into a struct. `tests/test_probe.c` with fixtures under
  `tests/fixtures/ffprobe/`.
- New `src/detect/detect.c`, `.h`: pure. File name tokens, shape rules, and the merge of
  the six sources into projection, layout, packing and their sources.
  `tests/test_detect.c`.
- `src/render/projection.c`, `.h`: a projection enum; flat direction-to-uv mirrored in C;
  screen half-extents; the eye sub-image transform on both axes; auto packing and picture
  aspect.
- `src/render/shaders/`, `src/render/renderer_gl.c`, `.h`: the flat branch, the two-axis
  eye split, trilinear sampling when shrinking. Whether this is one fragment shader with a
  branch or two programs is the builder's choice.
- `src/video/player_mpv.c`, `.h`: coded-size texture (5.10), mipmap generation after a new
  frame, subtitle visibility, the reworded warning, the codec and frame-rate strings for `I`.
- `src/app/controls.c`, `.h`: projection, packing, per-projection zoom, zoom mode, screen
  lock, the new cycles.
- `src/store/filestate.c`, `.h`, `src/values.c`, `.h`: the new keys and values.
- `src/config.c`, `.h`: the new flags and ini keys.
- `src/main.c`: probe and detect between loading the record and opening the player; the
  new keys; `?` and `I` output.
- `tools/make_pattern.sh`: flat patterns (2D 16:9 with edge marks and a fine-line area for
  judging shimmer; half SBS; full top-bottom) and stereo-tagged files for fixtures.

## 8. Testing

### 8.1 Unit tests (`meson test`, no hardware)

- Flat mapping: straight ahead gives (0.5, 0.5); the screen's edge is at
  `atan(S * tan_h)`; behind is outside; points along the screen's top edge all give `v = 1`
  at several head yaws (no bow); `magnify` and `screen` agree at the centre for equal zoom.
- Screen size: 16:9 at 1.5 on the Viture is 54.5 x 32.4 degrees; 2.39:1 and 4:3 pictures
  are contained at `S = 1`.
- Picture aspect and auto packing for every row of 5.3.
- Eye transform on both axes, with swap.
- Probe parser against fixtures: the owner's two files (2880x1440 Matroska, 4320x2160 MP4,
  no side data), a stereo-tagged Matroska and MP4, a spherical-tagged file, an attached
  picture before the video stream, empty and truncated output.
- Detection: every row of the three tables in 5.8; the precedence order per field; the
  owner's two files resolve to `hequirect`, `sbs-lr` by frame shape; a 1920x1080 file with
  no hints resolves to `flat`, `mono`; no probe resolves to the fallback.
- File name tokens: capitals-only rule; `1800` and `Routes` do not match.
- Records: round trip with the new keys; `proj = flat` loads; an old record still loads;
  `tb-lr` round trip. The existing test that expects `proj = flat` to be unsupported changes.
- Config: each new flag and ini key, bad values, `auto` defaults.
- Seek: Up and Down request +60 s and -60 s; Left and Right stay at 10 s.
- Controls: `P` keeps a zoom per projection; `P` re-derives a guessed layout and keeps a
  chosen one; `M` cycles per projection; `Z`; `L`.
- Records and `P`: a `hequirect` record with `zoom = 0.56`, opened and switched to flat,
  still has `zoom = 0.56` after quit and gains no `flat_zoom` unless the flat zoom was
  changed; a file with no record and only a `P` press saves `proj` and no zoom.
- Shapes: 1080x1080 is `flat`, `mono`; 1920x2160 is `flat`, `tb-lr`; 3840x2160 is `flat`,
  `mono`, and after `P` is `hequirect`, `sbs-lr`.

### 8.2 Render tests (offscreen, llvmpipe)

- Flat 2D: centre colour, black outside the screen, the top edge of the picture lies on one
  pixel row across the eye at yaw 0.
- Half SBS and full top-bottom: each eye shows only its own colour, with bilinear,
  Catmull-Rom and trilinear at 4x shrink.
- VR180 output for the existing pattern is unchanged from `main` (same frame checks).

### 8.3 Smoke tests (Xvfb)

- Existing cases keep passing; cases that relied on a 16:9 pattern being played as VR180 are
  updated to pass `--proj hequirect`.
- New: the 2:1 pattern with no flags prints `detect: proj=hequirect` and its frame check
  passes; a 16:9 pattern prints `detect: proj=flat` and passes a flat frame check; a name
  with `HSBS` is detected; a stereo-tagged Matroska file is detected by metadata and its
  texture is the coded size; `ffprobe` made unavailable prints one note and plays.
- Record: a flat run with `--zoom` saves `proj = flat`; the next run restores it.
- Subtitles: a 16:9 pattern with an `.srt` beside it shows subtitle pixels in flat 2D and
  none in a stereo layout.

### 8.4 Hardware run (Viture Pro XR, owner at the device, hardware-run skill)

On AC power. Glasses switched to SBS by hand first. Timing runs use `--no-memory` and
change one thing between the two runs of a pair.

1. Build with 0 warnings; unit tests pass.
2. VR180 regression: `test.mkv` and `test2.mp4` with no flags print
   `detect: proj=hequirect (frame shape)` and look as before.
3. A flat 1080p film with no flags opens flat at 1.5: edges straight, the screen stays put
   when the head turns, `[` `]` resize it, `Z` switches mode, `L` locks and unlocks, `R`
   recentres.
4. A 4K flat file: `--mipmap on` against `--mipmap off` at the same `--zoom`, 3600 frames
   each. Target: 0 missed vsyncs with hardware decode in both; if not 0, `on` within 0.5
   percentage points of `off`. Then at `--zoom 0.5`, fine detail shimmers with `off` and
   not with `on`.
5. A half-SBS 3D film: `M` to half SBS; depth is correct, and `S` inverts it.
6. Records: quit and reopen the flat film (restored `proj=flat` and zoom); an existing
   VR180 record still restores.
7. `ffprobe -show_streams` on a stereo-tagged and a spherical-tagged fixture file with the
   owner's FFmpeg 9.0.2, compared with the fixtures the parser was written against.
8. `?` and `I` print; Up and Down skip a minute forward and back.

Not on the Viture in this run unless the owner has such files: top-bottom, full SBS, the
XREAL Air 2 Pro.

## 9. Documentation

- `README.md`: status paragraph; keys (including Up / Down) and flags; a "Flat video" section (what it is, zoom
  modes, screen lock, sizes in degrees); a "Format detection" section (order, tables, the
  two known wrong guesses, the `detect:` line); per-file settings (new keys, `proj`
  pinning); sharpness (mipmaps); dependencies (`ffprobe` at run time).
- `docs/STATUS.md`: the per-file settings heading still says branch
  `feature/per-file-settings` at `6d93d87`; change it to merged in PR #1, `main` at
  `3358de5`. Add this feature, its decisions and its hardware run. Candidate features: the
  per-eye HUD next, taking over `?` and `I`.
- The project doc `claude/xrdome-status.md` is updated to match at the end.

## 10. Left to the builder

- Module and function names, struct layouts, and how `xrd_renderer_draw`'s growing argument
  list is packaged.
- One shader with a branch or two programs.
- How the mipmap levels are kept from reading across the eye seam (capped levels, wider
  margins, or both), as long as the render tests in 8.2 pass.
- How mpv is made to deliver the coded frame (5.10).
- The exact `ffprobe` arguments and how its output is read.
- Which SDL events carry `?`.
- The exact wording of notes, beyond the lines quoted here.
- How `I` gets codec and frame rate from mpv.

## 11. Not verified

- The text `ffprobe` prints for Spherical Mapping side data, on any version, and for
  Stereo 3D on FFmpeg 9.0.2. Verified: Stereo 3D on FFmpeg 6.1 for Matroska and MP4.
- That mpv on the owner's laptop (libmpv 2.6.0) reports `dwidth`/`dheight` for
  stereo-tagged Matroska the way mpv 0.37 does. 5.10 makes the answer irrelevant.
- That libmpv draws subtitles into the render target with `osd-level=0`.
- Mipmap generation cost on the owner's Radeon. Measured in the hardware run.
- That FFmpeg does not decode both eyes of MVC. Not relied on; MVC is a non-goal.
- Rotated video (phone footage with a rotation tag). Expected to work through mpv; not
  tested.
- Nothing in this spec has been built. It is based on the code at `3358de5`.
