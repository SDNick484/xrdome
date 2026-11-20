import subprocess
from pathlib import Path

import pytest

from xrdome_tools import hw
from xrdome_tools.errors import ToolError
from xrdome_tools.run import ChildFailed

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


def body(name: str) -> str:
    return fx(name).split("\n", 1)[1]


def test_parse_torch_sees():
    assert hw.parse_torch_sees(body("torch_sees_gfx1152.txt")) == ("rocm", "gfx1152")
    assert hw.parse_torch_sees("True\n12.8\nNone\n-\n") == ("cuda", "")
    assert hw.parse_torch_sees("True\nNone\n6.4\ngfx90a:sramecc+:xnack-\n") == ("rocm", "gfx90a")
    assert hw.parse_torch_sees("False\nNone\n6.4\n-\n") == ("none", "")
    assert hw.parse_torch_sees("") == ("none", "")


def test_parse_kfd_target():
    assert hw.parse_kfd_target("cpu_cores_count 8\ngfx_target_version 110502\nsimd_count 16\n") == "gfx1152"
    assert hw.parse_kfd_target("gfx_target_version 90010\n") == "gfx90a"
    assert hw.parse_kfd_target("gfx_target_version 0\n") == ""
    assert hw.parse_kfd_target("") == ""


def test_torch_error_line():
    tail = body("torch_kernels_gfx1152_no_override.txt").splitlines()
    assert hw.torch_error_line(tail, 1) == "torch.AcceleratorError: HIP error: invalid device function"
    tail = body("torch_kernels_gfx1152_override_11.0.0.txt").splitlines()
    assert hw.torch_error_line(tail, -6).startswith("HSA_STATUS_ERROR_INVALID_ISA: The instruction")
    assert hw.torch_error_line([], -6) == "exited with status -6"


def test_torch_problem_names_the_fix():
    no = hw.torch_problem("rocm", "gfx1152", "invalid device function", "")
    assert "no GPU code for gfx1152" in no and "HSA_OVERRIDE_GFX_VERSION=11.0.2" in no and "--cpu" in no
    wrong = hw.torch_problem("rocm", "gfx1152", "INVALID_ISA", "11.0.0")
    assert "with HSA_OVERRIDE_GFX_VERSION=11.0.0" in wrong and "works with HSA_OVERRIDE_GFX_VERSION=11.0.2" in wrong
    right = hw.torch_problem("rocm", "gfx1152", "out of memory", "11.0.2")
    assert "works with" not in right and "--cpu" in right
    other = hw.torch_problem("rocm", "gfx1036", "invalid device function", "")
    assert "gfx1036" in other and "same family" in other
    assert "nvidia-smi" in hw.torch_problem("cuda", "", "no kernel image", "")


class TorchRunner(hw.Runner):
    def __init__(self, sees: str, fail: list[str] | None):
        super().__init__()
        self.sees, self.fail = sees, fail

    def run(self, argv, **kw):
        if argv[-1] == hw.TORCH_SEES:
            return subprocess.CompletedProcess(argv, 0, self.sees, "")
        if self.fail is not None:
            raise ChildFailed(argv, -6, self.fail)
        return subprocess.CompletedProcess(argv, 0, "ok 0.0\n", "")


def test_check_torch(monkeypatch):
    monkeypatch.delenv("HSA_OVERRIDE_GFX_VERSION", raising=False)
    monkeypatch.setattr(hw, "_kfd_arch", lambda: "")
    sees = body("torch_sees_gfx1152.txt")
    ok = hw.check_torch(TorchRunner(sees, None), Path("/py"))
    assert ok == hw.TorchCheck("rocm", "rocm", "gfx1152")
    tail = body("torch_kernels_gfx1152_no_override.txt").splitlines()
    bad = hw.check_torch(TorchRunner(sees, tail), Path("/py"))
    assert bad.compute == "none" and bad.seen == "rocm" and "HSA_OVERRIDE_GFX_VERSION=11.0.2" in bad.problem
    monkeypatch.setenv("HSA_OVERRIDE_GFX_VERSION", "11.0.0")
    tail = body("torch_kernels_gfx1152_override_11.0.0.txt").splitlines()
    bad = hw.check_torch(TorchRunner(sees, tail), Path("/py"))
    assert "INVALID_ISA" in bad.problem and "with HSA_OVERRIDE_GFX_VERSION=11.0.0" in bad.problem
    # Under an override PyTorch names the overridden chip; the kernel still names the real one.
    monkeypatch.setattr(hw, "_kfd_arch", lambda: "gfx1152")
    bad = hw.check_torch(TorchRunner("True\nNone\n6.4\ngfx1100\n", tail), Path("/py"))
    assert bad.arch == "gfx1152" and "works with HSA_OVERRIDE_GFX_VERSION=11.0.2" in bad.problem
    none = hw.check_torch(TorchRunner("False\nNone\nNone\n-\n", None), Path("/py"))
    assert none == hw.TorchCheck("none", "none", "")
