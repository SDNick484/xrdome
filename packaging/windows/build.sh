#!/bin/bash
# Builds xrdome for Windows and zips it: dist/xrdome-<version>-win64.zip.
# Run in an MSYS2 "UCRT64" shell (the UCRT64 icon in the Start menu, or
# `msys2_shell.cmd -ucrt64`), from a checkout of xrdome; build.ps1 does that for
# you. CI runs the same script.
#
#   packaging/windows/build.sh [--package-only] [BUILD_DIR [OUT_DIR]]     defaults: build, dist
#
# Without --package-only it installs the build dependencies, runs the gate's
# meson configure, compile and test, then packages. With it the build in
# BUILD_DIR must already exist (CI builds and tests first, then packages that
# same build: no second build). Packaging stages dist/xrdome/ (see collect.py:
# xrdome.exe, ffprobe.exe, their MSYS2 DLLs, COPYING, LICENSES/, SOURCES.md,
# README-windows.txt) and zips it. It refuses an ffprobe built with
# --enable-nonfree and any bundled package whose licence is not open source.
set -euo pipefail

package_only=0
if [ "${1:-}" = "--package-only" ]; then
    package_only=1
    shift
fi
build=${1:-build}
out=${2:-dist}
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
py=${PYTHON:-python}

if [ "${MSYSTEM:-}" != "UCRT64" ]; then
    echo "build.sh: run this in an MSYS2 UCRT64 shell (MSYSTEM is '${MSYSTEM:-unset}')" >&2
    exit 1
fi

P=mingw-w64-ucrt-x86_64
# What packaging needs: ntldd walks the DLLs, python runs collect.py, git names the commit.
pkgs=($P-ntldd $P-python git)
if [ "$package_only" = 0 ]; then
    # What the build and the tests need (the same list as the CI job's).
    pkgs+=($P-gcc $P-meson $P-ninja $P-pkgconf $P-sdl2-compat $P-libepoxy $P-mpv $P-cmocka $P-ffmpeg)
fi
pacman -S --needed --noconfirm "${pkgs[@]}"

cd "$root"
if [ "$package_only" = 0 ]; then
    # --reconfigure keeps a repeat run working on an existing build directory.
    if [ -d "$build" ]; then
        meson setup --reconfigure "$build" -Dwerror=true
    else
        meson setup "$build" -Dwerror=true
    fi
    meson compile -C "$build"
    meson test -C "$build" --print-errorlogs
fi
if [ ! -f "$build/xrdome.exe" ]; then
    echo "build.sh: $build/xrdome.exe not found: build first (without --package-only)" >&2
    exit 1
fi

built_on="Windows (NT $(uname -s | sed 's/^[A-Za-z0-9_]*-//'))" # MSYS_NT-10.0-26100 -> 10.0-26100

# collect.py takes Windows-form paths (cygpath -m), so a native Python reads them.
"$py" "$here/collect.py" stage \
    --root "$(cygpath -m "$root")" --build "$(cygpath -m "$build")" --out "$(cygpath -m "$out")" \
    --bindir "$(cygpath -m /ucrt64/bin)" --msys-root "$(cygpath -m /)" \
    --dbpath "$(cygpath -m "$(pacman-conf DBPath)")" \
    --built-on "$built_on"
"$py" "$here/collect.py" zip --root "$(cygpath -m "$root")" --out "$(cygpath -m "$out")"
ls -l "$out"/xrdome-*-win64.zip
