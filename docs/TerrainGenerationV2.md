# Terrain Generation v2 — Grand Worlds Design

> Status: **DESIGN / not yet started** (2026-07-09). This is the canonical plan for the
> next-generation terrain system: art-directable, hydrologically-correct, drawn-map-capable
> worlds at both bounded (Middle-earth map) and infinite-streaming scale.
>
> It **supersedes the roadmap** in [`docs/TerrainGenerationBiomes.md`](TerrainGenerationBiomes.md)
> (the v1 column-first pipeline) and folds in [`docs/WorldRecipeAndFlora.md`](WorldRecipeAndFlora.md)
> and [`docs/WaterSystem.md`](WaterSystem.md). Those remain accurate for **what shipped**; this
> doc is **where we're going**. The v1 pipeline is not thrown away — it becomes **Layer 1**
> (per-chunk detail) beneath a new **Layer 0** (coarse global model).

---

## 0. The goal, in the user's words

> "grand sweeping worlds based off drawn maps (like the map of Middle-earth) … underground caves,
> very tall mountains, rivers, lakes, ponds, and just about anything needed to create a large open
> world … Minecraft-like techniques, but able to fine-tune and control that far more."

Four capabilities the current engine cannot deliver, and why:

| Want | Current reality | Root cause |
|------|-----------------|------------|
| Very tall mountains | Height = `Y=16 + ±9 continental + biome heightScale` | No large-scale relief mechanism; additive fBm can't sharpen ridgelines |
| Rivers / lakes / ponds / oceans | **Zero procedural water** | Water needs *global* downhill knowledge; per-chunk gen can't see it |
| Underground caves | One blobby 3D-noise threshold, only in `Caves` world type | No tunnel/cheese/aquifer model; not wired into normal worlds |
| Drawn-map control | No import path at all | Height is a hard-coded noise formula, not a controllable field |
| Fleshed-out biomes | Differ only by material + heightScale + flora | `continentalness` unused in selection; no ocean/beach/alpine; no per-biome carving |

## Scope decisions (user, 2026-07-09)

- **World scale: BOTH** bounded (finite, fully-baked map) and infinite-streaming are equal
  priority. The coarse-model abstraction must serve both from day one — a drawn map is a fully
  baked coarse model; an infinite world is deterministic coarse partitions.
- **Water: HYBRID** — static water voxels at baked flat levels everywhere (cheap, streamable,
  always-correct), with the existing `WaterManager` CA sim activated only in a region around the
  player for interactive flow/splashing. Generation *feeds* the sim (springs/channels/levels)
  instead of hand-authoring.
- **Sequencing: FOUNDATION FIRST** — build the coarse world model + density-function evaluator
  before flashy features, so every later phase slots on cleanly (accepting a slower first "wow").
- **Deliverable: this design doc**, then implement phase by phase under the standing discipline
  (grounding-auditor on every number, red-before-green + solution-auditor on every "works" claim,
  the stress-test phase, and a per-placer validation ledger).

---

## 1. The core architecture — two-tier (Layer 0 / Layer 1)

Every serious reference (Minecraft 1.18 density functions, Red Blob mapgen4, Génevaux hydrology,
Barnes priority-flood, alcatrazEscapee's river analysis, Vintage Story, Dwarf Fortress) converges
on one split, and it is exactly the seam Phyxel is missing:

```
┌─────────────────────────────────────────────────────────────────────┐
│ LAYER 0 — CoarseWorldModel  (computed ONCE, persisted in world.db)    │
│   low-res grid, ~1 sample per chunk (or per 4×4 chunks)               │
│   holds everything that needs WHOLE-WORLD knowledge:                  │
│     • heightfield        • climate fields (temp/moist/continental)     │
│     • biome IDs          • river graph (polylines + Strahler order)   │
│     • lake levels + rims • basin/watershed labels • sea level          │
│   sourced from EITHER procedural noise+splines OR a drawn map          │
└─────────────────────────────────────────────────────────────────────┘
                              │ sampled + upsampled by
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│ LAYER 1 — Per-chunk detail   f(x,y,z,seed), STATELESS, ANY ORDER      │
│   (this is today's column-first WorldGenerator, refactored)           │
│     • bilinear/mesh upsample of Layer-0 height                        │
│     • domain-warped ridged-multifractal mountain detail               │
│     • carve rivers/lakes from the graph (distance-to-segment)         │
│     • carve caves (isosurface cheese + spaghetti + aquifer)           │
│     • material by slope/altitude/flow                                 │
└─────────────────────────────────────────────────────────────────────┘
                              │ then
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│ LAYER 2 — Decoration + structures  (existing flora + structure gen)   │
│   position-hash scatter; Minecraft-style 8-block neighbor-offset      │
└─────────────────────────────────────────────────────────────────────┘
```

### Why the split is mandatory, not stylistic

Rivers, lakes, erosion, coastlines, and drainage require answering *global* questions — "what is
the lowest saddle on this basin's rim?", "does flow from this cell reach the sea?" — that are
**impossible per-chunk**. But Phyxel's chunks must stream independently and deterministically
(`f(x,z,seed)`, order-independent). The **only** reconciliation is: bake global features into a
coarse map up front; let each chunk read from it locally. This is a hard architectural law, not a
preference. (See alcatrazEscapee, "Why Are Rivers So Complicated?": Context-Free vs Bounded vs
Unbounded context; rivers are Unbounded → require world partitioning.)

### Why this also gives us the drawn map for free

A hand-drawn Middle-earth map **is a hand-authored Layer 0.** Noise-worlds and map-worlds diverge
only in *how the coarse model is filled* (noise+splines vs rasterize-image); **everything
downstream is identical** — same rivers, same lakes-to-sea, same biomes, same streaming, same far
LOD. That is the payoff of building the coarse model as the universal substrate.

### How BOTH scales fall out of one abstraction

`CoarseWorldModel` is an interface with two backings:

- **Bounded backing** — a finite grid, fully computed once (from a drawn map or a one-shot noise
  bake), held resident or memory-mapped from `world.db`. Global passes (priority-flood, erosion)
  run over the *entire* grid — simple and correct. This is the Middle-earth case.
- **Infinite backing** — deterministic **partitions** (e.g. 512×512-cell regions keyed by seed).
  Each region computes its own coarse model on demand; global passes run *within* a region and
  **reconcile across region borders** (tiled priority-flood, Barnes 2016). Larger partitions →
  grander river networks but weaker height integration; smaller → accurate but bounded scale. This
  is the endless-world case.

Layer 1 doesn't know or care which backing it samples. That indifference is the whole design.

---

## 2. The two load-bearing primitives

Two pieces of machinery unlock most of the design space. Build these well; everything else composes.

### 2a. Density-function / nested-spline evaluator (art direction)

Minecraft 1.18's mechanism, and the answer to "fine-tune and control far more." A small composable
graph of `pos → scalar` nodes: `noise`, `spline`, `abs/square/invert`, `add/mul/min/max`,
`y_gradient`, `shift` (domain warp), `cache`, `blend`. Terrain is solid where `final_density > 0`.

The load-bearing trick is **nested multi-dimensional splines**: the continentalness→height spline's
control points are *themselves* splines over erosion, whose points are splines over peaks-valleys —
so "how much erosion flattens you depends on how continental you are." This decouples **how
mountainous** (shape noise) from **how tall** (authored curve), which is precisely the control the
user wants. It is also the natural target for drawn-map channels (a painted value drives a spline).

**Phyxel form:** a data-driven node graph in the world recipe (JSON), evaluated per-column in
Layer 1, sourcing its low-frequency inputs from Layer 0. Not a from-scratch scripting language — a
fixed primitive set, serializable, persisted per world. (Reference: Minecraft density-function data
packs; Red Blob "Terrain from Noise" redistribution/lerp/terrace formulas.)

### 2b. Priority-Flood (hydrology — Barnes, Lehman & Mulla 2014)

~20 lines; **O(n) for integer heights** (voxels are integer — this is optimal for us). A single
min-priority-queue flood inward from the coarse grid's edges simultaneously yields:

- **Depression-filled heightmap** — every cell gets a monotonic downhill path (no fake pits).
- **D8 flow directions** — the `Priority-Flood+FlowDirs` variant assigns them during the fill.
- **Basin / watershed labels** — propagate a basin-ID during the flood.
- **Flat lake levels** — each depression fills to its **spill point** (lowest rim saddle);
  `filledZ` **is** the flat water surface, and the rim **is** the containment (no leaks — the #1
  failure of naive noise-lakes). Threshold by fill volume to discard micro-puddles.

Then **D8 flow-accumulation + Strahler stream order** (post-order over the drainage tree) →
**rivers = accumulation > threshold**, with order driving width/depth. The **tiled** variant
(Barnes 2016) is purpose-built for chunked/partitioned worlds → serves the infinite backing.

This one primitive is our river-router, lake-detector, ocean-fill, *and* depression-filler.

---

## 3. Phase plan

Sequenced foundation-first; each phase is independently testable (red-before-green) and names its
required validation depth (L1 exists · L2 structural invariant on real output · L3 functional
agent-usability · L4 live runtime). Numeric parameters are flagged **⚑GROUND** where the
grounding-auditor must supply a citable source (river widths by order, mountain heights vs real
ranges, sea level, cave dimensions) before shipping.

### P0 — CoarseWorldModel scaffold + immediate mountain drama
- **Foundation:** introduce `CoarseWorldModel` (interface + bounded backing first), persisted in
  `world.db`. Seed it trivially from the *current* noise so nothing regresses. Wire Layer-1
  `WorldGenerator` to sample it. Also wire it as the source for the existing far-terrain LOD
  (`sampleSurface`), which already wants exactly this heightmap.
- **Local drama (cheap, high-impact):** add **ridged multifractal** (`1−abs(noise)`, octave-weighted
  by the previous octave → rough peaks, smooth lowlands) + **domain warping** (`fbm(p+fbm(p))`) +
  an **amplification spline** on final height. Remove the ±9 continental cap and deepen the usable
  vertical range (Y is already unbounded in the engine; only the constants are shallow). ⚑GROUND
  peak heights & sea-level baseline against a real reference range.
- **Validation:** L2 (height-profile / ridge-continuity assertions on real output) + L4 (runtime
  screenshots of a dramatic skyline). **Stress:** generate a full region, assert no seams at
  chunk borders and monotonic ridge behavior.

### P1 — Density-function evaluator + biome overhaul
- Replace the ad-hoc height formula with the **density/spline node graph** (2a). Model
  continentalness → erosion → peaks-valleys as nested splines.
- **Biomes:** make `continentalness` actually drive selection (currently unused); add **ocean,
  beach, alpine/snow-cap, wetland** biomes; add per-biome **carving hooks** (cave frequency, valley
  bias) and slope/altitude material rules. ⚑GROUND biome climate bands.
- **Validation:** L2 (biome-map continuity, no cliff seams at borders) + L4. Keep the recipe as the
  DB source of truth.

### P2 — Hydrology bake (the headline: rivers, lakes, oceans)
- On the coarse grid: **Priority-Flood → D8 accumulation → Strahler order** (2b). Persist river
  graph, basin labels, lake levels, sea level to `world.db`. Tiled variant for the infinite backing.
- **Layer-1 carve:** rivers by distance-to-nearest-segment × order → channel width/depth ⚑GROUND
  (real river-width-by-Strahler-order); lakes/ocean filled to the **baked flat level** with the
  flood-derived rim (no leaks); ponds = small terminal basins.
- **Water existence (HYBRID):** emit **static water voxels** to the baked level everywhere;
  generation **feeds the `WaterManager` CA** (springs at river heads, channels along riverbeds,
  sea level) so the sim can activate in a **player-region** for interactive flow — *generalize the
  CA off its fixed 64×32×64 box* to a sparse player-follow region. (See `docs/WaterSystem.md` for
  the existing CA; this connects generation to it for the first time.)
- **Validation:** **L3** — this is usability-critical and silent-failure-prone. Assert every river
  is **continuously downhill to a lake or sea** (walk the graph), every lake surface is **flat and
  contained** (single spill scalar), no water on a slope, no chunk-border level mismatch. **Stress:**
  a river crossing many chunks and a lake spanning a region border must derive identical levels.

### P3 — Cave overhaul
- Minecraft-1.18 model, all per-chunk/**[LOCAL]**: low-freq **isosurface "cheese" caverns** +
  abs/ridged **spaghetti/noodle tunnels** + **aquifer noise** (local water tables — pockets flood
  to different Y; water above, lava deep) ⚑GROUND cavern/tunnel dimensions. Decouple from the
  `Caves` world type so **every** world has an underground.
- **Validation:** L2 (connectivity — flood-fill the carved air, assert reachable tunnel networks,
  cull orphan pockets) + L3 (a character-box can traverse a tunnel — `TraversalProbe`) + L4.
  Note the existing **occlusion-culling** lever helps most underground (see AgentContext).

### P4 — Drawn-map importer
- Image → coarse control fields: **grayscale → heightfield** (Azgaar-style, `<20%`=water),
  **color mask → biome IDs**, optional **polyline layer → river paths** the hydrology bake biases
  toward. Feed straight into the bounded `CoarseWorldModel`. Everything P1–P3 then applies to the
  hand-drawn map unchanged.
- Optional follow-on: an **offline erosion / uplift bake** (Sebastian Lague droplet erosion, or
  Cordonnier uplift + stream-power) on the coarse grid for weathered valleys — runs once, bakes
  into Layer 0, stays fully streamable.
- **Validation:** L4 — import the actual Middle-earth map, walk from a drawn coastline up a drawn
  mountain range along a drawn river; assert the world matches the map's structure.

### P5 — Scale hardening (infinite backing + LOD + far distance)
- Finish the **infinite partition backing** with border reconciliation (tiled priority-flood).
- Feed the coarse model into far-terrain LOD (far water + far flora, currently missing);
  retry the reverted voxel-LOD with a watertight coarse mesher.
- **Camera-relative rendering** for the documented >100km float wobble — the standing large-world limit.

---

## 4. What we reuse vs. build new

**Reuse (already in your favor):** column-first pipeline, custom fractal Perlin + domain warp,
climate model, async streaming worker + private generator snapshot, `WorldRecipe`/`world_meta`
persistence, `WorldGenerator::sampleSurface` (far LOD), the `WaterManager`/`WaterSimulation` CA
(ocean seam + springs + channels — generalize its region), Poisson flora, structure gen (Layer 2).

**Build new:** `CoarseWorldModel` (bounded + infinite backings), density-function/spline evaluator,
priority-flood hydrology bake, river/lake carve in Layer 1, cave-model overhaul, drawn-map importer,
generation→WaterManager wiring, optional offline erosion bake.

## 5. Risks & open questions (to resolve during P0/P1)

- **Physics lifecycle under streaming** — the standing "every DB-load path must call
  `buildAllChunkPhysics()`" rule (AgentContext). Water voxels and carved caves both churn occupancy
  grids; verify the per-chunk build/teardown holds.
- **Coarse-grid resolution** — 1 sample/chunk vs 1/4×4. Trades hydrology fidelity vs memory/bake
  cost. Decide with a measurement in P0.
- **Partition size (infinite backing)** — grand networks vs height-accuracy vs seam visibility.
- **Water CA generalization cost** — the sim is a fixed box today; a sparse player-region port is
  real work (its GPU port "NOT yet a perf win"). Static-far/sim-near hybrid contains the blast radius.
- **Render density** — the standing #1 engine issue; caves benefit from occlusion culling, open
  terrain does not. Watch face counts as relief detail increases.
- **Grounding** — every ⚑GROUND marker above is a hard gate; no invented dimensions.

---

## References (from the design-space research)

Minecraft 1.18 density functions / noise router (art direction, splines, noise caves, aquifers);
Red Blob Games — Terrain from Noise, mapgen4, Procedural river drainage basins; Barnes, Lehman &
Mulla 2014 (Priority-Flood) + Barnes 2016 (parallel/tiled); Génevaux et al. 2013 (hydrology-based
terrain); Cordonnier et al. 2016 (uplift + stream-power erosion); Musgrave (ridged multifractal);
Inigo Quilez (domain warping, fBm); Sebastian Lague & Nick McDonald/weigert (droplet erosion &
SimpleHydrology); Azgaar Fantasy Map Generator (image→heightmap); alcatrazEscapee (river/chunk
tension); Vintage Story & Dwarf Fortress (precomputed coarse world model).
