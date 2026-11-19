#!/usr/bin/env python3
"""Stage the macOS test bundle: xrdome and ffprobe with their Homebrew libraries.

Standard library only (Python >= 3.11). Subcommands:

  version                  print the project version from meson.build
  stage                    build OUT/xrdome/ (see below)
  verify DIR               check that nothing in DIR loads from outside the bundle

`stage` copies xrdome and ffprobe into OUT/xrdome/, copies every non-system
dylib they load (transitively) into OUT/xrdome/lib/, rewrites each load path to
@executable_path/lib/<name>, signs everything ad hoc (Apple Silicon refuses
modified, unsigned code), and adds COPYING, LICENSES/, SOURCES.md and
README-macos.txt. It refuses an ffprobe built with --enable-nonfree. The zip
itself is made by bundle.sh.

The pure parts (otool parsing, path resolution, the dependency walk, the
non-free check, the SOURCES.md text) are unit tested on Linux in
tools/tests/test_macos_bundle.py; the rest only runs on a Mac.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from collections.abc import Callable, Sequence
from pathlib import Path

LIB_PREFIX = "@executable_path/lib/"
REPO_URL = "https://github.com/SDNick484/xrdome"
ISSUE_URL = REPO_URL + "/issues/65"
LICENSE_FILE_RE = re.compile(r"^(COPYING|COPYRIGHT|LICEN[CS]E|NOTICE|AUTHORS)", re.IGNORECASE)
KEG_RE = re.compile(r"/Cellar/([^/]+)/([^/]+)(?:/|$)")
MACHO_MAGIC = (b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe")  # 64-bit little endian, fat


class BundleError(Exception):
    pass


# ---- pure helpers -----------------------------------------------------------


def version_from_meson(text: str) -> str:
    m = re.search(r"project\s*\(.*?version\s*:\s*'([^']+)'", text, re.DOTALL)
    if not m:
        raise BundleError("no project version in meson.build")
    return m.group(1)


def parse_otool_deps(text: str) -> list[str]:
    """The install names listed by `otool -L FILE`, in order. The first line is
    the file's own name; a dylib also lists its own id (see parse_otool_id)."""
    deps = []
    for line in text.splitlines()[1:]:
        if not line[:1].isspace():
            continue
        m = re.match(r"\s*(.+?) \(compatibility version", line)
        if m:
            deps.append(m.group(1))
    return deps


def parse_otool_id(text: str) -> str | None:
    """`otool -D FILE`: the file name, then its install name (nothing for an executable)."""
    lines = [ln.strip() for ln in text.splitlines() if ln.strip()]
    return lines[1] if len(lines) >= 2 else None


def parse_otool_rpaths(text: str) -> list[str]:
    """The LC_RPATH entries of `otool -l FILE`."""
    rpaths = []
    want = False
    for line in text.splitlines():
        s = line.strip()
        if s.startswith("cmd "):
            want = s == "cmd LC_RPATH"
        elif want and s.startswith("path "):
            m = re.match(r"path (.+?) \(offset \d+\)", s)
            if m:
                rpaths.append(m.group(1))
            want = False
    return rpaths


def is_system(path: str) -> bool:
    return path.startswith(("/usr/lib/", "/System/Library/", "/Library/Apple/"))


def check_nonfree(buildconf: str) -> None:
    if "--enable-nonfree" in buildconf:
        raise BundleError(
            "ffprobe is built with --enable-nonfree and cannot be redistributed; "
            "use a GPL build (Homebrew's ffmpeg)")


def _expand(path: str, loader_dir: str, exe_dir: str) -> str:
    if path.startswith("@loader_path"):
        return loader_dir + path[len("@loader_path"):]
    if path.startswith("@executable_path"):
        return exe_dir + path[len("@executable_path"):]
    return path


def resolve_dep(dep: str, referrer: str, rpaths: Sequence[str], exe_dir: str,
                search_dirs: Sequence[str] = ()) -> str | None:
    """The real file an install name stands for, or None. @rpath names are
    tried against rpaths in order, then against search_dirs (Homebrew's lib)."""
    loader_dir = os.path.dirname(referrer)
    if dep.startswith("@rpath/"):
        rest = dep[len("@rpath/"):]
        cands = [os.path.join(_expand(r, loader_dir, exe_dir), rest) for r in rpaths]
        cands += [os.path.join(d, rest) for d in search_dirs]
    else:
        cands = [_expand(dep, loader_dir, exe_dir)]
    for c in cands:
        if os.path.isfile(c):
            return os.path.realpath(c)
    return None


def collect(roots: Sequence[str], deps_of: Callable[[str], list[str]],
            rpaths_of: Callable[[str], list[str]], search_dirs: Sequence[str] = (),
            extras: dict[str, str] | None = None
            ) -> tuple[dict[str, str], dict[str, list[tuple[str, str]]]]:
    """Walks the non-system dylibs the roots load, transitively.

    Returns (libs, edges). libs maps each bundled file name (the basename of
    the real file, so two names for one file bundle it once) to its real path.
    edges maps every walked file (a root, or a lib's real path) to its
    (old install name, new) pairs: new is a bundled file name, or an absolute
    system path for an @rpath name that resolves into the system. Two
    different real files with one basename are an error.

    extras maps a bundled file name to the real path of a library that nothing
    links but that is loaded at run time (see sdl3_extra): it is bundled under
    that name, and walked like any other library.
    """
    exe_dir = os.path.dirname(roots[0])
    libs: dict[str, str] = dict(extras or {})
    names = {real: name for name, real in libs.items()}  # a real file keeps its extra name
    edges: dict[str, list[tuple[str, str]]] = {}
    todo: list[tuple[str, list[str]]] = [(r, []) for r in roots] + [(r, []) for r in libs.values()]
    while todo:
        path, inherited = todo.pop()
        if path in edges:
            continue
        rpaths = list(rpaths_of(path)) + inherited  # dyld also tries its loaders' rpaths
        changes: list[tuple[str, str]] = []
        for dep in deps_of(path):
            if is_system(dep) or dep.startswith(LIB_PREFIX):
                continue
            real = resolve_dep(dep, path, rpaths, exe_dir, search_dirs)
            if real is None:
                raise BundleError(f"{path}: cannot find {dep}")
            if is_system(real):
                changes.append((dep, real))
                continue
            name = names.get(real) or os.path.basename(real)
            if libs.setdefault(name, real) != real:
                raise BundleError(f"two different libraries are both called {name}: "
                                  f"{libs[name]} and {real}")
            changes.append((dep, name))
            todo.append((real, rpaths))
        edges[path] = changes
    return libs, edges


def sdl3_extra(libs: dict[str, str], info: dict,
               prefix_of: Callable[[str], str]) -> dict[str, str]:
    """Homebrew's sdl2 is sdl2-compat: libSDL2's load-time constructor dlopen()s
    SDL3 (never linked, so otool does not list it) and, when that fails, shows a
    modal error dialog that nothing dismisses on a headless machine. It tries
    @loader_path/libSDL3.dylib first, so SDL3 goes in lib/ under that name.
    Returns {"libSDL3.dylib": real path} when a bundled libSDL2 comes from a
    formula that depends on sdl3, else {}."""
    for name, real in libs.items():
        if not re.match(r"libSDL2-.*\.dylib$", name):
            continue
        keg = formula_of(real)
        if keg is None or "sdl3" not in formula_record(info, keg[0]).get("dependencies", []):
            continue
        path = os.path.join(prefix_of("sdl3"), "lib", "libSDL3.dylib")
        if not os.path.isfile(path):
            raise BundleError(f"{keg[0]} loads SDL3 at run time but {path} is missing "
                              "(brew install sdl3)")
        return {"libSDL3.dylib": os.path.realpath(path)}
    return {}


def formula_of(real_path: str) -> tuple[str, str, str] | None:
    """(formula, version, keg directory) for a file inside Homebrew's Cellar."""
    m = KEG_RE.search(real_path)
    if not m:
        return None
    return m.group(1), m.group(2), real_path[:m.end()].rstrip("/")


def formula_record(info: dict, name: str) -> dict:
    """The entry for a formula in `brew info --json=v2 --installed`."""
    for f in info.get("formulae", []):
        if name in (f.get("name"), f.get("full_name")):
            return f
    return {"name": name}


def license_text(lic) -> str:
    if lic is None:
        return "not stated by Homebrew"
    if isinstance(lic, str):
        return lic
    return json.dumps(lic, sort_keys=True)  # brew's nested all_of / any_of form


def build_sources_md(version: str, commit: str, kegs: dict[str, str], info: dict,
                     buildconf: str) -> str:
    """SOURCES.md: where each bundled library's source is, by Homebrew formula."""
    out = [
        f"# xrdome {version} (macOS arm64 test bundle): sources",
        "",
        f"xrdome: {REPO_URL}, commit {commit}. It is GPL-2.0-or-later (see COPYING); the",
        "sharpening shader is a port of AMD FidelityFX CAS under the MIT licence (see",
        "LICENSES/xrdome/).",
        "",
        "The libraries in lib/ and the ffprobe program are unmodified Homebrew bottles.",
        "Each formula's source is at the URL below, at the version listed; the build",
        "recipe is the formula in Homebrew's tap (https://github.com/Homebrew/homebrew-core).",
        "",
        "| Formula | Version | License (Homebrew) | Source | Licence files |",
        "|---|---|---|---|---|",
    ]
    for name in sorted(kegs):
        rec = formula_record(info, name)
        url = ((rec.get("urls") or {}).get("stable") or {}).get("url", "unknown")
        out.append(f"| {name} | {kegs[name]} | {license_text(rec.get('license'))} | {url} | "
                   f"LICENSES/{name}/ |")
    out += ["", "## ffprobe build configuration", "", "```", buildconf.strip(), "```", ""]
    if "--enable-version3" in buildconf:
        out += ["This FFmpeg is built with --enable-gpl --enable-version3, so the libraries in",
                "this bundle are combined under GPL version 3 or later; xrdome's own terms",
                "(GPL-2.0-or-later) allow that. Nothing here is --enable-nonfree (the bundle",
                "script refuses it).", ""]
    return "\n".join(out)


# ---- running tools (a Mac only) ---------------------------------------------


def run(argv: list[str]) -> str:
    p = subprocess.run(argv, capture_output=True, text=True)
    if p.returncode != 0:
        raise BundleError(f"{' '.join(argv)} failed ({p.returncode}): {p.stderr.strip()}")
    return p.stdout


def otool_deps(path: str) -> list[str]:
    deps = parse_otool_deps(run(["otool", "-L", path]))
    ident = parse_otool_id(run(["otool", "-D", path]))
    if ident in deps:
        deps.remove(ident)
    return deps


def otool_rpaths(path: str) -> list[str]:
    return parse_otool_rpaths(run(["otool", "-l", path]))


def brew_lib_dirs() -> list[str]:
    return [run(["brew", "--prefix"]).strip() + "/lib"]


def ffprobe_buildconf(ffprobe: str) -> str:
    p = subprocess.run([ffprobe, "-hide_banner", "-buildconf"], capture_output=True, text=True)
    if p.returncode != 0:
        raise BundleError(f"{ffprobe} -buildconf failed: {p.stderr.strip()}")
    return (p.stdout + p.stderr).strip()  # ffmpeg logs to stderr


def copy_writable(src: str, dst: Path) -> None:
    shutil.copyfile(src, dst)
    os.chmod(dst, 0o755)  # Homebrew's files are read-only; install_name_tool writes


def rewrite(dst: Path, ident: str | None, changes: list[tuple[str, str]],
            rpaths: list[str]) -> None:
    argv = ["install_name_tool"]
    if ident:
        argv += ["-id", LIB_PREFIX + ident]
    for old, new in changes:
        argv += ["-change", old, new if new.startswith("/") else LIB_PREFIX + new]
    for r in rpaths:
        if not r.startswith("@") and not is_system(r):
            argv += ["-delete_rpath", r]
    if len(argv) > 1:
        run(argv + [str(dst)])


def verify(root: Path) -> list[str]:
    """Problems in every Mach-O file under root: an install name that is neither
    a system library nor a file in lib/, or an rpath outside the bundle."""
    problems = []
    for f in sorted(p for p in root.rglob("*") if p.is_file() and not p.is_symlink()):
        with open(f, "rb") as fh:
            if fh.read(4) not in MACHO_MAGIC:
                continue
        ident = parse_otool_id(run(["otool", "-D", str(f)]))
        for dep in parse_otool_deps(run(["otool", "-L", str(f)])):
            if dep == ident or is_system(dep):
                continue
            if dep.startswith(LIB_PREFIX) and (root / "lib" / dep[len(LIB_PREFIX):]).is_file():
                continue
            problems.append(f"{f.relative_to(root)}: loads {dep}")
        for r in otool_rpaths(str(f)):
            if not r.startswith("@") and not is_system(r):
                problems.append(f"{f.relative_to(root)}: rpath {r}")
    return problems


# ---- stage ------------------------------------------------------------------


def stage(args: argparse.Namespace) -> int:
    root = Path(args.root).resolve()
    out = Path(args.out).resolve() / "xrdome"
    xrdome = os.path.realpath(Path(args.build) / "xrdome")
    ffprobe = os.path.realpath(args.ffprobe or shutil.which("ffprobe") or "")
    if not os.path.isfile(xrdome):
        raise BundleError(f"{xrdome} not found: build first")
    if not os.path.isfile(ffprobe):
        raise BundleError("ffprobe not found (brew install ffmpeg, or --ffprobe PATH)")

    buildconf = ffprobe_buildconf(ffprobe)
    check_nonfree(buildconf)
    version = version_from_meson((root / "meson.build").read_text())

    shutil.rmtree(out, ignore_errors=True)
    (out / "lib").mkdir(parents=True)

    info = json.loads(run(["brew", "info", "--json=v2", "--installed"]))
    roots = [xrdome, ffprobe]
    libs, edges = collect(roots, otool_deps, otool_rpaths, brew_lib_dirs())
    extras = sdl3_extra(libs, info, lambda f: run(["brew", "--prefix", f]).strip())
    if extras:  # again, with the run-time library and what it loads
        libs, edges = collect(roots, otool_deps, otool_rpaths, brew_lib_dirs(), extras)

    # Copy, rewrite the copies' load paths, sign them.
    targets = {xrdome: out / "xrdome", ffprobe: out / "ffprobe"}
    for name, real in libs.items():
        targets[real] = out / "lib" / name
    for src, dst in targets.items():
        copy_writable(src, dst)
    for src, dst in targets.items():
        ident = dst.name if dst.parent.name == "lib" else None
        rewrite(dst, ident, edges[src], otool_rpaths(src))
    for dst in targets.values():
        run(["codesign", "--force", "-s", "-", str(dst)])
        run(["codesign", "--verify", "--strict", str(dst)])

    problems = verify(out)
    if problems:
        raise BundleError("the bundle still reaches outside itself:\n  " + "\n  ".join(problems))

    # Licences and sources.
    shutil.copyfile(root / "LICENSE", out / "COPYING")
    own = out / "LICENSES" / "xrdome"
    own.mkdir(parents=True)
    shutil.copyfile(root / "src/render/shaders/sharpen.frag", own / "sharpen.frag")
    kegs: dict[str, str] = {}
    for real in [ffprobe] + list(libs.values()):
        f = formula_of(real)
        if f is None:
            raise BundleError(f"{real} is not from a Homebrew formula: no source to list")
        name, ver, keg = f
        if name in kegs:
            continue
        kegs[name] = ver
        dest = out / "LICENSES" / name
        dest.mkdir(parents=True)
        for entry in sorted(os.listdir(keg)):
            src = Path(keg) / entry
            if LICENSE_FILE_RE.match(entry) and src.is_file():
                shutil.copyfile(src, dest / entry)
        if not any(dest.iterdir()):
            (dest / "NO-LICENSE-FILE.txt").write_text(
                f"The {name} {ver} bottle has no licence file; see SOURCES.md.\n")
    commit = os.environ.get("GITHUB_SHA") or run(["git", "-C", str(root), "rev-parse", "HEAD"]).strip()
    (out / "SOURCES.md").write_text(build_sources_md(version, commit, kegs, info, buildconf))

    macos = run(["sw_vers", "-productVersion"]).strip()
    readme = Path(__file__).with_name("README-macos.txt").read_text()
    (out / "README-macos.txt").write_text(
        readme.replace("@VERSION@", version).replace("@BUILT_ON@", f"macOS {macos}")
              .replace("@ISSUE_URL@", ISSUE_URL))
    print(f"staged {out}: {len(libs)} libraries from {len(kegs)} formulae")
    return 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("version")
    p.add_argument("--root", default=".")
    p = sub.add_parser("stage")
    p.add_argument("--root", default=".")
    p.add_argument("--build", default="build")
    p.add_argument("--out", default="dist")
    p.add_argument("--ffprobe", help="default: ffprobe on PATH")
    p = sub.add_parser("verify")
    p.add_argument("dir")
    args = ap.parse_args(argv)
    try:
        if args.cmd == "version":
            print(version_from_meson((Path(args.root) / "meson.build").read_text()))
            return 0
        if args.cmd == "stage":
            return stage(args)
        problems = verify(Path(args.dir))
        for line in problems:
            print(line, file=sys.stderr)
        return 1 if problems else 0
    except BundleError as e:
        print(f"bundle: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
