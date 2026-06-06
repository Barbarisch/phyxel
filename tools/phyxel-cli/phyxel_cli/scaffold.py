"""Project scaffolding: write the path-free Claude-facing files into a game project.

`link` retrofits an existing project dir; `new` creates a minimal dev project then links it.
All emitted files are portable (no absolute paths, no OS assumptions) — see
docs/GameDevWorkflow.md.
"""
from __future__ import annotations

import json
from pathlib import Path
from typing import Optional

from . import paths

# The single portable MCP wiring every project commits. `phyxel-mcp` (a console script on
# PATH) resolves the engine per-machine and reads the port from .phyxel/config.json.
MCP_JSON = {
    "mcpServers": {
        "phyxel": {
            "command": "phyxel-mcp"
        }
    }
}

CLAUDE_MD_TEMPLATE = """\
# {name} — Phyxel game project

A **Phyxel game project** developed with Claude Code driving the engine. (Engine *internals*
live in the Phyxel repo — do not edit engine source from here; this is the *game*.)

## Engine & MCP
- The **`phyxel` MCP server** is wired (`.mcp.json`) and targets THIS project's own engine
  instance on port **{port}** (from `.phyxel/config.json`), so multiple projects can run at once.
- The engine auto-starts for this project on session start (or run `phyxel up`).
- Drive everything through the MCP tools: `build_project`, `launch_engine`/`stop_engine`/
  `engine_running`, `screenshot`, `get_engine_logs`, and the game-building tools
  (`load_game_definition`, `create_game_npc`, `add_scene`, `spawn_template`, `fill_region`, …).
- **Verify changes by running the engine** (launch → act → `screenshot`), not just by editing.

## Layout
- `game.json` — world + player + camera + npcs + story (loaded at launch via `--project`).
- `worlds/` — SQLite world DB(s).
- `.phyxel/config.json` — this project's engine API port (committed, portable).

## Workflow
Use the `phyxel-*` skills (characters, scenes, menus, assets, playtest, package) for standard
procedures. Log gotchas / engine feature-requests with `/feedback` so they reach engine dev.
"""

# A minimal dev-project game.json for `phyxel new` (flat ground + an animated player).
NEW_GAME_JSON = {
    "name": "",
    "description": "A new Phyxel game project.",
    "version": "1.0",
    "player": {"type": "animated", "position": {"x": 16, "y": 17, "z": 16}},
    "camera": {"position": {"x": 16, "y": 25, "z": 30}, "yaw": -180, "pitch": -25},
    "world": {"type": "Flat", "from": {"x": 0, "y": 0, "z": 0}, "to": {"x": 0, "y": 0, "z": 0}},
}


def _write_json(path: Path, data: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def link(project_dir: Path, port: Optional[int] = None) -> dict:
    """Write the Claude-facing files into an existing project dir. Idempotent: reuses an
    already-assigned port; never clobbers an existing CLAUDE.md."""
    project_dir = project_dir.expanduser().resolve()
    if not project_dir.is_dir():
        raise NotADirectoryError(f"{project_dir} is not a directory")

    # Port: explicit arg > already-committed port > freshly allocated.
    existing = paths.project_config(project_dir).get("apiPort")
    if port is not None:
        chosen = port
        paths.record_port(chosen)
    elif existing is not None:
        chosen = int(existing)
        paths.record_port(chosen)
    else:
        chosen = paths.allocate_port()

    actions = []
    _write_json(project_dir / ".phyxel" / "config.json", {"apiPort": chosen})
    actions.append(".phyxel/config.json")
    _write_json(project_dir / ".mcp.json", MCP_JSON)
    actions.append(".mcp.json")

    claude_md = project_dir / "CLAUDE.md"
    if claude_md.exists():
        actions.append("CLAUDE.md (kept existing)")
    else:
        claude_md.write_text(
            CLAUDE_MD_TEMPLATE.format(name=project_dir.name, port=chosen), encoding="utf-8")
        actions.append("CLAUDE.md")

    return {"project": str(project_dir), "port": chosen, "wrote": actions}


def new(name: str, output_dir: Path, port: Optional[int] = None) -> dict:
    """Create a minimal dev project (game.json + worlds/) then link it."""
    output_dir = output_dir.expanduser().resolve()
    if output_dir.exists() and any(output_dir.iterdir()):
        raise FileExistsError(f"{output_dir} already exists and is not empty")
    output_dir.mkdir(parents=True, exist_ok=True)

    gj = dict(NEW_GAME_JSON)
    gj["name"] = name
    _write_json(output_dir / "game.json", gj)
    (output_dir / "worlds").mkdir(exist_ok=True)

    result = link(output_dir, port=port)
    result["created"] = True
    return result
