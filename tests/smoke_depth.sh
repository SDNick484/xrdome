#!/bin/sh
# Smoke test of depth 3D for photos under Xvfb (software GL, no glasses): a
# synthetic photo and a step depth in the cache, the eyes' shift measured on a
# dumped frame against the expected pixels (spec 6 of the depth 3D design).
# usage: smoke_depth.sh XRDOME
# Exits 77 (meson: skipped) when xvfb-run, ffmpeg or python3 is missing.
set -u

xrdome=$1
case $xrdome in /*) ;; *) xrdome=$(pwd)/$xrdome ;; esac

for tool in xvfb-run ffmpeg python3; do
    command -v "$tool" >/dev/null 2>&1 || { echo "skip: $tool not found"; exit 77; }
done

work=$(mktemp -d "${TMPDIR:-/tmp}/xrdome-depth.XXXXXX") || exit 1
work=$(cd "$work" && pwd)
trap 'rm -rf "$work"' EXIT
export LIBGL_ALWAYS_SOFTWARE=1
export XRDOME_XR_DRIVER_DIR="$work/no-xr-driver"
export XDG_STATE_HOME="$work/state"
export XDG_CACHE_HOME="$work/cache"
cache="$XDG_CACHE_HOME/xrdome-tools/depth"
mkdir -p "$cache"

xvfb() { # as smoke_player.sh: the program's own exit status
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

# A 200x100 photo: grey, a red bar at x 40-49 (the far half) and a white bar
# at x 140-149 (the near half).
ffmpeg -v error -y -f lavfi -i "color=c=0x404040:s=200x100" -vf \
    "drawbox=x=40:y=0:w=10:h=100:color=0xff0000:t=fill,drawbox=x=140:y=0:w=10:h=100:color=0xffffff:t=fill,format=rgb24" \
    -frames:v 1 "$work/photo.png" || { echo "FAIL: ffmpeg could not make the photo"; exit 1; }
cp "$work/photo.png" "$work/plain.png"
printf 'x' >>"$work/plain.png" # other bytes, so no depth is cached for it

# depth PHOTO W H KIND: the cache file of PHOTO, as tools/xrdome_depth/cache.py
# writes it: KIND step (0 left of the middle, 65535 from it on) or bad.
depth() {
python3 -I - "$cache" "$@" <<'PY'
import hashlib, sys
cache, photo, w, h, kind = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), sys.argv[5]
key = hashlib.sha256(open(photo, "rb").read()).hexdigest()
row = b"".join((0 if x < w // 2 else 65535).to_bytes(2, "big") for x in range(w))
body = row * h if kind == "step" else row[:5]
open(f"{cache}/{key}-dav2s-v1.pgm", "wb").write(f"P5\n{w} {h}\n65535\n".encode() + body)
PY
}
depth "$work/photo.png" 200 100 step || { echo "FAIL: could not write the depth"; exit 1; }

# bars PPM: "<picture width> <red shift> <white shift> <eyes identical>": the
# picture's width in the left eye, and for each bar the left eye's bar centre
# minus the right eye's, both measured from their picture's left edge, in
# pixels; "yes" when the two eyes are the same pixels.
bars() {
python3 -I - "$1" <<'PY'
import sys
data = open(sys.argv[1], "rb").read()
magic, size, maxval, px = data.split(b"\n", 3)
w, h = map(int, size.split())
assert magic == b"P6" and len(px) == w * h * 3, "bad PPM"
eye, y = w // 2, h // 2
row = [tuple(px[(y * w + x) * 3:(y * w + x) * 3 + 3]) for x in range(w)]
def measure(x0):
    lit = [x for x in range(x0, x0 + eye) if sum(row[x]) > 60]
    left = lit[0]
    def centre(test):
        xs = [x for x in range(x0, x0 + eye) if test(row[x])]
        return sum(xs) / len(xs) - left
    red = centre(lambda c: c[0] > 150 and c[1] < 100 and c[2] < 100)
    white = centre(lambda c: c[0] > 150 and c[1] > 150 and c[2] > 150)
    return lit[-1] - left + 1, red, white
bw, rl, wl = measure(0)
_, rr, wr = measure(eye)
same = all(px[(yy * w + x) * 3:(yy * w + x) * 3 + 3] == px[(yy * w + x + eye) * 3:(yy * w + x + eye) * 3 + 3]
           for yy in range(0, h, 4) for x in range(eye))
print(f"{bw} {rl - rr:.2f} {wl - wr:.2f} {'yes' if same else 'no'}")
PY
}

# near VALUE WANT TOL: |VALUE - WANT| <= TOL
near() { awk -v v="$1" -v w="$2" -v t="$3" 'BEGIN { d = v - w; if (d < 0) d = -d; exit !(d <= t) }'; }

# play NAME ARGS...: one run at zoom 0.8 (the whole photo in view), without
# dithering, which differs between the eyes.
play() {
    _n=$1
    shift
    xvfb "$xrdome" --no-memory --windowed --pose null --zoom 0.8 --dither off --frames 3 \
         --dump-frame "$work/$_n.ppm" "$@" >"$work/$_n.err" 2>&1
}
xrd_lines() { grep '^xrdome:' "$work/$1.err" | grep -v "^xrdome: warning: hardware decoding is '[^']*', not vaapi"; }

# shifted NAME STRENGTH SIGN: the run's red bar moved by -SIGN and the white
# bar by +SIGN times STRENGTH * 0.5 * the picture width (plane 0.5: the near
# half at depth 1 and the far half at 0 sit half the strength each side of
# the screen), within 1.5 pixels.
shifted() {
    set -- "$1" "$2" "$3" $(bars "$work/$1.ppm")
    _want=$(awk -v s="$2" -v g="$3" -v w="$4" 'BEGIN { printf "%.2f", g * s * 0.5 * w }')
    _neg=$(awk -v x="$_want" 'BEGIN { printf "%.2f", -x }')
    near "$5" "$_neg" 1.5 && near "$6" "$_want" 1.5
    check "$1 red bar moved $5 px (want $_neg), white bar $6 px (want $_want), picture $4 px" $?
}

# 1. The default strength (2%) and plane (0.5): the near bar comes out of the
# screen (the left eye's copy right of the right eye's), the far bar goes in.
play s1 "$work/photo.png"
st=$?
check "S1 exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
grep -qx 'depth: 200x100 from the cache' "$work/s1.err"; check "S1 depth: line" $?
[ -z "$(xrd_lines s1)" ]; check "S1 no xrdome errors or warnings" $?
shifted s1 0.02 1

# 2. = six times: 5% (the maximum, one press over it).
play s2 --press "=,=,=,=,=,=,=" "$work/photo.png"
grep -qx 'depth: on, strength 5.0%, plane 0.50' "$work/s2.err"; check "S2 depth: line at 5%" $?
[ "$(grep -c '^depth: on' "$work/s2.err")" -eq 6 ]; check "S2 no line for the press at the limit" $?
shifted s2 0.05 1

# 3. S swaps the eyes, which inverts the depth.
play s3 --press "=,=,=,=,=,=,s" "$work/photo.png"
shifted s3 0.05 -1

# 4. The plane: . brings it nearer (0.6), so the near bar comes out less and the
# far bar goes in more: shifts of 0.4 and 0.6 times the strength.
play s4 --press "=,=,=,=,=,=,.,." "$work/photo.png"
grep -qx 'depth: on, strength 5.0%, plane 0.60' "$work/s4.err"; check "S4 plane 0.60" $?
set -- $(bars "$work/s4.ppm")
near "$2" "$(awk -v w="$1" 'BEGIN { print -0.05 * 0.6 * w }')" 1.5 &&
    near "$3" "$(awk -v w="$1" 'BEGIN { print 0.05 * 0.4 * w }')" 1.5
check "S4 red bar $2 px, white bar $3 px at plane 0.6" $?

# 5. Strength 0 and D off: both eyes are the 2D picture, pixel for pixel.
play s5 --press "-,-,-,-" "$work/photo.png"
grep -qx 'depth: on, strength 0.0%, plane 0.50' "$work/s5.err"; check "S5 strength 0" $?
[ "$(bars "$work/s5.ppm" | cut -d' ' -f4)" = yes ]; check "S5 strength 0: eyes identical" $?
play s6 --press d "$work/photo.png"
grep -qx 'depth: off, strength 2.0%, plane 0.50' "$work/s6.err"; check "S6 D prints depth: off" $?
[ "$(bars "$work/s6.ppm" | cut -d' ' -f4)" = yes ]; check "S6 D off: eyes identical" $?

# 6. No depth cached: 2D, and one line saying how to make it.
play s7 --press d "$work/plain.png"
grep -qx 'depth: none cached; 2D (xrdome-tools depth photo makes it)' "$work/s7.err"
check "S7 depth: none cached line" $?
grep -q '^depth: none for this photo: none cached' "$work/s7.err"; check "S7 D says why" $?
[ "$(bars "$work/s7.ppm" | cut -d' ' -f4)" = yes ]; check "S7 eyes identical" $?
[ -z "$(xrd_lines s7)" ]; check "S7 no xrdome errors or warnings" $?

# 7. A malformed cache file and a depth of the wrong shape: a warning, 2D.
cp "$work/photo.png" "$work/bad.png"
printf 'y' >>"$work/bad.png"
depth "$work/bad.png" 200 100 bad
play s8 "$work/bad.png"
st=$?
check "S8 exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
grep -q '^xrdome: warning: depth .*-dav2s-v1.pgm: 5 bytes of samples for 200x100 (need 40000); showing the photo in 2D$' "$work/s8.err"
check "S8 warning for a truncated cache file" $?
[ "$(bars "$work/s8.ppm" | cut -d' ' -f4)" = yes ]; check "S8 eyes identical" $?
cp "$work/photo.png" "$work/turned.png"
printf 'z' >>"$work/turned.png"
depth "$work/turned.png" 100 200 step
play s9 "$work/turned.png"
grep -q '^xrdome: warning: the depth (100x200) does not fit the picture shown (200x100)' "$work/s9.err"
check "S9 warning for a depth of the wrong shape" $?
[ "$(bars "$work/s9.ppm" | cut -d' ' -f4)" = yes ]; check "S9 eyes identical" $?

# 8. A JPEG mirrored by EXIF (Orientation 2): 3D on mpv 0.42 and later, which
# mirror it as the helper's transpose does; 2D with a note on older mpv.
ffmpeg -v error -y -i "$work/photo.png" -q:v 1 "$work/base.jpg"
python3 -I - "$work/base.jpg" "$work/mirror.jpg" <<'PY'
import struct, sys
data = open(sys.argv[1], "rb").read()
tiff = b"MM\x00\x2a" + struct.pack(">I", 8)
ifd = struct.pack(">H", 1) + struct.pack(">HHIHH", 0x0112, 3, 1, 2, 0) + struct.pack(">I", 0)
body = b"Exif\x00\x00" + tiff + ifd
open(sys.argv[2], "wb").write(data[:2] + b"\xff\xe1" + struct.pack(">H", len(body) + 2) + body + data[2:])
PY
depth "$work/mirror.jpg" 200 100 step
play s10 "$work/mirror.jpg"
if grep -q '^xrdome: note: EXIF Orientation 2 mirrors this photo and mpv 0\.[0-9]* does not; showing it in 2D (mpv 0.42 does)$' "$work/s10.err"; then
    echo "info: S10 this mpv does not mirror EXIF: the mirrored photo is shown in 2D"
    [ "$(bars "$work/s10.ppm" | cut -d' ' -f4)" = yes ]; check "S10 eyes identical" $?
else
    grep -qx 'depth: 200x100 from the cache' "$work/s10.err"; check "S10 depth for the mirrored photo" $?
    echo "info: S10 this mpv mirrors EXIF: the mirrored photo is in 3D"
fi

# 9. Strength and plane are remembered per photo.
xvfb "$xrdome" --windowed --pose null --frames 3 --press "=,comma" "$work/photo.png" \
    >"$work/m1.err" 2>&1
grep -qx 'settings: saved proj=flat depth-strength=2.5% depth-plane=0.45' "$work/m1.err"
check "M1 settings: saved with the depth strength and plane" $?
xvfb "$xrdome" --windowed --pose null --frames 3 "$work/photo.png" >"$work/m2.err" 2>&1
grep -qx 'settings: restored proj=flat depth-strength=2.5% depth-plane=0.45' "$work/m2.err"
check "M2 settings: restored the depth strength and plane" $?

exit $fail
