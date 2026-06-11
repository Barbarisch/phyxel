---
name: phyxel-world
description: Use when designing or editing a Phyxel game's terrain, world, or levels — choosing a world-gen type and size, filling/clearing regions, placing structures, and setting up single- or multi-scene games. Invoke for "build the world / terrain / level / arena / dungeon" tasks.
---

# Building worlds & scenes

## World generation (the `"world"` block in game.json, or `generate_world`)
Types: **Flat** (indoor/arenas/custom builds), **Perlin** (natural terrain), **Mountains**
(epic landscapes), **Caves** (dungeons), **City** (urban). Size by chunk range (each chunk =
32³ blocks): `from {-1,0,-1}` to `{1,0,1}` = 3×3 chunks (small); `{-2..2}` = 5×5 (medium);
`{-3..3}` = 7×7 (large). `seed` makes it deterministic. Base terrain sits around Y=16.

> An empty void: omit the `"world"` block entirely (a `--project` load regenerates terrain
> from that block on top of `default.db` every launch; `"Empty"` is NOT a valid type and
> aborts the load).

## Structures (the `"structures"` array, or live edits)
- **fill** — solid/`"hollow": true` box of a material:
  `{"type":"fill","from":{...},"to":{...},"material":"Stone"}`.
  NOTE: fills only place into EMPTY air — voxels already occupied (terrain, earlier fills)
  are skipped. Add `"replace": true` to overwrite occupied voxels. The loader logs
  `placed/failed` counts per fill — check them after load.
- **template** — a pre-built object: `{"type":"template","name":"tree.voxel","position":{...}}`.

**Templates FIRST for interiors & props.** Fills are for shells/terrain (walls, floors,
platforms) ONLY. Before hand-building any furniture or prop from fills, `search_templates` —
the catalog has ~50 detailed assets (subcube/microcube detail far beyond fills), e.g. the
tavern set: `tavern_bar`, `tavern_table`, `table_wood`, `chair_wood`, `stool`, `bench_wood`,
`barrel`, `crate_wood`, `fireplace`, `candle_holder`, `lantern`, `torch_wall`. Mix in one
structures array: fills for the building shell + `type:"template"` entries for furnishing.
If the catalog lacks an item, generate it (`generate_template`) or log `/feedback`.

## Live terrain editing (MCP)
`fill_region` (+ `material`) / `clear_region` (max 100k voxels) for boxes; `clear_chunk`
(chunk coords) to wipe a whole 32³ chunk instantly; `place_voxel` / `place_voxels_batch` for
detail; `spawn_template` for objects. Coordinates: X=right, **Y=up**, Z=toward viewer;
right-handed; world coords may be negative. After big edits, `save_world`.

## Materials
The full palette (case-sensitive, from `resources/materials.json` — confirm live with
`list_materials`): Default, Dirt, Grass, Stone, Cobblestone, StoneBricks, Sand, Gravel,
Wood, Log, Bricks, Sandstone, Glass, Metal, Gold, Ice, Leaf, glow, Mirror.
An unknown name renders as a magenta missing-texture checkerboard — validate before load.

## Multi-scene games
Use a `"scenes"` array instead of a top-level `"world"`; each scene has its own `id`,
`worldDatabase`, and `definition` (same schema). `startScene` picks the first; `playerDefaults`
and `globalStory` persist across transitions. Tools: `list_scenes`, `get_active_scene`,
`transition_scene`, `add_scene`/`remove_scene`, `save_scene_manifest`. Pre-bake each scene's
terrain by loading it, building, and `save_world` (→ `worlds/<scene_id>.db`). See
`docs/SceneSystem.md`.

Validate with `validate_game_definition` before `load_game_definition`.
