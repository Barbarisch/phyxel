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
NARROW / needs-confirmation (NOT a claim that Perlin is broken — grass Perlin terrain is known to work generally, e.g. via the generate_world MCP path). Observation: a survival starter world defined by a game.json `world` Perlin spec and loaded via --project came out with a bare STONE surface at high altitude (terrain top ~y32) instead of grass, so no grass-blades — a barren look. Trees/flora DID place. Suspicion: on the --project STATIC-generate path, game.json world.params (heightScale=4) may not be applied — terrain topped at y=32 (= base16 + DEFAULT heightScale16), i.e. high altitude → stone surface; a low-terrain world (or generate_world) would be grass. Questions for engine-dev: (1) does the --project static-load path actually honor world.params? (2) does surface-material pick Grass at low altitude on that path? If this is just a starter-config mistake, disregard.

## 2026-07-12 — Emberwake — feature-request
Stable UUID / persistent-ID system for items, structures, placed objects, and entities. Every item/structure/object/entity should get a persistent unique identifier so a user OR the LLM can query, modify, or remove a SPECIFIC one by ID (e.g. get_object(uuid) / move / rotate / remove_object(uuid)), instead of addressing things by position/type/index which is ambiguous and brittle. Underpins reliable agent-driven world editing and lets the game-production tracker reference specific world content precisely.

