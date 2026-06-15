# World Recipe & Flora Generation Architecture

> Status: **design + step 1 in progress** (2026-06-14). Builds on
> [`TerrainGenerationBiomes.md`](TerrainGenerationBiomes.md). Companion memory:
> `project_biome_flora`.

## Problem

Biome *categories* ("what a Forest is") are reusable, but how biomes are laid out and
tuned **differs per game world**. And tree generation is needed in two very different
contexts. Today everything is driven by a **global** `resources/biomes.json` + pre-baked
`.voxel` templates, so:

- Editing `biomes.json` silently changes *every* world (no per-world reproducibility).
- Trees are always predefined templates — no fresh per-instance variation.
- Streaming/expanding chunks get **no** flora (decoration only runs in `loadWorld` /
  `generate_world`).

## The model

### Two kinds of world, one generator algorithm

| | **Procedural world** (runtime) | **Authored world** (game-dev) |
|---|---|---|
| Grows? | Ever-expanding as the player moves | Fixed, hand-set extent |
| Generator runs | **In-engine (C++)** as chunks stream in | **`gen_tree.py` (Python)** at authoring time |
| Trees | Fresh per-tree from `hash(seed,pos)` — dense, unique | Baked once; pool repetition is fine |
| `world.db` holds | Recipe + edit-deltas; regenerate the rest | Fully baked result (all voxels) |

The branch-driven tree algorithm is **shared** between `gen_tree.py` (authoring) and the
C++ port (runtime) so authored and procedural trees look identical.

### Biome data splits into two layers

- **Biome *category* library** — climate ranges, materials, flora rules. Reusable, shared,
  authored. Stays a definition set (`biomes.json`-style), referenced by name.
- **Per-world biome *layout/tuning*** — which biomes this world uses, their **size**
  (`climateFrequency`), and **extremeness** (terrain drama, tree height/fullness, flora
  density/spacing/mode). This is per-game and **lives in `world.db`** as the *world recipe*.

> Rule of thumb: **store what you can't recompute.** Definitions → files. Procedural base
> (terrain, biome map, base flora) → regenerate from `seed` + recipe. `world.db` → seed +
> recipe + **player edit-deltas** + entities.

## `world.db` — the world recipe

New `world_meta` key/value table (`key TEXT PRIMARY KEY, value TEXT`). The recipe is a JSON
blob under key `recipe`:

```jsonc
{
  "version": 1,
  "seed": 1337,
  "type": "Perlin",
  "climateFrequency": 0.002,        // biome size
  "biomes": [
    { "type": "Forest",
      "extremeness": { "heightScale": 1.0 },
      "flora": { "mode": "pool", "density": 0.7, "spacing": 7,
                 "items": [ {"template": "tree_oak_lush", "weight": 5}, ... ] } },
    ...
  ]
}
```

- The **category library** supplies defaults/material rules; the recipe *selects and
  overrides* per world.
- Authoring (`game.json` world block / tools) writes the recipe into `world.db` at world
  creation. After that **the DB is the runtime source of truth** — reproducible, immune to
  global config edits.
- `flora.mode`: `"pool"` (stamp pre-gen templates — works today) | `"procedural"` (C++
  generator, fresh trees).

## Build order

1. **World-recipe in DB** *(this step)* — `world_meta` table + get/set; a `WorldRecipe`
   struct (JSON to/from); `loadWorld` reads the recipe (synthesizes from `game.json` +
   `biomes.json` and persists if absent) and applies it to the generator.
2. **Decoration in the deterministic chunk path** — so streaming/procedural worlds get
   flora (`pool` mode, reuses today's templates). Closes the streaming-flora gap.
3. **C++ tree generator** — port the branch-driven algorithm; enables `procedural` mode
   and shipped-game runtime generation.
4. **Authoring flow** — `gen_tree.py` bakes static worlds + builds the template pool.

`1 → 2` unlock self-contained worlds + streaming flora with no new generator. `3` is the
big one (true procedural density). `4` is the static-world authoring path.
