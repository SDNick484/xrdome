#!/bin/bash
# Builds the macOS test bundle and zips it: dist/xrdome-<version>-macos-arm64.zip.
# Run from CI (or a Mac with Homebrew's mpv and ffmpeg) after `meson compile -C build`.
#   packaging/macos/bundle.sh [BUILD_DIR [OUT_DIR]]     defaults: build, dist
# The work is in bundle.py; this adds the architecture check and the zip.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
build=${1:-build}
out=${2:-dist}
py=${PYTHON:-python3}

if [ "$(uname -s)-$(uname -m)" != "Darwin-arm64" ]; then
    echo "bundle.sh: this makes the Apple Silicon bundle; run it on an arm64 Mac" >&2
    exit 1
fi

"$py" "$here/bundle.py" stage --root "$root" --build "$build" --out "$out"
version=$("$py" "$here/bundle.py" version --root "$root")
zip_name="xrdome-$version-macos-arm64.zip"

# -y keeps symlinks, -X drops extra file attributes; modes (the exec bits) are kept.
rm -f "$out/$zip_name"
(cd "$out" && /usr/bin/zip -qryX "$zip_name" xrdome)
ls -l "$out/$zip_name"
