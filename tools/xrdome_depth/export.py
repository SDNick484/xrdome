#!/usr/bin/env python3
"""export.py --out DIR --work DIR: Depth Anything V2 Small to ncnn at the three sizes xrdome uses.

Runs in the temporary export venv made by `xrdome-tools setup depth` (torch CPU, transformers, pnnx,
huggingface_hub), as a script: it imports nothing from xrdome_tools. Spec 4 is the contract:

  - the model is depth-anything/Depth-Anything-V2-Small-hf at REVISION; its model.safetensors must
    have sha256 SAFETENSORS_SHA256 before anything is traced;
  - traced with attn_implementation="eager", converted with pnnx, fp32 weights, input in0, output out0;
  - one dav2s_<W>x<H>.ncnn.{param,bin} per size in SIZES, written to --out, plus a copy of the verified
    model.safetensors. setup checks the sha256 of every output; this script does not.

--work holds the Hugging Face cache and pnnx's scratch files; setup deletes it afterwards.
"""

import argparse
import hashlib
import shutil
import sys
from pathlib import Path

REPO = "depth-anything/Depth-Anything-V2-Small-hf"
REVISION = "5426e4f0f36572d16453bbda7a8389317b1bef99"
SAFETENSORS = "model.safetensors"
SAFETENSORS_SHA256 = "3152477ce0d8d6978d76b995120de97cb5b928701fd0f817769f59e249a16b70"
# (width, height), multiples of 14: landscape photo, portrait photo, video.
SIZES = ((518, 392), (392, 518), (322, 182))


def output_name(width: int, height: int, ext: str) -> str:
    """The file `setup depth` expects: ext is "param" or "bin"."""
    return f"dav2s_{width}x{height}.ncnn.{ext}"


def _sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", required=True, type=Path, help="where the ncnn files and the source copy go")
    ap.add_argument("--work", required=True, type=Path, help="scratch: Hugging Face cache, pnnx files")
    args = ap.parse_args(argv)

    # Imported here so the constants above can be read without torch installed.
    import pnnx
    import torch
    from huggingface_hub import hf_hub_download
    from transformers import AutoModelForDepthEstimation

    cache = args.work / "hf"
    cached = Path(hf_hub_download(REPO, SAFETENSORS, revision=REVISION, cache_dir=str(cache)))
    got = _sha256(cached)
    if got != SAFETENSORS_SHA256:
        print(f"export: {SAFETENSORS} sha256 is {got}, expected {SAFETENSORS_SHA256}; refusing to export",
              file=sys.stderr)
        return 2

    class Wrap(torch.nn.Module):
        def __init__(self, m):
            super().__init__()
            self.m = m

        def forward(self, x):
            return self.m(pixel_values=x).predicted_depth

    model = AutoModelForDepthEstimation.from_pretrained(
        REPO, revision=REVISION, cache_dir=str(cache), attn_implementation="eager").eval()
    wrapped = Wrap(model).eval()
    torch.manual_seed(0)
    args.out.mkdir(parents=True, exist_ok=True)
    for width, height in SIZES:
        d = args.work / f"pnnx-{width}x{height}"
        d.mkdir(parents=True, exist_ok=True)
        pt = d / "m.pt"
        pnnx.export(wrapped, str(pt), torch.randn(1, 3, height, width), fp16=False, optlevel=2)
        for ext in ("param", "bin"):
            shutil.copyfile(d / f"m.ncnn.{ext}", args.out / output_name(width, height, ext))
        print(f"export: {width}x{height} ok")
    shutil.copyfile(cached, args.out / SAFETENSORS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
