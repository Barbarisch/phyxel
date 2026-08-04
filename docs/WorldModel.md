# World Model — one kind of world

> Status: **DESIGN + ground truth.** Direction approved by the user 2026-08-04: *"whether a world
> is very large or not it should behave the same at the engine level."* Nothing in §5 is built;
> §2 is surveyed fact. This doc owns **world semantics** — what a world *is*, how it loads, what
> persists, why "streaming" exists and how it retires. Perf/scale phase history stays in
> [`LargeWorldScalePlan.md`](LargeWorldScalePlan.md); terrain *content* stays in
> [`TerrainGenerationV2.md`](TerrainGenerationV2.md); water stays in [`Water.md`](Water.md).
> Supersedes [`WorldRecipeAndFlora.md`] — ledger in §9.

---

## 1. The problem

The engine has two axes that pretend to be one:

- **Generation type** (`Perlin`/`Flat`/`Mountains`/`Caves`/`City`/`Random`/`Custom`) — which
  terrain function derives voxels. Fine.
- **`world.streaming: true|false`** — which *sounds* like "when do chunks generate," but
  actually gates **five separate concerns**:

| Concern | streaming: false | streaming: true |
|---|---|---|
| Extent | fixed `from`/`to` chunk range | unbounded (32 km bake box) |
| When generated | all at load | on demand around the camera (worker copies) |
| Generator features | none of terrain-v2's world model | hydrology bake, rivers, water table, procedural flora (`getStreamingGenerator()`) |
| Water placement | implicit flat sea → grounded grid | the baked table |
| DB semantics | DB **is** the world | DB is an **overlay of edits**; missing chunks regenerate from seed |

This grew historically: Terrain Generation v2 (coarse model, hydrology) was built on the
streaming generator because that is where a 32 km world needed it, and the static
`generate_world` path was never retrofitted. "Streaming" drifted from a *loading strategy*
into *"the world that has the modern features."*

**Measured/observed cost of the split** — each of these is a defect class it caused:
- The CLAUDE.md gotcha: generating a low chunk range in a tall world yields a "flat stone
  plane" (deep underground), because bounded generation doesn't know where the surface is.
- Water accumulated **three placement paths** (implicit sea / coarse bake / grounded grid,
  `Water.md` §2) — the phantom-sea-through-rock defect lived in the seam between them;
  `water_ground_sync` must *refuse* baked worlds; rivers cannot exist in non-streaming labs.
- Subsystems grow `if (streaming)` forks (water, flora mode, far terrain, boot physics), each
  a place for the two world kinds to disagree.

---

## 2. Ground truth (surveyed 2026-08-04)

- **Chunks are already mode-agnostic.** One chunk format, one blob codec (v2 with water
  spans), one SQLite schema (`chunk_blobs`), one physics/occupancy registration. Nothing at
  the chunk level knows which kind of world it belongs to.
- **Two load paths exist:** core `WorldInitializer` (`--project` boot: bulk
  `loadChunksNearAndDeferRest` + rebuild-all) and `ChunkStreamingManager` (per-frame stream
  in/out). The editor's `applyProjectSelection` is a third, near-duplicate of the first —
  the from-spans water bind had to be added to two places because of this (2026-08-04).
- **The world recipe already exists** (`WorldRecipe`, `world_meta` key `recipe`): seed-adjacent
  tuning (climate frequency, biome layout, flora, `seaLevelY`, height spline) persisted in the
  DB, stored-value-wins. ⚑Known gap carried from the retired recipe doc: **the recipe does not
  yet own the generation seed** — `applyRecipe` applies tuning but the seed still comes from
  `game.json`, so DB-as-source-of-truth is incomplete.
- **Dirty-chunk-wins already exists** in streaming persistence (an edited chunk is never
  regenerated); non-streaming worlds get the same effect trivially because nothing regenerates.
- **The hydrology bake is memoized and bounded** (256² × 128 m) and its cost on a small world
  is small; nothing *architecturally* ties it to streaming — only the fact that it hangs off
  `getStreamingGenerator()`.

---

## 3. Target model

> **Every world is: a RECIPE (seed + generation type + tuning + optional BOUNDS) plus a DB
> OVERLAY of edits.** Loading is always streaming. Size is a number, not a mode.

Consequences, stated as invariants:

1. **One load path.** Boot = point the streaming pump at the camera. A 16-chunk world streams
   completely in the first pump; a 32 km world streams forever. No bulk-load path to keep in
   sync (and no third editor copy of it).
2. **Bounds are data.** A bounded world is the same pipeline with a clamp: chunks outside the
   bounds are void (never generated, never loaded). `from`/`to` moves into the recipe.
3. **World-model facts are recipe facts.** Hydrology (and anything else that today lives only
   on the streaming generator) is computed from the recipe for every world that has terrain to
   compute it on. A flat 4-chunk world gets a trivial bake — and with it, ONE water placement
   rule everywhere (`Water.md` §2 interim table collapses).
4. **Edits always win.** The DB overlay beats regeneration in every world. An authored world
   is simply a world whose recipe is `Flat`/empty and whose content is almost all overlay.
5. **`generate_world` becomes "pre-generate region"** — an editor command that forces a region
   of the same pipeline to materialize (and persist), not a different kind of world-building.
6. **The `streaming` flag retires** — accepted with a deprecation warning, derived meaning:
   `streaming: false` ≙ "bounds = the from/to range". No behavior forks on it.

What does **not** change: chunk format, blob codec, DB schema, scene system, packaging,
per-world *parameters* that are legitimately different (view distance, flora density, bake
resolution — parameters, not modes).

---

## 4. Migration order (each increment shippable, red-before-green)

1. **Recipe owns the seed** (small; closes the carried gap). Acceptance: two boots of the same
   DB with a *changed* `game.json` seed produce identical chunks; red = today they don't.
2. **Bake from the recipe for bounded worlds.** `WorldGenerator` computes hydrology whenever
   the recipe's type is height-based, streaming or not (bounded worlds clamp the bake box to
   their bounds). Acceptance: a bounded Perlin world answers `water_table_level`; the
   `water_ground_sync` "refuses baked worlds" guard becomes unreachable and is deleted.
3. **One load path.** `WorldInitializer` and `applyProjectSelection` both delegate to the
   streaming pump with an initial-anchor burst. Acceptance: the `--project` boot log shows the
   same path as camera-motion loading; the duplicated post-load block (faces/physics/
   occupancy/water-bind) exists exactly once.
4. **Bounds as recipe data + `generate_world` as pre-generate.** Acceptance: a `from`/`to`
   world built by the old flow round-trips (loader synthesizes bounds into the recipe).
5. **Retire the flag.** `world.streaming` parses with a WARN and maps onto bounds; grep for
   `if (streaming)`-class forks is empty in world-lifecycle code (water's forks die in step 2;
   flora's `procedural is streaming-only` limitation retires with the shared path).

Sequencing vs water: water's remaining render-from-spans work (streaming near-field,
`Water.md` §6 step 2b) does not block on this and lands first; step 2 here then *deletes* the
editor/baked split in water placement rather than patching it again.

---

## 5. Risks / open questions

- **Regeneration parity:** a chunk that regenerates from seed must byte-match what the bulk
  path produced, or old worlds shimmer on migration. Guard: generate-then-compare harness over
  a saved world before switching the boot path (the storage-v2 migration precedent).
- **Boot latency shape changes:** bulk-load loads near chunks synchronously; a pure pump boot
  must still guarantee "ground under the spawn camera before first frame" (initial-anchor
  burst = the existing `loadChunksNearAndDeferRest` behavior, kept as the pump's first tick).
- **Bake cost on world open** for bounded worlds: memoized and clamped to bounds; measure on
  the smallest and largest bounded worlds before enabling by default.
- **Flora decoration split** (pool vs procedural, fixed-region `decorateFlora` vs streaming
  `decorateChunk`) — carried from the retired doc; unify onto the streaming decorator when the
  load paths merge (step 3), not before.
- Whether `City`/`Random`/`Custom` (non-height-based) worlds get a null world-model or a
  degenerate one (probably null: no terrain to hold water).

---

## 6. Carried TODOs from the retired recipe doc (unchanged priorities)

- One-command "bake authored world" workflow (generate → decorate → `save_world`).
- Expand the pool-template variety; author-facing recipe tuning docs/example.
- **Palm** procedural archetype (currently aliases to acacia in `ProceduralTree`).
- Procedural flora in fixed-region worlds (retires with migration step 3/5).
- `CharacterTestbed` DB bloat: wipe `worlds/default.db` for clean tests.

---

## 9. Superseded documents (deleted 2026-08-04; recover via `git show <hash>:docs/<name>.md`)

| Doc | Last hash | What it was | What survived where |
|---|---|---|---|
| `WorldRecipeAndFlora.md` | `f4f2f717` | The recipe model ("store what you can't recompute"), the two-kinds-of-world table this doc dissolves, flora build order (steps 1–3 shipped), tree-forge supersession notes | §2 recipe ground truth + seed gap; §5 flora risk; §6 TODOs; tree specifics live in `TreeForgeDesign.md` / `TerrainGenerationBiomes.md` |
