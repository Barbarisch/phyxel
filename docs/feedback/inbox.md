# Feedback inbox

Lessons-learned and feature-requests captured during Phyxel **game-dev** sessions, appended by
`phyxel feedback` (usually via the `/feedback` command). **Engine-dev** sessions consume this
via `/triage-feedback`: summarize, fold actionable items into `docs/AgentContext.md`'s roadmap,
then move handled entries to `archive.md`.

Entry format:

    ## <date> — <project> — <bug|gotcha|feature-request>
    <description>

---
## 2026-07-12 — Emberwake — feature-request
Axe tool item + chopping/gathering swing animation. Survival/RPG gathering needs a real axe item (equippable via RpgItem/EquipmentSystem) plus a chop/swing animation family (like melee_anim_families) so chopping a tree for wood is an actual swing with wind-up + impact, not an instant voxel break. Drives the survival gather verb (Emberwake) and general woodcutting.

## 2026-07-12 — Emberwake — feature-request
Satisfying tree/object destruction - a chopped tree should topple and fall, not vanish. When a tree is chopped through, it should physically fall over (coherent-fragment destruction via the GPU AVBD / VoxelDynamicsWorld bonds-to-weld path) and settle as gatherable logs/debris. Applies to destructible objects broadly; survival wood-gathering is the driving case, so felling reads as a real event.

## 2026-07-12 — Emberwake — feature-request
MCP input-injection tool for controllability verification (surfaced by the game-production dogfood). The tracker's L4 'player is controllable' milestone cannot be auto-verified because no MCP tool can simulate WASD/keypress input to the running engine - only entity placement is checkable. Add a tool to inject synthetic input (move/jump/attack key events) so agent verification can confirm player control + movement without a human at the keyboard.

## 2026-07-12 — Emberwake — RETRACTED (was filed as a terrain-gen bug — it is NOT a bug)
RETRACTED: operator error, terrain-gen v2 works correctly. I had claimed Perlin made a "flat stone plane at y31"; the truth is I generated only LOW chunk layers (chunk y=0..1, world y 0-63) via generate_world / a game.json `world` from/to range, but the v2 surface sits HIGH (kSeaLevelY=16, peaks ~384 above), so my range was entirely DEEP UNDERGROUND → solid Stone (materialForColumn: depth>=4 = deepMaterial). Verified correct once I generated a tall enough range: varied surface_y (74 / 75 / 96 across a small area) with proper Grass/Dirt-over-Stone layering. OPTIONAL usability nit for engine-dev (low priority, NOT a bug): static generation (generate_world / a shallow game.json from/to y-range) silently yields underground stone with no hint the surface is far above — an auto-surface-follow or a warning would prevent this trap. Documenting the gotcha in CLAUDE.md.

## 2026-07-12 — Emberwake — feature-request
Expose average FPS / frame time via the HTTP API (e.g. an 'fps'/'frameTimeMs' field on /api/render/stats or /api/status). Today /api/render/stats has mesh + chunk-update timings but no actual frame FPS, so the game-production tracker cannot runtime-validate the perf_target milestone. Needed for production(op=validate) runtime perf checks.

## 2026-07-12 — Emberwake — feature-request
API to fire a declarative trigger's 'when' event on demand AND query the current shell screen state (playing / victory / game_over / menu / pause). Lets the game-production tracker runtime-validate win_condition/lose_condition (the trigger actually fires -> the victory/game-over screen shows) without a human. Ties into the triggers[] {when,then} system (TriggerSystem) + the ScreenState shell. Pairs with the logged input-injection request.

## 2026-07-12 — Emberwake — feature-request
Expose the structure-gen TraversalProbe (character-box reachability) at whole-GAME/level scale via the API: given the loaded game, is the win/goal state reachable from the start AND from representative mid-game states (softlock / completability check). Underpins the game-production Phase 7 completability validator (an all-green milestone checklist can still be an unwinnable game). Also useful: resource-loop-closure for survival (no needed consumable without a reachable source).

