#!/bin/sh
# Writes a side-by-side VR180 test pattern: left eye red grid, right eye blue grid,
# each 2048x2048. Each eye has a yellow band along its top edge and a yellow box
# just above its centre, so a vertical flip is visible both in the file and
# straight ahead on the glasses (the box sits 3.5 to 8.8 degrees above the
# horizon, inside the Viture Pro's ~22 degree vertical FOV).
#
# usage: tools/make_pattern.sh [DURATION_S [OUTPUT [ENCODER]]]
#   defaults: 60, pattern_sbs.mp4, libx265. ENCODER libx264 is much faster
#   (used by the smoke test).
set -eu

dur=${1:-60}
out=${2:-pattern_sbs.mp4}
enc=${3:-libx265}

case $enc in
libx264) encopts="-c:v libx264 -preset ultrafast" ;;
libx265) encopts="-c:v libx265 -x265-params log-level=error" ;;
*) echo "make_pattern.sh: unsupported encoder '$enc' (libx264 or libx265)" >&2; exit 2 ;;
esac

marks="drawbox=x=0:y=0:w=iw:h=128:color=yellow:t=fill,drawbox=x=iw/2-256:y=ih/2-100:w=512:h=60:color=yellow:t=fill"

# shellcheck disable=SC2086 # encopts is a word list on purpose
ffmpeg -nostdin -hide_banner -loglevel error -y \
       -f lavfi -i "color=c=red:s=2048x2048:r=30,drawgrid=w=128:h=128:t=2:c=white,$marks" \
       -f lavfi -i "color=c=blue:s=2048x2048:r=30,drawgrid=w=128:h=128:t=2:c=white,$marks" \
       -filter_complex hstack -t "$dur" $encopts -pix_fmt yuv420p "$out"
