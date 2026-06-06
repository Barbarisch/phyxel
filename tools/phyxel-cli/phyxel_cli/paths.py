"""Cross-platform path & per-machine config resolution.

This module is the ONLY place that knows machine- and OS-specific details (where the engine
repo lives, the executable name, the user-config location). Keeping that knowledge here is
what lets every committed project file (.mcp.json, .phyxel/config.json, CLAUDE.md) stay
path-free and identical on every machine/OS.
"""
from __future__ import annotations

import json
import os
import sys
from pathlib import Path
from typing import Optional

APP = "phyxel"


# --- per-machine user config -------------------------------------------------------------

def config_dir() -> Path:
    """OS-appropriate per-user config directory for Phyxel."""
    if sys.platform.startswith("win"):
        base = os.environ.get("APPDATA") or str(Path.home() / "AppData" / "Roaming")
        return Path(base) / APP
    if sys.platform == "darwin":
        return Path.home() / "Library" / "Application Support" / APP
    base = os.environ.get("XDG_CONFIG_HOME") or str(Path.home() / ".config")
    return Path(base) / APP


def config_path() -> Path:
    return config_dir() / "config.json"


def load_config() -> dict:
    p = config_path()
    if p.is_file():
        try:
            return json.loads(p.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            return {}
    return {}


def save_config(cfg: dict) -> Path:
    config_dir().mkdir(parents=True, exist_ok=True)
    p = config_path()
    p.write_text(json.dumps(cfg, indent=2), encoding="utf-8")
    return p


# --- engine location ---------------------------------------------------------------------

def engine_home() -> Optional[Path]:
    """Resolve the Phyxel engine repo root.

    `PHYXEL_HOME` env var wins (handy for CI / one-off overrides); otherwise the value saved
    by `phyxel init`. Returns None if neither is set.
    """
    env = os.environ.get("PHYXEL_HOME")
    if env:
        return Path(env).expanduser()
    h = load_config().get("engine_home")
    return Path(h).expanduser() if h else None


def looks_like_engine(path: Path) -> bool:
    """Heuristic check that `path` is the Phyxel engine repo root."""
    return (path / "scripts" / "mcp" / "phyxel_mcp_server.py").is_file()


def mcp_server_script(home: Path) -> Path:
    return home / "scripts" / "mcp" / "phyxel_mcp_server.py"


def engine_binary(home: Path) -> Optional[Path]:
    """Best-effort path to the built engine executable (OS-aware; searches build locations)."""
    name = "phyxel.exe" if sys.platform.startswith("win") else "phyxel"
    candidates = [
        home / name,
        home / "build" / "editor" / "Debug" / name,
        home / "build" / "editor" / "Release" / name,
        home / "build" / "editor" / name,
    ]
    return next((c for c in candidates if c.is_file()), None)


# --- per-project config (committed, portable: just data, no paths) -----------------------

DEFAULT_API_PORT = 8090  # reserved for the dev/editor default single instance


def project_config(project_dir: Path) -> dict:
    p = project_dir / ".phyxel" / "config.json"
    if p.is_file():
        try:
            return json.loads(p.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            return {}
    return {}


def project_api_port(project_dir: Path, default: int = DEFAULT_API_PORT) -> int:
    return int(project_config(project_dir).get("apiPort", default))
