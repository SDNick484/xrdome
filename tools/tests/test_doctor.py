import json

from xrdome_tools import cli, doctor, hw


def fixed(**kw) -> hw.Probe:
    base = dict(
        platform="linux", gpus=["amd"],
        encoders_built={"hevc_nvenc", "hevc_vaapi", "libx265"},
        encoders_ok={"hevc_nvenc": "Cannot load libcuda.so.1", "hevc_vaapi": "ok"},
        vaapi=hw.VaapiInfo("/dev/dri/renderD128", {"VAProfileHEVCMain"}, {"VAProfileHEVCMain"}),
        vulkan_devices=["AMD Radeon 780M Graphics (RADV PHOENIX)"], compute="rocm")
    base.update(kw)
    return hw.Probe(**base)


def test_doctor_json(monkeypatch, capsys):
    monkeypatch.setattr(hw, "probe", lambda runner, **kw: fixed())
    assert cli.main(["doctor", "--json"]) == 0
    data = json.loads(capsys.readouterr().out)
    assert {"platform", "gpus", "encoders", "vaapi", "vulkan", "compute", "paths"} <= data.keys()
    assert set(data["paths"]) >= {"transcode", "to3d", "enhance"}
    assert data["paths"]["transcode"]["value"] == "encode=hevc_vaapi"
    assert data["paths"]["to3d"]["value"] == "compute=rocm"


def test_doctor_table(monkeypatch, capsys):
    monkeypatch.setattr(hw, "probe", lambda runner, **kw: fixed())
    assert cli.main(["doctor"]) == 0
    out = capsys.readouterr().out
    assert "platform" in out and "path: encode=hevc_vaapi (" in out


def test_doctor_nothing_found(monkeypatch, capsys):
    p = fixed(gpus=[], encoders_built=set(), encoders_ok={}, vaapi=None,
              vulkan_devices=[], compute="not set up")
    monkeypatch.setattr(hw, "probe", lambda runner, **kw: p)
    assert cli.main(["doctor"]) == 0
    assert "path: encode=none" in capsys.readouterr().out
    assert cli.main(["doctor", "--json"]) == 0

