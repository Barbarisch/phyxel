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
Snowy grass block material + texture for snowy/tundra biomes. Survival games set in snow (e.g. the Emberwake dogfood) need snow-topped ground, but today there is no SnowGrass material and no snow biome in biomes.json (Perlin worlds render bare stone/grass). Add a snow-grass material (snow top + snow-dusted sides, matching the coursed-vs-varied rules) and a snowy/tundra biome so snow worlds generate correctly.

## 2026-07-12 — Emberwake — feature-request
Axe tool item + chopping/gathering swing animation. Survival/RPG gathering needs a real axe item (equippable via RpgItem/EquipmentSystem) plus a chop/swing animation family (like melee_anim_families) so chopping a tree for wood is an actual swing with wind-up + impact, not an instant voxel break. Drives the survival gather verb (Emberwake) and general woodcutting.

## 2026-07-12 — Emberwake — feature-request
Satisfying tree/object destruction - a chopped tree should topple and fall, not vanish. When a tree is chopped through, it should physically fall over (coherent-fragment destruction via the GPU AVBD / VoxelDynamicsWorld bonds-to-weld path) and settle as gatherable logs/debris. Applies to destructible objects broadly; survival wood-gathering is the driving case, so felling reads as a real event.

## 2026-07-12 — Emberwake — feature-request
MCP input-injection tool for controllability verification (surfaced by the game-production dogfood). The tracker's L4 'player is controllable' milestone cannot be auto-verified because no MCP tool can simulate WASD/keypress input to the running engine - only entity placement is checkable. Add a tool to inject synthetic input (move/jump/attack key events) so agent verification can confirm player control + movement without a human at the keyboard.

## 2026-07-12 — Emberwake — RETRACTED (was filed as a terrain-gen bug — it is NOT a bug)
RETRACTED: operator error, terrain-gen v2 works correctly. I had claimed Perlin made a "flat stone plane at y31"; the truth is I generated only LOW chunk layers (chunk y=0..1, world y 0-63) via generate_world / a game.json `world` from/to range, but the v2 surface sits HIGH (kSeaLevelY=16, peaks ~384 above), so my range was entirely DEEP UNDERGROUND → solid Stone (materialForColumn: depth>=4 = deepMaterial). Verified correct once I generated a tall enough range: varied surface_y (74 / 75 / 96 across a small area) with proper Grass/Dirt-over-Stone layering. OPTIONAL usability nit for engine-dev (low priority, NOT a bug): static generation (generate_world / a shallow game.json from/to y-range) silently yields underground stone with no hint the surface is far above — an auto-surface-follow or a warning would prevent this trap. Documenting the gotcha in CLAUDE.md.

## 2026-07-12 — Emberwake — feature-request
Stable UUID / persistent-ID system for items, structures, placed objects, and entities. Every item/structure/object/entity should get a persistent unique identifier so a user OR the LLM can query, modify, or remove a SPECIFIC one by ID (e.g. get_object(uuid) / move / rotate / remove_object(uuid)), instead of addressing things by position/type/index which is ambiguous and brittle. Underpins reliable agent-driven world editing and lets the game-production tracker reference specific world content precisely.

