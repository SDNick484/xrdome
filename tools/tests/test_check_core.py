"""tools/check_core.py: libxrdome-core (src/core_sources.txt) includes no X11,
GLX, mpv or SDL header, directly or through a src/ header."""
import importlib.util
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools/check_core.py"
spec = importlib.util.spec_from_file_location("check_core", SCRIPT)
cc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cc)


def run(list_path, root):
    return subprocess.run([sys.executable, str(SCRIPT), str(list_path), str(root)],
                          capture_output=True, text=True)


def test_clean_tree_passes():
    r = run(ROOT / "src/core_sources.txt", ROOT)
    assert r.returncode == 0, r.stderr
    assert "clean" in r.stdout


def test_file_including_sdl_fails_with_its_name(tmp_path):
    (tmp_path / "src/app").mkdir(parents=True)
    (tmp_path / "src/ok.c").write_text('#include "app/ok.h"\n#include <stdio.h>\n')
    (tmp_path / "src/app/ok.h").write_text("#include <stdbool.h>\n")
    (tmp_path / "src/bad.c").write_text("#include <math.h>\n#include <SDL.h>\n")
    lst = tmp_path / "core_sources.txt"
    lst.write_text("# core\n\nsrc/ok.c\nsrc/bad.c\n")
    r = run(lst, tmp_path)
    assert r.returncode == 1
    assert "src/bad.c:2" in r.stderr
    assert "SDL.h" in r.stderr
    assert "ok.c" not in r.stderr


def test_header_included_recursively_is_checked(tmp_path):
    (tmp_path / "src/a").mkdir(parents=True)
    (tmp_path / "src/core.c").write_text('#include "a/one.h"\n')
    (tmp_path / "src/a/one.h").write_text('#include "two.h"\n')  # next to one.h
    (tmp_path / "src/a/two.h").write_text("#include <mpv/client.h>\n")
    lst = tmp_path / "list.txt"
    lst.write_text("src/core.c\n")
    msgs = cc.problems(lst, tmp_path)
    assert len(msgs) == 1
    assert msgs[0].startswith("src/a/two.h:1: includes <mpv/client.h>")
    assert "src/core.c -> src/a/one.h -> src/a/two.h" in msgs[0]


def test_each_forbidden_prefix(tmp_path):
    (tmp_path / "src").mkdir()
    names = ["X11/Xlib.h", "GL/glx.h", "epoxy/glx.h", "SDL2/SDL.h", "mpv/render_gl.h"]
    for i, n in enumerate(names):
        (tmp_path / f"src/f{i}.c").write_text(f"#  include <{n}>\n")
    (tmp_path / "src/fine.c").write_text("#include <epoxy/gl.h>\n#include <GL/gl.h>\n")
    lst = tmp_path / "list.txt"
    lst.write_text("".join(f"src/f{i}.c\n" for i in range(len(names))) + "src/fine.c\n")
    msgs = cc.problems(lst, tmp_path)
    assert len(msgs) == len(names)
    assert not any("fine.c" in m for m in msgs)


def test_missing_listed_file_fails(tmp_path):
    (tmp_path / "src").mkdir()
    lst = tmp_path / "list.txt"
    lst.write_text("src/gone.c\n")
    r = run(lst, tmp_path)
    assert r.returncode == 1
    assert "src/gone.c" in r.stderr and "missing" in r.stderr
