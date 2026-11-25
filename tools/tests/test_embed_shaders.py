"""tools/embed_shaders.py: the desktop arrays are unchanged and each NAME also gets an
_ES array for GLES 3.0."""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools/embed_shaders.py"

SRC = '#version 330 core\nuniform float u;\nvoid main() { }\n'


def run(tmp_path, text, name="X"):
    src = tmp_path / "a.frag"
    src.write_text(text)
    out = tmp_path / "out.h"
    r = subprocess.run([sys.executable, str(SCRIPT), str(out), "%s=%s" % (name, src)],
                       capture_output=True, text=True)
    return r, out


def array(header, name):
    m = re.search(r"%s\[\] = \{\n(.*?)\n\};" % re.escape(name), header, re.S)
    assert m, name
    return [ln.strip().removesuffix(",")[1:-1].removesuffix("\\n")
            for ln in m.group(1).split("\n")]


def test_es_rewrite(tmp_path):
    r, out = run(tmp_path, SRC)
    assert r.returncode == 0, r.stderr
    h = out.read_text()
    assert array(h, "XRD_SHADER_X") == ["#version 330 core", "uniform float u;",
                                        "void main() { }"]
    es = array(h, "XRD_SHADER_X_ES")
    assert es[:4] == ["#version 300 es", "precision highp float;", "precision highp int;",
                      "precision highp sampler2D;"]
    assert es[4:] == ["uniform float u;", "void main() { }"]


def test_bad_version_fails(tmp_path):
    r, out = run(tmp_path, "#version 150\nvoid main() { }\n")
    assert r.returncode == 1
    assert "a.frag" in r.stderr
