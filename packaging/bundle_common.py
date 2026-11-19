"""What the macOS and Windows bundle scripts share (standard library only).

packaging/macos/bundle.py and packaging/windows/collect.py both stage xrdome
and ffprobe with the libraries they load, refuse an ffprobe built with
--enable-nonfree, and write COPYING, LICENSES/ and SOURCES.md. The parts that do
not depend on the platform live here; the dependency walk, the package
manager (Homebrew, pacman) and the SOURCES.md table stay in each script.
Unit tested in tools/tests/test_macos_bundle.py and test_windows_collect.py.
"""
from __future__ import annotations

import re
import shutil
import subprocess
from pathlib import Path

REPO_URL = "https://github.com/SDNick484/xrdome"
LICENSE_FILE_RE = re.compile(r"^(COPYING|COPYRIGHT|LICEN[CS]E|NOTICE|AUTHORS)", re.IGNORECASE)


class BundleError(Exception):
    pass


def version_from_meson(text: str) -> str:
    m = re.search(r"project\s*\(.*?version\s*:\s*'([^']+)'", text, re.DOTALL)
    if not m:
        raise BundleError("no project version in meson.build")
    return m.group(1)


def check_nonfree(buildconf: str, hint: str = "use a GPL build") -> None:
    if "--enable-nonfree" in buildconf:
        raise BundleError(
            "ffprobe is built with --enable-nonfree and cannot be redistributed; " + hint
            + "\nits configure line:\n" + buildconf.strip())


def ffprobe_buildconf(ffprobe: str) -> str:
    try:
        p = subprocess.run([ffprobe, "-hide_banner", "-buildconf"], capture_output=True, text=True)
    except OSError as e:
        raise BundleError(f"cannot run {ffprobe}: {e}")
    if p.returncode != 0:
        raise BundleError(f"{ffprobe} -buildconf failed: {p.stderr.strip()}")
    return (p.stdout + p.stderr).strip()  # ffmpeg logs to stderr


def copy_own_licences(root: Path, out: Path) -> None:
    """COPYING (xrdome's LICENSE) and LICENSES/xrdome/ (the MIT-licensed sharpen shader)."""
    shutil.copyfile(root / "LICENSE", out / "COPYING")
    own = out / "LICENSES" / "xrdome"
    own.mkdir(parents=True)
    shutil.copyfile(root / "src/render/shaders/sharpen.frag", own / "sharpen.frag")


def sources_xrdome_lines(commit: str) -> list[str]:
    return [
        f"xrdome: {REPO_URL}, commit {commit}. It is GPL-2.0-or-later (see COPYING); the",
        "sharpening shader is a port of AMD FidelityFX CAS under the MIT licence (see",
        "LICENSES/xrdome/).",
    ]


def sources_buildconf_lines(buildconf: str) -> list[str]:
    """The ffprobe configure line, and (for a version 3 build) what that means."""
    out = ["", "## ffprobe build configuration", "", "```", buildconf.strip(), "```", ""]
    if "--enable-version3" in buildconf:
        out += ["This FFmpeg is built with --enable-gpl --enable-version3, so the libraries in",
                "this bundle are combined under GPL version 3 or later; xrdome's own terms",
                "(GPL-2.0-or-later) allow that. Nothing here is --enable-nonfree (the bundle",
                "script refuses it).", ""]
    return out
