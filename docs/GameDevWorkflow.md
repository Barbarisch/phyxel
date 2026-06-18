# Game-Dev Workflow — Claude Code per-project sessions

> Status: **IMPLEMENTED** (CLI + plugin + feedback loop all built; phased roadmap below is done).
> Architecture for using Claude Code as the game-development front-end for Phyxel: start a session
> in a game project directory and it already knows how to drive the engine, auto-launches the right
> instance, carries the standard game-dev procedures, and feeds lessons/requests back to engine
> development. **New machine / new contributor: do the "Per-machine setup" runbook below ONCE — if
> it's skipped, `.mcp.json`→`phyxel-mcp` and the `phyxel up` hook silently fail and sessions fall
> back to manual launches on port 8090 (collisions).**

## Per-machine setup (one-time, repeatable — do this on every machine)

```sh
# 1. Install the CLI (exposes the `phyxel` and `phyxel-mcp` console scripts on PATH).
pip install -e <engine-repo>/tools/phyxel-cli
# 2. Tell it where the engine lives on THIS machine (writes ~/.config|%APPDATA%\phyxel\config.json).
phyxel init --home <engine-repo>
# 3. Verify (engine_home valid, mcp server + engine bin resolved).
phyxel where
```
Then, in Claude Code (one-time): `/plugin marketplace add <engine-repo>` →
`/plugin install phyxel-gamedev@phyxel` (loads the `phyxel-*` skills + `/feedback` + the
SessionStart `phyxel up` hook). Verify with `/help`.

**Per project:** `phyxel new <name>` (minimal data project) or `phyxel link <dir>` to retrofit an
existing one; `tools/create_project.py` (full C++ project) auto-runs `phyxel link` for you. Each
project gets its own committed port (`.phyxel/config.json`) + path-free `.mcp.json` + `CLAUDE.md`,
so a Claude session opened in the folder drives that project's own engine instance.

## Goal

```
cd C:\Users\jack\Documents\PhyxelProjects\CharacterTestbed
claude        # <- session already knows Phyxel, engine auto-starts on this project's port
```

A session opened in a game project dir should, with zero manual setup:
1. Have the **Phyxel MCP** wired to *this* project's engine instance.
2. **Auto-launch** that engine (with the project loaded) if it isn't already running.
3. Carry the **standard game-dev knowledge** (characters, scenes, menus, assets, dialogue/
   story, playtest/build/package) — distinct from engine-*internals* context.
4. **Log lessons-learned / feature-requests** to a place engine-dev sessions consume.

Most of the heavy lifting already exists: the engine, the MCP server (large tool surface),
`tools/create_project.py`, and the guides. This is the **Claude-facing layer** on top.

## Decisions (locked 2026-06-06)

- **Cross-platform / cross-machine is a hard requirement** (Windows + Linux at least). This
  drives everything: **committed files contain ZERO absolute paths and ZERO OS assumptions;**
  all machine/OS specifics live behind a single per-machine indirection (the `phyxel` CLI +
  per-machine config). See "Portability" below.
- **Packaging:** a **Claude Code plugin committed in the engine repo** (`phyxel-gamedev`).
  Versioned with the engine, travels via git, one update updates all projects, no per-project
  duplication. Per-project files just *activate* it.
- **Ports:** **explicit per-project**, assigned at scaffold time (next free port, reserving
  8090 for the dev/editor default), stored as a plain integer in the project's committed
  `.phyxel/config.json` — portable as-is.
- **Feedback:** **`/feedback` command + log-as-you-go** (a CLAUDE.md instruction to record
  gotchas/requests as they arise) into a git-tracked inbox. No transcript auto-scraping.

## Portability (the load-bearing design choice)

Isolate all variability behind **one per-machine indirection point** so the committed surface
is identical on every machine/OS:

- **A small pip-installable `phyxel` CLI** (Python, `tools/phyxel-cli`) exposing console scripts
  `phyxel` and `phyxel-mcp`. Python + console scripts are the most reliably cross-platform
  option and dodge every path/interpreter trap (`python` vs `python3` handled by the shim;
  `phyxel-mcp.exe` shim auto-created on Windows).
- **One-time per machine:** `pip install -e <engine>/tools/phyxel-cli` + `phyxel init` records
  the engine location in an OS-appropriate user-config dir (`~/.config/phyxel/` on Linux/macOS,
  `%APPDATA%\phyxel\` on Windows) or honors a `PHYXEL_HOME` env var. **This is the only place
  that knows machine-specific paths.**
- The CLI owns every OS branch: engine binary name (`phyxel.exe` vs `phyxel`), detached-process
  spawn, path joining, build invocation.
- **Committed project files are path-free:** `.mcp.json` → `{"command": "phyxel-mcp"}` (project
  defaults to cwd, port read from `.phyxel/config.json`); SessionStart hook → `phyxel up`;
  `CLAUDE.md` → prose only.
- Lighter fallback (if we skip the CLI): `${PHYXEL_HOME}` env-var expansion in `.mcp.json`
  (Claude Code supports `${VAR}`), but the hook still hand-handles `python`/OS branches — the
  CLI is preferred for true multi-OS.

> **Dependency / likely out of initial scope:** actually *running* on Linux needs the **engine
> itself to build+run on Linux** (today: MSVC + PowerShell build, Windows `phyxel.exe`; Vulkan
> is portable in principle but the toolchain isn't proven there). The tooling above is designed
> OS-agnostic so it's ready when that port lands; the engine Linux build is tracked separately.

## Architecture

### 0. `phyxel` CLI + per-machine config (portability primitive)
See "Portability" above. A pip-installable `tools/phyxel-cli` exposing `phyxel` / `phyxel-mcp`
console scripts; `phyxel init` records engine location per machine. Every later piece routes
through it so committed files stay path-free. Build first.

### 1. Multi-instance ports (engine change)
Today `EngineAPIServer` and `scripts/mcp/phyxel_mcp_server.py` are hardwired to
`localhost:8090`. Changes:
- `phyxel[.exe] --port <N>` → `EngineAPIServer` binds `<N>` (default **8090** for back-compat).
  *(Already implemented: `main.cpp` parses `--port` → `setApiPortOverride` → the server.)*
- MCP server (`phyxel-mcp`) reads the port from the project's `.phyxel/config.json` (and project
  dir = cwd), exports `PHYXEL_API_PORT`; `phyxel_mcp_server.py` builds its target URL from it
  (or `PHYXEL_API_URL` full override). Engine location comes from the per-machine config.
- Result: N projects → N engines on N ports → N MCP servers, each pinned to its own engine.

### 2. Per-project bootstrap (`phyxel new` / extend `create_project.py`)
Scaffold (and a `phyxel link`/retrofit command for existing projects) writes, into the project
dir, files that are **path-free and OS-agnostic**:
- **`.mcp.json`** — bare command, no paths:
  ```json
  { "mcpServers": { "phyxel": { "command": "phyxel-mcp" } } }
  ```
  Claude Code auto-loads it from the project root (one-time approval); runs it with cwd = project
  root, so `phyxel-mcp` infers the project; it reads the port from `.phyxel/config.json`.
- **`.phyxel/config.json`** — committed, portable: `{ "apiPort": 8091 }` (+ any project-level
  knobs). Just data, no paths.
- **`CLAUDE.md`** — a *short* game-dev project file: "You're developing a Phyxel game. Engine MCP
  = `phyxel`. Standard workflow → use the phyxel-gamedev skills. Log feedback with `/feedback`."
  **Intentionally separate** from the engine repo's CLAUDE.md (engine internals — wrong audience).

### 3. Game-dev knowledge as plugin skills
Author from the existing guides (`GameCreationGuide.md`, scene/menu docs, the MCP tool list)
as model-invoked **skills** bundled in the plugin (shared by all projects):
- `phyxel-characters` — animated characters, NPCs, dialogue, story arcs.
- `phyxel-scenes` — multi-scene games, transitions, world generation.
- `phyxel-menus` — menus / UI.
- `phyxel-assets` — object templates + BlockSmith AI generation + placement.
- `phyxel-playtest` — the build → launch → screenshot → iterate loop (+ engine lifecycle rules).
- `phyxel-package` — build_game / run_game / package_game.

### 4. Auto-launch (SessionStart hook in the plugin)
The hook command is just **`phyxel up`** (portable — no script paths). The CLI reads the
project's port (`.phyxel/config.json`) → HTTP-pings `/api/status` → if dead, launches the
engine (OS-aware binary, from the per-machine engine location) **detached** with `--project
<cwd> --port <port>` + writes `<project>/.phyxel/engine.pid`. Idempotent (no-op if up).

### 5. Feedback loop
- **`/feedback <text>`** (plugin command, available in game-dev sessions): appends a
  structured entry to the engine repo's inbox — `docs/feedback/inbox.md`:
  ```
  ## 2026-06-06 — CharacterTestbed — feature-request
  <what was needed / what was learned>
  ```
  Types: `bug | gotcha | feature-request`. The project CLAUDE.md also instructs Claude to
  log as-it-goes (same discipline as the memory system).
- **`/triage-feedback`** (engine-dev side): reads the inbox, groups + summarizes, folds items
  into `AgentContext.md`'s roadmap, archives handled entries to `docs/feedback/archive.md`.
- The inbox lives **next to `AgentContext.md`/`docs/`**, exactly where engine-dev Claude
  already looks — closing the loop between the two worlds.

## Plugin layout (in the engine repo)
```
phyxel/
  tools/phyxel-cli/          # pip-installable: `phyxel`, `phyxel-mcp` console scripts
    pyproject.toml           # entry_points + deps (cross-platform)
    phyxel_cli/  __init__.py, cli.py (init/new/link; up=Phase4), mcp.py, paths.py, scaffold.py
  .claude-plugin/
    marketplace.json         # marketplace listing the plugin (install entry point)
  tools/phyxel-gamedev/      # the plugin itself
    .claude-plugin/plugin.json
    skills/  phyxel-playtest/SKILL.md, phyxel-world/, phyxel-characters/, phyxel-assets/,
             phyxel-mechanics/, phyxel-package/
    commands/  feedback.md, triage-feedback.md   # Phase 5
    hooks/  hooks.json  (SessionStart -> `phyxel up`)   # Phase 4
  docs/feedback/  inbox.md, archive.md   # Phase 5
```
Install (per machine): `/plugin marketplace add <repo>` then `/plugin install phyxel-gamedev@phyxel`.
Per machine (one-time): `pip install -e tools/phyxel-cli`, `phyxel init`, install the plugin
via `/plugin`. Per project: `phyxel new <name>` (or `phyxel link` to retrofit) writes the
path-free `.mcp.json` + `.phyxel/config.json` + `CLAUDE.md`.

## Phased roadmap
0. **CLI + per-machine config:** `tools/phyxel-cli` (`phyxel init`, resolves engine location
   per machine/OS). The portability primitive — build first.
1. **Ports:** engine `--port` (already exists) reports the real bound port; `phyxel-mcp` reads
   port from `.phyxel/config.json` and `phyxel_mcp_server.py` honors `PHYXEL_API_PORT`.
2. **Scaffold:** `phyxel new` / extend `create_project.py` — emit path-free `.mcp.json` +
   `.phyxel/config.json` (free port) + game-dev `CLAUDE.md`; `phyxel link` retrofits existing
   projects (e.g. CharacterTestbed).
3. **Skills:** author the `phyxel-*` skill set from the existing guides.
4. **Auto-launch:** SessionStart hook → `phyxel up`.
5. **Feedback:** `/feedback` + `/triage-feedback` + `docs/feedback/`.

Each phase is independently useful; 0+1 unblock everything.

## Open wrinkles / risks
- **Engine on Linux** (the real gate to actually using this off-Windows): MSVC + PowerShell
  build + Windows `phyxel.exe` today; Vulkan portable in principle. Tracked separately; tooling
  is OS-agnostic so it's ready when the port lands.
- **Per-machine setup is manual** (pip install + `phyxel init` + `/plugin`) — not automatic from
  a clone. Document a one-liner in the engine README.
- **Port exhaustion / stale pidfiles:** `phyxel up` must verify liveness (ping), not trust the pidfile.
- **Audit hardcoded `8090`** across docs/tools/MCP before parameterizing.
- **Claude Code MCP cwd assumption:** confirm project-scoped MCP servers run with cwd = project
  root (so `phyxel-mcp` can infer the project); fall back to a `--project` arg if not.
