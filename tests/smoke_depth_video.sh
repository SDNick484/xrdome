#!/bin/sh
# Smoke test of video frames out to the depth helper under Xvfb (software GL,
# no glasses, no model; spec 8 of the depth 3D design, first half): D on a
# flat mono film starts tests/fake_depth_helper.py as its "xrdome-tools",
# which maps the shared-memory ring, reads the 322x182 frames (top row
# first) and writes depth back. With 3D off nothing is read back and no
# helper starts; a helper that refuses video, --depth-helper off and a
# stereo film each leave the film in 2D.
# usage: smoke_depth_video.sh XRDOME FAKE_HELPER
# Exits 77 (meson: skipped) when xvfb-run, ffmpeg or python3 is missing.
set -u

xrdome=$1
helper=$2
case $xrdome in /*) ;; *) xrdome=$(pwd)/$xrdome ;; esac
case $helper in /*) ;; *) helper=$(pwd)/$helper ;; esac

for tool in xvfb-run ffmpeg python3; do
    command -v "$tool" >/dev/null 2>&1 || { echo "skip: $tool not found"; exit 77; }
done

work=$(mktemp -d "${TMPDIR:-/tmp}/xrdome-dvideo.XXXXXX") || exit 1
work=$(cd "$work" && pwd)
trap 'rm -rf "$work"' EXIT
export LIBGL_ALWAYS_SOFTWARE=1
export XRDOME_XR_DRIVER_DIR="$work/no-xr-driver"
export XDG_STATE_HOME="$work/state"
export XDG_CACHE_HOME="$work/cache"
export XDG_CONFIG_HOME="$work/config" # no ini: the defaults
export FAKE_DEPTH_IDLE=2

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

# A flat 16:9 film: the top half red, the bottom half blue.
ffmpeg -v error -y -f lavfi -i "color=c=red:s=640x360:r=30:d=8" -vf \
    "drawbox=x=0:y=180:w=640:h=180:color=blue:t=fill,format=yuv420p" \
    -c:v libx264 -preset ultrafast "$work/film.mp4" || { echo "FAIL: ffmpeg could not make the film"; exit 1; }

# play NAME HELPER FRAMES ARGS...: one run with its own runtime directory
# $work/NAME.run and helper log $work/NAME.log; stderr in $work/NAME.err, the
# timing log in $work/NAME.csv.
play() (
    _n=$1 _h=$2 _f=$3
    shift 3
    XDG_RUNTIME_DIR="$work/$_n.run"
    FAKE_DEPTH_LOG="$work/$_n.log"
    export XDG_RUNTIME_DIR FAKE_DEPTH_LOG
    mkdir -p "$XDG_RUNTIME_DIR"
    xvfb "$xrdome" --no-memory --windowed --pose null --frames "$_f" \
         --timing-log "$work/$_n.csv" --depth-helper "$_h" "$@" >"$work/$_n.err" 2>&1
)
xrd_lines() { grep '^xrdome:' "$work/$1.err" | grep -v "^xrdome: warning: hardware decoding is '[^']*', not vaapi"; }
status() { check "$1 exits 0 (got $2)" "$([ "$2" -eq 0 ]; echo $?)"; }
# out_us NAME: the sum of the timing log's frames_out_us column
out_us() { awk -F, 'NR > 1 { s += $6 } END { print s + 0 }' "$work/$1.csv"; }
# stopped NAME FIELD: a number from the "depth: video stopped:" line
stopped() {
    sed -n 's/^depth: video stopped: \([0-9]*\) frames out, \([0-9]*\) skipped (readbacks busy), \([0-9]*\) depth maps back;.*/\1 \2 \3/p' \
        "$work/$1.err" | cut -d' ' -f"$2"
}
leftover_shm() { # names of this run's rings still in /dev/shm (Linux); none elsewhere
    [ -d /dev/shm ] && ls /dev/shm | grep "^xrdome-$1-" ; true
}

# 1. D on a flat film: frames go out, the helper reads them top row first and
# writes depth back; the ring is gone afterwards.
play v1 "$helper" 150 --press d "$work/film.mp4"
status V1 $?
grep -q '^detect: proj=flat .*layout=mono' "$work/v1.err"; check "V1 the film is flat mono" $?
shm=$(sed -n 's/^depth: video frames to the depth helper (322x182 through \(\/xrdome-[0-9]*-1\))$/\1/p' "$work/v1.err")
[ -n "$shm" ]; check "V1 frames to the depth helper through a ring ($shm)" $?
grep -qx "depth: the depth helper reads $shm" "$work/v1.err"; check "V1 the helper mapped the ring" $?
[ "$(head -2 "$work/v1.log")" = "start
video $shm" ]
check "V1 the helper started once and was given the ring" $?
frame=$(grep '^frame ' "$work/v1.log")
echo "$frame" | awk '{
    split(substr($2, 5), t, ","); split(substr($3, 8), b, ",")
    exit !(t[1] > 200 && t[3] < 60 && b[1] < 60 && b[3] > 200) }'
check "V1 the helper saw red at the top and blue at the bottom ($frame)" $?
sent=$(stopped v1 1) back=$(stopped v1 3)
[ "${sent:-0}" -ge 30 ] && [ "${back:-0}" -ge 10 ]
check "V1 frames out ($sent) and depth maps back ($back)" $?
[ -z "$(xrd_lines v1)" ]; check "V1 no xrdome errors, warnings or notes" $?
head -n 1 "$work/v1.csv" | grep -qx 'frame,interval_us,pose_to_swap_us,pose_age_us,swap_us,frames_out_us'
check "V1 timing log has frames_out_us" $?
[ "$(out_us v1)" -gt 0 ]; check "V1 frames_out_us logged ($(out_us v1) us in all)" $?
[ -z "$(leftover_shm "$(echo "$shm" | cut -d- -f2)")" ]; check "V1 no ring left in shared memory" $?

# 2. 3D off (the default for a film): nothing read back, no helper started.
play v2 "$helper" 60 "$work/film.mp4"
status V2 $?
! grep -q '^depth: video' "$work/v2.err"; check "V2 no frames out" $?
[ ! -e "$work/v2.log" ]; check "V2 no helper started" $?
[ "$(out_us v2)" -eq 0 ]; check "V2 frames_out_us all 0" $?

# 3. D on, then off: the ring stops, once.
play v3 "$helper" 120 --press d,d "$work/film.mp4"
status V3 $?
[ "$(grep -c '^depth: video frames to the depth helper' "$work/v3.err")" -eq 1 ] &&
    [ "$(grep -c '^depth: video stopped:' "$work/v3.err")" -eq 1 ]
check "V3 one ring started and stopped" $?
grep -B1 '^depth: video stopped:' "$work/v3.err" | head -1 | grep -q '^depth: off'
check "V3 stopped when 3D went off (the frame after D)" $?

# 4. A helper that refuses video: one note, the film plays on in 2D.
(FAKE_DEPTH_NO_VIDEO="no GPU for video"; export FAKE_DEPTH_NO_VIDEO
 play v4 "$helper" 120 --press d "$work/film.mp4")
status V4 $?
xrd_lines v4 | grep -qx "xrdome: note: depth helper: cannot map /xrdome-[0-9]*-1: no GPU for video; the film stays 2D"
check "V4 one note: the helper refused" $?
[ "$(xrd_lines v4 | wc -l)" -eq 1 ]; check "V4 nothing else" $?

# 5. --depth-helper off: D says why, nothing goes out.
play v5 off 30 --press d "$work/film.mp4"
status V5 $?
grep -qx "depth: none for this film: a film's depth comes from the depth helper, which is off" "$work/v5.err"
check "V5 D says the helper is off" $?
! grep -q '^depth: video' "$work/v5.err"; check "V5 no frames out" $?

# 6. A stereo layout: D refuses, nothing goes out.
play v6 "$helper" 30 --press d --layout sbs-lr "$work/film.mp4"
status V6 $?
grep -qx 'depth: photos and flat mono films only' "$work/v6.err"; check "V6 D refuses a stereo film" $?
[ ! -e "$work/v6.log" ]; check "V6 no helper started" $?

exit $fail
