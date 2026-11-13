# xrdome: per-file settings memory (design spec)

Date: 2026-11-12
Status: approved 2026-11-12 (one record file per video confirmed)
Branch: `feature/per-file-settings`
Builds on: `docs/superpowers/specs/2026-11-11-xrdome-design.md`

Lines marked **(owner)** were decided by the owner. Lines marked **(call)** are the
author's proposals; the owner can change them at review.

## 1. Intent

VR180 files differ in what they need to be watchable: how far to zoom out before close
subjects fuse, how many degrees the file covers, which eye is on which side. Today those are
set again on every run, by flags and key presses. The owner wants to open a file he has
watched before and get the same picture back, and have a file he has never opened start from
his normal defaults.

Hardware testing on the Viture Pro XR also showed that the convergence keys do not do what
the README says they do, so this feature settles what happens to them.

## 2. Goals

- Remember, per video file: zoom, layout, swap-eyes, content FOV, convergence, projection.
- Restore them when the same file is opened again, including after it was renamed or moved.
- A file with no record starts from the device and ini defaults, exactly as today. **(owner)**
- Save automatically when the player quits. **(owner)**
- Demote convergence: remove its keys, keep the flag.
- Reword the aspect warning so an ordinary flat video is named as the likely cause.

## 3. Non-goals

- A flat (rectilinear) projection with head tracking and zoom. The owner wants it, as a
  separate feature next to format auto-detection. **(owner)** This feature only reserves the
  `proj` field for it.
- Format auto-detection from metadata or file names.
- Remembering the playback position.
- Remembering filter, dither, FBO format, pose or output. Those belong to the machine and the
  glasses, not the file.
- A different saved zoom per glasses model. One zoom per file. **(call)** The two built-in
  devices differ by 2 degrees of diagonal FOV.
- Listing, browsing or garbage-collecting records. Records for deleted videos stay until
  removed by hand.
- A runtime "reset this file" key. `--forget` covers it.

## 4. Anti-goals

- A missing, unreadable, corrupt or newer-format record must never stop a file from playing.
- No file or hashing I/O inside the render loop. All of it happens before the loop starts and
  after it ends.
- Running the test suite must never read or write the developer's real state directory.

## 5. Behaviour

### 5.1 File identity **(owner: hash, not path)**

The key is `<size>-<hash>`:

- `size`: the file size in bytes, decimal.
- `hash`: 64-bit FNV-1a, 16 lowercase hex digits, over the first `min(size, 65536)` bytes
  followed by the last `min(size, 65536)` bytes (the two ranges may overlap). Implemented
  in-tree; no new dependency. **(call)**

Example: `8123456789-9f3c0a1b22d4e6f7`.

If the argument is not a regular file that can be opened and read (a URL, a pipe, a missing
file), memory is off for that run: nothing is read or written, and nothing is printed about
it. mpv still gets the argument unchanged and reports its own errors.

Consequences the owner accepted: a rename or move keeps the settings; a re-encode or a trim
is a new file.

### 5.2 Where records live

One file per video **(call; the owner agreed the shared parts but did not choose between one
file per video and one combined file)**:

```
$XDG_STATE_HOME/xrdome/files/<size>-<hash>.ini
```

`$XDG_STATE_HOME` is used when it is an absolute path; otherwise `$HOME/.local/state`. This
is the same rule `xrd_config_default_path` uses for `XDG_CONFIG_HOME`. If neither is usable,
memory is off for the run. Directories are created on first save with mode 0700.

### 5.3 Record format

```ini
# xrdome per-file settings
version = 1
path = /home/nick/vr/coast-walk.mp4
saved = 2026-11-12
proj = hequirect
zoom = 0.7513
content_fov = 120x90
```

- Flat `key = value` lines, `#` comments, no sections.
- `version`, `path`, `saved` and `proj` are always written. `path` is the absolute path at
  the time of saving and `saved` is the local date; both are labels for a person reading the
  file (and for `grep -l`), and are never used to find or validate a record.
- `proj` is always `hequirect` for now.
- The settings keys are `zoom` (0.25 to 4), `layout` (`sbs-lr`, `sbs-rl`, `mono`), `swap`
  (`yes`, `no`), `content_fov` (`HxV`, each 10 to 180) and `convergence` (-5 to 5). Each is
  written only if it is "set" for this file (5.5). A key that is absent means "follow the
  defaults".
- Ranges and spellings are the same as the matching command-line flags.

Reading is tolerant, unlike `config.ini`:

| In the record | Result |
|---|---|
| Unknown key | Ignored |
| `version` greater than 1, or `proj` not `hequirect` | Record not used; one `xrdome: note:` line; record left untouched at quit |
| Missing `version`, a malformed line, or a value out of range | Record not used; one `xrdome: warning:` line naming the file and line; record left untouched at quit |
| File cannot be read (other than not existing) | Same as the row above |

"Left untouched at quit" means this run does not save, so a record written by a newer xrdome
or hand-edited into a bad state is never overwritten silently. `--forget` is the way out.

### 5.4 Precedence

Lowest to highest: built-in defaults, built-in device values, `config.ini`, the file's
record, explicit command-line flags.

`xrd_config` gains a record of which of `--zoom`, `--layout`, `--content-fov` and
`--convergence` were given on the command line, so a record value is applied only where the
flag was not given.

### 5.5 What "set" means, and what is saved

A setting is set for a file when any of these is true at quit:

1. it was present in the record loaded at start;
2. its flag was given on the command line this run;
3. its value at quit differs from its value when playback started (a key changed it).

Set settings are written with their value at quit. Settings that are not set are left out, so
they keep following the defaults: if the owner later changes `zoom` in `config.ini`, files
where he never adjusted zoom pick up the new value.

Comparison for rule 3 uses a tolerance (zoom: 0.001), because zooming out and back in by
factors of 1.1 does not return to the exact starting float. Zoom is written with four
decimals.

A flag given once is therefore remembered. This is the only way content FOV is remembered,
since it has no key.

### 5.6 When it saves

Once, after the render loop ends, for any reason: `Q`, window close, end of file, SIGTERM,
the glasses being unplugged, or a playback error. The condition is that at least one video
frame was rendered during the run **(call)**. mpv loads the file asynchronously, so "the
player object exists" is too early: a text file, an audio-only file or a truncated download
given with `--content-fov` would otherwise get a record without ever playing. A run that
never rendered a frame saves nothing.

- The record is written to a temporary file in the same directory and renamed into place.
- If nothing is set and no record existed, no file is created.
- If the record would be identical to what was loaded (ignoring `saved`), nothing is written.
- A failed save prints one `xrdome: warning:` line and does not change the exit status.

### 5.7 Messages

All on stderr. The `settings:` prefix keeps them apart from `xrdome:` warnings, which the
smoke tests count.

- At open, when a record supplied at least one value:
  `settings: restored zoom=0.75 content-fov=120x90`
  Only the values actually applied are listed (not ones a flag overrode).
- At quit, when a record was written:
  `settings: saved zoom=0.68 layout=sbs-rl`
- With `--forget`, when a record was deleted: `settings: forgot <key>`

Nothing is printed for a file with no record.

### 5.8 New flags

- `--no-memory`: do not read or write the record for this run. For timing runs, comparisons
  and tests.
- `--forget`: delete this file's record at startup (including an unreadable or newer-format
  one), then continue from the defaults. The run saves at quit as usual. With `--no-memory`
  as well, the record is still deleted, and then nothing is read or written. **(call)**

Both are command-line only.

### 5.9 Convergence: demoted

Finding (Viture Pro XR, 2026-11-12): zooming out is what makes close subjects fusable. Up to
2 degrees of convergence made no significant difference, and 5 degrees put the scene out of
focus until zoomed far out.

- Remove the `,` `.` and `/` keys, the `xrd_controls_convergence_inc`, `_dec` and `_reset`
  functions, `convergence_start`, and their unit tests.
- Keep `--convergence DEG`, the renderer support, and the existing smoke case. It is saved
  per file like the other settings.
- Not removed outright, because the finding is one viewer on one pair of glasses and the
  XREAL Air 2 Pro has not been tested (plan Task 10).
- Rewrite the README's "Viewing comfort" section: zoom out (`[`) for close subjects is the
  advice; `--convergence` is described as an experimental flag, with the finding stated.

### 5.10 Aspect warning

The existing one-time warning in `player_mpv.c` (frame aspect does not fit the layout and
content FOV) is reworded, with no new detection logic:

```
xrdome: warning: video is 1920x1080, which is not the aspect of a side-by-side video covering
180x180 degrees. If this is an ordinary flat video, xrdome has no flat projection yet;
--layout mono --content-fov 60x34 shows it as an approximate curved screen. If it is VR
video covering less than 180 degrees, set --content-fov. Playing it as given.
```

It is printed as one physical line (it is wrapped above only for this document), because
the smoke tests count lines that start with `xrdome:`. A flat 3D movie in half-SBS has the same frame
size as a flat 2D one, so the message does not try to tell them apart.

## 6. Approaches considered

| Approach | Trade-off | Outcome |
|---|---|---|
| One record file per video | Atomic by rename, `--forget` is an unlink, no locking; many tiny files | Chosen **(call)** |
| One combined `files.ini` with a section per video | One place to read; rewritten on every quit, needs locking and a second sectioned parser | Not chosen; the owner may switch to it at review |
| SQLite | A new dependency for a few hundred small records | Rejected |
| Extended attributes on the video | Follow renames for free; lost on exFAT/NFS copies, need write access to the media | Rejected |
| mpv watch-later | Stores mpv options only | Rejected |
| Key by absolute path, or by basename | Simpler; lost on rename or move, or collides on generic names | Rejected by the owner in favour of the hash |

## 7. Structure

- New module `src/store/filestate.c` and `.h` (prefix `xrd_filestate_`), added to
  `core_sources`, with `tests/test_filestate.c`. No SDL, GL or mpv includes. It holds:
  key computation from a path; state-directory path; parse and serialise; load, save
  (temp file and rename) and delete; and two pure functions, one that applies a record to
  the starting settings given the explicit-flag set, and one that builds the record to save
  from the loaded record, the explicit-flag set, the starting values and the values at quit.
- `src/config.c`, `config.h`: the explicit-flag record, `--no-memory`, `--forget`.
- `src/app/controls.c`, `controls.h`: convergence key functions removed.
- `src/main.c`: compute the key and load the record after `xrd_config_load` and before the
  player opens; apply it; note the starting values; save at the common exit path once the
  file has opened. `xrd_player_set_content` and `xrd_renderer_set_content_fov` receive the
  restored layout and content FOV.
- `src/video/player_mpv.c`: warning text.

Language, build and test framework are unchanged (C11, meson, cmocka). No new dependencies
and no new Gentoo packages.

## 8. Testing

Unit tests (`meson test`, no hardware):

- FNV-1a against published test vectors; key for an empty file, a file under 64 KiB, one
  between 64 and 128 KiB (overlapping ranges) and a larger one; the key is unchanged by a
  rename and changed by a one-byte edit in the first or last 64 KiB.
- Parse and serialise round trip; absent keys stay absent; every row of the table in 5.3.
- Apply: record under flags, record over ini, per setting.
- Build-record: each of the three "set" rules alone; zoom out then back in is not a change;
  nothing set and no prior record yields no record.
- Save into a temporary state directory: file appears, no temp file is left, identical
  content is not rewritten; delete removes it.
- State path selection for `XDG_STATE_HOME` set, unset, empty and relative.
- Config: the explicit-flag record for each of the four flags; `--no-memory`; `--forget`.
- Controls: convergence key tests removed; the rest unchanged.

Smoke tests (Xvfb):

- Both smoke scripts export `XDG_STATE_HOME` to a directory under their work directory, and
  the existing cases must not affect one another through saved records (they replay one
  pattern file with different flags). How that is achieved is left to the builder.
- New cases: a run with `--zoom` and `--content-fov 90x90` writes a record and prints
  `settings: saved`; a second run with no flags prints `settings: restored` and its dumped
  frame passes the existing 90x90 frame check; after `mv` to a new name it still restores;
  `--no-memory` does not restore; `--forget` prints `settings: forgot` and the frame is back
  to the 180x180 result; a corrupt record prints one warning, plays, and is not overwritten.
- A file that never renders a video frame (the existing audio-only case) leaves no record,
  even with `--content-fov` given.
- The flat-video wording: a 16:9 pattern prints the new warning once.

Hardware check for the owner (Viture Pro XR):

```sh
./build/xrdome clip.mp4          # press [ a few times, then Q
cat ~/.local/state/xrdome/files/*.ini
./build/xrdome clip.mp4          # expect "settings: restored zoom=..."; picture as left
mv clip.mp4 renamed.mp4 && ./build/xrdome renamed.mp4   # still restored
```

Timing must not regress: the late-frame share for the same file with and without
`--no-memory` should match the current 0.6 percent within run-to-run noise.

## 9. Documentation

- `README.md`: keys table (convergence keys removed), flags (`--no-memory`, `--forget`), a
  new "Per-file settings" section (what is remembered, where, how to inspect, edit and
  forget), the rewritten "Viewing comfort" section, and the out-of-date status paragraph at
  the top.
- `docs/STATUS.md`: this feature, the convergence decision, and "flat projection with head
  tracking and zoom" added to the candidate features next to format auto-detection.

## 10. Left to the builder

- Function names and signatures inside `src/store/`.
- Whether `xrd_controls` keeps `convergence_deg` or main passes the configured value to the
  renderer directly.
- Exact value formatting beyond what 5.3 and 5.5 state, and the order of keys.
- How non-printable characters in `path` are written.
- The temporary file's name.
- How the smoke scripts keep existing cases independent.

## 11. Not verified

- Nothing in this spec was built or run; it is based on reading the code at commit `008fd01`.
- The convergence finding comes from the owner's testing on the Viture Pro XR only.
- Whether `$XDG_STATE_HOME` is set in the owner's MATE session. The fallback covers it either
  way.
