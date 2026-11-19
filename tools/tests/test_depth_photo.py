"""`depth photo`: the numpy parts, with a fake backend (no ncnn). Skipped when numpy is missing (CI's tools job)."""

import pytest

np = pytest.importorskip("numpy", reason="numpy is only in the depth venv; run with a python that has it")

from xrdome_depth import cache, photo
from xrdome_depth.backend import Backend, BackendError


REAL_RESIZE = photo.resize


def _resize(img, width, height, cubic):
    """Nearest-neighbour stand-in for OpenCV (not installed here); same shape contract."""
    ys = np.minimum(np.arange(height) * img.shape[0] // height, img.shape[0] - 1)
    xs = np.minimum(np.arange(width) * img.shape[1] // width, img.shape[1] - 1)
    return img[ys][:, xs]


@pytest.fixture(autouse=True)
def fake_resize(monkeypatch):
    monkeypatch.setattr(photo, "resize", _resize)


class Fake(Backend):
    """Depth = the red channel's first pixel row ramp, scaled; records what it was asked."""

    def __init__(self, maker=None):
        self.calls = []
        self.maker = maker

    def infer(self, chw, width, height):
        self.calls.append((chw.shape, chw.dtype, width, height))
        if self.maker:
            return self.maker(width, height)
        return np.tile(np.linspace(0, 10, width, dtype=np.float32), (height, 1))


def read_pgm(path):
    raw = path.read_bytes()
    magic, size, maxval, body = raw.split(b"\n", 3)
    w, h = map(int, size.split())
    assert magic == b"P5" and maxval == b"65535"
    assert len(body) == 2 * w * h
    return np.frombuffer(body, dtype=">u2").reshape(h, w)


def test_normalise_maps_percentiles_to_0_and_1():
    d = np.arange(10001, dtype=np.float32).reshape(1, -1) * 3 + 7  # 7 .. 30010, a ramp
    n = photo.normalise(d)
    lo, hi = np.percentile(d, [1, 99])
    assert n.dtype == np.float32 and n.min() == 0.0 and n.max() == 1.0
    assert n[0, np.searchsorted(d[0], lo)] == pytest.approx(0.0, abs=1e-3)
    assert n[0, np.searchsorted(d[0], hi)] == pytest.approx(1.0, abs=1e-3)
    assert (n[0, :90] == 0).all() and (n[0, -90:] == 1).all()  # outside the percentiles: clipped
    assert np.all(np.diff(n[0]) >= 0)  # larger stays larger


def test_normalise_outliers_do_not_stretch_the_range():
    d = np.linspace(0, 1, 1000, dtype=np.float32).reshape(1, -1)
    d[0, 0] = -1e6
    d[0, -1] = 1e6
    n = photo.normalise(d)
    assert n[0, 500] == pytest.approx(0.5, abs=0.01)


def test_normalise_flat_depth_is_mid_and_nan_is_an_error():
    assert (photo.normalise(np.full((4, 4), 3.0, dtype=np.float32)) == 0.5).all()
    with pytest.raises(BackendError):
        photo.normalise(np.array([[1.0, np.nan]], dtype=np.float32))


def test_samples_and_byte_order():
    s = photo.to_samples(np.array([[0.0, 1.0, 1 / 65535, 258 / 65535, 2.0, -1.0]]))
    assert s.dtype == np.uint16 and s.tolist() == [[0, 65535, 1, 258, 65535, 0]]
    assert photo.big_endian(s) == b"\x00\x00\xff\xff\x00\x01\x01\x02\xff\xff\x00\x00"


def test_preprocess_matches_the_export_contract():
    rgb = np.zeros((20, 30, 3), dtype=np.uint8)
    rgb[..., 0], rgb[..., 1], rgb[..., 2] = 255, 0, 51
    x = photo.preprocess(rgb, 518, 392)
    assert x.shape == (3, 392, 518) and x.dtype == np.float32 and x.flags["C_CONTIGUOUS"]
    assert x[0, 0, 0] == pytest.approx((1.0 - 0.485) / 0.229, rel=1e-5)  # R first: channel order kept
    assert x[1, 0, 0] == pytest.approx((0.0 - 0.456) / 0.224, rel=1e-5)
    assert x[2, 0, 0] == pytest.approx((0.2 - 0.406) / 0.225, rel=1e-5)


def test_depth_map_landscape_and_portrait_choose_their_model():
    for (w, h), want in (((40, 30), (518, 392)), ((30, 40), (392, 518))):
        fake = Fake()
        d = photo.depth_map(np.zeros((h, w, 3), dtype=np.uint8), fake)
        assert fake.calls == [((3, want[1], want[0]), np.dtype("float32"), *want)]
        assert d.shape == (h, w) and d.dtype == np.uint16  # the photo's displayed size


def test_depth_map_is_capped_at_2048():
    d = photo.depth_map(np.zeros((300, 4096, 3), dtype=np.uint8), Fake())
    assert d.shape == (150, 2048)


def test_larger_is_nearer_survives_the_pipeline():
    d = photo.depth_map(np.zeros((30, 40, 3), dtype=np.uint8), Fake())
    assert d[:, 0].max() == 0 and d[:, -1].min() > 60000  # nearest-neighbour stand-in: not exactly the end
    assert (np.diff(d[0].astype(int)) >= 0).all()


def test_process_writes_the_cache_file_once(tmp_path):
    Image = pytest.importorskip("PIL.Image")
    src = tmp_path / "p.png"
    Image.new("RGB", (40, 30), (10, 20, 30)).save(src)
    fake = Fake()
    dest, note = photo.process(src, tmp_path / "c", fake)
    assert dest == cache.cache_path(tmp_path / "c", cache.cache_key(src))
    assert read_pgm(dest).shape == (30, 40)
    assert [f.name for f in dest.parent.iterdir()] == [dest.name]  # no .part left
    assert "s (decode" in note
    again, note = photo.process(src, tmp_path / "c", fake)
    assert again == dest and note == "cached" and len(fake.calls) == 1
    photo.process(src, tmp_path / "c", fake, force=True)
    assert len(fake.calls) == 2


def test_exif_rotation_is_applied_before_the_model_size_is_chosen(tmp_path):
    Image = pytest.importorskip("PIL.Image")
    # A 40x20 landscape file with Orientation 6 (rotate 90 CW to display) is a 20x40 portrait photo.
    im = Image.new("RGB", (40, 20), (200, 0, 0))
    for x in range(20):  # left half green, so the rotation shows in the pixels too
        for y in range(20):
            im.putpixel((x, y), (0, 200, 0))
    exif = Image.Exif()
    exif[0x0112] = 6
    src = tmp_path / "rot.jpg"
    im.save(src, exif=exif.tobytes(), quality=100)
    rgb = photo.load_displayed(src)
    assert rgb.shape == (40, 20, 3)
    assert rgb[0, 0, 1] > 150 and rgb[-1, 0, 0] > 150  # Orientation 6: the left half ends up on top
    fake = Fake()
    dest, _ = photo.process(src, tmp_path / "c", fake)
    assert fake.calls[0][2:] == (392, 518)
    assert read_pgm(dest).shape == (40, 20)


def test_main_folder_continues_after_a_bad_photo(tmp_path, capsys):
    Image = pytest.importorskip("PIL.Image")
    d = tmp_path / "photos"
    d.mkdir()
    Image.new("RGB", (8, 6)).save(d / "a.png")
    (d / "b.jpg").write_bytes(b"not a jpeg")
    Image.new("RGB", (6, 8), (1, 2, 3)).save(d / "c.png")
    fake = Fake()
    rc = photo.main(["--cache-dir", str(tmp_path / "c"), "--models-dir", str(tmp_path / "m"), str(d)],
                    make_backend=lambda models, vulkan: fake)
    out = capsys.readouterr()
    assert rc == 1
    assert "a.png:" in out.out and "c.png:" in out.out and "b.jpg: failed" in out.err
    assert len(list((tmp_path / "c").glob("*-dav2s-v1.pgm"))) == 2


def test_main_backend_error_stops_with_status_2(tmp_path, capsys):
    Image = pytest.importorskip("PIL.Image")
    Image.new("RGB", (8, 6)).save(tmp_path / "a.png")

    class Broken(Backend):
        def infer(self, chw, width, height):
            raise BackendError("model file x is missing")

    rc = photo.main(["--cache-dir", str(tmp_path / "c"), "--models-dir", "m", str(tmp_path / "a.png")],
                    make_backend=lambda models, vulkan: Broken())
    assert rc == 2 and "model file x is missing" in capsys.readouterr().err


def test_real_resize_with_opencv():
    pytest.importorskip("cv2", reason="OpenCV is only in the depth venv")
    img = np.arange(6 * 4 * 3, dtype=np.uint8).reshape(4, 6, 3)
    assert REAL_RESIZE(img, 12, 8, True).shape == (8, 12, 3)
    assert REAL_RESIZE(np.zeros((4, 6), dtype=np.float32), 3, 2, False).shape == (2, 3)
