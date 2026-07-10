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
>
> **Water has a dedicated runtime companion:** [`docs/WaterSystemV2.md`](WaterSystemV2.md). This
> doc owns *generation* (what the world bakes: sea level, per-basin lake levels, river graph);
> WaterSystemV2 owns the *runtime* (how the CA sim receives and renders it). **§P2 here == Water
> Phase C there** — the seam where the two meet. Keep the two docs reconciled; a change to the
> baked hydrology contract in P2 must be mirrored in WaterSystemV2 Phase C, and vice-versa.

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

### P0 — CoarseWorldModel scaffold + immediate mountain drama  ✅ IMPLEMENTED (2026-07-09, validated L2 + L4)

> **Shipped in working tree (not yet committed):** `CoarseWorldModel` (Layer 0, `engine/{include,src}/core/CoarseWorldModel.*`),
> noise extracted to pure free functions, `WorldGenerator::sampleColumn` sources its continental
> base from the coarse model, and ridged-multifractal + domain-warp + amplification-spline mountain
> relief in `surfaceVariationFor`. Far-terrain LOD is consistent for free (it routes through
> `sampleSurface`→`sampleColumn`). **Measured:** Mountains peak relief **~334 voxels** (was ~64),
> steepFrac 0.49 vs hills 0.05, worst chunk-border adjacent-column jump **4** (no seams),
> deterministic. Runtime L4: peaks Y 199–322 over a 128-wide region, renders as a towering
> stone-faced grass-based mountain. Full unit suite: **2618 passed, 0 failed, 4 skipped** (the 4
> skips are env-gated — 3 AIEndToEndTest need PHYXEL_AI_API_KEY, 1 CharacterSkeletonTest needs an
> anim file — unrelated to terrain). Independently audited (solution-auditor reproduced
> red→green: pre-P0 peakRelief=25 FAIL → P0 peakRelief=334 PASS). Feature claims verified REAL.
> Tuned constants: `kRmFreq=0.006`, `kMountainAmp=288`, `kContinentalMax=96`, continentalness
> contrast ×1.9. Tests: `tests/core/{CoarseWorldModelTest,TerrainReliefTest}.cpp`.

- **Foundation:** introduce `CoarseWorldModel` (interface + bounded backing first), persisted in
  `world.db`. Seed it trivially from the *current* noise so nothing regresses. Wire Layer-1
  `WorldGenerator` to sample it. Also wire it as the source for the existing far-terrain LOD
  (`sampleSurface`), which already wants exactly this heightmap.
- **Local drama (cheap, high-impact):** add **ridged multifractal** (`1−abs(noise)`, octave-weighted
  by the previous octave → rough peaks, smooth lowlands) + **domain warping** (`fbm(p+fbm(p))`) +
  an **amplification spline** on final height. Remove the ±9 continental cap and deepen the usable
  vertical range (Y is already unbounded in the engine; only the constants are shallow).
- **Validation:** L2 (height-profile / ridge-continuity assertions on real output) + L4 (runtime
  screenshots of a dramatic skyline). **Stress:** generate a full region, assert no seams at
  chunk borders and monotonic ridge behavior.

**Grounded values for P0 (grounding-auditor, 2026-07-09):**
| Value | Grounded/decided | Source |
|-------|------------------|--------|
| Meters per voxel | **1.00 m** (structures/characters 1:1) | CDC/NCHS NHANES 2017-20: 175.4 cm ÷ 1.751-cube character = 1.0017 |
| Terrain vertical scale | **COMPRESSED** — grandest peaks **~384 voxels** above sea level (~12 chunks); typical mountains 128–256. ~1 terrain-voxel ≈ 12–23 m of real relief for large landforms *(user decision — a tunable amplitude, dial up in P5)* | design call; real range Mont Blanc 4,809 m → Everest 8,849 m compressed |
| Sea-level Y | **Y=16** (named `kSeaLevelY`) — *engineering continuity* with existing worlds (Flat stays Y=16), NOT a geographic figure; Y is an arbitrary unbounded origin | stated rationale, not a citation (auditor: Y=16 is unsourceable convenience — declare the rationale) |
| Ridged-multifractal | H=1.0, offset=1.0, gain=2.0, **lacunarity=2.0**, **octaves=6** (tunable knob, not a fact) | Musgrave `musgrave.c` (H/offset/gain); libnoise/SharpNoise lineage (lacunarity/octaves default) |
| Ocean/shelf depth | *range grounded, point value deferred to P1/P2:* near-shore 60–140 voxels; abyssal 3,000–6,000 (cap for perf, declare the cap) | NOAA/Britannica shelf-break ~133 m, avg ocean 3,682 m |

> Note: the repo asserts "1 cube ≈ 1 m" **uncited** in 4 places (`DimensionCanon.h:9`, `scale.py:6-7`,
> `StructureGenerationPipeline.md:113`, `character_design_constraints.json`). Housekeeping follow-up:
> paste the NHANES citation there so the ratio stops being bare. Tracked, not P0-blocking.

### P1 — Density-function evaluator + biome overhaul

> **Increment 1 ✅ IMPLEMENTED (2026-07-09, uncommitted): slope + altitude/temperature material
> rules.** `sampleColumn` now surfaces the terrain physically on top of the biome material:
> lapse-rate **snow line** (Ice), **exposed rock** (Stone) on slopes past the angle of repose, and
> a **sand seabed** below sea level. Grounded (grounding-auditor): temperature field anchored to
> Whittaker 1975 (−5..+30 °C / 35° span → freezing=0.143); ISA lapse 6.5 °C/km × ~15 m/voxel
> compression = 0.00279 /voxel; angle-of-repose 35° → 0.70 rise/run. Measured (TerrainMaterialTest,
> real output): 17,224 tall peaks / 0 grassy, 14,795 snow columns (0 effective-temp violations),
> 28,584 rock faces, seabed cols all sand; L4 shows rock faces + grass base render on a dramatic
> peak. Solution-auditor reproduced red→green.
> **Deferred to P2 (needs real coastline/ocean depth — the auditor caught these as overreach/dead
> code):** coastal **beaches** (an altitude-only rule sands inland lowland, no shore to abut) and
> **seabed sediment zonation** (shallow sand → deep gravel; the ocean floor only reaches ~40 voxels
> down at P1's compressed base, so a 130 m shelf split was unreachable). Follow-up: add a white
> **Snow** material (Ice reads glassy for caps).
>
> **Increment 2 ✅ IMPLEMENTED (2026-07-09, uncommitted, pending auditor): continentalness axis +
> climate contrast + new biomes.** (a) Added a **continentalness** third axis to biome selection
> (`Biome::contMin/contMax`, default full-range → term cancels for land biomes, a dormant hook for
> P2 ocean/coast). (b) **Contrast-expanded temperature/moisture** (×1.9): multi-octave fbm clustered
> at 0.5 so Plains won ~98% and extreme biomes were unreachable — now all biomes appear. (c) Two new
> grounded land biomes: **Jungle** (Köppen Af/Am/Cfa, hot+wet, temp01 0.75–1.0 = MAT ~21–30 °C,
> moist01 0.65–1.0 = ~1500–4000 mm/yr via Holdridge log2) and **Tundra** (Köppen ET, cold+dry
> treeless, temp01 0–0.16 = MAT −5–+0.6 °C, moist01 0–0.18 = ~250–410 mm/yr). Measured
> (TerrainBiomeTest): Jungle 5,794 cols @ temp 0.76/moist 0.74, Tundra 4,276 @ 0.25/0.22, all
> pre-existing biomes retained; full suite 0 failures. **Flora gate added:** plants no longer land
> on the seabed, bare-rock cliffs, or snow-capped non-snow biomes (Snow biome keeps its conifers).
> **Perf:** `surfaceVariationFor` takes continentalness so the slope pass samples the coarse model
> once per neighbour instead of twice.
> Ocean/Beach/Marsh deferred to P2 (need coastline/hydrology — grounding-auditor confirmed no real
> climate band for a marsh).
> **Known limitations (tracked, not bugs):** (1) Desert is rare (~5/90k cols) — the hot+dry corner
> is improbable because temperature and moisture are independent noise; realistic biome frequency
> needs correlated climate / rain-shadow (future). (2) Tundra's Gravel surface is usually overridden
> to Ice by the lapse rule (physically correct — it's snow-covered); its distinctness is being
> treeless. (3) No GrassJungle / Snow / Mud materials yet (Jungle reuses GrassForest, caps use Ice).
> (4) `kRidgedNorm=1.35` is an empirical normalization (validated by peak-relief bounds, not derived).
> (5) Treeline == snowline for now (no separate alpine-meadow band below permanent snow).
> **Increment 3 ✅ IMPLEMENTED (2026-07-09, uncommitted, pending auditor): spline evaluator +
> recipe-driven terrain shaping.** New `Spline` primitive (`engine/{include,src}/core/Spline.*`) —
> a piecewise smoothstep curve over control points, the "how tall" art-direction knob (Minecraft-
> style). The continentalness → base-elevation composition now runs through
> `WorldGenerator::m_continentalHeightSpline`; the default 2-point ramp is **byte-identical** to the
> old hardcoded `continentalBase` (behavior-preserving — relief 364 / biome counts unchanged). The
> spline is **recipe-overridable** (`WorldRecipe::heightSpline`) and round-trips through `world.db`
> JSON, so a world can reshape its coastline/plateau profile without recompiling — the first concrete
> art-direction control and the groundwork for drawn-map height fields (P4). Captured by value into
> the coarse-model pure source (worker-copy-safe). Tests: `SplineTest` (8), `TerrainRecipeTest` (2,
> incl. override-reshapes-terrain + JSON round-trip). Full suite 0 failures.
> **Follow-on (not this increment):** nested splines (continentalness→erosion→peaks-valleys, the full
> Minecraft router), an `erosion` noise field for terrain variety, and mountain-amplitude as a spline.

- Replace the ad-hoc height formula with the **density/spline node graph** (2a). Model
  continentalness → erosion → peaks-valleys as nested splines.
- **Biomes:** make `continentalness` actually drive selection (currently unused); add **ocean,
  beach, alpine/snow-cap, wetland** biomes; add per-biome **carving hooks** (cave frequency, valley
  bias) and slope/altitude material rules. ⚑GROUND biome climate bands.
- **Validation:** L2 (biome-map continuity, no cliff seams at borders) + L4. Keep the recipe as the
  DB source of truth.

### P2 — Hydrology bake (the headline: rivers, lakes, oceans)

> **Progress:** P2.1 `PriorityFlood` (commit 1937b95) · P2.2 `HydrologyMap` — lake/sea levels
> (eaa1a15) · P2.3a `FlowField` — flow accumulation (river signal; valley funnels 81% of a region to
> its mouth) · P2.3b-1 Strahler order + grounded channel geometry · P2.3b-2a `channelAt` segment
> carve query — all standalone/tested/audited. **P2.3b-2b SHIPPED (commit f7b6be4, 2026-07-10):**
> the pipeline is now WIRED INTO the live generator — `rebuildCoarseModel` bakes a `HydrologyMap` +
> `FlowField` over a bounded 256×256 @32m region on the full surface height; `sampleColumn` carves
> order≥3 channels and shapes VALLEYS (attenuates Layer-1 relief toward the thalweg over a corridor
> `channelHalfWidth·kValleyWidthMul`, `kValleyWidthMul=5.0` per Williams 1986 / Rosgen 1994). L3 test
> `TerrainRiverTest` (carve-wired, 0.86 beds-in-valley, acyclic-downhill, deterministic; red-before-
> green for both carve + valley shaping, solution-auditor PASS). Rivers are visible carved valleys at
> L4; WATER FILL is WaterSystemV2's job. Next: sinuosity/meander, order≥4 rivers, infinite-region
> partitioning (P5), bed material grounding.

**Grounded values for P2.3 rivers (grounding-auditor, 2026-07-09; coarse cell = 32 m ≈ 1024 m²):**
| Value | Grounded | Source | Design decision to state |
|-------|----------|--------|--------------------------|
| Channel-initiation threshold | **49–977 cells** (0.05–1 km², climate-dependent) | Montgomery & Dietrich 1992, *Science* 255 | Pick one value + its climate analog. Also: our pure cell-count D8 is a **simplification** of the real area×slope² criterion — state it. |
| Width by Strahler order 1–6 | **2, 3, 5, 8, 14, 22 voxels** | Doll et al. NC Coastal-Plain regression W=10.97·A^0.36 (r²=0.87) + Horton area-ratio ≈4 chaining | Region analog (Coastal Plain vs Piedmont vs Mtn) + the order↔area Horton chaining are design synthesis, not a measured per-order table. |
| Depth by order 1–6 | 0.24, 0.36, 0.55, 0.84, 1.27, 1.92 m | Doll et al. D=1.29·A^0.30 (W/D 5–19, narrower than Rosgen's 10–30) | **Orders 1–2 are SUB-VOXEL** — do NOT round to 1 voxel (300% error). Carve bed only for order ≥3 (~1,1,1,2 voxels); orders 1–2 are surface-only until fractional/subcube carving. Incision is Layer-1 local detail → ~1:1 voxels (not vertically compressed). |
| Sinuosity (deferred) | straight <1.05 / sinuous 1.05–1.5 / meandering ≥1.5 | Leopold-Wolman-Miller 1964; Rosgen 1994 | Later refinement; fetch Rosgen 1994 directly before shipping. |

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
  **The water-runtime work this requires — freeing the CA from its fixed box, active-set/sleep,
  per-region levels, sparse field persistence in `world.db`, and this generation→CA wiring — is
  planned phase-by-phase in [`docs/WaterSystemV2.md`](WaterSystemV2.md) (Phases A–C). This P2 is
  its Phase C.**
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
