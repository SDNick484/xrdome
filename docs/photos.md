# Photos

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
[Per-file settings](per-file-settings.md#per-file-settings)); the filter, sharpening, curve, screen lock and mouse look
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

## Depth 3D for photos

A photo whose depth is in the cache is drawn in 3D: each eye sees the picture's points shifted
sideways by their depth, nearer points out of the screen and further ones into it. Make the depth
with [`xrdome-tools depth photo FILE|DIR`](tools.md#depth) (it needs `xrdome-tools setup
depth`); xrdome finds it by the sha256 of the photo's bytes, in
`$XDG_CACHE_HOME/xrdome-tools/depth/` (default `~/.cache/xrdome-tools/depth/`), and prints
`depth: 1365x2048 from the cache` when it opens the photo.

Without a cached depth, xrdome asks the depth helper for it and shows the photo in 2D meanwhile:
`depth: none cached; asking the depth helper`, then, about a second later (a few seconds for the
first photo, while the helper starts), `depth: 1365x2048 from the depth helper` and the photo turns
3D. In a directory the previous and next photos are asked for too, so PageDown usually lands on a
photo whose depth is ready. The helper is
[`xrdome-tools depth serve`](tools.md#depth-serve), which needs `xrdome-tools setup depth`
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

## Depth for films

Experimental, and not part of release 0.1.0: it is built and tested against a fake helper, but the
glasses run (the real model's rate, flicker and cost) has not been done.

A flat 2D film is drawn in 3D live from its depth (spec section 8 of
`docs/superpowers/specs/2026-11-19-depth-3d-design.md`), with the same stereo pass as a photo's, so
`-` / `=` (strength) and `,` / `.` (screen plane) work on it too. 3D is off at the start of a film;
`D` turns it on. Then each new frame is scaled on the GPU to 322x182 and read back a frame later,
never waiting, into a shared-memory ring that [`xrdome-tools depth serve`](tools.md#depth-serve)
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
