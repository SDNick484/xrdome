#!/bin/sh
# Writes a side-by-side VR180 test pattern: left eye red grid, right eye blue grid,
# each 2048x2048. Each eye has a yellow band along its top edge and a yellow box
# just above its centre, so a vertical flip is visible both in the file and
# straight ahead on the glasses (the box sits 3.5 to 8.8 degrees above the
# horizon, inside the Viture Pro's ~22 degree vertical FOV).
#
# usage: tools/make_pattern.sh [DURATION_S [OUTPUT [ENCODER [EYE_WxH]]]]
#   defaults: 60, pattern_sbs.mp4, libx265, 2048x2048. ENCODER libx264 is much
#   faster (used by the smoke test). EYE_WxH sizes each eye; the marks keep
#   their place relative to the eye. An OUTPUT ending in .mkv is tagged
#   stereo_mode=left_right, which Matroska also expresses as a display size of
#   one eye.
set -eu

dur=${1:-60}
out=${2:-pattern_sbs.mp4}
enc=${3:-libx265}
eye=${4:-2048x2048}

case $enc in
libx264) encopts="-c:v libx264 -preset ultrafast" ;;
libx265) encopts="-c:v libx265 -x265-params log-level=error" ;;
*) echo "make_pattern.sh: unsupported encoder '$enc' (libx264 or libx265)" >&2; exit 2 ;;
esac

case $out in
*.mkv) tag="-metadata:s:v stereo_mode=left_right" ;;
*) tag="" ;;
esac

# At 2048x2048: a 128-pixel band, and a 512x60 box 100 pixels above the centre.
marks="drawbox=x=0:y=0:w=iw:h=ih/16:color=yellow:t=fill"
marks="$marks,drawbox=x=iw*3/8:y=ih/2-ih*25/512:w=iw/4:h=ih*15/512:color=yellow:t=fill"

# shellcheck disable=SC2086 # encopts and tag are word lists on purpose
ffmpeg -nostdin -hide_banner -loglevel error -y \
       -f lavfi -i "color=c=red:s=$eye:r=30,drawgrid=w=128:h=128:t=2:c=white,$marks" \
       -f lavfi -i "color=c=blue:s=$eye:r=30,drawgrid=w=128:h=128:t=2:c=white,$marks" \
       -filter_complex hstack -t "$dur" $encopts -pix_fmt yuv420p $tag "$out"
