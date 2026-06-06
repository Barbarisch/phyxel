# phyxel-cli

The cross-platform indirection layer for the Phyxel game-dev workflow (see
`docs/GameDevWorkflow.md`). It's the **only** place that knows machine/OS-specific paths, so
everything a game project commits (`.mcp.json`, `.phyxel/config.json`, `CLAUDE.md`) stays
path-free and identical on every machine/OS.

## One-time setup (per machine)

```
python -m pip install -e tools/phyxel-cli
phyxel init            # run from the engine repo root (records its location)
phyxel where           # verify resolved paths
```

`phyxel init` stores the engine location in the per-user config
(`%APPDATA%\phyxel\config.json` on Windows, `~/.config/phyxel/config.json` on Linux,
`~/Library/Application Support/phyxel/config.json` on macOS). A `PHYXEL_HOME` env var
overrides it.

## Commands

| Command        | Status   | Purpose |
|----------------|----------|---------|
| `phyxel init`  | ✅ Phase 0 | Record the engine repo location for this machine. |
| `phyxel where` | ✅ Phase 0 | Print resolved config/paths (diagnostics). |
| `phyxel-mcp`   | wired    | Launch the MCP server against the resolved engine + project port. The bare command a project's `.mcp.json` references. (Engine `--api-port` plumbing lands in Phase 1.) |
| `phyxel new`   | Phase 2  | Scaffold a new game project (path-free `.mcp.json` + `.phyxel/config.json` + `CLAUDE.md`). |
| `phyxel link`  | Phase 2  | Retrofit an existing project. |
| `phyxel up`    | Phase 4  | Ensure this project's engine instance is running (SessionStart hook target). |

> Note: `phyxel-mcp` runs the existing `scripts/mcp/phyxel_mcp_server.py` with the same Python
> environment, so install its deps there too: `pip install mcp httpx`.
