"""The inference backend: Depth Anything V2 Small through ncnn on Vulkan, fp16 (spec 2 and 4).

`infer` takes the pre-processed image (float32, shape 3 x H x W, RGB, already divided by 255 and normalised
with the ImageNet mean and std) and returns the model's relative inverse depth as a float32 H x W array
(larger = nearer). The model files are the ones `setup depth` generates, one pair per input size.
"""

import os
from pathlib import Path

from xrdome_depth.export import output_name

INPUT = "in0"
OUTPUT = "out0"


class BackendError(Exception):
    """The backend cannot run: a message for the user. The same cause would hit every photo."""


class Backend:
    def infer(self, chw, width: int, height: int):
        """Depth for a 3 x height x width float32 array; returns a height x width float32 array."""
        raise NotImplementedError


class NcnnBackend(Backend):
    def __init__(self, models_dir: Path, vulkan: bool = True):
        self.models_dir = Path(models_dir)
        self.vulkan = vulkan
        self._nets: dict[tuple[int, int], object] = {}
        self._ncnn = None

    def _import(self):
        if self._ncnn is None:
            try:
                import ncnn
            except ImportError as e:
                raise BackendError(f"cannot import ncnn ({e}); run `xrdome-tools setup depth`") from e
            if self.vulkan and ncnn.get_gpu_count() == 0:
                raise BackendError("ncnn finds no Vulkan GPU (ncnn.get_gpu_count() is 0); depth on the CPU "
                                   "is slow and costs xrdome frames: setup depth --cpu, then try again")
            self._ncnn = ncnn
        return self._ncnn

    def _net(self, width: int, height: int):
        net = self._nets.get((width, height))
        if net is not None:
            return net
        files = [self.models_dir / output_name(width, height, ext) for ext in ("param", "bin")]
        for f in files:  # ncnn would carry on without them, so look first
            if not f.is_file():
                raise BackendError(f"model file {f} is missing; run `xrdome-tools setup depth`")
        ncnn = self._import()
        net = ncnn.Net()
        net.opt.use_vulkan_compute = self.vulkan
        # fp16 on the GPU: the files hold fp32 weights, ncnn converts them on load.
        net.opt.use_fp16_packed = net.opt.use_fp16_storage = net.opt.use_fp16_arithmetic = self.vulkan
        # ncnn reports a failed load only through the return code; the net would then run on nothing.
        loads = (("param", net.load_param), ("model", net.load_model))
        for (what, load), f in zip(loads, files):
            ret = load(os.fspath(f))
            if ret != 0:
                raise BackendError(f"ncnn could not load {f} (the {what} file; returned {ret}); "
                                   "run `xrdome-tools models --check`, then `xrdome-tools setup depth`")
        self._nets[(width, height)] = net
        return net

    def infer(self, chw, width: int, height: int):
        import numpy as np

        net = self._net(width, height)
        ncnn = self._ncnn
        ex = net.create_extractor()
        ex.input(INPUT, ncnn.Mat(np.ascontiguousarray(chw, dtype=np.float32)).clone())
        ret, out = ex.extract(OUTPUT)
        if ret != 0:
            raise BackendError(f"ncnn inference failed (extract {OUTPUT} returned {ret})")
        depth = np.array(out, dtype=np.float32)
        if depth.size != width * height:
            raise BackendError(f"the model returned {depth.shape}, expected {height} x {width}")
        return depth.reshape(height, width)
