#!/bin/sh
# Writes a test pattern.
#
# usage: tools/make_pattern.sh [--kind KIND] [DURATION_S [OUTPUT [ENCODER [EYE_WxH]]]]
#   KIND (default sbs180):
#   sbs180     side-by-side VR180: left eye red grid, right eye blue grid, each
#              EYE_WxH (default 2048x2048). Each eye has a yellow band along its
#              top edge and a yellow box just above its centre, so a vertical
#              flip is visible both in the file and straight ahead on the glasses
#              (the box sits 3.5 to 8.8 degrees above the horizon, inside the
#              Viture Pro's ~22 degree vertical FOV).
#   flat2d     ordinary flat video, 1920x1080: green field, white 8-pixel border,
#              a yellow box above the centre, and a 256x256 block of 1-pixel
#              black and white lines at the centre for judging shimmer.
#   flat-hsbs  half side-by-side 3D, 1920x1080: the left half red, the right half
#              blue, each a 16:9 picture with the flat2d marks squeezed to half
#              width.
#   flat-tb    full top-bottom 3D, 1920x2160: the top half red, the bottom half
#              blue, each a 1920x1080 picture with the flat2d marks.
#   Defaults: 60, pattern_sbs.mp4, libx265. ENCODER libx264 is much faster (used
#   by the smoke tests). EYE_WxH is each eye's size for sbs180 (default
#   2048x2048), the picture's size for flat2d (default 1920x1080) and each
#   half's size for flat-hsbs (default 960x1080); flat-tb ignores it. The marks
#   keep their place relative to the eye, drawn at 1920x1080 and scaled. A flat
#   picture of a size given here also gets a white border at least 4 pixels
#   wide after scaling, so a filter reaching a few pixels across a stereo seam
#   (including the 4:2:0 chroma of the encode) only reaches white. An OUTPUT ending in .mkv is tagged
#   stereo_mode=left_right, which Matroska also expresses as a display size of
#   one eye.
set -eu

kind=sbs180
if [ "${1:-}" = --kind ]; then
    [ $# -ge 2 ] || { echo "make_pattern.sh: --kind needs a value" >&2; exit 2; }
    kind=$2
    shift 2
fi

dur=${1:-60}
out=${2:-pattern_sbs.mp4}
enc=${3:-libx265}
eye=${4:-}

case $enc in
libx264) encopts="-c:v libx264 -preset ultrafast" ;;
libx265) encopts="-c:v libx265 -x265-params log-level=error" ;;
*) echo "make_pattern.sh: unsupported encoder '$enc' (libx264 or libx265)" >&2; exit 2 ;;
esac

case $out in
*.mkv) tag="-metadata:s:v stereo_mode=left_right" ;;
*) tag="" ;;
esac

# flat_eye COLOUR SIZE [EXTRA]: a 16:9 flat picture with the flat2d marks, drawn
# at 1920x1080 and scaled (squeezed) to SIZE, with square pixels as in a typical
# half side-by-side file (scale alone would declare the squeeze as pixel aspect).
# EXTRA is appended to the filter chain after scaling.
flat_eye() {
    printf '%s' "color=c=$1:s=1920x1080:r=30,drawbox=x=0:y=0:w=iw:h=ih:color=white:t=8"
    printf '%s' ",drawbox=x=iw*3/8:y=ih*5/16:w=iw/4:h=ih/16:color=yellow:t=fill[bg$1];"
    printf '%s' "color=c=black:s=256x256:r=30,drawgrid=w=2:h=256:t=1:c=white[lines$1];"
    printf '%s' "[bg$1][lines$1]overlay=x=(W-w)/2:y=(H-h)/2,scale=$2,setsar=1${3:-}"
}

case $kind in
sbs180)
    eye=${eye:-2048x2048}
    # At 2048x2048: a 128-pixel band, and a 512x60 box 100 pixels above the centre.
    marks="drawbox=x=0:y=0:w=iw:h=ih/16:color=yellow:t=fill"
    marks="$marks,drawbox=x=iw*3/8:y=ih/2-ih*25/512:w=iw/4:h=ih*15/512:color=yellow:t=fill"
    # shellcheck disable=SC2086 # encopts and tag are word lists on purpose
    ffmpeg -nostdin -hide_banner -loglevel error -y \
           -f lavfi -i "color=c=red:s=$eye:r=30,drawgrid=w=128:h=128:t=2:c=white,$marks" \
           -f lavfi -i "color=c=blue:s=$eye:r=30,drawgrid=w=128:h=128:t=2:c=white,$marks" \
           -filter_complex hstack -t "$dur" $encopts -pix_fmt yuv420p $tag "$out"
    exit 0
    ;;
flat2d | flat-hsbs)
    # Only a size given here gets the post-scaling border: the default patterns stay as they were.
    border=${eye:+,drawbox=x=0:y=0:w=iw:h=ih:color=white:t=4}
    if [ "$kind" = flat2d ]; then
        graph="$(flat_eye 0x00b000 "${eye:-1920x1080}" "$border")[v]"
    else
        half=${eye:-960x1080}
        graph="$(flat_eye red "$half" "$border")[l];$(flat_eye blue "$half" "$border")[r];[l][r]hstack[v]"
    fi
    ;;
flat-tb) graph="$(flat_eye red 1920x1080)[t];$(flat_eye blue 1920x1080)[b];[t][b]vstack[v]" ;;
*)
    echo "make_pattern.sh: unknown kind '$kind' (sbs180, flat2d, flat-hsbs or flat-tb)" >&2
    exit 2
    ;;
esac

# shellcheck disable=SC2086 # encopts and tag are word lists on purpose
ffmpeg -nostdin -hide_banner -loglevel error -y -filter_complex "$graph" -map '[v]' \
       -t "$dur" $encopts -pix_fmt yuv420p $tag "$out"
