"""packaging/windows/collect.py: the parts that run on any machine (ntldd text,
path matching, pacman's database, the licence guard, SOURCES.md, the zip) and a
whole `stage` run over a fixture MSYS2 tree with ntldd and ffprobe stood in."""
import importlib.util
import sys
import zipfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("windows_collect", ROOT / "packaging/windows/collect.py")
c = importlib.util.module_from_spec(spec)
sys.modules["windows_collect"] = c
spec.loader.exec_module(c)

BIN = "C:/hostedtoolcache/windows/msys64/ucrt64/bin"
MSYS = "C:/hostedtoolcache/windows/msys64"

NTLDD = """\
\tntdll.dll => C:\\WINDOWS\\SYSTEM32\\ntdll.dll (0x00007ffd5a1d0000)
\tKERNEL32.DLL => C:\\WINDOWS\\System32\\KERNEL32.DLL (0x00007ffd58c70000)
\tlibmpv-2.dll => C:\\hostedtoolcache\\windows\\msys64\\ucrt64\\bin\\libmpv-2.dll (0x00007ffd32a50000)
\t\tavcodec-63.dll => C:\\hostedtoolcache\\windows\\msys64\\ucrt64\\bin\\avcodec-63.dll (0x00007ffd2e630000)
\t\tapi-ms-win-core-synch-l1-2-0.dll => not found
\t\tlibfoo-1.dll => not found
\tSDL2.dll => C:\\hostedtoolcache\\windows\\msys64\\UCRT64\\bin\\SDL2.dll (0x00007ffd32210000)
\tlibmpv-2.dll => C:\\hostedtoolcache\\windows\\msys64\\ucrt64\\bin\\libmpv-2.dll (0x00007ffd32a50000)
\tplugin.dll => C:\\hostedtoolcache\\windows\\msys64\\ucrt64\\lib\\plugins\\plugin.dll (0x00007ffd32000000)
"""


def test_parse_ntldd():
    got = c.parse_ntldd(NTLDD)
    assert ("ntdll.dll", "C:\\WINDOWS\\SYSTEM32\\ntdll.dll") in got
    assert ("api-ms-win-core-synch-l1-2-0.dll", None) in got
    assert ("libmpv-2.dll", "C:\\hostedtoolcache\\windows\\msys64\\ucrt64\\bin\\libmpv-2.dll") in got
    assert c.parse_ntldd("xrdome.exe:\n\n") == []


def test_norm_and_relative():
    assert c.norm_path("C:\\A\\B\\") == "c:/a/b"
    assert c.norm_path("/c/Users/x") == "c:/users/x"
    assert c.relative_to("C:\\Msys64\\UCRT64\\bin\\X.dll", "c:/msys64/ucrt64/bin") == "x.dll"
    assert c.relative_to("C:/msys64/ucrt64/binary/x.dll", "C:/msys64/ucrt64/bin") is None
    assert c.relative_to("C:/Windows/System32/x.dll", "C:/msys64/ucrt64/bin") is None


def test_bundled_dlls_keeps_only_bindir():
    warned = []
    got = c.bundled_dlls(c.parse_ntldd(NTLDD), BIN, warned.append)
    assert got == ["avcodec-63.dll", "libmpv-2.dll", "SDL2.dll"]  # once each, no system DLLs
    assert warned == ["ntldd could not find libfoo-1.dll"]  # the API set is not reported


def desc(name, version, base, licenses):
    return (f"%NAME%\n{name}\n\n%VERSION%\n{version}\n\n%BASE%\n{base}\n\n%DESC%\nx\n\n"
            "%LICENSE%\n" + "\n".join(licenses) + "\n\n%VALIDATION%\nsha256\n")


def make_db(tmp_path, pkgs):
    """pkgs: (name, version, base, licenses, files); also writes the files under tmp_path/msys."""
    db = tmp_path / "db"
    for name, version, base, lic, files in pkgs:
        d = db / "local" / f"{name}-{version}"
        d.mkdir(parents=True)
        (d / "desc").write_text(desc(name, version, base, lic))
        (d / "files").write_text("%FILES%\n" + "\n".join(files) + "\n")
        for f in files:
            if not f.endswith("/"):
                p = tmp_path / "msys" / f
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text(f"content of {f}")
    return db


P = "mingw-w64-ucrt-x86_64-"


def test_read_local_db_and_owners(tmp_path):
    db = make_db(tmp_path, [
        (P + "ffmpeg", "9.0.2-4", "mingw-w64-ffmpeg", ["spdx:GPL-3.0-or-later"],
         ["ucrt64/", "ucrt64/bin/", "ucrt64/bin/ffprobe.exe", "ucrt64/bin/avcodec-63.dll",
          "ucrt64/share/licenses/ffmpeg/COPYING.GPLv3"]),
        (P + "vulkan-loader", "1~1.4.363.0-1", "mingw-w64-vulkan-loader", ["spdx:Apache-2.0"],
         ["ucrt64/bin/vulkan-1.dll"]),
    ])
    pkgs = c.read_local_db(db)
    assert [p.name for p in pkgs] == [P + "ffmpeg", P + "vulkan-loader"]
    assert pkgs[0].short == "ffmpeg" and pkgs[0].base == "mingw-w64-ffmpeg"
    owners = c.index_owners(pkgs)
    assert c.owner_of(owners, "C:\\X\\ucrt64\\bin\\AVCODEC-63.dll", "C:/x").name == P + "ffmpeg"
    with pytest.raises(c.BundleError, match="no pacman package owns"):
        c.owner_of(owners, "C:/x/ucrt64/bin/other.dll", "C:/x")
    with pytest.raises(c.BundleError, match="no pacman package owns"):
        c.owner_of(owners, "C:/elsewhere/a.dll", "C:/x")
    with pytest.raises(c.BundleError, match="local database"):
        c.read_local_db(tmp_path / "nowhere")


def pkg(name, lic, files=()):
    return c.Package(P + name, "1.0-1", "mingw-w64-" + name, list(lic), list(files))


@pytest.mark.parametrize("lic", [
    ["spdx:GPL-3.0-or-later"], ["spdx:MIT"], ["spdx:LGPL-2.1-or-later"], ["GPL"], ["LGPL3", "GPL"],
    ["MPL", "GPL"], ["spdx:GPL-3.0-or-later WITH GCC-exception-3.1 AND GFDL-1.3-or-later"],
    ["spdx:LGPL-2.1-or-later", "documentation:spdx:GPL-3.0-or-later"],
    ["spdx:MIT AND BSD-3-Clause"], ["spdx:BSD-3-Clause-Clear"], ["spdx:WTFPL"], ["PublicDomain"],
    ["custom:CC0"], ["custom:BSD-like"], ["custom: public domain"], ["spdx:Zlib"], ["Apache"],
    ["spdx:GPL-2.0-or-later OR FTL"],
])
def test_open_licences_pass(lic):
    assert c.license_problems(pkg("x", lic)) == []


@pytest.mark.parametrize("lic", [
    ["custom"], ["proprietary"], ["spdx:LicenseRef-Nvidia"], ["unredistributable"],
    ["spdx:GPL-3.0-or-later AND LicenseRef-fdk"], ["custom:commercial"], ["nonfree"],
    ["spdx:CC-BY-NC-4.0"], [],
])
def test_other_licences_are_refused(lic):
    problems = c.license_problems(pkg("x", lic))
    assert len(problems) == 1 and P + "x" in problems[0]


def test_reviewed_custom_passes_and_unreviewed_names_the_package():
    assert c.license_problems(pkg("libpng", ["custom"])) == []
    assert "REVIEWED_CUSTOM" in c.license_problems(pkg("libmystery", ["custom"]))[0]
    assert c.license_problems(pkg("libmystery", ["custom"]), {"libmystery": "ok"}) == []


def test_nonfree_guard():
    c.check_nonfree("configuration: --enable-gpl --enable-version3 --enable-libx264")
    with pytest.raises(c.BundleError, match="nonfree") as e:
        c.check_nonfree("configuration: --enable-gpl --enable-nonfree --enable-libfdk-aac")
    assert "--enable-libfdk-aac" in str(e.value)  # the configure line is printed


def test_source_tarball_url():
    p = c.Package(P + "libx264", "0.165.r3222.b35605a-3", "mingw-w64-x264", ["custom"])
    assert c.source_tarball_url(p) == \
        "https://mirror.msys2.org/mingw/sources/mingw-w64-x264-0.165.r3222.b35605a-3.src.tar.zst"
    q = c.Package(P + "vulkan-loader", "1:1.4.363.0-1", "mingw-w64-vulkan-loader", [])
    assert c.source_tarball_url(q).endswith("mingw-w64-vulkan-loader-1~1.4.363.0-1.src.tar.zst")


def test_licence_files_are_those_under_share_licenses_and_named_in_share_doc():
    p = pkg("ffmpeg", ["spdx:GPL-3.0-or-later"], [
        "ucrt64/share/licenses/", "ucrt64/share/licenses/ffmpeg/", "ucrt64/share/licenses/ffmpeg/COPYING.GPLv3",
        "ucrt64/share/licenses/ffmpeg/sub/NOTICE", "ucrt64/bin/ffprobe.exe",
        "ucrt64/share/doc/ffmpeg/COPYING", "ucrt64/share/doc/ffmpeg/README", "ucrt64/share/doc/ffmpeg/NOTICE"])
    assert c.licence_files(p) == [("ucrt64/share/licenses/ffmpeg/COPYING.GPLv3", "COPYING.GPLv3"),
                                  ("ucrt64/share/licenses/ffmpeg/sub/NOTICE", "sub/NOTICE"),
                                  ("ucrt64/share/doc/ffmpeg/COPYING", "COPYING"),
                                  ("ucrt64/share/doc/ffmpeg/NOTICE", "NOTICE")]
    assert c.licence_files(pkg("x", ["MIT"], ["ucrt64/bin/x.dll"])) == []


def test_gnu_texts_follow_the_licences():
    assert c.gnu_texts([pkg("a", ["spdx:MIT"])]) == []
    assert c.gnu_texts([pkg("a", ["spdx:GPL-2.0-or-later"])]) == []  # COPYING is version 2
    assert c.gnu_texts([pkg("a", ["spdx:GPL-3.0-or-later"]), pkg("b", ["spdx:LGPL-3.0-or-later OR MIT"]),
                        pkg("c", ["spdx:LGPL-2.1-or-later", "documentation:spdx:GPL-3.0-or-later"])]) == \
        ["GPL-3.0.txt", "LGPL-2.1.txt", "LGPL-3.0.txt"]
    for n in ("GPL-3.0.txt", "LGPL-2.1.txt", "LGPL-3.0.txt"):
        assert (c.TEXTS_DIR / n).read_text().lstrip().startswith("GNU ")


def test_sources_md():
    pkgs = [pkg("zlib", ["spdx:Zlib"]),
            c.Package(P + "ffmpeg", "9.0.2-4", "mingw-w64-ffmpeg", ["spdx:GPL-3.0-or-later"]),
            c.Package(P + "libx264", "0.165-3", "mingw-w64-x264", ["custom"])]
    md = c.build_sources_md("0.1.0", "abc123", pkgs, "configuration: --enable-gpl --enable-version3")
    assert md.startswith("# xrdome 0.1.0 (Windows x64): sources")
    assert "https://github.com/SDNick484/xrdome, commit abc123" in md
    rows = [ln for ln in md.splitlines() if ln.startswith("| ") and "---" not in ln][1:]
    assert [r.split(" | ")[0] for r in rows] == ["| ffmpeg", "| libx264", "| zlib"]  # sorted
    assert ("| ffmpeg | 9.0.2-4 | GPL-3.0-or-later | "
            "https://mirror.msys2.org/mingw/sources/mingw-w64-ffmpeg-9.0.2-4.src.tar.zst | "
            "https://packages.msys2.org/base/mingw-w64-ffmpeg | LICENSES/ffmpeg/ |") in md
    assert "custom (GPL-2.0-or-later (x264))" in md  # a reviewed custom licence says what it is
    assert "```\nconfiguration: --enable-gpl --enable-version3\n```" in md
    assert "GNU GPL version 3 or later" in md  # an ffmpeg that is GPL 3
    plain = c.build_sources_md("0.1.0", "abc", [pkg("zlib", ["spdx:Zlib"])], "configuration: --enable-gpl")
    assert "version 3" not in plain


def test_readme_placeholders():
    text = c.render_readme((ROOT / "packaging/windows/README-windows.txt").read_text(), "1.2.3", "Windows X")
    assert "xrdome 1.2.3 for Windows 11" in text and "CI on Windows X" in text
    assert "@VERSION@" not in text and "@BUILT_ON@" not in text and "@ISSUE_URL@" not in text
    assert "https://github.com/SDNick484/xrdome/issues" in text
    assert "100% scale" in text and "3840x1080" in text
    assert "%APPDATA%\\xrdome\\config.ini" in text and "%LOCALAPPDATA%\\xrdome\\files" in text


def test_make_zip(tmp_path):
    stage = tmp_path / "xrdome"
    (stage / "LICENSES" / "a").mkdir(parents=True)
    (stage / "xrdome.exe").write_bytes(b"MZ")
    (stage / "LICENSES" / "a" / "COPYING").write_text("x")
    z = tmp_path / "out.zip"
    assert c.make_zip(stage, z) == 2
    with zipfile.ZipFile(z) as zf:
        assert sorted(zf.namelist()) == ["xrdome/LICENSES/a/COPYING", "xrdome/xrdome.exe"]
        assert zf.testzip() is None


# ---- stage over a fixture tree ------------------------------------------------


def fixture(tmp_path, ffmpeg_lic=("spdx:GPL-3.0-or-later",), mpv_lic=("spdx:GPL-2.0-or-later",)):
    db = make_db(tmp_path, [
        (P + "ffmpeg", "9.0.2-4", "mingw-w64-ffmpeg", list(ffmpeg_lic),
         ["ucrt64/bin/ffprobe.exe", "ucrt64/bin/avcodec-63.dll", "ucrt64/share/licenses/ffmpeg/COPYING.GPLv3"]),
        (P + "mpv", "0.40.0-1", "mingw-w64-mpv", list(mpv_lic),
         ["ucrt64/bin/libmpv-2.dll", "ucrt64/share/licenses/mpv/Copyright"]),
        (P + "sdl2-compat", "2.32-1", "mingw-w64-sdl2-compat", ["spdx:Zlib"], ["ucrt64/bin/SDL2.dll"]),
        (P + "sdl3", "3.4-1", "mingw-w64-sdl3", ["spdx:Zlib"], ["ucrt64/bin/SDL3.dll"]),
        (P + "libpng", "1.6-1", "mingw-w64-libpng", ["custom"], ["ucrt64/bin/libpng16-16.dll"]),
        (P + "not-bundled", "1-1", "mingw-w64-nb", ["proprietary"], ["ucrt64/bin/nb.dll"]),
    ])
    repo = tmp_path / "repo"
    (repo / "src/render/shaders").mkdir(parents=True)
    (repo / "meson.build").write_text("project('xrdome', 'c',\n  version : '2.3.4')\n")
    (repo / "LICENSE").write_text("GPL text")
    (repo / "src/render/shaders/sharpen.frag").write_text("// cas")
    build = tmp_path / "build"
    build.mkdir()
    (build / "xrdome.exe").write_bytes(b"MZ-xrdome")
    return repo, build, db


def run_stage(tmp_path, ntldd_text=None, buildconf="configuration: --enable-gpl --enable-version3", **kw):
    repo, build, db = fixture(tmp_path, **kw)
    root = str(tmp_path / "msys").replace("\\", "/")
    bindir = root + "/ucrt64/bin"
    dlls = [f"{bindir}/{n}".replace("/", "\\") for n in ("libmpv-2.dll", "avcodec-63.dll", "SDL2.dll", "libpng16-16.dll")]
    text = ntldd_text if ntldd_text is not None else "".join(
        f"\t{Path(d).name} => {d} (0x1)\n" for d in dlls) + "\tKERNEL32.dll => C:\\WINDOWS\\KERNEL32.dll (0x2)\n"
    return c.stage(repo, build, tmp_path / "dist", bindir, root, str(db),
                   ntldd=lambda exe: text, buildconf_of=lambda ff: buildconf, commit="deadbeef"), \
        tmp_path / "dist" / "xrdome"


def test_stage_builds_the_folder(tmp_path):
    r, d = run_stage(tmp_path)
    assert r["version"] == "2.3.4"
    assert sorted(p.name for p in d.iterdir()) == sorted([
        "xrdome.exe", "ffprobe.exe", "libmpv-2.dll", "avcodec-63.dll", "SDL2.dll", "SDL3.dll",
        "libpng16-16.dll",
        "COPYING", "LICENSES", "SOURCES.md", "README-windows.txt"])
    assert (d / "xrdome.exe").read_bytes() == b"MZ-xrdome"
    assert (d / "COPYING").read_text() == "GPL text"
    assert (d / "LICENSES/xrdome/sharpen.frag").is_file()
    assert (d / "LICENSES/ffmpeg/COPYING.GPLv3").read_text() == "content of ucrt64/share/licenses/ffmpeg/COPYING.GPLv3"
    assert (d / "LICENSES/mpv/Copyright").is_file()
    assert (d / "LICENSES/sdl2-compat/NO-LICENSE-FILE.txt").is_file()  # a package with none says so
    assert (d / "LICENSES/GPL-3.0.txt").read_text() == (c.TEXTS_DIR / "GPL-3.0.txt").read_text()
    assert not (d / "LICENSES/LGPL-2.1.txt").exists()
    assert sorted(p.name for p in (d / "LICENSES").iterdir()) == [
        "GPL-3.0.txt", "ffmpeg", "libpng", "mpv", "sdl2-compat", "sdl3", "xrdome"]  # not-bundled stays out
    md = (d / "SOURCES.md").read_text()
    assert "commit deadbeef" in md and "mingw-w64-ffmpeg-9.0.2-4.src.tar.zst" in md
    assert "not-bundled" not in md
    assert "xrdome 2.3.4 for Windows 11" in (d / "README-windows.txt").read_text()
    assert r["packages"] == [P + "ffmpeg", P + "libpng", P + "mpv", P + "sdl2-compat", P + "sdl3"]


def test_runtime_loaded_sdl3_is_bundled_with_its_own_dependencies(tmp_path):
    msys = str(tmp_path / "msys").replace("\\", "/")
    bindir = f"{msys}/ucrt64/bin"
    (tmp_path / "msys/ucrt64/bin").mkdir(parents=True)
    asked = []
    base = f"\tSDL2.dll => {bindir}/SDL2.dll (0x1)\n"

    def ntldd(exe):
        asked.append(Path(exe).name)
        return base + (f"\tlibusb-1.0.dll => {bindir}/libusb-1.0.dll (0x2)\n" if exe.endswith("SDL3.dll") else "")

    repo, build, db = fixture(tmp_path)
    make_db(tmp_path, [(P + "libusb", "1-1", "mingw-w64-libusb", ["spdx:LGPL-2.1-or-later"], ["ucrt64/bin/libusb-1.0.dll"])])
    r = c.stage(repo, build, tmp_path / "dist", bindir, msys, str(db), ntldd=ntldd,
                buildconf_of=lambda f: "configuration: --enable-gpl", commit="x")
    assert "SDL3.dll" in asked
    assert r["dlls"] == ["libusb-1.0.dll", "SDL2.dll", "SDL3.dll"]
    assert (tmp_path / "dist/xrdome/SDL3.dll").is_file() and (tmp_path / "dist/xrdome/libusb-1.0.dll").is_file()
    assert c.runtime_dlls(["sdl2.dll", "SDL3.dll"]) == []  # already there
    assert c.runtime_dlls(["libmpv-2.dll"]) == []


def test_stage_needs_ntldd_to_find_something(tmp_path):
    with pytest.raises(c.BundleError, match="ntldd listed no DLL"):
        run_stage(tmp_path, ntldd_text="\tKERNEL32.dll => C:\\WINDOWS\\KERNEL32.dll (0x2)\n")


def test_stage_refuses_nonfree_ffprobe(tmp_path):
    with pytest.raises(c.BundleError, match="nonfree"):
        run_stage(tmp_path, buildconf="configuration: --enable-gpl --enable-nonfree")
    assert not (tmp_path / "dist" / "xrdome").exists()  # nothing staged


def test_stage_refuses_a_package_that_is_not_open_source(tmp_path):
    with pytest.raises(c.BundleError, match="mpv: licence 'proprietary'"):
        run_stage(tmp_path, mpv_lic=("proprietary",))
    assert not (tmp_path / "dist" / "xrdome").exists()


def test_stage_refuses_an_unowned_dll(tmp_path):
    msys = str(tmp_path / "msys").replace("\\", "/")
    text = f"\tstray.dll => {msys}/ucrt64/bin/stray.dll (0x1)\n"
    with pytest.raises(c.BundleError, match="stray.dll: no pacman package owns"):
        run_stage(tmp_path, ntldd_text=text)


def test_stage_needs_the_build_and_ffprobe(tmp_path):
    repo, build, db = fixture(tmp_path)
    (build / "xrdome.exe").unlink()
    with pytest.raises(c.BundleError, match="build first"):
        c.stage(repo, build, tmp_path / "d", "x", "y", str(db))


def test_main_version_and_zip(tmp_path, capsys):
    assert c.main(["version", "--root", str(ROOT)]) == 0
    assert capsys.readouterr().out.strip() == c.version_from_meson((ROOT / "meson.build").read_text())
    (tmp_path / "meson.build").write_text("project('x', 'c', version : '9.9.9')")
    (tmp_path / "dist" / "xrdome").mkdir(parents=True)
    (tmp_path / "dist" / "xrdome" / "a").write_text("a")
    assert c.main(["zip", "--root", str(tmp_path), "--out", str(tmp_path / "dist")]) == 0
    assert (tmp_path / "dist" / "xrdome-9.9.9-win64.zip").is_file()
    assert c.main(["stage", "--bindir", "x", "--msys-root", "y", "--dbpath", "z",
                   "--root", str(tmp_path), "--build", str(tmp_path / "nob")]) == 1
