#!/usr/bin/env python3
"""Stage the Windows release folder: xrdome and ffprobe with their MSYS2 DLLs.

Standard library only (Python >= 3.11). Run in an MSYS2 UCRT64 shell (build.sh
does). Subcommands:

  version                  print the project version from meson.build
  stage                    build OUT/xrdome/ (see below)
  zip                      zip OUT/xrdome/ to OUT/xrdome-<version>-win64.zip

`stage` copies xrdome.exe and ffprobe.exe into OUT/xrdome/, finds every DLL
they need with `ntldd -R` and copies the ones that live in /ucrt64/bin beside
them (never a system DLL), maps each DLL to the pacman package that owns it,
and adds COPYING, LICENSES/ (the licence files of every bundled package),
SOURCES.md (each package's exact version and source tarball on
mirror.msys2.org) and README-windows.txt. It refuses an ffprobe built with
--enable-nonfree, and a bundled package whose licence is not open source.

All paths on the command line are Windows form with forward slashes
(`cygpath -m`), so a native or an MSYS Python reads them alike. The package
owner of a file is read from pacman's local database (the same data
`pacman -Qo` reads) rather than by running pacman, because pacman is an MSYS
program and would be handed Windows paths it does not take.

The pure parts (ntldd parsing, path matching, the package database, the
licence check, the SOURCES.md text, the zip) are unit tested on Linux in
tools/tests/test_windows_collect.py; ntldd and ffprobe are only run on Windows.
"""
from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import zipfile
from collections.abc import Callable, Iterable, Sequence
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # packaging/bundle_common.py
from bundle_common import (  # noqa: E402
    LICENSE_FILE_RE, REPO_URL, BundleError, check_nonfree as _check_nonfree, copy_own_licences,
    ffprobe_buildconf, sources_buildconf_lines, sources_xrdome_lines, version_from_meson)

PKG_PREFIX = "mingw-w64-ucrt-x86_64-"
SOURCES_URL = "https://mirror.msys2.org/mingw/sources/"
PACKAGES_URL = "https://packages.msys2.org/base/"
LICENSE_DIR = "ucrt64/share/licenses/"  # root-relative, as in pacman's file lists
DOC_DIR = "ucrt64/share/doc/"
TEXTS_DIR = Path(__file__).with_name("licenses")  # the FSF texts MSYS2's packages do not ship
ISSUE_URL = REPO_URL + "/issues"


# ---- paths ------------------------------------------------------------------


def norm_path(p: str) -> str:
    """A Windows path in a form two spellings of one file share: forward slashes,
    lower case (Windows ignores case), no trailing slash; /c/x becomes c:/x."""
    p = p.strip().replace("\\", "/")
    m = re.match(r"^/([A-Za-z])/(.*)$", p)
    if m:
        p = f"{m.group(1)}:/{m.group(2)}"
    return p.rstrip("/").lower()


def relative_to(path: str, root: str) -> str | None:
    """path below root as a lower-case relative path, or None when it is not below it."""
    p, r = norm_path(path), norm_path(root)
    return p[len(r) + 1:] if p.startswith(r + "/") else None


# ---- ntldd ------------------------------------------------------------------

NTLDD_RE = re.compile(r"^\s*(\S+)\s+=>\s+(.+?)(?:\s+\(0x[0-9a-fA-F]+\))?\s*$")
SYSTEM_NAME_RE = re.compile(r"^(api-ms-win-|ext-ms-win-)", re.IGNORECASE)


def parse_ntldd(text: str) -> list[tuple[str, str | None]]:
    """(name, path) for every line of `ntldd -R` output; path is None for
    "not found". The tree is flattened: a DLL listed under several parents
    appears more than once."""
    out: list[tuple[str, str | None]] = []
    for line in text.splitlines():
        m = NTLDD_RE.match(line)
        if not m:
            continue
        path = m.group(2).strip()
        out.append((m.group(1), None if path.lower() == "not found" else path))
    return out


def bundled_dlls(entries: Iterable[tuple[str, str | None]], bindir: str,
                 warn: Callable[[str], None] = lambda s: None) -> list[str]:
    """The file names (as spelled in bindir's listing, by ntldd's path) of the
    DLLs that live directly in bindir; everything else (Windows' own DLLs) is
    left out. A name ntldd could not find is reported through warn: the
    API-set names and a few delay-loaded system DLLs are listed that way, and
    the smoke test of the staged folder is what proves nothing is missing."""
    found: dict[str, str] = {}
    for name, path in entries:
        if path is None:
            if not SYSTEM_NAME_RE.match(name):
                warn(f"ntldd could not find {name}")
            continue
        rel = relative_to(path, bindir)
        if rel is not None and "/" not in rel:
            found.setdefault(rel, path.replace("\\", "/").rsplit("/", 1)[-1])
    return sorted(found.values(), key=str.lower)


# DLLs a bundled DLL loads by name at run time, which no import table (so no ntldd)
# shows. sdl2-compat's SDL2.dll is a shim that LoadLibrary()s SDL3.dll.
RUNTIME_DLLS = {"sdl2.dll": ["SDL3.dll"]}


def runtime_dlls(dlls: Iterable[str]) -> list[str]:
    """The run-time loaded DLLs that the DLLs in dlls call for, not already among them."""
    have = {d.lower() for d in dlls}
    return [x for d in sorted(have) for x in RUNTIME_DLLS.get(d, []) if x.lower() not in have]


# ---- pacman's local database ------------------------------------------------


@dataclass
class Package:
    name: str
    version: str
    base: str
    licenses: list[str]
    files: list[str] = field(default_factory=list)

    @property
    def short(self) -> str:
        return self.name[len(PKG_PREFIX):] if self.name.startswith(PKG_PREFIX) else self.name


def parse_db_section(text: str) -> dict[str, list[str]]:
    """A pacman database `desc` or `files` file: %KEY% lines, each followed by
    its values, one per line, up to a blank line."""
    out: dict[str, list[str]] = {}
    key = None
    for line in text.splitlines():
        s = line.strip()
        if re.fullmatch(r"%[A-Z0-9]+%", s):
            key = s.strip("%")
            out[key] = []
        elif s and key:
            out[key].append(s)
    return out


def read_local_db(dbpath: str | Path) -> list[Package]:
    """Every installed package under DBPATH/local/*/{desc,files}."""
    pkgs = []
    local = Path(dbpath) / "local"
    if not local.is_dir():
        raise BundleError(f"{local} is not pacman's local database (--dbpath)")
    for d in sorted(local.iterdir()):
        desc = d / "desc"
        if not desc.is_file():
            continue
        sec = parse_db_section(desc.read_text(encoding="utf-8", errors="replace"))
        name = (sec.get("NAME") or [""])[0]
        version = (sec.get("VERSION") or [""])[0]
        if not name or not version:
            continue
        files_path = d / "files"
        files = (parse_db_section(files_path.read_text(encoding="utf-8", errors="replace"))
                 .get("FILES", []) if files_path.is_file() else [])
        pkgs.append(Package(name, version, (sec.get("BASE") or [name])[0],
                            sec.get("LICENSE", []), files))
    return pkgs


def index_owners(pkgs: Iterable[Package]) -> dict[str, Package]:
    """Root-relative lower-case file path -> the package that owns it."""
    owners: dict[str, Package] = {}
    for p in pkgs:
        for f in p.files:
            if not f.endswith("/"):
                owners[f.lower()] = p
    return owners


def owner_of(owners: dict[str, Package], path: str, root: str) -> Package:
    rel = relative_to(path, root)
    pkg = owners.get(rel) if rel is not None else None
    if pkg is None:
        raise BundleError(f"{path}: no pacman package owns this file (is it under {root}?)")
    return pkg


# ---- licences ---------------------------------------------------------------

# Free licences by SPDX id or by the older names MSYS2 packages still use (after
# lower-casing and dropping "spdx:"). Anything else, an unreviewed "custom"
# included, is refused: the guard errs towards stopping the build.
OPEN_RE = re.compile(
    r"^(a?l?gpl[-0-9.]*(only|or-later|\+)?|gfdl[-0-9.]*(only|or-later)?|mit(-[a-z0-9-]+)?|x11|isc|"
    r"zlib|0bsd|bsd(-[0-9a-z-]+)?|apache[-0-9.]*|mpl[-0-9.]*|psf[-0-9.]*|python-2.0|wtfpl|"
    r"cc0[-0-9.]*|ftl|unlicense|bsl-1.0|boost|libpng(-2.0)?|ijg|publicdomain|"
    r"gcc-exception-[0-9.]+|lgpl-3.0-linking-exception|llvm-exception|curl|openssl|"
    r"ofl-1.1|bitstream-vera|zlib-acknowledgement)$")
OPERATORS = {"and", "or", "with", "(", ")"}

# Packages whose licence field is a bare "custom" (their own text in
# share/licenses/), with what a person found when reading it. A new one needs
# a person to read its licence and add a line here.
REVIEWED_CUSTOM = {
    "bzip2": "bzip2 licence (BSD-style)",
    "fontconfig": "fontconfig licence (MIT-style)",
    "gsm": "TU Berlin GSM licence (permissive, keeps the notice)",
    "libpng": "libpng licence (permissive)",
    "libvorbis": "BSD-3-Clause",
    "libx264": "GPL-2.0-or-later (x264)",
}


def license_problems(pkg: Package, reviewed: dict[str, str] = REVIEWED_CUSTOM) -> list[str]:
    """Why this package's licence field is not known to be open source (empty: it is)."""
    problems = []
    if not pkg.licenses:
        return [f"{pkg.name}: no licence in pacman's database"]
    for entry in pkg.licenses:
        text = entry.strip().lower()
        text = re.sub(r"^documentation:", "", text)
        if text == "custom":
            if pkg.short not in reviewed:
                problems.append(f"{pkg.name}: licence 'custom' has not been reviewed "
                                "(read share/licenses/ and add it to REVIEWED_CUSTOM)")
            continue
        text = re.sub(r"^custom:\s*", "", text).replace("public domain", "publicdomain")
        text = text.replace("(", " ( ").replace(")", " ) ")
        for tok in text.split():
            tok = re.sub(r"^spdx:", "", tok)
            if tok in OPERATORS:
                continue
            if not OPEN_RE.match(tok):
                problems.append(f"{pkg.name}: licence '{entry}' is not known to be open source "
                                f"({tok})")
                break
    return problems


def licence_summary(pkg: Package) -> str:
    text = " ".join(pkg.licenses)
    text = re.sub(r"(documentation:)?spdx:", "", text)
    if "custom" in text and pkg.short in REVIEWED_CUSTOM:
        text = text.replace("custom", f"custom ({REVIEWED_CUSTOM[pkg.short]})")
    return text.strip() or "unknown"


def check_nonfree(buildconf: str) -> None:
    _check_nonfree(buildconf, "use MSYS2's GPL ffmpeg package (pacman -S "
                   "mingw-w64-ucrt-x86_64-ffmpeg)")


def licence_files(pkg: Package) -> list[tuple[str, str]]:
    """(root-relative path, path below the package's directory) for the licence
    files a package installs: everything under share/licenses/<dir>/, and the
    COPYING-like files in share/doc/<dir>/ (some packages keep them there)."""
    out: list[tuple[str, str]] = []
    for prefix, only_named in ((LICENSE_DIR, False), (DOC_DIR, True)):
        for f in pkg.files:
            if not f.lower().startswith(prefix) or f.endswith("/"):
                continue
            below = f[len(prefix):].split("/", 1)
            if len(below) != 2 or (only_named and not LICENSE_FILE_RE.match(below[1])):
                continue
            if all(b != below[1] for _, b in out):
                out.append((f, below[1]))
    return out


def gnu_texts(pkgs: Iterable[Package]) -> list[str]:
    """The names of the FSF licence texts (in TEXTS_DIR) the bundled packages call for.
    MSYS2's FFmpeg and mpv install no licence file, and the GPL asks that its text
    goes with every copy; xrdome's own COPYING is version 2 only."""
    want = set()
    for p in pkgs:
        for lic in p.licenses:
            low = lic.lower()
            if re.search(r"(?<!l)gpl-3\.0", low):
                want.add("GPL-3.0.txt")
            if "lgpl-2.1" in low:
                want.add("LGPL-2.1.txt")
            if "lgpl-3.0" in low:
                want.add("LGPL-3.0.txt")
    return sorted(want)


# ---- SOURCES.md and README --------------------------------------------------


def source_tarball_url(pkg: Package) -> str:
    """The package's source archive (PKGBUILD, patches, the upstream tarball's
    name) on MSYS2's mirror: <pkgbase>-<version>.src.tar.zst, an epoch's ':' as '~'."""
    return f"{SOURCES_URL}{pkg.base}-{pkg.version.replace(':', '~')}.src.tar.zst"


def build_sources_md(version: str, commit: str, pkgs: Sequence[Package], buildconf: str) -> str:
    """SOURCES.md: the exact version of every bundled package and where its source is."""
    out = [
        f"# xrdome {version} (Windows x64): sources",
        "",
        *sources_xrdome_lines(commit),
        "",
        "Every DLL beside xrdome.exe, and ffprobe.exe, is an unmodified MSYS2 UCRT64 package",
        "(https://www.msys2.org). Each package's source archive (the PKGBUILD, its patches and",
        "the URL of the upstream source) is at the address below, for the version listed; the",
        "packages are built from it by MSYS2's CI. This list is the offer of corresponding source",
        f"for them. If a link is gone, ask at {ISSUE_URL} and a copy will be sent.",
        "",
        "| Package | Version | License (MSYS2) | Source | Package page | Licence files |",
        "|---|---|---|---|---|---|",
    ]
    for p in sorted(pkgs, key=lambda p: p.short):
        out.append(f"| {p.short} | {p.version} | {licence_summary(p)} | {source_tarball_url(p)} | "
                   f"{PACKAGES_URL}{p.base} | LICENSES/{p.short}/ |")
    out += sources_buildconf_lines(buildconf)
    if any("gpl-3.0" in lic.lower() for p in pkgs for lic in p.licenses):
        out += ["Some of these packages (MSYS2's FFmpeg among them) are GPL-3.0-or-later, so the",
                "bundle as a whole is distributed under the GNU GPL version 3 or later; xrdome's own terms",
                "(GPL-2.0-or-later) allow that. COPYING is xrdome's licence; each package's own",
                "licence files, GPL version 3 included, are in LICENSES/.", ""]
    return "\n".join(out)


def render_readme(template: str, version: str, built_on: str) -> str:
    return template.replace("@VERSION@", version).replace("@BUILT_ON@", built_on) \
                   .replace("@ISSUE_URL@", ISSUE_URL)


# ---- the zip ----------------------------------------------------------------


def make_zip(stage_dir: Path, zip_path: Path) -> int:
    """Zips stage_dir as <stage_dir name>/..., returns the number of files."""
    top = stage_dir.name
    n = 0
    zip_path.unlink(missing_ok=True)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for f in sorted(p for p in stage_dir.rglob("*") if p.is_file()):
            z.write(f, f"{top}/{f.relative_to(stage_dir).as_posix()}")
            n += 1
    return n


# ---- running tools (Windows only) -------------------------------------------


def run_ntldd(exe: str) -> str:
    p = subprocess.run(["ntldd", "-R", exe], capture_output=True, text=True)
    if p.returncode != 0:
        raise BundleError(f"ntldd -R {exe} failed ({p.returncode}): {p.stderr.strip()}")
    return p.stdout


def git_commit(root: Path) -> str:
    if os.environ.get("GITHUB_SHA"):
        return os.environ["GITHUB_SHA"]
    try:
        p = subprocess.run(["git", "-C", str(root), "rev-parse", "HEAD"], capture_output=True,
                           text=True)
        if p.returncode == 0 and p.stdout.strip():
            return p.stdout.strip()
    except OSError:
        pass
    return "unknown (not a git checkout)"


# ---- stage ------------------------------------------------------------------


def stage(root: Path, build: Path, out: Path, bindir: str, msys_root: str, dbpath: str, *,
          ntldd: Callable[[str], str] = run_ntldd,
          buildconf_of: Callable[[str], str] = ffprobe_buildconf,
          commit: str | None = None, built_on: str = "Windows") -> dict:
    """Builds out/xrdome/. ntldd and buildconf_of are injected by the tests."""
    xrdome = build / "xrdome.exe"
    ffprobe = Path(bindir) / "ffprobe.exe"
    for f in (xrdome, ffprobe):
        if not f.is_file():
            raise BundleError(f"{f} not found" + (": build first" if f == xrdome else
                                                   " (pacman -S mingw-w64-ucrt-x86_64-ffmpeg)"))
    buildconf = buildconf_of(str(ffprobe))
    check_nonfree(buildconf)
    version = version_from_meson((root / "meson.build").read_text(encoding="utf-8"))

    entries: list[tuple[str, str | None]] = []
    for exe in (xrdome, ffprobe):
        entries += parse_ntldd(ntldd(str(exe)))
    warnings: list[str] = []
    dlls = bundled_dlls(entries, bindir, warnings.append)
    if not dlls:
        raise BundleError(f"ntldd listed no DLL under {bindir}; the first lines of its output "
                          "were:\n  " + "\n  ".join(f"{n} => {p}" for n, p in entries[:5]))
    for extra in runtime_dlls(dlls):
        if not (Path(bindir) / extra).is_file():
            raise BundleError(f"{bindir}/{extra} not found: a bundled DLL loads it at run time")
        entries += parse_ntldd(ntldd(f"{bindir}/{extra}"))
    dlls = bundled_dlls(entries + [(n, f"{bindir}/{n}") for n in runtime_dlls(dlls)], bindir,
                        warnings.append)
    for w in sorted(set(warnings)):
        print(f"warning: {w}", file=sys.stderr)

    owners = index_owners(read_local_db(dbpath))
    pkgs: dict[str, Package] = {}
    for name in dlls + ["ffprobe.exe"]:
        p = owner_of(owners, f"{bindir}/{name}", msys_root)
        pkgs[p.name] = p
    problems = [m for p in pkgs.values() for m in license_problems(p)]
    if problems:
        raise BundleError("a bundled package is not known to be open source:\n  "
                          + "\n  ".join(problems))

    dest = out / "xrdome"
    shutil.rmtree(dest, ignore_errors=True)
    dest.mkdir(parents=True)
    shutil.copyfile(xrdome, dest / "xrdome.exe")
    shutil.copyfile(ffprobe, dest / "ffprobe.exe")
    for name in dlls:
        shutil.copyfile(Path(bindir) / name, dest / name)

    copy_own_licences(root, dest)
    for p in pkgs.values():
        d = dest / "LICENSES" / p.short
        d.mkdir(parents=True)
        for rel, below in licence_files(p):
            target = d / below
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(Path(msys_root) / rel, target)
        if not any(d.iterdir()):
            (d / "NO-LICENSE-FILE.txt").write_text(
                f"The {p.short} {p.version} package installs no licence file of its own. Its licence\n"
                "and source are named in SOURCES.md; the GNU licence texts are in LICENSES/.\n",
                encoding="utf-8")
    for name in gnu_texts(pkgs.values()):
        shutil.copyfile(TEXTS_DIR / name, dest / "LICENSES" / name)
    (dest / "SOURCES.md").write_text(
        build_sources_md(version, commit or git_commit(root), list(pkgs.values()), buildconf),
        encoding="utf-8")
    readme = (Path(__file__).with_name("README-windows.txt")).read_text(encoding="utf-8")
    (dest / "README-windows.txt").write_text(render_readme(readme, version, built_on),
                                              encoding="utf-8")
    return {"dir": dest, "version": version, "dlls": dlls, "packages": sorted(pkgs)}


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("version")
    p.add_argument("--root", default=".")
    p = sub.add_parser("stage")
    p.add_argument("--root", default=".")
    p.add_argument("--build", default="build")
    p.add_argument("--out", default="dist")
    p.add_argument("--bindir", required=True, help="cygpath -m /ucrt64/bin")
    p.add_argument("--msys-root", required=True, help="cygpath -m /")
    p.add_argument("--dbpath", required=True, help="cygpath -m /var/lib/pacman")
    p.add_argument("--built-on", default="Windows")
    p = sub.add_parser("zip")
    p.add_argument("--root", default=".")
    p.add_argument("--out", default="dist")
    args = ap.parse_args(argv)
    try:
        if args.cmd == "version":
            print(version_from_meson((Path(args.root) / "meson.build").read_text(encoding="utf-8")))
            return 0
        if args.cmd == "stage":
            r = stage(Path(args.root).resolve(), Path(args.build).resolve(),
                      Path(args.out).resolve(), args.bindir, args.msys_root, args.dbpath,
                      built_on=args.built_on)
            print(f"staged {r['dir']}: {len(r['dlls'])} DLLs from {len(r['packages'])} packages")
            return 0
        out = Path(args.out).resolve()
        version = version_from_meson((Path(args.root) / "meson.build").read_text(encoding="utf-8"))
        zip_path = out / f"xrdome-{version}-win64.zip"
        n = make_zip(out / "xrdome", zip_path)
        print(f"{zip_path}: {n} files, {zip_path.stat().st_size} bytes")
        return 0
    except BundleError as e:
        print(f"collect: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
