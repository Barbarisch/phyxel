# Terrain Generation & Biomes — Design

> Status: **largely implemented on main.** Chunk streaming, column-first generation with
> depth profiles, data-driven biomes (`resources/biomes.json`: temperature / moisture /
> continentalness climate with Gaussian-blended per-biome height), and the flora
> decoration pass (branch-driven trees via `Core::ProceduralTree` for `"procedural"`-mode flora;
> the `"pool"`-mode template AUTHORING tool is now `tools/tree_forge.py` — `gen_tree.py` was marked
> DEPRECATED 2026-07-05 and superseded, `resources/biomes.json` pool items are all `forge_*`
> templates now) all landed. Per-world generation tuning is persisted via
> `Core::WorldRecipe` (`world.db` `world_meta`). See **`docs/WorldRecipeAndFlora.md`** for the
> flora + world-recipe design and remaining work. **Update (verified against source):** the
> streaming worker thread (Phase 1c) has SHIPPED (`ChunkStreamingManager`'s async generation
> worker pool, `kGenWorkerCount`), and water integration is now a large active workstream
> (`docs/Water.md`, `docs/TerrainGenerationV2.md` §P2) — neither is still deferred.
> Genuinely still **TODO**: caves/ore carving (`docs/TerrainGenerationV2.md` §P3, not started).
> The sections below remain the authoritative design rationale for the v1 pipeline.

## Goals (agreed scope)

- **Streaming, not whole-world.** No world — authored *or* procedural — is ever fully
  resident in memory. Chunks stream in around an anchor (player, or `(0,0,0)` fallback)
  within a configurable radius/budget and evict beyond it. This is the universal model;
  authored levels are just streaming with generation disabled.
- **Opt-in procedural generation.** A `"terrain"` block in `game.json` configures
  streaming + (optionally) generation. Games that ship a hand-built world omit the
  `generation` sub-block and get authored-only behavior (empty beyond the DB).
- **Infinite-Y by default.** Unlike Minecraft's tall-but-bounded columns, Phyxel chunks
  are 32³ and stack in all axes. Generation extends downward indefinitely as the player
  digs/descends. Default depth is **unbounded**; a game may set a bedrock floor.
- **Full biomes from the start** — temperature + moisture + continentalness climate
  fields, data-driven biome table, and weighted height-blending across biome borders.
- **Decoration connects to flora.** A post-fill decoration pass stamps the existing
  `gen_tree.py` `.voxel` templates per biome, finally wiring vegetation into world-gen.

## Ground truth (what exists today)

The engine is closer to streaming-procedural than it looks — most of the machinery is
present but a key wire was never connected.

- **`WorldGenerator` is already 3D and deterministic.** `generateChunk(chunk, chunkCoord)`
  iterates all 32³ local positions including Y; every generator is a pure function of
  world position + seed (`worldPos.y <= height`). A chunk at `chunkCoord.y = -3` already
  generates as solid stone. Infinite-Y is latent, not missing.
- **`ChunkStreamingManager` already streams a 3D cube radius** around the player
  (`dx/dy/dz` loops in `loadChunksAroundPosition`), unloads distant chunks, and persists
  dirty ones to SQLite.
- **THE GAP:** `ChunkStreamingManager::generateOrLoadChunk()` calls
  `chunk->populateWithCubes()` — the *old random fill* — **not** `WorldGenerator`. So
  streamed chunks are noise garbage, and `WorldGenerator` is only used in an up-front
  whole-world pass. In project mode the whole DB loads at once, so streaming generation
  is effectively never exercised.

Other facts that shape the design:

- **Perf debt.** `generatePerlin` recomputes 3D noise *per voxel* (32k× per chunk ×
  octaves); random fill spins up an `mt19937` *per voxel*. A real pipeline computes a 2D
  heightmap + biome map **once per column** then fills the Y span. Generation also runs
  on the main thread today — streaming gen must move to a worker.
- **No biome concept.** `getMaterialForPosition` is a flat depth-based switch keyed on
  `generationType`.
- **Flora is offline only.** `tools/gen_tree.py` / `tools/gen_nature_textures.py` produce
  `.voxel` templates; nothing places them during world-gen.

## Design principles

1. **Generation is a pure function of `(worldPos, seed)`.** Already true; keep it sacred.
   Guarantees seam-free chunk borders, reproducibility, and identical results whether a
   chunk is reached by walking or by digging.
2. **Column-first, then voxel-fill.** Compute per-`(x,z)` data once — heightmap, biome,
   climate — then fill the Y column. Kills the per-voxel noise cost.
3. **Opt-in via config, not a code fork.** The `"terrain"` block selects streaming budget
   and whether generation runs. Authored games never touch generation.
4. **Layered pipeline, each layer skippable:** `climate → height → carve (caves) →
   material → decorate (flora/ores/structures)`. Each stage reads the previous; any can
   be disabled.

## Configuration (`game.json` → `terrain`)

```jsonc
"terrain": {
  "streaming": true,            // always stream; never load whole DB
  "loadRadius":   { "horizontal": 4, "vertical": 3 },   // chunks
  "unloadRadius": { "horizontal": 6, "vertical": 5 },
  "maxLoadedChunks": 512,       // hard memory ceiling; evict LRU beyond
  "anchor": "player",           // or "origin"
  "generation": {               // OMIT this block → authored-only (empty beyond DB)
    "mode": "procedural",
    "seed": 12345,
    "depth": "unbounded",       // default; or { "bedrockY": -2048 }
    "biomes": "resources/biomes.json"
  }
}
```

The single flag that separates an authored world from a procedural one: **when a needed
chunk isn't in the DB, do we generate it or leave it empty?** (presence of `generation`).

## Universal streaming & the physics lifecycle (the load-bearing risk)

Making *every* world stream — instead of the current "project mode loads the whole DB
up front" — surfaces the engine's known fall-through hazard.

> **Hard rule (from `reference_collision_occupancy`):** every DB-load path must register
> static-collision occupancy grids in `VoxelDynamicsWorld`. Today that's a one-shot
> whole-world `buildAllChunkPhysics()`.

Under universal streaming this becomes a **per-chunk lifecycle**: build the occupancy
grid when a chunk streams **in**, tear it down when it **evicts** — every frame, on the
worker. If the register/unregister pairing is wrong, you get intermittent character
fall-through that is miserable to debug. This is a **first-class Phase 1 concern**, not
an afterthought. It is the single highest-risk part of the whole effort — higher than
generation itself.

## Vertical / "dig downward" model

Generation does **not** happen on dig. The chunk below the player is already streamed in
(vertical radius), so digging merely removes already-present voxels. What matters is the
**depth profile** of a column:

- **Surface band** (biome-defined, ~4 voxels): grass / sand / etc.
- **Subsurface band** (~biome dirt/stone).
- **Deep stone band:** extends downward — stone + ore + caves.
- **Floor:** unbounded by default; optional indestructible bedrock at a game-set
  `bedrockY`.

The only streaming change is a configurable (and asymmetric) vertical load radius — you
rarely need chunks far *above* the surface, but want a few *below* as the player descends.

## Biome model (full from the start)

Climate-field approach with three low-frequency 2D noise fields:

- **temperature**, **moisture**, **continentalness** (land/ocean + elevation base).
- A **data-driven biome table** (`resources/biomes.json`, in the spirit of
  `materials.json`) maps climate cells → biome. Each biome defines:
  - surface material, subsurface material,
  - height amplitude/offset modifiers,
  - flora set + densities,
  - optional structure set,
  - optional cave-frequency multiplier.
- **Border blending (the genuinely hard part):** sample climate per column, pick the
  biome, but **blend height params across the N nearest biomes** (weighted by climate
  distance) so plains→mountain transitions don't produce cliff seams. Internal milestone:
  ship hard borders working first, then add blending — but the *delivered* feature is the
  blended version.

This replaces the `generationType`-driven `getMaterialForPosition` with biome-driven
material and height selection.

## Caves & underground

The existing 3D-noise carve (`generateCaves`) is a fine v1 but produces "swiss cheese."
Progression:

- **v1 (cheap):** keep 3D noise carve, but gate it to the deep band and run it as a
  **post-pass** over the filled column (carve = set-to-air) rather than baked into the
  fill test — cleaner stage separation.
- **v2 (worm caves):** ridged / Worley noise tunnels — sample `abs(noise)` near zero for
  connected, walkable tunnels instead of blobs.
- **v3 (features):** ore veins (cluster noise per material), large caverns, an
  underground water table at a fixed Y. All decoration-pass work.

## Decoration pass — flora & structures

After a chunk's voxels are filled, a **decoration pass** runs per column:

- Find surface Y, query biome, roll against biome flora densities (deterministic hash of
  `(x, z, seed)`), and stamp a `.voxel` template (`gen_tree.py` output) at the surface.
- This is where biomes finally connect to vegetation: biome → flora set → template stamp.
- **Cross-chunk problem:** a tree rooted near a chunk edge overflows into the neighbor.
  Preferred fix: **defer decoration** to a separate pass that runs only once a chunk *and
  its neighbors* are all present — flag chunks "filled but not decorated" until the
  neighborhood is ready. (Alternative: generate into a padded region and write into
  neighbors, which requires them loaded anyway.)

## Phased roadmap

| Phase | Scope | Notes |
|---|---|---|
| **1. Universal streaming + generation wire** | DB-load streams by default with `loadRadius`/`maxLoadedChunks` budget; wire `generateOrLoadChunk` → `WorldGenerator`; **per-chunk physics build/teardown on stream-in/out**; move gen + physics-build to a worker thread. | The unlock. Highest risk is the physics lifecycle, not the generation. |
| **2. Column-first refactor + depth profile** | Per-column heightmap/climate computed once; surface/subsurface/deep bands; unbounded-down + optional bedrock; asymmetric vertical radius. | Perf foundation. |
| **3. Full biome system** | 3 climate fields incl. continentalness; data-driven `biomes.json`; weighted border blending; biome-driven materials + height + flora sets. | Headline feature. |
| **4. Caves/underground** | Worm caves (ridged/Worley) as a carve post-pass; ore veins; water table. | Parallelizable with 3. |
| **5. Decoration pass** | Neighbor-aware flora stamping from `.voxel` templates; connects biomes to `gen_tree.py` output. | Depends on 3. |

**Recommendation:** Phase 1 is the critical path *and* the riskiest, because universal
streaming touches the physics-registration lifecycle with a history of fall-through bugs.
Treat Phase 1 as its own hardened milestone — build it, then stress-test with the
test-world snapshot discipline (walk/dig across many chunk boundaries, confirm no
fall-through, confirm the memory ceiling holds) **before** layering biomes on top. Phases
3/4/5 are opt-in features to schedule individually; 4 can run parallel to 3.

## Key files

- `engine/include/core/WorldGenerator.h` / `engine/src/core/WorldGenerator.cpp` — the
  generator; already 3D/deterministic, needs column-first refactor + biome hooks.
- `engine/include/core/ChunkStreamingManager.h` / `…/ChunkStreamingManager.cpp` — the
  streaming loop; `generateOrLoadChunk` is where the generation wire goes; streaming
  budget + per-chunk physics lifecycle land here.
- `engine/src/core/ChunkManager.cpp` — owns the `WorldGenerator` and calls
  `updateStreaming`; the DB-load-everything path that must become streaming lives near
  here.
- `tools/gen_tree.py`, `tools/gen_nature_textures.py` — offline flora/texture generators
  the decoration pass will consume.
- `resources/materials.json` — model for the new data-driven `resources/biomes.json`.

## Related

- `docs/Water.md` — water table / sea level interplay (the consolidated water doc).
- Memory: `reference_collision_occupancy` (the fall-through rule), `project_biome_flora`
  (the flora generators), `reference_empty_world` (current project-load regen behavior).
