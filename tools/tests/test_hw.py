from pathlib import Path

import pytest

from xrdome_tools import hw
from xrdome_tools.errors import ToolError

FIX = Path(__file__).parent / "fixtures" / "hw"


def fx(name: str) -> str:
    return (FIX / name).read_text()


def probe(built=(), ok=None, **kw) -> hw.Probe:
    base = dict(platform="linux", gpus=[], encoders_built=set(built), encoders_ok=ok or {},
                vaapi=None, vulkan_devices=[], compute="none")
    base.update(kw)
    return hw.Probe(**base)


def test_fixtures_name_their_source():
    for f in FIX.glob("*.txt"):
        assert f.read_text().startswith("# source: "), f.name


def test_parse_proc_version():
    assert hw.parse_proc_version(fx("proc_version_wsl2.txt")) is True
    assert hw.parse_proc_version(fx("proc_version_gentoo.txt")) is False


def test_parse_drm_vendors():
    assert hw.parse_drm_vendors(["0x10de", "0x1002", "0x8086", "0x1234"]) == ["nvidia", "amd", "intel"]
    assert hw.parse_drm_vendors(["0x1002\n", "0x1002"]) == ["amd"]


def test_parse_ffmpeg_encoders():
    six = {"hevc_nvenc", "av1_nvenc", "hevc_vaapi", "av1_vaapi", "libx265", "libsvtav1"}
    assert six <= hw.parse_ffmpeg_encoders(fx("ffmpeg_encoders_full.txt"))
    assert not six & hw.parse_ffmpeg_encoders(fx("ffmpeg_encoders_minimal.txt"))
    assert "libx264" in hw.parse_ffmpeg_encoders(fx("ffmpeg_encoders_minimal.txt"))


def test_parse_vainfo():
    v = hw.parse_vainfo(fx("vainfo_radeon.txt"))
    assert v.encode >= {"VAProfileHEVCMain", "VAProfileHEVCMain10", "VAProfileAV1Profile0"}
    assert "VAProfileHEVCMain" in v.decode
    assert hw.parse_vainfo(fx("vainfo_decode_only.txt")).encode == set()


def test_parse_vulkaninfo_summary():
    assert hw.parse_vulkaninfo_summary(fx("vulkaninfo_summary_radeon.txt")) == [
        "AMD Radeon 780M Graphics (RADV PHOENIX)", "llvmpipe (LLVM 18.1.8, 256 bits)"]


def test_parse_nvidia_smi_list():
    assert hw.parse_nvidia_smi_list(fx("nvidia_smi_L.txt")) == ["NVIDIA GeForce RTX 4080"]


ALL = {"hevc_nvenc", "av1_nvenc", "hevc_vaapi", "av1_vaapi", "libx265", "libsvtav1"}


def test_choose_encoder_order():
    p = probe(ALL, {"hevc_nvenc": "ok", "hevc_vaapi": "ok"})
    assert hw.choose_encoder(p, "hevc").name == "hevc_nvenc"
    p = probe(ALL, {"hevc_nvenc": "Cannot load libcuda.so.1", "hevc_vaapi": "ok"})
    assert hw.choose_encoder(p, "hevc").name == "hevc_vaapi"
    p = probe({"libx265", "libsvtav1"})
    c = hw.choose_encoder(p, "hevc")
    assert (c.name, c.family) == ("libx265", "software") and "slow" in c.reason
    assert hw.choose_encoder(p, "av1").name == "libsvtav1"


def test_choose_encoder_listed_but_broken():
    err = "Cannot load libcuda.so.1"
    p = probe(ALL, {"hevc_nvenc": err, "hevc_vaapi": "ok"})
    c = hw.choose_encoder(p, "hevc")
    assert c.name == "hevc_vaapi" and c.family == "vaapi" and err in c.reason


def test_choose_encoder_override_broken():
    p = probe(ALL, {"hevc_nvenc": "Cannot load libcuda.so.1", "hevc_vaapi": "ok"})
    with pytest.raises(ToolError) as e:
        hw.choose_encoder(p, "hevc", override="hevc_nvenc")
    assert e.value.code == 2 and "Cannot load libcuda.so.1" in str(e.value)
    with pytest.raises(ToolError) as e:
        hw.choose_encoder(probe({"libx265"}), "hevc", override="hevc_vaapi")
    assert e.value.code == 2
    assert hw.choose_encoder(p, "hevc", override="hevc_vaapi").name == "hevc_vaapi"


def test_choose_encoder_nothing_usable():
    with pytest.raises(ToolError) as e:
        hw.choose_encoder(probe(), "hevc")
    assert e.value.code == 2


def test_test_encode_argv():
    va = hw.encode_test_argv("ffmpeg", "hevc_vaapi", "/dev/dri/renderD128")
    joined = " ".join(va)
    assert "-vaapi_device /dev/dri/renderD128" in joined
    assert "format=nv12,hwupload" in joined and joined.endswith("-f null -")
    nv = " ".join(hw.encode_test_argv("ffmpeg", "hevc_nvenc"))
    assert "-vaapi_device" not in nv and "hwupload" not in nv and nv.endswith("-f null -")
    assert "testsrc2=s=256x256:d=1" in nv
