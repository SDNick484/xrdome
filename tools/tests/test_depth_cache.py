"""The depth cache contract (spec 5) and the stdlib parts of `depth photo`: no numpy needed."""

import hashlib
import sys
import types

import pytest

from xrdome_depth import backend, cache, export, photo


def test_cache_key_is_sha256_of_the_bytes(tmp_path):
    f = tmp_path / "a.jpg"
    data = bytes(range(256)) * 5000  # more than one read block
    f.write_bytes(data)
    key = cache.cache_key(f)
    assert key == hashlib.sha256(data).hexdigest()
    assert len(key) == 64 and key == key.lower()
    # the name does not matter, only the bytes
    g = tmp_path / "other.png"
    g.write_bytes(data)
    assert cache.cache_key(g) == key


def test_cache_path(tmp_path):
    key = "ab" * 32
    assert cache.cache_path(tmp_path, key) == tmp_path / (key + "-dav2s-v1.pgm")


def test_pgm_header_and_byte_order(tmp_path):
    dest = tmp_path / "sub" / "x.pgm"
    # 3 x 2: samples 0, 1, 256, 258, 65534, 65535 (the byte order shows in 1, 256, 258 and 65534)
    values = [0, 1, 256, 258, 65534, 65535]
    body = b"".join(v.to_bytes(2, "big") for v in values)
    cache.write_pgm(dest, 3, 2, body)
    raw = dest.read_bytes()
    assert raw.startswith(b"P5\n3 2\n65535\n")
    assert raw[len(b"P5\n3 2\n65535\n"):] == body
    assert raw[-4:] == b"\xff\xfe\xff\xff"
    assert len(raw) == len(b"P5\n3 2\n65535\n") + 12


def test_pgm_is_renamed_from_part(tmp_path):
    dest = tmp_path / "x.pgm"
    cache.write_pgm(dest, 1, 1, b"\x00\x01")
    assert sorted(f.name for f in tmp_path.iterdir()) == ["x.pgm"]


def test_pgm_failure_leaves_no_part_and_no_file(tmp_path, monkeypatch):
    dest = tmp_path / "x.pgm"
    monkeypatch.setattr(cache.os, "replace", lambda *a: (_ for _ in ()).throw(OSError("nope")))
    with pytest.raises(OSError):
        cache.write_pgm(dest, 1, 1, b"\x00\x01")
    assert list(tmp_path.iterdir()) == []


def test_pgm_rejects_a_wrong_body_length(tmp_path):
    with pytest.raises(ValueError):
        cache.write_pgm(tmp_path / "x.pgm", 2, 2, b"\x00" * 7)
    assert list(tmp_path.iterdir()) == []


def test_model_size_follows_displayed_orientation():
    assert photo.model_size(4000, 3000) == (518, 392)
    assert photo.model_size(3000, 4000) == (392, 518)
    assert photo.model_size(100, 100) == (518, 392)  # square: landscape
    assert photo.model_size(1, 2) == (392, 518)
    # the sizes are the ones setup depth generates
    assert {photo.LANDSCAPE, photo.PORTRAIT} <= set(export.SIZES)


def test_output_size_caps_the_long_side():
    assert photo.output_size(1000, 500) == (1000, 500)
    assert photo.output_size(2048, 1536) == (2048, 1536)
    assert photo.output_size(4096, 3072) == (2048, 1536)
    assert photo.output_size(3000, 4000) == (1536, 2048)
    assert photo.output_size(8000, 3) == (2048, 1)  # never below one pixel


def test_list_photos(tmp_path):
    for name in ("b.JPG", "a.png", "notes.txt", "c.jpeg"):
        (tmp_path / name).write_bytes(b"x")
    (tmp_path / "sub").mkdir()
    (tmp_path / "sub" / "d.jpg").write_bytes(b"x")
    assert [f.name for f in photo.list_photos(tmp_path)] == ["a.png", "b.JPG", "c.jpeg"]
    one = tmp_path / "notes.txt"
    assert photo.list_photos(one) == [one]  # a named file is taken as it is


def _fake_ncnn(monkeypatch, param_ret=0, model_ret=0, gpus=1):
    class Opt:
        pass

    class Net:
        def __init__(self):
            self.opt = Opt()

        def load_param(self, path):
            return param_ret

        def load_model(self, path):
            return model_ret

    mod = types.ModuleType("ncnn")
    mod.Net = Net
    mod.get_gpu_count = lambda: gpus
    monkeypatch.setitem(sys.modules, "ncnn", mod)


def _models(tmp_path, names=None):
    for ext in ("param", "bin"):
        (tmp_path / export.output_name(518, 392, ext)).write_bytes(b"x")


def test_backend_missing_model_file_fails_before_ncnn_is_touched(tmp_path, monkeypatch):
    monkeypatch.setitem(sys.modules, "ncnn", None)  # importing it would fail differently
    b = backend.NcnnBackend(tmp_path)
    with pytest.raises(backend.BackendError, match=r"dav2s_518x392\.ncnn\.param is missing.*setup depth"):
        b._net(518, 392)


def test_backend_load_return_code_is_checked(tmp_path, monkeypatch):
    _models(tmp_path)
    _fake_ncnn(monkeypatch, model_ret=-1)
    with pytest.raises(backend.BackendError, match=r"could not load .*\.bin.*returned -1"):
        backend.NcnnBackend(tmp_path)._net(518, 392)
    _fake_ncnn(monkeypatch, param_ret=-1)
    with pytest.raises(backend.BackendError, match=r"could not load .*\.param"):
        backend.NcnnBackend(tmp_path)._net(518, 392)


def test_backend_loads_fp16_vulkan(tmp_path, monkeypatch):
    _models(tmp_path)
    _fake_ncnn(monkeypatch)
    net = backend.NcnnBackend(tmp_path)._net(518, 392)
    assert net.opt.use_vulkan_compute is True
    assert net.opt.use_fp16_storage is True and net.opt.use_fp16_arithmetic is True
    cpu = backend.NcnnBackend(tmp_path, vulkan=False)._net(518, 392)
    assert cpu.opt.use_vulkan_compute is False and cpu.opt.use_fp16_storage is False


def test_backend_without_a_vulkan_gpu_refuses_unless_cpu(tmp_path, monkeypatch):
    _models(tmp_path)
    _fake_ncnn(monkeypatch, gpus=0)
    with pytest.raises(backend.BackendError, match="no Vulkan GPU"):
        backend.NcnnBackend(tmp_path)._net(518, 392)
    backend.NcnnBackend(tmp_path, vulkan=False)._net(518, 392)
