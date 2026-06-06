"""Ensure a project's engine instance is running — backs `phyxel up` and the SessionStart hook.

Idempotent: if the project's API port already answers, do nothing; otherwise launch the engine
detached (OS-aware) on that port with the project loaded, and write a pidfile.
"""
from __future__ import annotations

import subprocess
import sys
import urllib.error
import urllib.request
from pathlib import Path

from . import paths


def _ping(port: int, timeout: float = 1.0) -> bool:
    try:
        with urllib.request.urlopen(f"http://localhost:{port}/api/status", timeout=timeout) as r:
            return 200 <= getattr(r, "status", 200) < 300
    except (urllib.error.URLError, OSError):
        return False


def _spawn_detached(args: list[str], cwd: Path) -> subprocess.Popen:
    """Launch fully detached so the engine outlives this short-lived CLI/hook process."""
    common = dict(
        cwd=str(cwd),
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        close_fds=True,
    )
    if sys.platform.startswith("win"):
        DETACHED_PROCESS = 0x00000008
        CREATE_NEW_PROCESS_GROUP = 0x00000200
        return subprocess.Popen(args, creationflags=DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, **common)
    return subprocess.Popen(args, start_new_session=True, **common)


def ensure_up(project_dir: Path) -> dict:
    """Make sure the engine is running for `project_dir`. Returns a status dict."""
    project_dir = project_dir.expanduser().resolve()

    # Only act in LINKED projects. The SessionStart hook runs in every Claude
    # session (engine repo, random dirs, ...) — without this guard it would launch
    # the engine with --project <whatever-cwd> on the default port.
    if not (project_dir / ".phyxel" / "config.json").is_file():
        return {"status": "skipped",
                "reason": f"{project_dir} is not a linked Phyxel project (no .phyxel/config.json)"}

    port = paths.project_api_port(project_dir)

    if _ping(port):
        return {"status": "already-running", "port": port, "project": str(project_dir)}

    home = paths.engine_home()
    if home is None or not paths.looks_like_engine(home):
        return {"status": "error", "error": "engine home not set/invalid - run `phyxel init`"}
    binary = paths.engine_binary(home)
    if binary is None:
        return {"status": "error", "error": f"engine binary not found under {home} - build it first"}

    # cwd = engine repo (the engine resolves shaders/resources relative to it; the project's
    # game.json/worlds come from --project).
    args = [str(binary), "--project", str(project_dir), "--port", str(port)]
    proc = _spawn_detached(args, cwd=home)

    pid_dir = project_dir / ".phyxel"
    pid_dir.mkdir(parents=True, exist_ok=True)
    (pid_dir / "engine.pid").write_text(str(proc.pid), encoding="utf-8")

    return {"status": "launched", "port": port, "pid": proc.pid,
            "project": str(project_dir), "binary": str(binary)}
