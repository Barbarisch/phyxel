---
name: phyxel-playtest
description: Use whenever building, launching, running, or iterating a Phyxel game — the engine lifecycle (launch/stop/restart and polling readiness), the build→launch→act→screenshot verify loop, and the critical project-vs-editor mode rule. Invoke at the start of any game-dev task that touches the running engine.
---

# Playtesting a Phyxel game (engine lifecycle + verify loop)

You drive the engine through the **`phyxel` MCP server**, which is already wired to THIS
project's own engine instance on its dedicated port (from `.phyxel/config.json`).

## Project vs editor mode — the #1 gotcha
The engine auto-launches for this project in **project mode** (`--project <this dir>`), so
`save_world` and all edits target **this project's** `worlds/default.db` and `game.json`.
Never drive a bare editor-mode engine for game work — changes would go to the engine's own
world, not the game.

## Lifecycle (always in this order)
1. `engine_running` — check first. If `process_alive` is false, the SessionStart hook should
   have launched it; otherwise `launch_engine`. (Or `phyxel up` from a shell.)
2. Poll `engine_running` until `api_responsive: true` before issuing commands.
3. **`stop_engine` before any rebuild** — the linker cannot overwrite a running `phyxel.exe`.
4. `build_project` → `launch_engine` again after a rebuild. For a long/full build, use
   `build_project` with `background: true` and poll `build_status` until it reports done, then launch.
5. `restart_engine` for a clean reload.

## Verify by RUNNING, never just "it loaded"
After any change, confirm at runtime: trigger the scenario, then `screenshot` (or
`get_visual_diagnostic`) and `get_engine_logs`. "The definition loaded" is not verification —
look at the result.

## Tight iteration loop
`load_game_definition` (or individual edits) → `screenshot` → adjust → repeat. Use
`set_camera` to frame what you're checking (overview: 30–50 units out, pitch −25° to −35°).
`create_snapshot` before risky edits; `restore_snapshot` to revert.

## Diagnostics work in any mode
`screenshot`, `get_engine_logs`, `get_render_stats`, `engine_running` work even with no
project/world loaded — handy when debugging a launch.

See `docs/GameCreationGuide.md` for the full workflow.
