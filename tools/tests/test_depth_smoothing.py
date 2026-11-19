"""Temporal smoothing and scene-cut reset (spec 8, second half) and the flicker measurement.

The decisions (SceneCut, Smoothing) and the flicker metric are standard library only and run everywhere,
CI's tools job included; the tests that run the numpy pipeline (and OpenCV for frame images) skip with a
reason when those are missing, as in the depth venv's absence."""

import argparse
import json
import random
import sys

import pytest

from xrdome_depth import cache, flicker, video
from xrdome_depth.flicker import Flicker
from xrdome_depth.video import SceneCut, Smoothing, VideoSession
from xrdome_tools import cli, config, depth, depthenv
from xrdome_tools.errors import EXIT_SETUP, ToolError

W, H = 64, 36  # SceneCut works at any frame size; the ring's own is 322x182


def solid(r: int, g: int, b: int, w: int = W, h: int = H) -> bytes:
    return bytes([r, g, b, 255]) * (w * h)


def stripes(period: int, w: int = W, h: int = H, shift: int = 0) -> bytes:
    """Vertical black and white stripes `period` pixels wide each, moved right by `shift` pixels."""
    row = b"".join(bytes([255, 255, 255, 255] if ((x - shift) // period) % 2 else [0, 0, 0, 255])
                   for x in range(w))
    return row * h


# ---- SceneCut ---------------------------------------------------------------------------

def test_first_frame_is_never_a_cut():
    sc = SceneCut()
    assert sc.update(solid(10, 20, 30), W, H) is False
    assert sc.last == 0.0


def test_the_same_frame_is_not_a_cut():
    sc = SceneCut()
    sc.update(stripes(8), W, H)
    assert sc.update(stripes(8), W, H) is False
    assert sc.last == 0.0


def test_a_different_scene_is_a_cut():
    sc = SceneCut()
    sc.update(solid(0, 0, 0), W, H)
    assert sc.update(solid(255, 255, 255), W, H) is True
    assert sc.last == pytest.approx(1.0)
    assert sc.update(solid(255, 255, 255), W, H) is False  # and the next frame of it is not
    sc.update(stripes(8), W, H)
    assert sc.update(stripes(8, shift=8), W, H) is True  # the same stripes, inverted: every sample flips


def test_motion_noise_and_fades_are_not_cuts():
    rng = random.Random(1)
    sc = SceneCut()
    base = bytearray(stripes(8))
    sc.update(bytes(base), W, H)
    for _ in range(20):  # sensor noise of +-6 levels
        noisy = bytearray(base)
        for i in range(0, len(noisy), 4):
            d = rng.randint(-6, 6)
            for c in range(3):
                noisy[i + c] = min(255, max(0, noisy[i + c] + d))
        assert sc.update(bytes(noisy), W, H) is False
    sc.reset()
    sc.update(stripes(8), W, H)
    assert sc.update(stripes(8, shift=1), W, H) is False  # a pan of one pixel
    sc.reset()
    level = 0
    sc.update(solid(0, 0, 0), W, H)
    for _ in range(20):  # a fade up at 12 levels a frame
        level += 12
        assert sc.update(solid(level, level, level), W, H) is False


def test_threshold_is_the_mean_change_of_full_scale():
    sc = SceneCut(threshold=0.30)
    sc.update(solid(0, 0, 0), W, H)
    assert sc.update(solid(100, 100, 100), W, H) is True  # 100 * (77 + 150 + 29) / 256 = 100: 0.39 of 255
    assert sc.last == pytest.approx(100 / 255, abs=0.01)
    sc.reset()
    sc.update(solid(0, 0, 0), W, H)
    assert sc.update(solid(60, 60, 60), W, H) is False  # 0.24


def test_reset_forgets_the_previous_frame():
    sc = SceneCut()
    sc.update(solid(0, 0, 0), W, H)
    sc.reset()
    assert sc.update(solid(255, 255, 255), W, H) is False


def test_samples_cover_the_frame_at_any_size():
    sc = SceneCut()
    for w, h in ((322, 182), (64, 36), (17, 9), (32, 18)):
        px = bytearray(w * h * 4)
        got = sc.sample(bytes(px), w, h)
        assert len(got) == 32 * 18 and set(got) == {0}
    # luma weights: pure red, green and blue differ, white is 255 (the weights sum to 256)
    assert sc.sample(solid(255, 0, 0), W, H)[0] == 76
    assert sc.sample(solid(0, 255, 0), W, H)[0] == 149
    assert sc.sample(solid(0, 0, 255), W, H)[0] == 28
    assert sc.sample(solid(255, 255, 255), W, H)[0] == 255


# ---- Smoothing ---------------------------------------------------------------------------

def test_first_map_is_taken_as_it_is():
    sm = Smoothing()
    assert sm.step(solid(1, 2, 3), W, H, 0.2, 0.8) == (0.2, 0.8, 1.0)


def test_range_is_averaged_with_the_weight_of_the_newest():
    sm = Smoothing(weight=0.5)
    f = solid(5, 5, 5)
    sm.step(f, W, H, 0.0, 1.0)
    assert sm.step(f, W, H, 0.4, 0.6) == (0.2, 0.8, 0.5)  # halfway to the new range
    lo, hi, a = sm.step(f, W, H, 0.4, 0.6)
    assert (lo, hi, a) == (pytest.approx(0.3), pytest.approx(0.7), 0.5)
    sm = Smoothing(weight=0.25)
    sm.step(f, W, H, 0.0, 1.0)
    lo, hi, a = sm.step(f, W, H, 0.4, 0.6)
    assert (lo, hi, a) == (pytest.approx(0.1), pytest.approx(0.9), 0.25)


def test_a_scene_cut_restarts_the_average():
    sm = Smoothing()
    sm.step(solid(0, 0, 0), W, H, 0.0, 1.0)
    sm.step(solid(0, 0, 0), W, H, 0.0, 1.0)
    assert sm.cuts == 0
    assert sm.step(solid(255, 255, 255), W, H, 5.0, 9.0) == (5.0, 9.0, 1.0)  # the new scene's own range
    assert sm.cuts == 1
    lo, hi, a = sm.step(solid(255, 255, 255), W, H, 5.0, 9.0)
    assert (lo, hi, a) == (5.0, 9.0, 0.5)  # and it is averaged again from there


def test_reset_starts_over():
    sm = Smoothing()
    f = solid(1, 1, 1)
    sm.step(f, W, H, 0.0, 1.0)
    sm.reset()
    assert sm.step(f, W, H, 0.3, 0.4) == (0.3, 0.4, 1.0)
    assert sm.cuts == 0


def test_weight_must_be_in_range():
    for bad in (0.0, -0.1, 1.5):
        with pytest.raises(ValueError, match="weight"):
            Smoothing(weight=bad)
    Smoothing(weight=1.0)  # no smoothing at all: every map is the newest


def test_range_jitter_is_halved_in_amplitude_or_better():
    rng = random.Random(7)
    sm = Smoothing()
    f = solid(9, 9, 9)
    raw, smooth = [], []
    for _ in range(400):
        lo = 0.2 + rng.uniform(-0.05, 0.05)
        smooth.append(sm.step(f, W, H, lo, 0.8)[0])
        raw.append(lo)

    def spread(xs):
        m = sum(xs) / len(xs)
        return (sum((x - m) ** 2 for x in xs) / len(xs)) ** 0.5

    # an average with weight 0.5 has variance w / (2 - w) = 1/3 of the input's
    assert spread(smooth) < 0.65 * spread(raw)


def test_session_log_counts_cuts():
    class Fn:
        cuts = 3

        def __call__(self, rgba, w, h):
            return bytes(video.DEPTH_BYTES)

    ring = video.Ring(bytearray(video.SIZE), init=True)
    t = [0.0]
    lines = []
    sess = VideoSession(ring, Fn(), clock=lambda: t[0], log=lines.append, log_every=0.0)
    ring.frame_write(1, bytes(video.FRAME_BYTES))
    t[0] = 1.0
    assert sess.step()
    assert lines and "3 scene cuts so far" in lines[0]
    sess.close()


# ---- the flicker metric ------------------------------------------------------------------------

def test_minmax_scales_each_map_to_0_1():
    assert flicker.minmax([2, 4, 6]) == [0.0, 0.5, 1.0]
    assert flicker.minmax([5, 5, 5]) == [0.0, 0.0, 0.0]  # flat: nothing to flicker


def test_a_static_sequence_does_not_flicker():
    m = [0.1, 0.5, 0.9, 0.3]
    f = flicker.measure([m] * 30)
    assert len(f.deltas) == 29 and f.mean == 0 and f.max == 0


def test_the_metric_is_the_spikes():
    # Mean absolute per-pixel change between consecutive maps, each min-max normalised on its own.
    a = [0.0, 1.0, 0.0, 1.0]
    b = [1.0, 0.0, 1.0, 0.0]
    f = flicker.measure([a, b, a])
    assert f.deltas == [1.0, 1.0] and f.mean == 1.0 and f.max == 1.0
    c = [0.0, 1.0, 0.0, 0.0]  # differs from a in one pixel of four
    f = flicker.measure([a, c, a, a])
    assert f.deltas == [0.25, 0.25, 0.0] and f.mean == pytest.approx(1 / 6) and f.max == 0.25
    # normalised per frame: a map scaled and offset is the same map
    assert flicker.measure([a, [10 + 7 * v for v in a]]).max == 0.0
    assert flicker.measure([[3, 9, 3, 9], [100, 9000, 100, 9000]]).max == 0.0


def test_flat_maps_count_as_no_change():
    assert flicker.measure([[4] * 6, [9] * 6, [4] * 6]).deltas == [0.0, 0.0]


def test_measure_refuses_what_it_cannot_measure():
    with pytest.raises(ValueError, match="at least two"):
        flicker.measure([[1, 2]])
    with pytest.raises(ValueError, match="at least two"):
        flicker.measure([])
    with pytest.raises(ValueError, match="different sizes"):
        flicker.measure([[1, 2], [1, 2, 3]])


def synthetic_sequence(n=30, size=2000, noise=0.03, seed=3):
    """A near-static scene: a fixed depth ramp plus independent noise on every map."""
    rng = random.Random(seed)
    return [[i / size + rng.uniform(-noise, noise) for i in range(size)] for _ in range(n)]


def test_noise_shows_as_flicker_in_proportion():
    quiet = flicker.measure(synthetic_sequence(noise=0.01)).mean
    loud = flicker.measure(synthetic_sequence(noise=0.04)).mean
    assert 0.002 < quiet < 0.02 and loud > 2.5 * quiet  # the mean change of two uniform noises: ~ 0.67 * noise


def test_flicker_line_and_report():
    f = Flicker([0.01, 0.03])
    assert f.line("x") == "x: mean 0.0200 max 0.0300 over 2 deltas (0-1 scale)"
    text = flicker.report({"a": Flicker([0.02]), "b": Flicker([0.010, 0.012])})
    assert "NOT met" not in text and "met" in text and "0.011" in text
    assert "NOT met" in flicker.report({"a": Flicker([0.02])})
    assert json.loads(flicker.report({"a": Flicker([0.5, 0.25])}, as_json=True))["a"]["mean"] == 0.375


def _write_maps(tmp_path, maps, w=50, h=40):
    files = []
    for k, m in enumerate(maps):
        samples = b"".join(int(max(0.0, min(1.0, v)) * 65535).to_bytes(2, "big") for v in m)
        path = tmp_path / f"d{k:02d}.pgm"
        cache.write_pgm(path, w, h, samples)
        files.append(str(path))
    return files


def test_pgm_round_trip(tmp_path):
    files = _write_maps(tmp_path, [[0.0, 1.0] * 1000], w=50, h=40)
    got = flicker.read_pgm(files[0])
    assert len(got) == 2000 and got[0] == 0 and got[1] == 65535


def test_read_pgm_refuses_other_files(tmp_path):
    (tmp_path / "x.pgm").write_bytes(b"P6\n2 2\n255\n" + bytes(12))
    with pytest.raises(ValueError, match="16-bit binary PGM"):
        flicker.read_pgm(str(tmp_path / "x.pgm"))
    (tmp_path / "y.pgm").write_bytes(b"P5\n2 2\n65535\n" + bytes(3))
    with pytest.raises(ValueError, match="3 bytes of samples"):
        flicker.read_pgm(str(tmp_path / "y.pgm"))
    (tmp_path / "z.pgm").write_bytes(b"")
    with pytest.raises(ValueError, match="not a PGM"):
        flicker.read_pgm(str(tmp_path / "z.pgm"))


def test_maps_command_on_a_synthetic_sequence(tmp_path, capsys):
    maps = synthetic_sequence(n=30, size=2000, noise=0.01)
    files = _write_maps(tmp_path, maps)
    assert flicker.main(["maps", *files]) == 0
    out = capsys.readouterr().out
    assert "maps: mean 0.00" in out and "over 29 deltas" in out
    expected = flicker.measure(maps)  # the 16-bit rounding changes it by far less than 0.001
    mean = float(out.split("mean ")[1].split()[0])
    assert mean == pytest.approx(expected.mean, abs=0.001)
    assert flicker.main(["maps", "--json", *files]) == 0
    assert len(json.loads(capsys.readouterr().out)["maps"]["deltas"]) == 29


def test_maps_command_reports_bad_input(tmp_path, capsys):
    assert flicker.main(["maps", str(tmp_path / "nope.pgm"), str(tmp_path / "nope2.pgm")]) == 2
    assert "flicker:" in capsys.readouterr().err
    files = _write_maps(tmp_path, [[0.5] * 2000])
    assert flicker.main(["maps", files[0]]) == 2
    assert "at least two" in capsys.readouterr().err


# ---- the numpy pipeline ----------------------------------------------------------------------------

@pytest.fixture
def np():
    return pytest.importorskip("numpy", reason="numpy is not installed (the depth venv has it)")


class Scripted:
    """A backend that answers with the next map in its list (any size: the maps are tiny)."""

    def __init__(self, maps):
        self.maps = list(maps)
        self.n = 0

    def infer(self, chw, width, height):
        m = self.maps[min(self.n, len(self.maps) - 1)]
        self.n += 1
        return m


def _out(np, fn, rgba, w=W, h=H):
    return np.frombuffer(fn(rgba, w, h), dtype="=u2").astype(np.float64) / 65535.0


def test_pipeline_averages_depth_with_the_weight(np):
    a = np.linspace(0, 1, W * H, dtype=np.float32).reshape(H, W)
    b = a[::-1, ::-1].copy()  # the same range, the opposite depth
    fn = video.make_depth_fn(Scripted([a, b]))
    f = solid(40, 40, 40)
    first = _out(np, fn, f)
    second = _out(np, fn, f)
    na = video.DepthPipeline(Scripted([a]))  # unsmoothed references
    assert np.allclose(first, _out(np, video.make_depth_fn(Scripted([a]), smooth=False), f), atol=1e-4)
    # alpha 0.5 between the first map and the second normalised with the (equal) range
    assert np.allclose(second, 0.5 * first + 0.5 * first[::-1], atol=2e-4)
    assert na.cuts is None and fn.cuts == 0


def test_pipeline_restarts_on_a_scene_cut(np):
    a = np.linspace(0, 1, W * H, dtype=np.float32).reshape(H, W)
    b = a[::-1, ::-1].copy()
    fn = video.make_depth_fn(Scripted([a, b]))
    _out(np, fn, solid(0, 0, 0))
    second = _out(np, fn, solid(255, 255, 255))  # a different scene: taken as it is
    assert fn.cuts == 1
    alone = _out(np, video.make_depth_fn(Scripted([b]), smooth=False), solid(255, 255, 255))
    assert np.allclose(second, alone, atol=1e-4)  # b as photos would normalise it, no trace of a


def test_pipeline_smooths_the_percentile_range(np):
    base = np.linspace(0.0, 1.0, W * H, dtype=np.float32).reshape(H, W)
    wide = base * 2.0  # the model's scale moved: a smoothed range follows it halfway
    fn = video.make_depth_fn(Scripted([base, wide]))
    f = solid(40, 40, 40)
    _out(np, fn, f)
    lo, hi = np.percentile(base, (1, 99))
    wlo, whi = np.percentile(wide, (1, 99))
    second = _out(np, fn, f)
    slo, shi = (lo + wlo) / 2, (hi + whi) / 2
    unit = np.clip((wide - slo) / (shi - slo), 0, 1)
    first = np.clip((base - lo) / (hi - lo), 0, 1)
    assert np.allclose(second, (0.5 * first + 0.5 * unit).ravel(), atol=2e-4)


def test_pipeline_flat_depth_sits_on_the_screen_plane(np):
    fn = video.make_depth_fn(Scripted([np.full((H, W), 3.0, dtype=np.float32)]))
    assert set(np.frombuffer(fn(solid(9, 9, 9), W, H), dtype="=u2").tolist()) == {round(0.5 * 65535)}


def test_pipeline_refuses_a_nan_depth(np):
    from xrdome_depth.backend import BackendError

    bad = np.full((H, W), np.nan, dtype=np.float32)
    for smooth in (True, False):
        with pytest.raises(BackendError, match="NaN"):
            video.make_depth_fn(Scripted([bad]), smooth=smooth)(solid(9, 9, 9), W, H)


def test_warm_leaves_no_state_behind(np):
    a = np.linspace(0, 1, 322 * 182, dtype=np.float32).reshape(182, 322)
    fn = video.make_depth_fn(Scripted([a * 9, a]))
    video.warm(fn)
    assert fn.prev is None and fn.smoothing.range is None
    out = np.frombuffer(fn(solid(1, 1, 1, 322, 182), 322, 182), dtype="=u2")
    assert out[0] == 0 and out[-1] == 65535  # normalised on its own range, not blended with the warm-up


def test_smoothing_lowers_flicker_on_a_noisy_static_shot(np):
    rng = np.random.default_rng(5)
    base = np.linspace(0.0, 1.0, 64 * 36, dtype=np.float32).reshape(36, 64)
    maps = [base + rng.normal(0, 0.04, base.shape).astype(np.float32) for _ in range(30)]
    frame = solid(70, 70, 70)  # a static shot: no cut
    plain = video.make_depth_fn(Scripted(maps), smooth=False)
    smooth = video.make_depth_fn(Scripted(maps))
    outs = {"plain": [], "smooth": []}
    for _ in maps:
        outs["plain"].append(_out(np, plain, frame).tolist())
        outs["smooth"].append(_out(np, smooth, frame).tolist())
    f_plain = flicker.measure(outs["plain"])
    f_smooth = flicker.measure(outs["smooth"])
    assert len(f_smooth.deltas) == 29
    assert f_smooth.mean < 0.8 * f_plain.mean  # the average of two noisy maps flickers less than one


def test_measure_frames_runs_the_three_measurements(np, tmp_path):
    cv2 = pytest.importorskip("cv2", reason="OpenCV is not installed (the depth venv has it)")
    rng = np.random.default_rng(2)
    for k in range(6):
        img = (rng.random((90, 160, 3)) * 20 + 100).astype(np.uint8)  # near-identical frames
        assert cv2.imwrite(str(tmp_path / f"frame_{k:04d}.png"), img)
    base = np.linspace(0.0, 1.0, 322 * 182, dtype=np.float32).reshape(182, 322)

    class Backend:
        n = 0

        def infer(self, chw, width, height):
            assert chw.shape == (3, 182, 322)
            self.n += 1
            return base + np.float32(0.01 * self.n)

    paths = flicker.frame_files(str(tmp_path), 1, 4)
    assert [p[-14:] for p in paths] == ["frame_0001.png", "frame_0002.png", "frame_0003.png", "frame_0004.png"]
    got = flicker.measure_frames(paths, Backend())
    assert list(got) == ["raw model (the spike's number)", "helper, not smoothed", "helper, smoothed"]
    assert all(len(r.deltas) == 3 for r in got.values())
    with pytest.raises(ValueError, match="needed from index"):
        flicker.frame_files(str(tmp_path), 4, 4)


def test_flicker_main_frames_reports_a_missing_backend(tmp_path, capsys):
    pytest.importorskip("numpy", reason="numpy is not installed (the depth venv has it)")
    cv2 = pytest.importorskip("cv2", reason="OpenCV is not installed (the depth venv has it)")
    import numpy

    from xrdome_depth.backend import BackendError

    for k in range(2):
        cv2.imwrite(str(tmp_path / f"f{k}.png"), numpy.zeros((9, 16, 3), dtype=numpy.uint8))

    class Broken:
        def __init__(self, models_dir, vulkan):
            pass

        def infer(self, chw, w, h):
            raise BackendError("model file x is missing; run `xrdome-tools setup depth`")

    assert flicker.main(["frames", str(tmp_path), "--models-dir", "/nowhere", "--count", "2"],
                        make_backend=Broken) == 2
    assert "model file x is missing" in capsys.readouterr().err
    assert flicker.main(["frames", str(tmp_path), "--models-dir", "/nowhere", "--count", "1"]) == 2


# ---- the launcher ---------------------------------------------------------------------------------------

@pytest.fixture
def paths(monkeypatch, tmp_path):
    for var in ("XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME"):
        monkeypatch.setenv(var, str(tmp_path / var.lower()))
    return config.paths()


def test_flicker_is_a_subcommand():
    args = cli.build_parser().parse_args(["depth", "flicker", "frames_dir", "--start", "60", "--count", "10", "--json"])
    assert (args.what, str(args.frames), args.start, args.count, args.json) == ("flicker", "frames_dir", 60, 10, True)
    args = cli.build_parser().parse_args(["depth", "flicker", "d"])
    assert (args.start, args.count, args.json) == (0, 30, False)


def test_flicker_argv(paths, tmp_path):
    argv = depth.flicker_argv(paths, tmp_path, start=5, count=30, as_json=False, cpu=False)
    assert argv == [str(depthenv.python(paths)), "-u", str(depth.FLICKER_SCRIPT), "frames", str(tmp_path),
                    "--models-dir", str(depthenv.ncnn_dir(paths)), "--start", "5", "--count", "30"]
    assert depth.flicker_argv(paths, tmp_path, start=0, count=2, as_json=True, cpu=True)[-2:] == ["--json", "--cpu"]
    assert depth.FLICKER_SCRIPT.is_file()


def _flicker(frames, **kw):
    ns = dict(what="flicker", frames=frames, start=0, count=30, json=False)
    ns.update(kw)
    return depth.run(argparse.Namespace(**ns))


def test_flicker_refuses_without_a_folder_or_setup(paths, tmp_path):
    with pytest.raises(ToolError, match="not a folder") as e:
        _flicker(tmp_path / "nope")
    assert e.value.code == EXIT_SETUP
    with pytest.raises(ToolError, match="setup depth"):
        _flicker(tmp_path)


def test_flicker_runs_the_script(paths, tmp_path, monkeypatch, capsys):
    monkeypatch.setattr(depthenv, "installed", lambda p: None)
    monkeypatch.setattr(depthenv, "variant", lambda p: "gpu")
    ran = []
    monkeypatch.setattr(depth.Runner, "run", lambda self, argv, **kw: ran.append(argv))
    with pytest.raises(ToolError, match="at least 2"):
        _flicker(tmp_path, count=1)
    assert _flicker(tmp_path, start=3, count=12) == 0
    assert ran == [depth.flicker_argv(paths, tmp_path, start=3, count=12, as_json=False, cpu=False)]


def test_flicker_script_runs_standalone(tmp_path):
    """Started as a script (as the launcher does), with nothing but the standard library."""
    import subprocess

    files = _write_maps(tmp_path, synthetic_sequence(n=4, size=2000, noise=0.01))
    r = subprocess.run([sys.executable, "-I", str(depth.FLICKER_SCRIPT), "maps", *files],
                       capture_output=True, text=True, timeout=60)
    assert r.returncode == 0 and "over 3 deltas" in r.stdout, r.stderr
