#!/bin/sh
# Smoke test of the automatic SBS switch (--sbs-switch) under Xvfb, with
# tests/fake_xr_driver.py in place of XRLinuxDriver. Nothing here touches
# /dev/shm: XRDOME_XR_DRIVER_DIR points xrdome at a temporary directory.
# usage: smoke_sbs.sh XRDOME MAKE_PATTERN FAKE_DRIVER
# Exits 77 (meson: skipped) when xvfb-run, ffmpeg, python3 or xrandr is missing.
set -u

xrdome=$1
make_pattern=$2
fake=$3

for tool in xvfb-run ffmpeg python3 xrandr; do
    command -v "$tool" >/dev/null 2>&1 || { echo "skip: $tool not found"; exit 77; }
done

work=$(mktemp -d "${TMPDIR:-/tmp}/xrdome-sbs.XXXXXX") || exit 1
work=$(cd "$work" && pwd)
trap 'rm -rf "$work"' EXIT
export LIBGL_ALWAYS_SOFTWARE=1
export XDG_STATE_HOME="$work/state"
export XDG_CONFIG_HOME="$work/config"
drv="$work/drv"
export XRDOME_XR_DRIVER_DIR="$drv"

fail=0
check() { # check DESCRIPTION CONDITION-EXIT-STATUS
    if [ "$2" -eq 0 ]; then echo "ok:   $1"; else echo "FAIL: $1"; fail=1; fi
}

# glasses NAME DRIVER -- XRDOME-ARGS...: one run of xrdome in an Xvfb whose
# only output is 1920x1080, like the glasses in 1080p mirror mode. Xvfb lets
# RandR grow the screen only up to its starting size, so it starts at
# 3840x1080 and the output is turned off at 1920x1080 first (SDL then lists
# one 1920x1080 display); -noreset keeps that across client disconnects.
# DRIVER is "switch" or "ignore" (fake_xr_driver.py runs alongside, with
# --ignore for "ignore") or "none". stderr goes to $work/NAME.err; returns
# xrdome's exit status.
glasses() {
    _name=$1 _driver=$2
    shift 3
    rm -rf "$drv"
    mkdir -p "$drv"
    [ -n "${STATE:-}" ] && printf '%s' "$STATE" >"$drv/xr_driver_state"
    _xsf="$work/$_name.status"
    XRD_STATUS=$_xsf XRD_DRIVER=$_driver XRD_FAKE=$fake \
    xvfb-run -a -s "-noreset -screen 0 3840x1080x24" sh -c '
        xrandr --output screen --off --fb 1920x1080 || exit 90
        if [ "$XRD_DRIVER" != none ]; then
            if [ "$XRD_DRIVER" = ignore ]; then
                python3 -I "$XRD_FAKE" "$XRDOME_XR_DRIVER_DIR" --ignore &
            else
                python3 -I "$XRD_FAKE" "$XRDOME_XR_DRIVER_DIR" &
            fi
            i=0
            while [ ! -e "$XRDOME_XR_DRIVER_DIR/ready" ] && [ $i -lt 100 ]; do
                sleep 0.05; i=$((i + 1))
            done
        fi
        "$@"; st=$?
        if [ "$XRD_DRIVER" != none ]; then
            # Let the driver take a request written at exit, then stop it.
            i=0
            while [ -e "$XRDOME_XR_DRIVER_DIR/xr_driver_control" ] && [ $i -lt 50 ]; do
                sleep 0.1; i=$((i + 1))
            done
            touch "$XRDOME_XR_DRIVER_DIR/stop"
            wait
        fi
        echo $st >"$XRD_STATUS"' sh "$@" >"$work/$_name.err" 2>&1
    _st=$?
    [ -s "$_xsf" ] && read -r _st <"$_xsf"
    echo "--- $_name:"; cat "$work/$_name.err"; echo "---"
    return "$_st"
}
requests() { cat "$drv/requests.log" 2>/dev/null; }
no_control() { [ ! -e "$drv/xr_driver_control" ]; }
# xrdome's own lines other than the headless hwdec warning.
xrd_lines() { grep '^xrdome:' "$work/$1.err" | grep -v "^xrdome: warning: hardware decoding is '[^']*', not vaapi"; }

"$make_pattern" 2 "$work/pattern.mp4" libx264 || { echo "FAIL: make_pattern.sh"; exit 1; }
nl='
'

# S1. The glasses are in 1080p and the driver is running: xrdome asks for SBS,
# the fake driver grows the screen to 3840x1080, playback starts full screen
# there, and at exit xrdome asks for 1080p again.
glasses s1 switch -- "$xrdome" --no-memory --pose null --frames 30 \
    --dump-frame "$work/s1.ppm" "$work/pattern.mp4"
st=$?
check "S1 exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
[ "$(grep -c '^display: switched the glasses to SBS in [0-9]*\.[0-9] s$' "$work/s1.err")" -eq 1 ]
check "S1 one 'display: switched the glasses to SBS' line" $?
[ "$(requests)" = "sbs_mode=enable${nl}sbs_mode=disable" ]
check "S1 requests: sbs_mode=enable, then sbs_mode=disable at exit" $?
grep -qx 'display: asked XRLinuxDriver to switch the glasses back to 1080p' "$work/s1.err"
check "S1 switch-back line" $?
[ "$(head -c 15 "$work/s1.ppm" 2>/dev/null | head -n 2 | tail -n 1)" = "3840 1080" ]
check "S1 played full screen on the 3840x1080 output" $?
[ -z "$(xrd_lines s1)" ]; check "S1 no xrdome errors or warnings" $?

# S2. --sbs-switch off: nothing is written and the start fails as before the
# switch existed, with exactly the old two lines.
glasses s2 switch -- "$xrdome" --no-memory --pose null --sbs-switch off "$work/pattern.mp4"
st=$?
check "S2 --sbs-switch off exits 1 (got $st)" "$([ "$st" -eq 1 ]; echo $?)"
[ -z "$(requests)" ] && no_control; check "S2 nothing written to the control file" $?
[ "$(xrd_lines s2 | head -n 1)" = "xrdome: no 3840x1080 output was found; switch the glasses to SBS mode" ] &&
    [ "$(xrd_lines s2 | wc -l)" -eq 2 ] &&
    xrd_lines s2 | tail -n 1 | grep -qx 'xrdome: outputs: [^,]* (1920x1080)'
check "S2 only the old error and outputs lines" $?
! grep -q '^display:' "$work/s2.err"; check "S2 no display: line" $?

# S3. A stale state file (the driver crashed and left it): no request, and
# the error says why.
hb=$(( $(date +%s) - 600 ))
STATE="heartbeat=$hb
sbs_mode_supported=true
sbs_mode_enabled=false
"
glasses s3 none -- "$xrdome" --no-memory --pose null "$work/pattern.mp4"
st=$?
unset STATE
check "S3 stale driver exits 1 (got $st)" "$([ "$st" -eq 1 ]; echo $?)"
no_control; check "S3 nothing written to the control file" $?
grep -q "^xrdome: no 3840x1080 output was found; switch the glasses to SBS mode\$" "$work/s3.err" &&
    grep -q "^xrdome: automatic SBS switch: XRLinuxDriver is not running (the heartbeat in $drv/xr_driver_state is 60[0-9] s old)\$" "$work/s3.err"
check "S3 the old error, then why there was no switch" $?

# S4. The driver takes the request but the output never changes: after the
# wait, the old error and the timeout; nothing is switched back.
glasses s4 ignore -- "$xrdome" --no-memory --pose null "$work/pattern.mp4"
st=$?
check "S4 no switch exits 1 (got $st)" "$([ "$st" -eq 1 ]; echo $?)"
[ "$(requests)" = "sbs_mode=enable" ]; check "S4 only sbs_mode=enable was written" $?
grep -qx 'xrdome: automatic SBS switch: asked XRLinuxDriver to switch the glasses to SBS, but no 3840x1080 output appeared within 8 s' "$work/s4.err"
check "S4 timeout line" $?
grep -qx 'xrdome: outputs: [^,]* (1920x1080)' "$work/s4.err"
check "S4 outputs line lists the 1080p output" $?

# S5. --windowed never looks for the glasses' output, so never asks.
glasses s5 switch -- "$xrdome" --no-memory --windowed --pose null --frames 10 "$work/pattern.mp4"
st=$?
check "S5 --windowed exits 0 (got $st)" "$([ "$st" -eq 0 ]; echo $?)"
[ -z "$(requests)" ] && no_control; check "S5 nothing written" $?
! grep -q '^display:' "$work/s5.err"; check "S5 no display: line" $?

exit $fail
