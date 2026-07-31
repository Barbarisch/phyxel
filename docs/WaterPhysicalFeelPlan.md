# Water — Physical Feel Plan

> Status: **PARTIALLY BUILT.** Written 2026-07-28; state below current as of **2026-07-30**.
> Successor workstream to [`docs/WaterSystemV3.md`](WaterSystemV3.md), which made water *look* like a
> volume (refraction, absorption, swell, surf, flow shading). This plan is about making it *behave*
> like water against the world: breaking on land, splashing off voxels, moving grass, and running
> downhill in rivers you can actually go and look at.
>
> Standing discipline applies (⚑GROUND every dimension, red-before-green, named validation layer
> L1–L4 per deliverable) — **plus the look-first rule in §7**, and **§2e, which you must read before
> trusting any diagnosis in §2c/§2d.**

---

## 0. CURRENT STATE — pick up here (2026-07-30)

### Done, verified, on `origin/main`

> **ACTIVE WORKSTREAM (2026-07-30): the SMALL-SCALE plan** — user-chosen scope: (a) close-up
> physical feel (splashes, ripples, wading, buoyancy) + (b) small water bodies (creeks, puddles,
> ponds). Phases: 0 foundations (✅ DONE, below) → 1 MIN_HOLD shallow-water gate → 2 creek
> fractional ribbon (2a static, user-chosen) → 3 RippleField → 4 entity coupling (4.1 query API,
> 4.2 buoyancy, 4.3 wading; swimming deferred by user decision). The far-field Tier-0/outflow
> work in this doc's §2d is deliberately DEFERRED behind it.

| # | Item | Evidence |
|---|---|---|
| ✅ | **Bake honors `water.seaLevel`** (2026-07-30) — plumbed into `HydrologyMap`/`FlowField` construction + the seabed/altitude/flora/fauna material gates + the hydro-cache key; persisted as `WorldRecipe.seaLevelY` in `world_meta` (stored value wins, loader WARNs on game.json mismatch); loud closed-basin WARN at bake time (`HydrologyMap::minTerrain/hasOutlet`) | Red→green `TerrainRecipeTest.SeaLevelOverrideReachesTheHydrologyBake` (was 16 regardless of recipe); `SeaLevelRoundTripsThroughRecipeJson`; `HydrologyMapTest.ReportsOutletAndMinTerrain`; full suite 3021/3024 (2 fails = pre-existing LLM-network tests, 1 skip) |
| ✅ | **Near-field probes** (2026-07-30) — `water_probe` (per-cell mass/floor/solid/channel/pin/flow), `water_footprint` (rect confinement scan — the L2 assertion surface), `water_waterfalls`, `water_bake_info`, `water_ripple` (stub until Phase 3) | All exercised live in CreekLab/RiverLab; `WaterSimulation::sourceAt` added for the pin probe |
| ✅ | **Testbeds measured, §2e OVERTURNED** (2026-07-30) — see the correction in §2e; labs need NO seaLevel change and NO regen; reference baselines committed in `docs/water-refs/` with cameras recorded in each lab's game.json | CreekLab: min_terrain −23.5, outlet TRUE, drainage TRUE. RiverLab: min_terrain −14.1, outlet TRUE, max order 5, order-3 gorge verified wet (`water_footprint` 448 wet cells) |
| ✅ | **MIN_HOLD donor gate — shallow water can now EXIST** (small-scale Phase 1, 2026-07-30) — a donor at/below `MIN_HOLD` (0.3, `setMinHold`) makes no horizontal transfers; gravity/pressure ungated; deep bodies level exactly as before (a body only stalls when mass/area ≤ hold). Puddles rest and survive evaporation (hold > EVAP_THRESHOLD). **Three spec changes, deliberate:** a spill now settles as a bounded persistent puddle (was: sheets thin + evaporates to nothing); a point spring builds a compact pond whose radius scales with (pin − hold) via rim evaporation; a LINE source (river breach) is no longer evaporation-bounded — it fills its containment to spill and saturates (geometric bound; run-to-saturation test). Real baked rivers stay contained by construction: pins require carve ≥ 0.5 = exactly when `lround` recesses the bed a full voxel. GPU compute path lacks the gate (WARN on `enableGpu`). | Red→green ×5 (`FractionalCellOnDryFlatGroundHoldsStill`, `SpillFootprintIsBounded` incl. the invisible-film clause, `PuddlePersistsUnderEvaporation`, …); 95/95 water+hydrology tests incl. `ActiveSetMatchesFullSweepExactly` unmodified; **L4 CreekLab: the same 0.8 pour that measured 0→0.0 mass in 2 s now rests as a 4-cell puddle at 0.716 mass, constant over 60 s** (`docs/water-refs/creeklab_first_persistent_puddle_20260730.png`). |
| ✅ | **CREEKS CARRY WATER** (small-scale Phase 2a, 2026-07-30) — the four order-gates re-opened safely: `channelDepth` orders 1-2 → fractional 0.33/0.66; `segmentChannel` reports them; terrain carve explicitly clamped to order ≥ 3 (0.66 would `lround` to a trench); `applyRiverInflows` pins creek beds at `min(depth, MIN_HOLD)` — a pinned cell at/below the hold cannot make horizontal transfers, so the worst case is a static ribbon, never the 496cdc10 hillside sheet. Creek flora suppressed via `riverOrder`. **Bug found+fixed at L4:** the runtime order binding briefly used `orderAt` (the coarse CELL's order) with `channelAt`'s depth — at every junction cell the order-2 segment crossing the order-3 cell got FULL pins on uncarved ground → measured ~900-mass sheet flood. Contract: BOTH facts from the same `channelAt` hit; pinned by a segment-vs-cell FlowFieldTest assertion. | 97/97 water suites; red→green `CreekPinIsFractionalAndConfined` (zero off-band wet cells, no evaporation needed), `FractionalPinnedRibbonSettles`; **L4 CreekLab: order-2 ribbon exactly 1-2 columns wide at 0.300/cell over a 64-row strip incl. the ex-flood junction zone, total mass 38.4 constant over 60 s** (`docs/water-refs/creeklab_creek_ribbon_20260730.png`; geometry notes in CreekLab game.json — centerlines run between hydrology cell CENTRES, `river_order` is a cell label, `river_channel` is the line). |
| ✅ | **RippleField — water RESPONDS to disturbances** (small-scale Phase 3, 2026-07-31) — a 128×128 half-voxel damped wave-equation heightfield following the player (`RippleField.h/.cpp`, owned by `WaterManager`, `addRipple(worldPos, radius, strength)`); consumed by CELL water only in v1: `water_cell.vert` displaces the surface by world-XZ texture sample (curtain decision on undisplaced corners; skirt bottoms fixed), `water_cell.frag` tilts the normal by its gradient. GPU: R32F texture + set-1 binding 2 (VERTEX\|FRAGMENT), double-buffered persistent staging, in-frame barrier/copy before the water pass; push block 96→112 B (its last free vec4). **Sampler honors `SAMPLED_IMAGE_FILTER_LINEAR` support for R32F** (optional in Vulkan — LINEAR on unsupported hardware is UB). Debug: `water_ripple {x,z,radius,strength}`. Sea plane deferred to the shore-field work (its push block is full). | 6 `RippleFieldTest` (ring symmetry+propagation, long-range decay + genuine sleep, world-stationary recentre, window no-ops); L4 WaterLab tub: visible expanding ring (`docs/water-refs/waterlab_tub_ripple_ring_20260731.png`); measured frame-pair delta 23.15 (rippling, 6 s apart) vs 14.71 (still, 34 s apart) in a fixed pool crop — 57% more change at a 5.7× shorter interval. **Verification traps hit + recorded:** a ring lives ~4-6 s — screenshot latency alone can miss it entirely (pump impulses continuously while capturing); Σ\|h\| is not monotone for a wave field; multiplicative verlet damping needs exp(−2k·dt). |
| ✅ | **Sea mesh: polar → Cartesian clipmap** (`SeaMesh.h/.cpp`, `water.vert`) | Camera-centred wave vortex gone: top-down crest-orientation concentration **0.915** about one direction. **29,312 tris reaching 1024u** vs the old ~33,800 reaching 700u. 5 tests in `SeaMeshTest`. |
| ✅ | **Shallow-water "quilt"** — was the foam layer, now simplex + 2-scale mask | Attributed by A/B probe (`WATER_FOAM_DEBUG_SCALE`), not inference. Axis-alignment 1.13 → 0.98 against a 1.02 isotropic reference. |
| ✅ | **Per-cell water surface read as a tiled grid** (`water_cell.vert`) | Side faces nudged 0.04 outward overlapped the neighbour's top quad → double-blended bright lines at cell pitch (artifact peak 36.3px ≈ one voxel). Now only real curtains draw. |
| ✅ | **Rivers are findable** — `POST /api/debug/water_find_river` | Rivers need `world.streaming: true` (the bake lives on the streaming generator). Reports *why* a result is empty. |

### THE open defect

**Water is drawn from the sim's cell list, not from where water actually is.** The CA region is a
64×32×64 box centred on the viewer, so any body larger than it renders as a flat slab with straight
edges that ignores terrain and travels with the camera. Bounding the *simulation* is correct;
bounding the *render* by it is not. Design in §2d (two tiers, one renderer). Three cheap shortcuts
are ruled out with evidence in §2c/§2d — read those before proposing one.

### Attempted and REVERTED (do not retry as-is)

- **Far-water plane at one basin level** → filled the whole frame; terrain 234-291 is mostly below a
  277.6 level, so the dry-land gate correctly wetted it all. A bbox does not help.
- **Reusing the per-cell renderer for far water** → cells are hardcoded 1×1 in the vertex offsets,
  no scale field; ~90k instances for a modest lake.
- **Growing the CA region to 256×32×256** → that is *simulating an ocean*: **19 FPS**, mass
  oscillating 531k → 889k → 571k instead of settling. The region should get SMALLER, not bigger.
- **Creek water** (orders 1-2 were bone dry behind FOUR separate order≥3 gates) → the fix worked
  mechanically (0 → 1593 cells) but pinned a full voxel into channels with no bed, so it spread into
  a sheet across a hillside. Reverted. Creek *shape* needs a sub-voxel pin or a shallow carve.

### Traps that cost this session a day

1. **§2e** — the testbeds were misconfigured; most "water in the wrong place" evidence is void.
2. `set_camera` right after `launch_engine` is silently overwritten by the project's camera config.
   **`get_camera` and confirm before trusting a frame.**
3. `git checkout` does not rebuild. A "revert" is not real until the binary is rebuilt AND relaunched.
4. A restart empties the water sim; it refills over ~1 min. Check `total_mass` is RISING before
   concluding a change broke rendering.
5. Water shading edits: `build_shaders.bat` does NOT rebuild on `#include` changes — force-compile
   `water.frag`/`water_cell.frag` after touching `water_common.glsl`.

### Suggested order

1. **Tier 0 static water field** (§2d step 1) — removes the slab and is what makes water scale.
2. Give the CA region an **outflow boundary** instead of walls (§2d step 2).
3. Only then: creeks, shore field (§2), spray (§4), grass swash (§5).

### Decisions still needed from the user

Shore-field window size · whether sea wind direction belongs in `game.json` · how physical rivers
should get (flow forces on entities? erosion? visual only) · whether editing `voxel.frag` for shore
wetness is acceptable (it is on the hot path for all terrain).

---

## 1. Honest starting position

**What water does today**
- Sea: a Gerstner swell on a camera-following radial mesh, with whitecaps, screen-space refraction,
  Beer-Lambert absorption, a soft shoreline, a depth-driven surf band, and underwater fog.
- Sim: a mass-conserving CA with momentum, a per-cell flow proxy, sub-voxel floors, and waterfall
  lips that already spawn `VfxSystem` mist.
- Rivers: the drainage network supplies a kinematic flow direction for baked channels.

**What it does NOT do — the four things this plan targets**
1. **Waves ignore land.** The swell travels in one global wind direction. It does not refract, so an
   island gets a foam ring that correctly wraps its contour while the crests march straight past it.
   Nothing steepens, nothing peaks, nothing breaks *on* anything.
2. **Nothing splashes.** Water meeting a voxel wall produces no spray. The only particles in the
   whole water system are waterfall mist.
3. **Water and vegetation ignore each other.** Grass has a displacer system, but it holds **16**
   spheres — enough for characters, useless for a coastline. Grass standing in the surf neither
   bends nor looks wet.
4. ~~**Rivers have never been seen.**~~ **RESOLVED 2026-07-29.** The cause was structural: WaterLab
   has no hydrology bake (streaming off), so it could not contain a river at all. A river was then
   found and looked at in a streaming world - a wet, flowing order-3 gorge with whitewater - via the
   new `water_find_river`. NOTE the follow-on caveat in 2e: that testbed is itself misconfigured, so
   the river EXISTS and renders, but its water LEVEL there is not trustworthy evidence of anything.

---

## 2. The unifying primitive: the SHORE FIELD

Three of the four gaps need the same missing fact: *for this point on the water, where is land, how
far, and in which direction?* Building that once, well, is most of the work.

**What it is.** A CPU-baked 2D field that follows the player, per cell:

| channel | meaning |
|---|---|
| `depth` | water depth here (water surface Y − terrain top Y), negative on land |
| `shoreDist` | signed distance to the waterline (+ in water, − on land) |
| `shoreDir` | unit XZ direction pointing from land toward open water (the gradient of `shoreDist`) |
| `cliffiness` | local slope of the terrain — separates a gentle beach from a vertical sea wall |

**How it is built.** Terrain column heights come from the chunk system; the waterline is the
sea-level contour (or the baked hydrology level per column, when one exists). The distance transform
is a jump-flood over the grid — a handful of passes, trivially parallel, and it runs on the existing
`JobSystem` off the main thread. It recentres with the player exactly as the water sim region does.

⚑GROUND **cell size 2 m over a 512 m window = 256×256 cells**. 2 m is a quarter of the shortest
swell component (~8 m), which is the resolution refraction needs to bend smoothly; 512 m comfortably
covers the wave zone. Cost: 256² × 16 B = **1 MB**, rebuilt incrementally.

**Who consumes it**
- `water.vert` — refraction and shoaling (needs a **vertex-stage sampler**; the water descriptor set
  is currently `FRAGMENT_BIT` only, so its stage flags must widen).
- `water_common.glsl` — surf placement keyed to real distance-from-shore instead of view-ray depth.
- CPU — spray emitter placement, grass swash, and future gameplay (is this point in the surf zone?).

**Validation.** **L2** on the distance transform against analytic shapes: a circular island must give
radially symmetric `shoreDist` and `shoreDir` pointing outward at every bearing; a straight coast
must give a linear ramp and constant direction. **L4** a debug overlay that draws the field so it can
be eyeballed. Red-verify by feeding a known island and asserting the direction field.

**Risk.** This is the load-bearing dependency for §3–§5. If the bake is wrong, everything downstream
is subtly wrong in ways that look like shader bugs. Build and visualise it *first*, alone.

---

## 2b. ✅ DONE (2026-07-29) — the sea mesh topology was wrong; replaced with a clipmap

**Resolved.** The polar mesh is gone, replaced by the Cartesian clipmap in `SeaMesh.h/.cpp`, with
per-component Nyquist fading in `water.vert`. Measured outcome:

- **The vortex is gone.** Top-down from 396 units, crest orientation concentration is **0.915** about
  a single dominant direction — the crests are parallel. A radial/spoke pattern cannot score that,
  because its orientation varies with angular position by definition.
- **Cheaper, and reaches further:** 4 levels, 14,785 verts, **29,312 triangles reaching 1024 units**,
  against the polar mesh's ~33,800 triangles reaching only 700. Cost now grows one *level* per
  doubling of reach instead of one ring per 4.1 units.
- **The far sea kept its shape** — the added long-wavelength component survives the coarse outer
  levels, so swell lines still read to the horizon at reducing detail rather than flattening.
- Invariants pinned by `SeaMeshTest` (5 tests): no T-junctions, no edge shared by >2 triangles,
  constant angular density per level, coverage reaches the requested radius, bounded cost. Two of
  them **failed first and caught real bugs** — see the commit.

The analysis that led here is kept below because the *reasoning* is the reusable part.

### Original diagnosis

Looking straight down from altitude, the ocean shows a spiral centred on the camera. A first fix
(pushing the amplitude taper beyond the far plane) removed one cause, but the artifact returns as
soon as the swell is anything but tiny. The real cause is the **polar mesh itself**, and it cannot
be tuned away.

A camera-centred ring/sector grid has uniform RADIAL spacing but its ANGULAR spacing grows linearly
with radius (`arc = 2πr / sectors`). Measured at 96 sectors against the 14-unit swell:

| radius | arc | wavelengths per segment |
|---|---|---|
| 60 | 3.9 | 0.28 ✓ |
| 120 | 7.9 | 0.56 ✗ |
| 250 | 16.4 | 1.17 ✗ |
| 691 | 45.2 | 3.23 ✗ |

Nyquist needs ≤ 0.5, so everything past ~107 units is aliased **azimuthally**, which is precisely
what draws radial spokes. Brute force does not rescue it: Nyquist at only r=250 needs **224 sectors
(~79k triangles)** and r=691 would need ~620. The earlier analysis checked radial spacing, found it
uniform at 0.29 wavelengths, and wrongly concluded the sampling was sound — the angular axis was
never examined.

**The fix is a different topology, and it is a prerequisite for Phase A** (refraction only makes the
wave shape more important, so it will expose this harder):

- **Projected grid** — a grid uniform in SCREEN space, projected onto the water plane. Sampling is
  then uniform in the space that actually matters (pixels), there is no centre singularity, and
  vertex count is fixed regardless of view distance. The standard ocean technique. Main risk is the
  usual projected-grid edge cases (camera near the water plane, looking at the horizon).
- **Cartesian clipmap** — nested uniform grids, each ring 2× coarser, centred on the camera and
  snapped to world space. No angular singularity, LOD falls out naturally, simpler to reason about
  than a projected grid and easier to keep stable.

Recommendation: **clipmap**, for the stability and because snapping to world space avoids the
swimming that projected grids show when the camera rotates.

⚑Perf note: both give a FIXED vertex count independent of render distance, which is strictly better
than the current mesh, whose ring count scales with view distance.

## 2c. THE REAL REMAINING DEFECT — water renders only inside the sim box (found 2026-07-29)

From any vantage over a lake or river, the water reads as a flat translucent **square that follows
the camera**. Diagnosis history matters here, because two wrong answers came first:

1. "A mis-baked lake." Wrong — a bake is static in world space and would not follow the viewer.
2. "The bake pins water ~30 voxels above terrain." Wrong, and the more instructive error: that
   number is a lake's DEPTH, not an offset. `HydrologyMap.h` states the design outright — the flood
   runs on the layer-0 coarse height, water level is flat per basin by Priority-Flood construction,
   and "a column is under water only where its actual surface Y is below `waterLevelAt(x,z)`". A
   flat surface is always far above the bed at the deepest point. Comparing surface against bed and
   reading the difference as an error is how correct behaviour got filed as a bug.

**What is actually wrong is only the square.** Cell water exists solely inside the player-following
sim region (64×32×64), so any body larger than that is clipped to the box, and the box tracks the
camera. This is already a known unbuilt feature, not a defect — CLAUDE.md lists "far/near render LOD
for water beyond the sim region (Phase B)".

### Design — reuse the sea clipmap, do NOT build a second water renderer

The sea already solves the hard half of this. `SeaMesh`'s clipmap plus water.frag's dry-land gate
(`alpha *= 1 - smoothstep(0, 0.25, seabedY - restLevelY)`) means a horizontal plane rendered at a
given level **derives its own shoreline from the depth buffer**: pixels whose terrain is above the
level get no water, pixels below get water. That is exactly a lake's outline, for free, out to the
full render distance. So:

1. **CPU** — find the water body at/nearest the camera from the baked table: its flat level, plus its
   world-space bounding box by flood-filling the table (256×256 cells, trivial).
2. **Render** the existing clipmap a second time at that level, clipped to the bbox.
3. The shoreline, refraction, absorption and foam all come from the shared `water_common.glsl`, so a
   far lake looks like the near water instead of being a separate look.

### ❌ ATTEMPTED AND REVERTED 2026-07-29 — a single-level plane CANNOT work

The single-plane version above was implemented and reverted the same hour. It is recorded because
the failure rules out a whole family of cheap approaches.

Drawing the clipmap at the nearest basin's level (277.6) in RiverLab **filled the entire frame with
water.** The dry-land gate worked exactly as designed; the problem is that it can only ask "is the
terrain here below the level I was given". In this mountain range terrain runs 234-291, so more than
half of it IS below 277.6 and correctly received water. The result was a flooded world.

So the deferred bbox bound was never optional — and a bbox would not have saved it either, because
the terrain varies by tens of voxels WITHIN any single basin's bounding box. There is no
world-space rectangle that separates "this basin" from "lower ground that happens to be below its
level".

⚑**Therefore the per-cell table is mandatory, not an optimisation.** Each column has to be tested
against ITS OWN basin level, which is precisely what the CPU runtime does via `tableLevelAt`. The
shader needs the same data:
  * upload `HydrologyMap`'s level grid as an R32F texture (256x256 = 256 KB — trivial);
  * the far-water vertex/fragment stage samples it per column, discarding where the column is dry or
    where its own level differs from the surface being drawn;
  * that also removes the need to pick a single "nearest body" on the CPU at all — one draw can
    then cover every body in view, each at its own correct level.

Estimated scope: texture creation + upload on bake, one descriptor binding, a `far_water.frag`
variant of the existing shading, and the level lookup. The clipmap mesh and all of
`water_common.glsl` are reused unchanged.

### ❌ Also ruled out — reusing the per-cell renderer for far water

The tempting cheap alternative is to keep the existing `WaterCellRenderPipeline` and just feed it
coarse cells generated from the bake instead of from the sim. It does not work: `water_cell.vert`
bakes the quad size into the mesh's vertex offsets, so **every cell is exactly 1×1 world unit**, and
`WaterSurfaceCell` is four fully-occupied `vec4`s with nowhere to put a scale. Covering water to 384
units at 1×1 is ~90k instances for a modest lake and several MB of instance upload per frame.

So the choice is genuinely between (a) the hydrology texture on the clipmap, or (b) adding a cell-size
field to the cell vertex format so the same renderer can emit coarse far cells. (a) is preferred: it
leaves the near-field renderer untouched and one draw then covers every body in view.

### Why the camera-relative coupling exists at all

Worth stating so it is not mistaken for a bug: simulating the CA near the viewer is correct and
necessary — you cannot run a cellular automaton over a whole world. The mistake was that RENDERING
was implemented as "draw the cells the sim happens to have", so a static world-space fact (where
water is) inherited a camera-relative one (where it is being simulated). The bake is the right source
for rendering; the sim should only drive the near field where it adds motion.

⚑Cost: one extra clipmap draw (~29k triangles, the same mesh already resident), no new geometry and
no per-frame CPU meshing. Lakes are calm, so it should be drawn with a low wave amplitude.

## ⚠️ 2e. READ FIRST — much of §2b–§2d was diagnosed against BROKEN TEST WORLDS (2026-07-30)

> **⚡ MEASURED CORRECTION (2026-07-30, later the same day): the "broken testbeds" theory below is
> itself WRONG.** After plumbing `water.seaLevel` into the bake and adding `water_bake_info`
> (`HydrologyMap::minTerrain/hasOutlet`), both labs measure SOUND: CreekLab min terrain **−23.5**,
> RiverLab **−14.1** — both below sea level 16, so Priority-Flood has a real ocean outlet and
> drainage completes in both. The "terrain y≈36-41 / y≈100-380" readings were NEAR-SPAWN samples;
> over the full 32 km bake box both worlds reach the sea. Consequences: the perched high lakes in
> RiverLab (flat levels y≈272-346) are **legitimate mountain basins filled to their spill — do not
> "fix" them**; the labs need no seaLevel override and no regeneration; and every §2c/§2d symptom
> reduces to the two real defects — the **render slab** (§2c, still open) and the **small-scale
> gaps** (orders 1-2 dry, shallow water deleted — the active small-scale plan). The paragraphs
> below are kept as a record of the reasoning error: a config-level theory was adopted without a
> region-wide measurement, twice. The METHOD warnings (camera confirm, restart refill) remain valid.

Anyone picking this up should treat the diagnoses below with suspicion, because the testbeds they
were measured in are misconfigured **by me**, and I spent a day debugging the engine against my own
bad inputs.

**The broken testbeds.** `RiverLab` (Mountains) and `CreekLab` (Perlin) both set `bakedTable: true`
with **no `seaLevel`**, in worlds whose terrain sits at y≈100-380 (RiverLab) and y≈36-41 (CreekLab)
while the engine's `kSeaLevelY` is **16**. Priority-Flood needs an OUTLET at sea level to drain to.
With no terrain anywhere near y=16 the map is effectively one closed basin above the outlet, so it
fills to spill — producing lakes perched at y=143 / 277 / 321 **on hillsides**. That is the bake doing
exactly what it was asked with nonsense inputs, not an engine defect.

⚑**Before diagnosing any water behaviour, check that the world's terrain actually reaches sea level**,
or that `seaLevel` is set to match the terrain (as `WaterLab` correctly does: seaLevel 54 against
terrain at y 49-70). Every "water in the wrong place" symptom recorded here was measured in a world
where water genuinely belonged nowhere sensible.

**What survived and is trustworthy** (verified in WaterLab and by unit test, independent of the above):
the sea clipmap replacing the polar mesh (§2b — pinned by `SeaMeshTest`), the foam/quilt shading fix,
the per-cell water surface curtain fix, and `water_find_river`. These are shading/geometry and do not
depend on the broken worlds.

**What is NOT trustworthy:** the specific numbers and causal claims in §2c/§2d about lake levels,
flooding extent, and "water where it doesn't belong". The mechanism described (sealed sim region pools
water against its own walls) is probably real and worth investigating, but it was only ever observed in
worlds that had absurd water levels to begin with. **Re-derive it in a sanely configured world first.**

⚑**A second method error that makes some screenshots unreliable:** `set_camera` issued too soon after
`launch_engine` is silently overwritten by the project's own camera config once the game definition
loads. Several before/after comparisons in this session were captured at positions that were never
actually reached. **Always `get_camera` and confirm the position matches before trusting a frame.**

## 2d. THE ARCHITECTURE THIS SHOULD HAVE HAD (2026-07-29)

Everything above chases symptoms of one wrong assumption: that water which is DRAWN must be water
that is SIMULATED. It must not be. Almost all water in a world is at rest, and simulating rest is
pure waste.

**Two tiers, one surface.**

**Tier 0 - STATIC WATER FIELD.** One number per column: the water-surface level. The hydrology bake
already computes exactly this (`HydrologyMap` water levels + river channel levels). It is a 2D field,
so its cost is AREA, not volume, and it tiles/streams with the world exactly like terrain heightmaps
do. A column is wet iff `level > terrainTop`. No cells, no solver, no per-frame work. This is what
makes oceans and lakes viable across a multi-thousand-km world.

**Tier 1 - DYNAMIC CA.** A small region, only where water is actually CHANGING: near the player,
around terrain edits (a breached dam, a dug channel), waterfalls, splashes. It is seeded from Tier 0
and takes Tier 0 as its boundary condition, so its edges are not walls - water leaving the region
simply becomes static again at the field's level.

**One renderer, reading both.** The surface level always comes from Tier 0; Tier 1 only PERTURBS it
where it exists. This is the part that kills the slab: today simulated water and non-simulated water
are drawn by different systems (cells vs nothing at all), so the boundary between them is a hard
visible edge. If the surface is always the field, there is no seam - the CA just adds motion near the
viewer, the same way terrain LOD adds detail near the viewer without changing what the ground IS.

### What was tried instead, and why it failed

**Growing the CA region to cover the view** (256x32x256, 2.1M cells). This is simulating an ocean.
Measured: **19 FPS**, and total mass oscillating 531k -> 889k -> 571k rather than settling, i.e. the
solver never converges because it is being handed a vast body of standing water. Reverted to 64x32x64.
The region should if anything get SMALLER once Tier 0 renders.

**A single flat plane at one basin level.** Floods every basin below that level; a bbox does not help
because terrain varies by tens of voxels inside one basin's box.

**Reusing the per-cell renderer for far water.** Cells are hardcoded 1x1 in the vertex offsets with no
scale field, so covering any real distance means ~90k instances and MB of upload per frame.

All three are the same error in different clothes: trying to make the SIMULATION cover what only the
RENDERER needed to cover.

### Order of work

1. Expose Tier 0 as a sampleable field to the renderer (level grid as a texture, or a streamed tile
   cache alongside terrain). This alone removes the slab and makes oceans/lakes scale.
2. Make the CA take Tier 0 as its boundary condition instead of a hard region wall.
3. Only then revisit creeks, spray, and grass swash - each of which currently fights the slab.

## 3. Phase A — waves that attack the coast

Depends on §2. This is the "a round island should have waves moving toward it" ask.

- **Refraction.** Each Gerstner component's direction rotates from the wind heading toward
  `shoreDir` as depth falls. Real waves turn to run parallel to the depth contours, which is why
  surf wraps an island and arrives head-on from every bearing.
  ⚑GROUND: Snell's law for water waves, `sin θ / c` constant, with shallow-water celerity
  `c = sqrt(g·d)`.
- **Shoaling.** Amplitude *grows* as the wave enters shallow water before it breaks.
  ⚑GROUND: **Green's law, H ∝ d^(−1/4)**.
- **Compression.** Wavelength shortens in shallow water (`L = c·T`, `c = sqrt(g·d)`), so crests
  bunch up as they approach — the visual signature of an incoming set.
- **Breaking.** Cap amplitude at the McCowan limit **H = 0.78·d** (already the surf criterion), and
  past that point sharpen the crest and hand off to foam and spray.

**Watch the two shader footguns already documented in `water_common.glsl`:** never let an offset
grow with time, and never warp the sample coordinate anisotropically. Refraction rotates the wave
*direction*, which is a different thing from warping the sample point — keep it that way.

**Validation.** **L2** on the refraction helper: a synthetic circular island must produce wave
directions converging on it from all bearings. **L4** build a literal round island in WaterLab and
photograph it from above — the crests should curve in. That screenshot is the acceptance test for
this phase, and it is exactly the picture that is impossible today.

---

## 4. Phase B — splash and spray against voxels

The wave function is deterministic and cheap, so the **CPU can evaluate the same Gerstner sum the
vertex shader does**. That means the CPU knows where crests are, and combined with the shore field it
knows where they break — so it can place particles without any GPU readback.

- **Breaker spray** along the break line: `VfxSystem::spawnBurst` with a `Cone` shape aimed up and
  shoreward, rate scaled by wave height.
- **Impact spray on vertical faces.** Where `cliffiness` says the shore is a wall rather than a
  beach, a crest hitting it throws a much taller, narrower plume — the sea-wall look, and the thing
  that makes water feel like it has mass.
- **Reuse the existing pattern.** Waterfall mist already does continuous `spawnBurst` from detected
  lips with a `MAX_WATERFALLS = 48` cap; splash should copy that structure, including the cap.
- **Plunge-pool foam** where a waterfall lands: a persistent foam disc, not just airborne mist.

⚑GROUND emitter budget: a hard cap in the same class as `MAX_WATERFALLS` (48), chosen against the
particle system's own budget, with the count per emitter scaled by wave energy.

**Validation.** **L2** emitters land on the break line and inside the cap under a stress case (a long
convoluted coast). **L4** the coast at a fixed vantage, before/after. **Perf** measured in Release —
particles are the classic way to quietly lose 5 ms.

---

## 5. Phase C — water moves the world (grass, wetness)

- **Swash.** Grass inside the surf band bends away from the incoming wave and springs back as it
  retreats. This must be a **field**, not the displacer array — 16 spheres cannot describe a
  coastline. Feed `grass.vert` the shore field plus the wave phase so each blade derives its own
  bend, exactly as it already derives its own wind response.
- **Submerged grass** should flatten and damp — grass under water does not wave in the wind.
- **Wetness.** Terrain within the swash band darkens, and the darkening *lags* the retreating wave.
  A wet-sand band that recedes after each wave is one of the strongest cues that water is real. This
  touches the terrain shader (`voxel.frag`) — a small, contained change, but outside the water pass,
  so it needs its own before/after.

**Validation.** **L2** the swash bend is zero outside the band and continuous across it (no popping
at the boundary — the same class of defect as the flow-tiling bug). **L4** stand in the surf and
watch. **Perf** grass is already a heavy pass; measure it, do not assume.

---

## 6. Phase D — rivers you can actually see

**This phase starts by fixing the reason rivers are invisible, before touching any shading.**

- **D0 — a river locator + testbed.** A `water_find_river` debug command that scans the baked
  `FlowField` for the nearest channel of order ≥ N and returns its world position (and can put the
  camera there). Rivers are ~5 voxels wide and can be kilometres inland; hunting for one by hand is
  why this has never been looked at. Then a **RiverLab** project: streamed, height-based, hydrology
  baked, with a saved camera on a known order-3+ channel. **Nothing else in this phase is
  meaningful until you can get to a river in one command.**
- **D1 — verify what already exists.** The flow proxy and kinematic river direction have L2 tests
  and zero L4. Confirm at runtime that a baked river reads as flowing, and report honestly if it
  does not.
- **D2 — river surface.** Bank foam where fast water meets the bank (shore field again), streaks
  advected along the flow (already built), and **obstacle wakes** — a bow wave upstream of a voxel
  standing in the current and a foam tail downstream. That is the river equivalent of "splashes
  against static voxels".
- **D3 — waterfall polish.** Spray density scaled by drop height, plus the plunge-pool foam from §4.
- **D4 — honest limit.** A baked river is a *pinned reservoir*: it does not advect mass, so nothing
  floats down it. Real transport needs the Phase 4A capacity work plus genuine inflow/outflow. Call
  that out rather than implying rivers "flow" in a physical sense when they are shaded to look like
  they do.

---

## 7. How this work gets judged (the process fix)

On 2026-07-28 a day of individually-justified changes made the water look worse, because each was
validated against a *metric* (perf, Nyquist sampling, physical grounding) and never against the
previous image. The rules now:

1. **Keep a reference screenshot** of the best-known state, at a fixed camera, committed alongside
   the work.
2. **Every visual change gets a same-vantage A/B** against that reference before it is committed.
3. **A metric improving is not evidence it looks better.** Perf wins, correct sampling and honest
   physical grounding are necessary, not sufficient. The question is always "is this prettier than
   the reference?" — and if it is not, the change does not land, however correct it is.
4. **Useful measured proxies:** fine-scale roughness at 4 px in an open-water crop catches ridging
   and corduroy (good look ≈ 1.7, over-detailed ≈ 5.0); row-to-row luminance change catches relief;
   frame-to-frame delta in a fixed crop proves motion is real rather than painted.
5. **Slope, not amplitude, is what the eye sees** for surface detail: an octave's visual weight is
   `amplitude × frequency`. Adding octaves without holding the slope sum roughly constant is what
   produced the corduroy.

---

## 8. Suggested order, and why

| # | Phase | Why here |
|---|---|---|
| 1 | §2 Shore field + §6 D0 river locator | Unblocks everything; and D0 is the difference between rivers being reviewable or not. Cheap, high leverage, no visual risk. |
| 2 | §3 Phase A — refraction, shoaling, breaking | The biggest single step toward "physical", and the direct answer to waves-should-move-toward-land. |
| 3 | §4 Phase B — spray | Turns a shading effect into something with apparent mass. Needs A's break line. |
| 4 | §6 D1–D3 — rivers | Makes an already-built system visible and finishes it. |
| 5 | §5 Phase C — grass and wetness | Highest polish-per-risk once the field exists, but it touches the grass and terrain passes, so it goes after the water is settled. |

---

## 9. Decisions needed before starting

- **Shore-field window size vs cost.** 512 m at 2 m/cell is the proposal. A bigger window means
  refraction that keeps working as view distance grows; it costs memory and bake time.
- **Does the sea get a wind direction per world?** Refraction needs a base heading; today it is one
  shader constant. It probably belongs in the `water` block of `game.json` alongside amplitude.
- **How physical should rivers get?** D2 shading is cheap and convincing. Real advection (things
  floating downstream) is Phase 4A-scale work and should be a deliberate decision, not a drift.
- **Wetness on terrain** means editing the voxel fragment shader, which every material in the game
  goes through. Worth confirming that is acceptable before starting §5.
