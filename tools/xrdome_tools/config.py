"""Where xrdome-tools keeps its configuration, data and cache."""

import configparser
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping


@dataclass(frozen=True)
class Paths:
    config_file: Path
    data_dir: Path
    cache_dir: Path
    iw3_dir: Path
    models_dir: Path


def _xdg(env: Mapping[str, str], var: str, fallback: str) -> Path:
    # The XDG spec says a relative value is invalid and must be ignored.
    value = env.get(var, "")
    if value and os.path.isabs(value):
        return Path(value)
    return Path(env.get("HOME") or os.path.expanduser("~")) / fallback


def paths(env: Mapping[str, str] = os.environ) -> Paths:
    config = _xdg(env, "XDG_CONFIG_HOME", ".config")
    data = _xdg(env, "XDG_DATA_HOME", ".local/share") / "xrdome-tools"
    cache = _xdg(env, "XDG_CACHE_HOME", ".cache") / "xrdome-tools"
    return Paths(
        config_file=config / "xrdome" / "tools.ini",
        data_dir=data,
        cache_dir=cache,
        iw3_dir=data / "iw3",
        models_dir=data / "models",
    )


def program(name: str, p: Paths | None = None) -> str:
    """The program to run for `name`: [programs] <name> in tools.ini, else `name`."""
    p = p or paths()
    cp = configparser.ConfigParser(interpolation=None)
    try:
        cp.read(p.config_file, encoding="utf-8")
    except (OSError, configparser.Error):
        return name
    return cp.get("programs", name, fallback="").strip() or name
