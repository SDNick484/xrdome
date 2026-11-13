#!/bin/sh
# Desktop smoke test for xrdome under Xvfb (software GL, no glasses).
# usage: smoke_player.sh XRDOME MAKE_PATTERN
# Exits 77 (meson: skipped) when xvfb-run or ffmpeg is missing.
set -u

xrdome=$1
make_pattern=$2

for tool in xvfb-run ffmpeg python3; do
    command -v "$tool" >/dev/null 2>&1 || { echo "skip: $tool not found"; exit 77; }
done

work=$(mktemp -d "${TMPDIR:-/tmp}/xrdome-smoke.XXXXXX") || exit 1
trap 'rm -rf "$work"' EXIT
export LIBGL_ALWAYS_SOFTWARE=1
export XDG_STATE_HOME="$work/state"

# xvfb-run merges the program's stderr into its stdout, so callers capture both.
# Returns the program's own exit status: xvfb-run sometimes fails with
# "problem while cleaning up temporary directory" (status 5) after the
# program has already exited.
xvfb() {
    _xsf=$(mktemp "$work/status.XXXXXX") || return 99
    XRD_STATUS=$_xsf xvfb-run -a -s "-screen 0 1920x1080x24" \
        sh -c '"$@"; echo $? >"$XRD_STATUS"' sh "$@"
    _xst=$?
    [ -s "$_xsf" ] && read -r _xst <"$_xsf"
    rm -f "$_xsf"
    return "$_xst"
}
fail=0
check() { # check DESCRIPTION CONDITION-EXIT-STATUS
    if [ "$2" -eq 0 ]; then echo "ok:   $1"; else echo "FAIL: $1"; fail=1; fi
}

# check_frame PPM: the red/blue/orientation assertions on a dumped frame.
check_frame() {
python3 -I - "$1" <<'PY'
import sys

with open(sys.argv[1], "rb") as f:
    data = f.read()
magic, size, maxval, px = data.split(b"\n", 3)
w, h = map(int, size.split())
assert magic == b"P6" and maxval == b"255" and len(px) == w * h * 3, "bad PPM"
print(f"frame {w}x{h}")

def frac(x0, x1, y0, y1, pred):
    """Fraction of pixels in [x0,x1) x [y0,y1) (top-down rows) matching pred."""
    n = hit = 0
    for y in range(y0, y1):
        row = y * w * 3
        for x in range(x0, x1):
            r, g, b = px[row + 3 * x: row + 3 * x + 3]
            n += 1
            hit += pred(r, g, b)
    return hit / n

red = lambda r, g, b: r > 200 and g < 100 and b < 100
blue = lambda r, g, b: b > 200 and r < 100 and g < 100
yellow = lambda r, g, b: r > 200 and g > 200 and b < 100

# Eye halves; regions in fractions of the eye: centre (around the horizon),
# upper (where the yellow box above the centre lands) and the mirrored lower band.
eye = w // 2
def region(e, fx0, fx1, fy0, fy1):
    return (e * eye + int(fx0 * eye), e * eye + int(fx1 * eye), int(fy0 * h), int(fy1 * h))

ok = True
def expect(name, value, cond):
    global ok
    good = cond(value)
    ok &= good
    print(f"  {'ok  ' if good else 'FAIL'} {name}: {value:.3f}")

expect("left centre red", frac(*region(0, .25, .75, .42, .58), red), lambda v: v > .8)
expect("right centre blue", frac(*region(1, .25, .75, .42, .58), blue), lambda v: v > .8)
for e, name in ((0, "left"), (1, "right")):
    expect(f"{name} upper band yellow", frac(*region(e, .1, .9, .15, .30), yellow), lambda v: v > .8)
    expect(f"{name} lower band not yellow", frac(*region(e, .1, .9, .70, .85), yellow), lambda v: v < .05)
sys.exit(0 if ok else 1)
PY
}

# 1. Play a 2 s pattern, dump the 30th presented frame.
"$make_pattern" 2 "$work/pattern.mp4" libx264 || { echo "FAIL: make_pattern.sh"; exit 1; }
xvfb "$xrdome" --no-memory --windowed --pose null --frames 30 --dump-frame "$work/out.ppm" \
     "$work/pattern.mp4" >"$work/play.err" 2>&1
st=$?
echo "--- stderr:"; cat "$work/play.err"; echo "---"
check "playback exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
grep -q '^hwdec: ' "$work/play.err"; check "stderr reports hwdec" $?
hwdec=$(sed -n 's/^hwdec: //p' "$work/play.err")
# Xvfb with software GL normally has no VA-API interop, so hwdec is "no" and
# the player must warn once; that warning is the only xrdome line allowed.
if [ "$hwdec" = vaapi ]; then
    ! grep -q '^xrdome:' "$work/play.err"; check "no xrdome warnings or errors" $?
else
    [ "$(grep -c "^xrdome: warning: hardware decoding is '$hwdec', not vaapi" "$work/play.err")" -eq 1 ]
    check "one hwdec warning (hwdec: $hwdec)" $?
    ! grep '^xrdome:' "$work/play.err" | grep -qv 'not vaapi'; check "no other xrdome warnings or errors" $?
fi
grep -q '^timing: [0-9]* frames, [0-9]* missed vsyncs' "$work/play.err"; check "exit summary reports missed vsyncs" $?
# mpv's own warnings are forwarded as "mpv: [<prefix>] <text>" (headless runs
# have no VA-API or audio device, so some are expected); they are allowed, and
# the only other mpv line is xrdome's own "mpv: profile=" report.
! grep '^mpv: ' "$work/play.err" | grep -qv '^mpv: \[[^]]*\] .\|^mpv: profile='
check "mpv lines are 'mpv: [prefix] text' or 'mpv: profile='" $?

# The default video texture is 10-bit and the eye pass dithers.
grep -q '^video texture: rgb10_a2 dither=on$' "$work/play.err"
check "stderr reports video texture rgb10_a2 dither=on" $?

check_frame "$work/out.ppm"
check "dumped frame: colours and orientation" $?

# 1-8bit. --fbo-format rgba8 --dither off: the previous 8-bit output.
xvfb "$xrdome" --no-memory --windowed --pose null --fbo-format rgba8 --dither off --frames 30 \
     --dump-frame "$work/out8.ppm" "$work/pattern.mp4" >"$work/rgba8.err" 2>&1
st=$?
cat "$work/rgba8.err"
check "--fbo-format rgba8 --dither off exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
grep -q '^video texture: rgba8 dither=off$' "$work/rgba8.err"
check "--fbo-format rgba8 --dither off reported" $?
check_frame "$work/out8.ppm"
check "--fbo-format rgba8 --dither off: colours and orientation" $?

# 1a. --content-fov 90x90 on the same 180-degree pattern: the content fills twice
# the angle, so the yellow box (1.8 to 4.4 degrees up) now sits lower in the
# view, and the horizon centre is still red / blue. No aspect warning (4096x2048
# is two square eyes, which is what 90x90 expects too).
xvfb "$xrdome" --no-memory --windowed --pose null --content-fov 90x90 --frames 30 \
     --dump-frame "$work/out90.ppm" "$work/pattern.mp4" >"$work/fov90.err" 2>&1
st=$?
cat "$work/fov90.err"
check "--content-fov 90x90 playback exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
! grep -v '^mpv: \[' "$work/fov90.err" | grep -q 'aspect'; check "--content-fov 90x90: no aspect warning" $?
# check_narrow FULL.ppm NARROW.ppm: the 90x90 picture against the 180x180 one.
check_narrow() {
python3 -I - "$1" "$2" <<'PY'
import sys

def load(path):
    with open(path, "rb") as f:
        magic, size, maxval, px = f.read().split(b"\n", 3)
    w, h = map(int, size.split())
    assert magic == b"P6" and len(px) == w * h * 3, "bad PPM"
    return w, h, px

def frac(img, e, fx0, fx1, fy0, fy1, pred):
    w, h, px = img
    eye = w // 2
    n = hit = 0
    for y in range(int(fy0 * h), int(fy1 * h)):
        for x in range(e * eye + int(fx0 * eye), e * eye + int(fx1 * eye)):
            r, g, b = px[(y * w + x) * 3:(y * w + x) * 3 + 3]
            n += 1
            hit += pred(r, g, b)
    return hit / n

red = lambda r, g, b: r > 200 and g < 100 and b < 100
blue = lambda r, g, b: b > 200 and r < 100 and g < 100
yellow = lambda r, g, b: r > 200 and g > 200 and b < 100

full, narrow = load(sys.argv[1]), load(sys.argv[2])
assert full[:2] == narrow[:2], "frame sizes differ"
ok = True
def expect(name, value, cond):
    global ok
    good = cond(value)
    ok &= good
    print(f"  {'ok  ' if good else 'FAIL'} {name}: {value:.3f}")

expect("90x90 left centre red", frac(narrow, 0, .25, .75, .42, .58, red), lambda v: v > .8)
expect("90x90 right centre blue", frac(narrow, 1, .25, .75, .42, .58, blue), lambda v: v > .8)
for e, name in ((0, "left"), (1, "right")):
    # rows 36-40 percent down: 2.2 to 3.1 degrees up, inside the box only at 90x90
    expect(f"{name} box at 90x90", frac(narrow, e, .35, .65, .36, .40, yellow), lambda v: v > .8)
    expect(f"{name} no box there at 180x180", frac(full, e, .35, .65, .36, .40, yellow), lambda v: v < .05)
sys.exit(0 if ok else 1)
PY
}
check_narrow "$work/out.ppm" "$work/out90.ppm"
check "--content-fov 90x90 dump: centre colours, content larger than at 180x180" $?

# 1a1. --convergence 2 --filter bilinear: each eye turns 1 degree inwards and
# samples bilinearly; the centre, the box band and the orientation still hold.
xvfb "$xrdome" --no-memory --windowed --pose null --convergence 2 --filter bilinear --frames 30 \
     --dump-frame "$work/outconv.ppm" "$work/pattern.mp4" >"$work/conv.err" 2>&1
st=$?
cat "$work/conv.err"
check "--convergence 2 --filter bilinear exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
check_frame "$work/outconv.ppm"
check "--convergence 2 --filter bilinear: colours and orientation" $?

# 1a2. mpv options: --mpv-opt that mpv rejects exits 1 naming the key; the
# default (fast) profile is reported; a bogus profile fails naming it.
grep -q '^mpv: profile=fast$' "$work/play.err"; check "stderr reports mpv profile fast" $?
xvfb "$xrdome" --no-memory --windowed --pose null --frames 5 --mpv-opt bogus-option-xyz=1 \
     "$work/pattern.mp4" >"$work/badopt.err" 2>&1
st=$?
cat "$work/badopt.err"
check "bad --mpv-opt exits 1 (got $st)" "$([ "$st" -eq 1 ]; echo $?)"
grep -q '^xrdome: .*bogus-option-xyz' "$work/badopt.err"; check "bad --mpv-opt names the key" $?

# 1a3. --mpv-profile default: no profile, same picture.
xvfb "$xrdome" --no-memory --windowed --pose null --frames 30 --mpv-profile default \
     --dump-frame "$work/outdef.ppm" "$work/pattern.mp4" >"$work/defprof.err" 2>&1
st=$?
cat "$work/defprof.err"
check "--mpv-profile default exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
grep -q '^mpv: profile=default$' "$work/defprof.err"; check "--mpv-profile default reported" $?
check_frame "$work/outdef.ppm"
check "--mpv-profile default: colours and orientation" $?

# 1b. --require-hwdec: exit 1 without VA-API, play normally with it.
xvfb "$xrdome" --no-memory --windowed --pose null --require-hwdec --frames 5 "$work/pattern.mp4" \
     >"$work/reqhw.err" 2>&1
st=$?
cat "$work/reqhw.err"
if [ "$hwdec" = vaapi ]; then
    check "--require-hwdec with vaapi exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
else
    check "--require-hwdec without vaapi exits 1 (got $st)" "$([ "$st" -eq 1 ]; echo $?)"
    grep -q "^xrdome: hardware decoding is '$hwdec', not vaapi (--require-hwdec)" "$work/reqhw.err"
    check "--require-hwdec message" $?
fi

# 1c. --frames N not reached: SIGTERM (SDL turns it into SDL_QUIT) after 3 s,
# long before 100000 frames, must exit 1.
xvfb sh -c '"$1" --no-memory --windowed --pose null --frames 100000 "$2" & pid=$!
            sleep 3; kill -TERM "$pid"; wait "$pid"' sh "$xrdome" "$work/pattern.mp4" \
     >"$work/early.err" 2>&1
st=$?
cat "$work/early.err"
check "--frames not reached exits 1 (got $st)" "$([ "$st" -eq 1 ]; echo $?)"
grep -q "^xrdome: --frames 100000: exited after [0-9]* presented frames" "$work/early.err"
check "--frames not reached message" $?

# 1d. Plain playback to the end of the file exits 0 (no --frames).
xvfb "$xrdome" --no-memory --windowed --pose null "$work/pattern.mp4" >"$work/eof.err" 2>&1
st=$?
cat "$work/eof.err"
check "plays to end of file and exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"

# 1e. A file without a video track: exit 1 at load, before audio output
# matters (the sandbox has no audio device).
ffmpeg -nostdin -hide_banner -loglevel error -y -f lavfi -i anullsrc -t 2 -vn \
       "$work/audio.m4a" || { echo "FAIL: make audio.m4a"; exit 1; }
xvfb "$xrdome" --no-memory --windowed --pose null "$work/audio.m4a" >"$work/novideo.err" 2>&1
st=$?
cat "$work/novideo.err"
check "audio-only file exits 1 (got $st)" "$([ "$st" -eq 1 ]; echo $?)"
grep -q "^xrdome: '$work/audio.m4a' has no video track" "$work/novideo.err"
check "audio-only file: no video track message" $?

# 2. Missing file: exit 1 with mpv's error string.
xvfb "$xrdome" --no-memory --windowed --pose null /nonexistent/clip.mp4 >"$work/missing.err" 2>&1
st=$?
cat "$work/missing.err"
check "missing file exits 1 (got $st)" "$([ "$st" -eq 1 ]; echo $?)"
grep -q "^xrdome: cannot play '/nonexistent/clip.mp4': loading failed" "$work/missing.err"; check "missing file names mpv error" $?

# 3. No 3840x1080 output (Xvfb screen is 1920x1080): exit 1, tells the user to switch to SBS.
xvfb "$xrdome" --no-memory --pose null "$work/pattern.mp4" >"$work/nosbs.err" 2>&1
st=$?
cat "$work/nosbs.err"
check "non-SBS output exits 1 (got $st)" "$([ "$st" -eq 1 ]; echo $?)"
grep -q "^xrdome: .*switch the glasses to SBS" "$work/nosbs.err"; check "non-SBS message" $?

# ---- per-file settings memory (M1 to M10) ----
cp "$work/pattern.mp4" "$work/mem.mp4"
rec() { ls "$XDG_STATE_HOME"/xrdome/files/*.ini 2>/dev/null; }
nrec() { rec | wc -l; }

# M1. no flags, no keys: nothing is saved and no record appears.
xvfb "$xrdome" --windowed --pose null --frames 30 "$work/mem.mp4" >"$work/m1.err" 2>&1
st=$?
check "M1 exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
! grep -q '^settings:' "$work/m1.err"; check "M1 no settings line" $?
[ -z "$(rec)" ]; check "M1 no record" $?

# M2. flags are saved.
xvfb "$xrdome" --windowed --pose null --zoom 1.0 --content-fov 90x90 --frames 30 \
     "$work/mem.mp4" >"$work/m2.err" 2>&1
st=$?
check "M2 exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
[ "$(grep -c '^settings:' "$work/m2.err")" -eq 1 ] &&
    grep -qx 'settings: saved zoom=1.00 content-fov=90x90' "$work/m2.err"; check "M2 saved line" $?
[ "$(nrec)" -eq 1 ]; check "M2 exactly one record" $?
grep -q '^content_fov = 90x90' "$(rec)" && grep -q "^path = $work/mem.mp4\$" "$(rec)"
check "M2 record has content_fov and path" $?

# M3. restored with no flags.
xvfb "$xrdome" --windowed --pose null --frames 30 --dump-frame "$work/mem90.ppm" \
     "$work/mem.mp4" >"$work/m3.err" 2>&1
st=$?
check "M3 exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
grep -qx 'settings: restored zoom=1.00 content-fov=90x90' "$work/m3.err"; check "M3 restored line" $?
! grep -q '^settings: saved' "$work/m3.err"; check "M3 nothing saved" $?
check_narrow "$work/out.ppm" "$work/mem90.ppm"
check "M3 dumped frame is the 90x90 picture" $?

# M4. survives a rename (the record is keyed by content).
mv "$work/mem.mp4" "$work/moved.mp4"
xvfb "$xrdome" --windowed --pose null --frames 30 --dump-frame "$work/mov90.ppm" \
     "$work/moved.mp4" >"$work/m4.err" 2>&1
st=$?
check "M4 exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
grep -qx 'settings: restored zoom=1.00 content-fov=90x90' "$work/m4.err"; check "M4 restored line" $?
[ "$(nrec)" -eq 1 ]; check "M4 still one record" $?
check_narrow "$work/out.ppm" "$work/mov90.ppm"
check "M4 dumped frame is the 90x90 picture" $?

# M5. a flag wins and is saved.
xvfb "$xrdome" --windowed --pose null --content-fov 180x180 --frames 30 \
     --dump-frame "$work/mem180.ppm" "$work/moved.mp4" >"$work/m5.err" 2>&1
st=$?
check "M5 exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
grep -qx 'settings: restored zoom=1.00' "$work/m5.err"; check "M5 restored line names only zoom" $?
grep -qx 'settings: saved zoom=1.00 content-fov=180x180' "$work/m5.err"; check "M5 saved line" $?
check_frame "$work/mem180.ppm"
check "M5 dumped frame is the 180x180 picture" $?

# M6. --no-memory neither reads nor writes.
xvfb "$xrdome" --windowed --pose null --zoom 1.0 --content-fov 90x90 --frames 30 \
     "$work/moved.mp4" >"$work/m6a.err" 2>&1
xvfb "$xrdome" --no-memory --windowed --pose null --frames 30 --dump-frame "$work/nm.ppm" \
     "$work/moved.mp4" >"$work/m6.err" 2>&1
st=$?
check "M6 exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
! grep -q '^settings:' "$work/m6.err"; check "M6 no settings line" $?
check_frame "$work/nm.ppm"
check "M6 dumped frame is the 180x180 picture" $?

# M7. a corrupt record is reported, kept, and not overwritten.
echo "zoom = 99" >>"$(rec)"
xvfb "$xrdome" --windowed --pose null --frames 30 "$work/moved.mp4" >"$work/m7.err" 2>&1
st=$?
check "M7 exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
[ "$(grep -c '^xrdome: warning: settings record' "$work/m7.err")" -eq 1 ]; check "M7 one warning" $?
! grep -q '^settings: saved' "$work/m7.err"; check "M7 nothing saved" $?
grep -qx 'zoom = 99' "$(rec)"; check "M7 record untouched" $?

# M8. --forget deletes the record and plays from the defaults.
xvfb "$xrdome" --forget --windowed --pose null --frames 30 --dump-frame "$work/fg.ppm" \
     "$work/moved.mp4" >"$work/m8.err" 2>&1
st=$?
check "M8 exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
grep -q '^settings: forgot ' "$work/m8.err"; check "M8 forgot line" $?
check_frame "$work/fg.ppm"
check "M8 dumped frame is the 180x180 picture" $?
[ -z "$(rec)" ]; check "M8 no record afterwards" $?

# M9. a run that never renders a frame saves nothing.
xvfb "$xrdome" --windowed --pose null --content-fov 120x90 "$work/audio.m4a" >"$work/m9.err" 2>&1
st=$?
check "M9 audio-only exits 1 (got $st)" "$([ "$st" -eq 1 ]; echo $?)"
[ -z "$(rec)" ]; check "M9 no record" $?

# M10. an unwritable state directory warns once and keeps the exit status.
XDG_STATE_HOME="$work/pattern.mp4" xvfb "$xrdome" --windowed --pose null --zoom 0.5 --frames 30 \
     "$work/moved.mp4" >"$work/m10.err" 2>&1
st=$?
check "M10 exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
[ "$(grep -c '^xrdome: warning: cannot save settings' "$work/m10.err")" -eq 1 ]; check "M10 one warning" $?


exit $fail
