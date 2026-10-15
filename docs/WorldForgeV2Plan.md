# WorldForge v2 — the Ultimate World/City Forge (plan of record)

> **Status (2026-10-14, rev 3): PLAN, nothing built. Branch `feat/worldforge-v2`.** Rev 3 carries the F4/F4b grounding results (§10), the F5 audit-of-audit corrections (four wrong claims fixed, eleven citations re-cited), the F6 lab worlds (§16) and the M0 design-check verdict. Written from a
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
| T7 | **Water-ready**: every river/lake/coast is a shaped basin with bed, banks, rim and spring sites that WaterCore can fill with zero rim leaks | `water_validate rim_leaks == 0` on the bake (owner's acceptance test, `water-must-be-tied-to-terrain`) |
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
| `analyzeSite` receives the live water/channel predicate (creek + lake bed = `Water`) | `SettlementTerrainPlanTest.CreekInsideFootprintIsNotBuildable` |
| Site prep reordered, not rolled back: clearing + terrace + street grading stay before buildings (buildings re-sample graded ground, `:507-509`); **spurs, fences and yard props move after the building unit**, skip lots in `lotFailures`, and anchor spurs on the realized door from `recordDoor` instead of the footprint centre (`:462`). A refused lot keeps only its cleared terrace, which the ledger records as an empty lot | `SettlementBuildServiceTest.RefusedLotLeavesNoFurniture` (fence/prop/spur cell census on a refused lot == 0; control: a built lot has all three) |
| Forge gates run read-only before any mutation. Stage order today is intake → floorplan → validate_program → validate_assets → **footprint (removes overlapping structures + placed trees, `:439-487`)** → realize → validate_realized → place (grounding `:571-589`, vegetation `:594-645`) → furnish → emit (`StructureForge.cpp:157-176`). So a `realize` failure, a `validate_realized` traversal refusal AND the grounding/vegetation refusals all leave the removals behind (F5 correction: `validate_realized` is NOT pre-mutation). Fix: the grounding scan and the vegetation-presence check become read-only pre-checks before `footprint`, and the object/tree removal moves after `validate_realized`, immediately before `place` | `StructureBuildServiceTest.RefusalIsClean` (object + tree-cell census before/after is identical for a grounding refusal, a `keep_vegetation` refusal AND a traversal refusal) |
| Street sweep protects cells owned by placed objects | `SettlementBuildServiceTest.SweepNeverErasesPlacedObjectCells` |
| Road earthworks bounded: max cut depth / fill height (NEEDS-GROUNDING, F4), bank at angle of repose, retaining wall beyond (subcube-coursed, not a full-cube cliff). **Gated by `WorldForgeParams.version = 2`** (default for new recipes; version 1 keeps today's unbounded drape so existing worlds' unvisited roads do not change — pristine chunks are not saved by eviction or `save_world`, BUT `saveAllChunks` writes every resident chunk with no dirty test (`ChunkStreamingManager.cpp:549-560`) and the WorldForge checkpoint and `save_world {all:true}` call it, so resident pristine neighbours of a built site are frozen at checkpoint; never-resident chunks still regenerate). New `ColumnSample.earthwork{cutY, fillY, retaining}` fields; the flora gate widens by the earthwork band. Lands after the water merge (F8) | `RoadFieldSeamTest.EarthworkDepthBounded` on the mountain seed; `RoadFieldSeamTest.EarthworkFieldsIdenticalPerChunkAndWholeRegion`; `WorldForgeRecipeTest.LegacyVersionKeepsDrape` |
| All arriving roads register as street spurs, not just the first | `WorldForgeBuildFlowTest.SecondRoadGetsASpur` |
| Walkability gate is BLOCKING with `allow_unwalkable:true` override (T-27/G-61), **and runs for every morphology** (F7 live baseline: the gate unit ran only for the three main_street sites; the five cluster hamlets were never gated). **Semantics settled by the owner 2026-10-14: mark failed, keep the voxels, retry later** — the build's result is `success:false, error:"unwalkable"` with the route report; the nav-rebuild and resident units are skipped; under WorldForge the site is recorded `refused` and NOT checkpointed, so the next run rebuilds it after the generator is fixed; no teardown (the blocked town stays in memory until restream) | `SettlementWalkabilityTest.GateRefusesPinchedTown`; `WorldForgeBuildFlowTest.UnwalkableSiteIsRefusedNotCheckpointed` |
| Road raster cap logs and refuses; `room_program.json` failure is loud | `WorldForgePlanTest.RasterCapSurfaces`, `SettlementProgramTest.MissingRoomProgramRefuses` |
| Terrace/fence/path materials from the biome + program, not literals | `SettlementTerrainPlanTest.TerraceUsesBiomeSurface` |
| Parcel canopy margin derived from the biome's flora pool (max template half-width from `# bounds:`, §10 F4b: enchanted oak ≈ 11.5, megaflora ≤ 26 vs the literal 8 at `SettlementBuildService.cpp:1351`), megaflora by an explicit exclusion radius | `SettlementTerrainPlanTest.CanopyMarginCoversTheBiomesWidestTree` (red: an enchanted-oak crown overhangs a cleared parcel today) |
| Order 1–2 creeks under roads get a culvert/ford stamp (plank deck over, bed kept, subcube rail per the bridge pattern). Same `version = 2` gate and seam test as the earthworks row; lands after the water merge (F8) | `RoadFieldSeamTest.CreekUnderRoadIsFordNotNotch` (L3: `TraversalProbe` walks the road across the creek, 0 unclimbable steps) |
| Settlement passes `period` to every building; `resources/room_programs/` created with the medieval pack as its first entry; both ignored `room_program.json` load results become refusals | `SettlementBuildServiceTest.BuildingsCarryTheEraPeriod` (red: `period` absent from `makeBp` output) |
| Placement into un-generated terrain refuses. Mechanism (F5-corrected): `place()` calls `ensureChunkAt` first (`StructureGenerator.cpp:828`), which LOADS a saved chunk or else **creates an EMPTY chunk** (`ChunkManager.cpp:390-415`, `createChunk(origin, populate=false)`); the `failed++` null branch (`:830`) fires only when a saved chunk fails to load. The live hazard is therefore not dropped voxels but a building materialized into a void chunk that the streamer then never generates terrain for (the "structures on thin air" incident). Fix: before placing, every target chunk must be resident AND generated (or saved); otherwise refuse with the chunk list. `StructureBuildService` also refuses on `failed > 0` (today only `placed == 0`, `:113`) | `StructureBuildServiceTest.PlacementIntoUngeneratedChunkIsRefused` (red: today an empty chunk is created and `success:true` returned; control: all chunks resident → builds, `failed == 0`) — the exact live state is UNVERIFIED until the L4 run (F7) |
| Refusal leaves no site-prep residue: `stageFootprint`'s object/tree removal is deferred until every gate has passed, or snapshotted and restored | `StructureBuildServiceTest.RefusalIsClean` (cell + object census before/after) |
| One core factory producing BOTH `SettlementBuildService::Deps` (two fill sites) and `StructureBuildService::Deps` (two fill sites) | each struct is filled in exactly one place (`grep` per type == 1); `GameDefinitionLoader` gets lights/itemProps/keepClear |
| `worldforge_build` owns a `shared_ptr<const WorldForgePlan>`; raised load radii restored by RAII on any exit | `WorldForgeBuildFlowTest.RadiiRestoredOnAbort` |
| One truth for "surface": `terrain_height` gains `source:"generator"` and the resident-chunk scan is labelled | route test in the L4 rig |
| MCP mirrors for `build_settlement`, `world/validate`, `walk/probe`, `navgrid/navgraph`, `water_validate`; stale V1 text in `worldforge_*` descriptions removed | `scripts/mcp` tool list diff |
| Owner action: wire `solution-audit-gate.sh` as a Stop hook in `.claude/settings.json` — **DONE 2026-10-14** (local file, tested: blocks an unaudited "works", allows a VERDICT) | the gate blocks a turn ending on an unaudited "fixed" |
| **Remove BlockSmith** (owner, 2026-10-14: "not adequate for our uses"). Inventory of what goes: the `external/blocksmith` submodule + its `.gitmodules` entry; `tools/blocksmith_generate.py`, `tools/building_prompts.py`, `tools/asset_pipeline/prompt_enhancer.py`, `tools/generate_furniture_library.py` (all import or shell out to it); the six MCP tools that route to it — `generate_template`, `search_templates`, `list_generated_templates`, `critique_template`, `refine_template`, `generate_asset` (`phyxel_mcp_server.py:1671-2864`) — plus its `_run_blocksmith_native` (`:7234-7276`) and the `PHYXEL_AI_API_KEY → ANTHROPIC_API_KEY` alias (`:6580`); the hidden dependency where the MCP server imports `blocksmith.generators.phyxel_parser.read_voxel_metadata` at THREE sites (`:7124`, `:7370`, `:7647`; the first wrapped in try/except with a manual fallback) plus `sys.path` insertions at `:7121-7122`, `:7218-7222`, `:7366`, `:7643` and the subprocess path at `:5398` — all replaced by `scripts/mcp/voxel_meta.py`, a dependency-free reader of the two shipped formats (`# key: value` header lines + `C x y z Mat` / `S` / `M` cube lines, and the fine tier `# grid: N` + `V x y z Mat [tint=]` lines, per `docs/FineVoxelItems.md`), returning the same dict keys `inspect_template` reads today, with a unit test over one template of each format from `resources/templates/`; the skills `.claude/commands/asset.md`, `.claude/commands/generate.md` and `tools/phyxel-gamedev/skills/phyxel-assets/SKILL.md` (rewritten for the Meshy route, §14); docs: the CLAUDE.md "BlockSmith AI Model Generation" section (replaced by a Meshy section), `AssetLibrary.md`, `GameDevWorkflow.md`, `game-production/README.md` + `recipes/farming-survival-loop.md`, `WantedAssetsBacklog.md`, `CharacterLibraryPlan.md`, `ForgePattern.md`, `VoxelAppearanceModel.md`, the structure-generation docs that cite it. History comments in `ItemPropManager.cpp:231` and `ItemPropPhysicsTest.cpp:261` stay (they record a past defect). Also to clean: `README.md`, `.claude/settings.local.json`, `docs/AgentContext.md`, `docs/feedback/archive.md`, `docs/structure-generation/FinishDetailPlan.md`, `docs/structure-generation/StructureGenerationPlacers.md`, `tools/phyxel-gamedev/README.md` (F5 found these seven missing from the list). No shipped template depends on it (`resources/templates/generated/` does not exist; the catalog carries no `source: blocksmith`). | red: `grep -rniI blocksmith --exclude-dir=external --exclude-dir=.git` lists 29 files today; green: only the two history comments + this plan; the MCP server imports with the submodule absent; `asset_index.py --validate` still passes |

Docs: fold `WorldForge.md` "Known gaps" into its resolved sections; fix `WorldModel.md` §2;
retire StructureGenerationV2 P8–P10 with a pointer here; one "next" (this file §5).
Validation: L2 unit + one L4 Release run on the canonical 8-site world (`WorldForgeStressTest`
numbers re-recorded). Stress: the 8-site mountain seed. Perf gate: none (no new cost).

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
rim is ≥ spill everywhere → `water_validate rim_leaks == 0` on the bake — the owner's acceptance
test), L3 (`TraversalProbe` along both banks: a character can walk a river bank), L4 (fixed-pose
captures of a confluence, a lake rim, a coast; stated defect in frame before/after). Red tests:
`BasinRimTest` fails on today's 128 u levels; `RiverGeometryTest.NoRightAngleJunctions` fails.
Stress: order-6 trunk across 100 km, 500-lake continent, a lake exactly on a meso tile edge.
Test world: `WorldSheetLab` + `RiverLab` (one tile, one order-3 river into one lake, one coast).
Perf gate: hydro geometry bake per meso tile.

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
| Noise evals per column | ~140 (audit) | ≤ 25 |
| Macro bake 200² | — | < 2 s Release (record) |
| Meso tile bake 256² | — | < 50 ms on a worker (record) |
| Civ + routes plan, 300 sites | all-pairs A* O(n²) at 8 sites today | < 10 s Release, off the main thread |
| Settlement map, 200-lot city | — | < 2 s headless |
| Streaming, dense forest | 7 chunks/min Debug (only number on file) | ≥ 2× Release baseline measured in M1 |
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
| River width / depth vs drainage area | F4b: Leopold & Maddock PP252 is a JBIG2 scan (OCR needed); a USGS OFR 77-38 extract gives downstream b = 0.5, f = 0.4, m = 0.1 (secondary) — consistent with the row. `TerrainGenerationV2.md:356-357`: Doll et al. NC Coastal-Plain regressions W = 10.97·A^0.36 (r² 0.87), D = 1.29·A^0.30, chained through a Horton area ratio ≈ 4 to get the per-order table 2/3/5/8/14/22 u — the doc itself labels the chaining "design synthesis". Leopold & Maddock 1953 ([USGS PP252](https://pubs.usgs.gov/publication/pp252)) exponents w∝Q^0.5, d∝Q^0.4 recalled, ⚠verify | regression GROUNDED; per-order chaining REASONED; the 1/1/1 depth plateau at orders 3–5 is grid convenience (real ≈ 1.0/1.5/2.3 m) — M2 moves to drainage-area-driven width/depth and drops the table | GROUNDED (regression) / REASONED (chaining) |
| Tributary junction angle | [PMC9897458](https://pmc.ncbi.nlm.nih.gov/articles/PMC9897458) (read): global mean junction angle **61°** for near-equal tributaries; groundwater-seepage model tends to 72°; Hooshyar et al. 2017 modes ≈ 49.5° and 75°. Howard 1971 ([abstract](https://arch.neicon.ru/xmlui/handle/123456789/3195164)) and Horton 1932 unread | M2 test: junction angle in [40°, 80°], mean ≈ 61° over the network; never 90° | GROUNDED (observed mean; Howard's own range still unread) |
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
| Rebuild point | `applyRecipe → rebuildCoarseModel` clears memos, re-bakes hydro + plan (`WorldGenerator.cpp:295-422, 1481-1534`); runs **twice per load** (`GameDefinitionLoader.cpp:496/506/546`); hydro memo `g_hydroCache` keyed on type/seed/climateFreq/seaLevel/spline, NOT the plan (`:348-363`) | macro bake memoized process-wide by **recipe hash incl. sheet + worldforge params**; the twice-per-load path hits the memo |
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
| **WorldSheetLab** (new) | M1 | `type: Perlin, seed: 7001, streaming: true, sheet{macroCellU: 512, mesoCellU: 32, continentCells: 16, uplift: "flat+ridge"}` — a 16×16-macro (8 km) sheet, one painted ridge along z = 0 of amplitude 160 u | the ridge is the only relief source; variable = erosion steps (0 → N) | ridge crest (0, crest+30, 0) pitch −20 looking +x; tile seam at x = 4096: (4096, surface+60, 0) pitch −60 (the meso tile boundary must be invisible); valley floor at the first order-3 confluence | erosion steps = 0 (pure uplift: no valleys, drainage is a fan) | drainage is a tree with no sinks; valley density per km² rises monotonically with steps; slope histogram; `MesoSeamTest` bit-equality across x = 4096; column cost ≤ 25 noise evals; macro bake ms, meso tile ms |
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
| F5 | solution-auditor reads this plan's §1 and §13 claims against the code and confirms the file:line facts the plan builds on (an audit of the audit) | agent | **RUN 2026-10-14 — VERDICT: FAIL on rev 2** (≈125 claims: 92 CONFIRMED, 11 STALE citations, 4 WRONG, 6 UNVERIFIABLE-without-a-run). All four WRONG claims (far mesher one-shot; `validate_realized` pre-mutation; partial-placement mechanism; Deps type conflation) and the incomplete BlockSmith parser inventory are **corrected in rev 3**, the 11 stale citations re-cited, and the two ordering caveats (`saveAllChunks` writes pristine chunks; `ensureChunkAt` creates empty chunks) carried into the M0 rows. The 6 UNVERIFIABLE claims (column cost ~140, road dead-end at a wide river, creek notch, mountain-flank hole, gate ships pinched towns, C-100 ≈ 50 ms) are F7's to measure. **Re-audit of the corrected rows owed before F10.** |
| F6 | Lab worlds specified (`WorldSheetLab`, `RiverLab`, `CivLab`, `RouteLab`, `CityLab`): one tile each, poses, predictions written before any run | agent | **SPECIFIED 2026-10-14 (§16)**: recipes, contents, poses, controls and metrics for all six worlds; predictions written for the F7 baseline and M0; per-milestone predictions for M1–M8 are owed at each milestone's F3 design-check, before its first run |
| F7 | Baselines measured and archived in `docs/evidence/worldforge_v2/` before M0 changes anything: `WorldForgeStressTest` numbers, column cost, chunks/s in forest (Release, named GPU), test count (`tools/test_count_check.sh`), the C-25…C-100 city ladder | agent | **DONE 2026-10-14 for everything that has an instrument** (`f7_baseline.json`, `f7_l4_baseline.json`): Release unit suites 176/176; stress step fractions re-measured post-grading (canonical 1/1440 = 0.069 %, mountains 31/6722 = 0.46 %; the doc's 0/1320 and 45/6496 predate grading); **live 8-site build on the laptop: 8/8 built, 37 buildings, 0 lot failures, 541 s**; walkability gate: town 1 blocked, village 5 1 blocked, village 6 clean, **hamlets never gated** (cluster morphology skips the unit) and both blocked sites shipped as 'built' — the M0 blocking-gate red state verified live twice; 40 crossings all order-1 creeks, 0 bridges by design (the creek-notch row); two 'before' captures at the §16 poses with `mode:"free"` (an earlier pair without it showed the spawn void and was discarded). Residue census not measurable (0 refusals on this seed; unit rig owns that red). Not instrumented yet: chunks/s, column cost (M1 adds both). Test count (full suite, Debug exe): **4286 tests, 4265 passed, 19 skipped, 2 FAILED pre-existing** (`AtlasManagerTest.BuildAtlasFromSourcePNGs`, `FineFaceMerge.SubcubeMerge_CrossCubeSplitsOnLightBoundaryBetweenCubes`; no source changed on this branch) — `f7_test_count.txt`. |
| F8 | `feat/water-rethink` merged into `main` by the owner; `feat/worldforge-v2` rebased or merged onto it before M1 | owner | NOT DONE (M0 may proceed before this) |
| F9 | Doc consolidation list (§1 "doc rot") confirmed complete as part of M0's definition of done | agent | NOT DONE |
| F10 | Owner sign-off on this plan as a whole (after F3–F7 are folded in) | owner | NOT DONE |

Order: F3 (M0 first) → F4 → F5 → F6 → F7 → F10, then M0 begins. F3 for M1–M8 is run when each
milestone is next, not all up front, so each design-check sees the code as it then is.
