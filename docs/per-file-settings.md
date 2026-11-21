# Per-file settings

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

## Resume (where you left off)

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
