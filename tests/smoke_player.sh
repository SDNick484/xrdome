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

# 1. Play a 2 s pattern, dump the 30th presented frame.
"$make_pattern" 2 "$work/pattern.mp4" libx264 || { echo "FAIL: make_pattern.sh"; exit 1; }
xvfb "$xrdome" --windowed --pose null --frames 30 --dump-frame "$work/out.ppm" \
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

python3 -I - "$work/out.ppm" <<'PY'
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
check "dumped frame: colours and orientation" $?

# 1b. --require-hwdec: exit 1 without VA-API, play normally with it.
xvfb "$xrdome" --windowed --pose null --require-hwdec --frames 5 "$work/pattern.mp4" \
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
xvfb sh -c '"$1" --windowed --pose null --frames 100000 "$2" & pid=$!
            sleep 3; kill -TERM "$pid"; wait "$pid"' sh "$xrdome" "$work/pattern.mp4" \
     >"$work/early.err" 2>&1
st=$?
cat "$work/early.err"
check "--frames not reached exits 1 (got $st)" "$([ "$st" -eq 1 ]; echo $?)"
grep -q "^xrdome: --frames 100000: exited after [0-9]* presented frames" "$work/early.err"
check "--frames not reached message" $?

# 1d. Plain playback to the end of the file exits 0 (no --frames).
xvfb "$xrdome" --windowed --pose null "$work/pattern.mp4" >"$work/eof.err" 2>&1
st=$?
cat "$work/eof.err"
check "plays to end of file and exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"

# 1e. A file without a video track: exit 1 at load, before audio output
# matters (the sandbox has no audio device).
ffmpeg -nostdin -hide_banner -loglevel error -y -f lavfi -i anullsrc -t 2 -vn \
       "$work/audio.m4a" || { echo "FAIL: make audio.m4a"; exit 1; }
xvfb "$xrdome" --windowed --pose null "$work/audio.m4a" >"$work/novideo.err" 2>&1
st=$?
cat "$work/novideo.err"
check "audio-only file exits 1 (got $st)" "$([ "$st" -eq 1 ]; echo $?)"
grep -q "^xrdome: '$work/audio.m4a' has no video track" "$work/novideo.err"
check "audio-only file: no video track message" $?

# 2. Missing file: exit 1 with mpv's error string.
xvfb "$xrdome" --windowed --pose null /nonexistent/clip.mp4 >"$work/missing.err" 2>&1
st=$?
cat "$work/missing.err"
check "missing file exits 1 (got $st)" "$([ "$st" -eq 1 ]; echo $?)"
grep -q "/nonexistent/clip.mp4': loading failed" "$work/missing.err"; check "missing file names mpv error" $?

# 3. No 3840x1080 output (Xvfb screen is 1920x1080): exit 1, tells the user to switch to SBS.
xvfb "$xrdome" --pose null "$work/pattern.mp4" >"$work/nosbs.err" 2>&1
st=$?
cat "$work/nosbs.err"
check "non-SBS output exits 1 (got $st)" "$([ "$st" -eq 1 ]; echo $?)"
grep -q "switch the glasses to SBS" "$work/nosbs.err"; check "non-SBS message" $?

exit $fail
