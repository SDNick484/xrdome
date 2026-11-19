"""Code that runs inside the depth venvs (numpy, ncnn, and for export only, PyTorch).

Not imported by xrdome_tools, which stays standard-library only; `setup depth` and (later) `depth`
start these modules with the venv's interpreter, like `to3d` runs iw3 in its venv.
"""
