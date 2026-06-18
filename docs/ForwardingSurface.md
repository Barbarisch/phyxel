# Forwarding Surface — what must stay in sync with engine changes

The engine feeds information to **future Claude sessions** (engine-dev *and* game-dev) through a
fixed set of files — the **forwarding surface**. When engine code changes, the matching surface
must change too, or sessions get stale guidance (e.g. a skill that says "objectives show top-right"
after the HUD moved them top-left). This doc is the map: **change type → what to update.** It is
referenced by the `/sync-docs` command and the `tools/check_doc_sync.py` gate (pre-push hook +
CI), which fails a push that changes engine code without touching any surface file.

## The surface (downstream readers)
| File / area | Audience | Holds |
|---|---|---|
| `docs/AgentContext.md` | engine-dev sessions | portable working context + roadmap (update at session end) |
| `docs/<Feature>.md` (HudSystem, TurnBasedCombat, WaterSystem, CameraControlSystem, DestructionSystem, …) | both | per-subsystem design + ground truth |
| `docs/GameCreationGuide.md` | game-dev | how to build a game with the MCP tools |
| `docs/GameDevWorkflow.md` | both | the per-project session workflow + per-machine setup |
| `scripts/mcp/phyxel_mcp_server.py` + engine API handlers | game-dev (the **API**) | MCP tool list + descriptions |
| `tools/phyxel-gamedev/skills/*` | game-dev | procedures (world, characters, assets, mechanics, playtest, package) |
| `tools/phyxel-gamedev/commands/*` | both | `/feedback`, `/triage-feedback` |
| `tools/phyxel-cli/phyxel_cli/scaffold.py` (CLAUDE.md / .mcp.json templates) | new game projects | what every project session is told |
| `tools/create_project.py` | full C++ project scaffold | generated project + wiring |
| `CLAUDE.md`, `README` | engine-dev | engine overview / build |

## Change type -> update these
- **New/changed/removed MCP command or tool param** → the tool's description in `scripts/mcp/...`
  + the engine handler; the relevant `phyxel-*` skill; `docs/GameCreationGuide.md`.
- **UI / HUD / menus / dialogue** → `docs/HudSystem.md`; `phyxel-mechanics` skill (HUD/menus
  sections); the `scaffold.py` CLAUDE.md UI section; `resources/ui/default_hud.json` if defaults
  changed.
- **New gameplay subsystem** (combat, items, story, …) → `docs/<it>.md`; `AgentContext.md`
  roadmap; the matching skill (`phyxel-mechanics`/`phyxel-characters`/…).
- **World gen / scenes / entities / materials** → `phyxel-world` skill; `docs/GameCreationGuide.md`;
  CLAUDE.md material/entity tables.
- **Characters / NPCs / dialogue / story** → `phyxel-characters` skill; relevant docs.
- **Build / lifecycle / engine flags** (`--port`, ports, multi-instance) → `AgentContext.md`
  operational lessons; `docs/GameDevWorkflow.md`; `phyxel-playtest` skill.
- **CLI / workflow / scaffolding** (`tools/phyxel-cli`, `create_project.py`) → `docs/GameDevWorkflow.md`
  (incl. the per-machine setup runbook); `scaffold.py` templates.
- **Packaging / bundled assets** → `tools/package_game.py` REQUIRED_RESOURCES; `phyxel-package` skill.

## The gate
`tools/check_doc_sync.py` is a **mechanical forget-gate**: if a push changes `engine/`, `editor/`,
`scripts/mcp/`, or `shaders/` but no surface file, it fails — pointing here + at `/sync-docs`.
It can't verify the *content* is right (that's `/sync-docs`/review), only that the surface was
considered. Genuinely doc-irrelevant engine changes opt out with **`[skip-docs]`** in a commit
message. Wired as: a pre-push hook (`.githooks/pre-push`, enabled via
`git config core.hooksPath .githooks`) and a GitHub Action (`.github/workflows/doc-sync.yml`).
