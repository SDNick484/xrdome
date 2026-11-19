"""packaging/macos/bundle.py: the parts that run on any machine (otool text,
path resolution, the dependency walk, the non-free guard, SOURCES.md)."""
import importlib.util
import os
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
BUNDLE_DIR = ROOT / "packaging" / "macos"
spec = importlib.util.spec_from_file_location("macos_bundle", BUNDLE_DIR / "bundle.py")
b = importlib.util.module_from_spec(spec)
sys.modules["macos_bundle"] = b
spec.loader.exec_module(b)

OTOOL_L_EXE = """\
build/xrdome:
\t/opt/homebrew/opt/mpv/lib/libmpv.2.dylib (compatibility version 2.0.0, current version 2.5.0)
\t@rpath/libSDL2-2.0.0.dylib (compatibility version 3001.0.0, current version 3001.2.0)
\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0, current version 1351.0.0)
\t/System/Library/Frameworks/Cocoa.framework/Versions/A/Cocoa (compatibility version 1.0.0, current version 23.0.0)
"""

OTOOL_L_DYLIB = """\
/opt/homebrew/Cellar/mpv/0.40.0/lib/libmpv.2.dylib:
\t/opt/homebrew/opt/mpv/lib/libmpv.2.dylib (compatibility version 2.0.0, current version 2.5.0)
\t/opt/homebrew/opt/ffmpeg/lib/libavcodec.62.dylib (compatibility version 62.0.0, current version 62.3.100)
"""

OTOOL_LOADS = """\
Load command 12
          cmd LC_LOAD_DYLIB
         cmdsize 56
         name /usr/lib/libSystem.B.dylib (offset 24)
Load command 18
          cmd LC_RPATH
      cmdsize 48
         path /opt/homebrew/lib (offset 12)
Load command 19
          cmd LC_RPATH
      cmdsize 40
         path @loader_path/../lib (offset 12)
"""


def test_parse_deps_skips_the_file_line():
    assert b.parse_otool_deps(OTOOL_L_EXE) == [
        "/opt/homebrew/opt/mpv/lib/libmpv.2.dylib",
        "@rpath/libSDL2-2.0.0.dylib",
        "/usr/lib/libSystem.B.dylib",
        "/System/Library/Frameworks/Cocoa.framework/Versions/A/Cocoa",
    ]
    assert len(b.parse_otool_deps(OTOOL_L_DYLIB)) == 2


def test_parse_id():
    assert b.parse_otool_id("/x/libmpv.2.dylib:\n/opt/homebrew/opt/mpv/lib/libmpv.2.dylib\n") \
        == "/opt/homebrew/opt/mpv/lib/libmpv.2.dylib"
    assert b.parse_otool_id("build/xrdome:\n") is None


def test_parse_rpaths_only_lc_rpath():
    assert b.parse_otool_rpaths(OTOOL_LOADS) == ["/opt/homebrew/lib", "@loader_path/../lib"]


def test_is_system():
    assert b.is_system("/usr/lib/libz.1.dylib")
    assert b.is_system("/System/Library/Frameworks/Metal.framework/Metal")
    assert not b.is_system("/opt/homebrew/opt/zlib/lib/libz.1.dylib")
    assert not b.is_system("/usr/local/lib/libfoo.dylib")
    assert not b.is_system("@rpath/libfoo.dylib")


def test_nonfree_guard():
    b.check_nonfree("configuration: --enable-gpl --enable-version3 --enable-libx264")
    with pytest.raises(b.BundleError, match="nonfree"):
        b.check_nonfree("configuration: --enable-gpl --enable-nonfree --enable-libfdk-aac")


def test_version_from_meson():
    text = "project('xrdome', 'c',\n  version : '1.2.3',\n  license : 'GPL')\n"
    assert b.version_from_meson(text) == "1.2.3"
    assert b.version_from_meson((ROOT / "meson.build").read_text())
    with pytest.raises(b.BundleError):
        b.version_from_meson("project('x')")


def test_formula_of():
    p = "/opt/homebrew/Cellar/ffmpeg/8.0.1/lib/libavcodec.62.dylib"
    assert b.formula_of(p) == ("ffmpeg", "8.0.1", "/opt/homebrew/Cellar/ffmpeg/8.0.1")
    assert b.formula_of("/opt/homebrew/Cellar/openssl@3/3.6.0/lib/libssl.3.dylib")[0] == "openssl@3"
    assert b.formula_of("/usr/lib/libz.dylib") is None


def test_license_text():
    assert b.license_text("GPL-2.0-or-later") == "GPL-2.0-or-later"
    assert b.license_text(None) == "not stated by Homebrew"
    assert "any_of" in b.license_text({"any_of": ["MIT", "Apache-2.0"]})


# ---- resolution and the walk, on a fake Homebrew tree ------------------------


def touch(p: Path) -> Path:
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(b"x")
    return p


@pytest.fixture
def brew(tmp_path):
    cellar = tmp_path / "Cellar"
    prefix = tmp_path / "prefix"
    (prefix / "lib").mkdir(parents=True)
    (prefix / "opt").mkdir()
    return tmp_path, cellar, prefix


def link_opt(prefix: Path, cellar: Path, formula: str, ver: str):
    (prefix / "opt" / formula).symlink_to(cellar / formula / ver)


def test_resolve_absolute_follows_symlinks(brew):
    _, cellar, prefix = brew
    real = touch(cellar / "mpv/0.40.0/lib/libmpv.2.40.0.dylib")
    (real.parent / "libmpv.2.dylib").symlink_to(real.name)
    link_opt(prefix, cellar, "mpv", "0.40.0")
    got = b.resolve_dep(str(prefix / "opt/mpv/lib/libmpv.2.dylib"), "/x/xrdome", [], "/x")
    assert got == str(real.resolve())
    assert b.resolve_dep(str(prefix / "opt/none/lib/x.dylib"), "/x/xrdome", [], "/x") is None


def test_resolve_rpath_loader_and_search_dirs(brew):
    tmp, cellar, prefix = brew
    lib = touch(cellar / "sdl2/2.32/lib/libSDL2-2.0.0.dylib")
    ref = touch(cellar / "mpv/0.40.0/lib/libmpv.2.dylib")
    # through an rpath written relative to the referrer
    assert b.resolve_dep("@rpath/libSDL2-2.0.0.dylib", str(ref),
                         ["@loader_path/../../../sdl2/2.32/lib"], "/x") == str(lib.resolve())
    # through Homebrew's lib directory when the rpath list does not help
    touch(prefix / "lib/libfoo.dylib")
    assert b.resolve_dep("@rpath/libfoo.dylib", str(ref), ["/nowhere"], "/x",
                         [str(prefix / "lib")]) == str((prefix / "lib/libfoo.dylib").resolve())
    assert b.resolve_dep("@rpath/libbar.dylib", str(ref), [], "/x", [str(prefix / "lib")]) is None


def test_collect_is_transitive_dedupes_and_rewrites(brew):
    tmp, cellar, prefix = brew
    exe = touch(tmp / "build/xrdome")
    probe = touch(cellar / "ffmpeg/8.0/bin/ffprobe")
    mpv = touch(cellar / "mpv/0.40.0/lib/libmpv.2.40.0.dylib")
    avc = touch(cellar / "ffmpeg/8.0/lib/libavcodec.62.dylib")
    z = touch(cellar / "zlib/1.3/lib/libz.1.dylib")
    link_opt(prefix, cellar, "mpv", "0.40.0")
    link_opt(prefix, cellar, "ffmpeg", "8.0")
    link_opt(prefix, cellar, "zlib", "1.3")
    (mpv.parent / "libmpv.2.dylib").symlink_to(mpv.name)
    sys_z = "/usr/lib/libSystem.B.dylib"
    graph = {
        str(exe): [f"{prefix}/opt/mpv/lib/libmpv.2.dylib", f"{prefix}/opt/ffmpeg/lib/libavcodec.62.dylib", sys_z],
        str(probe.resolve()): [f"{prefix}/opt/ffmpeg/lib/libavcodec.62.dylib", sys_z],
        str(mpv.resolve()): [f"{prefix}/opt/ffmpeg/lib/libavcodec.62.dylib", "@rpath/libz.1.dylib", sys_z],
        str(avc.resolve()): [f"{prefix}/opt/zlib/lib/libz.1.dylib"],
        str(z.resolve()): ["/usr/lib/libz.1.dylib"],
    }
    rpaths = {str(mpv.resolve()): [f"{prefix}/opt/zlib/lib"]}
    libs, edges = b.collect([str(exe), str(probe.resolve())], lambda p: graph[p],
                            lambda p: rpaths.get(p, []), [str(prefix / "lib")])
    assert libs == {"libmpv.2.40.0.dylib": str(mpv.resolve()),
                    "libavcodec.62.dylib": str(avc.resolve()),
                    "libz.1.dylib": str(z.resolve())}
    assert (f"{prefix}/opt/mpv/lib/libmpv.2.dylib", "libmpv.2.40.0.dylib") in edges[str(exe)]
    assert all(new != sys_z for _, new in edges[str(exe)])  # system libraries stay
    assert ("@rpath/libz.1.dylib", "libz.1.dylib") in edges[str(mpv.resolve())]
    assert edges[str(z.resolve())] == []


def test_collect_reports_what_it_cannot_find(brew):
    tmp, _, _ = brew
    exe = touch(tmp / "xrdome")
    with pytest.raises(b.BundleError, match="libmissing"):
        b.collect([str(exe)], lambda p: ["/opt/homebrew/opt/x/lib/libmissing.dylib"], lambda p: [])


def test_collect_rejects_two_libraries_with_one_name(brew):
    tmp, cellar, _ = brew
    exe = touch(tmp / "xrdome")
    a = touch(cellar / "a/1/lib/libdup.dylib")
    c = touch(cellar / "c/1/lib/libdup.dylib")
    with pytest.raises(b.BundleError, match="libdup"):
        b.collect([str(exe)], lambda p: [str(a), str(c)] if p == str(exe) else [], lambda p: [])


def test_collect_rpath_to_system_library_becomes_absolute(brew):
    tmp, _, _ = brew
    exe = touch(tmp / "xrdome")
    # A path under /usr/lib that exists on the machine running the test is not
    # guaranteed, so fake the file check through a real "system" directory name.
    sysdir = tmp / "usr" / "lib"
    touch(sysdir / "libc++.1.dylib")
    orig = b.is_system
    b.is_system = lambda p: p.startswith(str(sysdir)) or orig(p)
    try:
        libs, edges = b.collect([str(exe)], lambda p: ["@rpath/libc++.1.dylib"],
                                lambda p: [str(sysdir)] if p == str(exe) else [])
    finally:
        b.is_system = orig
    assert libs == {}
    assert edges[str(exe)] == [("@rpath/libc++.1.dylib", str((sysdir / "libc++.1.dylib").resolve()))]


def test_sources_md_lists_formulae_and_urls():
    info = {"formulae": [
        {"name": "ffmpeg", "license": "GPL-2.0-or-later",
         "urls": {"stable": {"url": "https://ffmpeg.org/releases/ffmpeg-8.0.tar.xz"}}},
        {"name": "zlib", "license": "Zlib", "urls": {"stable": {"url": "https://zlib.net/zlib-1.3.tar.gz"}}},
        {"name": "unrelated", "urls": {"stable": {"url": "https://example.invalid/x"}}},
    ]}
    md = b.build_sources_md("0.1.0", "abc123", {"ffmpeg": "8.0", "zlib": "1.3", "libfoo": "2"},
                            info, "configuration: --enable-gpl --enable-version3")
    assert "| ffmpeg | 8.0 | GPL-2.0-or-later | https://ffmpeg.org/releases/ffmpeg-8.0.tar.xz |" in md
    assert "| zlib | 1.3 | Zlib | https://zlib.net/zlib-1.3.tar.gz |" in md
    assert "| libfoo | 2 | not stated by Homebrew | unknown |" in md  # missing from brew info
    assert "unrelated" not in md
    assert "commit abc123" in md and "GPL version 3" in md
    plain = b.build_sources_md("0.1.0", "abc", {}, {}, "configuration: --enable-gpl")
    assert "GPL version 3" not in plain


def test_rewrite_command_line(monkeypatch):
    seen = []
    monkeypatch.setattr(b, "run", lambda argv: seen.append(argv) or "")
    b.rewrite(Path("/o/lib/libmpv.dylib"), "libmpv.dylib",
              [("/opt/homebrew/opt/x/lib/libx.dylib", "libx.dylib"), ("@rpath/libc++.dylib", "/usr/lib/libc++.1.dylib")],
              ["/opt/homebrew/lib", "@loader_path/x", "/usr/lib/swift"])
    assert seen == [[
        "install_name_tool", "-id", "@executable_path/lib/libmpv.dylib",
        "-change", "/opt/homebrew/opt/x/lib/libx.dylib", "@executable_path/lib/libx.dylib",
        "-change", "@rpath/libc++.dylib", "/usr/lib/libc++.1.dylib",
        "-delete_rpath", "/opt/homebrew/lib", "/o/lib/libmpv.dylib"]]
    seen.clear()
    b.rewrite(Path("/o/ffprobe"), None, [], [])
    assert seen == []  # nothing to change, no call


def test_bundle_sh_is_valid_and_refuses_other_platforms():
    sh = BUNDLE_DIR / "bundle.sh"
    assert os.access(sh, os.X_OK)
    assert subprocess.run(["bash", "-n", str(sh)]).returncode == 0
    if os.uname().sysname != "Darwin":
        p = subprocess.run([str(sh)], capture_output=True, text=True)
        assert p.returncode != 0 and "arm64 Mac" in p.stderr


def test_version_subcommand_and_template(capsys):
    assert b.main(["version", "--root", str(ROOT)]) == 0
    assert capsys.readouterr().out.strip() == b.version_from_meson((ROOT / "meson.build").read_text())
    text = (BUNDLE_DIR / "README-macos.txt").read_text()
    for token in ("@VERSION@", "@BUILT_ON@", "@ISSUE_URL@", "xattr -dr com.apple.quarantine"):
        assert token in text


# ---- SDL3: loaded at run time by sdl2-compat, so nothing links it -----------


def sdl_tree(brew, sdl2_deps=("sdl3",)):
    tmp, cellar, prefix = brew
    sdl2 = touch(cellar / "sdl2/2.32.60/lib/libSDL2-2.0.0.dylib")
    sdl3 = touch(cellar / "sdl3/3.4.0/lib/libSDL3.0.dylib")
    (sdl3.parent / "libSDL3.dylib").symlink_to(sdl3.name)
    link_opt(prefix, cellar, "sdl3", "3.4.0")
    info = {"formulae": [{"name": "sdl2", "dependencies": list(sdl2_deps)}]}
    return sdl2, sdl3, info, (lambda f: str(prefix / "opt" / f))


def test_sdl3_extra_for_sdl2_compat(brew):
    sdl2, sdl3, info, prefix_of = sdl_tree(brew)
    libs = {"libSDL2-2.0.0.dylib": str(sdl2.resolve())}
    # Named as sdl2-compat's loader asks for it (@loader_path/libSDL3.dylib).
    assert b.sdl3_extra(libs, info, prefix_of) == {"libSDL3.dylib": str(sdl3.resolve())}


def test_sdl3_extra_not_for_a_real_sdl2_or_no_sdl2(brew):
    sdl2, _, info, prefix_of = sdl_tree(brew, sdl2_deps=())
    assert b.sdl3_extra({"libSDL2-2.0.0.dylib": str(sdl2.resolve())}, info, prefix_of) == {}
    assert b.sdl3_extra({"libmpv.2.dylib": str(sdl2.resolve())}, info, prefix_of) == {}


def test_sdl3_extra_missing_library_is_an_error(brew):
    tmp, cellar, prefix = brew
    sdl2 = touch(cellar / "sdl2/2.32.60/lib/libSDL2-2.0.0.dylib")
    info = {"formulae": [{"name": "sdl2", "dependencies": ["sdl3"]}]}
    with pytest.raises(b.BundleError, match="sdl3"):
        b.sdl3_extra({"libSDL2-2.0.0.dylib": str(sdl2.resolve())}, info,
                     lambda f: str(prefix / "opt" / f))


def test_collect_bundles_extras_under_their_name_and_walks_them(brew):
    tmp, cellar, prefix = brew
    exe = touch(tmp / "build/xrdome")
    sdl2, sdl3, _, _ = sdl_tree(brew)
    usb = touch(cellar / "libusb/1.0/lib/libusb-1.0.0.dylib")
    graph = {
        str(exe): [str(sdl2)],
        str(sdl2.resolve()): [],                       # SDL3 is not linked
        str(sdl3.resolve()): [str(usb), "/usr/lib/libSystem.B.dylib"],
        str(usb.resolve()): [],
    }
    libs, edges = b.collect([str(exe)], lambda p: graph[p], lambda p: [],
                            extras={"libSDL3.dylib": str(sdl3.resolve())})
    assert libs["libSDL3.dylib"] == str(sdl3.resolve())
    assert "libusb-1.0.0.dylib" in libs                # its own dependencies come along
    assert "libSDL3.0.dylib" not in libs               # not twice under another name
    assert edges[str(sdl3.resolve())] == [(str(usb), "libusb-1.0.0.dylib")]
    # Something that does link SDL3 by its versioned name is pointed at the one copy.
    graph[str(exe)] = [str(sdl2), str(sdl3.parent / "libSDL3.0.dylib")]
    libs, edges = b.collect([str(exe)], lambda p: graph[p], lambda p: [],
                            extras={"libSDL3.dylib": str(sdl3.resolve())})
    assert (str(sdl3.parent / "libSDL3.0.dylib"), "libSDL3.dylib") in edges[str(exe)]
    assert [n for n in libs if n.startswith("libSDL3")] == ["libSDL3.dylib"]
