# Format detection

Each file is opened as flat or VR180, with its layout and packing, without flags. Before mpv
opens the file, xrdome runs `ffprobe -v error -show_streams -show_format -i FILE` (no shell; killed
after 3 seconds; skipped when the argument is not a regular file). For each of projection, layout and
packing, the first of these that has a value wins:

1. A flag: `--proj`, `--layout`, `--packing`, when not `auto`.
2. The file's saved record (see [Per-file settings](per-file-settings.md#per-file-settings)).
3. Container metadata read by `ffprobe`.
4. File name hints.
5. The frame shape.
6. Fallback: VR180 side by side, what xrdome did before detection.

An image file (see [Photos](photos.md#photos)) skips the last two: what 1 to 4 leave open is flat and
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
