# Flat Projection and Format Auto-Detection Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ordinary video plays on the glasses as a flat screen fixed in space, in 2D and 3D, with zoom and head tracking, and each file opens as flat or VR180 without flags.

**Architecture:** `ffprobe` is run once as a child process before mpv opens the file; a pure detection module merges flags, the saved record, metadata, file name hints and frame shape into projection, layout and packing. The eye pass gains a flat (ray-plane) branch, a two-axis eye split and trilinear sampling when shrinking. A small pure "session" module turns config, record and detection into the starting view state, so `main.c` only wires.

**Tech Stack:** C11, meson, cmocka, OpenGL 3.3 core on EGL, SDL2, libmpv render API. No new libraries. Runtime: the `ffprobe` program.

**Spec:** `docs/superpowers/specs/2026-11-13-flat-projection-design.md`. Section numbers below ("spec 5.8") refer to it.

## Global Constraints

- Nothing from FFmpeg is linked. `ffprobe` is executed with `execvp`, never through a shell.
- No process spawn, file I/O or detection work inside the render loop.
- A side-by-side VR180 file whose frame is 2:1 opens exactly as on `main` at `3358de5`, with no flags.
- Detection never stops playback: every failure is at most one `xrdome: note:` line.
- Lines xrdome prints for this feature start with `detect:`, `keys:`, `info:`, `settings:`, `zoom mode:`, `screen:`, `proj:`, `layout:`; only warnings and notes start with `xrdome:` (the smoke tests count those).
- Zoom range 0.25 to 4, step x1.1 (unchanged). Built-in `flat_zoom` 1.5 for both devices.
- Record format stays `version = 1`.
- C prefix `xrd_`; new pure modules include no SDL, GL or mpv headers.
- OpenRC and Xorg only; no systemd units, no Wayland paths.
- Verification gate for every task and every PR (the repo has no CI):
  `meson setup build --wipe && meson compile -C build && meson test -C build --print-errorlogs`
  with 0 compiler warnings, in an environment that has `xvfb-run`, `ffmpeg`, `ffprobe` and libmpv, so that neither smoke test reports SKIP.
- Commits end with the session's attribution lines. PRs target `main`, are never merged by the worker, and say `Part of the flat projection plan, Task N`.

## Review Focus

1. **A file name that starts with `-` or contains spaces or quotes.** `ffprobe` must receive it as one argument after `-i`, and playback must start. Test in Task 2.
2. **`ffprobe` hangs, prints megabytes, or dies from a signal.** It is killed after 3 s, output is capped at 256 KiB, no zombie is left, one note is printed. Test in Task 2.
3. **Probe values that are zero or junk** (`width=0`, `sample_aspect_ratio=0:1` or `N/A`). They count as unknown; no aspect is ever NaN, infinite or zero. Tests in Tasks 1 and 3.
4. **Odd frame sizes in a stereo layout** (1919x1080 SBS, 1920x1081 top-bottom). Neither eye shows a line of the other at any filter, including trilinear at 4x shrink. Test in Tasks 4 and 5.
5. **Mipmap state after the video size changes or the layout or projection is switched mid-file.** The eye pass never samples trilinearly from a texture whose chain was not regenerated for the current frame. Test in Task 5.

---

## File map

| File | Responsibility | Task |
|---|---|---|
| `src/detect/probe.h`, `probe.c` | Parse `ffprobe` text; run `ffprobe` | 1, 2 |
| `src/detect/detect.h`, `detect.c` | Name hints, shape rules, merge of the six sources | 1 |
| `tests/fixtures/ffprobe/*.txt` | Captured `ffprobe -v error -show_streams` output | 1 |
| `src/render/projection.h`, `.c` | Enums, flat mapping, picture aspect, eye split on both axes, view geometry | 3 |
| `src/render/shaders/vr180.frag`, `renderer_gl.c`, `.h` | Flat branch, two-axis split, trilinear | 4, 5 |
| `src/video/player_mpv.c`, `.h` | Coded-size texture, mipmaps, subtitles, codec and fps strings, warning | 5, 8 |
| `src/values.c`, `.h`, `src/config.c`, `.h` | New value parsers, flags, ini keys | 6 |
| `src/store/filestate.c`, `.h` | New record keys | 6 |
| `src/app/controls.c`, `.h` | Projection, packing, per-projection zoom, zoom mode, lock, cycles | 7 |
| `src/app/session.c`, `.h` | Pure: starting state from config, record and detection; state to record | 7 |
| `src/main.c` | Wiring, keys, `detect:` / `keys:` / `info:` output | 8 |
| `tools/make_pattern.sh`, `tests/smoke_*.sh` | Flat patterns, new smoke cases | 8 |
| `README.md`, `docs/STATUS.md` | Documentation | 9 |

`meson.build` gains one line per new source in `core_sources` and one name per new test in `test_names`.

PR grouping (each PR is one prompt issue): **A** Tasks 1-2, **B** Tasks 3-4, **C** Task 5, **D** Tasks 6-7, **E** Tasks 8-9, then the hardware run.

---

### Task 1: Probe parser and detection (pure)

**Files:**
- Create: `src/detect/probe.h`, `src/detect/probe.c`, `src/detect/detect.h`, `src/detect/detect.c`
- Create: `tests/test_probe.c`, `tests/test_detect.c`, `tests/fixtures/ffprobe/*.txt`
- Modify: `meson.build` (sources, test names, pass the fixture directory as `-DXRD_FIXTURE_DIR="..."` to the tests)
- Modify: `src/render/projection.h` only to add the enums below (Task 3 fills in the rest)

**Interfaces:**
- Produces, in `render/projection.h` (append to the layout enum so existing values keep their numbers):

```c
typedef enum { XRD_PROJ_HEQUIRECT, XRD_PROJ_FLAT } xrd_proj;
typedef enum { XRD_LAYOUT_SBS_LR, XRD_LAYOUT_SBS_RL, XRD_LAYOUT_MONO,
               XRD_LAYOUT_TB_LR, XRD_LAYOUT_TB_RL } xrd_layout;
typedef enum { XRD_PACKING_AUTO, XRD_PACKING_HALF, XRD_PACKING_FULL } xrd_packing;
bool xrd_layout_is_sbs(xrd_layout l);
bool xrd_layout_is_tb(xrd_layout l);
```

- Produces, in `detect/probe.h`:

```c
typedef enum { XRD_STEREO_NONE, XRD_STEREO_2D, XRD_STEREO_SBS, XRD_STEREO_TB,
               XRD_STEREO_OTHER } xrd_probe_stereo;
typedef enum { XRD_SPH_NONE, XRD_SPH_EQUIRECT, XRD_SPH_OTHER } xrd_probe_spherical;
typedef struct {
    bool have_size;            /* width and height are both > 0 */
    int width, height;         /* coded frame, pixels */
    int sar_num, sar_den;      /* 1:1 when absent, N/A or non-positive */
    xrd_probe_stereo stereo;
    bool stereo_inverted;
    char stereo_name[32];      /* ffprobe's own word, for notes */
    xrd_probe_spherical spherical;
    char spherical_name[32];
} xrd_probe;
/* Parses `ffprobe -v error -show_streams` text: the first [STREAM] block with
 * codec_type=video and DISPOSITION:attached_pic=0 (or no such line).
 * False when there is none; *out is then zeroed with sar 1:1. */
bool xrd_probe_parse(const char *text, xrd_probe *out);
```

- Produces, in `detect/detect.h`:

```c
typedef enum { XRD_SRC_NONE, XRD_SRC_FLAG, XRD_SRC_RECORD, XRD_SRC_METADATA, XRD_SRC_NAME,
               XRD_SRC_SHAPE, XRD_SRC_AUTO, XRD_SRC_FALLBACK } xrd_detect_src;
typedef struct {               /* what one source states; unset fields are open */
    bool have_proj, have_layout, have_packing;
    xrd_proj proj; xrd_layout layout; xrd_packing packing; /* packing: HALF or FULL */
} xrd_detect_given;
typedef struct {
    xrd_proj proj; xrd_layout layout;
    xrd_packing packing;       /* AUTO unless a source fixed it */
    xrd_detect_src proj_src, layout_src, packing_src; /* packing_src AUTO when packing is AUTO */
    char proj_token[16], layout_token[16], packing_token[16]; /* for XRD_SRC_NAME */
    double frame_aspect;       /* F of spec 5.3; 0 when the size is unknown */
    char notes[3][200]; int note_count; /* text after "xrdome: note: " */
} xrd_detect;
void xrd_detect_name_hints(const char *path, xrd_detect_given *out,
                           char proj_tok[16], char layout_tok[16], char packing_tok[16]);
/* flags and record may be NULL; probe NULL means no probe data. */
void xrd_detect_resolve(const xrd_detect_given *flags, const xrd_detect_given *record,
                        const xrd_probe *probe, const char *path, xrd_detect *out);
/* The "layout only" rows of the shape table for new_proj; used by the P key.
 * With frame_aspect 0: sbs-lr for hequirect, mono for flat. Packing becomes AUTO. */
void xrd_detect_relayout(double frame_aspect, xrd_proj new_proj,
                         xrd_layout *layout, xrd_packing *packing);
/* "proj=flat (frame shape) layout=sbs-lr (file name \"HSBS\") packing=half (...)".
 * packing is included for flat stereo layouts only; AUTO prints the resolved
 * value with source "auto". Returns length or -1. */
int xrd_detect_describe(const xrd_detect *d, char *buf, size_t len);
```

`F = width * sar_num / (height * sar_den)`, with the pixel aspect forced to 1:1 when `stereo != XRD_STEREO_NONE` (spec 5.3).

- [ ] **Step 1: Capture fixtures.** In `tests/fixtures/ffprobe/`, one file per case, each the stdout of `ffprobe -v error -show_streams FILE`:
  - `owner_vr180_mkv.txt` and `owner_vr180_mp4.txt`: the `[STREAM]` blocks from the owner's capture in the design conversation (2880x1440 AV1 Matroska; 4320x2160 H.264 MP4; no side data). Strip the shell prompt and banner lines.
  - `sbs_mkv.txt`, `tb_mkv.txt`, `sbs_inverted_mkv.txt`, `sbs_mp4.txt`: generate with `ffmpeg -f lavfi -i testsrc=s=1920x1080:d=1 -c:v libx264 -metadata:s:v stereo_mode=left_right` (and `top_bottom`, `right_left`; MP4 by remuxing with `-strict unofficial`).
  - `spherical_mkv.txt`: a Matroska file with an equirectangular projection element. Try `mkvpropedit FILE --edit track:v1 --set projection-type=1` (package `mkvtoolnix`); if that does not produce a `Spherical Mapping` block, try Google's `spatial-media` injector on an MP4. Record in the fixture directory's `README` which tool and FFmpeg version produced each file. If neither works, stop and report: the spec's metadata table for projection cannot be implemented blind.
  - `coverart_first.txt`: a file whose first video stream has `DISPOSITION:attached_pic=1`.
  - `audio_only.txt`, `empty.txt`, `truncated.txt` (cut mid-block).
- [ ] **Step 2: Write failing parser tests** in `tests/test_probe.c`:

```c
static void owner_files(void **s) {
    xrd_probe p;
    assert_true(xrd_probe_parse(fixture("owner_vr180_mkv.txt"), &p));
    assert_true(p.have_size); assert_int_equal(p.width, 2880); assert_int_equal(p.height, 1440);
    assert_int_equal(p.sar_num, 1); assert_int_equal(p.sar_den, 1);
    assert_int_equal(p.stereo, XRD_STEREO_NONE); assert_int_equal(p.spherical, XRD_SPH_NONE);
    assert_true(xrd_probe_parse(fixture("owner_vr180_mp4.txt"), &p));
    assert_int_equal(p.width, 4320); assert_int_equal(p.height, 2160);
}
```

  Also: `sbs_mkv` gives `XRD_STEREO_SBS`, not inverted; `sbs_inverted_mkv` inverted; `tb_mkv` `XRD_STEREO_TB`; `sbs_mp4` `XRD_STEREO_SBS`; `spherical_mkv` `XRD_SPH_EQUIRECT`; `coverart_first` returns the second stream's size; `audio_only`, `empty` return false; `truncated` does not crash; inline text with `width=0`, `sample_aspect_ratio=0:1` and `sample_aspect_ratio=N/A` gives `have_size == false` and sar 1:1 (Review Focus 3); an unknown Stereo 3D `type=` gives `XRD_STEREO_OTHER` with the word in `stereo_name`.
- [ ] **Step 3: Run** `meson test -C build probe`; expect FAIL (undefined functions).
- [ ] **Step 4: Implement `xrd_probe_parse`.** A spherical `projection=` value containing `equirectangular` is `XRD_SPH_EQUIRECT`; any other is `XRD_SPH_OTHER`.
- [ ] **Step 5: Run** `meson test -C build probe`; expect PASS.
- [ ] **Step 6: Write failing detection tests** in `tests/test_detect.c`, one test per table in spec 5.8, values copied from the spec:
  - Name hints: every row of the token table; `movie.HSBS.1080p.mkv` gives layout `sbs-lr`, packing half, tokens `HSBS`; `Routes.1800.mkv` gives nothing; `ou.est.tb.mkv` (lower case) gives nothing; `A_OU.mkv` gives `tb-lr`; `clip_3D.mp4` gives `sbs-lr`; `x_180_LR.mp4` gives `hequirect`, `sbs-lr`.
  - Shape, nothing else known: 2880x1440 and 4320x2160 give `hequirect`, `sbs-lr`, both sources `XRD_SRC_SHAPE`; 1920x1080 gives `flat`, `mono`; 3840x1080 gives `flat`, `sbs-lr`; 1920x2160 gives `flat`, `tb-lr`; 1080x1080 gives `flat`, `mono`; 3840x2160 gives `flat`, `mono`; 1960x1000 (2% edge: `F = 1.96`) gives `hequirect` and 1940x1000 does not.
  - Shape with one field known: every "projection only" and "layout only" row.
  - Metadata: every row of the mapping table, including the two note cases (`note_count == 1`).
  - Precedence per field: flag beats record beats metadata beats name beats shape; a record that sets only `proj` leaves layout to lower sources.
  - No probe (`probe == NULL`) and nothing else: `hequirect`, `sbs-lr`, both `XRD_SRC_FALLBACK`, one note.
  - Stereo metadata present with `sar 1:2`: `frame_aspect` uses 1:1.
  - `xrd_detect_relayout`: (2.0, flat) gives `mono`; (16/9, hequirect) gives `sbs-lr`; (0.889, flat) gives `tb-lr`; (0, flat) gives `mono`.
  - `xrd_detect_describe`: the three example lines of spec 5.8, byte for byte.
- [ ] **Step 7: Run**; expect FAIL. **Step 8: Implement `detect.c`.** **Step 9: Run** the full gate; expect PASS with no new warnings.
- [ ] **Step 10: Commit** `feat(detect): ffprobe output parser and format detection`.

### Task 2: Run ffprobe

**Files:**
- Modify: `src/detect/probe.h`, `src/detect/probe.c`
- Create: `tests/test_probe_run.c`, `tests/fake_ffprobe.sh`
- Modify: `meson.build`

**Interfaces:**
- Consumes: `xrd_probe_parse` (Task 1).
- Produces:

```c
#define XRD_PROBE_TIMEOUT_MS 3000
#define XRD_PROBE_MAX_BYTES (256 * 1024)
typedef enum { XRD_PROBE_OK, XRD_PROBE_SKIPPED, XRD_PROBE_FAILED } xrd_probe_status;
/* Runs: <ffprobe> -v error -show_streams -i <file>   (execvp; stdin and stderr
 * to /dev/null). SKIPPED when file is not a regular file (no err). FAILED, with
 * err set, when the program cannot be started, exits non-zero, exceeds
 * timeout_ms (it is killed and reaped), or prints no video stream. */
xrd_probe_status xrd_probe_run(const char *ffprobe, const char *file, int timeout_ms,
                               xrd_probe *out, char *err, size_t errlen);
```

- [ ] **Step 1: Write `tests/fake_ffprobe.sh`**: a POSIX script whose behaviour is chosen by the environment variable `FAKE_FFPROBE` (`ok`: print `$FAKE_FFPROBE_FILE`; `argv`: write its arguments one per line to `$FAKE_FFPROBE_OUT` then print the fixture; `hang`: `sleep 30`; `flood`: print 2 MiB of `x`; `fail`: exit 3; `segv`: `kill -SEGV $$`).
- [ ] **Step 2: Write failing tests** in `tests/test_probe_run.c` (each creates its own temp file as the "video"):
  - `ok`: status OK and the parsed size matches the fixture.
  - `argv` with a video named `-weird "name".mp4` in a temp directory: the recorded arguments are exactly `-v`, `error`, `-show_streams`, `-i`, the path; status OK (Review Focus 1).
  - `hang` with `timeout_ms = 200`: FAILED within 1 s, err mentions the timeout, and `waitpid(-1, NULL, WNOHANG)` afterwards returns -1 with `ECHILD` (Review Focus 2).
  - `flood`: FAILED or OK without reading more than `XRD_PROBE_MAX_BYTES`, returns within 2 s, no zombie.
  - `fail` and `segv`: FAILED with err set.
  - Program `/nonexistent/ffprobe`: FAILED, err names it.
  - File is a directory, and a missing file: SKIPPED.
- [ ] **Step 3: Run**; expect FAIL. **Step 4: Implement** with `fork`, `execvp`, a pipe and `poll` against a monotonic deadline; `SIGKILL` then `waitpid` on timeout or overflow. **Step 5: Run** the full gate; expect PASS.
- [ ] **Step 6: Commit** `feat(detect): run ffprobe with a timeout`.

### Task 3: Projection math

**Files:**
- Modify: `src/render/projection.h`, `src/render/projection.c`, `tests/test_projection.c`

**Interfaces:**
- Consumes: the enums from Task 1. If Task 1 has not merged, add the same enums here; they are identical text.
- Produces:

```c
typedef enum { XRD_ZOOM_SCREEN, XRD_ZOOM_MAGNIFY } xrd_zoom_mode;

/* HALF or FULL. mono gives FULL. AUTO: sbs is FULL when F >= 3.0, tb is FULL
 * when F < 1.5, otherwise HALF. */
xrd_packing xrd_packing_resolve(xrd_layout layout, xrd_packing packing, double frame_aspect);
/* A of spec 5.3 from a resolved packing. Returns 16.0/9.0 when frame_aspect
 * is not finite or not > 0. */
double xrd_picture_aspect(xrd_layout layout, xrd_packing resolved, double frame_aspect);

typedef struct {
    float tan_h, tan_v;    /* view half-FOV tangents for the eye pass */
    float half_w, half_h;  /* flat: screen half-extents on the plane z = -1; 0 for hequirect */
} xrd_view_geom;
/* hequirect: tan = display tan / zoom. flat SCREEN: tan = display tan,
 * S = zoom. flat MAGNIFY: tan = display tan / zoom, S = 1.
 * half_w = S * min(tan_h0, tan_v0 * A), half_h = half_w / A, with tan_h0 and
 * tan_v0 the display tangents at zoom 1. */
void xrd_view_geometry(xrd_proj proj, xrd_zoom_mode mode, float fov_diag_deg, float eye_aspect,
                       float zoom, double picture_aspect, xrd_view_geom *out);

/* Mirrors the shader's flat branch. False when d[2] >= 0, d is degenerate, or
 * the hit is outside the screen (1e-6 tolerance counts the edge as inside). */
bool xrd_flat_dir_to_uv(const float d[3], float half_w, float half_h, float uv[2]);

/* Replaces xrd_eye_u_transform: tex = uv * scale + offset per axis.
 * tb-lr: the left eye is the top half (offset[1] = 0.5, t = 1 is the top). */
void xrd_eye_uv_transform(xrd_layout layout, int eye, bool swap,
                          float scale[2], float offset[2]);

/* Source pixels per display pixel at the centre of the view, the larger of
 * the two axes; > 1 means the picture is being shrunk. eye_src_w/h is one
 * eye's sub-image in texels. content_*fov_deg are used for hequirect only. */
float xrd_minify_ratio(xrd_proj proj, const xrd_view_geom *g, int eye_src_w, int eye_src_h,
                       int out_eye_w, int out_eye_h,
                       float content_hfov_deg, float content_vfov_deg);
```

`xrd_eye_u_transform` is removed; its callers and tests move to the new function.

- [ ] **Step 1: Write failing tests** (Viture: `fov_diag 43`, `eye_aspect 16/9`):
  - `xrd_packing_resolve`: every row of spec 5.3's auto rule (1920x1080 sbs half, 3840x1080 sbs full, 1920x1080 tb half, 1920x2160 tb full); explicit HALF/FULL pass through; mono is FULL.
  - `xrd_picture_aspect`: the six cells of the spec 5.3 table; `frame_aspect` 0, NaN and infinity return 16/9 (Review Focus 3).
  - `xrd_view_geometry`, flat SCREEN, zoom 1.5, A = 16/9: `2*atan(half_w)` is 54.5 degrees and `2*atan(half_h)` is 32.4 degrees, each within 0.1; `tan_h` equals the zoom-1 display tangent. Zoom 2.0: 69.0 and 42.3.
  - Contain at S = 1: A = 2.39 gives `half_w == tan_h0` and `half_h < tan_v0`; A = 4/3 gives `half_h == tan_v0` and `half_w < tan_h0`.
  - MAGNIFY at zoom 1.5: `half_w == tan_h0` (S = 1) and `tan_h == tan_h0 / 1.5`; and the centre magnification `half_w / tan_h` is equal in both modes.
  - hequirect: `tan_h`, `tan_v` equal `xrd_fov_tan_half` output; `half_w == 0`.
  - `xrd_flat_dir_to_uv`: (0,0,-1) gives (0.5, 0.5); (half_w, 0, -1) gives u = 1 and (1.001*half_w, 0, -1) is outside; d.z = 0 and d.z > 0 are outside; zero vector and NaN are outside.
  - No bow: for yaw in {0, 10, 25} degrees, three view rays through the screen's top edge at different x all give `v == 1` within 1e-5.
  - `xrd_eye_uv_transform`: all five layouts, both eyes, with and without swap. sbs keeps today's u values with `scale[1] = 1, offset[1] = 0`.
  - `xrd_minify_ratio`: flat SCREEN, 3840x2160 eye, 1920x1080 out eye: zoom 1.5 gives 1.33, zoom 1.0 gives 2.0, zoom 0.5 gives 4.0 (within 0.01); 1920x1080 eye at zoom 1.5 gives 0.67; hequirect 2160x2160 eye, 180x180, zoom 1 gives about 0.24.
- [ ] **Step 2: Run** `meson test -C build projection`; expect FAIL. **Step 3: Implement.** **Step 4: Run** the full gate; expect PASS.
- [ ] **Step 5: Commit** `feat(projection): flat mapping, picture aspect, two-axis eye split`.

### Task 4: Renderer: flat branch and two-axis split

**Files:**
- Modify: `src/render/shaders/vr180.frag`, `src/render/renderer_gl.c`, `src/render/renderer_gl.h`, `tests/test_render.c`, `src/main.c` (the one draw call)

**Interfaces:**
- Consumes: Task 3.
- Produces:

```c
typedef struct {
    xrd_proj proj;
    xrd_layout layout;
    bool swap_eyes;
    xrd_view_geom geom;
    bool trilinear;   /* sample through the mipmap chain; the caller guarantees it is valid */
} xrd_draw_params;
/* Replaces the old argument list; the contract comment stays, plus: when
 * dp->trilinear is false the texture's min filter must be GL_LINEAR. */
void xrd_renderer_draw(xrd_renderer *r, unsigned video_tex, const float rot[9],
                       const xrd_draw_params *dp, int out_w, int out_h);
```

Shader requirements: a `u_proj` uniform selects the mapping; the flat branch mirrors `xrd_flat_dir_to_uv`; the eye sub-image is addressed with vec2 scale and offset; the half-texel clamp and every Catmull-Rom tap stay inside the eye's sub-image on both axes; texture lookups that rely on implicit derivatives are not made inside branches that depend on per-fragment values (use `textureGrad` or `textureLod` there). In this task `trilinear` is accepted and ignored. Whether this stays one fragment shader is the implementer's choice.

- [ ] **Step 1: Write failing render tests** (offscreen, same harness as the existing ones):
  - Flat 2D, 16:9 solid green texture with a 1-texel white top row, zoom 1.0, identity rotation: centre pixel green; a pixel just outside the screen at zoom 0.5 is black; at zoom 1.0 the white row lies on a single output row at x = 10%, 50% and 90% of the eye.
  - Flat half SBS (left red, right blue), flat full top-bottom (top red, bottom blue): the left viewport is all red and the right all blue at 20 sample points each, for bilinear and Catmull-Rom; with `swap_eyes` the colours exchange.
  - The same two layouts with a 1919x1080 and a 1920x1081 texture: no sample in the left viewport has blue > 8/255 (Review Focus 4).
  - hequirect `tb-lr` with the top-bottom pattern: left viewport red.
  - Every existing VR180 render test passes unchanged apart from the call signature.
- [ ] **Step 2: Run** `meson test -C build render`; expect FAIL. **Step 3: Implement.** In `main.c`, build an `xrd_draw_params` with `XRD_PROJ_HEQUIRECT` from the existing values so behaviour is unchanged. **Step 4: Run** the full gate; both smoke tests must PASS, not SKIP.
- [ ] **Step 5: Commit** `feat(render): flat projection and top-bottom eye split`.

### Task 5: Coded-size texture and mipmaps

**Files:**
- Modify: `src/video/player_mpv.c`, `.h`, `src/render/shaders/vr180.frag`, `src/render/renderer_gl.c`, `tests/test_render.c`, `tests/test_player.c`, `tests/smoke_player.sh`, `tools/make_pattern.sh`

**Interfaces:**
- Consumes: Task 4.
- Produces:

```c
#define XRD_MIP_MAX_LEVEL 3
/* While want is true, the chain (levels 0..XRD_MIP_MAX_LEVEL) is regenerated
 * right after each video frame mpv renders, and the texture's min filter is
 * GL_LINEAR_MIPMAP_LINEAR. While false: GL_LINEAR and no generation. */
void xrd_player_set_mipmaps(xrd_player *p, bool want);
/* True when the texture's chain matches its current level 0 (generated for the
 * frame now in the texture, at the current size). */
bool xrd_player_mipmapped(const xrd_player *p);
```

`xrd_player_video_size` now returns the coded frame size (after rotation), and the texture has exactly that size with the video filling it, whatever aspect the container declares (spec 5.10).

Renderer: with `dp->trilinear` the eye pass samples trilinearly and Catmull-Rom is not used; the clamp margin on each split axis grows with the level actually sampled so that no level reads across the eye seam (or levels are capped; implementer's choice, the tests decide).

- [ ] **Step 1: Failing render tests:** the half-SBS and full top-bottom colour tests from Task 4, with a 3840x2160 texture, mipmaps generated, `trilinear = true`, at flat zoom 0.25 (shrink 8x) and 0.5: no cross-eye colour above 8/255, including the 1919- and 1081-sized textures (Review Focus 4). A 1-texel checkerboard at 4x shrink renders within 16/255 of mid grey at the centre with `trilinear` and does not with it off.
- [ ] **Step 2: Failing player tests:** the pure parts only (for example a helper that decides the texture size from coded size and rotation, if one is introduced).
- [ ] **Step 3: Failing smoke cases** in `tests/smoke_player.sh`:
  - A stereo-tagged Matroska pattern (`-metadata:s:v stereo_mode=left_right`, 1920x1080): the dumped frame's video texture is 1920x1080. Print the texture size on the existing `video texture:` line as `video texture: rgb10_a2 1920x1080 dither=on mipmap=off` and assert on it.
  - Mipmap state (Review Focus 5): a development flag `--mipmap-check` makes the player assert, before every draw with `trilinear`, that `xrd_player_mipmapped` is true, exiting 1 otherwise. Run a file made by concatenating a 1280x720 and a 1920x1080 segment with mipmaps forced on; exit status 0.
- [ ] **Step 4: Run**; expect FAIL. **Step 5: Implement.** `main.c` is not wired to request mipmaps yet (Task 8); the smoke case forces them with a development flag `--mipmap-force`, undocumented like `--frames`.
- [ ] **Step 6: Run** the full gate; expect PASS.
- [ ] **Step 7: Commit** `feat(player): coded-size video texture and mipmaps when shrinking`.

### Task 6: Values, flags, ini keys, record keys

**Files:**
- Modify: `src/values.c`, `.h`, `src/config.c`, `.h`, `src/store/filestate.c`, `.h`, `tests/test_values.c`, `tests/test_config.c`, `tests/test_filestate.c`

**Interfaces:**
- Consumes: enums from Tasks 1 and 3.
- Produces, `values.h`:

```c
bool xrd_value_proj(const char *s, xrd_proj *out);            /* "hequirect", "flat" */
const char *xrd_value_proj_name(xrd_proj p);
bool xrd_value_packing(const char *s, xrd_packing *out);      /* "half", "full" */
const char *xrd_value_packing_name(xrd_packing p);            /* "auto" for AUTO */
bool xrd_value_zoom_mode(const char *s, xrd_zoom_mode *out);  /* "screen", "magnify" */
const char *xrd_value_zoom_mode_name(xrd_zoom_mode m);
/* xrd_value_layout and _name also accept and return "tb-lr", "tb-rl". */
```

- Produces, `filestate.h`:

```c
enum { XRD_SET_PROJ = 1u << 5, XRD_SET_FLAT_ZOOM = 1u << 6, XRD_SET_PACKING = 1u << 7,
       XRD_SET_ZOOM_MODE = 1u << 8 };
/* xrd_file_settings gains: */
    xrd_proj proj; float flat_zoom; xrd_packing packing; xrd_zoom_mode zoom_mode;
```

Record rules (spec 5.9): `proj = flat` and `proj = hequirect` both parse to `XRD_FS_OK` with `XRD_SET_PROJ`; any other `proj` value is `XRD_FS_UNSUPPORTED`. `xrd_filestate_format` always writes the `proj` line from `st->v.proj`. `flat_zoom`, `packing`, `zoom_mode` are written only when their bit is set. `xrd_filestate_describe` adds, in this order after the existing words: `proj=flat`, `flat-zoom=1.65`, `packing=half`, `zoom-mode=magnify`. `xrd_filestate_collect` and `_equal` cover the new fields with tolerance `XRD_FS_TOL_ZOOM` for `flat_zoom`.

- Produces, `config.h` (new fields of `xrd_config`):

```c
    bool proj_auto;        /* --proj auto (default true) */
    xrd_proj proj;         /* valid when !proj_auto */
    bool layout_auto;      /* --layout auto (default true); layout valid when false */
    xrd_packing packing;   /* --packing auto|half|full, default AUTO */
    xrd_zoom_mode zoom_mode; /* --zoom-mode, default XRD_ZOOM_SCREEN */
    float zoom_default;    /* VR180 zoom from device and ini, before --zoom */
    float flat_zoom;       /* [device.X] flat_zoom, default 1.5 */
    bool screen_locked;    /* --screen fixed|locked, [general] screen */
    bool mipmap;           /* --mipmap on|off, default on */
    char ffprobe[256];     /* [general] ffprobe, default "ffprobe" */
```

`explicit_flags` gains `XRD_SET_PROJ` (for `--proj` other than `auto`), `XRD_SET_PACKING` (other than `auto`) and `XRD_SET_ZOOM_MODE`; `XRD_SET_LAYOUT` is set only for a `--layout` other than `auto`. `--zoom` keeps writing `cfg.zoom` and setting `XRD_SET_ZOOM`.

- [ ] **Step 1: Failing tests.**
  - Values: each parser round-trips its names and rejects `""`, `"Flat"`, `"auto"` (auto is handled by config, not by the value parsers).
  - Records: the README example record still parses to the same settings plus `XRD_SET_PROJ` with `XRD_PROJ_HEQUIRECT`; `version = 1\nproj = flat\nflat_zoom = 1.65\nlayout = tb-lr\npacking = half\nzoom_mode = magnify\n` round-trips; `proj = fisheye` is UNSUPPORTED; `flat_zoom = 9` and `packing = auto` are BAD with a line number; a record with only `zoom` formats without a `flat_zoom` line; the existing test `proj = flat` is UNSUPPORTED is changed to expect OK.
  - Collect: start and end equal with nothing loaded and no flags gives `set == 0`; `end.proj != start.proj` sets `XRD_SET_PROJ` and nothing else; loaded `zoom = 0.56` with a changed `flat_zoom` keeps `zoom = 0.56` and adds `flat_zoom`.
  - Config: defaults (`proj_auto`, `layout_auto`, packing AUTO, zoom mode SCREEN, `flat_zoom` 1.5, not locked, mipmap on, ffprobe `"ffprobe"`); each flag with each value and one bad value; `[device.viture-pro] flat_zoom = 2.0`, `[general] screen = locked`, `[general] ffprobe = /opt/ff/ffprobe`; bad ini values name the file and line; `zoom_default` holds the ini zoom when `--zoom` is also given; `--layout mono` sets `XRD_SET_LAYOUT` and `--layout auto` does not.
- [ ] **Step 2: Run**; expect FAIL. **Step 3: Implement.** **Step 4: Run** the full gate; expect PASS. (The smoke tests still pass: `main.c` ignores the new fields until Task 8, and treats `layout_auto` as `sbs-lr`.)
- [ ] **Step 5: Commit** `feat(config): flags, ini keys and record keys for flat projection`.

### Task 7: Controls and session state

**Files:**
- Modify: `src/app/controls.c`, `.h`, `tests/test_controls.c`
- Create: `src/app/session.c`, `src/app/session.h`, `tests/test_session.c`
- Modify: `meson.build`

**Interfaces:**
- Consumes: Tasks 1, 3, 6.
- Produces, `controls.h` (new fields and functions):

```c
    xrd_proj proj;
    xrd_packing packing;        /* AUTO, HALF or FULL */
    float zoom_other;           /* the zoom of the projection not in effect */
    xrd_zoom_mode zoom_mode;
    bool screen_locked;
    bool layout_chosen;         /* M or S was pressed in this run */

void xrd_controls_init_format(xrd_controls *c, xrd_proj proj, xrd_packing packing,
                              float zoom_other, xrd_zoom_mode mode, bool locked);
void xrd_controls_toggle_proj(xrd_controls *c);     /* flips proj, exchanges zoom and zoom_other */
bool xrd_controls_toggle_zoom_mode(xrd_controls *c); /* false, unchanged, in hequirect */
bool xrd_controls_toggle_lock(xrd_controls *c);      /* false, unchanged, in hequirect */
/* frame_aspect is used to resolve AUTO packing when stepping the flat cycle. */
void xrd_controls_cycle_layout(xrd_controls *c, double frame_aspect);
#define XRD_SEEK_SMALL_S 10.0
#define XRD_SEEK_LARGE_S 60.0
```

Cycles (spec 5.3). hequirect: `sbs-lr`, `sbs-rl`, `tb-lr`, `mono`. flat: (mono), (sbs-lr, HALF), (sbs-lr, FULL), (tb-lr, HALF), (tb-lr, FULL), back to mono; the current position is found from the layout family and the resolved packing, so an `-rl` layout or AUTO packing steps to the next entry. `xrd_controls_cycle_layout` and `xrd_controls_toggle_swap` set `layout_chosen`.

- Produces, `session.h`:

```c
typedef struct {
    xrd_file_settings s;        /* every field filled: proj, zoom (VR180), flat_zoom, layout,
                                   packing, zoom_mode, swap, content fov, convergence */
    unsigned explicit_flags;    /* cfg->explicit_flags with XRD_SET_ZOOM moved to
                                   XRD_SET_FLAT_ZOOM when the file opens flat */
    unsigned applied;           /* record bits actually used, for "settings: restored" */
} xrd_session;
/* What the flags and the record state for detection. rec may be NULL. */
void xrd_session_given(const xrd_config *cfg, const xrd_filestate *rec,
                       xrd_detect_given *flags, xrd_detect_given *record);
/* Starting state. Order per value: defaults and device, ini, record (rec may be
 * NULL), flags. proj, layout and packing come from d. --zoom applies to the
 * projection in d->proj; the other projection starts from cfg->zoom_default or
 * cfg->flat_zoom, then the record. */
void xrd_session_start(const xrd_config *cfg, const xrd_filestate *rec, const xrd_detect *d,
                       xrd_session *out);
/* The view state as record fields (zoom and flat_zoom taken from zoom and
 * zoom_other according to proj). */
void xrd_session_settings(const xrd_controls *c, float content_hfov, float content_vfov,
                          xrd_file_settings *out);
/* Controls for a session: xrd_controls_init, _init_display and _init_format. */
void xrd_session_controls(const xrd_session *ss, const xrd_config *cfg, xrd_controls *c);
```

- [ ] **Step 1: Failing controls tests:** `toggle_proj` from flat zoom 1.5 with `zoom_other` 1.0 gives hequirect zoom 1.0, `zoom_other` 1.5, and back; zoom keys change only the active zoom; the two cycles step exactly as listed, starting from each entry and from `sbs-rl` and from AUTO packing with `frame_aspect` 16/9 and 32/9; `toggle_zoom_mode` and `toggle_lock` return false in hequirect; `layout_chosen` is false after init and true after a cycle or a swap.
- [ ] **Step 2: Failing session tests:**
  - No record, no flags, detection flat/mono: `s.flat_zoom == 1.5`, `s.zoom == cfg.zoom_default`, `explicit_flags == 0`.
  - `--zoom 2.0` with detection flat: `s.flat_zoom == 2.0`, `s.zoom == cfg.zoom_default`, `explicit_flags` has `XRD_SET_FLAT_ZOOM` and not `XRD_SET_ZOOM`; with detection hequirect the reverse.
  - Record `proj = hequirect`, `zoom = 0.56`, detection (from the record) hequirect: `s.zoom == 0.56`, `applied` has `XRD_SET_ZOOM | XRD_SET_PROJ`.
  - The reviewer's two cases (spec 8.1): that record, then `xrd_controls_toggle_proj` only; `xrd_session_settings` plus `xrd_filestate_collect` gives `zoom = 0.56`, `proj = flat` set, no `XRD_SET_FLAT_ZOOM`. And no record, toggle only: `XRD_SET_PROJ` alone.
  - `xrd_session_given`: `--proj flat` gives `flags.have_proj`; `--layout auto` gives `!flags.have_layout`; a record always gives `record.have_proj`; a record without `layout` gives `!record.have_layout`.
- [ ] **Step 3: Run**; expect FAIL. **Step 4: Implement.** `main.c` passes `0.0` as `frame_aspect` to the cycle for now. **Step 5: Run** the full gate; expect PASS.
- [ ] **Step 6: Commit** `feat(app): view state for two projections and session start-up`.

### Task 8: Wiring, keys, output, subtitles, smoke tests

**Files:**
- Modify: `src/main.c`, `src/video/player_mpv.c`, `.h`, `tools/make_pattern.sh`, `tests/smoke_player.sh`, `tests/smoke_tracking.sh`, `tests/test_player.c`

**Interfaces:**
- Consumes: everything above.
- Produces, `player_mpv.h`:

```c
/* The aspect warning is printed only while this is on (hequirect). Default on. */
void xrd_player_set_aspect_warning(xrd_player *p, bool on);
void xrd_player_set_sub_visible(xrd_player *p, bool on);
const char *xrd_player_codec(xrd_player *p);   /* mpv "video-codec", "" if unknown */
double xrd_player_fps(xrd_player *p);          /* mpv "container-fps", 0 if unknown */
```

Start-up order in `main`, all before the window is created: config; record load (existing); `xrd_probe_run(cfg.ffprobe, cfg.file, XRD_PROBE_TIMEOUT_MS, ...)`; `xrd_session_given`; `xrd_detect_resolve`; print each note as `xrdome: note: <text>` and then `detect: <describe>`; `xrd_session_start`; print `settings: restored ...` from `applied`. The record's `block_save` behaviour for bad and unsupported records is unchanged.

Per frame: `xrd_packing_resolve` and `xrd_picture_aspect` from the player's coded size and the probe's pixel aspect (1:1 when stereo metadata was present or there was no probe); `xrd_view_geometry`; `xrd_minify_ratio`; `want = cfg.mipmap && ratio > 1.0` to `xrd_player_set_mipmaps`; `trilinear = want && xrd_player_mipmapped(p)`; the rotation is the identity when `ctl.screen_locked` and the projection is flat.

Keys (spec 5.2, 5.3, 5.4, 5.7), each ignoring key repeat except zoom and seek:

| Key | Action and output |
|---|---|
| Up / Down | `xrd_player_seek(p, +XRD_SEEK_LARGE_S)` / `-XRD_SEEK_LARGE_S` |
| P | `xrd_controls_toggle_proj`; if `!ctl.layout_chosen` and the detected `layout_src` is `XRD_SRC_SHAPE` or `XRD_SRC_FALLBACK`, `xrd_detect_relayout`; print `proj: flat` or `proj: hequirect`, and the `layout:` line if the layout changed |
| M | cycle; print `layout: sbs-lr half` (flat stereo), `layout: mono`, or `layout: sbs-lr` (hequirect) |
| Z | print `zoom mode: screen`, `zoom mode: magnify`, or `zoom mode: magnify (VR180 has no screen mode)` |
| L | print `screen: fixed`, `screen: head-locked`, or `screen: fixed (VR180 cannot be head-locked)`; unlocking calls `xrd_tracker_recenter(trk, false)` and `xrd_controls_recenter` |
| ? | the key list, one `keys: <key>  <action>` line per key in the README table's order |
| I | the `info:` block of spec 5.7, one item per line |

Subtitles (spec 5.11): `xrd_player_set_sub_visible(p, proj == flat && layout == mono)` at start and after every `P` and `M`. Aspect warning (spec 5.12): `xrd_player_set_aspect_warning(p, proj == hequirect)`; the new text is exactly the line in spec 5.12 with the frame size, layout word and content FOV substituted. `--content-fov` given while the file opens flat prints `xrdome: note: --content-fov applies to VR180 only; ignored for this flat video`.

At quit, `xrd_session_settings` feeds `xrd_filestate_collect`; `memory_save` writes as today.

- [ ] **Step 1: Patterns.** `tools/make_pattern.sh` gains a first argument form `--kind sbs180|flat2d|flat-hsbs|flat-tb` (default `sbs180`, the existing output, so current callers are unaffected). `flat2d`: 1920x1080, green field, white 8-pixel border, a yellow box above centre, and a 256x256 block of 1-pixel black and white lines at the centre for judging shimmer. `flat-hsbs`: 1920x1080, left half red, right half blue, each squeezed 16:9 content with the same marks. `flat-tb`: 1920x2160, top red, bottom blue.
- [ ] **Step 2: Failing smoke cases** (`tests/smoke_player.sh`; every case uses `--windowed --pose null --frames N --no-memory` unless it is about records):
  - The existing 2:1 pattern with no format flags prints `detect: proj=hequirect (frame shape) layout=sbs-lr (frame shape)` and passes the existing frame check.
  - Existing cases that play a 16:9 pattern as VR180 pass `--proj hequirect`; the reworded warning is asserted there, once.
  - `flat2d` with no flags prints `detect: proj=flat (frame shape) layout=mono (frame shape)`; with `--zoom 1.0` the dumped frame has the white border at the eye's edges and black nowhere inside; with `--zoom 0.5` the corners are black; no line starts with `xrdome:`.
  - `flat-hsbs` copied to `clip.HSBS.mp4` prints `layout=sbs-lr (file name "HSBS") packing=half (file name "HSBS")`; left viewport red, right blue.
  - `flat-tb` with no hint is detected by shape; left viewport red.
  - The stereo-tagged Matroska case from Task 5 prints `layout=sbs-lr (metadata)`.
  - `[general] ffprobe = /nonexistent` in a temp config: exactly one `xrdome: note:` line, exit 0, and the 2:1 pattern still plays as `hequirect` by fallback.
  - `--screen locked` with `flat2d` under `smoke_tracking.sh`'s sweep: two frames dumped at different times are identical; with `--screen fixed` they differ.
  - Records: `flat2d --zoom 2.0` (memory on) prints `settings: saved proj=flat flat-zoom=2.00`; the next run prints `settings: restored proj=flat flat-zoom=2.00` and `detect: proj=flat (record)`.
  - Subtitles: `flat2d` with an `.srt` beside it (one cue covering the whole clip) has non-green pixels in the lower third that the same run with `--mpv-opt sub-visibility=no` does not have; `flat-hsbs` with an `.srt` has none. If libmpv does not draw subtitles here at all, remove the first assertion, make `xrd_player_set_sub_visible` always hide, and record the finding for Task 9 (spec 5.11).
  - A file named `-weird name.mp4` (copy of `flat2d`, run as `./-weird\ name.mp4`) plays (Review Focus 1).
- [ ] **Step 3: Run**; expect FAIL. **Step 4: Implement.** Remove the development flag `--mipmap-force` only if nothing uses it; keep `--mipmap-check` in the smoke runs.
- [ ] **Step 5: Run** the full gate three times; expect PASS each time (the smoke tests are timing-sensitive).
- [ ] **Step 6: Commit** `feat: flat projection, format detection and new keys wired into the player`.

### Task 9: Documentation

**Files:**
- Modify: `README.md`, `docs/STATUS.md`, `docs/superpowers/specs/2026-11-13-flat-projection-design.md` (status line only)

- [ ] **Step 1: README.** Status paragraph; keys table (Up/Down, P, Z, L, ?, I; the `M` cycles); flags list and a paragraph each for `--proj`, `--packing`, `--zoom-mode`, `--screen`, `--mipmap`; config keys `flat_zoom`, `screen`, `ffprobe`; new sections "Flat video" (what it is; the two zoom modes; the size table for the Viture from spec 5.2; screen lock) and "Format detection" (the order, the three tables, the `detect:` line, the four known wrong guesses with the key that fixes each); "Per-file settings" (the `proj` line now pins the projection; `flat_zoom`, `packing`, `zoom_mode`; the note about films saved under the old workaround); "Sharpness" (mipmaps and the shrink table); dependencies (`ffprobe` at run time). Remove the sentence that `--proj hequirect` is the only projection. State plainly what has not been run on glasses yet.
- [ ] **Step 2: STATUS.** Change the heading `## Per-file settings memory (branch \`feature/per-file-settings\`, head \`6d93d87\`)` to `## Per-file settings memory (merged in PR #1, \`main\` at \`3358de5\`)`, and line 4's "`main` only" wording if it is now wrong. Add a section for this feature (decisions, what the sandbox verified, what awaits the hardware run), the unverified items of spec section 11 that are still open, and under candidate features put the per-eye HUD first, taking over `?` and `I`.
- [ ] **Step 3: Spec status** line to `approved 2026-11-13; implemented in PRs ...`.
- [ ] **Step 4: Check** every flag, key and message quoted in the README against `./build/xrdome` output and the source. Run the gate.
- [ ] **Step 5: Commit** `docs: flat projection, format detection, STATUS heading fix`.

### After the last PR: hardware run

Spec 8.4, with the owner at the Viture Pro XR, using the hardware-run skill. On AC power; glasses switched to SBS by hand; timing pairs use `--no-memory` and differ in one flag only. The result is one PR that records what was seen in `docs/STATUS.md` and the README, and updates the project doc `claude/xrdome-status.md`.
