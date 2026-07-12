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

## 2026-07-12 — Emberwake — feature-request
Generated Perlin worlds (game.json world loaded via --project) render a bare STONE surface - no grass, so no grass-blades and a barren look; flora/trees DO place but the ground is uniformly stone/dirt-brown. Blocks representative survival/RPG starter worlds (the game-production genre starters). Two things to investigate: (1) WorldGenerator Perlin surface-material selection - why the top voxel is Stone not biome Grass; (2) whether game.json world.params (heightScale etc.) actually change the loaded terrain - observed terrain top at y=32 (= base16 + DEFAULT heightScale16) even with params heightScale=4, suggesting params may not take effect on the --project static-generate path.

