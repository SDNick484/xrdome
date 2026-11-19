#!/bin/sh
# Smoke test of the depth helper under Xvfb (software GL, no glasses, no
# model): xrdome starts tests/fake_depth_helper.py as its "xrdome-tools",
# asks it for the depth of a photo with none cached, shows the photo in 2D,
# and switches to 3D when the reply lands (spec 7 of the depth 3D design).
# Prefetch of the next and previous photos, a helper already running, a
# helper that is missing, exits, or fails on a photo.
# usage: smoke_depth_serve.sh XRDOME FAKE_HELPER
# Exits 77 (meson: skipped) when xvfb-run, ffmpeg or python3 is missing.
set -u

xrdome=$1
helper=$2
case $xrdome in /*) ;; *) xrdome=$(pwd)/$xrdome ;; esac
case $helper in /*) ;; *) helper=$(pwd)/$helper ;; esac

for tool in xvfb-run ffmpeg python3; do
    command -v "$tool" >/dev/null 2>&1 || { echo "skip: $tool not found"; exit 77; }
done

work=$(mktemp -d "${TMPDIR:-/tmp}/xrdome-dserve.XXXXXX") || exit 1
work=$(cd "$work" && pwd)
helpers="$work/pids"
cleanup() {
    [ -s "$helpers" ] && kill $(cat "$helpers") 2>/dev/null
    rm -rf "$work"
}
trap cleanup EXIT
export LIBGL_ALWAYS_SOFTWARE=1
export XRDOME_XR_DRIVER_DIR="$work/no-xr-driver"
export XDG_STATE_HOME="$work/state"
export XDG_CACHE_HOME="$work/cache"
export XDG_CONFIG_HOME="$work/config" # no ini: the defaults
export FAKE_DEPTH_IDLE=3
cache="$XDG_CACHE_HOME/xrdome-tools/depth"

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

# The photo of smoke_depth.sh: 200x100, grey, a red bar at x 40-49 (the far
# half of the fake helper's step depth) and a white bar at x 140-149 (near).
ffmpeg -v error -y -f lavfi -i "color=c=0x404040:s=200x100" -vf \
    "drawbox=x=40:y=0:w=10:h=100:color=0xff0000:t=fill,drawbox=x=140:y=0:w=10:h=100:color=0xffffff:t=fill,format=rgb24" \
    -frames:v 1 "$work/photo.png" || { echo "FAIL: ffmpeg could not make the photo"; exit 1; }
# photo NAME: a copy with other bytes, so its depth key is its own.
photo() {
    mkdir -p "$(dirname "$work/$1")"
    cp "$work/photo.png" "$work/$1"
    printf '%s' "$1" >>"$work/$1"
}

# bars PPM: "<picture width> <red shift> <white shift> <eyes identical>" (as
# smoke_depth.sh).
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
near() { awk -v v="$1" -v w="$2" -v t="$3" 'BEGIN { d = v - w; if (d < 0) d = -d; exit !(d <= t) }'; }

# in3d NAME: the default strength (2%) and plane (0.5) in the dumped frame.
in3d() {
    set -- "$1" $(bars "$work/$1.ppm")
    _want=$(awk -v w="$2" 'BEGIN { printf "%.2f", 0.02 * 0.5 * w }')
    near "$3" "-$_want" 1.5 && near "$4" "$_want" 1.5
    check "$1 in 3D: red bar moved $3 px, white bar $4 px (want -$_want and $_want)" $?
}
in2d() {
    [ "$(bars "$work/$1.ppm" | cut -d' ' -f4)" = yes ]; check "$1 in 2D: eyes identical" $?
}

# play NAME HELPER FRAMES ARGS...: one run with its own runtime directory
# (socket, helper log) $work/NAME.run and helper request log $work/NAME.log;
# stderr in $work/NAME.err, the last frame in $work/NAME.ppm.
play() (
    _n=$1 _h=$2 _f=$3
    shift 3
    XDG_RUNTIME_DIR="$work/$_n.run"
    FAKE_DEPTH_LOG="$work/$_n.log"
    export XDG_RUNTIME_DIR FAKE_DEPTH_LOG
    mkdir -p "$XDG_RUNTIME_DIR"
    xvfb "$xrdome" --no-memory --windowed --pose null --zoom 0.8 --dither off --frames "$_f" \
         --dump-frame "$work/$_n.ppm" --depth-helper "$_h" "$@" >"$work/$_n.err" 2>&1
)
xrd_lines() { grep '^xrdome:' "$work/$1.err" | grep -v "^xrdome: warning: hardware decoding is '[^']*', not vaapi"; }
status() { check "$1 exits 0 (got $2)" "$([ "$2" -eq 0 ]; echo $?)"; }
# lines NAME: its depth: lines and xrdome: notes, in order, one per line
lines() { grep -E '^(depth: |xrdome: note|photo: )' "$work/$1.err"; }
key() { python3 -I -c 'import hashlib, sys; print(hashlib.sha256(open(sys.argv[1], "rb").read()).hexdigest())' "$1"; }
abs() { (cd "$(dirname "$1")" && printf '%s/%s\n' "$(pwd -P)" "$(basename "$1")"); }

# 1. No depth cached: the photo opens in 2D, xrdome starts the helper, asks,
# and draws 3D once the reply lands (about 0.3 s later here).
photo h1.png
play h1 "$helper" 240 "$work/h1.png"
status H1 $?
[ "$(lines h1)" = "depth: none cached; asking the depth helper
depth: 200x100 from the depth helper" ]
check "H1 depth: lines, 2D first then the helper's depth" $?
[ -z "$(xrd_lines h1)" ]; check "H1 no xrdome errors, warnings or notes" $?
in3d h1
[ "$(cat "$work/h1.log")" = "start
$(abs "$work/h1.png") prefetch=0" ]
check "H1 the helper started once and was asked for the absolute path" $?
[ -f "$cache/$(key "$work/h1.png")-dav2s-v1.pgm" ]; check "H1 the depth is in the cache" $?
[ -f "$work/h1.run/xrdome-depth.log" ]; check "H1 the helper's log beside its socket" $?

# 2. The same photo again: from the cache, and no helper is started.
play h2 "$helper" 3 "$work/h1.png"
grep -qx 'depth: 200x100 from the cache' "$work/h2.err"; check "H2 depth from the cache" $?
[ ! -e "$work/h2.log" ]; check "H2 no helper started for a cached photo" $?

# 3. A helper already running is used, not started again (the configured
# helper does not even exist).
photo h3.png
mkdir -p "$work/h3.run"
XDG_RUNTIME_DIR="$work/h3.run" FAKE_DEPTH_LOG="$work/h3.log" "$helper" depth serve \
    --socket "$work/h3.run/xrdome-depth.sock" 2>"$work/h3.helper.err" &
echo $! >>"$helpers"
for _ in $(seq 100); do [ -S "$work/h3.run/xrdome-depth.sock" ] && break; sleep 0.1; done
play h3 "$work/no-such-helper" 240 "$work/h3.png"
status H3 $?
grep -qx 'depth: 200x100 from the depth helper' "$work/h3.err"; check "H3 depth from the running helper" $?
[ -z "$(xrd_lines h3)" ]; check "H3 no xrdome errors, warnings or notes" $?
in3d h3

# 4. A directory: the open photo is asked for, then the previous and the next
# are prefetched (the next last, so it is the helper's newest prefetch);
# PageDown shows the next one in 3D.
photo dir/a.png
photo dir/b.png
photo dir/c.png
play h4 "$helper" 240 --press PageDown "$work/dir"
status H4 $?
a=$(abs "$work/dir/a.png") b=$(abs "$work/dir/b.png") c=$(abs "$work/dir/c.png")
[ "$(head -3 "$work/h4.log")" = "start
$a prefetch=0
$b prefetch=1" ]
check "H4 the first photo asked for, the second prefetched" $?
grep -qx "$a prefetch=1" "$work/h4.log" && grep -qx "$c prefetch=1" "$work/h4.log"
check "H4 after PageDown: the previous and the next prefetched" $?
lines h4 | grep -q '^depth: 200x100 from the \(cache\|depth helper\)$'
check "H4 the second photo has depth" $?
[ "$(lines h4 | tail -1)" = "depth: 200x100 from the cache" ] ||
    [ "$(lines h4 | tail -1)" = "depth: 200x100 from the depth helper" ]
check "H4 the last depth: line is the second photo's depth" $?
[ -z "$(xrd_lines h4)" ]; check "H4 no xrdome errors, warnings or notes" $?
in3d h4
missing=0
for f in a b c; do
    [ -f "$cache/$(key "$work/dir/$f.png")-dav2s-v1.pgm" ] || missing=1
done
check "H4 all three depths in the cache" $missing

# 5. The helper is missing: one note for the run, each photo 2D.
photo miss/a.png
photo miss/b.png
play h5 "$work/no-such-helper" 60 --press PageDown "$work/miss"
status H5 $?
[ "$(xrd_lines h5 | wc -l)" -eq 1 ] &&
    xrd_lines h5 | grep -q "^xrdome: note: depth helper: .*$work/no-such-helper.*; photos without cached depth stay 2D\$"
check "H5 one note: the helper cannot be started" $?
grep -qx 'depth: none cached; 2D (the depth helper is not running)' "$work/h5.err"
check "H5 the second photo: 2D, the helper is not running" $?
in2d h5

# 6. The helper exits at start: the note names its status and its log.
photo h6.png
(FAKE_DEPTH_EXIT=2; export FAKE_DEPTH_EXIT; play h6 "$helper" 120 "$work/h6.png")
status H6 $?
[ "$(xrd_lines h6)" = "xrdome: note: depth helper: $helper depth serve exited with status 2 (its log: $work/h6.run/xrdome-depth.log); photos without cached depth stay 2D" ]
check "H6 one note with the status and the log" $?
grep -qx 'depth serve: fake failure' "$work/h6.run/xrdome-depth.log"; check "H6 the helper's stderr in its log" $?
in2d h6

# 7. The helper cannot make a photo's depth: a note, 2D, xrdome goes on.
photo bad.png
play h7 "$helper" 240 "$work/bad.png"
status H7 $?
[ "$(xrd_lines h7)" = "xrdome: note: depth helper: cannot decode bad.png; showing the photo in 2D" ]
check "H7 a note for the photo" $?
in2d h7

# 8. --depth-helper off: as before the helper existed.
photo h8.png
play h8 off 3 "$work/h8.png"
grep -qx 'depth: none cached; 2D (xrdome-tools depth photo makes it)' "$work/h8.err"
check "H8 off: the line of a photo with no depth" $?
[ ! -e "$work/h8.log" ] && [ ! -e "$work/h8.run/xrdome-depth.log" ]; check "H8 off: nothing started" $?

exit $fail
