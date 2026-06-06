"""`phyxel-mcp` entry: launch the Phyxel MCP server against the resolved engine.

This is the single portable command a project's committed `.mcp.json` references
(`{"command": "phyxel-mcp"}`). It resolves the engine location (per-machine config) and the
project's API port (`.phyxel/config.json` in the cwd), then hands off to the existing MCP
server. The engine-side `--api-port` plumbing lands in Phase 1; until then the port env is
still exported so the wiring is ready.
"""
from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

from . import paths


def main(argv=None) -> int:
    home = paths.engine_home()
    if home is None or not paths.looks_like_engine(home):
        print(
            "phyxel-mcp: engine home not set or invalid. "
            "Run `phyxel init --home <engine repo>` once on this machine.",
            file=sys.stderr,
        )
        return 1

    project = Path(os.environ.get("PHYXEL_PROJECT") or Path.cwd()).expanduser()
    port = paths.project_api_port(project)
    server = paths.mcp_server_script(home)

    env = dict(os.environ)
    env["PHYXEL_API_PORT"] = str(port)
    env["PHYXEL_PROJECT"] = str(project)
    env["PHYXEL_HOME"] = str(home)

    # Run the server from the engine repo; sibling imports resolve off the script dir.
    args = [sys.executable, str(server)]
    if not sys.platform.startswith("win") and hasattr(os, "execve"):
        # POSIX: replace this process so the stdio MCP transport owns stdin/stdout directly.
        os.chdir(home)
        os.execve(sys.executable, args, env)
        return 0  # unreachable
    # Windows: no exec semantics; run as a child that inherits our stdio, and wait.
    return subprocess.run(args, env=env, cwd=str(home)).returncode


if __name__ == "__main__":
    raise SystemExit(main())
