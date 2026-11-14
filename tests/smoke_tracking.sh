#!/bin/sh
# End-to-end head-tracking test for xrdome under Xvfb (software GL, no glasses).
# usage: smoke_tracking.sh XRDOME MAKE_PATTERN OT_SWEEP
# Exits 77 (meson: skipped) when xvfb-run or ffmpeg is missing.
#
# Two runs with the UDP pose backend on a free port, fed by tools/ot_sweep.py:
#   ref:  yaw 0 throughout
#   turn: yaw 0 until the player has used a pose (it recenters on the first
#         one), then a constant yaw of +20 degrees (NWU: head turned LEFT)
# Turning the head left must move the image content to the RIGHT on screen:
# the yellow box straight ahead (45 degrees wide, wider than the 37.9 degree
# view) fills the box row in ref and sits in the right part of each eye in turn.
set -u

xrdome=$1
make_pattern=$2
ot_sweep=$3

for tool in xvfb-run ffmpeg python3; do
    command -v "$tool" >/dev/null 2>&1 || { echo "skip: $tool not found"; exit 77; }
done

work=$(mktemp -d "${TMPDIR:-/tmp}/xrdome-track.XXXXXX") || exit 1
work=$(cd "$work" && pwd) # absolute: a relative XDG_STATE_HOME is ignored by xrdome
sender=""
cleanup() {
    [ -n "$sender" ] && kill "$sender" 2>/dev/null
    rm -rf "$work"
}
trap cleanup EXIT
export LIBGL_ALWAYS_SOFTWARE=1
# Never the real XRLinuxDriver: runs without --windowed would otherwise read
# /dev/shm/xr_driver_state and could ask live glasses to switch to SBS.
export XRDOME_XR_DRIVER_DIR="$work/no-xr-driver"
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

"$make_pattern" 2 "$work/pattern.mp4" libx264 || { echo "FAIL: make_pattern.sh"; exit 1; }

# A free UDP port, so a real driver on 4242 cannot interfere.
port=$(python3 -I -c 'import socket
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])') || exit 1
echo "udp port $port"

send() { # send YAW: start a constant-yaw sender in the background
    python3 -I "$ot_sweep" --port "$port" --rate 100 --yaw "$1" &
    sender=$!
}
stop_sender() {
    kill "$sender" 2>/dev/null
    wait "$sender" 2>/dev/null
    sender=""
}

sleep_ds() { python3 -I -c 'import sys, time; time.sleep(int(sys.argv[1]) / 10)' "$1"; }

wait_for() { # wait_for FILE REGEX: up to 30 s for a line matching REGEX
    i=0
    until grep -q "$2" "$1" 2>/dev/null; do
        [ "$i" -ge 300 ] && return 1
        sleep_ds 1
        i=$((i + 1))
    done
}

run() { # run NAME YAW [BAD_PACKETS [FILE [FLAG...]]]
    name=$1
    yaw=$2
    bad=${3:-0}
    file=${4:-$work/pattern.mp4}
    if [ $# -ge 4 ]; then shift 4; else shift $#; fi
    csv="$work/$name.csv"
    send 0
    xvfb "$xrdome" --no-memory --windowed --pose "udp-opentrack:port=$port" --frames 120 \
         --timing-log "$csv" --dump-frame "$work/$name.ppm" "$@" "$file" \
         >"$work/$name.err" 2>&1 &
    player=$!

    # Wait (up to 60 s) until a presented video frame used a pose: the timing
    # log starts at the first video frame, and pose_age_us (column 4) is
    # non-zero once a pose was used. So the video is playing and the tracker
    # has recentered on a yaw-0 pose. The player is paced to at most 60 Hz, so
    # 120 frames leave at least 2 s after the switch for the new yaw to arrive.
    i=0
    until [ -f "$csv" ] && awk -F, 'NR > 1 && $4 != 0 { found = 1 } END { exit !found }' "$csv"; do
        if ! kill -0 "$player" 2>/dev/null || [ "$i" -ge 600 ]; then
            echo "FAIL: $name: player used no pose"
            stop_sender
            wait "$player"
            cat "$work/$name.err"
            fail=1
            return 1
        fi
        sleep_ds 1
        i=$((i + 1))
    done

    stop_sender
    send "$yaw"
    if [ "$bad" -gt 0 ]; then
        python3 -I -c 'import socket, sys
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
for _ in range(int(sys.argv[2])):
    s.sendto(b"\0" * 51, ("127.0.0.1", int(sys.argv[1])))' "$port" "$bad"
    fi
    wait "$player"
    st=$?
    stop_sender
    echo "--- $name stderr:"; cat "$work/$name.err"; echo "---"
    check "$name: exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
    head -n 1 "$csv" | grep -qx 'frame,interval_us,pose_to_swap_us,pose_age_us,swap_us'
    check "$name: timing log header" $?
    rows=$(($(wc -l <"$csv") - 1))
    frames=$(sed -n 's/^timing: \([0-9]*\) frames.*/\1/p' "$work/$name.err")
    check "$name: one timing row per frame ($rows rows, summary $frames frames)" \
          "$([ "$rows" -gt 0 ] && [ "$rows" = "$frames" ]; echo $?)"
    return 0
}

# 1. No pose data at all: plays at identity orientation, warns exactly once.
xvfb "$xrdome" --no-memory --windowed --pose "udp-opentrack:port=$port" --frames 10 \
     --dump-frame "$work/nopose.ppm" "$work/pattern.mp4" >"$work/nopose.err" 2>&1
st=$?
echo "--- nopose stderr:"; cat "$work/nopose.err"; echo "---"
check "nopose: exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
[ "$(grep -c '^xrdome: no pose data$' "$work/nopose.err")" -eq 1 ]
check "nopose: one 'no pose data' warning" $?

# 2. Head turn (see the header).
run ref 0
run turn 20 3
grep -q '^pose: 3 malformed UDP packets dropped$' "$work/turn.err"
check "turn: exit reports 3 dropped packets" $?
grep -q '^pose: 0 malformed UDP packets dropped$' "$work/ref.err"
check "ref: exit reports 0 dropped packets" $?

# 3. Sender stops: "pose stale" once and the view holds; it restarts: "pose
# resumed". --frames 100000 holds the last frame so the 2 s file cannot end
# first; SIGTERM then ends the run early, which --frames reports as exit 1.
send 0
PIDFILE="$work/stale.pid" xvfb sh -c '"$@" & echo $! >"$PIDFILE"; wait $!' sh \
     "$xrdome" --no-memory --windowed --pose "udp-opentrack:port=$port" --frames 100000 \
     "$work/pattern.mp4" >"$work/stale.err" 2>&1 &
player=$!
wait_for "$work/stale.err" '^hwdec: '; check "stale: video started" $?
stop_sender
wait_for "$work/stale.err" '^xrdome: pose stale$'; check "stale: 'pose stale' logged" $?
sleep_ds 10
send 0
wait_for "$work/stale.err" '^xrdome: pose resumed$'; check "stale: 'pose resumed' logged" $?
sleep_ds 5
kill -TERM "$(cat "$work/stale.pid")"
wait "$player"
st=$?
stop_sender
echo "--- stale stderr:"; cat "$work/stale.err"; echo "---"
check "stale: SIGTERM before --frames exits 1 (got $st)" "$([ "$st" -eq 1 ]; echo $?)"
grep -q '^xrdome: --frames 100000: exited after' "$work/stale.err"
check "stale: ended by SIGTERM, not by an error" $?
[ "$(grep -c '^xrdome: pose stale$' "$work/stale.err")" -eq 1 ] &&
    [ "$(grep -c '^xrdome: pose resumed$' "$work/stale.err")" -eq 1 ]
check "stale: each logged exactly once" $?

python3 -I - "$work/ref.ppm" "$work/turn.ppm" "$work/nopose.ppm" <<'PY'
import sys

def load(path):
    with open(path, "rb") as f:
        data = f.read()
    magic, size, maxval, px = data.split(b"\n", 3)
    w, h = map(int, size.split())
    assert magic == b"P6" and maxval == b"255" and len(px) == w * h * 3, "bad PPM"
    return w, h, px

def yellow(r, g, b):
    return r > 200 and g > 200 and b < 100

def box_row_profile(img, eye):
    """Per-column fraction of yellow pixels in the box row (rows .15-.30 of
    the eye, where the yellow box above the horizon lands), for one eye."""
    w, h, px = img
    ew = w // 2
    y0, y1 = int(.15 * h), int(.30 * h)
    prof = []
    for x in range(eye * ew, (eye + 1) * ew):
        hit = 0
        for y in range(y0, y1):
            i = (y * w + x) * 3
            hit += yellow(*px[i:i + 3])
        prof.append(hit / (y1 - y0))
    return prof

def centroid(prof):
    tot = sum(prof)
    return sum(i * v for i, v in enumerate(prof)) / tot / len(prof) if tot else float("nan")

def band(prof, f0, f1):
    n = len(prof)
    seg = prof[int(f0 * n):int(f1 * n)]
    return sum(seg) / len(seg)

ref, turn, nopose = load(sys.argv[1]), load(sys.argv[2]), load(sys.argv[3])
ok = True
def expect(name, value, good):
    global ok
    ok &= good
    print(f"  {'ok  ' if good else 'FAIL'} {name}: {value:.3f}")

for eye, name in ((0, "left"), (1, "right")):
    pn = box_row_profile(nopose, eye)
    expect(f"{name} nopose: identity view, box fills the row", min(band(pn, 0, .2), band(pn, .8, 1)),
           min(band(pn, 0, .2), band(pn, .8, 1)) > .8)

for eye, name in ((0, "left"), (1, "right")):
    pr, pt = box_row_profile(ref, eye), box_row_profile(turn, eye)
    cr, ct = centroid(pr), centroid(pt)
    expect(f"{name} ref: box fills the row (left fifth)", band(pr, 0, .2), band(pr, 0, .2) > .8)
    expect(f"{name} ref: box fills the row (right fifth)", band(pr, .8, 1), band(pr, .8, 1) > .8)
    expect(f"{name} turn: left fifth has no box", band(pt, 0, .2), band(pt, 0, .2) < .05)
    expect(f"{name} turn: right fifth has the box", band(pt, .8, 1), band(pt, .8, 1) > .8)
    expect(f"{name} box centroid shift to the right (turn - ref)", ct - cr, ct - cr > .1)
sys.exit(0 if ok else 1)
PY
check "no pose is identity; head turned left moves the image right" $?

# 4. Screen lock (spec 5.4) with a flat 2D file: head-locked, the frame with the
# head turned 20 degrees is the frame with the head straight; fixed, they differ.
"$make_pattern" --kind flat2d 2 "$work/flat2d.mp4" libx264 || { echo "FAIL: make flat2d"; exit 1; }
run lock0 0 0 "$work/flat2d.mp4" --screen locked
run lock20 20 0 "$work/flat2d.mp4" --screen locked
run fixed0 0 0 "$work/flat2d.mp4" --screen fixed
run fixed20 20 0 "$work/flat2d.mp4" --screen fixed
# same_frames A B: at most 0.2 percent of pixels differ by more than 32 in any channel.
same_frames() {
python3 -I - "$1" "$2" <<'PY'
import sys

def load(path):
    with open(path, "rb") as f:
        magic, size, maxval, px = f.read().split(b"\n", 3)
    assert magic == b"P6" and maxval == b"255", "bad PPM"
    return size, px

(sa, a), (sb, b) = load(sys.argv[1]), load(sys.argv[2])
assert sa == sb and len(a) == len(b), "frame sizes differ"
n = len(a) // 3
diff = sum(1 for i in range(0, len(a), 3)
           if max(abs(a[i] - b[i]), abs(a[i + 1] - b[i + 1]), abs(a[i + 2] - b[i + 2])) > 32)
print(f"  {diff / n:.4f} of pixels differ")
sys.exit(0 if diff / n <= .002 else 1)
PY
}
same_frames "$work/lock0.ppm" "$work/lock20.ppm"
check "screen locked: head turn leaves the frame unchanged" $?
! same_frames "$work/fixed0.ppm" "$work/fixed20.ppm"
check "screen fixed: head turn changes the frame" $?

exit $fail
