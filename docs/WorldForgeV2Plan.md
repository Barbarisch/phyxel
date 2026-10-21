# WorldForge v2 — the Ultimate World/City Forge (plan of record)

> **Status (2026-10-14, rev 3): SIGNED OFF; M0 IN PROGRESS on `feat/worldforge-v2`.** Rev 3 carries the F4/F4b grounding results (§10), the F5 audit-of-audit corrections (four wrong claims fixed, eleven citations re-cited), the F6 lab worlds (§16) and the M0 design-check verdict. Written from a
> read-only audit of the generator stack (six parallel code audits — planning cores, terrain/
> hydrology, realizer contracts, streaming/far-terrain/standalone, editor glue/API/tests/data — plus
> a docs/ledger survey and external research, all 2026-10-14) and the owner's brief. Rev 2 adds §13,
> the code-contact inventory per milestone, and corrects rev 1's standalone-streaming claim. It **supersedes** `WorldForge.md` "Key decisions" 1 and 3 and its "Known gaps",
> `StructureGenerationV2.md` phases P8–P10 (NetworkGraph / tensor-field roads / zoning — never the
> build), and the world-tier items scattered across `StructurePipelineGaps.md`, `WorldLookBacklog.md`
> and `WorldRenderV2Plan.md` M4–M6. `WorldForge.md` and `CityForgePlan.md` stay as the record of what
> V1 shipped. `WorldModel.md` (recipe + overlay, always-stream) and `LargeWorldScalePlan.md`
> (perf/scale ledger) remain authoritative in their own scope; this plan builds on them.
>
> **Boundary with the water overhaul (`feat/water-rethink`, `docs/WaterCore.md` §25–26):** this
> plan **shapes** terrain for water — beds, basins, banks, shelves, spring sites, drainage — and
> **never fills** it. No levels-as-rendering, no universal water level, no water placement. The
> exact hand-off contract is §4. Everything on the water side of that line is WaterCore's.

---

## 0. The brief (owner, 2026-10-14, paraphrased closely)

Procedurally generate **terrain, towns, cities, road ways and water ways** that are **epic in scale
and functional at small detail**. The current WorldForge/CityForge code is rough, has
immersion-breaking bugs, and lacks the scale. Look at what we have in code and plans, research the
field, and design the ultimate version.

Standing directives this plan is checked against (all still binding):

| Directive | Source |
|---|---|
| World size is not a mode; world = recipe + DB overlay; loading always streams | `WorldModel.md` §3 (2026-08-04) |
| Water exists only in terrain basins; basin first; existence never depends on the camera | `Water.md` §1–2, `WaterRethink.md` §0 (2026-08-03/10-08) |
| Walkability by construction; playtest defects feed the generator, never DB patches | `WalkabilityGateAndPlaytestLoop.md` |
| Chunks must not be visible: appearance = f(world position, persistent state) | `FeatureDesignKeys.md` |
| Generator output only — never a hand-assembled lookalike | `CLAUDE.md` hard rule |
| Ground every dimension; red-before-green; stress phase; validation depth per use | `CLAUDE.md`, `structure-generation/README.md` |
| "If a character cannot stand under it, it is not a roof"; "not a dumb pile of voxels" | `StructurePipelineGaps.md` 2026-08-28 |
| Furniture micro-precise; no embedded character spawns; no frozen engine during builds | memory directives |
| W1/W2 (NPCs inside buildings, seated pose) are NOT the priority | owner, 2026-08-28 |

---

## 1. Where we are (audit facts, verified in source 2026-10-14)

**Keep — this is a real foundation.** Everything is a pure function of (world position, seed,
recipe) with worker-safe copies, pinned by `RoadFieldSeamTest` / `FloraMarginTest`. Priority-Flood +
flow accumulation + Strahler (`PriorityFlood.cpp`, `FlowField.cpp`) are correct and cell-size
agnostic. Plan → ledger → realize has plan-hash drift refusal, bounded residency waits, idempotent
re-runs, atomic checkpoints. Roads baked into `ColumnSample` are the right streaming architecture.
The validators (`SettlementWalkability`, `StreetPaver` step-up invariant, `TraversalProbe`,
`RealizedWorldValidator`), the provenance-carrying `settlement_program.json`, and the
red-before-green + auditor discipline are assets. The structure forge (typologies, multi-story,
HearthForge, items everywhere, L3 gates) is the fine-detail spine and is **not** replaced here.

**Replace — three planning cores are prototypes, and they are the ceiling:**

| Layer | Today | Hard limit (file:line) |
|---|---|---|
| Terrain source | 2-oct fbm + spline + ridged multifractal, re-evaluated per column, never stored | no erosion, plateaus, canyons, shelves, beaches; ~140 noise evals/column incl. a 4-neighbour slope re-sample (`WorldGenerator.cpp:1028-1034`) |
| Hydrology bake | one fixed box centred on the origin | 256 × 128 u = 32 km (`WorldGenerator.cpp:129-131`); rivers die at the edge; river beds 1 voxel deep for orders 3–5 and 2 for order 6 (`FlowField.cpp:98-106`) |
| Siting | score, pick, fixed rects; rank 0 town, 1–2 villages, rest hamlets | 3–8 sites, radius 512–8192 u (`WorldForgeParams.h:39-40`); no city tier at world scale (`WorldForgePlan.cpp:349-350`) |
| Routing | all-pairs A* on the 128 u grid, MST + 1.4× detour loops | max grade 0.5 = 27° (`WorldForgePlan.h:131`); spans > 96 u get no bridge and the road dead-ends (`WorldForgePlan.cpp:604-611`); unbounded cut/fill with a 3 u shoulder |
| Settlement layout | one straight spine or cross-axis grid; axis-aligned rects in a rect site | city mode ignores terrain for its axes (`SettlementBuildService.cpp:252-253`); wall = the site rectangle |
| Realization | orchestrated serial bake job gated on live residency | no lazy realization; packaged games DO stream (`EngineRuntime.cpp:333-349`, G-177 — the 2026-09-25 gap-ledger entry is stale) but have **no realizer host**: `MainThreadJobs`, `WorldForgeBuildService`, `ResidentSpawner`, light/location restore, far-tree exclusions and structure LOD targets exist only in `Application.cpp` (§13 M6) |

**Immersion breaks that are code, not luck** (each gets a red test in M0): site analysis called
with an empty water function (`SettlementBuildService.cpp:228-231`); fences/props/spurs stamped
before buildings so a refused lot keeps its terrace, fence and a paved spur to nothing
(`:1066-1067`, `:593-607`); the street sweep erases a band over paving after buildings exist
(`:1074-1101`); roads cut/fill to unbounded depth (`WorldGenerator.cpp:1067-1079`); only the first
arriving road aligns the main street (`WorldForgeBuildService.cpp:34-52`); the walkability gate
reports and ships anyway; silent road-raster cap (`WorldForgePlan.cpp:769`); `room_program.json`
load result ignored (`SettlementBuildService.cpp:199`); terraces emit hard-coded Grass/Dirt in any
biome (`:440-442`); order 1–2 creeks are a notch across every road (`WorldGenerator.cpp:1070,1094`).

**Second-pass facts (realizer, runtime, glue audits 2026-10-14) that change the plan:**
- Every realizer is **axis-aligned-rect only**: a lot is min-corner + `footprint:[w,d]` + `front`
  side + typology/style/seed with no rotation input (`StructureForge.cpp:247-250`,
  `BuildingProgram.cpp:129-137`, `BuildingProgram.h:103-109`); `StreetPaver` takes rect runs graded
  along their long axis (`StreetPaver.h:59`, `StreetPaver.cpp:40`); `TownWall` is a 4-band rect
  circuit (`planTownWall`, `TownWall.cpp:60-168`);
  `analyzeSite`/terrace/pad are W×D grids. Polygons need a lot-extraction step and new street/wall
  planners (§13 M5).
- **Realizing lot k deletes any structure whose XZ bbox overlaps** (`StructureForge.cpp:447-457`)
  — in a zero-side-gap burgage row that includes the neighbour. **Placing into a non-resident,
  unsaved chunk materializes an EMPTY chunk** (`ensureChunkAt` → `createChunk(populate=false)`,
  `ChunkManager.cpp:390-415`) and returns success; the service refuses only at 0 placed
  (`StructureBuildService.cpp:113`). Both block lazy per-lot realization until fixed (§13 M6).
- Settlement buildings are built with **no `period`** (`SettlementBuildService.cpp:530-538`), so
  the era hook never reaches a building; `resources/room_programs/<period>.json` (the documented
  era data pack, `StructureForge.cpp:219-226`) **does not exist**; `morphology` dispatch is C++
  (`SettlementBuildService.cpp:183-195`); stories are hard-coded to one 3-high story (`:536`).
- The **mountain-flank streaming hole is confirmed**: the vertical wanted set is ±1 band around a
  surface sampled at ONE column per chunk column, cached forever (`ChunkStreamingManager.cpp:
  337-359`); relief > ~64 u inside a chunk column leaves a flank chunk unstreamed.
- **Five `WorldGenerator` instances** read Layer 0 concurrently (3 workers via the default copy
  ctor, the far-terrain mesher's private copy, the live one on the main thread,
  `ChunkManager.cpp:93-97`, `RenderCoordinator.cpp:1609`); the hydro memo is process-wide but its
  key omits the plan, and `applyRecipe` runs twice per load (`GameDefinitionLoader.cpp:496-546`).
  `FarTerrainManager::configure` IS re-callable (stops the worker, rebuilds the mesher from a fresh generator copy, `FarTerrainManager.cpp:36-55`) and the WorldForge live-apply path already calls it (`Application.cpp:13586-13596`); only the auto-config guard at `RenderCoordinator.cpp:1579` is one-shot. The real gap is any recipe change that bypasses that live-apply path (F5 correction).
- `world_meta` is TEXT-only (`WorldStorage.cpp:237-240`); the baked-sheet cache needs its own table
  (`chunk_lod_blobs` is the precedent). Pristine chunks are never written; a realized lot is its
  dirty chunks + `placed_objects` rows + locations saved only on explicit checkpoint — eager record
  saves create ghosts (`StructureBuildService.cpp:193-195`).
- `SettlementBuildService::Deps` is filled by hand at `Application.cpp:13410` and `:13646`, and the
  separate `StructureBuildService::Deps` at `Application.cpp:17298` and (weaker, no
  itemProps/keepClear/lights) `GameDefinitionLoader.cpp:861-866` (F5 correction: two types); `worldforge_build` holds a raw plan
  pointer for the job lifetime (`Application.cpp:13663`) and leaks the raised load radii on a
  mid-job crash (`:13672-13692`, no RAII).
- `/api/world/terrain_height` scans resident chunks to y = 255; `worldforge_focus` uses the
  generator — **two truths for "surface"**. `generate_world` bypasses the streaming generator
  entirely (a plan-less second terrain path).
- **The Stop-hook gate was not wired** when the audit ran (`.claude/settings.json` was `{"hooks": {}}`);
  it was wired and tested the same day (§15 F2).
- The active perf program measures a 104-building city at ~50 ms GPU on the 4090 after its first
  fix (`PerfProgram2026-09.md` §16.12); **T4's 200-building city is render-gated** and sequenced
  against that program's §17, not ahead of it.

**Doc rot to retire in M0:** `WorldForge.md` "Known gaps" contradicts its own resolved sections;
`WorldModel.md` §2 calls recipe-owns-seed a gap after it shipped; `StructurePipelineGaps.md`
2026-09-25 (standalone streaming) is stale; the MCP `worldforge_*` descriptions still say
"clamped 3-8", "128u each" and "restart_required"; "next" is claimed in six docs.

---

## 2. Target — what "epic scale + fine detail" means, measurably

| # | Target | How it is measured |
|---|---|---|
| T1 | A **continent** of at least 100 km × 100 km with drainage-consistent relief (dendritic valleys, plateaus, coasts with shelves/beaches), no origin-bound box, no edge where rivers die | macro sheet 200 × 200 cells at 512 u bakes < 2 s Release; `HydroEdgeTest`: no channel terminates at a tile edge |
| T2 | **Hundreds of settlements** in a 5-tier hierarchy (city / town / village / hamlet / outpost) + ruins, sited on a suitability field with an economy graph | `CivPlanTest`: 300 sites at 100 km, deterministic, spacing + hierarchy ratios hold; plan bake < 10 s Release |
| T3 | **Roads** with a realistic grade cap, bounded earthworks (cut/fill depth caps, retaining walls, embankment slopes), bridges/fords/tunnels/switchbacks, trunk sharing and junction merging; walkable end to end | `RouteWalkTest` (TraversalProbe along every route): 0 unclimbable steps on canonical AND mountain seeds (today 0.69 %) |
| T4 | **Cities** of 200+ buildings with districts, free-form street graphs that follow terrain, polygonal parcels, routed (non-rectangular) walls, markets/squares/civic precincts; every building reachable, every interior walkable | `SettlementMapTest` + existing L3 walkability gate made BLOCKING |
| T4a | City scale is **render-gated**: the lot count a city ships with is whatever the perf program's city ladder (C-25…C-100, `PerfProgram2026-09.md` §16) sustains at its stated frame-time target on both GPUs; the generator must be able to produce 200+ but ships the measured number | `CityBench_*` ladder re-run on every M5 increment |
| T5 | **First-visit realization**: a settlement exists because the player streamed into it, not because a job was run; identical in editor and standalone (streaming already is — the realizer host is what is missing) | `LazyRealizeTest` + packaged-game L4 (`--test` API gains placed-object listing) |
| T6 | **Horizon**: roads, bridges, rivers and settlement massing visible on far-terrain tiles to 4096 u today and to the macro-sheet horizon after M7 | pixel-diff A/B at a fixed pose, far tile vs resident chunk silhouette |
| T7 | **Water-ready**: every river/lake/coast is a shaped basin with bed, banks, rim and spring sites that WaterCore can fill with zero rim leaks | `BasinRimTest` on the generator's own output: no rim column below its basin's real spill (the shape form of `water-must-be-tied-to-terrain`; D17: the water runtime is not the test) |
| T8 | **Cost**: column sampling ≤ 25 noise evals; streaming ≥ 2× today's chunks/s in forest, measured on a named GPU | `WorldGenBench` red/green numbers in `docs/evidence/` |

---

## 3. Architecture — seven layers, each a pure function over a paged coarse sheet

```
L0  WORLD SHEET      macro tiles 512 u (continent: uplift → stream-power erosion → drainage,
                     climate, rock strength, sediment)  +  meso tiles 32 u (paged on demand:
                     macro + noise + local diffusion, tile+apron, deterministic per tile)
      │  heightAt / drainageAt / climateAt / rockAt   (bicubic across tiles — tiles bound COST only)
L1  HYDRO GEOMETRY   river centrelines (splines), width/depth/banks/floodplain from drainage area,
                     lake basins + rims + shelves, coastline + beach/shelf profile, deltas,
                     spring sites with discharge, underground water BODIES (shape only)
      │  bedHeight / channelAt / basinAt / springSites   ──────────►  WaterCore fills (§4)
L2  CIV PLAN         suitability + resource maps → sites in 5 tiers + ruins; spacing; a short
                     growth sim → history ledger; provinces; economy edges
      │  sites[] (pos, tier, seed, era, province, resources, history)
L3  ROUTE FIELD      anisotropic cost-field shortest paths (Galin) on macro, refined on meso with
                     segment masks; reuse discount; hierarchy merge; bridges/fords/tunnels/
                     switchbacks as edges; bounded earthworks  →  ColumnSample.road* (pure)
      │  roadAt / bridgeAt / earthworkAt
L4  SETTLEMENT MAP   per site, headless, no voxels: interest-map growth (Emilien) on the real
                     meso heightfield → street graph (free-form polylines) → parcels (anisotropic
                     conquest) → districts/rings with ground treatment → walls as routed hull →
                     an OSM-like VECTOR MAP (ways + areas + tags) in the ledger
      │  map.json (nodes, ways{highway,bridge,barrier,…}, areas{parcel,lot,landuse,…})
L5  REALIZATION      generation-time: footprint density modifier (beard/terrace) in sampleColumn;
                     first-stream-in: lots realized by the existing structure forge, streets by
                     StreetPaver, walls by TownWall — per-lot ledger, idempotent, standalone-identical
L6  LIVED-IN         residents (exists), economy edges from L2/L3, LLM authors names/owners/lore/
                     signs from the ledger (specs and text only — never geometry)
```

**Rules every layer obeys** (pinned by the chunked-vs-whole test shape from `FeatureDesignKeys.md`):

1. Output is a deterministic function of (world position, seed, recipe, and the *already-baked*
   coarser layer). A layer never reads a finer layer or a neighbour's *result*.
2. Tiles (macro, meso) and chunks bound cost only. Any field read across a tile edge is
   continuous (bicubic with apron) and pinned by `*SeamTest` (single tile vs whole-region equality).
3. Everything persisted that is not re-derivable is in the recipe (`world_meta`) or the ledger.
   Baked sheets are a **cache** keyed by recipe hash, never a source of truth.
4. Failure is loud and bounded: a layer refuses with a reason and a location; it never silently
   falls back to a lookalike (the M0 red tests enforce this on today's silent paths).
5. No universal water level anywhere in this stack. Sea is one body with one level that *WaterCore*
   owns; this plan only shapes the shelf and the coast against the level the recipe names.

### 3.1 Decisions (D1–D9) — **D1, D4, D6 SETTLED by the owner 2026-10-14 (all "yes")**

Settled: **D1** store the continent once with erosion as a paged coarse sheet; **D4** design each
settlement as a free-form vector map first; **D6** build settlements on first stream-in with a
per-frame budget, identically in packaged games. D2, D3 and D7 follow from D1 (erosion on the
sheet, hydrology at sheet resolution, a grounded grade cap); D5 follows from D4 (buildings stay
axis-aligned while streets/parcels go free-form). **D9 SETTLED (yes):** a second era is built early,
during M5, at hamlet size with one typology, as a deliberate test of the era switch.
**D8 SETTLED with a nuance (owner, 2026-10-14):** *"we want most of everything to be procedural and
deterministic, but I like the idea of LLM passes when desired to add flavor"* — so the rule is:
the deterministic pipeline always produces a complete, shippable result on its own; an LLM
**refinement pass** is an opt-in layer that may propose design-level choices (ring layout pattern,
palette, place type, names, owners, histories, sign text) as **validated data** that re-enters the
pipeline through its normal spec inputs (a critic/schema check gates it, as in EthosLM's
agent-decides / code-builds split); it never emits geometry and never becomes a dependency. M8
widens accordingly from "lore only" to "flavor passes", still last. **Water merge ruling (owner):**
`feat/water-rethink` is merged into `main` by the owner when ready, and **M1 starts after that**;
M0 proceeds now, with its two generator-side items (bounded road earthworks, creek ford) done last
inside M0 so they land on the merged generator. The milestone order in §5 stands as written.

| # | Decision | Why | Alternative rejected |
|---|---|---|---|
| D1 | **Layer-0 becomes a stored, paged coarse sheet** (macro 512 u global + meso 32 u tiles), not an analytic source | enables erosion, kills the 32 km box, cuts column cost ~6×, lets far terrain and water read the same grid | keep analytic noise + add erosion "look" noise (fakes valleys that disagree with drainage) |
| D2 | **Erosion is stream-power on the macro sheet** (Cordonnier 2016 / Veloren port; analytic Tzathas 2024 if iteration cost bites), thermal + diffusion on meso | the published way to get dendritic relief *consistent with* the river network | offline Gaea/Houdini (not deterministic from a seed, not streaming) |
| D3 | **Hydrology moves to L1 at meso resolution (32 u) with macro drainage as boundary condition**; `HydrologyMap`/`FlowField` APIs kept as adapters | lake levels sampled at terrain resolution → the 544² shoreline flood and the runtime snap retire; WaterCore's bake-time settle runs on the same grid | keep 128 u + more snapping |
| D4 | **Settlements are a vector map first** (OSM-like ways/areas/tags) and voxels second | headless, testable without a world; realizer becomes a port target for arnis-grade road/bridge realization; real towns can be piped through the realizer as a realism benchmark | keep rect-site planners and add curvature |
| D5 | **Buildings stay axis-aligned on the voxel grid (90° rotations); streets and parcels are free-form** | the structure forge, MicroCanvas and every validator are axis-aligned; organic streets + polygonal parcels + yard fill give the organic read (Minecraft villages, Veloren) | rotated buildings (needs a rotated canvas, breaks every placer) |
| D6 | **Lazy realization replaces the orchestrated bake job** (WorldForge V1 key decision 1 retired); the job survives as "pre-realize this region" | T5; standalone parity; unbounded exploration can have towns | keep the job and add a second path for standalone |
| D7 | **Route grade cap and earthwork bounds are grounded numbers** (NEEDS-GROUNDING, §10) replacing 0.5 | 27° roads are not roads | keep 0.5 and rely on TraversalProbe |
| D8 | **LLM authors specs, parameters and lore; never geometry** (EthosLM's split, the WorldGen / Actor-Critic consensus) | determinism, auditability, provenance rule | LLM-placed buildings |
| D9 | **A second era is built as a falsification of the era hook at M5**, not later | one instance of an abstraction is an assumption (`settlement-pipeline-analysis` #3) | defer eras until "needed" |
| D10 | **Existing worlds keep their terrain: recipe `version: 1` = the analytic Layer-0 (today's `CoarseWorldModel` + `reliefAt`) frozen as a pinned code path; only recipes written at `version: 2` get the sheet** (owner, 2026-10-15, on the M1 design check) | a world.db's stored chunks must never disagree with the chunks streamed next to them (Ravenmere is the live case); the persistence rule says a global change may not silently alter an existing world | refuse version-1 worlds until regenerated (rebuilds the owner's verified town on new terrain) |
| D11 | **One branch for everything planned in this session: `feat/worldforge-v2`** (owner, 2026-10-15) — M1 does not get its own branch | the owner's call on branch policy | `feat/world-sheet` off the merged branch |
| D13 | **The §9 streaming gate is re-scoped (owner, 2026-10-15: "totally agree, we need to be able to measure these type of things"):** chunks/s on the baseline pose is a REPORTED number, not an M1 gate; the per-chunk worker cost (`generateChunk` 138 ms + flora 42 ms, measured) gets its own perf milestone with its own red-before-green profile; the recorder's `streaming.{worker_gen_ms, worker_decorate_ms, finalize_ms}` fields and the `Dirty Remesh` scope are the standing instrument | a gate must measure the cost it names; this one did not | keep "≥ 2× chunks/s" on M1 |
| D14 | **The R0 post-teleport worker crash is chased and fixed before M2** (owner: "we probably want to chase and fix a crash if there is one") | a crash that fires 2 of 5 teleports blocks every L4 run | park it as a water-side note |
| D15 | **M2 drainage is the HYBRID (owner, 2026-10-20: "go with your recommendation"):** the macro erosion bake (M1c receivers, areas, filled surface, kept on the `MacroSheet`) is the global boundary condition and every lake's spill height; each meso tile routes its own flow with a derived apron and takes inflow at its edges from the macro graph. The seam invariant is CONTINUITY (a channel leaving tile A at an edge cell enters tile B at the matching cell with order ≥ the exiting order; a basin's spill is identical on both sides), not tile-equals-whole-region | per-tile cost and memory on the laptop; lake levels continuous across tiles by construction | a full 32 u meso bake per recipe (~10 M cells, ~130 MB resident) |
| D16 | **M2 may change how rivers LOOK on version-2 worlds** (owner, 2026-10-20: "yes"): spline centrelines, banks from drainage, smoothed confluences replace the meander-warped cell segments; version-1 worlds keep today's carve untouched (D10), so Ravenmere and the water tests do not move (D12). Read by the agent as "yes, change it"; if the owner meant "keep the old carve as a v2 option too", add a recipe switch | the old carve's right-angle junctions and 55 u warp are what M2 exists to replace | keep the cell-segment carve on v2 |
| D17 | **WorldForge makes water fixture SHAPES only and never populates a feature with water** (owner, 2026-10-21: "i dont care how you handle water for now, i really prefer you to just make the water fixture shapes and not populate any feature with actual water. the engine water system is still being realized and is not ready to incorporate into anything else yet"). Basins (rim + floor + real spill height), channel beds, banks, floodplains, shelves, coasts and spring SITES are terrain geometry + data handed over through §4's adapters; no WorldForge milestone edits the water runtime, binds through it, or uses `water_validate` / water captures as its acceptance test until the owner says the water core is ready. The M2c "lake shrink" question is therefore not a water question: the basin SHAPE is what we deliver, and its real spill (the rim minimum) is a shape fact | the water core is a moving target; WorldForge verifies its own geometry (generator-side tests such as `BasinRimTest`, captures of the dry shapes) | wiring water now, or shaping basins to a runtime that will change |
| D12 | **The terrain-pinned water tests are NOT re-baselined now** (owner, 2026-10-15: "dont worry about the other water stuff for now") — with D10 they stay on the version-1 path and do not move; revisit only if a water fixture is ever pointed at a version-2 recipe | — | re-record water numbers under the sheet |

---

## 4. The water boundary — exactly what this plan hands to WaterCore

WaterCore §25.3 makes the generator's bake "an initial condition and a source placer, no longer an
authority"; §25.4 places springs "where the terrain gathers water … with the rate the hydrology
bake's flow accumulation gives"; §25.21–25.22 chose option (a): water flows on the generator's
**continuous** `bedHeight`, creek beds carved to the 1/3 m grid by rounding down. This plan keeps
every one of those contracts and widens what the terrain offers:

| Provided by this plan (shape) | Field / query | Consumer on the water side |
|---|---|---|
| Continuous ground height after every terrain term, river carve as continuous depth | `ColumnSample::bedHeight` (exists on the branch) | bed rule `generator` (25.22) |
| Channel presence, order, width, depth, distance to centreline, bank slope, floodplain flag | `channelAt(x,z)` → `{order, halfWidth, depth, dist, bankSlope, floodplain}` | river pins, creek band, Manning bed |
| Basin id + spill level + rim polygon at meso resolution (lakes, sea, underground bodies) | `basinAt(x,z)` → `{id, spillY, kind}` | Priority-Flood + Fill-Spill-Merge settle inside the bake (25.7, F3), body records |
| Spring sites with discharge (catchment × runoff), kind (creek head, lake feed, karst) | `springSites(region)` → `[{pos, rate, kind, channelId}]` | `water_sources` table (25.3) |
| Drainage area and flow direction per meso cell | `drainageAt`, `flowDirAt` | discharge, water table (25.8) |
| Coastline distance, shelf depth profile, beach band | `coastAt(x,z)` | sea body boundary, far ocean tiles |
| Water-table depth hint (subdued replica of topography, Haitjema 2005 — WaterCore 25.8) | `waterTableHintAt` | underground drainage limit |

**Never done here:** placing water, levels as rendering, a universal level, deciding what is wet.
A column whose ground stands above a basin's spill reports a basin id and is still *dry* until
WaterCore fills it.

**Merge sequencing.** The branch's generator-side delta is small (104 lines across
`WorldGenerator.{h,cpp}`, `WorldRecipe.h`, `HydrologyMap.cpp`). **M1 starts from a tree that
contains those commits** (merge or cherry-pick the `bedHeight` / `creekCarve` / `inCreekBand`
contract first) so both sides share one `ColumnSample`. M2 keeps `HydrologyMap::filledAt`,
`waterLevelAt`, `orderAt`, `accumAt` and `FlowField::channelAt` as thin adapters over the meso
sheet for as long as WaterCore calls them; removal is WaterCore's call.

---

## 5. Milestones

Each milestone names: **contract** (what "works" means) · **validation layer required** (L1–L4) ·
**red test** (shown failing first) · **stress axis** · **test world** · **perf gate**. Order is
dependency order; nothing later starts on a prototype of something earlier. Sizes: S ≤ 1 week,
M 2–3 weeks, L 4+ weeks, for one agent-session cadence.

### M0 — Cleanup + doc consolidation (S) — *stops the bleeding, no new design*

> **Design-check 2026-10-14: NEEDS WORK** (14 of 19 rows READY). Unresolved, each with its fix:
> (1) the two generator rows (bounded earthworks, creek ford) wait on F4 grounded caps + F8 water
> merge and must sit behind `WorldForgeParams.version = 2` (legacy recipes keep today's drape —
> pristine chunks are never saved, so an unversioned generator change rewrites every existing
> world's unvisited roads; the `creekCarve` flag is the precedent) with the seam test
> `RoadFieldSeamTest.EarthworkFieldsIdenticalPerChunkAndWholeRegion` named now; (2) forge-side
> residue: reorder `StructureForge` so the read-only gates (grounding, vegetation presence) run
> before `stageFootprint` mutates (`StructureForge.cpp:439-487` precedes `:571-645` today) — a
> reorder, not a rollback; (3) settlement-side residue: street grading stays before buildings,
> but **spurs, fences and yard props move after the building unit**, skip `lotFailures`, and use
> the realized door from `recordDoor` (today's anchor is the footprint centre, `:462`); (4)
> blocking walkability gate = result `success:false, error:"unwalkable"`, nav/residents skipped,
> WorldForge site `refused` and NOT checkpointed (retried after a generator fix), no teardown —
> needs the owner's yes; (5) BlockSmith removal needs a local `.voxel` metadata parser specified
> first (`phyxel_mcp_server.py:7110-7148` imports BlockSmith's). API additions: `allow_unwalkable`
> (bool, default false, echoed as `walkability.gate`), `terrain_height?source=chunks|generator`
> (default chunks, echoed), four pinned tests updated in the same commit (listed in the check).
> Full report in the session transcript 2026-10-14.
> **Re-check after fold-in (same day): READY**, with two conditions carried as rows: the earthworks
> and ford rows cannot start until F4 (grounded caps) and F8 (water merge) are green; every other
> row is buildable now. Items (2)–(5) are folded into their rows above; (4) was settled by the
> owner ("mark it failed, keep the voxels, retry later").

Contract: today's code-level immersion breaks are each pinned red → green; the docs state one
roadmap.

| Fix | Red test |
|---|---|
| `analyzeSite` receives the live water/channel predicate (creek + lake bed = `Water`) | **L2 GREEN 2026-10-14** (`m0_water_red.txt` → `m0_water_green.txt`): `SettlementTerrainPlanTest.CreekInsideFootprintIsNotBuildable` with a dry control (red: a building planned over the creek band x 7..8, 0 plots dropped); predicate = the column's persisted chunk water spans OR the generator's `channelHitAt` / `waterSpanAt` (bake-guarded); 63/63 neighbour suites. Not yet audited. |
| Site prep reordered, not rolled back: clearing + terrace + street grading stay before buildings (buildings re-sample graded ground, `:507-509`); **spurs, fences and yard props move after the building unit**, skip lots in `lotFailures`, and anchor spurs on the realized door from `recordDoor` instead of the footprint centre (`:462`). A refused lot keeps only its cleared terrace, which the ledger records as an empty lot | **L2 GREEN 2026-10-14** (`m0_siteprep_red.txt` → `m0_siteprep_green.txt`): `SettlementBuildServiceTest.LotDressingRunsAfterTheBuildingsForEveryMorphology` (red: hamlet paths/fences at units 27/28 vs last building 53; village had no 'door spurs' unit), `FailedPlotsMapLotFailuresToPlotIndices`, `StreetPaverTest.DoorSpursMeetTheStampedStreetAndNeverRepaveIt` (new `planDoorSpurs`); 84/84 neighbour suites. Built: the paving applier lifted into `applyPavingPlan`, a post-building `door spurs` unit from the recorded door markers, cluster paths from realized doors, fences/yard props after the buildings skipping `failedPlots`. The cell-census test (`RefusedLotLeavesNoFurniture`) needs a refusing lot and is the StructGenTest L4 at the end of M0 (`keep_vegetation` on a treed lot forces the refusal). Not yet audited. |
| Forge gates run read-only before any mutation. Stage order today is intake → floorplan → validate_program → validate_assets → **footprint (removes overlapping structures + placed trees, `:439-487`)** → realize → validate_realized → place (grounding `:571-589`, vegetation `:594-645`) → furnish → emit (`StructureForge.cpp:157-176`). So a `realize` failure, a `validate_realized` traversal refusal AND the grounding/vegetation refusals all leave the removals behind (F5 correction: `validate_realized` is NOT pre-mutation). Fix: the grounding scan and the vegetation-presence check become read-only pre-checks before `footprint`, and the object/tree removal moves after `validate_realized`, immediately before `place` | **L2 GREEN 2026-10-14** (`m0_forgegate_red.txt` → `m0_forgegate_green.txt`): `StructureGroundingTest.ARefusalLeavesNoRemovalsBehind` (red: an ungrounded lot overlapping a registered neighbour refused at `place` with the neighbour already deleted); built as a new read-only `ground_gate` stage before `realize` and `footprint` moved after `validate_realized`; 37/37 structure/harness/settlement suites. The traversal-refusal and keep_vegetation variants share the same stage order and are covered by the order, not by separate rigs. Not yet audited. |
| Street sweep protects cells owned by placed objects | `SettlementBuildServiceTest.SweepNeverErasesPlacedObjectCells` |
| Road earthworks bounded: max cut depth / fill height (NEEDS-GROUNDING, F4), bank at angle of repose, retaining wall beyond (subcube-coursed, not a full-cube cliff). **Gated by `WorldForgeParams.version = 2`** (default for new recipes; version 1 keeps today's unbounded drape so existing worlds' unvisited roads do not change — pristine chunks are not saved by eviction or `save_world`, BUT `saveAllChunks` writes every resident chunk with no dirty test (`ChunkStreamingManager.cpp:549-560`) and the WorldForge checkpoint and `save_world {all:true}` call it, so resident pristine neighbours of a built site are frozen at checkpoint; never-resident chunks still regenerate). New `ColumnSample.earthwork{cutY, fillY, retaining}` fields; the flora gate widens by the earthwork band. Lands after the water merge (F8) | `RoadFieldSeamTest.EarthworkDepthBounded` on the mountain seed; `RoadFieldSeamTest.EarthworkFieldsIdenticalPerChunkAndWholeRegion`; `WorldForgeRecipeTest.LegacyVersionKeepsDrape` |
| All arriving roads register as street spurs, not just the first | `WorldForgeBuildFlowTest.SecondRoadGetsASpur` |
| Walkability gate is BLOCKING with `allow_unwalkable:true` override (T-27/G-61), **and runs for every morphology** (F7 live baseline: the gate unit ran only for the three main_street sites; the five cluster hamlets were never gated). **Semantics settled by the owner 2026-10-14: mark failed, keep the voxels, retry later** — the build's result is `success:false, error:"unwalkable"` with the route report; the nav-rebuild and resident units are skipped; under WorldForge the site is recorded `refused` and NOT checkpointed, so the next run rebuilds it after the generator is fixed; no teardown (the blocked town stays in memory until restream) | **L2 GREEN 2026-10-14** (`m0_gate_red.txt` → `m0_gate_green.txt`): `SettlementBuildServiceTest.TheWalkabilityGateRunsForEveryMorphology` (red: hamlet had no gate unit), `SettlementBuildServiceTest.GateOutcomeBlocksUnlessOverridden` (pure verdict), `WorldForgeBuildFlowTest.UnwalkableSiteIsRefusedNotCheckpointed` (red: pinched site built + checkpointed); 95/95 neighbour suites. The planned `GateRefusesPinchedTown` on real pinched voxels is the L4 on the baseline world at the end of M0 (prediction: sites 0 and 5 come back `refused`, hamlets log a gate line). Not yet audited. |
| Road raster cap logs and refuses; `room_program.json` failure is loud | **L2 GREEN 2026-10-14** (`m0_raster_red.txt` → `m0_raster_green.txt`): `WorldForgePlanTest.RasterCapSurfaces` (red: the plan carried no truncation record; a test-only cap hook lowers 0xFFFE) — the bake counts every segment past the cap, WARNs once, and records `raster_truncated_segments` (0 when none) in the plan JSON; the room-program refusal shipped with row 6. 52/52 WorldForge suites. Not yet audited. |
| Terrace/fence/path materials from the biome + program, not literals | **L2 GREEN 2026-10-14** (`m0_materials_red.txt` → `m0_materials_green.txt`): `SettlementTerrainPlanTest.TerraceFenceAndPathMaterialsComeFromBiomeAndProgram` (red: the plan recorded no materials; literals Dirt/Grass/Log/Cobblestone in three units) — terrace top/fill = the site biome's surface/subsurface, fences = tier `fences.material` (new data field, medieval = Log), cluster paths = tier `street.material`; all echoed in the plan record; 83/84 neighbour suites (the 1 failure is the next row's red). Not yet audited. |
| Parcel canopy margin derived from the biome's flora pool (max template half-width from `# bounds:`, §10 F4b: enchanted oak ≈ 11.5, megaflora ≤ 26 vs the literal 8 at `SettlementBuildService.cpp:1351`), megaflora by an explicit exclusion radius | **L2 GREEN 2026-10-14** (`m0_canopy_red.txt` → `m0_canopy_green.txt`): `SettlementTerrainPlanTest.CanopyMarginCoversTheBiomesWidestTree` (red: the plan echoed no margin; literals 4/8 in two units) — `ObjectTemplateManager::templateHalfFootprintColumns(name)` (per template, the same radius the flora margin grows from), `SettlementBuildService::canopyMarginCubes` (pure), parcel margin = widest crown half-width of the site biome's pool + layers (floor 4; procedural entries bounded by the library's widest crown), sweep = 2× (≥ 8), both echoed as `parcel_clear_margin` / `canopy_sweep_margin` / `canopy_margin_source`; 48/48 neighbour suites. Not yet audited. |
| Order 1–2 creeks under roads get a culvert/ford stamp (plank deck over, bed kept, subcube rail per the bridge pattern). Same `version = 2` gate and seam test as the earthworks row; lands after the water merge (F8) | `RoadFieldSeamTest.CreekUnderRoadIsFordNotNotch` (L3: `TraversalProbe` walks the road across the creek, 0 unclimbable steps) |
| Settlement passes `period` to every building; `resources/room_programs/` created with the medieval pack as its first entry; both ignored `room_program.json` load results become refusals | **L2 GREEN 2026-10-14** (`m0_period_red.txt` → `m0_period_green.txt`): `SettlementBuildServiceTest.BuildingsCarryTheEraPeriod` (red: no queued build carried a period), `MissingRoomProgramRefuses`, `StructureGroundingTest.MissingRoomProgramRefusesTheBuild` (path overridable via `room_program`; both ignored loads now refuse naming the file; a non-medieval era without `resources/room_programs/<era>.json` refuses in the planner AND the forge); `resources/room_programs/README.md` documents the pack layout (medieval stays in the legacy file, so no duplicate data); 51/51 neighbour suites. Not yet audited. |
| Placement into un-generated terrain refuses. Mechanism (F5-corrected): `place()` calls `ensureChunkAt` first (`StructureGenerator.cpp:828`), which LOADS a saved chunk or else **creates an EMPTY chunk** (`ChunkManager.cpp:390-415`, `createChunk(origin, populate=false)`); the `failed++` null branch (`:830`) fires only when a saved chunk fails to load. The live hazard is therefore not dropped voxels but a building materialized into a void chunk that the streamer then never generates terrain for (the "structures on thin air" incident). Fix: before placing, every target chunk must be resident AND generated (or saved); otherwise refuse with the chunk list. `StructureBuildService` also refuses on `failed > 0` (today only `placed == 0`, `:113`) | **L2 GREEN 2026-10-14** (`m0_ungenerated_red.txt` → `m0_ungenerated_green.txt`): `StructureGroundingTest.PlacementIntoUngeneratedTerrainIsRefused` — streaming Perlin generator, resident pad at y=28, croft roof reaching non-resident chunk y=1 where the generator has hill terrain (red: built, empty chunk materialized); non-streaming control still builds. Rule: before `place`, every target chunk must be resident or saved, or (streaming) the generator must leave it as air under the structure's columns; `failed > 0` now refuses too; 46/46 neighbour suites. Not yet audited. |
| Refusal leaves no site-prep residue: `stageFootprint`'s object/tree removal is deferred until every gate has passed, or snapshotted and restored | `StructureBuildServiceTest.RefusalIsClean` (cell + object census before/after) |
| One core factory producing BOTH `SettlementBuildService::Deps` (two fill sites) and `StructureBuildService::Deps` (two fill sites) | **L2 GREEN 2026-10-14** (`m0_deps_focus_red.txt` → `m0_deps_focus_green.txt`): `WorldForgeBuildFlowTest.StructureDepsAreDerivedFromSettlementDepsCompletely` (contract) — `SettlementBuildService::Deps::structureDeps()` is the ONE settlement→structure mapping (the building unit's hand copy replaced); the editor's `Application::makeSettlementBuildDeps()` is the one host factory (build_settlement, worldforge_build, build_structure via `.structureDeps()`; the worldforge copy had lost keepClear); the loader derives its building deps the same way and gains keepClear from its triggers (it has no item props/undo/lights — host services it lacks, stated in code). 82/82 neighbour suites. Not yet audited. |
| `worldforge_build` owns a `shared_ptr<const WorldForgePlan>`; raised load radii restored by RAII on any exit | **L2 GREEN 2026-10-14**: `WorldForgeBuildFlowTest.FocusIsReleasedWhenTheJobIsCancelled` (red: cancel → 0 releases) — `MainThreadJobs::setCancelHook` runs on the next MAIN-THREAD tick (cancel may come from the HTTP thread), `BuildState::releaseFocusOnce` guards completion + cancel; `WorldForgeBuildService::Deps::plan` is now `shared_ptr<const WorldForgePlan>` (`WorldGenerator::worldForgeShared()`), no raw pointer held for a job's lifetime. The radii themselves are restored inside the editor's `releaseFocus` lambda, so a release implies restoration. Not yet audited. |
| One truth for "surface": `terrain_height` gains `source:"generator"` and the resident-chunk scan is labelled | **BUILT 2026-10-14** (editor handler + route + MCP tool param; responses echo `source`, the chunk scan also echoes `max_y`); L4 check at the end of M0: the town centre read 127 (chunks) vs 136 (generator) in F7 |
| MCP mirrors for `build_settlement`, `world/validate`, `walk/probe`, `navgrid/navgraph`, `water_validate`; stale V1 text in `worldforge_*` descriptions removed | **BUILT 2026-10-14**: six new tools (`build_settlement`, `validate_world`, `walk_probe`, `navgrid_path`, `navgraph_path`, `water_validate`), `get_terrain_height` gains `source`, the 3-8 / 512-8192 / 128u / restart_required texts corrected; `py_compile` clean. Live MCP round-trip owed at the end-of-M0 L4 |
| Owner action: wire `solution-audit-gate.sh` as a Stop hook in `.claude/settings.json` — **DONE 2026-10-14** (local file, tested: blocks an unaudited "works", allows a VERDICT) | the gate blocks a turn ending on an unaudited "fixed" |
| **Remove BlockSmith** (owner, 2026-10-14: "not adequate for our uses"). Inventory of what goes: the `external/blocksmith` submodule + its `.gitmodules` entry; `tools/blocksmith_generate.py`, `tools/building_prompts.py`, `tools/asset_pipeline/prompt_enhancer.py`, `tools/generate_furniture_library.py` (all import or shell out to it); the six MCP tools that route to it — `generate_template`, `search_templates`, `list_generated_templates`, `critique_template`, `refine_template`, `generate_asset` (`phyxel_mcp_server.py:1671-2864`) — plus its `_run_blocksmith_native` (`:7234-7276`) and the `PHYXEL_AI_API_KEY → ANTHROPIC_API_KEY` alias (`:6580`); the hidden dependency where the MCP server imports `blocksmith.generators.phyxel_parser.read_voxel_metadata` at THREE sites (`:7124`, `:7370`, `:7647`; the first wrapped in try/except with a manual fallback) plus `sys.path.insert` calls at `:7123`, `:7226`, `:7369`, `:7646` and the subprocess path at `:5398` — all replaced by `scripts/mcp/voxel_meta.py`, a dependency-free reader of the two shipped formats (`# key: value` header lines + `C x y z Mat` / `S` / `M` cube lines, and the fine tier `# grid: N` + `V x y z Mat [tint=]` lines, per `docs/FineVoxelItems.md`), returning the same dict keys `inspect_template` reads today, with a unit test over one template of each format from `resources/templates/`; the skills `.claude/commands/asset.md`, `.claude/commands/generate.md` and `tools/phyxel-gamedev/skills/phyxel-assets/SKILL.md` (rewritten for the Meshy route, §14); docs: the CLAUDE.md "BlockSmith AI Model Generation" section (replaced by a Meshy section), `AssetLibrary.md`, `GameDevWorkflow.md`, `game-production/README.md` + `recipes/farming-survival-loop.md`, `WantedAssetsBacklog.md`, `CharacterLibraryPlan.md`, `ForgePattern.md`, `VoxelAppearanceModel.md`, the structure-generation docs that cite it. History comments in `ItemPropManager.cpp:231` and `ItemPropPhysicsTest.cpp:261` stay (they record a past defect). Also to clean: `README.md`, `.claude/settings.local.json`, `docs/AgentContext.md`, `docs/feedback/archive.md`, `docs/structure-generation/FinishDetailPlan.md`, `docs/structure-generation/StructureGenerationPlacers.md`, `tools/phyxel-gamedev/README.md` (F5 found these seven missing from the list). No shipped template depends on it (`resources/templates/generated/` does not exist; the catalog carries no `source: blocksmith`). | red: `grep -rniI blocksmith --exclude-dir=external --exclude-dir=.git` lists 29 files today. **DONE 2026-10-14:** submodule + `.gitmodules` entry + the four tools + the six MCP tools, their dispatch, `_enhance_prompt_inprocess` / `_run_blocksmith_native` / `_orbit_for_critique` / `_critique_template` / `_generate_asset` and the key alias removed; the imported parser turned out never to have existed in the submodule (the manual fallback was always the real path), so `inspect_template` now uses `scripts/mcp/voxel_meta.py` (self-checked on a micro furniture template and a fine-grid item); `/generate` rewritten for the Meshy route (import tool still planned), `/asset` + the gamedev skill/README re-pointed; CLAUDE.md section replaced by the Meshy section; usage docs re-pointed. **Green criterion as applied:** no file INSTRUCTS running BlockSmith; mentions that remain are the two code history comments and dated retirement notes in history/roadmap docs (AgentContext, ForgePattern, WantedAssetsBacklog, VoxelAppearanceModel, StructurePipelineGaps, plus untouched archive/plan history in CharacterLibraryPlan, feedback/archive, FinishDetailPlan, StructureGenerationPlacers). `.claude/settings.local.json` is an untracked local file (an old allow-rule) — owner's to clean. MCP server `py_compile` clean; `asset_index.py --validate` run at the end of M0. |

Docs: fold `WorldForge.md` "Known gaps" into its resolved sections; fix `WorldModel.md` §2;
retire StructureGenerationV2 P8–P10 with a pointer here; one "next" (this file §5).
**Docs row DONE 2026-10-14** (see §15 F9 for the file list).
Validation: L2 unit + one L4 Release run on the canonical 8-site world (`WorldForgeStressTest`
numbers re-recorded). Stress: the 8-site mountain seed. Perf gate: none (no new cost).

**M0 L4 run 1 (2026-10-14, Release, `tools/worldforge_m0_l4.sh`, `docs/evidence/worldforge_v2/m0_l4_summary.json`):**
6 of 8 sites REFUSED by the now-blocking gate (town, village 6, hamlets 3/4/7: a 1-micro street→door
pinch at longhouse / blacksmith / hall_house doors — the generator defect F7 shipped twice; hamlet 1:
no plots once its creek columns were unbuildable), 2 BUILT (village 5 with 2 of its 4 F7 lots dropped as
water, hamlet 2). The run SURFACED two M0 defects, both fixed as **row 2b** the same day:
(a) **door-spur count** — the town logged "2 planned from 5 realized doors, 3 too steep" (F7: 8 ok /
0, on a different lot set: 8 parcels then, 5 now). **My first hypothesis (the reordered spur unit starting
at the forge's MARKER, one cell short for the grade tube) was WRONG for these lots** — run 2's loud
numbers show the doorstep ground 18 micro below the street edge 5 micro away, which no spur start can
bridge; the count is unchanged in runs 2–4. What row 2b did deliver: `SettlementBuildService::doorApproach`
(ONE door-finding shared by the spur unit, the cluster path network and the gate; spurs start outside the
FOUND door, not the marker, which also fixes the regen-#14 marker-off-the-door case) and `paths.spur_failures`
with door/street/run/drop/cap/reason. The "pad 2 cubes below the street" reading of those numbers is a
HYPOTHESIS (not A/B'd, not L3-tested — owed, see auditor). Red→green:
`SettlementBuildServiceTest.DoorApproachFindsTheDoorAlongTheMarkersWall`,
`StreetPaverTest.DoorSpurFromATerracePadIsReportedWithItsNumbers` (`m0_spur_red.txt` → `m0_spur_green.txt`).
(b) **plan-hash drift** — row 10's `raster_truncated_segments` was hashed, so the lab's F7 hash
9693137997320541182 came back 10638243061351388589 on an unchanged recipe (every pre-M0 ledger would read
stale). Fix: `planHash` hashes identity only; `WorldForgePlanTest.RasterCapSurfaces` pins it (same red/green files).
Not M0's to fix, now visible: the door pinch itself (next generator task); creek-crossing siting (M3).
**Run 2 (`m0_l4b_summary.json`, predictions in `m0_l4b_predictions.json`):** F7 hash back
(9693137997320541182); **5 built / 3 refused** (hamlets 3/4/7 now build — PLAUSIBLY because the path network now meets the
FOUND door's outside cell (the only hamlet-path change), but no marker-vs-door A/B was run, so this is not isolated; town and village 6 still refused on a
hall_house / blacksmith pinch; hamlet 1 still plot-less). The town's three spurs still fail, now loudly:
`door y1296 -> street y1314, 6 cells, drop 18 micro, cap 2` — the doorstep is **2 cubes below the street
edge** (a terrace-pad / street-grade mismatch, upstream of the spur; logged as a generator finding, M5
realization). Those three lots had PASSED the gate: the probe free-falls without limit and `checkRoutes`
asked only start→goal, so a one-way cliff counted as walkable — **row 2c**: `checkRoutes` now requires
the way back (`RouteResult.oneWay`, `one_way` in the gate JSON), red→green
`SettlementWalkabilityTest.AOneWayDropIsNotWalkable` (`m0_bothways_red.txt` → `m0_bothways_green.txt`).
`worldforge_status` now carries `site_list` (the applied plan's sites with x/z/tier/surfaceY) so a probe
can aim at a built site. **Run 3 (`m0_l4c_summary.json`):** hash back; **4 built / 4 refused**: the general_store's one-way
drop is now caught (town 2 blocked of 14), but hamlet 7 was refused on `door -> solar … GOAL-UNSUPPORTED,
ONE-WAY` — the return walk started at the route's exact `to` (a room centre under a fixture, which does
not settle), a FALSE refusal from probe input. **Row 2c-2:** `reachable()` reports the arrival cell and
the way back starts there (`SettlementWalkabilityTest.TheWayBackStartsWhereTheWalkerArrived`,
`m0_arrival_red.txt` → `m0_arrival_green.txt`). `terrain_height` chunks vs generator at hamlet 2's centre:
144 vs 144 (the F7 town's 127/136 split was terracing at the centre). Captures still owed (script float
arithmetic, fixed). **Run 4 (`m0_l4d_summary.json`, END-OF-M0 STATE): every prediction held** — hash
9693137997320541182; **5 built / 3 refused** (town: hall_house pinch + general_store one-way drop; village 6:
blacksmith pinch; hamlet 1: creek plots), hamlet 7 builds again; `terrain_height` 144/144 at hamlet 2;
captures pose-verified, and a street-level capture (`tools/worldforge_m0_l4_capture.sh`,
`m0_l4d_site2_street_capture.png`) shows the engine-built hamlet (houses, doors, picket fences). Versus F7:
8/8 "built" with two pinched sites shipped → 5/8 built with every refusal a named generator defect. Owed
after M0: the solution-auditor pass over all rows; the StructGenTest residue L4; then the three generator
defects now visible (door pinch, pad-below-street, creek siting) before/with M3–M5.

**Solution-auditor over the whole M0 batch — RUN 2026-10-15, raw verdict: "VERDICT: FAIL (overall).
The code-level gate, reorder, water, forge-gate and placement rows are real red->green. The M0 write-up
overstates the spur story and one L4 causal claim. Some named rows and tests have no evidence at all.
Nothing was fabricated outright."** Per item: rows 1, 2c/2c-2, 3, 4, 5 CONFIRMED; row 2 partly (only 1 of
3 named tests was seen red; the refused-lot cell census is still owed); row 2b split (code+tests
CONFIRMED, narrative FAIL — corrected above); rows 6–10 CONFIRMED at L2 with weaknesses (canopy/materials
reds are key-presence assertions, the deps test has no seen-red, `terrain_height?source` and the MCP
mirrors are BUILT but unaudited); L4 claims mostly CONFIRMED. **Findings acted on the same day:** (1) the
gate silently SKIPPED a front door whose endpoint did not seat (town tavern: passage cell unseated at the
threshold; "5 front doors, 1 skipped" passed) — **row 2d**: `gateOutcome` fails the build on `skipped > 0`
(`gate:"unseated"`, overridable) — `SettlementBuildServiceTest.GateOutcomeBlocksUnlessOverridden`
(`m0_unseated_red.txt` → `m0_unseated_green.txt`); (2) the spur narrative retracted above; (3) the
hamlet-3/4/7 causal claim downgraded above. **Still owed from the audit:** an L3 test of the town lot
(terrace + street + TraversalProbe doorstep→street) to settle the pad-vs-street hypothesis; a marker-vs-door
A/B on hamlets 3/4/7; the refused-lot cell census (`RefusalIsClean`); world-level canopy-overhang and
material scans for rows 8–9; a live round-trip of the six MCP mirrors; rows named in the table without a
test (`SweepNeverErasesPlacedObjectCells`, `SecondRoadGetsASpur`, earthworks/ford) stay NOT GREEN.

**M0 L4 run 5 + the water merge — 2026-10-15 (`m0_l4e_summary.json`, predictions first).** The owner's
`feat/water-rethink` (WaterCore 26.16) is merged into main (489caee2, pushed) and main into this branch
(70f40163): F8 is done and M1 is unblocked. Merge conflicts were docs/manifest only; every `.spv` rebuilt
(main changed `occupancy.glsl`, which the water and solver shaders include). Run 5 (Release 2e78d1df =
row 2d + the merge): hash 9693137997320541182 held; the town's refusal now reads "unwalkable: 2 of 14
street->door routes blocked, 1 front door(s) unseated (walkability gate)" with `gate.skipped:1` in the job
JSON (the tavern @-1146,-1052 whose passage cell never seated). **Solution-auditor on row 2d (raw): "VERDICT:
FAIL (narrowly). Claim A is confirmed. Claim B's text/JSON plumbing is confirmed, but no L4 run shows the
new rule failing a build on skipped>0 alone."** Row 2d's decision branch (`gate:"unseated"`, blocked==0)
is proven at L2 only (`GateOutcomeBlocksUnlessOverridden`, red→green on the shipped case {13,0,1}); the town
was already blocked, and every built site logged 0 skipped, so the rule never fired alone at L4. Auditor
caveats acted on / recorded: the nav-rebuild WARN omitted the unseated count (fixed, same day); nothing
unit-tests that the probe loop increments `skipped` (only L4 does); `thresholdMicro<0` now counts as a
refusal. **Prediction misses:** (1) hamlet 1 BUILT (2 buildings, 8/8 walkable) where runs 1–4 refused it
"no buildable plots" → ledger 6 built / 2 refused; attributed by code reading + measurement, NOT A/B'd: the
merged generator's lake flood is gated by the bake's Priority-Flood `filled` surface
(`WaterOccupancy.cpp` `floodBodiesOverGrid`, WaterRethink D4), and the saved world measures DRY at the
houses (`water_table_level` wet=false at 5 columns, `water_validate` 3721 columns wet 0 / rim_leaks 0, air
above y135) where run 4's generator flooded the whole 40×32 rect (`terrain analyse buildable=0` → 0.525);
site 3 moved 0.903 → 0.925, sites 2/4/7 identical. Whether a lake belongs in that basin is the owner's
water work (fill), not M0's (shape); the white terraced floor in the captures is `SnowGrass` from the
lapse-rate snow line (measured at 4 columns), pre-existing. (2) duration 355 s vs 280–340. (3) the capture
targeted site 1, not site 2, as a consequence of (1). **Also found this day:** `ForgeStageOrder.
CanonicalStageSequence` had been RED since row 4 (1a2ffe32) — neither row 4 nor the M0 auditor pass ran
that fixture; fixed in 2e78d1df (test + placers doc). The merged main's full Debug suite: 4437 tests, 4415
pass, 19 skip, the 2 pre-existing fails + ONE order-dependent water test (`WaterCouplingTest.BlastBeside
ThePondMovesItAndItSettles`, 0.215 < 0.27 inside the full run, 0.284 alone; not bisected). **The merged
branch's full Debug suite (b15a3144, 2026-10-15): 4458 tests, 4435 pass, 19 skip, 4 fails** — the same 3 [re-run on 521f1ba2 after the M1 work + the cache fix, 2026-10-20, 73.6 min: **4493 tests (= 4458 + the 35 TEST macros the diff b15a3144..521f1ba2 adds, 0 removed), 4471 pass, 19 skip, 3 fails — exactly the two pre-existing (`AtlasManagerTest.BuildAtlasFromSourcePNGs`, `FineFaceMerge.SubcubeMerge_CrossCubeSplitsOnLightBoundaryBetweenCubes`) plus the order-dependent water gate (0.2149 < 0.27 inside the run again); `docs/evidence/worldforge_v2/full_suite_521f1ba2_summary.txt`]
plus `PeriodAxis.AnUnknownPeriodRefusesRatherThanSubstituting`, RED since row 6 (76f2529c): the
missing-pack refusal I added emitted `requested_period` + `room_program_pack` but dropped the
`known_periods` list the test's contract requires ("the refusal must say what IS available"); row 6's
"51/51" did not include that fixture. Fixed the same day (both the forge intake and the settlement-side era
refusal now carry `known_periods`); PeriodAxis + ForgeStageOrder + StructureForge/Build + SettlementBuild +
RoomProgram/BuildingProgram = 54/54 (`m0_period_fix_green.txt`). Second red-since-a-row contract test in two
days — the rule stands: after a row, run every fixture that names the thing changed. **Solution-auditor on
that fix (raw): "VERDICT: PASS for the narrow claim, which is the forge-intake fix and its fixture. The
settlement-side half of the claim has no test, and the test is weak on the content of `known_periods`."**
Both closed the same day: `PeriodAxis` now requires a non-empty list naming `medieval`; the settlement-side
era-without-pack refusal (reachable only with a second era, which shipped data lacks) is driven by
`SettlementBuildServiceTest.AKnownEraWithoutARoomProgramPackIsRefusedNamingThePack` through a new
`{"settlement_program": path}` override (mirrors row 6's `room_program`), red by mutation
(`m0_period_settlement_red.txt`: fails only on the absent `known_periods`) → green 55/55
(`m0_period_settlement_green.txt`). **Still owed,
updated:** the unseated-only L4 proof (a site with skipped>0, blocked==0 — the town once its two blocked
routes are fixed, or a lab); a unit test that drives the probe loop to produce `skipped`; the generator A/B
for hamlet 1 (old vs merged flood on this seed); the plan-time `site_list.surfaceY` vs chunk `surface_y`
gap (151 vs 134 at site 1, 150.66 vs 144 at site 2) — unexplained, for M1/M3; plus everything in the
auditor paragraph above.

### M1 — World Sheet (L) — *the one change that moves the scale ceiling*

Contract: `heightAt / drainageAt / climateAt / rockAt` are served from a **macro sheet** (512 u
cells, whole continent, baked once per recipe) and **meso tiles** (32 u cells, 256 × 256 per tile
= 8 km, paged on demand on the generator workers, LRU like `FarTerrainManager`'s 768-tile cap),
with bicubic interpolation across tile edges. Column sampling reads the sheet instead of
re-evaluating noise: ≤ 25 noise evals per column (today ~140).

- Macro bake: uplift field (recipe-overridable `Spline` + painted mask later) → implicit
  stream-power erosion (m = 0.4, n = 1, Gauss-Seidel, Veloren's `erosion.rs` port of Cordonnier
  2016) → Planchon-Darboux fill → multi-receiver drainage → two layers (bedrock/sediment) → rock
  strength from 3-D noise → per-cell max slope. Climate (temperature, moisture, continentalness)
  computed on the same grid with orographic rain shadow (DF model). Target < 2 s Release for
  200 × 200; measured and recorded.
- Meso tile = f(seed, tile coords, macro sheet): upsample macro → add ridged/warped detail at
  λ ≤ 512 u → thermal erosion + hillslope diffusion inside tile + 16-cell apron → clamp by rock
  max slope. Apron width is a pinned constant; `MesoSeamTest` proves tile-by-tile == whole-region.
- Recipe: `sheet {macroCell:512, mesoCell:32, continentCells:200, erosionSteps, uplift…}`
  (units in field comments; clamps echoed). Baked sheets cached in `world.db` as blobs keyed by
  recipe hash; a cache miss re-bakes; a hash mismatch refuses with the reason.
- `WorldModel.md` alignment: a bounded world is the same sheet with bounds; no streaming flag
  consulted anywhere new. Flat/heightmap worlds get a trivial sheet (so WorldForge no longer
  "has no plan" for them — `WorldForge.md` gap closed).
- Streaming's single-column surface band becomes min/max over the chunk column from the meso
  sheet (closes the latent mountain-flank hole, audit §5).

**Design-check resolutions (gate run 2026-10-15, verdict NEEDS WORK → the items below are the
answers; nothing is built until each is in code or measured):**
1. *Existing worlds* — D10. `WorldRecipe::version` 1 keeps `m_coarse`/`reliefAt` byte-for-byte
   (`CoarseWorldModelTest`, `TerrainReliefTest`, `MapCoarseSourceTest` pin it); version 2 reads the
   sheet. A version-2 recipe on a world.db that already holds chunks refuses with the reason.
2. *Meso sweep* — thermal erosion + diffusion inside a tile are **Jacobi (double-buffered)**, never
   in-place Gauss-Seidel (an in-place sweep leaks the sweep order past the apron). The apron is
   **derived**: `apron = steps × stencilRadius` (stencil radius 1 for both ops), and `erosionSteps`
   is clamped to `apron / stencilRadius` at the recipe boundary with that reason written at the
   clamp. `MesoSeamTest` is bit-exact (not approximate): a 2×2-tile block baked as four tiles in
   random order on different threads == the same block baked as one 4×-tile, every interior cell,
   all four fields, plus bicubic samples straddling the tile edge at sub-cell offsets.
3. *What stays per-column at 1 u* (from `sampleColumn`, `WorldGenerator.cpp:912-1140`): the climate
   domain warp (2 evals) + temperature/moisture (2×4 octaves) move to the macro climate grid (sheet);
   the broad ridged massif + its warp (`reliefAt`) move to the meso tile; **kept per-column**: the
   FINE ridged roughness band (1 eval), the surface-scatter patch noise (`:961`, 1 eval), the
   meander warp of the channel query (2 fbm bands, `:981-983`), the creek carve/band, road
   earthworks, bridge fields. Slope for the rock/snow material override (`:1070-1088`) comes from the
   **bicubic gradient of the meso height plus the per-column fine band's analytic gradient**, replacing
   the 4-neighbour re-sample — a cliff pose before/after is part of the L4 set because the banding
   resolution changes. `ColumnCostBench` counts evals through an instrumented counter; the ~140 in §9
   was an audit estimate, so the bench's red run is also the first real count.
4. *Tests that re-baseline* — none on the version-1 path (D10/D12). New-world fixtures that bake
   seeds at version 2 (`WorldForgeStressTest` seeds 20260816/424242, the M0 evidence hash) are
   re-recorded in the commit that flips their recipe to version 2, with the old numbers kept in the
   evidence JSON.
5. *`HydroEdgeTest` at M1* asserts **macro drainage** continuity across the old ±16384 box edge (a
   receiver chain crosses x = 16384 without a sink); channel continuity is M2's. *Tile origin*: meso
   tile `(tx, tz)` covers `[tx·8192, (tx+1)·8192)` in world units, so seams fall at multiples of 8192;
   the WorldSheetLab seam pose moves to **x = 0** (a real seam, and it crosses the ridge along z = 0)
   — §16 updated.
6. *Sheet hash* = seed, generation type, `sheet{…}`, `heightSpline`, `climateFrequency`, `seaLevelY`.
   It **excludes** `worldforge{…}` (the sheet never reads civ/route params; §13.1's "incl. worldforge
   params" is withdrawn). The hydro memo key (`WorldGenerator.cpp:350`) gains the sheet hash.
7. *Drainage-density band* — grounding-auditor row in §10 before `MacroErosionTest` asserts a number.
8. *Baseline first* — Release chunks/s on the forest pose of WorldForgeBaseline is measured on the
   named laptop BEFORE any M1 code lands (evidence must postdate nothing: the generator is unchanged
   since 2e78d1df, the Release exe on file).
9. *Surface band* — the per-chunk-column min/max is taken over the bicubic surface sampled at
   stride ≤ 8 u inside the chunk plus a margin for bicubic overshoot (Catmull-Rom overshoot ≤ 1/8 of
   the largest neighbouring cell difference — the bound is stated in code); pinned by
   `SurfaceBandBoundsEveryColumn` (every column's true `surfaceY` lies inside its reported band;
   red today at a mountain flank where the centre sample's band differs from a corner's).

**M1a BUILT 2026-10-15 — the meso tile pipeline + its seam invariant.** `engine/{include,src}/core/WorldSheet.*`:
`SheetParams` (clamps with reasons; `apronCells()` DERIVED = steps × stencil radius + bicubic reach),
`MesoTile` (padded height cells addressed by ABSOLUTE cell index), `MesoBaker` (h0 = macro field + detail
at the cell centre, then a fixed number of **Jacobi** thermal-erosion + hillslope-diffusion steps),
`mesoHeightAt` (Catmull-Rom from the absolute cell coordinate), `MesoPager` (shared mutex-guarded LRU).
`MesoSeamTest` 4/4: apron derivation + clamps; four tiles baked in shuffled order on four threads ==
one 2N×2N window, bit-exact, every interior cell; 152 bicubic samples straddling the seam served by the
pager == the window; evict + re-bake bit-identical. **Red first, as predicted by resolution 2:** the
in-place (Gauss-Seidel) sweep tried first differed in 881 of 16384 cells and 74 of 152 seam samples
(`m1_meso_seam_and_column_cost_red.txt`); the double-buffered sweep is green
(`m1_meso_seam_green.txt`). Rig deltas: 64-cell tiles (shipped 256), a synthetic macro field + hash
detail (the invariant is about the pipeline, not the field); the macro sheet, the generator's real
detail noise, the version-2 recipe gate and the `sampleColumn` wiring are M1b. The relaxation
constants (talus = `kRockSlope`, diffusion < 0.25 for stability, thermal rate ≤ 0.5) are stated in
the header; none is yet a grounded geomorphic rate (§10 K/U rows) — they shape, they do not claim.

**M1b BUILT 2026-10-15 — the sheet serves Layer-0 for version-2 recipes (D10).** `MacroSheet`
(continentCells² cells of base elevation + continentalness at 512 u, centred on the origin, bicubic,
edge-clamped; baked from the same continent noise the v1 coarse source evaluates) + the meso tile
gains raw temperature/moisture planes (the v1 climate fbm at the cell centre; the column still applies
its domain warp and the contrast expansion, so biome borders stay ragged). `WorldRecipe::version >= 2`
⇔ `sheet{…}` (`WorldRecipe::sheetToJson/FromJson`, clamped on the way in; a sheet block promotes
the version; legacy JSON byte-identical — `TerrainRecipeTest.SheetParamsRoundTripThroughRecipeJsonOnlyAtVersionTwo`);
game.json `world.sheet` → a version-2 recipe (loader), a stored recipe still wins. In `sampleColumn`
the v2 branch reads climate from the pager at the warped position, base + continentalness from the
macro sheet, relief = max(0, mesoHeight − base) + the per-column FINE ridged band
(`fineReliefAt`; the BROAD band `broadReliefAt` is the tile's detail term, relaxed), and the material
slope from the tile's analytic bicubic gradient; everything after (biome blend, valley shaping, carve,
roads, materials) is shared. `baseHeightFn()` is the one full-surface height the hydrology bake and the
worldforge preview flood (v1 closure unchanged; v2 = meso + fine); `HydroBakeKey` carries the sheet
identity so a v2 world never reuses a v1 hydro memo. The macro sheet + pager are memoized process-wide
by the SHEET key (no worldforge params, resolution 6) so the twice-per-load `applyRecipe` and every
worker copy share one bake and one LRU (`WorldSheetGeneratorTest.GeneratorCopySamplesIdenticalColumnsAcrossATileSeam`:
120 columns straddling x = 8192, original vs copy in opposite orders, bit-exact on every field — this
proves the copy is wired to the shared pager and the v2 path is order-independent; it compares one
pager against itself and CANNOT see a tile-vs-whole defect, which is `MesoSeamTest`'s job;
`VersionTwoRecipeEnablesTheSheetAndVersionOneDoesNot`; `BaseHeightFnReadsTheSheet`). **Column cost
gate GREEN: `ColumnCostBench.FreshColumnCostsAtMost25EvalsOnTheSheet` = 18.6 evals per fresh
column (v1 frozen at 136.6, pinned by `LegacyColumnCostIsFrozen`).** Meso tile bake 256² + apron 6:
201.5 ms **Debug** (the §9 gate is Release, measured at M1e). **Solution-auditor on M1b (raw, 2026-10-15): "VERDICT: FAIL. The 18.6-eval gate and the thread-safety
design are real, but the 'v1 byte-for-byte' and 'bit-identical across the seam' claims rest on code
reading and a self-comparing test; the suite-coverage claim cannot be verified from the evidence
file; and `setGenerationType` after `applyRecipe` leaves a stale sheet."** Closed the same day:
(a) `LegacyTerrainGoldenTest` pins the bit patterns of 2000 scattered columns (surfaceY, bedHeight,
temperature, moisture, continentalness, biomeIndex, surfaceMat) for Perlin and Mountains at seed
4242 as one FNV-1a hash each (Perlin 11413304086647064455, Mountains 5521812291101006622); the file
uses only the v1 API so the SAME file was built at the pre-M1 commit 4961a277 in a worktree — the
"before" run (`m1b_legacy_golden_parent.txt`, 2026-10-15 20:45) produced the SAME two hashes, so the
v1 freeze is MEASURED across M1a–M1d, not read; (b) `MesoSeamTest.ClimateAndGradientStraddlingTheSeamMatchTheWholeRegion`: the climate planes
and the analytic gradient served by a pager from two tiles == one 2N×2N window, bit-exact, with a
non-constant control; (c) `setGenerationType` now rebuilds Layer-0 (coarse model, hydrology, sheet)
— `SetGenerationTypeAfterRecipeRebuildsTheSheet`; it had no callers, the hole was latent and
pre-existing for the coarse model + hydrology too; (d) the terrain/hydrology/worldforge/flora/road/
streaming suites re-run with the exe and the filter written into the evidence header
(`m1b_suites_recorded.txt`). **M1b L4 on the lab (Release f958af2f, `m1b_lab_l4_run.txt`):** the
engine logged `[WORLD_SHEET] macro 16x16 @ 512 u baked … meso tiles 256 @ 32 u, apron 6, LRU 64`,
the loader promoted game.json `world.sheet` to recipe version 2 and world.db persisted it
(`version: 2` + the sheet block read back); the generator surface across the x = 0 tile seam at
z = 40 is 51 for every column from −6 to +6 (flat there — continuity, not a relief test); streaming
at a never-stored pose (3000, 151, −2000) ran the queue saturated and reached 961 resident chunks
in 50 s = **17.3 chunks/s** (frame p50 32.7 / p95 81.1 ms) — a different world and pose from the
11.4 baseline, so NOT the §9 gate measurement (that needs the baseline seed with a sheet block,
M1e); the seam-pose capture `m1b_lab_seam_pose.png` (0, 111, 40) pitch −60 shows continuous ground
across the seam with one chunk still un-meshed at the left edge 60 s after the teleport (not
diagnosed). **Release meso tile bake = 76.2 ms** (`phyxel_tests` Release, laptop) — **RED against
the §9 < 50 ms gate**; the bake is 268² cells × (warp 4 + broad 6 + climate 8 evals): the climate
planes alone are 8 of 18 evals per cell, the first lever. Deviation from the plan text,
with reason: climate lives on the MESO grid (32 u), not the macro grid — temperature/moisture at
climateFrequency 0.002 have a ~500 u wavelength, which a 512 u cell grid would alias into biome mush;
the macro orographic model comes with erosion (M1c). Not yet: erosion/drainage on the macro sheet
(M1c), the streaming band min/max (M1d), the lab L4 + Release perf gates (M1e), Flat/heightmap trivial
sheets, the DB tile cache (measure first: a 200 ms Debug bake per tile may not need it).

**M1d BUILT 2026-10-15 — the streaming surface band is a {lowest, highest} span per chunk column
(resolution 9, audit §5's latent flank hole).** `ChunkStreamingManager::SurfaceBandFunc` now returns
`glm::ivec2 {lo, hi}` and the wanted set spans `[lo − 1, hi + 1]`; `WorldGenerator::surfaceBandForChunkColumn`
samples the REAL surface on a 5×5 grid at stride 8 u (both edges included) and takes min/max —
chosen over the plan's "bicubic + overshoot margin" because the real surface (fine band, biome
blend, carve) is what streams, and the manager caches the answer per chunk column so 25 samples
are paid once. Applies to v1 worlds too (D10 freezes terrain VALUES; which chunks stream is the
manager's business, and Ravenmere's flanks benefit). **Red first:** `SurfaceBandTest.
EveryColumnOfEveryChunkColumnLiesInsideTheReportedBand` on Mountains 424242, 120 scattered chunk
columns: the centre-only band (the exact lambda it replaces) missed on 4 chunk columns, 1042 columns
outside the band, worst in-column span 5 bands (`m1d_surface_band_red.txt`) → 0 misses with the grid
(`m1d_surface_band_green.txt`, 35/35 with the streaming suites). Not a strict bound (a spike between
samples could still escape); the test is the measurement, and widening the grid, never the pad, is
the lever if a miss ever appears. **Solution-auditor (2026-10-19, raw): "VERDICT: PASS (with caveats
on Q4 and Q5). The core claim is real: I reran SurfaceBandTest and it passes, and it fails with the
old centre-only band. The cost of the wider band is not measured."** Caveats taken: the test now
prints the mean reported band spread (each column streams spread + 3 chunks) and the L4 cost of the
wider wanted set is UNMEASURED (the 1033 vs 854 resident counts are different runs, not an A/B);
`clearSurfaceBandCache()`'s only caller is `restreamWorldLive()`, so a recipe change that does not
restream keeps stale bands (pre-existing; the Layer-0 epoch now exists to hang that on).

**M1c BUILT 2026-10-15 — macro stream-power erosion, machinery only; constants stay recipe knobs,
default OFF.** `core/MacroErosion.{h,cpp}`: per step, `PriorityFlood::fillWithFlow` (sea = base level)
→ D8 receivers → a Braun & Willett stack (base level first, donors after receivers, deterministic
donor order) → drainage area by reverse stack → the implicit n = 1 update along the stack,
h_i = (h_i + dt·U_i + f_i·h_r) / (1 + f_i), f_i = K·dt·A_i^m / d(i, r), never below the receiver
— the form the grounding read CONFIRMED in Fastscapelib `spl.hpp` and Landlab's FastscapeEroder (the
Braun & Willett 2013 text itself is still unread). Single-receiver D8, a deliberate deviation from
Veloren's 8-receiver MFD (simpler; revisit if valleys read too straight). `SheetParams.erosion{Steps,
Dt, K, M, UpliftMax}` (recipe `sheet.erosion*`, clamped; `erosionM` to the GROUNDED [0.35, 0.6] band)
drive it from `rebuildSheet` with the uplift field = normalised base elevation above sea; **the
shipped default is `erosionSteps = 0`** because K and U are not groundable to a value (§10 F4d).
`MacroErosionTest` 6/6 on a stated RIG (96² cells at 512 u, a 300 u dome + hash roughness, K = 1e-5
m^0.2/yr — inside Stock & Montgomery's volcaniclastic band — m = 0.4, dt = 1000 yr, 20 steps, U =
1e-3 m/yr at the dome centre): steps = 0 is the bit-identity; receivers form a forest rooted at base
level and the stack orders donors after receivers; no receiver sits above its donor on the filled
surface; drainage area is conserved at base level; Dd falls monotonically with the threshold and
stays under the 2.7 km/km² grid ceiling; erosion lowers the mean without uplift, uplift raises it,
10× K erodes more; a receiver chain crosses the old +16384 box edge (`HydroEdgeTest`'s M1 scope).
**Constants grounded the same evening (§10 F4e): the recipe defaults are now Cordonnier 2016's
configuration (m 0.5, k 5.61e-7, U 5e-4, Δt 2.5e5) plus the 30° talus ceiling in the solver and a
monotonic remap of the eroded relief into the height spline's range; `erosionSteps` stays 0 by default
until the lab L4 runs (the lab recipe sets 200). `MacroErosionTest.GroundedConfigurationFormsReliefBy50StepsSettlesBy200AndHonoursTheTalusCeiling`
pins the paper's own claims on the rig (relief formed by 50 steps: 311 → 5086 m; settled by 200:
→ 4222 m, a smaller move; 0 cells above the 30° ceiling over their final receiver). **Two reds on the
way (`m1c_grounded_config_red.txt`):** (1) the §16 prediction "valley density rises monotonically with
steps" was MIS-SPECIFIED — the trough-count metric fell 1.60 → 1.30 → 1.25 /km² because it counts the
rig's hash roughness, which erosion smooths; it is printed now, not asserted, and the lab's valley
metric must be drainage-based (M2's channels), not a trough count; (2) the per-step talus ceiling
held against each step's receivers but re-routing left 16 of 9216 cells above their FINAL receiver by
more than the ceiling — the solver now re-applies the ceiling along the final routing. The rig's
relief (~4–5 km) is 2× the paper's hmax fit, so the fit does not transfer to this grid; the remap
into the spline range makes that moot for the engine.** **Solution-auditor on M1c (2026-10-19, raw): "VERDICT: FAIL — the structural tests pass, but the
update equation is never checked against a closed-form value, the `hn<hr` clamp silently fills
depressions, the 2.5x relief overshoot against the paper's same-size 50 km setup is explained away
without measurement, and the lab L4 has no erosion-off control."** Closed the same day
(`m1c_audit_closures_green.txt`): (1) `OneStepMatchesTheClosedFormOnAThreeCellChain` — one implicit step
on an interior chain equals (h + f·h_r)/(1 + f) to 1e-3 (first version sat on a 3×1 grid where every
cell is a PriorityFlood border outlet, so nothing eroded — a red worth having); (2) the clamp is GONE:
a cell under a lake (real surface below the filled surface) is uplifted only, never eroded or filled
by the solve — `ALakeCellIsNeitherErodedNorFilledBySolveOnlyUplifted` pins it, and for a non-lake cell
the implicit update is a convex combination of (h + dt·u) and h_r so no clamp is needed; (3)
`OneDimensionalChainReachesTheAnalyticSteadyState` — a 48-cell channel with uniform uplift run to
convergence matches S = (U/K)·A^(−m) cell by cell with **0.00 % error** (relief 9578 m analytic 9578 m):
the solver reproduces the stream-power equilibrium exactly, so the uniform-uplift rig's 5199 m vs the
paper's 2000 m (ratio 2.60, `UniformUpliftReliefIsWithinAFactorOfThreeOfThePaperFit`) is the
EQUATION's own relief for a 48-cell drainage path at 512 m cells, not a solver or area-unit defect —
the paper's hmax fit belongs to its painted uplift maps; (4) convergence is a RATE, not a weak
inequality: p95 |Δh| per 50 steps on the dome rig = 4.23 % of relief at 100→150, 1.64 % at 150→200,
**0.00 % at 300→350** (`…ConvergenceRateFallsAndIsUnderOnePercentBy350Steps`), so Cordonnier's
"converges after around 200 time steps" does not hold here at 1 % — their count is their setup's;
the lab recipe stays at 200 (fully shaped), the fact is recorded; (5) **the lab L4 control ran
(2026-10-19, Release 6e099e5e, `m1c_lab_ab_run4.txt` + `m1c_lab_eroded_run.txt`):** WorldSheetLab at the
RAISED height spline (game.json `world.heightSpline`, a new loader key: 16 at continentalness 0.30 →
380 at 0.80, so the lab's low-continentalness noise spans the vertical budget; rig delta stated), two
fresh boots at the same pose: **control** `erosionSteps 0` vs **eroded** `erosionSteps 200`
(`macro erosion 200 steps x 250000 yr: mean height 172.9 -> 410.9, base-level cells 79, eroded max above
sea 1395 u remapped x0.259 into the spline range (361 u)`). Generator profile along z = 40 over ±4 km,
129 columns: control range 64..402 u, eroded range 24..427 u, **127 of 129 columns changed by ≥ 2 u,
mean −59.7 u, max |Δ| 190 u** — erosion reshapes the terrain, it is not a rescale (`m1c_lab_control_
profile_z40.txt`, `m1c_lab_eroded_profile_z40.txt`). Captures at (0, surface+220, 700) pitch −32:
`m1c_lab_control_capture.png` is a long snow-capped forested flank; `m1c_lab_eroded_capture.png` at the
same pose (75 u lower, the local surface dropped) is a carved grassy valley slope with the snowy ridge
behind it. Both boots logged `Configured far-terrain mesher` once (the epoch wiring did not fire a
reconfigure, correctly: nothing rebuilt Layer-0 after load). Two operational findings on the way: every
engine boot that morning failed with "Port 8090 is already in use" because the 10/16 reboot put 8051–8150
in a Windows excluded port range (AgentContext gotcha; the lab scripts now pick a free port), and one
"ENGINE DIED" in the A/B run was a STALE backgrounded script's `taskkill` by image name, not a crash — the
scripts no longer kill by name. Measured, NOT
asserted (the band is NEEDS-RESEARCH): Dd × √A_c = 0.587 / 0.625 / 0.640 at thresholds
of 2 / 8 / 32 cells, then 0.292 at 128 cells where the 96² sample runs out — the near-constancy over
the first three is what the GROUNDED exponent Dd ∝ A_c^−0.5 predicts, and the one real-catchment
point the read found (Brushy, ρ√A ≈ 0.36) is the same order. **Lab L4 with erosion ON (Release cc305588, `m1c_lab_l4_run.txt`, `m1c_lab_sheet_log.txt`):** the
loader promoted the lab's `world.sheet` (erosionSteps 200) to a version-2 recipe; the generator logged
`macro erosion 200 steps x 250000 yr: mean height 43.9 -> 273.8, base-level cells 100, eroded max above
sea 1228 u remapped x0.0646 into the spline range (79.4 u)`; the generator profile along z = 40 over
±4 km (`m1c_lab_profile_z40.txt`) rises 20 → 60 → 40 u in smooth lobes; the elevated capture
(`m1c_lab_erosion_capture.png`, (0, 238, 600) pitch −30) shows terraced rolling hills with a snow
line on the far plateau and no tile seam at x = 0. Finding: on this 16-cell lab the continent noise
never reaches high continentalness, so the height spline's range is only 79 u and the 1228 u of eroded
relief is compressed 15×; to SEE the mountains the lab needs the "how tall" knob raised (a taller
`heightSpline`, or Mountains type) — the next lab run, not a code change. Not yet: the two-layer bedrock/sediment model and rock strength → max slope (plan text;
deferred until K/U are grounded — Veloren's tan 15°–60° bound is game-tuned, not a geoscience
source), the orographic climate.

**M1e, first measurement 2026-10-15 — the §9 streaming gate is RED at 1.65×.** The v1 baseline
procedure (same pose (300, 109, −1400), same laptop, Release, fresh world.db) on
`PhyxelProjects/WorldForgeBaselineV2` (the baseline recipe + a `world.sheet` block, erosion off):
`[WORLD_SHEET] macro 200x200 @ 512 u baked`, generator surface at the pose 69 (= v1: plains, relief
mask 0, the 512 u bicubic and the 32 u bilinear of the same continent noise agree to under a voxel
there), **18.86 chunks/s** over the saturated ramp vs 11.41 (`m1e_stream_gate.json`,
`m1e_stream_gate_run.txt`); frame p50 17.4 ms (was 34.0), p95 98.1 (was 92.6). The gate needs
22.8. **Profiled the same day (RelWithDebInfo b768801d + the M1e instrument, three runs, the third with
the generate/decorate split: `m1e_rwdi3_profile_summary.json`, 47 s saturated ramp, 786 chunks
generated, 773 landed, 17.8 chunks/s):** per chunk on the WORKER, `generateChunk` = **138 ms** and
flora decorate = **42 ms** (180 ms summed over the three workers ⇒ a 16.7 chunks/s ceiling, which is
the number measured); the column sampler at 18.6 evals × 1024 columns is ≈ 1 ms of that. On the MAIN
thread per landed chunk: Dirty Remesh ≈ 12 ms, Dirty Chunk Flush ≈ 13 ms, Light Occupancy ≈ 9 ms,
plus Fence Wait 10 s over the ramp; the async finalize itself is 0.1 ms. The 313–848 ms update frames
were the budgeted dirty remesh (`Frame/update/Update/Dirty Remesh`, now a scope; it was "the block
the profiler doesn't cover"). **Solution-auditor on M1e (2026-10-19, raw): "VERDICT: FAIL. The raw numbers in A and B reproduce
from the JSON, but D13's 'mis-targeted gate' conclusion rests on an unmeasured ~1 ms column cost, and
the plan's own 11.4 to 18.9 result points the other way."** Corrections taken: over the SAME 1.5–53.8 s
window the v1 baseline fits at 12.72 chunks/s, so the like-for-like ratio is **1.48×**, not 1.65×
(the baseline's 76 s window decays 15 → 9 chunks/s; n = 1 per side, uncertainty unquantified); the
180 ms is per chunk on ONE worker, not "summed over three"; the 3-worker ceiling 16.6 chunks/s sits
BELOW the measured 17.8 and 18.9, so the "ceiling matches" reading was wrong by ~10 % (wall-clock per
chunk includes pre-emption; the instrument is RelWithDebInfo, the 18.9 is Release); light occupancy
is 11.8 ms per landed chunk, not 9; 29 % of Update is still unattributed after the Dirty Remesh scope.
**The "~1 ms column sampling" figure was NOT measured** — it was 18.6 evals × 1024 × an assumed 60 ns —
and the v1 worker was never profiled, so "the column work does not own this cost" was an INFERENCE.
Two measurements replace it: a timer around the column loop in `columnsForChunk`
(`streaming.worker_column_ms` per recorder row, the column share of `generateChunk` in ms, no assumed
ns/eval), and the SAME instrument applied to the pre-M1 commit 4961a277 in a worktree so v1's
per-chunk `generateChunk` and column ms are measured at the same pose (`m1e_v1_profile_*`, below).
**MEASURED the same day (Release ea59df15, gate pose, free port, `m1e_colshare_profile_summary.json`,
`m1e_colshare_record.json`): column sampling = 148.7 ms of the 157.7 ms per chunk in `generateChunk`
(flora decorate 51.6 ms on top; 794 chunks over a 56 s saturated ramp at 14.8 chunks/s — run-to-run
variance against the 18.9 of 10/15 is large, n = 1 each). The voxel fill is ≈ 9 ms. So the D13 premise
("the per-voxel fill dominates, the column term never owned the cost") was WRONG and the auditor was
right: the column sampler owns ~94 % of generation. The noise-eval counter (18.6 per column) measured only
the noise; at ≈ 0.4 ms per fresh column the cost sits elsewhere in `sampleColumn` — the channel searches
(`nearestChannel`, `channelAt`, the meander warp, `inCreekBand` corner checks), the 8-biome exp() blend,
the worldforge road/bridge queries, the macro bicubic + three pager locks per column. D13's POLICY stands
(the owner's: chunks/s is reported; the generation cost gets its own milestone with a profile first).
**ROOT CAUSE FOUND AND FIXED 2026-10-20 (dc3c8376).** The per-phase timers put inside `sampleColumn`
(nanosecond, process-wide) and a bench over every block of the 17×17 streaming radius showed a column
costs **1.6–2.6 µs** everywhere (`m1h_radius_release.txt`: the whole radius is 560 ms single-threaded),
and every phase in the engine was inflated ~70× uniformly — so the columns were never slow. Three more
recorder numbers settled it (`m1e_cpu_profile_summary.json`): worker **cpu/wall 0.96** (the workers
really ran that long), **meso tile bakes 0** (no pager thrash), and **32.06 fresh 32×32 column blocks
sampled per generated chunk** — the water-span batch pads every chunk by `kWaterExtentSteps` (256 u = a
19×19-block neighbourhood) through a 512-entry FIFO column cache, and under the camera-priority wanted
order split across three workers the FIFO evicted the hot centre blocks first; `WorldGenerator.cpp`'s own
comment had predicted exactly this failure. Fix: the column cache is LRU, and the two neighbourhood
consumers (water spans, fine ponds) read a compact `SurfaceBlock` (surfaceY / riverOrder / channelDepth,
~9 KB) from a 2048-entry LRU that holds the whole padded neighbourhood. `ColumnCacheTest` reproduces a
worker's view of streaming the gate pose without the engine: **4.63 fresh blocks per chunk against the
analytic floor 4.77** (own blocks + the 33×33 padded area over 289 chunks; the engine's 32.06 is the red
on record — the Debug red run was aborted after 45 minutes, which is the thrash itself). Release: 430/430
across the water, terrain, streaming, pond, sheet, golden and road suites. **Engine, same pose/machine/
config (`m1e_fixed_profile_summary.json`): 52.3 chunks/s** (the wanted set filled in 20 s), column
sampling 9.3 ms per chunk, 2.96 fresh blocks per chunk, `generateChunk` 13.0 ms, flora decorate 39.3 ms,
cpu/wall 0.98. So the sheet (column cost 136 → 18.6 evals) plus the cache fix give 11.4 → 52.3 at the
baseline pose, 4.6× (the withdrawn ≥ 2× gate would have passed; it stays a reported number per D13).
**The generation milestone's target is now flora decoration (39 ms of the 52 ms per chunk), not the
voxel fill (13 ms incl. columns)** — the premise is corrected twice over by measurement. The v1 worktree
profile is dropped: the question it was built to answer (does the column term own the cost) is settled
by direct measurement in the live engine.
M1's own deliverable (≤ 25 evals per column) is GREEN; the 1.65× is what that deliverable buys.
Recommendation for the owner (a plan change, not taken unilaterally): keep "chunks/s on the baseline
pose" as a REPORTED number, and give the voxel-fill path its own perf milestone with its own
red-before-green profile (what inside `generateChunk` costs 138 ms per 32³ chunk: material
resolution, subcube shelves, occupancy grids), since it is shared by v1 and v2 and outside M1's scope.
M1d's wider wanted set (closing the flank hole) is also in the number. Pending at the end of the
window dropped to 4: the wanted set filled, so the ramp window is the honest part. Remaining M1 items after this: the generation profile and whatever it
names; the Release meso bake (76 ms vs 50); the lab erosion experiment (valley density vs steps);
far-mesher `configure` on every rebuild path (13.1); Flat/heightmap trivial sheets; the DB tile
cache (not needed at 76 ms/tile so far).

Validation: L2 (`MacroErosionTest`: drainage is a tree, no sinks, dendritic density within a
grounded band; `MesoSeamTest`; `ColumnCostBench`), L4 Release fly-through at a fixed pose set
before/after (same seed is NOT comparable — new terrain; compare *statistics*: slope histogram,
valley density, chunks/s). Red tests: `HydroEdgeTest` fails today (channels die at ±16384),
`ColumnCostBench` fails today (> 25 evals). Stress: continent 1000 × 1000 macro cells (512 km),
tile churn 10 000 page-ins, worker-copy determinism. Test world: `PhyxelProjects/WorldSheetLab`
(one meso tile, flat uplift + one painted ridge; predictions written before the bake). Perf gate:
macro bake time, meso tile time, column cost, chunks/s in forest, all on a named GPU/CPU.

### M2 — Hydro Geometry (M) — *rivers, lakes, coasts as shaped basins*

Contract: every channel of order ≥ 1 is a **spline centreline** through meso drainage with width,
depth and bank profile from drainage area (hydraulic geometry: keep the Doll et al. order table as
the fallback, move to discharge-based width/depth — grounding-auditor to pick the source), a
floodplain term, smoothed confluences (tributaries enter at a grounded angle, not 90°), deltas and
mouth widening; lakes get rims and a shelf; coasts get a shelf profile and a beach band (the
2026-07-10 gap); underground bodies are shaped by an aquifer-style scheme (Minecraft 1.18:
jittered cell centres, nearest-centre lookup, pressure lid — shape only). Springs are emitted at
creek heads and lake feeds with discharge = catchment × runoff. `HydrologyMap` / `FlowField`
become adapters over this (§4).

Validation: L2 (`RiverGeometryTest`: monotone bed along every centreline, width/depth monotone in
drainage, no 90° junctions, no channel terminates at a tile edge; `BasinRimTest`: every basin's
rim is ≥ its real spill everywhere, measured on `sampleSurface` — the acceptance test in its shape form, D17), L3 (`TraversalProbe` along both banks: a character can walk a river bank), L4 (fixed-pose
captures of a confluence, a lake rim, a coast; stated defect in frame before/after). Red tests:
`BasinRimTest` fails on today's 128 u levels; `RiverGeometryTest.NoRightAngleJunctions` fails.
Stress: order-6 trunk across 100 km, 500-lake continent, a lake exactly on a meso tile edge.
Test world: `WorldSheetLab` + `RiverLab` (one tile, one order-3 river into one lake, one coast).
Perf gate: hydro geometry bake per meso tile.

**M2 design check (2026-10-20): NEEDS WORK → the owner settled D15 (hybrid drainage) and D16 (rivers may
change on v2 worlds); the remaining items are engineering.** The adapter contract M2 must keep, field for
field (meanings unchanged, computation replaced), from the consumer inventory:
- `ColumnSample::{surfaceY, bedHeight (continuous, pre-carve height minus the carve — WaterCore 25.21/25.22:
  the water flows on it), channelDepth (order ≥ 3 carve; fractional for orders 1–2), riverOrder, creekBed
  (order 1–2 inner band, carved to the 1/3 m grid)}` — read by the water core's bed query
  (`Application.cpp:451`), `applyRiverInflows` (channel tags + order pins), the flora gate, the road stamp.
- `waterSpansForChunkColumn` / `waterSpansForBlock` (per-column spans from the basin levels; the padded
  batch now reads `SurfaceBlock`s) — read by the chunk writer, `SettlementBuildService::waterSpanAt`.
- `HydrologyMap::{filledAt, waterLevelAt}` and `FlowField::{channelAt, nearestChannel, orderAt, accumAt}` —
  read by `WorldForgePlan` (6 sites), `RenderCoordinator` (2), the water-span batch; M2 serves them as
  adapters over the macro drainage + meso channels.
- `WaterBodyIndex` (bodies with id/class/level/volume from the flood) — read by the renderer and the water
  core's body records; M2's basins must yield the same record shape.
**M2b L4 (2026-10-21, Release 1d633964, `m2b_l4_run.txt` + `m2b_l4*_*.json/png`):** the baseline world boots on the meso
bake (`water_bake_info`: 32 u, 1024², 56 macro inflows, lake area split 65 536 m²), streams at a river and at a lake,
and the rim-leak validator gives M2c its red numbers (row below). The lake's WATER is present as data — stored spans
in 27 chunks topping at 128–129.8, the render grid 8 511 wet columns at 129.8 in a 97×97 rect (`m2b_l4b_*.json`; the
project has no `water` block, so the span sheet is the only water) — but the two captures (`m2b_l4_lake.png`,
`m2b_l4b_lake_low.png`) show snow terraces and no unambiguous water plane, so the visual is NOT claimed; the
straight-down capture that would settle it crashed the editor (the Properties panel dereferenced the hovered voxel's
chunk after the teleport had unloaded it — `crash_20261021_104538.txt`, fixed in `VoxelInteractionSystem::
hasHoveredCube`, which now checks the chunk is still resident) and is rerun after the rebuild. Two tool gaps found and
fixed in the same pass: `water_find_river` reported width/depth from the per-order TABLE (now the cell's law geometry
+ `carved_river`), and its ring scan at step 32 lands on almost no 1–3 u-wide channel column (one site in 6 km) — it
should walk the FlowField's cells, not sample columns (open).
**M2c as built:** `BasinRimCorrector` (new) — lazy, memoized per basin, shared by every copy of a generator and by the
process-wide hydro cache, holding no generator pointer; `WorldGenerator::waterLevelAt / basinLevel` are THE level
queries (rim-corrected; `HydrologyMap::waterLevelAt` keeps the raw coarse spill as the adapter contract says), and
every consumer reads them: the span batch, road grading, flora/fauna gates, `WorldForgePlan` (new `LevelFn`
parameter), the editor's water table, body-identity and body-record bindings. The level is the minimum of the coarse
spill and the lowest voxel surface over the ring of dry columns bordering the wet cells (+ 0.9 so `floor(level)` equals
the rim top voxel), the wet set re-derived at the lowered level and the ring recomputed to a fixed point (≤ 16
iterations; the level only falls; a basin whose rim is below its floor dries). The ring is sampled with the
generator's own column sampler (the one truth for the voxel surface); a thread-local guard makes the sampler's own
level reads see the raw bake during a correction (stated second-order case: a road graded onto a lake). **M2c L3 on the engine (Release 235ca984, `m2c_l3_run.txt`): the same two validator rects as the red - lake rect
rim 543 / rim_leaks 418 -> rim 254 / rim_leaks 0; river rect 287 / 218 -> 0 / 0. The price is visible: the 68-cell lake
(spill 129.8) kept 2 wet cells (52 420 -> 2 048 wet columns) and the river-site basin dried, because their real voxel rims sit
up to ~18 u below the coarse spill - v2 worlds now carry far fewer and smaller lakes than the coarse flood promised. That
is the invariant doing its job on a bake that samples cell centres; the OTHER way to honour it (raise the rim instead
of lowering the water, or flood on per-cell minima so basins are found at voxel resolution) was put to the owner and answered by D17: WorldForge delivers the basin SHAPE with its real spill;
whether a lake sits in it is the water core's later business, so neither raising rims nor re-flooding is done for water's sake. Frame-time hitches after the teleports are the same magnitude as before (worst 2.4 s -> 2.7 s,
both fresh-DB boots), so the first-touch correction cost is not separable here. Still open
for M2c-2: the per-basin cost is paid on the first query of that basin (the water table's first touch on the main
thread) — measured by the test's printed ring-column count; the `water_validate` L3 on the engine is the next check.
M2 prerequisites built the same day (`m2a`, 387b42f3; red shown by MUTATION after the commit - drainage copy + uplift disabled -> both new tests fail, restored -> pass, `m2a_mutation_red.txt`): the `MacroSheet` carries `receiver / drainageArea / filled`
from the erosion bake's final routing (routing runs even at `erosionSteps 0`); a painted `upliftMask`
(Gaussian bumps/basins, in the sheet key and the recipe JSON) so `RiverLab` can paint its ridge and basin.
The three M2-proper items were answered by the engineering design below (one global meso bake: no
per-tile routing, no seam test, no apron derivation needed) and M2b-1/M2b-2 are built. The hydraulic-geometry law is settled in §10 F4f (both
primary sources READ from the scans 2026-10-20): **W = 2.37·A^0.36 u, D = 0.296·A^0.30 u** (Doll, A in km², valid
0.57–417 km²), extrapolated past 417 km² under Leopold & Maddock's downstream b = 0.5 / f = 0.4 (which reproduce
Doll's 0.36 exactly through Q ∝ A^0.72); the regional/biome knob is the **intercept only** (PP252 p. 14: coastal-
plain vs Great-Plains rivers differ ~7× in W/D at the same discharge, same exponents). Still NEEDS-RESEARCH before
a number is typed: floodplain multiple (keep `kValleyWidthMul = 5.0` labelled tunable), beach band, lake shelf.

**M2 engineering design (2026-10-21) — the three open items, answered in the plan before any code.**
The strictly-local per-tile routing the design check sketched has a flaw found while designing it: a
macro river crossing a tile edge exits the upstream tile at a meso cell the downstream tile cannot see
(it only knows the 512 u macro crossing), so the river would teleport at every seam unless crossings were
pinned on macro cell edges and every in-cell path became a constrained least-cost path. That is buildable
but it is a lot of mechanism for a bounded world: the sheet's continent is a fixed `continentCells = 200`
(102 km) and the hydrology box is a fixed ±16 384 u (32 km). So the hybrid is realised as: **macro drainage
everywhere (the whole continent, m2a) as the boundary condition; meso-resolution drainage baked ONCE over
the hydrology box (32 u cells = the meso cell, 1024² = 1 M cells instead of 256² at 128 u) with the macro
inflows injected at the box border.** Chunk independence is then by construction (one global bake, read
locally) instead of measured per tile, and D15's letter holds: macro supplies what lies beyond the meso
bake, meso supplies the local shape. The full-continent extension (3200² at 32 u ≈ 10× the cells, ~5 B per
cell ≈ 50 MB, Priority-Flood single-threaded) is a later increment that must bring its own measured bake
time; nothing in M2b assumes it. v1 recipes keep the 128 u bake byte-for-byte (D10; `LegacyTerrainGoldenTest`).

| Increment | Contract | Red test (fails on today's code) | Stress / lab | Perf gate |
|---|---|---|---|---|
| **M2b-1 meso drainage bake — BUILT 2026-10-21 (red → green, `m2b_red_green.txt`)** | v2 recipes bake `HydrologyMap`/`FlowField` at the meso cell (32 u) over the same ±16 384 box, sampled at cell CENTRES (today: corners); the channel-initiation threshold rescales by cell area as it already does (98 cells = 0.1 km²); `WaterBodyIndex`'s Lake/Pond split becomes an AREA (today `kInfiniteMinCells = 4` cells, which would shrink 16× silently); macro cells outside the box whose receiver lies inside inject their `drainageArea` at the border meso cell nearest the macro segment's crossing (D15's boundary condition — a river entering the box no longer starts from nothing); width/depth come from the continuous law `W = 2.37·A^0.36 u, D = 0.296·A^0.30 u` on the cell's drainage AREA (§10 F4f) with the order table as the v1 fallback only — **split out as M2b-2** (below): the water runtime branches river-vs-creek on ORDER (`applyRiverInflows`: order ≥ 3 pins full only where carve ≥ 0.5, else a 0.15–MIN_HOLD ribbon), and under the law an order-3 stream is 0.3 u deep, so the law needs the classification moved to depth on both sides first; `channelAt / nearestChannel / orderAt / accumAt / filledAt / waterLevelAt` keep their meaning (adapter contract above) | `HydroResolutionTest.V2BakesAtTheMesoCell` (cellSize 32 on a v2 recipe, 128 on v1 — the same seed's v1 hash unchanged); `RiverGeometryTest.WidthFollowsTheGroundedLaw` (every river cell's half-width within 10 % of the law at its area; today's table misses by up to 2× between orders); `HydroInflowTest.MacroInflowEntersAtTheBorder` (a painted `upliftMask` ridge outside the box drains in: the border cell's accum ≥ the macro area, today 1); `WaterBodyIndexTest.LakePondSplitIsAnArea` | order-6 trunk across the box; a basin exactly on the box border; the 8-site baseline world rebuilt (sites must still be legal — `WorldForgePlan` reads `filledAt`); lab `RiverLab` = `WorldSheetLab` recipe + one painted ridge outside the box and one painted basin inside | bake time Release AND Debug printed by the test and recorded in §9 (today's 65 k-cell bake is unmeasured; the Debug suite pays it once per distinct recipe key) |
| **M2b-2 the law - BUILT 2026-10-21 (red -> green, `m2b2_red_green.txt`; baseline box under the law: 73 637 river cells, 1 265 carved at >= 0.5 u, deepest 0.70 u, widest 6.7 u; 200/200 across the sheet, flow, river, hydrology, body, worldforge, water-manager, pond and occupancy fixtures; v1 golden unchanged)** | width/depth from `W = 2.37·A^0.36 u, D = 0.296·A^0.30 u` on the cell's drainage area (§10 F4f); river-vs-creek classified by the channel's centreline depth (≥ 0.5 u carves and pins full; 0.15–0.5 u is the creek ribbon on the 1/3 u recess) instead of by Strahler order — generator (`sampleColumn`, `creekBed`) and runtime (`applyRiverInflows`) together, with the order query kept as Strahler; v1 untouched (table + order) | `RiverGeometryTest.WidthFollowsTheGroundedLaw` (every v2 river cell's half-width within 10 % of the law at its area; today the table misses by up to 2× between orders and starts order 1 at 0.1 km² where the table's chaining assumed 0.62 km²); `CreekVsRiverIsADepthNotAnOrder` (an order-3 cell 0.3 u deep gets the ribbon, not a dry tagged bed) | the order-6 trunk (area 100+ km²: 12 u wide, 1.2 u deep) and a 0.1 km² head; the baseline 8-site world re-planned (`WorldForgePlan` reads `orderAt ≥ 3` to avoid carved rivers — must read the class) | per-column phase timers at the gate pose |
| **M2c rim guarantee — BUILT 2026-10-21 (red → green the same day, `m2c_red_green.txt`: 24 nearest above-sea basins of the baseline world: 2 778 leaks of 4 032 rim columns, worst 18 u → 0 leaks; 22 levels lowered, 21 basins DRIED — 1–5-cell coarse pits whose real rim lies below their floor are not basins at voxel resolution (voxel-scale ponds are the fine-pond machinery's), 3 survive at lowered levels; 4 160 ring columns sampled, every basin converged in one pass, max drop 17.8 u; 182/182 across the rim, worldforge-plan/build-flow/recipe/stress, body-index, hydrology, water-manager, river, golden, sheet, occupancy, pond and settlement fixtures). RED MEASURED earlier that day on the 32 u bake (`m2b_l4_run.txt`): the first above-sea lake near the origin of the baseline world (68 cells, level 129.8) validates at rim 543 / rim_leaks 418 (77 %), worst 19 u; a river site at (−1248, 224): rim 287 / leaks 218, worst 6 u; the sea-level inland sea: 75 / 80, worst 5 u. Finer sampling alone did NOT close the leaks — the guarantee below is required** | each basin's level = min(the coarse spill, the minimum per-voxel `bedHeight` over its RIM RING — the first row of columns in dry cells adjacent to wet cells); the wet cell set is re-derived at the lowered level and the ring recomputed to a fixed point (level only falls; bounded by the basin floor). This is the owner's acceptance test made true by construction: no rim column can sit below the level it borders, so `water_validate rim_leaks == 0` on the bake, and a carved outlet automatically becomes the lake's level (the outlet bed IS the spill) | `BasinRimTest.NoRimColumnBelowItsBasinLevel` over every basin in the box of the baseline seed (today: 606/606 leaks at 128 u on the coast world, worst 38 u) and the `water_validate` L3 on the lab lake (rim_leaks printed before/after) | a lake whose spill is a carved order-3 outlet; a pond of 1 cell; a basin straddling the box border; 500-lake continent for the fixed-point iteration count | correction cost per basin printed (ring columns × iterations) |
| **M2d channel geometry — M2d-1 + M2d-2 + M2d-3 BUILT 2026-10-21. M2d-3 (`m2d_red_green.txt`): the meander wavelength follows the channel width, λ = 7.3·w^1.1 u (F4f), as three fixed-frequency warp bands (λ 7.5 / 20 / 55.6 u) blended in log-λ by a blurred local-width field read bilinearly (smooth, so no seam; the widest band IS the shipped v1 warp; amplitude per band = λ, the shipped ratio, REASONED); measured zero-crossing spacing of the lateral warp 6.1 u at w = 1 vs 53.3 u at w = 6.3 (ratio 0.114, law 0.132); the M1 eval gate holds (18.6 of 25). Channel beds read as beds: gravel where the profile falls ≥ 0.001, sand where flatter (REASONED from Leopold & Wolman 1957 p. 60 Cottonwood Creek and PP252 p. 14 Tombigbee). Fillets now also inside coarse basins (centreline data there): 1 207 of 1 207 junctions filleted; bake-space mean 52.3° with 0 right angles (the raw grid: 51.5°, 22.5 %); world space through the warp 57.1°, 7.0 % — the bimodal 30–40 / 50–60 histogram is the clamp at work: Howard's ratio from the profile slopes rarely falls inside the band. The junction assertion is on the CONSTRUCTION (bake space); the world-space statistic through the warp is reported, not asserted, because 4 u tangents rotate at creek wavelengths. Three L4 dry-shape captures (`m2d_l4_*.png`): a 6.7 u-wide profiled river reads as a shallow grassy trough with a 2/3 shelf (grounded depth 0.7 u) — correct and near-invisible without water, hence the bed material; an in-basin confluence is a one-voxel groove across a stair-stepped pit. 136/136 across 15 fixtures incl. the material and column-cost benches; the M2d perf gate (≤ 10 % at the gate pose) is MISSED and recorded: Release 1.8–2.0 µs per column (±0.1 run to run) vs 1.7, radius 620 ms vs 539 (+15 %); the excess is the second warp band near channels and the polyline scans — an open perf item, not hidden. Earlier M2d-1 + M2d-2 record follows.** M2d-1 + M2d-2 BUILT 2026-10-21 (M2d-2 junction fillets, `m2d_red_green.txt`: at every confluence outside a basin the tributary's last segment is a quadratic Bézier whose end tangent meets the stem's downstream direction at Howard's angle cos θ = S_main / S_trib from the bed profile's slopes over 3 cells, clamped to the observed band 45–72°; 719 fillets on the baseline box; angles measured on the carved centrelines' own end tangents through the meander warp: network mean 62.4° (band 45–72), 5.6 % within 5° of 90 — the raw D8 grid in the same run: mean 51.5°, 22.5 % right angles. Honest caveats: the histogram stays broad (0–180°) because the warp rotates a short end tangent locally and 40 % of tributaries get no fillet (basin cells, junctions at sinks); 129/129 across the river, golden, sheet, flow, rim, occupancy, cache, pond, worldforge-plan/build-flow/stress and settlement fixtures; perf gate met on Release: gate pose 1.7 → 1.7 µs per column, 17×17 radius 539 → 583 ms (+8 %, the carve phase doubled to 52.9 ms), queries allocate nothing per column (thread-local polyline buffers)) (`m2d_red_green.txt`: the longest channel's carved reach 17 rises / worst 4.8 u -> 0 rises; a longitudinal bed profile that never rises between coarse basins, carved OR filled to, on the 1/3 u grid; creeks have their bumps cut to the profile, dips left as pond basins; junction angles MEASURED: 1 207 junctions, mean 44.3 deg, 7 % right angles, 28 % under 20 deg (near-parallel joins) - M2d-2 is the fillet)** | centreline = the D8 cell chain + the existing meander warp (one warp, `meanderedChannelPos`), carved to the 1/3 u grid for every order (today orders ≥ 3 round to whole voxels); parabolic bed over the law's width; floodplain = `kValleyWidthMul` × width (labelled tunable, F4f); junctions measured, not forced: the test asserts the NETWORK MEAN junction angle in 45–72° and no junction at 90° (F4f); meander wavelength λ = 7.3·w^1.1 u drives `kMeanderFreq` per reach instead of one constant | `RiverGeometryTest.NoRightAngleJunctions` and `.NetworkMeanJunctionAngle` (today's D8 junctions are 90°/45° before the warp — measured, then fixed by a junction fillet on the centreline); `.BedIsMonotoneAlongEveryCentreline` (today the carve can step up where the warp crosses a cell edge) | 100 km-equivalent trunk (the box diagonal), confluence of two order-5 rivers, a river crossing a lake | per-column phase timers (M1e instruments) must not move more than 10 % at the gate pose |
| **M2e springs, coasts, shelves — M2e-1 springs + M2e-2 coast BUILT 2026-10-21 (`m2e_red_green.txt`). Springs (data, D17): FlowField enumerates creek heads (channel cells with no channel inflow outside a basin), basin feeds (the last channel cell before a basin — the sea counts as a basin, so river mouths are feeds) and basin outlets; WorldGenerator::springSites gives each Q = runoff × catchment with runoff a recipe field (default 500 mm/yr, DESIGN inside the HA-710 range). Baseline box: 1 203 heads, 3 864 feeds, 3 461 outlets, largest Q 0.285 m³/s; the test enumerates the expected sets from the drainage graph independently (red 0 → green exact). Coast (shape): a CoastField (signed chamfer distance from the ocean bodies' cells, bilinear) and, in sampleColumn before the carve, the beach face sea + d·m₀ to a 2 u berm, a 1:10 backshore ramp to the natural terrain, and the Dean profile sea − A·d^(2/3) at sea out to the 6 u closure (m₀ 0.05, B 2, h* 6, A 0.115 for 0.25 mm: the CEM's Example Problems III-3-5/-6 and Table III-3-3, read; the ramp DESIGN); the face is cut AND filled to the profile, the backshore and seabed cut only; a column past the angle of repose keeps its cliff; the band is sand. CoastTest over 60 shoreline transects of the baseline world: faces steeper than m₀ + 1/3 u 7 → 0 (worst 1.24 → 0.10 per u), nearshore beds rising seaward 3 → 0, sand on 150/2400 → 1 858/1 880 face columns; 92/92 across 13 fixtures. Caveat: the chamfer field places the shoreline to ~15 u at 32 u cells, so the 40 u face is read from the field, not paced from the water's edge. Lake shelf stays NEEDS-RESEARCH (no shelf width/depth source; the littoral-slope figure is a summary). L4 dry-shape captures (`m2e_l4_beach2_*.png`, Release 2ffada2c): from the sea the inland sea's shore at (624, 0) is a wide sand beach stepping down in 1 u terraces under the forest edge; the transect reads 20 u at 40 u inland, 17 at the field's zero, 16 over the next 32 u seaward, 14 at 64 u - the waterline ~16 u seaward of the field's zero (the chamfer's cell scale). The first capture found the coast field counted Ocean-class bodies only, so the 114 km2 sea-level inland sea (class Lake) had no beach: fixed, every sea-level non-pond body is sea.** | `springSites` (creek heads + lake feeds, discharge = catchment × runoff with runoff a recipe parameter inside the HA-710 range), `coastAt` (distance + shelf profile; beach band derived once its two inputs are sourced — F4f NEEDS-RESEARCH), lake shelf as a labelled DESIGN default | `SpringTest.EveryCreekHeadHasASpring`; `CoastTest.ShelfIsMonotoneSeaward` | 500 springs; a 1-cell island | bake delta printed |

**M2b design check (run against the table above, 2026-10-21).** (1) Voxel aesthetic: no new asset; channels carve
on the 1/3 u grid in M2d, unconditional. (2) Chunk independence: every quantity is read from ONE global bake
keyed by `HydroBakeKey` (sheet identity included since M1) — nothing is derived from a chunk; the equality
test is the existing `DeterministicCarve` (two generator copies agree column for column) plus `WorldSheetGeneratorTest`'s
tile-vs-whole seam test for the heights the bake samples. (3) Pipeline: terrain → hydrology (this) → biome
materials → flora gate → structures; it consumes `baseHeightFn` (meso + fine band) and the macro drainage; it
changes what later stages see only through `ColumnSample` fields whose meanings are fixed by the adapter
contract; v2-only, so recipe version gates it and v1 worlds are untouched. (4) API: no new HTTP route in M2b;
`water_bake_info` reports `cellSize` (32 vs 128) so a caller can assert the bake it got; the Lake/Pond area
constant moves to m² and is echoed by `water_bake_info`. (5) Visual test: L2 is the row's red tests; L3 =
`water_validate` on the lab lake (rim_leaks before/after); L4 = a fixed-pose capture of the lab lake and the
lab river at the same pose before/after with the defect (a leaking rim / a stepped bed) named in frame. Test
world `RiverLab`: the WorldSheetLab recipe + one painted ridge outside the box (predicts a river entering at
the border cell nearest its crossing) + one painted basin inside (predicts one lake whose level is the ring
minimum). Rig vs shipped: identical recipe fields except the two mask entries. **Verdict: READY for M2b.**

### M3 — Civ Plan (M) — *hundreds of sites, five tiers, a history*

Contract: sites are chosen on a **suitability field** (slope, water distance bell, flood risk,
biome, resources counted in a radius: food/forest/mining/trade — Veloren's weighted log2 score;
defence: geographic domination — Emilien) with per-tier hard constraints and spacing, in a 5-tier
hierarchy + ruins; a short **growth simulation** (N epochs: found, grow, decline, abandon, war —
DF/Veloren-style, zero-player) writes a **history ledger** per site and province; economy edges
are the routes of M4. `siteCount` clamp 3–8 and the 3-tier split are retired; `WorldForgeParams`
v2 carries `tiers{city,town,village,hamlet,outpost}` targets per 100 km², spacing per tier
(central-place ratios as the grounding candidate), `epochs`, `pins`.

Validation: L2 (`CivPlanTest`: determinism, spacing, hierarchy ratios, no site in water / on a
slope above its tier's cap, every site has ≥ 1 route after M4), L4 (`worldforge_map` overlay on
the Release 100 km world). Red test: `CivPlanTest.ThreeHundredSitesAtHundredKm` fails on the clamp
today. Stress: 1000 sites; 0 habitable land (Mountains seed) degrades honestly; pins conflicting
with constraints refuse with the reason. Test world: `CivLab` (flat sheet with one painted river
and one painted massif; predict where the city lands before running). Perf gate: plan bake
< 10 s for 300 sites Release.

**M3 design check (2026-10-21, run against the contract above and the code as it stands): NEEDS WORK —
three owner decisions, then engineering.**
1. *Voxel aesthetic:* nothing new is drawn by M3 itself (sites are positions + tiers + a ledger); the
   buildings stay the structure forge's. Pass.
2. *Chunk independence:* the plan is ONE global bake read locally (as today), so every quantity is a
   function of world position; the equality test is the existing `WorldForgeRecipeTest` determinism
   plus `CivPlanTest.SameSitesFromAnyGeneratorCopy`. Pass by construction — IF the siting inputs are
   themselves global bakes (point 3).
3. *Pipeline:* siting consumes hydrology (basins, channels, springs, the coast field), biome climate, slope
   and resources. **Today every hydrology input exists only inside the ±16 km box** (1024² at 32 u), while
   T2 asks for 300 sites across the 100 km continent. The full-continent meso bake is 10× the cells and,
   at the per-cell state M2 now carries (~30 B: bed/carry/basin/width/dist + fillets), ~300 MB and ~35 s
   Release — not a drop-in. **D18 (owner):** site at MACRO resolution over the continent (the 512 u
   sheet already carries base, continentalness, climate planes, drainage area, filled surface = basins,
   and D15's receiver graph = rivers) and refine each chosen site locally with the meso bake when its
   region is realized; or raise the meso box to the continent first (memory + load time to be measured).
   Resources: no ore/mineral field exists — a "mining" resource is a new macro-scale noise band (DESIGN,
   must be labelled); food/forest come from biome + flora density (exist); trade from the coast field and
   river order (exist).
4. *API:* `WorldForgeParams` gains `tiers{city,town,village,hamlet,outpost}` targets per 100 km² and the
   spacing ladder; `worldforge_plan` must echo the clamped targets and the realized counts per tier (today it
   echoes `siteCount` 3–8, which this retires); the recipe persists them (`WorldForgeRecipeTest` round-trip
   extended). **D19 (owner):** the tier targets. Grounded anchors (§10 F4): Domesday ≈ 10 named places per
   100 km²; Christaller's ladder 12 / 21 / 36 / 62 / 108 km (K = 3). A 100 km continent then holds ~1 city,
   ~6 towns (21 km), ~25 villages (12 km) and the remainder hamlets/outposts; T2's 300 total = 3 per
   100 km² is "gameplay-thinned" (labelled). The split is a design pick inside those anchors.
5. *Visual test:* L2 `CivPlanTest` (determinism, spacing ≥ the tier's ladder step, hierarchy ratios ≈ K,
   no site in water / on a slope above its tier's cap / inside a coarse basin, pins refuse with a reason);
   L4 `worldforge_map` overlay on the Release 100 km world with the tier legend; `CivLab` = a flat sheet
   with one painted massif + one painted river (the `upliftMask` from m2a): the prediction written first
   (the city on the river at the massif's foot). Rig vs shipped: only the mask entries differ.
   **D20 (owner):** the growth simulation + history ledger (found/grow/decline/abandon/war, ruins) is a
   second increment (M3b) after siting (M3a), or part of M3 — it changes what "300 sites" means (live vs
   ever-founded) and what the ledger must persist.
Red tests ready to write once D18–D20 are settled: `CivPlanTest.ThreeHundredSitesAtHundredKm` (the clamp),
`.SpacingHoldsPerTier`, `.HierarchyRatioIsCentralPlace`, `.NoSiteInWaterOrOnACliff`, `.PinsRefuseWithReason`.

### M4 — Route Field (L) — *roads that are roads*

Contract: routes are **anisotropic shortest paths on a cost field** (Galin 2010: slope-along-
direction, curvature, water depth, vegetation; k-neighbourhood segment masks so 8-connectivity
never shows), solved on macro for corridors then refined on meso; **reuse discount** (build a new
road only if cheaper than a grounded fraction of the existing-network route — Veloren uses 1/3);
**hierarchy merge** into junctions by class (Galin 2011); **bridges, fords, tunnels, switchbacks
as edges** with their own cost and min/max lengths; **grade cap** and **bounded earthworks**
(cut/fill depth caps, bank at angle of repose, retaining walls beyond, crown/camber in subcubes)
all NEEDS-GROUNDING (§10). Realization stays a pure `ColumnSample` field (`roadClass`,
`roadDist`, `bridge*`, new `earthwork{cutY, fillY, retaining}`); bridge decks are the least-concave
majorant over clearance requirements with tapered ends (arnis `BridgeStructureMap::build`), piers
refused where they would land on a road/lower deck; far-terrain tiles draw roads AND bridges.
Settlement streets receive every arriving route as a spur with a class.

Validation: L2 (`RouteFieldTest`: grade ≤ cap along every route, earthwork within bounds,
junction count == merged graph, chunked-vs-whole seam test extended to `earthwork*`), **L3**
(`RouteWalkTest`: `TraversalProbe` walks every route end to end — 0 unclimbable steps, both seeds),
L4 (Release fly-along at fixed poses: a pass with switchbacks, a bridge, a ford, a cutting with a
retaining wall). Red tests: `RouteFieldTest.GradeCap` fails at 0.5 today;
`RouteWalkTest.MountainSeedZeroUnclimbable` fails at 0.69 % today; `BridgeSpanTest.OverNinetySixU`
fails (no bridge). Stress: 1000-route network, 1 km span, a route forced across a 60° cliff
(must produce a tunnel or refuse with the reason, never a trench). Test world: `RouteLab` (one
tile, one ridge, one river, two sites; predicted corridor drawn before the run). Perf gate:
route bake for 300 sites; per-column cost unchanged from M1.

### M5 — Settlement Map (L) — *cities as vector maps, towns on slopes*

Contract: for each site, a **headless** planner produces an OSM-like vector map from the real meso
heightfield: (1) **interest-map growth** (Emilien 2012: per-building-type interest = sociability ×
worship/civic × road access bell × slope bell × water × fortification × domination — the paper gives the FORMS only; bell parameters come from our own grounded program data, §10; seed a building,
connect it by road immediately with the M4 cost + reuse, update maps, repeat under a scenario =
tier + era program); (2) **parcels by anisotropic conquest** from road frontage (Chebyshev-like for
houses, Euclidean for fields, infinite cost through water/walls/roads, quadratic slope cost →
parcels elongate along contours); (3) **districts/rings** with purpose, layout pattern and
**ground treatment** per ring (EthosLM: terrace / podium / graded / preserve); (4) **walls as a
routed hull** (dilated hull → weighted A* → smoothed; GDMC 2022) with gates where routes enter;
(5) squares/markets/civic precincts/castle precinct (CityForge M9) as area features; (6) **the
map is data**: `nodes[]`, `ways[]{tags: highway=primary|residential|lane|path|steps, bridge,
barrier=wall|fence, width, grade}`, `areas[]{parcel, lot{typology, story count, orientation ∈ 4,
ground}, landuse=farmland|market|square|graveyard, building footprint ∈ axis-aligned polyomino}`,
stored in the ledger, exported by API, drawn by `worldforge_minimap`. Buildings are axis-aligned
(D5); the realizer fills the yard remainder. **Era hook falsified (D9):** a second era (pick one
that breaks burgage assumptions — Roman insula grid or Edo machiya) ships with its own program
and at least one typology, at hamlet tier, before M5 is called done.

Validation: L2 (`SettlementMapTest`: every lot touches a way; no lot overlaps; no way through a
building; parcels inside the hull; slope cap per lot; the map is identical from `plan()` twice),
**L3** (the existing walkability gate run on the *map* via a rasterized stand-in before any voxel —
and again on the realized voxels), L4 (a city of 200 lots on the hills seed, Release, residents
on). Red tests: `SettlementMapTest.CityAxesFollowTerrain` fails (city ignores terrain);
`SettlementMapTest.WallIsNotTheSiteRectangle` fails. Stress: 400 lots (the cap), a site on a 15°
slope, a site bisected by an order-3 river (bridge inside town), a site on a coast. Test world:
`CityLab` (one meso tile, gentle hill + creek; the flat `StructGenTest` stays for the structure
forge). Perf gate: map bake per city < 2 s Release (headless).

### M6 — Lazy Realization (M) — *towns exist because you walked there*

Contract: a site's footprint enters **generation** as a density/terrain modifier in `sampleColumn`
(Minecraft jigsaw `terrain_adaptation`: beard-thin fill under, clear inside, per-ring ground
treatment from M5) so the ground is right in never-visited terrain and on far tiles; lots, streets
and walls are realized **on first stream-in**, per lot, through the existing structure forge /
`StreetPaver` / `TownWall`, with a per-lot ledger row (idempotent, resumable, refusal recorded with
the reason); the orchestrated job survives as `worldforge_build` = "pre-realize region"; packaged
games realize identically to the editor (they already stream; what they lack is the realizer host —
§13 M6). No frozen engine: realization is time-sliced on a core work-unit queue ticked from
`EngineRuntime::beginFrame`, never inside the chunk-landing hook synchronously.

Validation: L2 (`LazyRealizeTest`: realize lot k alone == lot k inside a full build — which
requires lot ownership by ledger row/uuid, not bbox-overlap deletion; refusal ledger exact), L4
(editor and packaged `--test` build visited from two directions: identical placed-object UUID set
and counts via a new test-API listing; frame-time histogram during realization within budget).
Red test: `LazyRealizeTest.PackagedGameHasTown` fails today because `GameShell` hosts no
`SettlementBuildService`/`MainThreadJobs`/`ResidentSpawner` (not because it does not stream). Stress: 50 sites
visited in one flight; evict + revisit × 100; a lot straddling a chunk column seam; the y = 31→32
vertical seam. Test world: `CityLab` packaged via `produce_game.py`. Perf gate: ms per lot,
frames > 33 ms during realization = 0 on Release.

### M7 — Horizon (M) — *what you see from the ridge*

Contract: far-terrain tiles render from the meso sheet (not per-column noise), show rivers (bed
colour + the water branch's far body tiles), roads, bridges, and **settlement massing** (coarse
building proxies from the M5 vector map, cube-as-atomic-LOD, residency-gated dither per
`LodTierLedger`), out to the macro horizon beyond 4096 u (a 5th ring at 32 u step from macro).
`LodTierLedger.md` updated in the same commit.

Validation: L2 (tile vs resident-chunk silhouette diff), L4 (pixel A/B at fixed poses, the
"phantom impostor trees through a flattened footprint" defect in frame before/after —
`WorldLookBacklog` A). Red test: `FarTileTest.RoadAndBridgeDrawn` fails (bridges pop in today).
Stress: 20 km view distance on the 4090 and on the RTX 1000 Ada laptop, numbers both recorded.
Perf gate: far-tile mesh time per tile; frame time at the stated pose.

### M8 — Lived-in (M, optional, last) — *names, owners, grudges*

Contract: residents (exist) are assigned from the M3 history + M5 lots; economy edges from
M3/M4 drive market stalls and sign text; an LLM reads the ledger (site history, resources,
neighbours, routes) and writes names, owners, faction and lore **as data** (D8) that signs,
dialogue and the journal consume; a critic pass validates the spec against the schema before it is
accepted (Actor/Critic pattern). W1/W2 (NPCs inside buildings, seated) stay out per the owner.

Validation: L2 (schema validation, determinism given a cached LLM response), L4 (a sign census:
every sign names a real owner in the ledger). Red test: `LoreTest.SignNamesLedgerOwner` fails
(signs are write-only today). Stress: 300 sites × 5 names, cache hit rate, no engine stall on
LLM latency (async, defaults fill first).

---

## 6. Feature Design Keys — answered up front (`docs/FeatureDesignKeys.md`)

- **Voxel aesthetic.** Terrain, beds, banks, road crowns, parapets, retaining walls and shelves
  are emitted at cube/subcube/microcube per the existing creek-shelf and parapet patterns; nothing
  new is a mesh. Buildings stay on the voxel grid (D5).
- **Chunks must not be visible.** Macro/meso tiles are cost units; every served field is bicubic
  across tile edges with a pinned apron, and every new `ColumnSample` field (`earthwork*`,
  `basinId`, `settlementGround`) gets the chunked-vs-whole seam test. Far tiles read the same
  sheet the chunks do.
- **Procedural pipeline stage order.** L0 sheet → L1 hydro geometry → biome/material (reads L0
  climate + L1 floodplain/beach) → decoration (reads L3 road + L5 footprint gates) → L5
  structures. A later stage never feeds an earlier one (the V1 preview self-feed gap is closed by
  construction).
- **Order independence.** Each layer is evaluable for one column/tile/site without any other's
  result; pinned per milestone.
- **Persistence.** Sheet params, civ params, route params, era in the recipe (`world_meta`); vector
  maps, history and per-lot realization in the ledger; baked sheets are a hash-keyed cache.
- **API.** §7 — units named, omitted = unchanged, state echoed, clamps explained in code.
- **Visual test + small world.** Every milestone names a one-tile lab world and fixed poses; the
  defect must be in frame before and after.

---

## 7. API and recipe surface (v2 additions; V1 routes unchanged)

| Route / tool | Purpose | Returns (echoed state) |
|---|---|---|
| `GET /api/worldforge/sheet?tile=x,z&level=macro\|meso` | the baked sheet tile as a PNG/JSON for inspection | cell size (u), bounds, hash |
| `POST /api/worldforge/plan` (exists) | now returns `civ{sites[], provinces[], history[]}` + `routes[]` | clamped params, plan hash |
| `GET /api/worldforge/site/{id}/map` | the M5 vector map (nodes/ways/areas, tags) | map hash, lot count, refusals |
| `GET /api/worldforge/site/{id}/ledger` | per-lot realization rows | realized / refused / pending |
| `POST /api/worldforge/realize {site\|region}` | pre-realize (the V1 job) | job id (existing job API) |
| `GET /api/worldforge/hydro?x&z` | `channelAt`, `basinAt`, `springSites` near a point (the §4 contract, for WaterCore's probes) | fields with units |
| `worldforge_map` / `worldforge_minimap` (exist) | overlay civ tiers, routes by class, vector maps | — |

M0 additions (ahead of the v2 block): `WorldForgeParams.version = 2` enables bounded earthworks
+ creek fords (absent/1 = legacy drape, pinned); `ColumnSample.earthwork{cutY, fillY, retaining}`
(world Y / world Y / bool); `POST /api/settlement/build` `allow_unwalkable` (bool, default false,
echoed as `walkability.gate ∈ blocked|overridden|passed`); `GET /api/world/terrain_height?source=
chunks|generator` (default `chunks`, echoed).

Recipe (`WorldRecipe::worldforge`, `version: 2`): `sheet{macroCellU, mesoCellU, continentCells,
erosion{steps, m, n, k}, upliftSpline}`, `hydro{minChannelDrainage, beachBandU}`,
`civ{tiersPer100km2{…}, spacingU{…}, epochs, pins[]}`, `routes{gradeCap, cutMaxU, fillMaxU,
reuseFraction, bridgeMaxSpanU}`, `settlement{era, density}`. Every field: unit in the comment,
clamp with its reason, echoed back by `plan`.

---

## 8. Validation plan (depth axis) and stress plan (scale axis), summarized

| Layer | Required depth | Red test(s) shown failing first | Stress axis |
|---|---|---|---|
| World sheet | L2 + L4 stats | `HydroEdgeTest`, `ColumnCostBench`, `MesoSeamTest` | continent 512 km, tile churn 10k |
| Hydro geometry | L2 + L3 banks + L4 | `BasinRimTest` (rim_leaks==0), `NoRightAngleJunctions` | order-6 trunk 100 km, lake on a tile edge |
| Civ plan | L2 + L4 map | `ThreeHundredSitesAtHundredKm` | 1000 sites, uninhabitable seed |
| Routes | **L3** + L4 | `GradeCap`, `MountainSeedZeroUnclimbable`, `OverNinetySixU` | 1000 routes, 1 km span, 60° cliff |
| Settlement map | **L3** (map + voxels) + L4 | `CityAxesFollowTerrain`, `WallIsNotTheSiteRectangle` | 400 lots, 15° slope, river through town |
| Lazy realization | L2 + L4 packaged | `PackagedGameHasTown` | 50 sites one flight, evict×100, chunk seams |
| Horizon | L2 silhouette + L4 pixel | `RoadAndBridgeDrawn` | 20 km on both GPUs |

Every "works" claim goes through the solution-auditor; every number through the
grounding-auditor; `ValidationLedger.md` gets a row per new placer/field.

---

## 9. Perf budgets (to be MEASURED red → green, on a named machine; no targets by vibe)

| Quantity | Today (where known) | Gate |
|---|---|---|
| Noise evals per column | **136.6 measured 2026-10-15** on the v1 path (`ColumnCostBench`, one eval = one base 3-D noise call; the audit's ~140 was an estimate) — and `sampleSurface()` is UNCACHED (the chunk-block cache serves `generateChunk` only), so every streaming-band query and `terrain_height?source=generator` call pays the full column | ≤ 25 — **GREEN on the sheet (M1b): 18.6** (`ColumnCostBench.FreshColumnCostsAtMost25EvalsOnTheSheet`); v1 pinned at 136.6 (D10) |
| Macro bake 200² | — | < 2 s Release (record) |
| Meso tile bake 256² | **76.2 ms Release** / 201.5 ms Debug (laptop, `WorldSheetGeneratorTest.MesoTileBakeTimeRecorded`, 2026-10-15) | < 50 ms on a worker, Release — **RED** (lever: the climate planes are 8 of the 18 evals per cell) |
| Civ + routes plan, 300 sites | all-pairs A* O(n²) at 8 sites today | < 10 s Release, off the main thread |
| Settlement map, 200-lot city | — | < 2 s headless |
| Streaming, saturated generation queue | **11.4 chunks/s Release, RTX 1000 Ada laptop, 2026-10-15** (v1 baseline) → 18.9 on the sheet with the FIFO column cache (10/15) → **52.3 chunks/s with the sheet + the LRU/SurfaceBlock caches, same pose/machine/config (2026-10-20, `m1e_fixed_profile_summary.json`, Release dc3c8376): 4.6× the v1 baseline; n = 1 per side.** Was "≥ 2×" and RED; **re-scoped by D13 to a REPORTED number** (the gate named a cost the column work does not own). PROFILED (`m1e_rwdi3_profile_summary.json`): worker `generateChunk` 138 ms + flora decorate 42 ms per chunk (3 workers ⇒ 16.7 chunks/s ceiling); column sampling ≈ 1 ms of it. The gate targeted the wrong cost; see the M1e paragraph for the recommendation |
| Hydrology bake, v2 (M2b-1, NEW) | **Debug: 11.6 s for the whole v2 recipe apply incl. the 1024×1024 32 u bake (the same apply with the 256×256 128 u bake: 3.8 s), RTX 1000 Ada laptop 2026-10-21, `m2b_red_green.txt`; Release: 3.43 s for the same apply (the 256x256 apply was ~0.5 s class), `m2b_release.txt`** - a real load-time cost, single-threaded Priority-Flood + 1 M height samples; the sampling half parallelises (later). Per-column cost is UNCHANGED by the finer bake: v2 sheet 1.7 us per fresh column at the gate pose (relief+meander+valley 0.5 us), 17x17 radius mean 1.8 us / 539 ms (M1e: 1.6-2.6 us / 560 ms). The Debug suite pays it once per distinct recipe key (process-wide memo). 56 macro rivers enter the box (62.7 km² injected, 51 propagate downstream; the 5 that do not land on sink cells — a lake or the sea at the border). |
| Worker time per chunk (NEW, measured) | **after the cache fix (2026-10-20): generateChunk 13.0 ms (column sampling 9.3 ms, 2.96 fresh blocks), flora decorate 39.3 ms; before it: generateChunk 138–158 ms of which column sampling 100–149 ms = 32 fresh blocks per chunk (cache thrash, not slow columns)** per chunk on one worker (RelWithDebInfo, laptop, baseline pose, `m1e_rwdi3_profile_summary.json`; 3 workers ⇒ a 16.6 chunks/s ceiling that the 17.8 measured EXCEEDS by ~10 %, so the per-chunk wall-clock includes pre-emption); main thread per landed chunk: remesh ≈ 11.3 ms, flush ≈ 12.9 ms, light occupancy ≈ 11.8 ms; the column-sampling share is measured by `worker_column_ms` (next run) | its own perf milestone (D13): a profile INSIDE `generateChunk` first (material resolution / subcube shelves / occupancy grids), then a gate set from that profile | (`m1_stream_baseline.json`: lab world, never-stored oak-woodland pose (300,109,−1400), `pending_generation` pinned at 44–48 for the whole 76 s ramp = generation-bound; frame p50 34 ms / p95 93 ms while streaming; n = 1; the instrument is the perf recorder's per-frame `streaming.resident_chunks`, so `f7_baseline.json`'s "no instrument exists" is stale). The old "7 chunks/min Debug" was a dense-forest pose; this is open woodland — not the same pose | ≥ 2× = **≥ 22.8 chunks/s** on this pose/machine/config, measured by the same script (`m1_stream_baseline_land.sh` procedure in the JSON) |
| Realization | 0.9 s per furnished tavern place | 0 frames > 33 ms during lazy realization |

Both the RTX 4090 desktop and the RTX 1000 Ada laptop are stated on every evidence row.

---

## 10. Numbers that need grounding — F4 AUDIT RESULT (grounding-auditor + PDF reads, 2026-10-14)

**Verdict: not ready to type numbers.** Two groups are grounded, the rest are REASONED, STRUCK or
need the named document read first. Nothing below enters code until its row says GROUNDED.
Verdict key: GROUNDED (source + value) · REASONED (chain stated, no direct dataset) · STRIKE (must
not be typed as a constant; derive or drop) · NEEDS-RESEARCH (source exists, not yet read) ·
MISMATCH (a shipped value contradicts the sources).

| Value | Source | Engine value / range | Verdict |
|---|---|---|---|
| **Road grade cap, highway (main route)** | Carreras et al. 2019 quoting Lawton 2004: loaded animal-drawn wagons "could seldom deal with gradients larger than 5 %"; Encyclopaedia Britannica 9th ed. "Roads and Streets": 1 in 30 "the ruling gradient to be aimed at on first-class roads", Telford's Holyhead road ([Wikisource](https://en.wikisource.org/wiki/Encyclop%C3%A6dia_Britannica,_Ninth_Edition/Road)); Roman wagons "seldom" > 5 %, Hispania 6–8 % (2019 review of Roman mountain transport, [PDF](https://recercat.cat//bitstream/2072/369267/1/2019-Land-transport-mountainous-regions-roman-empire-Postprint.pdf) — figures from the search summary, ⚠verify against the PDF) | **0.05** (3.3 % lower bound for best practice) | GROUNDED (⚠verify Roman PDF) |
| **Road grade cap, road (cart)** | Carreras, de Soto & Muñoz 2019 (JAS:Reports 25) quoting Moreno 2006: cart roads 6–8 %; wheel-rut sites found at 6.7–10 % (where ruts occur, not a design limit). **F4b: the "Herzog 8–10 %" attribution is unsupported and is struck.** | **0.08** = top of Moreno's 6–8 % | GROUNDED (secondary extract; ⚠ full text still unread, DOI 10.1016/j.jasrep.2019.04.011) |
| **Road grade cap, track (foot / pack)** | USFS Forest Trail Handbook: pack-animal ruling grade 15 %, ideal 10 %, short pitches steeper ([sec 6](https://npshistory.com/publications/usfs/forest-trail-handbook/sec6.htm)); USFS notebook: mules manage > 10 %, even 20 %, at erosion cost ([FS](https://www.fs.usda.gov/t-d/pubs/htmlpubs/htm07232816/page08.htm)) | **0.15** sustained, ≤ 0.20 short pitch | GROUNDED by modern analogy (state it) |
| Shipped `kMaxRoadGrade = 0.5` (`WorldForgePlan.h:131`) | 50 % = 26.6°, above every cap above; the 0.69 % unclimbable steps on mountains is the symptom | replace by the three per-class caps; expect far more switchbacks and cuts (budget it) | **MISMATCH** |
| Agent step-up 4/9 m (`AnimatedVoxelCharacter.cpp:1611`, clamp 0.34–0.70) | engine physics; real stair risers: IRC R311.7.5.1 max 7¾ in = 196.85 mm ([Viewrail code guide](https://resources.viewrail.com/code-compliance/stair-code/tread-depth-and-risers)); UK Approved Document K private stairs max 220 / 200 mm ([BWF](https://www.bwf.org.uk/question-of-the-week/what-are-the-current-recommendations-for-the-rise-and-going-of-stairs/)) — **F4b GROUNDED** | keep 4/9 as the *unclimbable* floor; never use it as a comfort criterion | GROUNDED (engine) / CONVENIENCE vs real stairs |
| Embankment side slope | angle of repose, Pocket Ref via [Wikipedia](https://en.wikipedia.org/wiki/Angle_of_repose): soil 30–45°, dry sand 34°, gravel+sand 25–30°, crushed stone 45°, wet clay 15°; a built slope is flatter by a safety factor. **F4b: AASHTO/NJDOT 1V:3H–1V:4H figures are vehicle run-off-recovery thresholds, the WRONG FRAME for a pre-industrial bank — do not use them**; a geotechnical repose table (USACE EM 1110-2-1913 or NAVFAC DM-7) is still to be read. `materials.json` has NO repose column (friction is a contact coefficient) | per-material repose table to be added to `materials.json` with the Pocket Ref citation; built slope = repose − safety factor | REASONED (safety factor unsourced) |
| Max cut depth / fill height before a retaining wall | none found (medieval dry-stone revetment practice, FHWA/AASHTO embankment guidance: NEEDS-RESEARCH) | **STRIKE as constants.** Derive: cap = (lateral room before the parcel-clear margin) × tan(built slope); beyond it a dry-stone retaining wall is required | STRIKE → derivation |
| 3 u grading shoulder (`WorldGenerator.cpp:1067-1079`) | a constant regardless of cut height | **STRIKE as a constant**; shoulder = cut depth / tan(built slope) | STRIKE → derivation |
| Bridge clearance over water by river order | none found | **STRIKE.** V1 already sets the deck at the higher bank's surface (medieval bridges met road grade); if a freeboard is ever wanted it is `channelDepth` + a sourced freeboard | STRIKE |
| Stone bridge span, pier interval | [Alcántara Bridge](https://en.wikipedia.org/wiki/Alc%C3%A1ntara_Bridge) spans 13.8–28.8 m (⚠verify via Structurae) | shipped 12 u pier interval is conservative (real stone spans 14–29 u) | GROUNDED (span) / REASONED (12 u) |
| Timber span (medieval) | Kent HER Sherway Bridge scheduling record ([DKE19269](https://heritage.kent.gov.uk/Designation/DKE19269)): medieval single-arch bridges "typically 3m–6m in span … most commonly using timber"; Hemington (Leicester) ~10 m between piers, deck ~2.8 m wide and Aldwincle ~25 m overall (search extracts only) | single span 3–6 u; piered ~10 u interval — the shipped 12 u pier interval is close | GROUNDED (single span) / REASONED (piered) |
| "96 u max span" (`WorldForgePlan.cpp:604-611`) | it is a bank-search horizon over a piered deck (8 spans at 12 u), not a span; Alcántara is ~180–194 m long | relabel **max deck length**; raise after M4's deck model lands | REASONED → relabel |
| Route re-use discount | Veloren builds only if cheaper than existing/3 (`civ/mod.rs`, not read this pass); Emilien 2012: `C'(R) = w_ex·C(R)` on existing roads with `w_ex ≪ 1`, **no numeric value published** (PDF read 2026-10-14) | the *existence* of a discount is grounded (path dependence); the fraction is a **tunable labelled "game-tuned, Veloren precedent"** with a sensitivity run | REASONED / STRIKE as "grounded number" |
| Settlement hierarchy ratio | Christaller central-place theory K=3 market / K=4 transport / K=7 administrative ([Wikipedia](https://en.wikipedia.org/wiki/Central_place_theory)) | each tier up has K× fewer centres; K=3 default | GROUNDED (ratio) |
| Settlement spacing per tier (km) | Christaller 1933 fig. 1 p. 66 as reproduced in Buchholzer-Rémy 2006 (*Une ville en ses réseaux: Nuremberg à la fin du Moyen Âge*) and quoted in Radeff's review (PDF read 2026-10-14): nested circles of **12, 21, 36 km**, and around Nuremberg **21, 36, 62, 108, 188 km** — a √3 ladder (K=3) | tier spacing ladder 12 / 21 / 36 / 62 / 108 km ascending (hamlet–village–town–city–capital mapping is ours); the 7 km bottom rung is 12/√3, inferred | GROUNDED (ladder, secondary) / REASONED (7 km, tier mapping) |
| Settlement density | F4b: IHR market gazetteer (search extract) ~1,746 markets in England 1300, ~2,348 in 1400 (≈ 1.3–1.8 markets / 100 km², our division) — a *market* tier figure, no vill/borough split; Darby unread. Domesday Book: > 13,400 named places in 1086 (Domesday Gazetteer via [ADS](https://archaeologydataservice.ac.uk/catalogue/adsdata/arch-6172-1/dissemination/Records_43/rob_43_0_209.pdf)); ÷ England ≈ 130,000 km² (⚠verify) ≈ **10 places / 100 km², mean spacing ≈ 3 km** (our arithmetic, not a published density). Plan T2's 300 sites / 10,000 km² = 3 / 100 km², ~3× sparser: label **"gameplay-thinned"** | tier targets per 100 km² NEEDS-RESEARCH (Darby *Domesday England* 1977; Dyer on medieval towns) | REASONED |
| River width / depth vs drainage area | F4b: Leopold & Maddock PP252 is a JBIG2 scan — **rendered and read page by page 2026-10-20 (§10 F4f)**: p. 16 gives the downstream averages b = 0.5, f = 0.4, m = 0.1 (primary, replaces the OFR 77-38 extract). `TerrainGenerationV2.md:356-357`: Doll et al. NC Coastal-Plain regressions W = 10.97·A^0.36 (r² 0.87), D = 1.29·A^0.30, chained through a Horton area ratio ≈ 4 to get the per-order table 2/3/5/8/14/22 u — the doc itself labels the chaining "design synthesis". Leopold & Maddock 1953 ([USGS PP252](https://pubs.usgs.gov/publication/pp252)) exponents w∝Q^0.5, d∝Q^0.4 READ (F4f). **Doll units CONFIRMED from the scan (F4f): DA in mi², W/D in ft → W = 2.37·A^0.36 m, D = 0.296·A^0.30 m with A in km², valid 0.57–417 km².** | regression GROUNDED; per-order chaining REASONED (F4f checks the table against the metric law: it IS a sample of the regression); the 1/1/1 depth plateau at orders 3–5 is grid convenience (real ≈ 1.0/1.5/2.3 m) — M2 moves to drainage-area-driven width/depth and drops the table | GROUNDED (regression) / REASONED (chaining) |
| Tributary junction angle | [PMC9897458](https://pmc.ncbi.nlm.nih.gov/articles/PMC9897458) (read): global mean junction angle **61°** for near-equal tributaries (A_R ≥ 0.99); groundwater-seepage model tends to 72° in humid climates; angles widen as one tributary outgrows the other. **Citation corrected 2026-10-20 (M2 grounding pass, row 3d):** the "49.5–75.0°" figure is NOT in PMC9897458 (Hooshyar 2017 appears only in its reference list) — it is quoted in [ESurf 13:219 (2025) §1](https://esurf.copernicus.org/articles/13/219/2025/) as a range of network MEAN angles (Hooshyar 2017), beside Seybold 2017's 45–72°; the same paper's §3.1.6 measures CONUS valley-bottom means of 35–45° (a different angle definition — a source MISMATCH). Rule (ESurf §2.1.1, Howard): **cos θ = S_main / S_trib**, "nearly exact for all slopes smaller than ≈ 60°". Howard 1971 and Horton 1932 themselves unread | M2 test: **network mean in 45–72°, no junction at 90°**; the per-junction band [40°, 80°] is WITHDRAWN (too tight against the CONUS means) | GROUNDED (rule + observed means; clamp of the rule is DESIGN) |
| Beach band width | none | **STRIKE** the width; derive the band from the shelf slope × tidal/wave run-up once sourced | STRIKE |
| Nearshore / foreshore slope | F4b: USACE CEM returned 403; the grain-size profile tables are likely in EM 1110-2-1100 **Part V** (beach fills) as well as Part III — read both. Qualitative only: coarser grain = steeper ([Coastal Restoration Trust](https://ref.coastalrestorationtrust.org.nz/documents/relationship-between-grain-size-size-sorting-and-foreshore-slope-on-mixed-sand-shingle-beaches/)); King 1972 range 1:5–1:100. Read USACE EM 1110-2-1100 Part III | — | NEEDS-RESEARCH |
| Emilien 2012 interest-map parameters | **PDF read 2026-10-14**: functional forms only — per-building-type bells with λmin/λ0/λmax for road distance, slope, water; attraction–repulsion for sociability/worship; open-distance for fortification; ~150 parameters per village type in an unpublished "building encyclopedia". **Table 1 is computation time, not parameters** (rev-1 plan text corrected). | forms GROUNDED; every value must come from OUR grounded sources: slope bells from the per-typology relief caps already in `settlement_program.json` (with their `sources`), road-distance bells from burgage frontage/setback data there, water bells NEEDS-RESEARCH | GROUNDED (form) / derive (values) |
| Road widths 3/5/6 u (`WorldForgePlan.h:145` "grounded in settlement_program.json" — an internal file) | F4b: Historic England summary (search extract) Roman roads 5–10 m overall width; Ermine Street agger 6.5 m wide excavated; Twelve Tables 8 Roman ft ≈ 2.4 m is a *carriageway*, not overall width; Margary unread | the shipped 3/5/6 u sit below the 5–10 u overall range; the highway class should widen to ≥ 5 u once Margary is read | REASONED (partial) |
| Terrace skirt 4 cubes at 1:1 (`SettlementBuildService.cpp:406`) | 45° is the top of the soil repose range; aggressive as a built slope | keep width as tuning; flag the 45° and tie the ramp to the repose table above | REASONED |
| Parcel-clear margin 4, vegetation flood 16 | heuristics / search bounds, not dimensions | label as heuristics in code | UNSOURCED (label) |
| Canopy margin 8 cubes (`:1351`) | **F4b read the tree library**: template `# bounds:` widths — `tree_redwood_xl` 52×44, `tree_elder_oak_xl` 49×50, `forge_elder_oak_xxl` 41×42 (megaflora, half-width ≤ 26); `forge_enchanted_oak` 23×23 (≈ 11.5); `forge_jungle_l` 19×18; `forge_acacia_l` 17×17; `tree_oak_l` 16×14 (8). `ProceduralTree.h` has no crown constant | 8 covers only ordinary oaks; enchanted oak, jungle, acacia and all megaflora overhang a parcel cleared at 8 | **MISMATCH** → M0 row: margin derived from the biome's template bounds (max half-width of the biome's flora pool), megaflora handled by an explicit exclusion radius |
| `kMinCorridorWidthCubes = 2` (`SettlementWalkability.h:48`) | agent box 5 micro + boundary margin (its own comment) | keep | GROUNDED (engine character) |

**F4b — second pass run 2026-10-14 (grounding-auditor + PDF extraction):** CLOSED: stair riser, medieval timber span, junction angle (observed mean), Christaller ladder, canopy margin (MISMATCH found); PARTIAL: Roman grades (secondary extract, Herzog struck), road widths (Historic England extract), settlement density (markets only), hydraulic-geometry exponents (secondary); STILL OPEN: Margary full text, Darby per-tier density, USACE CEM Part III/V foreshore table, a geotechnical repose table (USACE EM 1110-2-1913 / NAVFAC DM-7 — AASHTO is the wrong frame), Howard 1971's own angle range, the Carreras 2019 full text. Saved PDFs for OCR: PP252 scan at `tool-results/webfetch-1792001093556-rhhpvi.pdf`. The original list: Emilien 2012 (done); Roman mountain transport
2019 PDF (verify the 5 / 6–8 / 8–10 % figures); Christaller 1933 or a secondary with the spacing
table; Darby 1977 for per-tier density; USACE EM 1110-2-1100 Part III for foreshore slope; USGS
PP252 exponents; Howard 1971 junction angle; AASHTO Roadside Design Guide for built embankment
slope; Historic England on medieval timber spans; Margary for Roman road widths; `ProceduralTree`
max crown radius. Rows marked ⚠verify were read from search summaries, not the documents.

**Consequence for M0:** the earthworks row types **no cut/fill constants** — it derives the shoulder
and the wall trigger from the repose table and the parcel margin, and replaces `kMaxRoadGrade` with
the three per-class caps (0.05 / 0.08 / 0.15) once the Roman PDF is verified (F4b). The ford row
has no new number. Everything else in M0 is number-free.

**F4c — M1 drainage-density grounding (grounding-auditor, 2026-10-15; nothing below is typed as a
constant until its row says GROUNDED).** Raw verdict: *"GROUNDED are only the principle that Dd depends on
the channel-initiation threshold and the m/n range 0.35-0.6 (Whipple & Tucker 1999, via an ESurf preprint).
REASONED are the absolute Dd range, the 2.7 km/km2 grid ceiling, Rb 3-5, m=0.4/n=1 and the Rb sample-size
rule. NEEDS-RESEARCH are the Dd x sqrt(A_c) band, Rl, and K and uplift. STRIKE are absolute Dd and
valley-count constants."*

| Value | Source | Engine value / range | Verdict |
|---|---|---|---|
| Drainage density Dd = ΣL/A, humid-temperate | Horton 1932/1945 (definition); Gregory & Walling 1973 *Drainage Basin Form and Process* (⚠ table unread); NetMap help page quotes 2–5 km/km² typical all-channel, other guides 2–12 semi-arid→humid | wide, ~1–12 km/km²; set by the extraction threshold, never one number | REASONED |
| Dd depends on the channel-initiation threshold | Montgomery & Dietrich 1989 WRR 25 (⚠verify year); Tucker & Bras 1998 WRR 34(10); Tucker, Catani, Rinaldo & Bras 2001 Geomorphology 36 (DOI 10.1016/S0169-555X(00)00056-8); channel-head review Frontiers Earth Sci 10.3389/feart.2021.755197 | any Dd band is a function of `hydro.minChannelDrainage` (unit stated at the clamp) | GROUNDED (principle) |
| Dd × √A_c dimensionless band | Horton 1945 length-of-overland-flow Lo ≈ 1/(2Dd) ⇒ Dd ∝ 1/√A_c (auditor's derivation) | no sourced constant | NEEDS-RESEARCH: Tarboton, Bras & Rodríguez-Iturbe 1992 WRR 28(4); Tucker 2001 §3; Gregory & Walling ch. 4 |
| Dd ceiling on a 512 u grid | arithmetic: ≤ ~1.4 cell-widths of channel per cell ⇒ ≤ 1.4/0.512 km | ≤ ~2.7 km/km² — sanity assert only | REASONED |
| Horton bifurcation ratio Rb | Horton 1945; Strahler 1957 Trans. AGU 38:913–920 (⚠unread); modern checks Peckham 1995, Tarboton 1996 J. Hydrol. (unread) | natural dendritic 3–5, mean ~4; min 2 | REASONED (→ GROUNDED after Strahler 1957 or Tarboton 1996 is read) |
| Length ratio Rl | Horton 1945; no numeric band found | none | NEEDS-RESEARCH (Strahler 1957) — do not assert |
| Rb sample-size floor | auditor's design choice: ≥ 3 Strahler orders and ≥ 10 streams in the lowest order; an 8 km lab sheet (16×16 macro cells) has ~2 orders | assert Rb on the 200×200 continent only | REASONED (unsourced floor) |
| Stream-power m/n (concavity) | Whipple & Tucker 1999 JGR 104(B8), DOI 10.1029/1999JB900120; ESurf preprint esurf-2018-7 reads 0.35–0.6 | m/n ∈ [0.35, 0.6], clamp with this reason | GROUNDED (range) |
| m = 0.4, n = 1 | inside the band; Landlab defaults m = 0.5, n = 1; Cordonnier 2016 / Veloren `erosion.rs` values NOT confirmed (unread) | 0.4 / 1 as a recipe default — not "derived", not "Veloren's value" until read | REASONED |
| m/n = 0.4 vs 0.5 | ESurf 5:807 (2017): without hillslope diffusion, 0.5 gives stretch-invariant landscapes | keep 0.4; the macro bake has no diffusion term (confirm) | REASONED |
| Erodibility K, uplift U | none | recipe fields, no grounded value | NEEDS-RESEARCH (Whipple & Tucker 1999 K calibration; Cordonnier 2016 Table 1) |
| Absolute valley/channel count per km² on the 8 km lab | — | — | STRIKE (scales with threshold, grid, seed; sample too small) |
| Absolute Dd constant (e.g. "3 km/km²") | the 2–12 range above | — | STRIKE as a constant; relative form only |

**F4d — primary-source read for M1c (grounding-auditor, 2026-10-15, 61 tool calls).** Raw verdict:
*"GROUNDING VERDICT: PARTIAL. Grounded: K units, the K lithology range (abstract), m/n in [0.35, 0.6],
Dd ∝ A_c^-η (η ≈ 0.5, secondary), and all Veloren values as code readings. The implicit n = 1 update
is confirmed only through Fastscapelib and Landlab code. Not grounded: K and U as engine constants,
any Cordonnier value, any Dd × √A_c band, and the Braun & Willett, Whipple & Tucker, Tarboton and
Tucker primary text."* What failed to open, so nothing is quoted from it: Whipple & Tucker 1999 (a
scanned image PDF, no text layer — needs OCR), Cordonnier 2016 (every mirror over the fetch size cap
or access-denied), Braun & Willett 2013 (paywalled), Tarboton 1992 / Tucker 2001 (abstracts only).
**Owner ask:** a locally saved copy of any of those four lets the rows below close.

| Value | Source | Engine value / range | Verdict |
|---|---|---|---|
| K units L^(1−2m)/T | Fastscape docs ("meters (to the power 1−2m) per year"); Harel et al. 2016 abstract; Landlab | m^(1−2m)/yr with A in m²; m^0.2/yr at m = 0.4 | GROUNDED (units, secondary) |
| K magnitude by lithology (m = 0.4) | Stock & Montgomery 1999 JGR 104, abstract (earthref.org/ERR/42355): "10⁻⁷–10⁻⁶ m^0.2/yr granite/metamorphic, 10⁻⁵–10⁻⁴ volcaniclastic, 10⁻⁴–10⁻² mudstones" | any typed K must fall inside the lithology it names | GROUNDED (range); lithology→material mapping NEEDS-RESEARCH |
| m = 0.4, n = 1 | Veloren `sim/mod.rs` `theta_func = 0.4`, `n_func = 1.0` ("we choose m = 0.4 and n = 1"); Landlab/Wikipedia "m/n around 0.5" | 0.4 / 1 | GROUNDED as Veloren's choice; still REASONED as physics |
| Implicit n = 1 update | Fastscapelib `spl.hpp` (`factor = k·dt·A^m / dist`, `h = (num + factor·h_r)/(1+factor)`, bottom-up over receivers first); Landlab `fastscape_stream_power.py` (`alpha = K dt A^m / L^n`, downstream-to-upstream order) | the engine's form | CONFIRMED in two implementations; the paper's own equation unread |
| Steady-state slope S = (U/K)^(1/n) A^(−m/n) | one-line rearrangement of ∂h/∂t = U − K A^m S^n | sizes K from U and a target slope | REASONED |
| Veloren K / U / dt / steps | `kf_func` land 1e-6 × grid_scale^(−2m); `dt = max_uplift / 1e-3` (⇒ peak U 1e-3 m/yr, an inference); `n_steps = 100`, Gauss-Seidel ≤ 99 sweeps to tol; 8-receiver MFD; slope cap tan 15°–tan 60° by rock strength; kd 1e-2/4 m²/yr | reference only; Veloren's units are game units | GROUNDED (code reading), not physics |
| Dd ∝ A_c^−η | HESS 28:3119 (2024) quoting Tarboton 1991 ("inversely related to A_o") and Moglen 1998 (η = 0.45 ± 0.04 over 14 networks; η = 0.5 by dimensional consistency) | Dd × √A_c ≈ constant per landscape | GROUNDED (exponent, secondary) |
| Dd × √A_c constant | one catchment (Brushy: A_o 0.14 km², ρ 0.97 km⁻¹ ⇒ ≈ 0.36) | do not assert | NEEDS-RESEARCH |
| U (rock uplift rate), engine default | none | recipe knob only | NEEDS-RESEARCH |

**Recommended, as design choices labelled as such (not sourced):** type a target steady-state slope and
a reference area in the recipe and DERIVE K (worked example: U = 2e-4 m/yr, S_ref = 0.1 at
100 km² ⇒ K ≈ 1.3e-6 m^0.2/yr, inside the granite–volcaniclastic bands); carry which unit A is in
(cell² vs m²: K_cell = K × 512^−0.2 ≈ 0.287 K at 512 m cells); run to steady state with a tolerance and
RECORD the step count rather than fixing it; a slope cap must handle 1-cell sources (U/(K·A^0.4) is
unphysical there). None of that is typed yet.

**F4e — the primary sources READ (2026-10-15, owner: "if you can download those pdfs do it").** Cordonnier
2016 (Purdue PDF, text layer) and Whipple & Tucker 1999 (UChicago PDF; it HAS a text layer — the F4d
"scanned, no OCR" finding was wrong) were downloaded and read by the grounding-auditor from the extracted
text. Braun & Willett 2013 (paywalled, no open copy found) and Tarboton 1992 (the USU records carry no
file) still wait. Raw verdict: *"Now GROUNDED (as the paper's published choice, with the quote): n = 1
and m = 0.5; m/n ≈ 0.5, measured concavity range 0.40–0.49; k = 5.61e-7 yr⁻¹ (tuned, not measured);
U = 5e-4 m/yr (design choice); Δt = 2.5e5 yr (design choice); steps ≈ 200 (50 for shape, 100–300 for
stability); hmax = 2.244 u/k (empirical fit; the paper's 'km' is a unit slip, metres in practice); 30°
talus ceiling, 6°–54° Perlin-modulated bounds (aesthetic choice)."*

| Value | Source (quote) | Engine value | Verdict |
|---|---|---|---|
| n = 1, m = 0.5 | Cordonnier §3.1: "the ratio m/n is constrained by the shape of the stream profiles and is thought of being m/n ≈ 0.5 [WT99]. As in most geomorphological studies, we use n = 1 and m = 0.5." W&T Table 1 concavity 0.40 ± .10 / 0.49 ± .10 / 0.41 ± .10, Table 2 m/n 0.50 | `erosionM = 0.5` (was 0.4), n = 1 fixed by the implicit solver | GROUNDED (published choice; measured band 0.40–0.50) |
| k = 5.61 × 10⁻⁷ yr⁻¹ | Cordonnier §6: "We set the erosion rate to k = 5.61 × 10⁻⁷ y⁻¹ for mountains to culminate at about 2000 m." Units: W&T Table 2 "[m^(1−2m) yr⁻¹]", "the dimensions of K depend on m" ⇒ yr⁻¹ at m = 0.5 with A in m² | `erosionK = 5.61e-7` | GROUNDED as the paper's tuned value; K is never a measurement (W&T: "treated as a constant", "greatly needed" research) |
| U = 5.0 × 10⁻⁴ m yr⁻¹ | Cordonnier §6: "the maximum tectonic uplift to U = 5.0 × 10⁻⁴ m y⁻¹ (meters per year), which is the average uplift among earth mountains" (no citation attached); W&T Table 2 reference U = 2 mm/yr | `erosionUpliftMax = 5e-4` at the uplift field's maximum | GROUNDED (design choice with an uncited gloss) |
| Δt = 2.5 × 10⁵ yr, ~200 steps | Cordonnier §6: "δt = 2.5 × 10⁵ y to ensure a fast convergence while avoiding the appearance of high unnatural cliffs"; §6.3: "Convergence is obtained after around 200 time steps"; "fully shaped after 50 iterations and the geometry stops evolving after 100 − 300 iterations"; §3.1 validity "between 10⁵ and 10⁷ years and a few tens to hundreds of kilometers" (200 × 2.5e5 = 5e7 yr exceeds it: Δt is a convergence knob, not physical time — stated) | `erosionDt = 2.5e5`; the lab runs 200 steps; the shipped default stays `erosionSteps = 0` until the lab L4 has run | GROUNDED |
| hmax = 2.244 u/k | Cordonnier §6.4: "only their ratio is relevant … the height in kilometers follows the rule hmax = 2.244u/k" — with their u/k = 891 m this gives 2000 (their stated ~2000 m), so the unit is METRES (a slip in the paper) | predicts ~2000 m ranges; the engine REMAPS the eroded relief monotonically into the height spline's [sea, max] range (design choice (a) of the read: least extrapolation; the drainage pattern lives in the receivers, not the absolute height) | GROUNDED (fit) + a stated DESIGN choice for the vertical budget |
| Talus ceiling 30° | Cordonnier §5: "if the result of the stream power equation leads to slopes higher than 30°, we reduce the change of elevation so as to keep slopes in the prescribed range"; §6: "30°, which is the usual talus angle for thermal erosion [MKM89]"; their 6°–54° Perlin modulation is aesthetic | `erosionMaxSlope = tan 30° = 0.577` as a per-receiver ceiling; the noise modulation is M2 | GROUNDED (paper's rule; MKM89 unread) |
| Uplift-erosion number N_E | W&T §3: "the dynamic behavior … is governed by a single nondimensional group that we term the uplift-erosion number"; "changes in the rock uplift relative to base level U are dynamically equivalent to changes in the coefficient of erosion K" | supports "only U/K matters"; the exact Eq. 19 is OCR-damaged and NOT transcribed | GROUNDED (qualitative) |
| Absolute geophysical K, MKM89, B&W 2013 text, Tarboton 1992 Dd band | — | — | still NEEDS-RESEARCH (no open copies) |

**What `MacroErosionTest` may assert today:** structure (tree, no sinks, every receiver ≤ its donor);
Dd ∈ [0, 2.7] km/km² at 512 u; Dd decreases monotonically as `minChannelDrainage` rises over ≥ 3
thresholds; Rb ∈ [3, 5] as a soft continent-scale assert under the sample-size floor. The first red run
PRINTS the measured Dd × √A_c; the numeric band is chosen from the NEEDS-RESEARCH reads, never from that run.

**F4f — the hydraulic-geometry sources READ (2026-10-20, M2 grounding; owner: "if you can download those
pdfs do it").** The M2 grounding pass saved two scans — USGS PP252 (Leopold & Maddock 1953, 64 pages, JBIG2,
no text layer) and three pages of Doll, *Stream Restoration Technical Guidebook — Coastal Stream Amendment*
(NC State, 2004 per the PDF metadata: Fig. 2 and Tables 1–2) — plus USGS PP282-B (Leopold & Wolman 1957, *River
Channel Patterns: Braided, Meandering and Straight*, 50 pages, JBIG2; pp. 58–60 read). They were rendered to page images with pymupdf and read page by page; nothing below is from a search summary.
Unit conversions are our arithmetic (1 mi² = 2.58999 km², 1 ft = 0.3048 m, 1 cfs = 0.0283168 m³/s).

| Value | Source (read) | What it says | Status |
|---|---|---|---|
| Doll regression units + coefficients | Doll 2004 Fig. 2a–d (NC Coastal Plain, 16 reaches) | A_bkf = 14.52·DA^0.66 ft² (R² 0.88); Q_bkf = 16.56·DA^0.72 cfs (R² 0.90); **W_bkf = 10.97·DA^0.36 ft (R² 0.87); D_bkf = 1.29·DA^0.30 ft (R² 0.74)**, DA in **square miles**. Metric: **W = 2.37·A^0.36 m, D = 0.296·A^0.30 m, Q_bkf = 0.236·A^0.72 m³/s, A_bkf = 0.720·A^0.66 m²**, A in km² | GROUNDED (read; the units question that blocked the M2 auditor is closed) |
| Doll validity range | Doll 2004 Table 2 | DA 0.22–161 mi² = **0.57–417 km²**; W 8.3–103 ft (2.5–31 m); D 0.8–6.7 ft (0.24–2.0 m); slope 0.005–0.735 %; Rosgen E/C/F; W/D 5–19 | GROUNDED (range) |
| L&M downstream exponents | PP252 p. 16 (printed), eqs. (1)–(3) | w = aQ^b, d = cQ^f, v = kQ^m with **b = 0.5, f = 0.4, m = 0.1** as "average values of the exponents for river basins studied"; b + f + m = 1 by continuity (Q = wdv); p. 51: b so consistently ≈ 0.5 downstream "is not known and constitutes an important unsolved problem"; p. 14: the rates "are of the same order of magnitude for rivers of different sized drainage basins". Q is **mean annual** discharge at each station, not bankfull | GROUNDED (read; replaces the ⚠verify recalled row) |
| The two families are one | arithmetic on the rows above | W ∝ Q^0.5 with Q ∝ A^0.72 (Doll) gives W ∝ A^0.36 — Doll's width exponent exactly; D ∝ Q^0.4 gives A^0.288 vs Doll's 0.30. So the Doll power laws may be **extrapolated past 417 km²** to the order-6 trunk (a 100 km continent's trunk drains thousands of km²) with L&M's exponents as the justification | GROUNDED (derivation on read sources) |
| Regional intercept (the W/D lever) | PP252 p. 14 + Fig. 9 (p. 15) | at 1,000 cfs mean annual: Tombigbee (Alabama coastal plain) depth ≈ 7.5 ft, width ≈ 110 ft; Republican + Kansas (Great Plains) ≈ 2.3 ft / ≈ 250 ft — "the intercepts of the graphs ... differ greatly among rivers but the slopes of the lines are similar in magnitude". M2's biome/lithology knob is therefore the **intercept only** (W/D ≈ 15 → ≈ 109 between those two), never the exponent | GROUNDED (read) |
| Shipped per-order table 2/3/5/8/14/22 u (`FlowField.cpp:92`) vs the law | inversion of W = 2.37·A^0.36 | the table samples A ≈ 0.62 / 1.9 / 8.0 / 29 / 139 / 488 km² (ratio ≈ 3.8 per order; order 6 is 17 % past 417 km², covered by the extrapolation row). Doll depths at those areas 0.26 / 0.36 / 0.55 / 0.82 / 1.30 / 1.90 m vs shipped 0.33 / 0.66 / 1.0 / 1.0 / 1.0 / 2.0 u: orders 3–4 rounded UP to the voxel, order 5 is 0.3 u shallow | GROUNDED (the table is a sample of the regression); the rounding is the known grid-convenience MISMATCH — M2 carves the continuous law on the 1/3 u grid and the table becomes the fallback only |
| Manning form | PP252 p. 14 eq. (4) | v = (1.5/n)·d^(2/3)·s^(1/2) in ft-s units (1.5 = rounded 1.486); the water core already runs the metric 1/n form (the suite's Manning depth/speed checks) — no change | GROUNDED (read) |
| Junction rule + test criterion | ESurf 13:219 (2025) §2.1.1 / §1 / §3.1.6 and PMC9897458 (auditor read) | cos θ = S_main / S_trib ("nearly exact" below ≈ 60°); network MEANS 45–72° (Seybold 2017), 49.5–75.0° (Hooshyar 2017), global mean 61° for near-equal tributaries, CONUS valley-bottom means 35–45° | GROUNDED (rule + means); per-junction band [40°, 80°] WITHDRAWN — M2 asserts the network mean in 45–72° and no 90° junction (row above corrected) |
| Runoff for Q = R·A | USGS HA-710 (Gebert, Graczyk & Krug 1987) and WRI 88-4094 — search summaries only | humid eastern US ≈ 250–1400 mm/yr | GROUNDED (range, ⚠ summary); the default inside it is a recipe parameter (DESIGN) — never a constant in code |
| Meander wavelength | PP282-B p. 58–59, Fig. 45B | **λ = 6.5·w^1.1 (ft)**, "the ratio of wavelength to bankfull width varies from about 7 for small streams having widths of 1 to 10 feet, up to 15 for large rivers having widths in excess of 1,000 feet"; the discharge form λ = 36·Q^0.5 (Inglis mean line, ft / cfs) has more scatter — wavelength "is more directly dependent on width than on discharge". Metric: **λ = 7.3·w^1.1 m**. The engine's `kMeanderFreq` comment (`WorldGenerator.cpp:166`) gives λ ≈ 55 m ≈ 11 w for the ~5 m order-3 channel; the law gives 43 m (8.6 w) at w = 5 m — 28 % long, inside Fig. 45B's scatter | GROUNDED (read); the shipped 55 m is REASONED and should become λ(w) from the carved width in M2 |
| Braided-vs-meandering discriminant | PP282-B p. 59–60, Fig. 46, eq. (1) | **s = 0.06·Q^−0.44** (Q bankfull, cfs) separates braided (above) from meandering (below); straight channels occur throughout. Metric: s = 0.0125·Q^−0.44 (Q in m³/s). A "meander" is sinuosity ≥ 1.5 — stated by the authors to be "an arbitrary" cut (matches the TerrainGenerationV2 sinuosity row) | GROUNDED (read); the M2 use is a pattern CLASS per reach (braid/meander/straight), the geometry of braids is out of M2 scope |
| Floodplain / meander-belt multiple | none read for the BELT (Williams 1986 *J. Hydrol.* 88:147; LWM 1964); PP282-B pp. 58–60 give wavelength, not amplitude or belt width (its only floodplain figure is an anecdote: Green River near Daniel, 600 mi² / 2,000 cfs, "a broad alluvial plain about 2 miles wide") | the shipped `kValleyWidthMul = 5.0` stays a labelled tunable; 10–20 is NOT adopted | NEEDS-RESEARCH |
| Beach face slope by grain size | **READ 2026-10-21** (the M2e grounding pass saved the PDF; text layer): Hong Kong CEDD *Port Works Design Manual* Table 1 "Typical Natural Beach Slopes for Various Sediment Sizes" — D50 0.2 mm 1:50–1:100, 0.3 mm 1:25–1:50, 0.5 mm 1:20–1:40, 5.0 mm 1:8–1:15 ("a beach of a given grain size will adopt a flatter slope in an area exposed to severe waves"); USACE CEM EM 1110-2-1100 Part III Ch. 3 Example Problem III-3-6 uses **beach face slope m₀ = 0.05 with D = 0.25 mm and berm height B = 2 m** | the face is sea + min(B, d·m₀); default m₀ = 0.05 (CEM's example, inside the HK 0.3 mm band 1:25–1:50 → 1:20 is its coarse edge) | GROUNDED (range + the CEM example); the pick is DESIGN |
| Berm height / backshore | CEM Part III Ch. 3 Example Problems III-3-5 and III-3-6 (read): **B = 2 m**, closure depth **h* = 6 m**, D = 0.2–0.25 mm; the backshore width behind the berm has no source | B = 2 u; the backshore rises 1:10 from the berm to the natural terrain (DESIGN, a dune/bluff ramp so the cut ends in a slope, not a scarp) | GROUNDED (B, h*: the manual's own example values) / DESIGN (backshore ramp) |
| Nearshore profile | CEM Part III Ch. 3 §III-3-3 (read): the recommended equilibrium beach profile **h = A·y^(2/3)** (Bruun 1954, Dean 1977: 2/3 is the best overall fit to ~500 US east/Gulf profiles), **Table III-3-3** A by D50: 0.10 mm 0.063, 0.20 mm 0.100, 0.25 mm 0.115, 0.30 mm 0.125, 0.40 mm 0.145, 0.50 mm 0.161, 0.70 mm 0.185, 1.00 mm 0.210 m^(1/3); limitations stated there: infinite slope at y = 0 and no bars | seabed = sea − A·d^(2/3) out to the closure depth, A = 0.115 (0.25 mm, the CEM example), closure 6 u ⇒ active width (h*/A)^1.5 = 377 u | GROUNDED (form + table) / DESIGN (grain size) |
| Beach band width | derived: face width B/m₀ = 40 u, nearshore width (h*/A)^1.5 = 377 u | no constant typed | DERIVED |
| Lake shelf depth / width | none (the auditor's macrophyte-depth bracket 2–11 u is not a shelf; Duarte & Kalff 1986 via a 2023 review: submerged vegetation drops off above ~2.24 % littoral slope — SUMMARY) | deferred: ship as an explicit DESIGN default when a lake shelf is built, labelled | NEEDS-RESEARCH |
| Cliff vs beach | Sunamura 1992 (via summaries): rock-strength driven, no slope threshold | a coast column whose local slope exceeds the angle of repose (`kRockSlope`, the existing scree rule) keeps its cliff; others take the beach profile | REASONED |
| Channel-initiation area | Montgomery & Dietrich 1992 *Science* 255 (`TerrainGenerationV2.md` grounded row, 2026-07-09) | 0.05–1 km², climate-dependent; `hydro.minChannelDrainage` is a recipe parameter inside that range (DESIGN) | GROUNDED (range) |

## 11. Research basis (what each borrowed idea is and where it came from)

- **Génevaux 2013** (hydrology-first analytic terrain) → L0/L1 ordering and "evaluable at any LOD".
- **Cordonnier 2016 / Veloren `erosion.rs`** (uplift + stream power) → M1 macro erosion;
  **Tzathas 2024** analytic form if iteration cost bites.
- **Peytavie 2019 Procedural Riverscapes** → M2 river geometry (trajectories, beds by type).
- **Minecraft 1.18 aquifers** → M2 underground bodies, shape only; **jigsaw `terrain_adaptation`**
  → M6 footprint-as-density-modifier.
- **Veloren `civ/mod.rs`** (site scoring, tracks with reuse, bridge sites, economy edges) → M3/M4.
- **Galin 2010/2011** (anisotropic cost-field roads with segment masks; hierarchical merge;
  bridges/tunnels as edges) → M4.
- **arnis** (`highways.rs`, `bridges.rs`: cross-section median elevation, at-grade cut/fill
  registration, deck = least-concave majorant, tapered ends, pier refusal, tunnels with portals;
  the 33-processor vocabulary) → M4 realization and the M5 tag vocabulary.
- **Emilien 2012** (interest maps, seed-then-connect, anisotropic parcel conquest, open shape
  grammar) → M5 core.
- **GDMC 2022 winner** (agent loop, wall = dilated hull + A* + smoothing, farms by per-height
  dilation) → M5 walls/fields.
- **EthosLM** (agent decides meaning/layout/palette, code decides math; rings with ground
  treatment; "library grows by refusal"; measured record vs request; pairwise blind judge) → M5
  ring spec, M8 split, and a later "typology grows by refusal" extension of the structure skill.
- **Dwarf Fortress / Songs of Syx** (zero-player civ sim writes history) → M3 history ledger.
- **WorldGen 2025, Actor/Critic map-gen, MIDI framework** → D8.

---

## 12. Risks and open questions

**R0 (found 2026-10-15 measuring the M1 baseline, Release 2e78d1df = the merged water core):** a free-camera
teleport to a never-stored OPEN-SEA pose (1200, 25, 1200; generator surface −15) crashed the engine with
`0xC0000005` on a worker thread (`phyxel.exe+0x8f17d1`, read of `0x00000d3500001156`) ~10 s after the
teleport while streaming chunk (37,−1,36); the same procedure at a land pose did not crash. Report:
`docs/evidence/worldforge_v2/m1_stream_baseline_sea_crash.txt`; minidump `crashes/crash_20261015_143809.dmp`
(58 MB, not committed; no Release PDB on disk so the frames are unsymbolized). n = 1, not bisected, matches
the shape of the parked "post-teleport streaming APPCRASH" in `water-system-v2` memory. **Second occurrence 2026-10-15 20:52, at a LAND pose** (the gate pose (300, 109, −1400) that had
run clean twice that day), Release 812f47f0 with the M1e profile instrument, ~10 s after the teleport,
same stack shape (worker thread via BaseThreadInitThunk, five phyxel.exe frames at the same relative
offsets: `m1e_profile_land_crash.txt`, minidump `crashes/crash_20261015_205202.dmp`). So it is NOT
sea-specific: an intermittent post-teleport streaming crash on a worker thread (2 of 5 teleport runs
today). The last log lines before it are off-thread `TreeLodMesh` LOD-set builds and the water region
follow. **Hunt, 2026-10-15 evening (D14):** three 12-pose teleport loops (RelWithDebInfo recorder-off,
Release recorder-off, RelWithDebInfo recorder-on; `r0a_run.txt`, `r0rel_run.txt`, `r0rec_run.txt`)
survived 36 teleports; the only remaining difference from the two crashing runs is luck, so the crash
is intermittent at a rate the loops did not reach. What changed so the NEXT occurrence is useful:
**Release builds now carry a PDB** (`CMakeLists.txt`: `/Zi` + `/DEBUG /OPT:REF /OPT:ICF`, codegen
unchanged) and `POST /api/debug/crash {"confirm":"crash"}` proves it — red: every report before
today was bare `phyxel.exe+0x…` offsets; green (`m1c_release_pdb_crash_report.txt`): the deliberate
crash reports `EngineAPIServer.cpp:2336` through `httplib::ThreadPool::worker` with file:line on every
frame. The two archived minidumps stay unsymbolized (no PDB for those binaries). Every L4 script keeps
its "ENGINE DIED" check.



1. **Erosion determinism across platforms.** Implicit solvers with float accumulation can differ by
   compiler/CPU; pin with a bit-exact fixed-step formulation or an integer-scaled solver, and a
   golden hash per seed in CI.
2. **Meso apron vs long-range processes.** Thermal/diffusion confined to tile+apron is only
   approximately order-independent; the seam test decides the apron width, and anything that
   cannot be made local goes to macro.
3. **Recipe hash churn.** Every sheet parameter change invalidates cached sheets AND the civ plan
   AND the ledger of realized sites. A world with realized towns must refuse a sheet re-bake
   (plan-hash drift refusal already exists for V1) — the owner's call per world.
4. **Free-form streets vs axis-aligned realizers.** `StreetPaver` paves rect runs; polylines need
   a raster-by-cell paver with the same step-up invariant. Budgeted inside M5, not M4.
5. **Two-era falsification cost.** A second era is real work (program + ≥ 1 typology); it is in M5
   because deferring it is how one-instance abstractions calcify.
6. **The water branch moves.** §4's adapters keep today's `HydrologyMap`/`FlowField` calls alive;
   if WaterCore §26 changes what it reads (bricks everywhere), the adapter list is revisited at
   the M2 design-check, not before.
7. **Scope pull.** The satisfying work is the visible M0 fixes and more typologies; M1 is invisible
   for weeks and is the only item that moves the ceiling. Order holds unless the owner re-rules.
8. **Render gate on cities.** A 104-building city costs ~50 ms GPU on the 4090 today
   (`PerfProgram2026-09.md` §16.12). Generating 200-lot cities before the perf program lands its
   next levers produces cities the renderer cannot show; T4a makes the shipped lot count a
   measured number.
9. **Discipline is currently unenforced.** The Stop-hook is not wired; until the owner wires it,
   every "works" claim in this plan's execution relies on the auditor being invoked by hand.

---

## 13. Code-contact inventory — what each milestone actually touches (second pass, 2026-10-14)

Everything below is from reading the code, not the docs. `file:line` is the working tree on
`feat/worldforge-v2` at rev 2. A milestone is not planned until its row here is complete.

### 13.1 M1 World Sheet

| Touch | Today | Change |
|---|---|---|
| Layer-0 source | `CoarseWorldModel` bilinear over a lambda, re-evaluated 4× per column (`CoarseWorldModel.cpp:10-43`); `reliefAt` 2 ridged multifractals + warp (`WorldGenerator.cpp:711-739`) | `Sheet` = `shared_ptr<const MacroSheet>` + `shared_ptr<MesoPager>` members of `WorldGenerator`, copied by the **default copy ctor** like `m_coarse/m_hydro/m_worldForge` (`WorldGenerator.h:352-377`) |
| Worker copies | 3 workers + far mesher + live = 5 instances, every cache per-instance and unlocked (`WorldGenerator.h:405-412`, `ChunkManager.cpp:93-97`, `RenderCoordinator.cpp:1609`) | the meso **page cache is shared and internally synchronized** (one LRU, read-mostly, tile = immutable blob); per-instance would be 5× memory |
| Rebuild point | `applyRecipe → rebuildCoarseModel` clears memos, re-bakes hydro + plan (`WorldGenerator.cpp:295-422, 1481-1534`); runs **twice per load** (`GameDefinitionLoader.cpp:496/506/546`); hydro memo `g_hydroCache` keyed on type/seed/climateFreq/seaLevel/spline, NOT the plan (`:348-363`) | macro bake memoized process-wide by the **sheet hash** (seed, type, sheet, heightSpline, climateFrequency, seaLevelY — NOT worldforge params, withdrawn 2026-10-15); the twice-per-load path hits the memo |
| Column cost | `sampleColumn` ≈ 140 noise evals incl. a 4-neighbour slope re-sample (`:1028-1034`) | slope from the meso gradient; `ColumnCostBench` pins ≤ 25 |
| Vertical streaming | one surface sample per chunk column at its centre, cached forever, ±1 band (`ChunkStreamingManager.cpp:337-359`); far coverage probe cy −4..12 (`RenderCoordinator.cpp:1600`); main-thread `sampleSurface` for the band (`ChunkManager.cpp:260-264`) | min/max surface over the chunk column from the meso tile; replaces the main-thread sample |
| Far mesher | `configure` is re-callable (`FarTerrainManager.cpp:36-55`) and live apply already calls it (`Application.cpp:13586-13596`); the one-shot is only the auto-config guard (`RenderCoordinator.cpp:1579`) | every recipe/sheet rebuild path (not just WorldForge live apply) must call `configure` with the new generator; pin with a test that the mesher's copy carries the new recipe hash |
| Persistence | `world_meta` TEXT-only (`WorldStorage.cpp:237-240`); precedent `chunk_lod_blobs` (created `:262`, used `:395-403`) | new `sheet_tiles(recipe_hash, kind, tx, tz, blob)` table; miss → re-bake; hash mismatch refuses with the reason |
| Heightmap / Flat | `setHeightmapSource` NULLS hydro/flow/bodies/plan (`WorldGenerator.cpp:290-312`); Flat skips hydro (`:342-347`); `previewWorldForge` returns null without `m_hydro` (`:438`) | macro = the image / a constant; drainage + climate computed from it or declared absent with a reason; the plan bake un-gated from `m_hydro` |
| Tests kept green | `CoarseWorldModelTest` (corner-exact, bilinear, continuous, order-independent), `TerrainReliefTest`, `MapCoarseSourceTest`, `RoadFieldSeamTest.GeneratorCopyStampsIdenticalRoadColumns` | add `MesoSeamTest`, `MacroErosionTest`, `HydroEdgeTest`, `ColumnCostBench` |

### 13.2 M2 Hydro Geometry

| Touch | Today | Change |
|---|---|---|
| Bake | `HydrologyMap`/`FlowField` each re-sample 65 536 cells (`HydrologyMap.cpp:16-22`, `FlowField.cpp:18-22`); D4 flow dirs though `flowDirAt` claims 8-neighbour (`FlowField.cpp:32-47, 236`) | meso-resolution drainage from the sheet; `HydrologyMap::{filledAt,waterLevelAt,orderAt,accumAt}` and `FlowField::channelAt` kept as **adapters** while WaterCore calls them |
| Carve | order-indexed table, 1–2 voxel depths, straight cell segments + 55 u warp (`FlowField.cpp:91-148`, `WorldGenerator.cpp:1205-1212`) | spline centrelines; width/depth/bank from drainage; `ColumnSample::channelDepth/bedHeight` (branch fields) stay the carry |
| Water spans | `waterSpansForBlock` (declared `WorldGenerator.h:229-230`) pads by `kWaterExtentSteps=256` (`WorldGenerator.cpp:755`) through the column cache `kColumnCacheMax=512` (`WorldGenerator.h:441`) for a 544² pad, with a `static_assert` (`WorldGenerator.cpp:841`) | basins at meso resolution shrink the pad; the assertion and cache size are re-derived, not deleted |
| Flora gate | `floraCellLayer` gates on sea level, Stone, Snow, `riverOrder`, `roadClass` (`WorldGenerator.cpp:1263-1275`) | add beach band / floodplain / basin-below-spill gates |
| Tests kept green | `TerrainRiverTest` (order ≥3 exist, carve non-vacuous, sinuosity, acyclic, deterministic, creek shelf), `HydrologyMapTest`, `FlowFieldTest`, `PriorityFloodTest` | ⚠ `CarvingIsNonVacuous` currently sees the 2-voxel trunk; thresholds re-grounded with real depths; add `RiverGeometryTest`, `BasinRimTest` |

### 13.3 M3 Civ Plan

| Touch | Today | Change |
|---|---|---|
| Params | `WorldForgeParams` v1 clamps 3..8 / 512..8192 (`WorldForgeParams.h:39-42`), `toJson/fromJson` (`:48-77`) | v2 block (§7); v1 JSON still parses (legacy keyless = disabled, `WorldForgeRecipeTest`) |
| Siting | score → stable_sort → rank 0 town / 1–2 village / rest hamlet (`WorldForgePlan.cpp:345-350`); fixed footprints 140×60 / 80×48 / 40×32; `sitePins` seated verbatim | suitability + resources + 5 tiers + ruins + epochs; a site carries `province`, `era`, `history[]`, `resources` |
| Site record | `WorldForgeSite` has no lot list (`WorldForgePlan.h:41-55`); layout derived at realize time from (tier, seed, W, D, terrain) (`WorldForgeBuildService.cpp:12-54`) | site references its persisted M5 map |
| Tests | ⚠ rewrite: `ViableRegionYieldsRequestedSiteCount`, `TierMixIsOneTownTwoVillages`, `SitesWithinRegion` (circular), `SiteCountClamped`, `EightSitesOverFullRegion` (`WorldForgeStressTest.cpp:64`), `RealizationOrderTownFirst`; keep: `SitesNotInWater`, `SitesRespectMinSpacing`, `SitePinsSeatedVerbatim`, `PlanDeterminism`, `SiteSeedDerivation`, `UnviableRegionYieldsFewerSitesHonestly`, `MountainWorldDegradesHonestly` | add `CivPlanTest` |

### 13.4 M4 Route Field

| Touch | Today | Change |
|---|---|---|
| Routing | all-pairs A* on 128 u cells, cost `(1+15·grade)²` + river/shore penalties, Kruskal MST, 1.4× detour loops, Chaikin×2 + 16 u resample (`WorldForgePlan.cpp:164-263, 457-503`); junction reconciliation O(roads²·verts²) (`:677-715`); synchronous inside `rebuildCoarseModel` | Galin cost field on macro → meso refinement with segment masks; off the main thread; junction merge by class |
| Grade / earthworks | `kMaxRoadGrade=0.5` lower envelope (`WorldForgePlan.h:131`, `.cpp:636-671`); `surfaceY` overwritten to `gradeY` with a 3 u shoulder, unbounded depth (`WorldGenerator.cpp:1067-1079`); creeks skipped (`:1070,1094`); lake check is sea-level only (`:1095`) | grounded cap per class; `earthwork{cutY, fillY, retaining}` in `ColumnSample`; ford/culvert for creeks; lake-level check |
| Bridges | banks within 96 u else no bridge + WARN (`:604-611`); flat Wood deck at the higher bank, Stone piers every 12 u (`:579-623, 829-872`); emitted only by `generateChunk` (`WorldGenerator.cpp:550-578`); far tiles use `bridgeDeckY` only to skip the road query (`FarTerrainMesher.cpp:141-142`) | deck = least-concave majorant over clearances, tapered ends, pier refusal (arnis `bridges.rs` shape); far-tile emitter (M7) |
| Raster | 8 u cells, 2048/axis cap, segment table cap 0xFFFE silent (`:769, 784-786`); `roadAt` checks ±1 segment (`:886-890`) | cap refuses loudly; junction cells carry both corridors |
| Street spurs | first arriving road only (`WorldForgeBuildService.cpp:34-52`, documented as intended `.h:71-72`); roads trimmed at footprint+1 (`WorldForgePlan.cpp:525-537`) | every route enters the M5 map as a way with a class |
| Tests | ⚠ rewrite: `CrossingsGetBridgeSpans` (96 u), `RoadsReachTheFootprintEdge` (rect site), `RoadStepProfileMostlyWalkable*` (50 % tolerance), `GradedRoadsAreStepWalkableOnMountains` (unbounded cut/fill), `SettlementParamsCarryArrivalAxis`; keep: `RoadsConnectAllSites`, `RoadsAvoidStandingWater`, `RoadCenterlinesContinuous`, `RoadNeverOverridesRiverOrSea`, `FloraKeepsOffTheRoadCorridor`, `OffRoadColumnsIdenticalToDisabledWorld`, `BridgeCrossingIsAgentWalkable`, `RoadQueryPerf` | add `RouteFieldTest`, `RouteWalkTest`, `BridgeSpanTest` |

### 13.5 M5 Settlement Map

| Touch | Today | Change |
|---|---|---|
| Lot contract | min corner + `footprint:[w,d]` + `front` ∈ {x0,x1,z0,z1} + typology/style/seed; no rotation (position read at `StructureForge.cpp:247-250`, footprint/front at `BuildingProgram.cpp:129-137`, `BuildingProgram.h:103-109`); L-shape = union of axis-aligned rects (`RoomLayout.h:44-45`); door must lie on the footprint perimeter (`StructureRealizer.cpp:82-145`) | **lot extraction**: largest axis-aligned rect inside the parcel polygon with ≥ 1 edge on a way; that edge → `front`; parcel remainder = yard polygon (new yard realizer: fence along the polygon, props inside) |
| Streets | `StreetPaver` rect runs, long-axis centreline grade, level across width, first-writer-wins at junctions (`StreetPaver.h:59`, `StreetPaver.cpp:40`; the file is 134 lines); door spurs walk an arbitrary `PathPlan` and carve `halfWidthMicro` perpendicular (the spur path in the same file — line to be re-cited when M5 is designed) | **polyline paver** built on the spur path: centreline `PathPlan` + width band + a cross-section level rule at bends (arnis cross-section median); the street-unit applier (`SettlementBuildService.cpp:651-793`: cut, headroom band, `sharedRoadBand`/`sharedStamp`, grass→Dirt) re-keyed on the band |
| Walls | `planTownWall(site Rect, streets, footprints)` 4 bands + corner towers + gates at street-edge hits, whole-circuit refusal (`TownWall.cpp:60-168`); per-column stamp lambda is cell-wise (`SettlementBuildService.cpp:1139-1162`) | new routed-hull planner emitting wall polyline segments + gate spans + tower sites; the stamp lambda reused |
| Ground | `analyzeSite` W×D grid (`SiteAnalysis.h:54-57`, `SiteAnalysis.cpp:17`); terrace per parcel rect, median, 4-cube skirt (`SettlementBuildService.cpp:400-449`); prepare_pad median + cut/fill inside `stagePlace` (`StructureForge.cpp:545-688`) | polygon masks (cut/fill loops are per column, small change); ring ground treatment selects terrace / podium / graded / preserve |
| Doors / spurs | spur anchors from `streetSide` + footprint midpoint, not the realized door (`SettlementBuildService.cpp:660-672`) | map carries the door position; spur re-derived after realization |
| Program data | `morphology` dispatch in C++ (`SettlementBuildService.cpp:183-195`); `period` never passed (`:530-538`); stories hard-coded (`:536`); roofs gable/hip/flat only (`StructureRealizer.cpp:681-868`); the generic BSP splits each room's longer axis (`RoomLayout.cpp:73-86`) while typology layouts are long-axis bays (`lengthIsX`, `RoomLayout.cpp:30,148,336`); hearth siting for every typology (`StructureForge.cpp:261-277`); fence Picket + 2-cube gate (`:886-891`); constants terrace skirt 4 / clear margin 4 / canopy 8 / veg flood 16; `kMinCorridorWidthCubes=2` (`SettlementWalkability.h:48`) | planner registry keyed by data; `period` + stories from the tier; the constants move to the program with provenance; second era = `eras.<id>` + `room_programs/<era>.json` + ≥ 1 typology |
| Nav / residents | full `buildNavGrid()` per site (`:1700-1702`); `ResidentSpawner` already lot-driven, clusters by 64 u (`kClusterLinkU`, `ResidentSpawner.h:71-73`); locations keyed `typology_x_z` with no link to the object uuid (`StructureRealizer.cpp:140-143`) | location ↔ lot ↔ uuid link in the ledger |
| Tests | ⚠ `SettlementLayoutTest` (whole file: rect plots on a rect footprint) becomes map invariants over polygons (frontage, no overlap, min width, every lot fronts a way); keep `SettlementWalkabilityTest`, `SettlementTraversalTest`, `StreetPaverTest` (+ polyline cases), `MainStreetLayoutTest` as the main-street morphology's own test | add `SettlementMapTest`, era falsification test |
| Perf coupling | `CityBench_*` C-25…C-100 (`PerfProgram2026-09.md` §16) | every M5 increment re-runs the ladder (T4a) |

### 13.6 M6 Lazy Realization

| Touch | Today | Change |
|---|---|---|
| Host | `MainThreadJobs` constructed/ticked only in the editor (`Application.cpp:613, 3530`); `WorldForgeBuildService::start` only from `:13734`; settlement `Deps` filled at `:13410-13432`, `:13646-13662`, structure `Deps` at `:17298-17318` and `GameDefinitionLoader.cpp:861-866` | core realization queue ticked from `EngineRuntime::beginFrame` (after the pump, `EngineRuntime.cpp:344-349`); one `Deps` factory; `GameShell` provides it |
| Trigger | `OnChunkLoaded` / `FinalizeGeneratedChunkFunc` fire per landed chunk on both hosts, main thread, unbudgeted (`ChunkStreamingManager.cpp:220-221`) | the hook only **enqueues** lots whose footprint columns are all resident; the queue realizes under a per-frame budget |
| Residency | `residencyReady` samples 5 columns (`Application.cpp:13695-13709`); `place()` materializes an empty chunk for a non-resident unsaved target (`ensureChunkAt`, `ChunkManager.cpp:390-415`) rather than refusing; prepare_pad scans `reqY+64` down to y = 0 (`StructureForge.cpp:555-565`); `effectivePlayBounds` is none for streaming worlds (`ChunkManager.cpp:222-226`) | full-footprint + pad-depth residency predicate; partial placement refuses (M0); a site hold keeps its chunks resident while its lots are in flight |
| Ownership | realize lot k deletes any overlapping `category=="structure"` (`StructureForge.cpp:447-457`); undo snapshots per unit (`StructureBuildService.h:53-54`) | ownership by ledger row + uuid; stream-in realization never pushes undo |
| Atomicity | chunks save on evict / `save_world` / shutdown; `placed_objects` + locations only on checkpoint; eager record save = ghosts (`StructureBuildService.cpp:193-195`) | per-lot atomic commit (dirty chunks + record + location + ledger row) in one transaction; `restreamWorldLive` (discards dirty chunks, `ChunkStreamingManager.cpp:515-539`) refuses while realized lots exist unless `force` |
| Generator-side footprint | road grading precedent blends `surfaceY` to `gradeY` (`WorldGenerator.cpp:1070-1079`) | footprint modifier in `sampleColumn` (pad level per lot from the map); prepare_pad reads the same level so it never fights the generator; flora gate gains `settlementFootprint` (fixes phantom far trees) |
| Site-wide units | sweep, doorsteps, fences, wall, orphan-canopy sweep depend on all buildings + in-memory `shared*` vectors (`SettlementBuildService.cpp:333-355, 1074-1101, 1333-1375, 1415-1520`) | per-lot: yard + doorstep + spur with the lot; per-site: wall + sweep as their own ledger rows triggered when their columns are resident |
| Nav | `onRegionChanged` is a box refresh; paving/fences outside building boxes only covered by the full rebuild (`NPCManager.h:222,251`, `:1695-1699`) | incremental region rebuild over the lot + its spur band |
| Lights / locations / residents / far trees | `restoreLights` once per load (`Application.cpp:2122-2138`); locations from `world_meta["locations"]` (`:2143-2148`); `ResidentSpawner` driven only by the editor (`:3695-3705`); far-tree exclusions polled from placed objects, 20 u pad (`:3623-3667`); `pumpDeferredDbLoads` editor-only (`:4217`) | all engine-side, per lot on realize; exclusions derived from the map, not from placed objects |
| Test API | `GameApiService` has no chunk/voxel query, no placed-object listing, no `worldforge_*` (`GameApiService.cpp:175-410`) | add listing + ledger read for the L4 parity check |

### 13.7 M7 Horizon

| Touch | Today | Change |
|---|---|---|
| Tile build | 64×64 columns, `sampleSurface` at 4 corners + centre per cell + exact scans ≤ step 4 ≈ 21k–37k column evals per tile (`FarTerrainMesher.cpp:75-363`); rings {2,4,8,16} to 4096 u, 768-tile LRU, one worker (`FarTerrainManager.h:64-70`) | tiles read the meso sheet directly (`heightAt/materialAt/roadAt/bridgeAt`); a 5th ring from macro beyond 4096 u |
| What it draws | materials, road ribbons, river-bed colour; no water, no bridge decks, no structures, 2.5-D only; `EvictedLodCache` quarantined (floating squashed canopies, `EvictedLodCache.h:42-60`) | bridge + settlement-massing emitter writing extra quads into `FarTileMesh` (plain vertex format, any face id); massing from the M5 map via the shared plan pointer |
| Rules | `LodTierLedger.md:65-80`: dither fades, residency-gated, correspondence, chunks invisible. There is **no literal "cube-as-atomic-LOD" rule**; the nearest is rule 1 (no geometry scaling) + the EvictedLodCache verdict | cite rule 1; add the atomic-cube rule explicitly to the ledger in the same commit |
| Tree exclusions | editor polling of placed objects (`Application.cpp:3623-3667`) | from the plan / map (M6) |

### 13.8 API and tooling deltas (from `EngineAPIServer.cpp` + `phyxel_mcp_server.py`)

Unchanged: `structure/build` (1452), `worldforge/apply` (1513, v2 fields), `worldforge/focus` (1535),
`world/validate` (1594), `world/generate` (3446), `walk/probe`, `navgrid/*`, `navgraph/*`,
`debug/water_*` (2146-2175). Changed: `worldforge/plan` (1502) returns civ + routes;
`worldforge/build` (1524) aliased to `realize`; `worldforge/status` (1546) ledger moves per site;
`worldforge/map` (1550) + `minimap` (1577) overlay tiers/classes/maps; `world/terrain_height` (2262)
gains the generator source. New: `sheet`, `site/{id}/map`, `site/{id}/ledger`, `hydro`. MCP: six
`worldforge_*` tools (`:535-620`) updated; mirrors added for `build_settlement`, `world/validate`,
`walk/probe`, `navgrid/navgraph`, `water_validate`, `realize`, `hydro`. Python tools that read
`terrain_height` (`tools/perf_harness.py`, `lighting_gate_rig.py`, `light_leak_rig.py`,
`debris_settle_bench.py`, `proving_grounds_probe.py`, `structure_pipeline/realize.py`,
`scripts/verify_terrain_seating.py`) keep the resident-chunk default.

### 13.9 Sequencing against the other active branches

- `feat/water-rethink` — §4; M1 starts from a tree containing its generator-side delta.
- `perf/lights-microvoxel-program` — T4a; M5 increments re-run the city ladder; no settlement
  density change ships without a ladder number.
- `main` was 34 commits ahead of `origin/main` when this branch was cut; the branch carries them.

---

## 14. Meshy AI — the reference and model source for voxelized assets (owner, 2026-10-14)

The owner has a paid **Meshy AI** account (text-to-3D / image-to-3D; textured GLB/FBX/OBJ/USDZ/STL
meshes, low-poly and quad-topology options) with commercial rights, and has ruled: **when something
needs to be voxelized — an asset, an outline, a floorplan reference, an era or cultural building
form — Meshy generates the 3D reference model for it.** This is the sanctioned source; it is not
optional and it is not a fallback.

**What it is used for**

- **Era and culture packs (D9, M5):** generate reference models of the building forms, roofs,
  ornament, street furniture and props of each era (Roman, Edo, adobe, Norse, …). Massing and
  outlines from the model inform the era's realizer rules (roof forms, bays, ornament) and its
  room program; the grounding-auditor still attaches a source to each dimension typed into data.
- **Props and furniture:** every new prop, furniture piece or item for a pack is generated in Meshy,
  voxelized, and shipped as a template.
- **Reference for anything the structure forge must learn to build:** a Meshy model of a feature
  (an arcade, a torii, a hypocaust floor, a minaret) is the visual spec the realizer rule is
  written against.

**The import tool — `tools/meshy_import.py` (build early; independent of M0–M4):**

1. Call the Meshy API (text-to-3D or image-to-3D; the key from the environment like the other
   tools) or take a downloaded GLB.
2. Voxelize with `trimesh` (installed, 4.10.1) at the chosen tier: fine-voxel item grid
   (`# grid: 81`) for hand-held/tableware, furniture micro grid for furniture, subcube/micro for
   architectural reference stamps.
3. Map materials from the mesh's texture colour clusters to `materials.json` names (a per-asset
   override table for the cases the clustering gets wrong).
4. Run the existing gates unchanged: `lint_voxel_detail.py`, `asset_index.py --validate`, spawn +
   screenshot in-engine at true scale.
5. Write provenance into `template_catalog.json`: `source: meshy`, prompt or image, date.

**One rule, unchanged (repo hard rule, not a caveat):** buildings in the world are produced by the
structure forge from a program, so they stay walkable, furnished and validated; Meshy models are the
reference those programs and rules are built from, and the direct source of the props that dress
them. That is exactly the owner's stated use.

**BlockSmith is REMOVED in M0** (owner, 2026-10-14: it was not adequate for our uses). The full
removal inventory is the M0 table row; its MCP tools and skills are replaced by the Meshy route
above. Meshy is the only model source going forward.

---

## 16. Lab worlds (F6) — recipes, contents, poses, controls, predictions

Rules (from `FeatureDesignKeys.md` test-rig discipline and `LodTierLedger.md` rule 6): one variable
per run; the prediction is written in the plan or the test name BEFORE the run; a control proves
the metric measures what it claims; the rig's deltas from shipped defaults are stated; far-field
look checks use ELEVATED poses; unit rigs on flat worlds stay inside ONE chunk. Projects live in
`Documents/PhyxelProjects/<name>` (scaffolded by `tools/create_project.py`); each lab's `game.json`
world block is given here so it can be regenerated. Camera poses are `set_camera` free-mode
positions (x, y, z, yaw, pitch); "surface" means `terrain_height?source=generator` once M0 adds it.

| Lab | Used by | World block (game.json) | Contents / the one variable | Poses | Control | Metrics |
|---|---|---|---|---|---|---|
| **WorldForgeBaseline** (exists as `samples/game_definitions/worldforge_test.json` scaled up; the stress fixture) | F7, M0 L4, M4 | `type: Perlin, seed: 20260816, streaming: true, worldforge{siteCount: 8, regionRadius: 2048, minSpacing: 400}`; Mountains variant `seed: 424242` (both from `WorldForgeStressTest.cpp:59-60, 84-85`) | the canonical 8-site region; variable = the M0 row under test, everything else pinned | per site: centre at surface+40, pitch −45 (town read); per bridge: 30 u off the deck axis at deck+6, pitch −15 | the pre-M0 binary on the same seed (archived numbers) | sites built/refused, `lotFailures`, residue census on refused lots, walkability routes walkable/blocked, road step profile (% unclimbable, both seeds), bridge count vs crossings, plan hash |
| **StructGenTest** (exists, flat) | M0 unit rigs | `type: Flat, from (0,0,0) to (4,0,2)` (5×1×3 chunks) | one lot per run inside chunk (0,0,0); variable = refusal path / sweep / partial placement | (16, 30, 48) yaw −90 pitch −35 | a lot that builds cleanly on the same rig | cell + object census before/after; `success` + `placed/failed` counts |
| **StructGenHills** (exists) | M0 terrace-material row | `type: Perlin, seed: 1234, from (0,0,0) to (128,64,128)` | one parcel on a slope; variable = biome under the parcel | parcel centre at surface+20, pitch −40 | the same parcel on StructGenTest (Grass biome) | terrace top/skirt materials == biome surface/subsurface |
| **WorldSheetLab** (scaffolded 2026-10-15 at `PhyxelProjects/WorldSheetLab`; game.json `world.sheet{continentCells:16, mesoSteps:4, erosionSteps:200 + Cordonnier's k/U/dt/m/talus}` = a version-2 recipe with erosion ON — the control is the same recipe at `erosionSteps: 0`; delete the lab's world.db before a recipe change, the stored recipe wins) | M1 | `type: Perlin, seed: 7001, streaming: true, sheet{macroCellU: 512, mesoCellU: 32, continentCells: 16, uplift: "flat+ridge"}` — a 16×16-macro (8 km) sheet, one painted ridge along z = 0 of amplitude 160 u | the ridge is the only relief source; variable = erosion steps (0 → N) | ridge crest (0, crest+30, 0) pitch −20 looking +x; tile seam at x = 0 (tiles cover [tx·8192, (tx+1)·8192), so 0 is a real seam and it crosses the ridge): (0, surface+60, 40) pitch −60 looking −z along the seam (the meso tile boundary must be invisible); valley floor at the first order-3 confluence | erosion steps = 0 (pure uplift: no valleys, drainage is a fan) | drainage is a tree with no sinks; valley density per km² rises monotonically with steps; slope histogram; `MesoSeamTest` bit-equality across x = 4096; column cost ≤ 25 noise evals; macro bake ms, meso tile ms |
| **RiverLab** (new) | M2 (+ WaterCore hand-off) | WorldSheetLab recipe + `hydro{minChannelDrainage}` tuned so exactly one order-3 river leaves the ridge, enters one basin (a painted low at (1500, −, 1500)) and the basin spills to a coast at x > 6000 (`seaLevelY: 16`) | one river, one lake, one coast; variable = the M2 geometry rule under test | confluence of the first two order-2 creeks, from 25 u up, pitch −50; lake rim at the spill point, pitch −30; coast at the mouth, elevated 40 u, pitch −35 | the same world with M1's straight-segment channels | `water_validate rim_leaks == 0` on the bake; bed monotone along every centreline; width/depth monotone in drainage; junction angles < 90°; `TraversalProbe` walks both banks end to end |
| **CivLab** (new) | M3 | WorldSheetLab recipe with one river (RiverLab) + one painted massif at (−2500, −, −2500) + `civ{tiersPer100km2: {city: 1, town: 3, village: 9, hamlet: 27}, epochs: 3}` on a 64-km² sheet | suitability inputs are exactly: one river, one massif, one coast; variable = the civ parameter under test | the `worldforge_minimap` PNG (no 3-D pose needed); one elevated pose over the city site | a flat featureless sheet (suitability uniform → sites spread by spacing alone) | every site obeys its tier's hard constraints; the city sits on the river or coast (prediction written at M3's F3); hierarchy ratio K=3 within ±1 per tier; determinism across two bakes |
| **RouteLab** (new) | M4 | WorldSheetLab recipe + RiverLab river + two pinned sites: A at (−1500, −, 0) west of the ridge, B at (1500, −, 0) east of it, `routes{gradeCap by class from §10}` | the route must cross the ridge and the river; variable = the cost-field term under test | ridge pass from 60 u above, pitch −35; bridge, 30 u off-axis; one cutting, from the road at eye height | the same sites with the grade cap set to 0.5 (today's behaviour) | grade ≤ cap along the whole route; `TraversalProbe` 0 unclimbable steps; bridge present over the river; earthwork within the derived bounds; the predicted corridor (drawn on the minimap before the run) vs the realized one |
| **CityLab** (new) | M5, M6, M7 | WorldSheetLab recipe with a gentle hill (amplitude 24 u) + one order-2 creek through the site + one pinned city-tier site at the origin, `settlement{era: medieval, density: 1.0}`; M6 variant packaged via `produce_game.py` | one city on one hill with one creek; variable = the map rule / realization step under test | elevated town read (0, surface+120, 200) pitch −35; street level at the market; the creek crossing inside town; for M7 the same site from 3000 u away at surface+200 | the main-street planner on the same site (today's rect layout) | every lot fronts a way; no lot overlaps; wall is a routed hull; walkability gate on the map AND on voxels; lot count vs the perf ladder (T4a); M6: editor vs packaged placed-object UUID set identical; M7: far-tile silhouette vs resident chunks |

**Predictions written now (F7 baseline and M0; the rest at each milestone's F3):**
- F7 baseline, canonical seed: 8/8 sites placed (`EightSitesOverFullRegion` asserts it); road step profile "mostly walkable" (the test's bound, `WorldForgeStressTest.cpp:148`); Mountains seed: fewer than 8 sites, honestly; 0.69 % unclimbable steps (`WorldForge.md` M3 result) — if the re-measure differs by more than the seed-drift headroom the test states, the baseline is re-recorded and the drift explained.
- M0, `RefusedLotLeavesNoFurniture`: today a refused lot carries a fence run, a woodpile and a paved spur (residue count > 0); after M0 the residue count is 0 and the lot appears in `lotFailures`. Control: a built lot carries all three.
- M0, `PartialPlacementIsRefused`: a lot whose footprint crosses into an absent chunk returns `success:true` with `failed > 0` today; after M0 it returns `success:false, error` naming the unplaced count. Control: the same lot fully inside resident chunks builds with `failed == 0`.
- M0, `CreekInsideFootprintIsNotBuildable`: today a plot is allocated over an order-1 channel column; after M0 that column is `Water` and the plot is dropped with the count surfaced. Control: the same site with the creek 50 u away allocates the plot.
- M0, `GateRefusesPinchedTown`: a town with a 1-cube street pinch returns `success:false, error:"unwalkable"` and spawns no residents; with `allow_unwalkable:true` it builds and reports `gate:"overridden"`. Control: an unpinched town passes with `gate:"passed"`.

**Rig vs shipped:** every lab pins `loadRadius`/`unloadRadius` to the game.json defaults (8 / 11 chunks)
except WorldForgeBaseline, which raises them to the site radius during realization exactly as
`worldforge_build` does; the sheet labs use a 16×16 macro grid (8 km), not a continent, so macro bake
time scales ×156 to the 200×200 target and is reported scaled; CityLab's hill is gentler than the
Perlin default so lot drops come from the rule under test, not from relief.

---

## 15. Finalization checklist — nothing is built until every row is green

The owner's instruction (2026-10-14): *"we are finalizing a plan. we still need to do design checks
and whatnot."* This section is the gate between the plan and the first commit of code.

| # | Item | Owner | Status |
|---|---|---|---|
| F1 | Decisions D1–D9 settled | owner | **DONE 2026-10-14** (§3.1) |
| F2 | Stop-hook wired and tested | owner (approved) | **DONE 2026-10-14** |
| F3 | `/design-check` run on **each** milestone M0–M8 and on `meshy_import.py`, verdict (NEEDS WORK / READY) recorded under that milestone in §5, findings folded in — the WaterCore.md §25.15 / §26.4 pattern | agent, per milestone, before it starts | NOT DONE |
| F4 | grounding-auditor on every §10 number; each gets a source row or is struck | agent | **TWO PASSES RUN 2026-10-14 — PARTIAL, usable for M0**: M0's only typed numbers (grade caps 0.05/0.08/0.15) are grounded to secondary extracts; canopy margin found MISMATCHED and added to M0; remaining open rows (§10 F4b) gate M2–M4 numbers, not M0. First pass: grade caps by class and the hierarchy ratio GROUNDED, the river regression GROUNDED, 8 rows STRUCK or re-expressed as derivations, the rest NEEDS-RESEARCH with the exact document named (§10 F4b). `kMaxRoadGrade = 0.5` is a MISMATCH. No number enters code until its row is GROUNDED. |
| F5 | solution-auditor reads this plan's §1 and §13 claims against the code and confirms the file:line facts the plan builds on (an audit of the audit) | agent | **RUN 2026-10-14 — VERDICT: FAIL on rev 2** (≈125 claims: 92 CONFIRMED, 11 STALE citations, 4 WRONG, 6 UNVERIFIABLE-without-a-run). All four WRONG claims (far mesher one-shot; `validate_realized` pre-mutation; partial-placement mechanism; Deps type conflation) and the incomplete BlockSmith parser inventory are **corrected in rev 3**, the 11 stale citations re-cited, and the two ordering caveats (`saveAllChunks` writes pristine chunks; `ensureChunkAt` creates empty chunks) carried into the M0 rows. The 6 UNVERIFIABLE claims (column cost ~140, road dead-end at a wide river, creek notch, mountain-flank hole, gate ships pinched towns, C-100 ≈ 50 ms) are F7's to measure. **Re-audit run 2026-10-14 (second solution-auditor pass over the corrected rows, the re-cited lines, the tree-bounds numbers and the F7 evidence files): raw verdict "VERDICT: FAIL — Item 5b: the four sys.path insertion cites in the BlockSmith row are off. Correct lines are :7123, :7226, :7369, :7646 ... Everything else is CONFIRMED."** The four citations were corrected in the same commit; no third pass has been run, so the row stands at: all audited claims CONFIRMED except one citation, fixed after the audit and not independently re-checked. |
| F6 | Lab worlds specified (`WorldSheetLab`, `RiverLab`, `CivLab`, `RouteLab`, `CityLab`): one tile each, poses, predictions written before any run | agent | **SPECIFIED 2026-10-14 (§16)**: recipes, contents, poses, controls and metrics for all six worlds; predictions written for the F7 baseline and M0; per-milestone predictions for M1–M8 are owed at each milestone's F3 design-check, before its first run |
| F7 | Baselines measured and archived in `docs/evidence/worldforge_v2/` before M0 changes anything: `WorldForgeStressTest` numbers, column cost, chunks/s in forest (Release, named GPU), test count (`tools/test_count_check.sh`), the C-25…C-100 city ladder | agent | **DONE 2026-10-14 for everything that has an instrument** (`f7_baseline.json`, `f7_l4_baseline.json`): Release unit suites 176/176; stress step fractions re-measured post-grading (canonical 1/1440 = 0.069 %, mountains 31/6722 = 0.46 %; the doc's 0/1320 and 45/6496 predate grading); **live 8-site build on the laptop: 8/8 built, 37 buildings, 0 lot failures, 541 s**; walkability gate: town 1 blocked, village 5 1 blocked, village 6 clean, **hamlets never gated** (cluster morphology skips the unit) and both blocked sites shipped as 'built' — the M0 blocking-gate red state verified live twice; 40 crossings all order-1 creeks, 0 bridges by design (the creek-notch row); two 'before' captures at the §16 poses with `mode:"free"` (an earlier pair without it showed the spawn void and was discarded). Residue census not measurable (0 refusals on this seed; unit rig owns that red). Not instrumented yet: chunks/s, column cost (M1 adds both). Test count (full suite, Debug exe): **4286 tests, 4265 passed, 19 skipped, 2 FAILED pre-existing** (`AtlasManagerTest.BuildAtlasFromSourcePNGs`, `FineFaceMerge.SubcubeMerge_CrossCubeSplitsOnLightBoundaryBetweenCubes`; no source changed on this branch) — `f7_test_count.txt`. |
| F8 | `feat/water-rethink` merged into `main` by the owner; `feat/worldforge-v2` rebased or merged onto it before M1 | owner | NOT DONE (M0 may proceed before this) |
| F9 | Doc consolidation list (§1 "doc rot") confirmed complete as part of M0's definition of done | agent | **DONE 2026-10-14**: `WorldForge.md` (apply row = live apply; Known gaps folded + V2 pointer; M3 numbers re-measured; header pointer), `WorldModel.md` step 1 DONE, `StructurePipelineGaps.md` 2026-09-25 marked stale, `StructureGenerationV2.md` P8–P10 superseded note, `LargeWorldScalePlan.md` far-terrain default, structure README ChimneyForge shipped, `CLAUDE.md` V2 pointer + post-grading numbers; the MCP descriptions were fixed in row 10. `WorldRenderV2Plan` M1/M2 table left (no milestone table found by its stated shape). |
| F10 | Owner sign-off on this plan as a whole (after F3–F7 are folded in) | owner | **SIGNED OFF 2026-10-14** (owner: "lets get to work"). M0 started the same day on `feat/worldforge-v2`; its two generator-side rows (earthworks, ford) remain parked until F8 (water merge) and the Roman-paper verification. |

Order: F3 (M0 first) → F4 → F5 → F6 → F7 → F10, then M0 begins. F3 for M1–M8 is run when each
milestone is next, not all up front, so each design-check sees the code as it then is.
