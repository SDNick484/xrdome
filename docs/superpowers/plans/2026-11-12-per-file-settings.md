# Per-File Settings Memory Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** xrdome remembers zoom, layout, swap-eyes, content FOV and convergence per video file and restores them on open; the convergence keys are removed; the aspect warning names flat video.

**Architecture:** A new pure module `src/store/filestate.c` computes a content key for a video, reads and writes one small record file per video under the XDG state directory, and merges a record with the starting settings. `config.c` records which flags were given explicitly. `main.c` loads the record before the player opens and saves once after the render loop ends. No I/O is added inside the loop.

**Tech Stack:** C11, meson, cmocka, POSIX file APIs. No new dependencies.

**Spec:** `docs/superpowers/specs/2026-11-12-per-file-settings-design.md` (read it first; section numbers below refer to it).

## Global Constraints

- Branch `feature/per-file-settings`. Never commit to or merge into `main`.
- C11, `warning_level=3`; new code compiles without warnings. Prefix `xrd_`.
- No new dependencies. `src/store/` includes no SDL, GL or mpv headers.
- Record location: `$XDG_STATE_HOME/xrdome/files/<size>-<hash>.ini` when `XDG_STATE_HOME` is an absolute path, otherwise `$HOME/.local/state/xrdome/files/`. Directories are created with mode 0700.
- Key: decimal size, `-`, 16 lowercase hex digits of 64-bit FNV-1a (offset basis `0xcbf29ce484222325`, prime `0x100000001b3`) over the first `min(size, 65536)` bytes then the last `min(size, 65536)` bytes.
- Value ranges: zoom 0.25 to 4, content FOV 10 to 180 per axis, convergence -5 to 5, layout `sbs-lr|sbs-rl|mono`, swap `yes|no`.
- A missing, unreadable, corrupt or newer record never stops playback and is never overwritten.
- Messages about settings start with `settings:`; warnings and notes start with `xrdome: warning:` / `xrdome: note:` and are one physical line each.
- Tests never touch the real state directory: unit tests use `mkdtemp`, smoke scripts export `XDG_STATE_HOME` under their work directory.
- Every task ends with `meson compile -C build && meson test -C build` passing, then a commit ending with the two trailer lines the session uses for commits.
- Build: `meson setup build` once, then `meson compile -C build` and `meson test -C build`. To run one test: `meson test -C build filestate -v`.

## Review Focus

1. State directory cannot be created or written (read-only home, a file in the way): one `xrdome: warning:` line at quit, exit status unchanged. Test in Task 2.
2. Video path containing a newline or `=`: the `path` label must not inject keys into the record. Test in Task 2.
3. Record edited by hand with CRLF line endings or trailing spaces: still parsed. Test in Task 2.
4. The argument is a directory, or a symlink to a video: a directory gives no key (memory off); a symlink gives the same key as its target. Test in Task 1.
5. Video given as a relative path: the `path` label is written absolute. Test in Task 2.

## File Structure

| File | Responsibility |
|---|---|
| `src/store/filestate.h`, `filestate.c` (create) | Key, state directory, record parse/format/load/save/delete, apply and collect |
| `tests/test_filestate.c` (create) | Unit tests for all of the above |
| `src/config.h`, `config.c` (modify) | `explicit_flags`, `--no-memory`, `--forget` |
| `src/app/controls.h`, `controls.c` (modify) | Convergence key functions removed |
| `src/main.c` (modify) | Load, apply, save, messages; convergence keys removed |
| `src/video/player_mpv.c` (modify) | Aspect warning text |
| `tests/smoke_player.sh`, `smoke_tracking.sh` (modify) | Isolation, new memory cases, flat-video warning |
| `README.md`, `docs/STATUS.md` (modify) | User documentation and status |

---

### Task 1: File key and state directory

**Files:**
- Create: `src/store/filestate.h`, `src/store/filestate.c`, `tests/test_filestate.c`
- Modify: `meson.build` (add `'src/store/filestate.c'` to `core_sources`, `'filestate'` to `test_names`)

**Interfaces:**
- Produces (in `filestate.h`, which includes `render/projection.h` for `xrd_layout`):

```c
enum { XRD_SET_ZOOM = 1u << 0, XRD_SET_LAYOUT = 1u << 1, XRD_SET_SWAP = 1u << 2,
       XRD_SET_CONTENT_FOV = 1u << 3, XRD_SET_CONVERGENCE = 1u << 4 };

typedef struct {
    float zoom;
    xrd_layout layout;
    bool swap;
    float content_hfov, content_vfov;
    float convergence;
} xrd_file_settings;

typedef struct {
    unsigned set;        /* XRD_SET_* bits: which fields of v are meaningful */
    xrd_file_settings v;
} xrd_filestate;

#define XRD_FNV1A64_INIT 0xcbf29ce484222325ULL
#define XRD_FILESTATE_KEY_MAX 40
#define XRD_FILESTATE_CHUNK 65536

uint64_t xrd_fnv1a64(uint64_t h, const void *data, size_t len);
/* 0 and key "<size>-<16 hex>"; -1 if path is not a readable regular file
 * (after following symlinks) or key does not fit. */
int xrd_filestate_key(const char *path, char *key, size_t keylen);
/* ".../xrdome/files" per Global Constraints; -1 if neither variable is usable. */
int xrd_filestate_dir(char *buf, size_t len);
```

- [ ] **Step 1: Write the failing tests** in `tests/test_filestate.c` (helper `write_file(dir, name, bytes, len)` using a `mkdtemp` directory removed in teardown):

```c
/* fnv_vectors */
assert_true(xrd_fnv1a64(XRD_FNV1A64_INIT, "", 0) == 0xcbf29ce484222325ULL);
assert_true(xrd_fnv1a64(XRD_FNV1A64_INIT, "a", 1) == 0xaf63dc4c8601ec8cULL);
assert_true(xrd_fnv1a64(XRD_FNV1A64_INIT, "foobar", 6) == 0x85944171f73967e8ULL);
/* key_small_files: empty file, and a 1-byte file "a" (both ranges are "a") */
assert_string_equal(key_of(empty), "0-cbf29ce484222325");
assert_string_equal(key_of(one_a), "1-089c4307b54596b7");
/* key_overlap_and_large: byte i is (i*7+3)&0xff */
assert_string_equal(key_of(file_100000), "100000-09f9b79371c62325");
assert_string_equal(key_of(file_200000), "200000-8ae86f2097da2325");
/* key_survives_rename: rename() the 200000 file, same key */
/* key_changes_on_edit: flip byte 10 -> different key; flip byte 199990 -> different key;
   flip byte 100000 (outside both ranges) -> same key */
/* key_rejects_non_files: a directory and a missing path return -1 */
/* key_follows_symlink: symlink() to the 200000 file gives the same key */
/* key_buffer_too_small: keylen 8 returns -1 */
/* dir_selection (setenv/unsetenv, restored in teardown): */
/*   XDG_STATE_HOME=/x/state          -> "/x/state/xrdome/files"          */
/*   XDG_STATE_HOME unset, HOME=/h/u  -> "/h/u/.local/state/xrdome/files" */
/*   XDG_STATE_HOME="" or "rel/path"  -> the HOME form                    */
/*   both unset or HOME=""            -> -1                                */
```

- [ ] **Step 2: Run** `meson compile -C build` and confirm it fails because `store/filestate.h` does not exist.
- [ ] **Step 3: Implement** the three functions. `xrd_filestate_key` uses `stat` (must be `S_ISREG`), `fopen`/`fseeko`/`fread`; a short read is a failure. Define `_POSIX_C_SOURCE 200809L` and `_FILE_OFFSET_BITS 64` at the top of the `.c` file.
- [ ] **Step 4: Run** `meson test -C build filestate -v`; expect all tests pass.
- [ ] **Step 5: Commit** `feat(store): file key and state directory`.

---

### Task 2: Record parse, format, load, save, delete

**Files:**
- Modify: `src/store/filestate.h`, `src/store/filestate.c`, `tests/test_filestate.c`

**Interfaces:**
- Consumes: Task 1 types.
- Produces:

```c
#define XRD_FILESTATE_VERSION 1
typedef enum { XRD_FS_OK, XRD_FS_NONE, XRD_FS_UNSUPPORTED, XRD_FS_BAD } xrd_fs_status;

/* Parses record text. OK fills *out; UNSUPPORTED (version > 1, or proj other
 * than hequirect) and BAD leave *out with set == 0. err gets "line N: ..." for BAD. */
xrd_fs_status xrd_filestate_parse(const char *text, xrd_filestate *out, char *err, size_t errlen);
/* Writes the record text (spec 5.3). Returns length, or -1 if it does not fit. */
int xrd_filestate_format(const xrd_filestate *st, const char *path_label, const char *date,
                         char *buf, size_t len);
/* "<dir>/<key>.ini"; -1 if it does not fit. */
int xrd_filestate_record_path(const char *dir, const char *key, char *buf, size_t len);
/* NONE when the file does not exist; BAD (with err) for any other read error
 * or a file over 65536 bytes. */
xrd_fs_status xrd_filestate_load(const char *record_path, xrd_filestate *out,
                                 char *err, size_t errlen);
/* Creates dir (and parents, 0700), writes a temp file in dir, renames it to
 * "<key>.ini". video_path is made absolute for the label. 0, or -1 with err. */
int xrd_filestate_save(const char *dir, const char *key, const xrd_filestate *st,
                       const char *video_path, char *err, size_t errlen);
/* 1 deleted, 0 no such record, -1 error. */
int xrd_filestate_delete(const char *dir, const char *key);
```

Decisions the tests rely on:

- Written keys, in order: `version`, `path`, `saved`, `proj`, then only the set ones of `zoom` (`%.4f`), `layout`, `swap` (`yes`/`no`), `content_fov` (`%gx%g`), `convergence` (`%.2f`). First line is `# xrdome per-file settings`.
- Parsing: trim whitespace (including `\r`); blank lines and `#` lines skipped; a line without `=` is BAD; unknown keys ignored; a later duplicate key wins; `path` and `saved` values are ignored; `proj` absent counts as `hequirect`; `version` absent or not a positive integer is BAD.
- All lines are parsed before deciding: if any `version` line is greater than 1 or `proj` is not `hequirect`, the result is UNSUPPORTED even when other lines are bad; otherwise the first bad line gives BAD.
- In the `path` label, every byte below 0x20 and 0x7f is written as `?`.
- `saved` is the local date as `YYYY-MM-DD`, computed inside `xrd_filestate_save`.

- [ ] **Step 1: Write the failing tests:**

```c
/* format_only_set_fields: set = ZOOM|CONTENT_FOV, zoom 0.7513, fov 120x90,
   label "/v/a.mp4", date "2026-11-12" -> exactly: */
"# xrdome per-file settings\nversion = 1\npath = /v/a.mp4\nsaved = 2026-11-12\n"
"proj = hequirect\nzoom = 0.7513\ncontent_fov = 120x90\n"
/* format_all_fields: adds "layout = sbs-rl\nswap = yes\n" between zoom and
   content_fov, and "convergence = 1.50\n" last */
/* round_trip: format then parse gives the same set bits and values (tolerance 1e-4) */
/* parse_minimal: "version = 1\n" -> OK, set == 0 */
/* parse_tolerates: CRLF endings, trailing spaces, "future_key = 7", a comment -> OK */
/* parse_duplicate_last_wins: "zoom = 0.5" then "zoom = 0.8" -> 0.8 */
/* parse_bad: each of these -> BAD, set == 0, err contains "line":
     no version line; "version = x"; "zoom = 9"; "zoom = abc"; "layout = tb";
     "swap = maybe"; "content_fov = 200x90"; "content_fov = 120"; "convergence = 6";
     "just some words" */
/* parse_unsupported: "version = 2\n" -> UNSUPPORTED; "version = 1\nproj = flat\n" ->
   UNSUPPORTED; "version = 2\nzoom = abc\n" -> UNSUPPORTED (not BAD) */
/* path_label_cannot_inject: format with label "/v/a\nzoom = 4.mp4", set == 0;
   the text has exactly one '\n' per record line and parse gives set == 0 */
/* save_load_delete (mkdtemp D): save(D "/s/xrdome/files", key, st, "/v/a.mp4") == 0;
   the directory exists with mode 0700; the only entry is "<key>.ini";
   load gives OK and the same values; a second save succeeds;
   delete returns 1, then 0; load then gives NONE */
/* save_relative_path_label: chdir(D), save with video_path "clip.mp4"; the file
   contains "path = <D>/clip.mp4" */
/* save_fails_cleanly: dir is "<regular file>/files" -> -1 with non-empty err;
   nothing else is created */
/* load_oversize: a 70000-byte record -> BAD */
```

- [ ] **Step 2: Run** `meson test -C build filestate -v`; expect compile failure on the new names.
- [ ] **Step 3: Implement.** Reuse the parsing rules above; share one table of key names between parse and format. The temp file is created with `mkstemp` in `dir` and removed if the rename fails.
- [ ] **Step 4: Run** `meson test -C build filestate -v`; expect pass.
- [ ] **Step 5: Commit** `feat(store): per-file record read and write`.

---

### Task 3: Apply, collect, compare, describe

**Files:**
- Modify: `src/store/filestate.h`, `src/store/filestate.c`, `tests/test_filestate.c`

**Interfaces:**
- Consumes: Task 1 types.
- Produces:

```c
#define XRD_FS_TOL_ZOOM 0.001f
#define XRD_FS_TOL_FOV 0.01f
#define XRD_FS_TOL_CONVERGENCE 0.001f

/* Copies each field that is set in rec and whose bit is NOT in explicit_flags
 * into *s. Returns the bits copied. */
unsigned xrd_filestate_apply(const xrd_filestate *rec, unsigned explicit_flags,
                             xrd_file_settings *s);
/* Spec 5.5. A field is set in *out when it is set in loaded (may be NULL), or
 * its bit is in explicit_flags, or end differs from start beyond the
 * tolerance. Values come from end. */
void xrd_filestate_collect(const xrd_filestate *loaded, unsigned explicit_flags,
                           const xrd_file_settings *start, const xrd_file_settings *end,
                           xrd_filestate *out);
/* Same set bits, and set fields equal within the tolerances. */
bool xrd_filestate_equal(const xrd_filestate *a, const xrd_filestate *b);
/* "zoom=0.75 layout=sbs-rl swap=yes content-fov=120x90 convergence=1.5" for the
 * fields in (st->set & mask), in that order, space separated. Returns length,
 * 0 for none, -1 if it does not fit. Formats: zoom %.2f, fov %gx%g, convergence %.1f. */
int xrd_filestate_describe(const xrd_filestate *st, unsigned mask, char *buf, size_t len);
```

- [ ] **Step 1: Write the failing tests:**

```c
/* apply_record_over_defaults: rec {ZOOM|CONTENT_FOV, zoom .75, 120x90}, explicit 0,
   s = defaults(1.0, sbs-lr, false, 180x180, 0) -> returns ZOOM|CONTENT_FOV;
   s.zoom .75, fov 120x90, layout/swap/convergence untouched */
/* apply_flag_wins: same rec, explicit = XRD_SET_ZOOM, s.zoom 2.0 ->
   returns CONTENT_FOV; s.zoom stays 2.0 */
/* apply_each_field: one test per bit, set alone, copied alone */
/* collect_rule1_loaded: loaded {ZOOM, .75}, explicit 0, start==end (zoom .75) ->
   out.set == ZOOM, zoom .75 */
/* collect_rule2_flag: loaded NULL, explicit CONTENT_FOV, start==end (90x90) ->
   out.set == CONTENT_FOV, 90x90 */
/* collect_rule3_changed: loaded NULL, explicit 0, end.zoom = start.zoom / 1.1f ->
   ZOOM; end.layout != start.layout -> LAYOUT; end.swap != start.swap -> SWAP */
/* collect_zoom_out_and_back: z = 1.0f; z /= 1.1f; z *= 1.1f; end.zoom = z -> set == 0 */
/* collect_nothing: loaded NULL, explicit 0, start == end -> set == 0 */
/* collect_values_from_end: loaded {ZOOM, .75}, end.zoom .5 -> zoom .5 */
/* equal: identical -> true; zoom differs by 0.0005 -> true; by 0.01 -> false;
   different set bits -> false; unset fields with different values -> true */
/* describe: all fields -> "zoom=0.75 layout=sbs-rl swap=yes content-fov=120x90 convergence=1.5";
   mask = CONTENT_FOV -> "content-fov=120x90"; mask 0 -> returns 0, buf "" */
```

- [ ] **Step 2: Run**; expect compile failure.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** `meson test -C build filestate -v`; expect pass.
- [ ] **Step 5: Commit** `feat(store): merge a record with flags and runtime changes`.

---

### Task 4: Config: explicit flags, `--no-memory`, `--forget`

**Files:**
- Modify: `src/config.h`, `src/config.c`, `tests/test_config.c`

**Interfaces:**
- Consumes: `XRD_SET_*` from `store/filestate.h` (include it from `config.h`).
- Produces, as new `xrd_config` fields:

```c
unsigned explicit_flags; /* XRD_SET_ZOOM|LAYOUT|CONTENT_FOV|CONVERGENCE given on the command line */
bool no_memory;          /* --no-memory */
bool forget;             /* --forget */
```

- [ ] **Step 1: Write the failing tests** in `tests/test_config.c`:

```c
/* defaults: explicit_flags == 0, !no_memory, !forget (add to defaults_match_spec) */
/* args_explicit_flags: via xrd_config_load with ini_path NULL:
     {"xrdome","a.mp4"}                         -> 0
     {"xrdome","--zoom","0.5","a.mp4"}          -> XRD_SET_ZOOM
     {"xrdome","--layout","mono","a.mp4"}       -> XRD_SET_LAYOUT
     {"xrdome","--content-fov","120x90","a.mp4"}-> XRD_SET_CONTENT_FOV
     {"xrdome","--convergence","1","a.mp4"}     -> XRD_SET_CONVERGENCE
     all four                                   -> all four bits */
/* explicit_default_value_counts: {"--zoom","1.0"} still sets XRD_SET_ZOOM */
/* ini_zoom_is_not_explicit: ini with "[device.viture-pro]\nzoom = 0.8\n", no flags ->
   zoom 0.8, explicit_flags == 0 */
/* args_memory_flags: "--no-memory" sets no_memory; "--forget" sets forget; both together both */
/* ini_rejects_new_keys: "no_memory = 1" and "forget = 1" under [general] are unknown keys */
```

- [ ] **Step 2: Run** `meson test -C build config -v`; expect failure.
- [ ] **Step 3: Implement** in `xrd_config_parse_args`: two `no_argument` options, and set the bit in the four existing cases on success. `xrd_config_defaults` already zeroes the struct.
- [ ] **Step 4: Run** `meson test -C build`; expect all pass.
- [ ] **Step 5: Commit** `feat(config): record explicit flags; add --no-memory and --forget`.

---

### Task 5: Remove the convergence keys

**Files:**
- Modify: `src/app/controls.h`, `src/app/controls.c`, `src/main.c`, `tests/test_controls.c`

**Interfaces:**
- Produces: `xrd_controls` without `convergence_start`; `xrd_controls_convergence_inc`, `_dec`, `_reset`, `XRD_CONVERGENCE_STEP` removed. `convergence_deg` and `xrd_controls_init_display(c, convergence_deg, filter)` stay, so `--convergence` still reaches the renderer.

- [ ] **Step 1: Edit the tests:** delete `convergence_steps_by_tenth`, `convergence_clamps_to_5`, `convergence_reset_returns_to_configured` and their registrations in `tests/test_controls.c`; keep the test that `xrd_controls_init_display` sets `convergence_deg`, dropping any `convergence_start` assertion.
- [ ] **Step 2: Remove** the functions, the field and the step constant; in `src/main.c` remove the `SDLK_COMMA`, `SDLK_PERIOD` and `SDLK_SLASH` cases and the `convergence:` line from `report_display_changes`.
- [ ] **Step 3: Run** `meson compile -C build && meson test -C build`; expect pass, including the existing smoke case `--convergence 2 --filter bilinear`.
- [ ] **Step 4: Verify** `grep -rn "convergence_inc\|convergence_dec\|convergence_reset\|convergence_start\|SDLK_COMMA\|SDLK_PERIOD\|SDLK_SLASH" src tests` prints nothing.
- [ ] **Step 5: Commit** `feat!: remove the convergence keys; --convergence stays`.

---

### Task 6: Load and save in `main.c`, with smoke tests

**Files:**
- Modify: `src/main.c`, `tests/smoke_player.sh`, `tests/smoke_tracking.sh`

**Interfaces:**
- Consumes: everything from Tasks 1 to 4.

Behaviour to implement in `main()` (spec 5.4 to 5.8):

1. After `xrd_config_load` succeeds: memory is available when `xrd_filestate_key(cfg.file, ...)` and `xrd_filestate_dir(...)` both return 0. If not available, skip every step below silently.
2. If `cfg.forget` and `xrd_filestate_delete` returns 1, print `settings: forgot <key>`.
3. If `cfg.no_memory`, memory is off from here on.
4. Load the record. On `XRD_FS_OK`: build an `xrd_file_settings` from `cfg` (zoom, layout, swap false, content FOV, convergence), call `xrd_filestate_apply` with `cfg.explicit_flags`, copy the result back into `cfg`, and if any bit was applied print `settings: restored <describe(applied bits)>`. On `XRD_FS_UNSUPPORTED` print `xrdome: note: settings record <path> is from a newer xrdome; ignoring it (--forget removes it)`. On `XRD_FS_BAD` print `xrdome: warning: settings record <path>: <err>; ignoring it (--forget removes it)`. In both of those cases, do not save at quit.
5. The settings after step 4 are the start values. After `xrd_controls_init`, set `ctl.swap_eyes` from them.
6. Track whether any video frame was rendered (`tex != 0` at least once).
7. At the `out:` label, before teardown: if memory is on, saving is not blocked and a frame was rendered, build the end values from `ctl` (zoom, layout, swap, convergence) and `cfg` (content FOV), call `xrd_filestate_collect`, and skip the write when the result has `set == 0` and no record was loaded, or when a record was loaded and `xrd_filestate_equal` holds. Otherwise `xrd_filestate_save`; on success print `settings: saved <describe(all set bits)>`, on failure print `xrdome: warning: cannot save settings: <err>`. The exit status is not changed by any of this.

- [ ] **Step 1: Isolate the existing smoke cases.** In both scripts, after `work=` is created add `export XDG_STATE_HOME="$work/state"`, and add `--no-memory` to every existing `xrdome` invocation. Run `meson test -C build`; expect failure (`unknown option` is already fixed by Task 4, so expect pass here and treat this as the baseline).
- [ ] **Step 2: Add failing smoke cases** at the end of `tests/smoke_player.sh`, before `exit $fail`, using a copy of the pattern at `$work/mem.mp4` and `rec() { ls "$XDG_STATE_HOME"/xrdome/files/*.ini 2>/dev/null; }`:

```sh
# M1. no flags, no keys: nothing is saved and no record appears
#     run: --windowed --pose null --frames 30 mem.mp4
#     expect: exit 0; no "settings:" line; rec prints nothing
# M2. flags are saved: --zoom 1.0 --content-fov 90x90 --frames 30
#     expect: exit 0; one line "settings: saved zoom=1.00 content-fov=90x90";
#     exactly one record; it contains "content_fov = 90x90" and "path = $work/mem.mp4"
# M3. restored: no flags, --frames 30 --dump-frame mem90.ppm
#     expect: "settings: restored zoom=1.00 content-fov=90x90"; no "settings: saved" line;
#     the dumped frame passes the same python check as case 1a (compare against out.ppm)
# M4. survives rename: mv mem.mp4 moved.mp4; run as M3 on moved.mp4
#     expect: the same "settings: restored" line; still exactly one record
# M5. a flag wins and is saved: --content-fov 180x180 --frames 30 --dump-frame mem180.ppm
#     expect: "settings: restored zoom=1.00" (no content-fov in it);
#     "settings: saved zoom=1.00 content-fov=180x180"; check_frame mem180.ppm passes
# M6. --no-memory: put 90x90 back (run M2's flags), then run with --no-memory --dump-frame nm.ppm
#     expect: no "settings:" line; check_frame nm.ppm passes (180x180 picture)
# M7. corrupt record: append "zoom = 99" to the record; run with no flags
#     expect: exit 0; exactly one "xrdome: warning: settings record" line; no "settings: saved";
#     the record still contains "zoom = 99"
# M8. --forget: run with --forget --frames 30 --dump-frame fg.ppm
#     expect: "settings: forgot "; check_frame fg.ppm passes; rec prints nothing afterwards
# M9. never rendered: --content-fov 120x90 on audio.m4a
#     expect: exit 1 as in case 1e; rec prints nothing
# M10. unwritable state dir: XDG_STATE_HOME="$work/pattern.mp4" with --zoom 0.5 --frames 30
#     expect: exit 0; exactly one "xrdome: warning: cannot save settings" line
```

- [ ] **Step 3: Run** `meson test -C build smoke -v`; expect the new cases to fail.
- [ ] **Step 4: Implement** the seven numbered behaviours in `src/main.c`.
- [ ] **Step 5: Run** `meson test -C build`; expect all pass.
- [ ] **Step 6: Commit** `feat: remember settings per file`.

---

### Task 7: Aspect warning names flat video

**Files:**
- Modify: `src/video/player_mpv.c` (the `fprintf` near the `xrd_video_aspect_matches` call), `tests/smoke_player.sh`

- [ ] **Step 1: Add a failing smoke case:** make a 2 s 1920x1080 file with `ffmpeg -f lavfi -i testsrc=s=1920x1080:r=30 -t 2 -c:v libx264 -preset ultrafast -pix_fmt yuv420p "$work/flat.mp4"`, play it with `--windowed --pose null --no-memory --frames 10`, and expect exit 0 and exactly one line matching `^xrdome: warning: video is 1920x1080, .*ordinary flat video.*--layout mono --content-fov 60x34`.
- [ ] **Step 2: Run** `meson test -C build smoke -v`; expect that case to fail.
- [ ] **Step 3: Change the message** to this single line (arguments as today: width, height, `mono`/`side-by-side`, H, V):

```
xrdome: warning: video is %dx%d, which is not the aspect of a %s video covering %gx%g degrees. If this is an ordinary flat video, xrdome has no flat projection yet; --layout mono --content-fov 60x34 shows it as an approximate curved screen. If it is VR video covering less than 180 degrees, set --content-fov. Playing it as given.
```

- [ ] **Step 4: Run** `meson test -C build`; expect pass (case 1a still finds no `aspect` line for 90x90).
- [ ] **Step 5: Commit** `feat: aspect warning names flat video`.

---

### Task 8: Documentation

**Files:**
- Modify: `README.md`, `docs/STATUS.md`

- [ ] **Step 1: README.**
  - Status paragraph at the top: replace the out-of-date "hardware timing check ... is still to do" sentence with the current state from `docs/STATUS.md`.
  - Keys table: remove the two convergence rows; remove "convergence" from the sentence under the table.
  - Flags: add `--no-memory` and `--forget`; change the sentence saying `--convergence` and `--content-fov` are "command-line flags only" to say they are also remembered per file.
  - New section "Per-file settings" after "Config file": what is remembered (zoom, layout, swap-eyes, content FOV, convergence); that only settings changed by key or flag are stored and the rest follow the defaults; precedence (defaults, device, ini, record, flags); where records live and the key format; that a rename or move keeps settings and a re-encode does not; saved at quit once a frame was shown; the three `settings:` lines; `--no-memory`, `--forget`; how to find and edit a record (`grep -l NAME ~/.local/state/xrdome/files/*`, `vim`); that a bad or newer record is ignored with one line and never overwritten.
  - "Viewing comfort and sharpness": rewrite the convergence paragraph. Lead with: close subjects are hard to fuse at zoom 1 and above; zoom out (`[`) until they fuse. Then: `--convergence DEG` is an experimental flag; on the Viture Pro XR up to 2 degrees made no significant difference and 5 degrees put the scene out of focus until zoomed far out; untested on the XREAL Air 2 Pro. Keep the description of what the flag does geometrically.
- [ ] **Step 2: `docs/STATUS.md`.** Add per-file settings to "What works" (desktop-verified; hardware check pending). Under "Decisions": convergence demoted to a flag, keys removed, with the reason. Under "Open": remove "per-file settings memory" and the "Convergence keys" line; add "flat projection with head tracking and zoom" to the candidate features next to format auto-detection; add "per-file settings: hardware check on the Viture" with the four commands from spec section 8.
- [ ] **Step 3: Verify** `grep -n "Press \`\.\`\|, / \.\|convergence_start" README.md docs/STATUS.md` prints nothing, and `meson test -C build` still passes.
- [ ] **Step 4: Commit** `docs: per-file settings, convergence demoted`.

---

## After the last task

- Whole-branch review against the spec.
- Hand the owner the hardware check from spec section 8 and the timing comparison (same file with and without `--no-memory`, each with `--timing-log`).
- Ask before merging to `main`.

## Not verified when this plan was written

- The plan was written from reading the code at `008fd01`; nothing was compiled. The sandbox used for execution had no mpv, SDL2, cmocka, epoxy or EGL development packages installed at that time, so the first execution step is installing them there (the owner's Gentoo machine already has them).
- Whether `tests/test_controls.c` has other assertions that touch `convergence_start` beyond those listed in Task 5.
