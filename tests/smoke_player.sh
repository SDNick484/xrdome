#!/bin/sh
# Desktop smoke test for xrdome under Xvfb (software GL, no glasses).
# usage: smoke_player.sh XRDOME MAKE_PATTERN
# Exits 77 (meson: skipped) when xvfb-run or ffmpeg is missing.
set -u

xrdome=$1
make_pattern=$2
case $xrdome in /*) ;; *) xrdome=$(pwd)/$xrdome ;; esac # F8 runs from another directory

for tool in xvfb-run ffmpeg python3; do
    command -v "$tool" >/dev/null 2>&1 || { echo "skip: $tool not found"; exit 77; }
done

work=$(mktemp -d "${TMPDIR:-/tmp}/xrdome-smoke.XXXXXX") || exit 1
work=$(cd "$work" && pwd) # absolute: a relative XDG_STATE_HOME is ignored by xrdome
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

# The default video texture is 10-bit, the coded frame size, and the eye pass
# dithers; no mipmaps unless asked for.
grep -q '^video texture: rgb10_a2 4096x2048 dither=on mipmap=off$' "$work/play.err"
check "stderr reports video texture rgb10_a2 4096x2048 dither=on mipmap=off" $?
# A 2:1 side-by-side file with no flags is VR180 by its frame shape, as before detection.
grep -qx 'detect: proj=hequirect (frame shape) layout=sbs-lr (frame shape)' "$work/play.err"
check "2:1 pattern detected as hequirect sbs-lr by frame shape" $?

check_frame "$work/out.ppm"
check "dumped frame: colours and orientation" $?

# 1-8bit. --fbo-format rgba8 --dither off: the previous 8-bit output.
xvfb "$xrdome" --no-memory --windowed --pose null --fbo-format rgba8 --dither off --frames 30 \
     --dump-frame "$work/out8.ppm" "$work/pattern.mp4" >"$work/rgba8.err" 2>&1
st=$?
cat "$work/rgba8.err"
check "--fbo-format rgba8 --dither off exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
grep -q '^video texture: rgba8 4096x2048 dither=off mipmap=off$' "$work/rgba8.err"
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

# 4. An ordinary flat video played as VR180 (--proj hequirect) gets one warning
# that names it and points at P and --proj flat (spec 5.12).
ffmpeg -v error -f lavfi -i testsrc=s=1920x1080:r=30 -t 2 -c:v libx264 -preset ultrafast \
    -pix_fmt yuv420p "$work/flat.mp4"
xvfb "$xrdome" --windowed --pose null --no-memory --proj hequirect --frames 10 "$work/flat.mp4" \
     >"$work/flat.err" 2>&1
st=$?
check "flat video as hequirect exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
[ "$(grep -cxF 'xrdome: warning: video is 1920x1080, which is not the aspect of a side-by-side VR video covering 180x180 degrees. If it is an ordinary flat video, press P or use --proj flat. If it is VR video covering less than 180 degrees, set --content-fov. Playing it as given.' "$work/flat.err")" -eq 1 ]
check "flat video as hequirect: the aspect warning, once" $?

# 5. Coded-size texture (spec 5.10). A stereo-tagged Matroska file declares
# one eye's display size, so mpv's own display size for this 1920x1080 frame
# is 1920x2160. The texture is the coded 1920x1080 and the picture fills it.
# It is a VR180 pattern with 16:9 eyes, so it is played with --proj hequirect;
# its layout comes from the stereo tag.
"$make_pattern" 2 "$work/stereo.mkv" libx264 960x1080 || { echo "FAIL: make stereo.mkv"; exit 1; }
xvfb "$xrdome" --no-memory --windowed --pose null --proj hequirect --frames 30 \
     --dump-frame "$work/stereo.ppm" "$work/stereo.mkv" >"$work/stereo.err" 2>&1
st=$?
cat "$work/stereo.err"
check "stereo-tagged mkv exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
grep -qx 'video texture: rgb10_a2 1920x1080 dither=on mipmap=off' "$work/stereo.err"
check "stereo-tagged mkv: video texture rgb10_a2 1920x1080" $?
grep -qx 'detect: proj=hequirect (flag) layout=sbs-lr (metadata)' "$work/stereo.err"
check "stereo-tagged mkv: layout=sbs-lr (metadata)" $?
check_frame "$work/stereo.ppm"
check "stereo-tagged mkv: the picture fills the texture (colours and orientation)" $?
# Control: with mpv's container aspect back on, the same file is squeezed into
# the middle of the texture and the frame check fails, so this file does make
# mpv's display size differ from the coded size.
xvfb "$xrdome" --no-memory --windowed --pose null --proj hequirect --frames 30 \
     --dump-frame "$work/stereo_ar.ppm" --mpv-opt video-aspect-override=-1 "$work/stereo.mkv" \
     >"$work/stereo_ar.err" 2>&1
st=$?
check "stereo-tagged mkv with container aspect exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
[ -s "$work/stereo_ar.ppm" ] && ! check_frame "$work/stereo_ar.ppm" >/dev/null
check "control: with mpv's container aspect the stereo mkv frame check fails" $?

# 6. Mipmaps forced on (development flag): every draw samples trilinearly and
# the VR180 pattern still passes the frame check.
xvfb "$xrdome" --no-memory --windowed --pose null --mipmap-force --mipmap-check --frames 30 \
     --dump-frame "$work/mip.ppm" "$work/pattern.mp4" >"$work/mip.err" 2>&1
st=$?
cat "$work/mip.err"
check "--mipmap-force exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
grep -qx 'video texture: rgb10_a2 4096x2048 dither=on mipmap=on' "$work/mip.err"
check "--mipmap-force: video texture reports mipmap=on" $?
grep -qx 'mipmap check: [1-9][0-9]* trilinear draws, texture 4096x2048' "$work/mip.err"
check "--mipmap-force: trilinear draws checked" $?
check_frame "$work/mip.ppm"
check "--mipmap-force: colours and orientation" $?

# 7. Review Focus 5: the video size changes mid-file (1280x720, then
# 1920x1080). --mipmap-check exits 1 before any trilinear draw from a chain
# not regenerated for the frame in the texture; both sizes must be drawn.
for seg in 1:1280x720 2:1920x1080; do
    ffmpeg -nostdin -v error -y -f lavfi -i "testsrc=s=${seg#*:}:r=30" -t 1 -c:v libx264 \
        -preset ultrafast -pix_fmt yuv420p "$work/seg${seg%%:*}.ts" ||
        { echo "FAIL: make seg${seg%%:*}.ts"; exit 1; }
done
printf "file '%s'\nfile '%s'\n" "$work/seg1.ts" "$work/seg2.ts" >"$work/segs.txt"
ffmpeg -nostdin -v error -y -f concat -safe 0 -i "$work/segs.txt" -c copy "$work/sizes.ts" ||
    { echo "FAIL: make sizes.ts"; exit 1; }
xvfb "$xrdome" --no-memory --windowed --pose null --mipmap-force --mipmap-check \
     "$work/sizes.ts" >"$work/sizes.err" 2>&1
st=$?
cat "$work/sizes.err"
check "size change with --mipmap-check exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
grep -qx 'mipmap check: [1-9][0-9]* trilinear draws, texture 1280x720 then 1920x1080' \
    "$work/sizes.err"
check "size change: trilinear draws at 1280x720 then 1920x1080, all checked" $?

# ---- flat projection and format detection (flat projection plan, Task 8) ----

# check_flat MODE PPM [PPM2]: assertions on dumped frames of the flat patterns.
#   fill    the picture fills each eye exactly: the white border on all four
#           edges, and black nowhere (the screen at zoom 1 is the display).
#   corners each eye's corners are black (the screen is smaller than the view)
#   eyes    left viewport red and right blue, with no pixel of the other eye
#   subs    PPM has subtitle pixels in the lower part of each eye that PPM2 lacks
#   nosubs  PPM and PPM2 do not differ in the lower part of each eye
check_flat() {
python3 -I - "$@" <<'PY'
import sys

def load(path):
    with open(path, "rb") as f:
        magic, size, maxval, px = f.read().split(b"\n", 3)
    w, h = map(int, size.split())
    assert magic == b"P6" and maxval == b"255" and len(px) == w * h * 3, "bad PPM"
    return w, h, px

def pix(img, x, y):
    w, _, px = img
    i = (y * w + x) * 3
    return px[i], px[i + 1], px[i + 2]

def frac(img, e, fx0, fx1, fy0, fy1, pred, skip=None):
    w, h, _ = img
    ew = w // 2
    n = hit = 0
    for y in range(int(fy0 * h), max(int(fy1 * h), int(fy0 * h) + 1)):
        for x in range(e * ew + int(fx0 * ew), e * ew + max(int(fx1 * ew), int(fx0 * ew) + 1)):
            if skip and skip((x - e * ew) / ew, y / h):
                continue
            n += 1
            hit += pred(*pix(img, x, y))
    return hit / n if n else 0.0

white = lambda r, g, b: r > 150 and g > 150 and b > 150
black = lambda r, g, b: max(r, g, b) < 40
red = lambda r, g, b: r > 200 and g < 100 and b < 100
blue = lambda r, g, b: b > 200 and r < 100 and g < 100
# The other eye's colour mixed into a pixel: blue raises b above g in the red
# eye, red raises r above g in the blue eye. Blends with the eye's own white,
# yellow and grey marks keep g at least level with b (red eye) or r (blue eye).
blue_in = lambda r, g, b: b > 40 and b > g + 30
red_in = lambda r, g, b: r > 40 and r > g + 30

ok = True
def expect(name, value, good):
    global ok
    ok &= good
    print(f"  {'ok  ' if good else 'FAIL'} {name}: {value:.4f}")

mode, img = sys.argv[1], load(sys.argv[2])
w, h, _ = img
ew = w // 2
eyes = ((0, "left"), (1, "right"))
if mode == "fill":
    centre = lambda fx, fy: .35 <= fx < .65 and .3 <= fy < .7  # the fine-line block
    for e, name in eyes:
        x0, x1 = e * ew, (e + 1) * ew - 1
        col = lambda x: sum(white(*pix(img, x, y)) for y in range(h)) / h
        row = lambda y: sum(white(*pix(img, x, y)) for x in range(x0, x1 + 1)) / ew
        expect(f"{name} left edge white", col(x0), col(x0) > .9)
        expect(f"{name} right edge white", col(x1), col(x1) > .9)
        expect(f"{name} top edge white", row(0), row(0) > .9)
        expect(f"{name} bottom edge white", row(h - 1), row(h - 1) > .9)
        v = frac(img, e, 0, 1, 0, 1, black, centre)
        expect(f"{name} no black outside the line block", v, v == 0)
elif mode == "corners":
    for e, name in eyes:
        for fx, fy in ((0, 0), (.95, 0), (0, .95), (.95, .95)):
            v = frac(img, e, fx, fx + .05, fy, fy + .05, black)
            expect(f"{name} corner {fx:.2f},{fy:.2f} black", v, v > .95)
        v = frac(img, e, .3, .4, .45, .55, black)
        expect(f"{name} picture left of the centre not black", v, v < .05)
elif mode == "eyes":
    for e, name, own, other in ((0, "left", red, blue_in), (1, "right", blue, red_in)):
        v = frac(img, e, 0, 1, 0, 1, own)
        expect(f"{name} viewport mostly its own colour", v, v > .5)
        v = frac(img, e, 0, 1, 0, 1, other)
        expect(f"{name} viewport has none of the other eye", v, v == 0)
elif mode in ("subs", "nosubs"):
    other = load(sys.argv[3])
    assert other[:2] == img[:2], "frame sizes differ"
    differs = lambda x, y: max(abs(a - b) for a, b in zip(pix(img, x, y), pix(other, x, y))) > 48
    for e, name in eyes:
        n = hit = 0
        for y in range(int(.75 * h), int(.97 * h)):
            for x in range(e * ew + 8, (e + 1) * ew - 8):
                n += 1
                hit += differs(x, y)
        v = hit / n
        if mode == "subs":
            expect(f"{name} lower part has subtitle pixels", v, v > .005)
        else:
            expect(f"{name} lower part unchanged by subtitles", v, v < .0005)
else:
    sys.exit(f"check_flat: unknown mode {mode}")
sys.exit(0 if ok else 1)
PY
}

# no_xrdome_lines ERR: no line starts with "xrdome:" except the hwdec warning
# a headless run without VA-API always prints.
no_xrdome_lines() {
    ! grep '^xrdome:' "$1" | grep -qv "^xrdome: warning: hardware decoding is '[^']*', not vaapi"
}

flat() { # flat NAME ARGS...: a --no-memory --mipmap-check flat run, stderr in NAME.err
    _name=$1
    shift
    xvfb "$xrdome" --no-memory --windowed --pose null --mipmap-check "$@" >"$work/$_name.err" 2>&1
    _st=$?
    cat "$work/$_name.err"
    check "$_name exits 0 (got $_st)" "$([ "$_st" -eq 0 ]; echo $?)"
}

"$make_pattern" --kind flat2d 2 "$work/flat2d.mp4" libx264 || { echo "FAIL: make flat2d"; exit 1; }
"$make_pattern" --kind flat-hsbs 2 "$work/clip.HSBS.mp4" libx264 || { echo "FAIL: make flat-hsbs"; exit 1; }
"$make_pattern" --kind flat-tb 2 "$work/flattb.mp4" libx264 || { echo "FAIL: make flat-tb"; exit 1; }

# F1. A 16:9 file with no flags opens flat 2D (spec 5.8); nothing to warn about.
flat f1 --frames 30 "$work/flat2d.mp4"
grep -qx 'detect: proj=flat (frame shape) layout=mono (frame shape)' "$work/f1.err"
check "F1 flat2d detected as flat mono by frame shape" $?
no_xrdome_lines "$work/f1.err"; check "F1 no xrdome lines" $?
# The 1920x1080 picture at flat zoom 1.5 in a 640x360 eye is shrunk 2x: mipmaps.
grep -qx 'video texture: rgb10_a2 1920x1080 dither=on mipmap=on' "$work/f1.err"
check "F1 shrunk picture uses mipmaps" $?
grep -qx 'mipmap check: [1-9][0-9]* trilinear draws, texture 1920x1080' "$work/f1.err"
check "F1 every trilinear draw found its chain" $?

# F2. --zoom 1.0: the 16:9 screen is exactly the 16:9 eye.
flat f2 --frames 30 --zoom 1.0 --dump-frame "$work/f2.ppm" "$work/flat2d.mp4"
no_xrdome_lines "$work/f2.err"; check "F2 no xrdome lines" $?
check_flat fill "$work/f2.ppm"; check "F2 zoom 1.0: border on the eye edges, no black inside" $?

# F3. --zoom 0.5: the screen is half the view, so the corners are black.
flat f3 --frames 30 --zoom 0.5 --dump-frame "$work/f3.ppm" "$work/flat2d.mp4"
no_xrdome_lines "$work/f3.err"; check "F3 no xrdome lines" $?
check_flat corners "$work/f3.ppm"; check "F3 zoom 0.5: corners black" $?

# F4. Half SBS by file name, with an .srt beside it: each eye shows only its
# own half, and no subtitles are drawn in a stereo layout (spec 5.11).
printf '1\n00:00:00,000 --> 00:01:00,000\nSUBTITLE TEST LINE\n' >"$work/clip.HSBS.srt"
flat f4 --frames 30 --zoom 1.0 --dump-frame "$work/f4.ppm" "$work/clip.HSBS.mp4"
grep -qx 'detect: proj=flat (frame shape) layout=sbs-lr (file name "HSBS") packing=half (file name "HSBS")' "$work/f4.err"
check "F4 half SBS detected by file name" $?
no_xrdome_lines "$work/f4.err"; check "F4 no xrdome lines" $?
check_flat eyes "$work/f4.ppm"; check "F4 left viewport red, right blue" $?
flat f4n --frames 30 --zoom 1.0 --mpv-opt sub-visibility=no --dump-frame "$work/f4n.ppm" \
     "$work/clip.HSBS.mp4"
check_flat nosubs "$work/f4.ppm" "$work/f4n.ppm"; check "F4 no subtitles in half SBS" $?

# F5. Full top-bottom, no hint: by frame shape.
flat f5 --frames 30 --dump-frame "$work/f5.ppm" "$work/flattb.mp4"
grep -qx 'detect: proj=flat (frame shape) layout=tb-lr (frame shape) packing=full (auto)' "$work/f5.err"
check "F5 full top-bottom detected by frame shape" $?
check_flat eyes "$work/f5.ppm"; check "F5 left viewport red (top), right blue (bottom)" $?

# F6. Subtitles in flat 2D are drawn into the video texture (spec 5.11).
cp "$work/flat2d.mp4" "$work/sub.mp4"
printf '1\n00:00:00,000 --> 00:01:00,000\nSUBTITLE TEST LINE\n' >"$work/sub.srt"
flat f6 --frames 30 --zoom 1.0 --dump-frame "$work/f6.ppm" "$work/sub.mp4"
flat f6n --frames 30 --zoom 1.0 --mpv-opt sub-visibility=no --dump-frame "$work/f6n.ppm" \
     "$work/sub.mp4"
check_flat subs "$work/f6.ppm" "$work/f6n.ppm"; check "F6 subtitles shown in flat 2D" $?

# F7. ffprobe unavailable: one note, and the 2:1 pattern still plays as VR180
# by the fallback.
mkdir -p "$work/cfg/xrdome"
printf '[general]\nffprobe = /nonexistent\n' >"$work/cfg/xrdome/config.ini"
had_cfg=${XDG_CONFIG_HOME+set} old_cfg=${XDG_CONFIG_HOME-}
XDG_CONFIG_HOME="$work/cfg"
export XDG_CONFIG_HOME
flat f7 --frames 30 --dump-frame "$work/f7.ppm" "$work/pattern.mp4"
if [ -n "$had_cfg" ]; then XDG_CONFIG_HOME=$old_cfg; else unset XDG_CONFIG_HOME; fi
[ "$(grep -c '^xrdome: note:' "$work/f7.err")" -eq 1 ]; check "F7 exactly one note" $?
grep -q '^xrdome: note: cannot run /nonexistent' "$work/f7.err"; check "F7 the note names the program" $?
grep -qx 'detect: proj=hequirect (fallback) layout=sbs-lr (fallback)' "$work/f7.err"
check "F7 hequirect sbs-lr by fallback" $?
check_frame "$work/f7.ppm"; check "F7 colours and orientation" $?

# F8. A file name that starts with '-' and has a space (Review Focus 1): probed
# and played.
cp "$work/flat2d.mp4" "$work/-weird name.mp4"
here=$(pwd)
cd "$work" || exit 1
flat f8 --frames 10 "./-weird name.mp4"
cd "$here" || exit 1
grep -qx 'detect: proj=flat (frame shape) layout=mono (frame shape)' "$work/f8.err"
check "F8 '-weird name.mp4' probed (flat by frame shape)" $?

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
    grep -qx 'settings: saved zoom=1.00 content-fov=90x90 proj=hequirect' "$work/m2.err"; check "M2 saved line" $?
[ "$(nrec)" -eq 1 ]; check "M2 exactly one record" $?
grep -q '^content_fov = 90x90' "$(rec)" && grep -q "^path = $work/mem.mp4\$" "$(rec)"
check "M2 record has content_fov and path" $?

# M3. restored with no flags.
xvfb "$xrdome" --windowed --pose null --frames 30 --dump-frame "$work/mem90.ppm" \
     "$work/mem.mp4" >"$work/m3.err" 2>&1
st=$?
check "M3 exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
grep -qx 'settings: restored zoom=1.00 content-fov=90x90 proj=hequirect' "$work/m3.err"
check "M3 restored line" $?
! grep -q '^settings: saved' "$work/m3.err"; check "M3 nothing saved" $?
check_narrow "$work/out.ppm" "$work/mem90.ppm"
check "M3 dumped frame is the 90x90 picture" $?

# M4. survives a rename (the record is keyed by content).
mv "$work/mem.mp4" "$work/moved.mp4"
xvfb "$xrdome" --windowed --pose null --frames 30 --dump-frame "$work/mov90.ppm" \
     "$work/moved.mp4" >"$work/m4.err" 2>&1
st=$?
check "M4 exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
grep -qx 'settings: restored zoom=1.00 content-fov=90x90 proj=hequirect' "$work/m4.err"
check "M4 restored line" $?
[ "$(nrec)" -eq 1 ]; check "M4 still one record" $?
check_narrow "$work/out.ppm" "$work/mov90.ppm"
check "M4 dumped frame is the 90x90 picture" $?

# M5. a flag wins and is saved.
xvfb "$xrdome" --windowed --pose null --content-fov 180x180 --frames 30 \
     --dump-frame "$work/mem180.ppm" "$work/moved.mp4" >"$work/m5.err" 2>&1
st=$?
check "M5 exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
grep -qx 'settings: restored zoom=1.00 proj=hequirect' "$work/m5.err"
check "M5 restored line names only zoom and proj" $?
grep -qx 'settings: saved zoom=1.00 content-fov=180x180 proj=hequirect' "$work/m5.err"
check "M5 saved line" $?
check_frame "$work/mem180.ppm"
check "M5 dumped frame is the 180x180 picture" $?

# M6. --no-memory neither reads nor writes.
xvfb "$xrdome" --windowed --pose null --zoom 1.0 --content-fov 90x90 --frames 30 \
     "$work/moved.mp4" >"$work/m6a.err" 2>&1
st=$?
check "M6 setup run exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
grep -q '^settings: saved .*content-fov=90x90' "$work/m6a.err"; check "M6 setup run saved 90x90" $?
cp "$(rec)" "$work/m6.rec"
xvfb "$xrdome" --no-memory --windowed --pose null --frames 30 --dump-frame "$work/nm.ppm" \
     "$work/moved.mp4" >"$work/m6.err" 2>&1
st=$?
check "M6 exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
! grep -q '^settings:' "$work/m6.err"; check "M6 no settings line" $?
check_frame "$work/nm.ppm"
check "M6 dumped frame is the 180x180 picture" $?
# A flag that differs from the record would be saved if --no-memory did not stop it.
xvfb "$xrdome" --no-memory --windowed --pose null --zoom 0.5 --frames 30 \
     "$work/moved.mp4" >"$work/m6b.err" 2>&1
st=$?
check "M6 flag run exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
! grep -q '^settings:' "$work/m6b.err"; check "M6 flag run no settings line" $?
cmp -s "$work/m6.rec" "$(rec)"; check "M6 record byte-identical" $?

# M7b. a record from a newer xrdome is noted once, kept, and not overwritten.
sed -i 's/^version = 1$/version = 2/' "$(rec)"
cp "$(rec)" "$work/m7b.rec"
xvfb "$xrdome" --windowed --pose null --zoom 0.5 --frames 30 "$work/moved.mp4" >"$work/m7b.err" 2>&1
st=$?
check "M7b exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
[ "$(grep -c '^xrdome: note: settings record' "$work/m7b.err")" -eq 1 ]; check "M7b one note" $?
! grep -q '^settings: restored' "$work/m7b.err"; check "M7b nothing restored" $?
! grep -q '^settings: saved' "$work/m7b.err"; check "M7b nothing saved" $?
grep -qx 'version = 2' "$(rec)"; check "M7b record still version 2" $?
cmp -s "$work/m7b.rec" "$(rec)"; check "M7b record byte-identical" $?
sed -i 's/^version = 2$/version = 1/' "$(rec)"

# M7. a corrupt record is reported, kept, and not overwritten.
echo "zoom = 99" >>"$(rec)"
cp "$(rec)" "$work/m7.rec"
xvfb "$xrdome" --windowed --pose null --zoom 0.5 --frames 30 "$work/moved.mp4" >"$work/m7.err" 2>&1
st=$?
check "M7 exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
[ "$(grep -c '^xrdome: warning: settings record' "$work/m7.err")" -eq 1 ]; check "M7 one warning" $?
! grep -q '^settings: saved' "$work/m7.err"; check "M7 nothing saved" $?
grep -qx 'zoom = 99' "$(rec)"; check "M7 record untouched" $?
cmp -s "$work/m7.rec" "$(rec)"; check "M7 record byte-identical" $?

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
( XDG_STATE_HOME="$work/pattern.mp4"; export XDG_STATE_HOME
  xvfb "$xrdome" --windowed --pose null --zoom 0.5 --frames 30 \
       "$work/moved.mp4" >"$work/m10.err" 2>&1 )
st=$?
check "M10 exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
[ "$(grep -c '^xrdome: warning: cannot save settings' "$work/m10.err")" -eq 1 ]; check "M10 one warning" $?

# M11. a --forget that cannot remove the record warns once, and the stale record is neither
# restored nor overwritten (a directory in the record's place makes unlink fail, even as root).
xvfb "$xrdome" --windowed --pose null --zoom 0.5 --frames 30 "$work/moved.mp4" >"$work/m11a.err" 2>&1
r=$(rec)
rm -f "$r" && mkdir "$r"
xvfb "$xrdome" --forget --windowed --pose null --zoom 0.75 --frames 30 \
     "$work/moved.mp4" >"$work/m11.err" 2>&1
st=$?
check "M11 exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
[ "$(grep -c '^xrdome: warning: cannot forget settings record ' "$work/m11.err")" -eq 1 ] &&
    ! grep -q '^xrdome: .*settings record .*ignoring' "$work/m11.err"
check "M11 one forget warning, record not loaded" $?
! grep -q '^settings:' "$work/m11.err"; check "M11 nothing restored or saved" $?
[ -d "$r" ]; check "M11 record left in place" $?


# M12. A flat file's record (spec 5.9): --zoom on a flat file is its flat zoom,
# and the record pins the projection on the next run.
( XDG_STATE_HOME="$work/state-flat"; export XDG_STATE_HOME
  cp "$work/flat2d.mp4" "$work/flatrec.mp4"
  xvfb "$xrdome" --windowed --pose null --zoom 2.0 --frames 30 "$work/flatrec.mp4" \
       >"$work/m12a.err" 2>&1
  echo $? >"$work/m12a.st"
  xvfb "$xrdome" --windowed --pose null --frames 30 "$work/flatrec.mp4" >"$work/m12b.err" 2>&1
  echo $? >"$work/m12b.st" )
cat "$work/m12a.err" "$work/m12b.err"
check "M12 both runs exit 0" "$([ "$(cat "$work/m12a.st")" -eq 0 ] && [ "$(cat "$work/m12b.st")" -eq 0 ]; echo $?)"
grep -qx 'settings: saved proj=flat flat-zoom=2.00' "$work/m12a.err"; check "M12 saved line" $?
grep -qx 'settings: restored proj=flat flat-zoom=2.00' "$work/m12b.err"; check "M12 restored line" $?
grep -qx 'detect: proj=flat (record) layout=mono (frame shape)' "$work/m12b.err"
check "M12 projection from the record" $?
! grep -q '^settings: saved' "$work/m12b.err"; check "M12 nothing saved on the second run" $?
grep -qx 'proj = flat' "$work"/state-flat/xrdome/files/*.ini && grep -qx 'flat_zoom = 2.0000' "$work"/state-flat/xrdome/files/*.ini
check "M12 record has proj = flat and flat_zoom" $?

exit $fail
