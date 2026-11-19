#!/bin/sh
# Smoke test of photo mode under Xvfb (software GL, no glasses): JPEGs with each
# EXIF Orientation, and a directory of two photos with PageDown / PageUp.
# usage: smoke_photos.sh XRDOME
# Exits 77 (meson: skipped) when xvfb-run, ffmpeg or python3 is missing.
set -u

xrdome=$1
case $xrdome in /*) ;; *) xrdome=$(pwd)/$xrdome ;; esac

for tool in xvfb-run ffmpeg python3; do
    command -v "$tool" >/dev/null 2>&1 || { echo "skip: $tool not found"; exit 77; }
done

work=$(mktemp -d "${TMPDIR:-/tmp}/xrdome-photos.XXXXXX") || exit 1
work=$(cd "$work" && pwd)
trap 'rm -rf "$work"' EXIT
export LIBGL_ALWAYS_SOFTWARE=1
export XRDOME_XR_DRIVER_DIR="$work/no-xr-driver"
export XDG_STATE_HOME="$work/state"

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

# A 96x48 JPEG: red, green, blue and yellow quadrants (top left, top right,
# bottom left, bottom right), 4:4:4 so the colours stay pure.
base="$work/base.jpg"
ffmpeg -v error -y -f lavfi -i "color=c=black:s=96x48" -vf \
    "drawbox=x=0:y=0:w=48:h=24:color=0xff0000:t=fill,drawbox=x=48:y=0:w=48:h=24:color=0x00ff00:t=fill,\
drawbox=x=0:y=24:w=48:h=24:color=0x0000ff:t=fill,drawbox=x=48:y=24:w=48:h=24:color=0xffff00:t=fill,\
format=yuvj444p" -frames:v 1 -q:v 1 "$base" || { echo "FAIL: ffmpeg could not make the base JPEG"; exit 1; }

# exif_jpeg BASE OUT ORIENTATION: BASE with an EXIF APP1 segment holding the
# Orientation tag (1 to 8) inserted after the SOI marker.
exif_jpeg() {
python3 -I - "$1" "$2" "$3" <<'PY'
import struct, sys
src, dst, orient = sys.argv[1], sys.argv[2], int(sys.argv[3])
data = open(src, "rb").read()
assert data[:2] == b"\xff\xd8"
tiff = b"MM\x00\x2a" + struct.pack(">I", 8)
ifd = struct.pack(">H", 1) + struct.pack(">HHIHH", 0x0112, 3, 1, orient, 0) + struct.pack(">I", 0)
body = b"Exif\x00\x00" + tiff + ifd
open(dst, "wb").write(data[:2] + b"\xff\xe1" + struct.pack(">H", len(body) + 2) + body + data[2:])
PY
}

# quadrants PPM: "<picture width>x<picture height> <letters>" for the left eye
# of a dumped frame: the bounding box of what is not black, and the colour at
# the middle of each quarter of it (top left, top right, bottom left, bottom
# right) as R, G, B, Y or ?.
quadrants() {
python3 -I - "$1" <<'PY'
import sys
data = open(sys.argv[1], "rb").read()
magic, size, maxval, px = data.split(b"\n", 3)
w, h = map(int, size.split())
assert magic == b"P6" and len(px) == w * h * 3, "bad PPM"
eye = w // 2
at = lambda x, y: tuple(px[(y * w + x) * 3:(y * w + x) * 3 + 3])
lit = [(x, y) for y in range(0, h, 2) for x in range(0, eye, 2) if sum(at(x, y)) > 60]
if not lit:
    print("0x0 ????")
    sys.exit(0)
x0, x1 = min(p[0] for p in lit), max(p[0] for p in lit)
y0, y1 = min(p[1] for p in lit), max(p[1] for p in lit)
bw, bh = x1 - x0 + 1, y1 - y0 + 1
def name(c):
    r, g, b = c
    if r > 150 and g < 100 and b < 100: return "R"
    if g > 150 and r < 100 and b < 100: return "G"
    if b > 150 and r < 100 and g < 100: return "B"
    if r > 150 and g > 150 and b < 100: return "Y"
    return "?"
q = "".join(name(at(x0 + bw * a // 4, y0 + bh * b // 4)) for b in (1, 3) for a in (1, 3))
print(f"{bw}x{bh} {q}")
PY
}

# shape WxH: landscape, portrait or other, from the picture's box.
shape() {
    awk -v s="${1%% *}" 'BEGIN { split(s, a, "x"); r = a[1] / a[2];
        print (r > 1.9 && r < 2.1) ? "landscape" : (r > 0.47 && r < 0.53) ? "portrait" : "other" }'
}

# play NAME ARGS...: one run at zoom 0.8 (the whole photo in view), stderr in
# $work/NAME.err, the frame in $work/NAME.ppm; returns xrdome's exit status.
play() {
    _n=$1
    shift
    xvfb "$xrdome" --no-memory --windowed --pose null --zoom 0.8 --frames 3 \
         --dump-frame "$work/$_n.ppm" "$@" >"$work/$_n.err" 2>&1
}
xrd_lines() { grep '^xrdome:' "$work/$1.err" | grep -v "^xrdome: warning: hardware decoding is '[^']*', not vaapi"; }

# What the helper's EXIF transpose (Pillow ImageOps.exif_transpose) makes of the
# quadrants for each Orientation tag: tag, quadrants, shape, mirrored.
cat >"$work/orients.txt" <<'LIST'
1 RGBY landscape no
2 GRYB landscape yes
3 YBGR landscape no
4 BYRG landscape yes
5 RBGY portrait yes
6 BRYG portrait no
7 YGBR portrait yes
8 GYRB portrait no
LIST

# 1. Each Orientation tag: the picture is shown upright. Rotations (1, 3, 6, 8,
# all a camera writes) must match the transpose; the mirrored tags (2, 4, 5, 7)
# must at least have the right shape, and say whether mpv mirrors them: mpv
# 0.37 with FFmpeg 6.1 turns the picture but does not mirror it.
while read -r tag want wshape mirrored; do
    exif_jpeg "$base" "$work/o$tag.jpg" "$tag" || { echo "FAIL: exif_jpeg $tag"; exit 1; }
    play "o$tag" "$work/o$tag.jpg"
    st=$?
    check "O$tag exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
    got=$(quadrants "$work/o$tag.ppm")
    [ "$(shape "$got")" = "$wshape" ]; check "O$tag shown $wshape ($got)" $?
    if [ "$mirrored" = no ]; then
        [ "${got#* }" = "$want" ]; check "O$tag quadrants $want, as the EXIF transpose" $?
    elif [ "${got#* }" = "$want" ]; then
        echo "info: O$tag (mirrored) quadrants $want: mpv mirrors it, as the EXIF transpose"
    else
        echo "info: O$tag (mirrored) quadrants ${got#* }, the transpose gives $want: this mpv turns but does not mirror"
    fi
    grep -qx 'detect: proj=flat (image file) layout=mono (image file)' "$work/o$tag.err"
    check "O$tag detected as a flat mono photo" $?
    [ -z "$(xrd_lines "o$tag")" ]; check "O$tag no xrdome errors or warnings" $?
done <"$work/orients.txt"

# 2. A directory of two photos, a landscape and a portrait, opened in name order.
mkdir "$work/dir" "$work/empty"
cp "$work/o1.jpg" "$work/dir/a.jpg"
cp "$work/o6.jpg" "$work/dir/b.jpg"
echo "not a photo" >"$work/dir/notes.txt"
cp "$work/o1.jpg" "$work/dir/.hidden.jpg"

play d0 "$work/dir"
st=$?
check "D0 the directory exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
got=$(quadrants "$work/d0.ppm")
[ "$got" != "${got%RGBY}" ] && [ "$(shape "$got")" = landscape ]
check "D0 opens a.jpg first ($got)" $?
! grep -q '^photo:' "$work/d0.err"; check "D0 no photo: line before a key" $?
[ -z "$(xrd_lines d0)" ]; check "D0 no xrdome errors or warnings" $?

# PageDown to b.jpg: shown upright, and detected again for it.
play d1 --press PageDown "$work/dir"
st=$?
check "D1 PageDown exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
got=$(quadrants "$work/d1.ppm")
[ "${got#* }" = BRYG ] && [ "$(shape "$got")" = portrait ]
check "D1 PageDown shows b.jpg upright ($got)" $?
[ "$(grep -c '^photo:' "$work/d1.err")" -eq 1 ] && grep -qx 'photo: 2 of 2: b.jpg' "$work/d1.err"
check "D1 one 'photo: 2 of 2: b.jpg' line" $?
[ "$(grep -c '^detect: proj=flat (image file) layout=mono (image file)$' "$work/d1.err")" -eq 2 ]
check "D1 detect: line for each photo" $?
[ -z "$(xrd_lines d1)" ]; check "D1 no xrdome errors or warnings" $?

# Past the last photo nothing happens; PageUp goes back, and stops at the first.
play d2 --press PageDown,PageDown,PageUp,PageUp "$work/dir"
st=$?
check "D2 exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
[ "$(grep '^photo:' "$work/d2.err")" = "photo: 2 of 2: b.jpg
photo: 2 of 2 (end of the directory)
photo: 1 of 2: a.jpg
photo: 1 of 2 (start of the directory)" ]
check "D2 photo: lines" $?
got=$(quadrants "$work/d2.ppm")
[ "${got#* }" = RGBY ] && [ "$(shape "$got")" = landscape ]
check "D2 back on a.jpg ($got)" $?

# A single photo has no neighbours: PageDown does nothing.
play d3 --press PageDown "$work/dir/a.jpg"
st=$?
check "D3 PageDown on one file exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
! grep -q '^photo:' "$work/d3.err"; check "D3 no photo: line" $?

# A directory with no photos is an error that says so.
xvfb "$xrdome" --no-memory --windowed --pose null "$work/empty" >"$work/d4.err" 2>&1
st=$?
check "D4 empty directory exits 1 (got $st)" "$([ "$st" -eq 1 ]; echo $?)"
grep -q "^xrdome: no images (jpg, png, webp, bmp, tif) in $work/empty\$" "$work/d4.err"
check "D4 says there are no images" $?

# 3. Settings are per photo: zoom in on a.jpg, go to b.jpg; each keeps its own.
xvfb "$xrdome" --windowed --pose null --frames 3 --press "],PageDown" \
     "$work/dir" >"$work/m1.err" 2>&1
st=$?
check "M1 exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
grep -qx 'zoom: 1.65' "$work/m1.err"; check "M1 zoom in on a.jpg" $?
[ "$(grep -c '^settings: saved' "$work/m1.err")" -eq 1 ]; check "M1 one settings: saved line, when leaving a.jpg" $?
xvfb "$xrdome" --windowed --pose null --frames 3 "$work/dir/a.jpg" >"$work/m2.err" 2>&1
grep -q '^settings: restored ' "$work/m2.err"; check "M2 a.jpg opens with its zoom" $?
xvfb "$xrdome" --windowed --pose null --frames 3 "$work/dir/b.jpg" >"$work/m3.err" 2>&1
! grep -q '^settings: restored' "$work/m3.err"; check "M3 b.jpg has no record" $?

exit $fail
