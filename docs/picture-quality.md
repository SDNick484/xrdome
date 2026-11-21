# Viewing comfort and sharpness

**Close subjects.** The glasses show both eyes' images on a virtual screen a few metres away, so
content that is identical in both eyes appears at that distance, not at infinity. A VR180 camera
was set up for objects at infinity to line up, so a subject close to the camera is hard to fuse
at zoom 1 and above: it can split into two images. Zoom out (`[`) until the near subject becomes
one image. Each press prints the new zoom, and the value is remembered for the file (see
[Per-file settings](per-file-settings.md#per-file-settings)).

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
bilinear (see [Performance notes](measurements.md#performance-notes)); `bicubic-ar` and `mitchell` have not been timed on the glasses
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
  own `--timing-log` (see [Performance notes](measurements.md#performance-notes)). At 0 there is no extra pass.

**Banding: debanding.** Dark gradients and skies in a low-bitrate file (a 2.5 Mbit/s AV1 file
looked far worse than 20 Mbit/s H.264) show visible steps that the 10-bit texture and dithering
cannot remove, because the steps are in the decoded picture. mpv's `deband` filter smooths them
inside its render pass, before xrdome's texture and dithering. It is off by default: `--deband on`
(or `deband = yes` in `[general]`) turns it on at start, and `B` switches it during playback so
you can compare on the glasses; the key prints `deband: on` or `deband: off` and `I` shows it.
It is a viewing preference, not a property of the file, so it is not saved in the per-file record.
It costs GPU time in mpv's render pass: on the owner's integrated Radeon a 4K film went from 0.03%
to 0.75% missed vsyncs and an 8K VR180 file from 1.6% to 28% (see
[Performance notes](measurements.md#performance-notes)); on a dark low-bitrate film the owner saw little
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
  [Performance notes](measurements.md#performance-notes). On the owner's Radeon it costs almost nothing for a
  1080p film; the look was "hard to tell, maybe slight improvements". Off by default.
