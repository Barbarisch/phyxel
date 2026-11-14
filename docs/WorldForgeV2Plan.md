# WorldForge v2 — the Ultimate World/City Forge (plan of record)

> **Status (2026-11-14, rev 4): M0-M4 and M5a / M5a-1 BUILT and committed on `feat/worldforge-v2`; M5b-1 (the street tiers grown from the map) built with open items, its closure record and two audit passes in the build log (ledger `docs/evidence/worldforge_v2/m5b_red_green.txt`; final tree: chains 138-146 and L4 run 12's fourth launch) — its gates O14 and O15 are NOT closed, O9 / O19 carry what was not measured, and two tests stand red (the O4 river lip, the M4 hairpin), see §5.0; M5b ruled 2026-11-06 (D43-D45); the lot's approach as map data (sequencing amendment 1, leg M5b-1a): increments A-C built with open items 2026-11-13 (the per-style threshold, the door and style, the way level / pad / threshold on the map lot; D47-D49 PROPOSED, not ruled; open items O21); O21 (a) and increments D-E built with open items 2026-11-14 (the restoring unit scoped to tree pits on map sites; the door's approach cells stated on the seated map lot and imposed by the doorsteps unit; the sloped headless rig at MAP level only; audit PASS WITH CONDITIONS - the approach is shown placed as stated, not shown correct); next: M5b-2 (starts with a design check), with O21's remainder carried; M5c-f, M6-M8 not started. What is still red, unmeasured or unowned is in §5.0. Build records (BUILT / AUDIT / L4 paragraphs) moved verbatim to [`WorldForgeV2BuildLog.md`](WorldForgeV2BuildLog.md) in rev 4; this file keeps contracts, decisions, design checks and open items.**
>
> *Rev 3 status (2026-10-14), kept: SIGNED OFF; M0 in progress.* Rev 3 carries the F4/F4b grounding results (§10), the F5 audit-of-audit corrections (four wrong claims fixed, eleven citations re-cited), the F6 lab worlds (§16) and the M0 design-check verdict. Written from a
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
| T8 | **Cost**: column sampling ≤ 25 noise evals (GREEN, 18.6); chunks/s in forest is a REPORTED number since D13 (the "≥ 2×" gate withdrawn; measured 4.6× on 2026-10-20), on a named GPU | `WorldGenBench` red/green numbers in `docs/evidence/` |

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

### 3.1 Decisions (D1–D46) — **D1, D4, D6 SETTLED by the owner 2026-10-14 (all "yes")**; the table is in number order (rev 4); D46 is OPEN

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
| D12 | **The terrain-pinned water tests are NOT re-baselined now** (owner, 2026-10-15: "dont worry about the other water stuff for now") — with D10 they stay on the version-1 path and do not move; revisit only if a water fixture is ever pointed at a version-2 recipe | — | re-record water numbers under the sheet |
| D13 | **The §9 streaming gate is re-scoped (owner, 2026-10-15: "totally agree, we need to be able to measure these type of things"):** chunks/s on the baseline pose is a REPORTED number, not an M1 gate; the per-chunk worker cost (`generateChunk` 138 ms + flora 42 ms, measured) gets its own perf milestone with its own red-before-green profile; the recorder's `streaming.{worker_gen_ms, worker_decorate_ms, finalize_ms}` fields and the `Dirty Remesh` scope are the standing instrument | a gate must measure the cost it names; this one did not | keep "≥ 2× chunks/s" on M1 |
| D14 | **The R0 post-teleport worker crash is chased and fixed before M2** (owner: "we probably want to chase and fix a crash if there is one") | a crash that fires 2 of 5 teleports blocks every L4 run | park it as a water-side note |
| D15 | **M2 drainage is the HYBRID (owner, 2026-10-20: "go with your recommendation"):** the macro erosion bake (M1c receivers, areas, filled surface, kept on the `MacroSheet`) is the global boundary condition and every lake's spill height; each meso tile routes its own flow with a derived apron and takes inflow at its edges from the macro graph. The seam invariant is CONTINUITY (a channel leaving tile A at an edge cell enters tile B at the matching cell with order ≥ the exiting order; a basin's spill is identical on both sides), not tile-equals-whole-region | per-tile cost and memory on the laptop; lake levels continuous across tiles by construction | a full 32 u meso bake per recipe (~10 M cells, ~130 MB resident) |
| D16 | **M2 may change how rivers LOOK on version-2 worlds** (owner, 2026-10-20: "yes"): spline centrelines, banks from drainage, smoothed confluences replace the meander-warped cell segments; version-1 worlds keep today's carve untouched (D10), so Ravenmere and the water tests do not move (D12). Read by the agent as "yes, change it"; if the owner meant "keep the old carve as a v2 option too", add a recipe switch | the old carve's right-angle junctions and 55 u warp are what M2 exists to replace | keep the cell-segment carve on v2 |
| D17 | **WorldForge makes water fixture SHAPES only and never populates a feature with water** (owner, 2026-10-21: "i dont care how you handle water for now, i really prefer you to just make the water fixture shapes and not populate any feature with actual water. the engine water system is still being realized and is not ready to incorporate into anything else yet"). Basins (rim + floor + real spill height), channel beds, banks, floodplains, shelves, coasts and spring SITES are terrain geometry + data handed over through §4's adapters; no WorldForge milestone edits the water runtime, binds through it, or uses `water_validate` / water captures as its acceptance test until the owner says the water core is ready. The M2c "lake shrink" question is therefore not a water question: the basin SHAPE is what we deliver, and its real spill (the rim minimum) is a shape fact | the water core is a moving target; WorldForge verifies its own geometry (generator-side tests such as `BasinRimTest`, captures of the dry shapes) | wiring water now, or shaping basins to a runtime that will change |
| D18 | **Sites are chosen at MACRO resolution over the whole continent and refined locally** (owner, 2026-10-22: "i will go with your recomendations"): the 512 u sheet (base, continentalness, climate, drainage area, filled surface, receiver graph) is the suitability field for all ~300 sites; a site inside the meso box is snapped to its best meso cell by the existing scoring, sites beyond it keep their macro position until the meso bake reaches them (planned, not realized or routed — stated) | the meso box is 32 km, the continent 102 km; the full-continent meso bake is ~10× (≈ 300 MB, ~35 s Release) and is a later measured increment | grow the meso box first |
| D19 | **Tier targets are derived from Christaller's ladder inside the Domesday anchor** (owner, 2026-10-22): spacing city 62 / town 36 / village 21 / hamlet 12 / outpost 7 km (the √3 ladder, §10 F4), density per tier ≈ 1 / (0.866·s²) ⇒ ≈ 0.03 / 0.09 / 0.26 / 0.8 / 2.4 per 100 km² ≈ 3.6 per 100 km² total (≈ 360 on the 100 km continent; Domesday's ≈ 10 per 100 km² is the ceiling, T2's 300 the "gameplay-thinned" target) — recipe fields, DESIGN inside grounded anchors | one ladder, one anchor, no hand-typed counts | hand-picked counts per tier |
| D20 | **M3a sites first, M3b history second** (owner, 2026-10-22): M3a delivers the sited, tiered, deterministic plan with the spacing and hierarchy invariants; the growth simulation, ruins and the history ledger follow as M3b with their own design check | "300 sites" means live sites until M3b defines founded/abandoned | history inside M3 |
| D21 | **History never changes a site's tier** (owner, 2026-10-22, took the recommendation): tiers stay the ladder's at founding; `state` (live / declining / abandoned) and the population index modulate realization density only | the spacing and hierarchy invariants M3a proved stay provable | a declined city realized as a town |
| D22 | **A ruin in M3b is a site state + map glyph only** (owner, 2026-10-22): the ruin DRESSING is a structure-forge typology for M5/M7 | keeps forge work out of a siting milestone | M3b dresses ruins |
| D23 | **Default arc: 12 epochs × 25 years, one shock at epoch 9 of −45 %, `civEpochs` shipped at 0** (owner, 2026-10-22): history is off until an engine run shows the ruins read well; at 0 the plan is M3a bit for bit | the England 1086 → 1377 anchors (§10 F4g) | history on by default |
| D24 | **The blue-noise / annealed site pick is DEFERRED** (owner, 2026-10-22): the greedy pick's ~70 % of the hex ceiling stands; revisit as its own increment (M3c) after M3b. **REVISITED 2026-11-02 (the M3c design check before M5): the gap measured by a red test — the plain city 3/3, town 6/6 (100 %), village 13/18 (72 %), hamlet 38/56 (68 %), outpost 112/162 (69 %) — and the relaxation that would close it designed; RULED 2026-11-03 (owner: the recommendation): (b) BUILT — the M3c paragraph; the plain at its target on every tier by a nested integer lattice (four mechanisms, the bound unchanged).** | keeps the history diff readable | inside M3b |
| D25 | **M4 in three increments** (owner, 2026-10-22, took the recommendation): M4a continent corridor graph on the macro sheet + box realization re-routed along the corridors; M4b per-class grade caps + the blended grade field + switchbacks + derived earthworks; M4c bridges/fords/tunnels as edges | three reds, three mechanisms | one L increment |
| D26 | **Per-class grade caps 0.05 / 0.08 / 0.15 on version-2 recipes** (owner, 2026-10-22): the shipped 0.5 is a MISMATCH and stays only on the pinned version-1 path; the cost (longer routes, switchbacks, cuttings on mountain seeds) is accepted | the grounded sources in section 10 | a relaxed track cap |
| D27 | **Reuse fraction 1/3, labelled REASONED** (owner, 2026-10-22): shipped and measured against the V1 MST-plus-detour network; a grounded value researched in parallel. **RESEARCHED 2026-11-02 (grounding-auditor; §10 row "Route reuse fraction"): no source prices a network cell as a fraction of a fresh cell; the nearest grounded figures are travel-TIME ratios on a path vs off it — Tobler 1993's off-path factor 0.6 (GROUNDED as a flat-ground walking-speed ratio) and the US Army's 2.5 mph on roads vs 1 mph cross-country march rate (0.4; a bibliography snippet, the primary field manual not opened). The 1/3 sits just below that 0.4–0.6 bracket. OWNER DECISION PENDING: keep 1/3 (a stronger merging bias, a design preference inside no source) or move to 0.5 (the bracket's midpoint, REASONED) — a change moves every version-2 road network and plan hash, so the golden, hash-pinned and census tests re-bake. **RULED 2026-11-02 (owner: the recommendation): 0.5, REASONED as the bracket's midpoint — `routeReuseFraction` default 0.5 (`WorldForgeParams.h`, the absent-key fallback follows the default; a persisted recipe keeps its own value). Red → green `d27_red_green.txt`: `RouteGraphTest.ReuseHalfIsTheDefaultAndSitsBetweenThirdAndNone` (the baseline's network junctions 295 / 343 / 636 at 1/3 / 0.5 / none; distinct cells 2941 / 2897 / 3046, route count 152 in all). The ripple re-measured, not carried: baseline 31 roads / 2 bridges, Mountains 25 / 26 / 8 refused; 0 wet road columns; one hairpin cliff defect found and fixed, two rig premises corrected; chain 35 earth 10/10, forge 96/96, terrain 105/106 (the six lips)** | Tobler 1993 (0.6); US Army march rates (0.4, verify); Helbing 1997 gives no number; Louf 2013 has no reuse term | keep the V1 detour rule |
| D28 | **Roads beyond the hydrology box are stamped from the macro corridors at macro fidelity** (owner, 2026-10-22): material + drape, no grading, no crossings; the box is where a route gains grade, earthworks and bridges | continuity across the continent without a moving or full-continent bake | a moving bake; the full-continent meso bake |
| D29 | **Rough-mountain roads: pitched roads accepted now, retaining-wall earthworks next** (owner, 2026-10-27, took the recommendation): the class caps are a ruling grade for favourable ground; on rough ground a road climbs in short pitches up to the walkable 0.5 with cuts bounded at the class floor except at cliffs, every figure reported per plan; the cut faces beyond the repose shoulder become dry-stone retaining walls in the next M4 increment (M4b-4); fine-resolution routing stays off | the pitched length is sub-128 u roughness, not slope (M4b-3 diagnostics) | fine-resolution routing (5-6 min Debug per box plan, no gentler line found) |
| D30 | **Tunnels DEFERRED** (owner, 2026-10-27, took the recommendation: no medieval precedent at our scale; a mountain route is pitched or walled) | Furlo (38 m, AD 77) the one Roman road tunnel of note | bored tunnels |
| D31 | **The grounded span rule** (owner, 2026-10-27): single timber span ≤ 6 u, piered ≤ 25 u OVERALL (audit: shoulders included), piers every ≈ 10 u; wider channels uncrossable for routing; version 1 keeps 96 u | Sherway, Hemington, Aldwincle; §10 | decking any width |
| D32 | **Fords = creek crossings** (owner, 2026-10-27): channel depth < 0.5 u = the M2b-2 class split; deeper water is bridge-or-nothing | NWS 0.15 m moving water; the class split | fords to the pin depth |
| D33 | **Cuts beyond the rock-cut precedent — RULED 2026-10-28 (owner: "recommendations" → (b)).** a walkable road across a cliff cuts up to 80 u on the Mountains world; the only sourced road cut is 36 m (Pisco Montano, vertical rock). Options: (a) realize as a rock face up to 36 u and REPORT deeper cuts as unprecedented (the D29 acceptance, now visible as rock); (b) make a cut beyond the precedent a refused EDGE like a crossing (the M4c lazy check: realize → judge → close the cells → re-route, else refuse the demand with the reason); (c) cap the envelope and let the road be unwalkable there (rejected by the M0 walkability gate). **Recommended: (b)** — the M4c machinery exists, a demand that can only be served by an unprecedented cut is honestly refused or goes around, and the Mountains refusal count becomes the measure of the terrain, not the road | one data point (36 m) | (a) |
| D34 | **One dry-stone tier, no terracing — RULED 2026-10-28 (owner took the recommendation).** a dry-stone retaining tier is supported to ≈ 2.5–3 m; terraced tiers exist only in modern segmental guidance with benches ≥ 2 × the lower wall, which stacks at a slope of ≈ 0.43 — SHALLOWER than the 0.70 repose bank, so a second tier never beats widening the bank. Recommended: one tier ≤ 3 u at the road edge, the repose bank above it over the 12 u shoulder, the remainder a rock face (D33). Alternative: cosmetic terraces (tiers with benches) for the look | 2.5 m trials; CMHA 2H rule (modern) | terraces |
| D35 | **Wall section — DERIVED, for the record (M4b-4):** thickness ⌈0.67 × tier height⌉ u (the loaded-wall base ratio; 1 u up to 1.5 u of wall, 2 u up to 3 u), batter one subcube (1/3 u) set back at the top course ≈ 1:9 (inside the 1:6–1:10 rule), material `Cobblestone` (the settlement's dry_stone), the rock face `Stone`; not an owner decision unless the look is wrong | handbook ratios | 1 u walls |
| D36 | **RULED 2026-10-29 (owner: "recommendations"); BUILT 2026-10-30 (M2d-4, the floor half-width shrinking with the bed slope, REASONED).** **The valley section on version-2 carved channels: a flat alluvial floor at the bankfull rim across `kValleyWidthMul` × the channel width, then a valley side rising at ≤ 0.70 per u, the natural ground winning wherever it is lower or itself steeper (a cliff the mountain has stays); replaces the fractional relief attenuation, which on 100–300 u of relief makes walls of 10–64 u per column** — RECOMMENDED (M2d-4 design check, 2026-10-29) | 0.70 = 35°, the top of the 30 ± 5° threshold-hillslope band (Frontiers Earth Sci. 2021, 10.3389/feart.2021.684365, read) and = the GROUNDED scree repose the roads use — REASONED as a bound; the flat floor REASONED (a floodplain is a flat landform), the 5 × width stays the labelled tunable | alternatives: 0.58 (30°, the modal value — wider valleys, more cut); keeping the fraction with an absolute depth cap (the creek fix) cannot hold mountain relief |
| D37 | **RULED 2026-10-29 (owner: "recommendations"); BUILT 2026-10-30 (M2d-4, the footprint of the EMITTED natural + the perch rule).** **The bed profile's base per CARVED cell = the minimum of the unattenuated ground over the channel's WARPED footprint (5 points along the cell's polyline × the centreline and ± half-width, each at the column under the warp; DESIGN), not the cell-centre point sample** — the embankment fix: the rim then never stands above a natural column inside the footprint by construction, the residual bounded by the sampling and asserted ≤ 1 u; creeks never fill, so they keep the point sample; the downstream minimum envelope stays — RECOMMENDED (M2d-4 design check, 2026-10-29) | natural levees: a few cm to 3 m above the floodplain, ≈ 20 % of bankfull depth (PMC7780550, read) — the 1 u bound GROUNDED-conservative | alternative: keep the point sample and clamp the fill — a river cannot be refused, so a clamp leaves the embankment standing at the clamp height |
| D38 | **RULED 2026-10-30 (owner: "go with your recommendations" → (b), (a) until a number); BUILT the same day (the paragraph before M5).** Cascades: a natural step steeper than the valley side — (a) entrench; (b) a CLIFF allowance at the lip; (c) a bounded gorge. Built: (b) with kRockFaceSlope 4.0 per u (76°, 0.25H:1V — GROUNDED to an engineering standard, a borrowed road-cut inclination, not a measured gorge wall; the natural face is vertical with no angle below it in the source) and the penalty slope 1.3 DERIVED (re-derived from 1.65 when the along-slope behind a head was added, patch 25); a step beyond the budget entrenches by the remainder. **The description that led to the ruling was wrong about the cause** (the floors wrapping heads and the lever flattened every channel, not the cascades — 20 of 1 984 reaches); the cause is fixed with it. | the natural ground has 1 479 steps beyond 0.70 on Mountains 424242; Haviv 2010: a waterfall face is vertical; Iowa DOT 200F-8: rock cuts to 0.25H:1V short of vertical | — |
| D39 | **RULED 2026-11-04 (owner): the Roman insula** — the second era for M5f (D9's falsification; its grid exercises the planner's non-organic branch). Was: the insula or the Edo machiya | one instance of an abstraction is an assumption | — |
| D40 | **RULED 2026-11-04 (owner): proceed with the forms and the program's grounded dimensions, every bell parameter labelled** (M5a: the hamlet's `bells` block, road access GROUNDED, the rest REASONED / NEEDS-RESEARCH); a grounding pass per tier as its own task | the §10 Emilien row | — |
| D41 | **RULED 2026-11-04 (owner): every tier from M5a** — every program tier is realized from the map (the street tiers' map is the main-street scenario as built until M5b-e grow them); the M0 contract re-proven on the map | the plan's M5 contract says "for each site" | — |
| D42 | **RULED 2026-11-05 (owner: "go with your recommendations"): the cut bank at repose RELATIVE to the natural** — the generator's design as built (`gradeBlendAt` blends the road grade into the natural across the shoulder), so the uphill bank's 0.83 absolute on the baseline is the design and the integer-aware bound stands as measured (`RouteFieldTest.CutBanksStandAtTheReposeSlope`); item (a) of the same STATUS header, the shoulder at exactly repose, was not put to the owner and stays stated | the M3c cut-bank STATUS header, item (b) | — |
| D43 | **RULED 2026-11-06 (owner: "go with your recommendations"): (b) civ-enabled recipes derive their site rects from the program, version-1 keeps the literal presets** (was: (a) every recipe with a re-pin, (c) a recipe key) | `site.width/depth` are identity in `planHash`; the hamlet 40 × 32 is REASONED and seats 5 / 3 / 2 against the 3-6 band | — |
| D44 | **RULED 2026-11-06 (owner): a house parcel is the conquered SUBSET of the nominal toft** (flat = today's rect, shortened across a slope); the full tessellation (parcels conquer the land between tofts) deferred to M5c with the fields | Emilien 2012 §5.3 read 2026-11-06; the growth's blocking rect and sociability bell stay as measured in M5a | the full tessellation in M5b (re-opens the M5a invariants) |
| D45 | **RULED 2026-11-06 (owner): map the civ ladder's cities to the city PROGRAM in M5b-1** (today city → town at `WorldGenerator.cpp:701`: the box plan has no city rect), the city's first L4 gated on the measured Release bake (< 2 s, else city stays → town, recorded) | the city program never realizes from a worldforge build today | keep city → town until M5e |
| D46 | **OPEN (raised 2026-11-09, rev 4): where the second era sits in M5.** D9 says the second era is built EARLY in M5 as the falsification of the era hook; the M5 increment list puts it last (M5f, after parcels, rings, the wall and the precincts). Each increment built before it adds single-era assumptions (M5b-1 added a measured threshold offset per style and a street-level seating rule tested on the medieval forge only). Options: (a) keep M5f last; (b) move it to directly after M5b-2 (recommended: the map, the growth, the ways and the parcels are then the surface it falsifies, before rings and walls are built on them); (c) a hamlet-only era spike now, the full pack at M5f | risk 5 (one-instance abstractions calcify) | — |
| D47 | **PROPOSED 2026-11-13, building on the recommendation (owner: "keep goin." after the design check - no explicit ruling; reversible until the leg closes): (a) each map way carries its graded centreline PROFILE and the paver stamps from it** **- CORRECTED 2026-11-13 after reading the paver: the stamped surface is the centreline grade broadcast over the capsule, first writer at junctions, then a 2-D slope-limited lower envelope - not a profile; (a) as written cannot state the level at a door. BUILT INSTEAD: the planner runs the paver's own pure plan (`planWayPaving`) and each lot carries its way level, pad and threshold (closer to (b)); the paver / map agreement is measured per seat** (was: (b) the planner calls the paver's plan and keeps only the level at each door) | the seat reads the way's level from the chunk-side paving stamp (`SettlementBuildService.cpp:1022-1050`: centre column, lowest column, a ring of neighbours); M6 needs the way grade as a pure function of the map | the paver and the map disagree at junctions under (b) | owner |
| D48 | **PROPOSED 2026-11-13 (as D47): a forge door that differs from the map's door REFUSES the lot** (recorded in `lot_failures`, like any forge refusal; was: count it and let the walkability gate decide) | the door is predicted by re-running the forge's deterministic autofill; a mismatch means the map's pad and approach were planned for another door | a silent mismatch ships a stoop at the wrong cell | owner |
| D49 | **PROPOSED 2026-11-13 (as D47): a map lot PINS its style, and a map lot has no variant re-roll** (the fallback variant could change the style, and with it the threshold the map states; a typology layout ignores the footprint shape, so the re-roll changes nothing else) | `pickBuildingVariant` draws the style by lot index in the realizer; the threshold above the pad is 9 + the style's floor thickness in micro (12 for 0.333, 14 for 0.5 - the unexplained "14 vs 12" of O19) | a refused first variant leaves the lot empty where today a second style is tried (0 lot failures in every live run of M5b-1) | owner |
| D50 | **PROPOSED 2026-11-14 (M5b-2 re-check): the footprint and the setback strip are in the parcel unconditionally** - the conquest starts from the BACK row of the footprint, not from the way, and spends its fund (the back-yard depth = depth - setback - dDim, + the half cell of D52) on the back yard only | Emilien 2012 s5.3 conquers from the road cells; under D44 (a) the building is already seated, so a conquest from the road could stop short of its own building on a steep lot (a building outside its parcel) | the parcel can never exclude its building; a steep lot keeps a zero-depth back yard, not a refusal | - |
| D51 | **PROPOSED 2026-11-14: the mask is regularized to a depth PROFILE** - per column across the frontage, the cells from the footprint's back row to the deepest cell the Dijkstra reached contiguously; side-yard columns take the profile of the nearest footprint column, capped by their own reach | Emilien 2012 s5.4 simplifies the conquered contour to straight lines; on our cube grid the polyomino is the record (D5), and an unregularized mask has holes and one-cell notches the fence would have to trace | DESIGN; the fence stays a few straight runs with steps; (m) counts runs per lot and prints the maximum | - |
| D52 | **PROPOSED 2026-11-14: the slope cost uses the WINDOWED directional gradient and the fund carries half a cell of slack** - step cost 1 + lambda_s x g^2 with g = (h(p + 6 dir) - h(p - 6 dir)) / 12 along the step (the 13-cube window the relief cap already uses), lambda_s = 4.7 (REASONED, the first check: a step at the TooSteep gradient 0.46 costs twice a flat one); a row is lost only when the accumulated slope cost exceeds 0.5 | adjacent-cell differences on integer terrain are 0 or 1: a single cube step would cost 5.7 cells of yard, and without slack ANY slope loses the last row (a knife edge at fund == depth) | REASONED / DESIGN; consequence stated in the check: parcels on <= 5 % ground are their rects | - |

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

### 5.0 Open items register (rev 4, 2026-11-09 — every standing red, unmeasured figure and unowned promise in one place)

Collected from the records now in the build log and from §9 / §12. A row leaves this table only by a red → green with its
ledger entry, or by an owner ruling that strikes it. "Owner" is the milestone that must close it.

| # | Item | Kind | State as recorded | Owner |
|---|---|---|---|---|
| O1 | Meso tile bake 256² | perf gate RED | 76.2 ms Release vs < 50 ms (the climate planes are 8 of 18 evals per cell) | M1 follow-up — unscheduled |
| O2 | Release column timing and Release hydrology bake after M2d-4 / D38 | unmeasured | only the eval-count proxy ran (18.6); the Debug v2 bake grew 12 → 18-21 s; the M2d ≤ 10 % column gate was MISSED at +15 % | M2 follow-up — needs a Release test build |
| O3 | Plan bake on the game loop | contract breach | §9: plan < 10 s Release OFF the main thread; MountainLab bakes 12.5-15.6 s on the game loop at boot; Debug fixtures 56-214 s per box | M4 follow-up — unscheduled |
| O4 | Six Mountains lips (`RiverGeometryTest.ChannelSectionsMeetTheLandAtABoundedSlope`) | RED by design since chain 38 | 4-6 lips under 2 u where the bed is steep between adjacent columns | M2d — owner hold |
| O5 | Hairpin bank steps (`EarthworkTest` hairpin sweep) | RED, owner hold | 3-cube bank step at 15 of 73 apexes (the shoulder's outer reach on three-road bundles) | M4b — owner hold |
| O6 | Grade-cap census over every version-2 road | not built | Mountains road 3 reads 0.44-0.50 per u between vertices at a hairpin against the 0.08 cap | M4b |
| O7 | Deck necessity; flush plank end; parapet at bank ends | unfalsified / open | no fixture has a bridged channel deep enough for a deck-removed control; a 1 u lip at every plank end fails the strict walker | M4c |
| O8 | Water-refusal record coverage; macro-stamp crossings outside the box; a synthetic hairpin / switchback rig | coverage gaps | each stated in its record, none has a test | M4 |
| O9 | M0 rows never given a test: `SecondRoadGetsASpur`, `RefusalIsClean` (cell census), the unseated-only L4, a live round-trip of the six MCP mirrors | owed since M0 | **the sweep row CLOSED 2026-11-10**: `SweepNeverErasesPlacedObjectCells` red in chain 122, green in 123 (`Core::planStreetSweep` keeps cells inside a placed prop's box; live, the band reached 5 of 4098 cells inside props' boxes on the town and 4 of 1602 on village 2 — box cells, not counted voxels). Still owed: a prop's voxel count across the sweep with the guard off / on (the service's wiring has no test), and the other four rows | M0 follow-up — unscheduled |
| O10 | M2 contract scope never built: underground bodies (aquifer shapes), deltas and mouth widening, the lake shelf, the L3 bank walk (`TraversalProbe` along both banks) | unowned | the contract paragraph still lists them; no increment carries them | M2 — needs an owner ruling: schedule or strike |
| O11 | M4 contract scope never built: road crown / camber, far tiles drawing bridges (→ M7), the 1000-route stress, tunnels (DEFERRED by D30) | unowned | as O10 | M4 / M7 |
| O12 | M1 remainder: Flat / heightmap trivial sheets, the `sheet_tiles` DB cache, far-mesher `configure` on every rebuild path, two-layer bedrock / sediment, orographic climate | unowned | listed as "remaining" in the M1e record | M1 follow-up — unscheduled |
| O13 | M3 remainder: a shock-dependent ruin mechanism (the ruin count does not move with the shock size), war, the lattice snap takes the nearest viable cell not the best-scored | NEEDS-RESEARCH / open | stated in the M3b and M3c records | M3 |
| O14 | The city perf ladder (T4a, §13.5): `CityBench_*` re-run on every M5 increment | SKIPPED | not run for M5a, M5a-1 or M5b-1: the `CityBench` project is absent on the laptop and the ladder is a 4090 campaign. M5b-1 took one same-pose face count instead (village 1, Release: 338,872 faces as built / 369,326 from the map, +9.0 %) — not the ladder | the next session on the 4090, then every increment |
| O15 | Settlement map bake at the contract's scale | **measured 2026-11-10, gate NOT met** | Release, laptop: 233 lots 3.15-3.26 s (the rig cannot stop at 200), 136 lots 0.90-0.94 s, the city as programmed (27 lots) 0.13-0.15 s; the time grows faster than the lot count (x 1.71 lots -> x 3.4). The 233 / 136 rigs are the cluster growth with the hamlet program on a flat field, the city figure a flat 160 x 120 (not the derived 160 x 160); not profiled; taken before patch 18. Gate < 2 s for a 200-lot city: open | M5b-2 / M5c (where the lot count grows) — profile first |
| O16 | M5 L4 as contracted: a 200-lot city, residents ON, on `CityLab` | not run | every M5 L4 so far is CivLabE0 hamlets / villages / a town with residents off; `CityLab` is specified (§16) and unbuilt; CivLabE0 has no civ city in its box | M5e (the city) — CityLab built before it |
| O17 | Era hook falsification position | OPEN decision D46 | — | owner |
| O18 | R0 post-teleport worker crash | intermittent, unreproduced | 2 of 5 teleports on 2026-10-15, 36 clean after; Release PDBs now on | watch (every L4 keeps its ENGINE DIED check) |
| O19 | Unexplained at M5b-1's closure | open | (a) a `worldforge_build` job result reports `"residents": 0` per site while the editor's outliner lists `res_*` NPCs at the town (L4 run 12); (b) a village on the generator's ground under a 96 x 28 rect seats 5 of target 9 at buildable fraction 1.0, where the flat field seats 9 (chain 132); (c) the village's measured pad offset reads 14 against the town's 12; (d) `EarthworkTest.HairpinBankHasNoCliffStepWithItsControl` is red (15 cliff steps) and has been since M4; (e) D45 (civ city -> the city program) has no test and no live city; (f) a creek across a village site is refused ("no dry route for the main street") where the M5a rows planned around it | (a) M6 (residents at remote sites); (b)(c) the approach-as-map-data leg; (d) M4 follow-up |
| O20 | Process: `SettlementTerrainPlanTest` was outside the per-increment test set through M5b-1 (chains 88-131) and broke unseen; found by the broad regression at closure (chain 126) | fixed for this leg (chains 132-133) | the suite joins the per-increment set of every M5 leg | every M5 increment |
| O21 | M5b-1a: what is open at its closure (build log, 2026-11-13 and 2026-11-14) | open | (a) SCOPED 2026-11-14 (map sites only, the tree-pit signature, a Debug test with four fixtures): what remains - a log post in a subdivided surface cell matches the signature, the skip paths have a live count of 0, one biome; (b) D48 / D49 failure paths and the "map seat where the stamp read nothing" branch never taken; D49 leaves a refused map lot empty; (c) one lot per site has no seat; (d) the planner's terrace rule is a second copy of the realizer's; (e) 49 columns west of the town stand above the generator's `surfaceY` (unexplained; the planner's ground model is wrong there); (f) the walkability gate counted 34 / 0 with a too-steep door spur; (g) the map bake grew (Debug live village 420 -> 560 ms, town 777 -> 1350 ms; Release 82-237 ms) - see O15; (h) O19 (c) is EXPLAINED (the threshold offset is per style: 12 / 12 / 14); (i) 2026-11-14, increment D: lots WITHOUT a seat are common (way more than 3 cells from the door wall: live village 2 9 seated of 13, flat village rig 4 of 9) - the map states no pad / threshold / approach there and the realizer derives them itself; a map-silent door with an empty reading prints nothing; (j) town lot 4: the map's approach and the realizer's reading differ (a raise onto the way's edge cell); (k) the approach assertions have no fresh red (`approachDiffers` bound fitted to lot 4; `imposed ==` / `notStanding == 0` never seen failing, standing read at the centre column only); the "thresholds differ" branch never taken; (l) the hamlet tier's doors not measured; (m) increment E is MAP level only - the realizer on a synthetic sloped field is not built, no mutation test of the map level | carried into M5b-2: (i) is a design input to parcels (the seat's reach); (j)-(m) owed before any claim that the approach is correct |

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

**M0 L4 run 1 (2026-10-14, Release, `tools/worldforge_m0_l4.sh`, `docs/evidence/worldforge_v2/m0_l4_summary.json`).** Record moved verbatim to [`WorldForgeV2BuildLog.md#r001`](WorldForgeV2BuildLog.md#r001) (5 KB).

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

**M0 L4 run 5 + the water merge.** Record moved verbatim to [`WorldForgeV2BuildLog.md#r002`](WorldForgeV2BuildLog.md#r002) (5 KB).

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

**M1a BUILT 2026-10-15.** Record moved verbatim to [`WorldForgeV2BuildLog.md#r003`](WorldForgeV2BuildLog.md#r003) (1 KB).

**M1b BUILT 2026-10-15.** Record moved verbatim to [`WorldForgeV2BuildLog.md#r004`](WorldForgeV2BuildLog.md#r004) (5 KB).

**M1d BUILT 2026-10-15.** Record moved verbatim to [`WorldForgeV2BuildLog.md#r036`](WorldForgeV2BuildLog.md#r036) (2 KB).

**M1c BUILT 2026-10-15.** Record moved verbatim to [`WorldForgeV2BuildLog.md#r037`](WorldForgeV2BuildLog.md#r037) (8 KB).

**M1e, first measurement 2026-10-15.** Record moved verbatim to [`WorldForgeV2BuildLog.md#r005`](WorldForgeV2BuildLog.md#r005) (7 KB).

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
**M2c correction 2026-10-22 (D10/D12):** the corrector is now constructed for VERSION-2 bakes only. On a version-1
recipe the corrected level had moved an existing world's WorldForge plan — the Mountains 424242 fixture went from
10 roads / 3 bridges (plan hash 18197215997962677593, every run 2026-10-15 → 2026-10-20) to 13 roads / 3 bridges
(15040555740693915406), and the re-routed network brought back a 2-cube step at a road junction
(`RoadFieldSeamTest.GradedRoadsAreStepWalkableOnMountains`: 3 of 9 917 centreline steps; grade 109.51 → 108.66 between
adjacent columns where roads 0-4 and 1-4 overlap near site 4). The M2c run had not included that fixture. Version-1
worlds keep the raw coarse spill everywhere (plan, grading gate, flora/fauna gates, water table), as before M2c.
The junction step itself is a latent defect of the nearest-road grade seam (vertex-only reconciliation between
corridors; the grade between two reconciled vertices can still disagree where the nearest road switches) — logged
in §10 as a v2 road-grading item, not fixed here. **Second drift found the same day:** with the corrector gated, the
Mountains plan came back at 10 roads / 3 bridges but under hash 6997471292175526340, and EVERY version-1 fixture hash
had moved (the lab's 9693137997320541182 → 17476495390967643798, the 15-road 9087854481993593608 → 7021189206181386486)
— M2b-2 had added `carved` to every crossing record in the hashed plan JSON, the exact identity-only trap M0 row 2b
records (`raster_truncated_segments`). `planHash()` now strips `carved` (reported, never hashed); and
`WorldForgeStressTest.VersionOnePlanHashesArePinned` pins the two version-1 hashes (Perlin 20260816 =
9693137997320541182, Mountains 424242 = 18197215997962677593) so a moved version-1 plan is a red test, not a log line.
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

**M3a BUILT 2026-10-22 (D18–D20 settled by the owner; red → green, `m3a_red_green.txt`).** Record moved verbatim to [`WorldForgeV2BuildLog.md#r006`](WorldForgeV2BuildLog.md#r006) (2 KB).

**M3a L4 2026-10-22 (Release, `CivLab` = `PhyxelProjects/CivLab`: the WorldForgeBaselineV2 recipe + `worldforge.civ.** Record moved verbatim to [`WorldForgeV2BuildLog.md#r007`](WorldForgeV2BuildLog.md#r007) (1 KB).

**M3a STRESS 2026-10-22 (`CivPlanStressTest`, synthetic macro sheets, red → green `m3a_stress_red_green.txt`).** Record moved verbatim to [`WorldForgeV2BuildLog.md#r008`](WorldForgeV2BuildLog.md#r008) (2 KB).

**M3a AUDIT 2026-10-22 (solution-auditor, verdict FAIL overall on the first pass; `m3a_red_green.txt` "AUDIT FOLLOW-UP").** Record moved verbatim to [`WorldForgeV2BuildLog.md#r009`](WorldForgeV2BuildLog.md#r009) (2 KB).

**M3b design check (2026-10-22, the history ledger — run against D20 and the M3a code as built): NEEDS WORK — three owner decisions, then engineering.**
1. *Voxel aesthetic:* M3b draws nothing itself; a ruin is a site STATE that the structure forge will dress later (an unroofed, part-collapsed typology authored in subcubes/microcubes like every other building — the forge's job, not M3b's). Pass, provided D22 keeps the ruin dressing out of M3b.
2. *Chunk independence:* the history is one more global bake on the civ plan (pure in sheet + params + seed; no column reads it except through the plan), so every quantity stays a function of world position; equality test = `CivPlanTest.DeterministicAcrossGenerators` extended over the history fields, plus `CivHistoryTest.EpochsZeroIsM3aBitForBit` (the `epochs: 0` plan must equal today's plan site for site — the pinned contract that M3b cannot move M3a's output). Pass by construction.
3. *Pipeline:* stage = civ plan, after siting, before M4's routes (the economy edges M4 reuses are an OUTPUT of the history). Model (Dwarf Fortress / Songs of Syx zero-player sim, §11): N epochs over the sited plan; per epoch each live site's population index grows with its suitability and its trade (coast / large river / later a route), a FOUND step seats new hamlets/outposts on free viable cells inside the spacing (the ladder stays the ceiling), one SHOCK epoch cuts every population index, sites below viability after it DECLINE then are ABANDONED → `ruin`; provinces = each city's/town's macro-cell Voronoi by cost distance (the economy graph M4 routes). **Grounding (§10 F4g, this check):** the arc is England 1086 → 1377: population ≈ 1.75–2.25 M at Domesday → ≈ 4.7–6 M at the 1300 peak → ≈ 2.5–3 M at the 1377 poll tax (Russell / Hatcher / Broadberry-Campbell-van Leeuwen; the shock is a −45 % to −50 % step) — so the default is 12 epochs of 25 years with the shock at epoch 9 (≈ 1348) [REASONED from the anchors]; deserted medieval villages 2 263 (Beresford & Hurst 1968 gazetteer) → 2 811 (1977 map) against ≈ 13 000 Domesday places ≈ **20 % of ever-founded villages abandoned** (GROUNDED as the ledger's control number; the Hull *Beresford's Lost Villages* project maps 2 222 today); planted towns 171 in England 1066–1370 (+ 84 Wales, 124 Gascony; the 1967 review of Beresford's *New Towns of the Middle Ages*) over ≈ 130 000 km² ≈ **0.13 town foundings per 100 km² per 300 years** (GROUNDED: town-tier founding is RARE; hamlet/outpost founding carries the growth). Persistence: the history is derived (never stored), the recipe carries `civ.epochs` + `civ.shockEpoch`; the realizer reads a site's `era`/`state` through the existing `SettlementProgram` era hook (M0 row "era reaches buildings"), so an abandoned site resolves to the `ruin` state the forge must own (D22).
4. *API:* `civ.sites[]` gain `founded`, `peak`, `state ∈ live|declining|abandoned`, `population` (index, unitless, echoed), `history[] {epoch, event ∈ founded|grew|declined|shock|abandoned, value}`; `civ.provinces[] {seat, cells, members[]}`; `worldforge_civ` draws ruins as lowercase glyphs; `worldforge_minimap` hollow grey squares. Params: `civ.epochs` (int, clamp 0..64; **0 = no history = M3a's plan bit for bit**, the pinned default until the owner flips it), `civ.shockEpoch` (clamp −1..epochs, −1 = none), `civ.shockFraction` (clamp 0..0.9, default 0.45 GROUNDED above). Defaults change nothing today (`epochs` 0); flipping the default re-pins `CivPlanTest` in the same commit.
5. *Visual test:* "works" = on the baseline world with `epochs 12`, the abandoned fraction of ever-founded village-and-below sites lands in 15–25 % (the DMV control), no live site breaks a spacing or water/slope invariant, every ruin was once live (its ledger shows found → … → abandoned), provinces partition the land cells, and the plan is deterministic. L2 `CivHistoryTest` (red first: `AbandonedFractionMatchesTheDesertedVillageRate`, `EveryRuinHasAFoundedEvent`, `LiveSitesKeepEveryM3aInvariant`, `ProvincesPartitionTheLand`, `EpochsZeroIsM3aBitForBit`); stress rigs on the synthetic sheets (`epochs 64`, shock at epoch 0, all-sea, ×10 density, 300 pins — the ruin fraction bounded, no site founded twice); L4 on `CivLab` (the civ map with ruins + the minimap, raw JSON archived). Rig vs shipped: only `epochs`/`shockEpoch` differ.
**RULED 2026-10-22: D21 no (tiers stay), D22 yes (state + glyph only), D23 yes (12 × 25 y, shock at 9, −45 %, `civEpochs` 0 shipped), D24 blue-noise pick deferred → verdict READY; M3b builds from the red tests above.**

**M3b BUILT 2026-10-22.** Record moved verbatim to [`WorldForgeV2BuildLog.md#r010`](WorldForgeV2BuildLog.md#r010) (3 KB).

**M3b AUDIT 2026-10-22 (solution-auditor on 22489db3.** Record moved verbatim to [`WorldForgeV2BuildLog.md#r011`](WorldForgeV2BuildLog.md#r011) (4 KB).

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

**M4 design check (2026-10-22, run against the contract above, the M3a/M3b plan as built and the road code as it stands): NEEDS WORK — four owner decisions, then engineering in three increments.**
1. *Voxel aesthetic:* roads are already a column field (surface material + grade); M4 adds earthworks (cut banks at the angle of repose, dry-stone retaining walls, crown/camber) that must be realized in subcubes/microcubes like the M0 creek fords — unconditional, never a quality tier. Bridges keep the 36-index deck/pier emission. Pass, with the earthwork cross-section owed a grounded source (§10: cut/fill caps STRUCK as constants, derived from lateral room × tan(repose); the 3 u shoulder STRUCK, derived from cut depth / tan(built slope)).
2. *Chunk independence:* today's roads are a pure function of world position through the plan's segment raster (`roadAt`), pinned by `RoadFieldSeamTest`; earthworks join the same `ColumnSample` field and the seam test extends to `earthwork*`. One live violation to retire inside M4: the **nearest-road grade seam** (§10 row, measured 2 cubes on the 13-road Mountains network) — the grade a column takes must be a continuous blend over every corridor touching it, not the nearest road's. The equality test: `RoadFieldSeamTest` shape over `roadClass/roadDist/gradeY/earthwork*` on a chunked vs whole-region pass. **The scale problem is the design question:** the civ plan has 154 sites over the continent but the plan's raster is a single uint16 segment index over the ±16 km box (`kRasterSegmentCap`, loud when hit). A continent network (≈ 300 routes, each hundreds of 16 u vertices) does not fit one raster, and the hydrology box — the only place channels, basins and beds exist at 32 u — is fixed at the origin. Pass only with D28 (below).
3. *Pipeline:* stage = after the civ plan (sites, tiers, provinces, history), before settlement realization; consumes the macro sheet (base, drainage, basins, the receiver graph), the meso bake inside the box (carved channels, rim-corrected levels, beds, the coast field), slope from the realized surface at 16 u. Produces `routes[]` (class by the hierarchy of the sites it joins: highway between cities/towns, road to villages, track to hamlets/outposts — Galin 2011's hierarchy merge), the segment field, bridges/fords/tunnels/switchbacks as EDGES, earthworks. Order-independent as today (one global bake, read locally). Economy edges: the province graph from M3b (each site to its seat, seats to each other) is the route demand; the **reuse discount** (a new route only if cheaper than a fraction of the existing-network route) decides which demands become new corridors. Disturbs nothing upstream; downstream, settlement streets receive each arriving route as a spur (exists in V1).
4. *API / recipe:* `WorldForgeParams::routes{gradeCapHighway, gradeCapRoad, gradeCapTrack (rise/run; GROUNDED 0.05 / 0.08 / 0.15, §10), reuseFraction (0..1; Veloren's 1/3 — REASONED, no source), bridgeMaxSpanU (GROUNDED: medieval single timber arch 3–6 m, multi-pier up to ≈ 25 m overall — Hemington/Aldwincle; the V1 96 u is a MISMATCH), fordMaxDepthU (NEEDS-GROUNDING), tunnelMinLengthU (NEEDS-GROUNDING)}`; `worldforge_plan` echoes `routes[] {a, b, class, lengthU, maxGrade, edges[] {kind, from, to, length}}` and the clamped caps; `worldforge_civ` / minimap draw routes by class. Defaults change: `kMaxRoadGrade = 0.5` (MISMATCH) becomes the per-class caps on VERSION-2 recipes only (D10: the version-1 pinned hashes 9693137997320541182 / 18197215997962677593 must not move — the cap stays 0.5 on the legacy path).
5. *Visual test:* "works" = every route is walkable end to end by `TraversalProbe` with 0 unclimbable steps (both seeds), every centreline step within its class cap (measured, not inferred), earthworks within the derived bounds, junction count equals the merged graph, bridges only over carved channels within the grounded span and fords only within the depth cap, a route forced across a 60° cliff produces a tunnel or a stated refusal (never a trench), the chunked field equals the whole-region field. L2 `RouteFieldTest` + L3 `RouteWalkTest` + the seam test; L4 Release fly-along at four fixed poses (a switchback pass, a bridge, a ford, a cutting with a retaining wall) on `RouteLab` (one meso tile: one painted ridge via the m2a `upliftMask`, one river, two sites; the corridor PREDICTED and drawn before the run; rig vs shipped = the mask + two pins only). Red tests exist on paper: `RouteFieldTest.GradeCap` fails at 0.5 today, `RouteWalkTest.MountainSeedZeroUnclimbable` fails at 0.46 % today (F7 re-measure), `BridgeSpanTest.OverNinetySixU` fails (no bridge). Stress: 1 000-route continent network (the raster cap), a 1 km span, the cliff, a route through a basin (must skirt the rim, never cross the floor), the Mountains seed. Perf gate: continent corridor bake for 154 sites < 2 s Release; box refinement < the V1 plan bake (today 1.6–7.7 s Release for 4–6 sites — ALREADY over the 10 s budget at 8 sites on the 32 u bake, so the refinement must route on macro corridors and only refine the corridor band on meso).
**RULED 2026-10-22: D25 three increments, D26 the grounded caps on version 2, D27 1/3 REASONED (→ 0.5 on 2026-11-02, the D27 row), D28 macro-fidelity stamping beyond the box → verdict READY; M4a builds from the red tests below.** Original decision text kept for the record: **D25 — scope split.** Recommended: M4a = the continent corridor graph on the macro sheet (demand from provinces + hierarchy, reuse discount, class by hierarchy, data + maps only) and the box realization re-routed ALONG those corridors on meso; M4b = per-class grade caps + the blended grade field (retires the junction seam) + switchbacks as edges + derived earthworks; M4c = bridges/fords/tunnels as edges with the grounded spans. Alternative: one increment (the design says it is L-sized; the three reds above belong to three different mechanisms). **D26 — the grade caps.** Recommended: adopt 0.05 / 0.08 / 0.15 by class on version-2 recipes (the grounded values; the shipped 0.5 is a MISMATCH) and accept the cost — on the Mountains seed routes will lengthen several-fold and need switchbacks and cuttings; the F7 unclimbable fraction becomes 0 by construction. Alternative: a relaxed track cap (USFS: ideal 10 %, short pitches steeper) if the switchback count reads badly in the L4. **D27 — the reuse fraction.** Recommended: ship Veloren's 1/3 labelled REASONED and measure the network on the baseline (routes, total length, junctions) against the MST-plus-1.4×-detour network V1 builds today; research a grounded value (Roman/medieval network density per km²) in parallel. Alternative: keep V1's detour rule and skip reuse. **D28 — roads beyond the box.** The continent's routes exist only as corridors outside the ±16 km hydrology box; realized roads end at the box edge today (a visible seam). Recommended: stamp roads OUTSIDE the box from the macro corridor polyline at macro fidelity (surface material + drape, no grading, no crossings — D18's "refine locally" applied to roads), so a route is continuous across the continent and the box is where it gains grade, earthworks and bridges; the far-terrain tiles already draw the road field. Alternatives: move the hydrology box with the streaming focus (a moving bake — rebake cost on every focus change), or the full-continent meso bake (M2's deferred item: ≈ 300 MB / 35 s Release measured as an estimate, not built).
Red tests ready to write once D25–D28 are settled: `RouteFieldTest.EveryStepWithinItsClassCap`, `RouteFieldTest.GradeFieldIsContinuousAcrossCorridors` (the junction seam), `RouteWalkTest.EveryRouteWalkableBothSeeds`, `RouteGraphTest.ContinentCorridorsReachEverySite`, `RouteGraphTest.ReuseDiscountPrunesParallelRoutes`, `BridgeSpanTest.NoDeckBeyondTheGroundedSpan`, `RoadFieldSeamTest` extended to `earthwork*`.

**M4a BUILT 2026-10-22.** Record moved verbatim to [`WorldForgeV2BuildLog.md#r012`](WorldForgeV2BuildLog.md#r012) (4 KB).

**M4a AUDIT 2026-10-22/24 (solution-auditor on 317798ef.** Record moved verbatim to [`WorldForgeV2BuildLog.md#r013`](WorldForgeV2BuildLog.md#r013) (3 KB).

**M4b-1 + M4b-2 BUILT 2026-10-26.** Record moved verbatim to [`WorldForgeV2BuildLog.md#r014`](WorldForgeV2BuildLog.md#r014) (4 KB).

**M4b AUDIT 2026-10-26 (solution-auditor on 9c0eed9a.** Record moved verbatim to [`WorldForgeV2BuildLog.md#r015`](WorldForgeV2BuildLog.md#r015) (3 KB).

**M4b-3 switchbacks BUILT 2026-10-26.** Record moved verbatim to [`WorldForgeV2BuildLog.md#r016`](WorldForgeV2BuildLog.md#r016) (2 KB).

**D29 (owner) — roads on rough mountains.** The class caps hold on the realized surface wherever a road is not pitched, and a pitch never exceeds 0.5 (walkable), but on the version-2 Mountains world 45 % of the network is pitched and single cuts reach 97 u where the 0.5 walkability envelope must cut a cliff. Three honest options: **(a) accept pitched mountain roads** — tracks and roads on rough ground climb in short pitches at up to 0.5 with bounded cuts (≤ the class floor except at cliffs), the caps being a ruling grade for wagon routes on favourable ground (USFS: "short pitches steeper"); ship as is, the figures reported per plan; **(b) earthworks + retaining walls** — let the envelope cut deeper than the floor on mountains and realize the cut faces as dry-stone retaining walls beyond the repose shoulder (the §10 STRUCK row's derivation: wall where cut > shoulder × repose), which keeps the caps and costs voxel work in the realizer (M4b-4); **(c) fine-resolution routing** — route each pitched stretch on the 32 u bake inside the band with the grade measured along every step (the code exists, off: 320–360 s Debug per baseline box plan and it found no gentler line where the roughness is isotropic). Recommended: **(a) now, (b) as the next M4 increment** — a mountain road with walls is the medieval answer (Roman and alpine mule roads are walled cuts, not 5 % grades), and (c) buys little at a cost the laptop cannot pay in Debug. **RULED 2026-10-27: (a) now, (b) next (owner took the recommendation) → M4b CLOSED on that basis; M4b-4 (retaining walls) queued after M4c.

**M4c design check (2026-10-27, bridges / fords / tunnels as edges — run against the V1 crossing code and the corridor-driven plans as built): NEEDS WORK — three owner decisions, then engineering.**
1. *Voxel aesthetic:* a bridge is already a plank deck + stone piers + rails emitted by `generateChunk` (36-index, back-culled); M4c changes WHERE and HOW LONG, not the look. Fords are the M0 creek notch (subcube steps into the bed). Pass.
2. *Chunk independence:* crossings are plan data (`crossings[]`, `bridges[]`) read per column through `bridgeAt`; unchanged. The equality test is `RoadFieldSeamTest` (exists). Pass.
3. *Pipeline:* a crossing is decided in the box plan after routing (the carve-accurate `channelAt` scan along the realized line) — today every CARVED crossing gets a deck if its banks are within 96 u, else no deck and a WARN (a half-road into the river), and creeks get fords by not being decked. Three measured defects set the scope: (a) **the 96 u span is a MISMATCH** (§10: a medieval single timber span is 3–6 m, piered bridges ≈ 10 m between piers and ≈ 25 m overall — Sherway, Hemington, Aldwincle); (b) **the approach fill is unbounded** — the deck sits at the HIGHER bank and the lower approach is filled up to it along a pin cone at the class cap: on the Mountains world a fill of 177 u (M4b census), a dam, never a road; (c) **an uncrossable channel is discovered after routing**, so the route cannot avoid it. The fix is to make the crossing a routing EDGE (the M4 contract): the box search already pays a river-cell penalty; it must pay by what the crossing IS — a ford where the channel is a creek (depth < 0.5 u, the M2b-2 class split; NWS: 0.15 m of MOVING water knocks an adult down, so a ford is slow shallow water — GROUNDED as the bound), a bridge where the channel width (the M2b-2 law, W = 2.37·A^0.36 u) fits the grounded spans and the banks differ by no more than the class cut floor + a pier's height, and IMPASSABLE otherwise (a gorge, a 30 u river) so the route crosses elsewhere or the demand is refused with the reason. The deck meets the LOWER bank's grade and the higher bank is cut to it within the floor (medieval bridges met the road, STRUCK clearance row); piers every ≈ 10 u (GROUNDED). Tunnels: the one Roman road tunnel of note is Furlo (38 m, Vespasian, AD 77); no medieval road tunnel at our scale — **D30 recommends deferring tunnels** (a mountain route is pitched or walled, D29, never bored). Consumes: the realized line, `channelAt` (depth, width, carved), `surfaceYAt` at the banks; produces: `crossings[] {kind ford|bridge|refused, span, deckY, reason}`. Upstream untouched; downstream the realizer's deck emission is unchanged.
4. *API / recipe:* `WorldForgeParams::bridgeMaxSpanU` (GROUNDED 25 u overall, clamp 3..96 with the V1 96 as the legacy value on version-1 recipes), `pierSpacingU` (≈ 10 u GROUNDED), `fordMaxDepthU` (0.5 u = the creek class; clamp 0.1..1), `bridgeMaxBankStepU` (the deck's bank mismatch before refusal = the class cut floor + pier height, DERIVED); `worldforge_plan` echoes every crossing with its kind and reason, and the refused demands; the civ map / minimap mark bridges and fords. Defaults change for corridor-driven plans only (version 1 keeps 96 u — pinned hashes).
5. *Visual test:* "works" = every carved crossing on the baseline is a ford, a bridge within the grounded span with its approach fill ≤ the class floor, or a refusal with its reason — never a half-road, never a dam; a character-box walks every bridge and ford end to end (`RoadFieldSeamTest.BridgeCrossingIsAgentWalkable` exists for version 1; its version-2 twin is the L3). L2 `BridgeSpanTest`: `NoDeckBeyondTheGroundedSpan` (red: the 96 u decks today), `ApproachFillIsBounded` (red: 177 u on Mountains), `EveryCarvedCrossingIsFordBridgeOrRefused`, `UncrossableChannelsAreRoutedAround` (the box A* with the crossing cost: the corridor graph's river penalty is a cell, the box edge is the real width). Test world: `RouteLab` = the CivLab recipe (the baseline has 1 bridge in 29 roads on Release, 4 crossings on the x10 stress) plus the painted river of the m2a `upliftMask` for a known width; the Mountains world for the fill red. Rig vs shipped: the mask only. Perf: no new per-column cost (the deck query stays gated to road/channel columns).
**Owner decisions:** **D30 — tunnels.** Recommended: DEFER (no medieval precedent; D29 pitches or walls a mountain route). **D31 — the span rule.** Recommended: single timber span ≤ 6 u, piered ≤ 25 u overall with piers every ≈ 10 u, wider channels uncrossable for routing (the route goes around or the demand is refused with the reason); the version-1 path keeps 96 u. Alternative: keep decking any width (today's look, wrong by the sources). **D32 — fords.** Recommended: a ford is a creek crossing (channel depth < 0.5 u, the existing class), realized as the M0 notch with the road material on the bed; deeper water is bridge-or-nothing. Alternative: fords up to the pin depth with a stone-paved bed.
Red tests ready once D30–D32 are settled: `BridgeSpanTest.NoDeckBeyondTheGroundedSpan`, `BridgeSpanTest.ApproachFillIsBounded`, `BridgeSpanTest.EveryCarvedCrossingIsFordBridgeOrRefused`, `RouteFieldTest.UncrossableChannelsAreRoutedAround`, `RoadFieldSeamTest.BridgeCrossingIsAgentWalkable` on a version-2 bridge.

**RULED 2026-10-27 (owner "keep going" after the three recommendations were stated — read as the recommendations taken): D30 tunnels DEFERRED; D31 the grounded span rule (single timber span ≤ 6 u, piered ≤ 25 u overall, piers every ≈ 10 u, wider channels uncrossable for routing; version 1 keeps 96 u); D32 a ford is a creek crossing (channel depth < 0.5 u = `FlowField::kRiverCarveDepth`, the M2b-2 class split), deeper water is bridge-or-nothing.**

**M4c BUILT 2026-10-27.** Record moved verbatim to [`WorldForgeV2BuildLog.md#r017`](WorldForgeV2BuildLog.md#r017) (10 KB).

**M4c AUDIT 2026-10-27/28 (solution-auditor on the working tree after the first L4.** Record moved verbatim to [`WorldForgeV2BuildLog.md#r018`](WorldForgeV2BuildLog.md#r018) (2 KB).

**M4c AUDIT 2 (2026-10-28, solution-auditor on the follow-up tree, running the exe itself.** Record moved verbatim to [`WorldForgeV2BuildLog.md#r019`](WorldForgeV2BuildLog.md#r019) (2 KB).

**M4b-4 design check (2026-10-28, retaining walls — run against the M4b/M4c realizer as built: `gradeBlendAt` widens the shoulder to |grade − natural| / repose capped at 12 u, so a cut beyond 8.4 u stands steeper than repose — at 80 u the bank's slope is 6.7, a dirt cliff): NEEDS WORK — owner decisions D33, D34, then engineering.**
1. *Voxel aesthetic:* the wall is a column-field feature like the parapet and the creek shelf — whole-cube `Cobblestone` courses from the road grade to the wall top with the top course a 2/3-height subcube set back one subcube on the road side (the batter; unconditional, no flag); the rock face above it is `Stone` on the exposed voxels (the column's material between the wall top and the bank foot), the bank above at repose in the natural material. Pass. The look to check in the L4: a walled cut reads as a dry-stone revetment under a rock face under a scree bank, not a grass cliff.
2. *Chunk independence:* every quantity is per column from `gradeBlendAt(x, z, natural)` — the column's own natural height, the blended grade, the distance to the road edge — all pure functions of world position via the baked plan (the road field's existing contract); the wall height, rock-face height and bank are functions of (natural − grade) AT THAT COLUMN, so a wall follows the hill without reading any neighbour. No cross-chunk lookup. Pinned by `RoadFieldSeamTest.GeneratorCopyStampsIdenticalRoadColumns` (two generators, identical columns) extended to the wall/rock/bank fields, plus the chunked-vs-whole shape: a region's columns sampled through `generateChunk` per chunk equal `sampleSurface` for the same columns (the `ChunkMicroWorld` helper already exists).
3. *Pipeline:* realization stage, inside the road grading (`WorldGenerator::sampleColumn` → `gradeBlendAt`), consuming the grade profile and the natural surface; it changes `surfaceY` and the material only in the band [half-width, half-width + 1 + 12 u] on dry, above-sea, non-channel columns (the grading's existing guards: never into a carved channel, under standing water, or below sea). Downstream: the flora gate is already closed on the road and its shoulder (M1), so a wall column gets no tree; water is untouched (walls stand on dry columns only); bridges excluded (deck columns are not graded). Order-independent by construction (per column). Per-world tuning: `wallTierU` (3) and `rockCutMaxU` (36) in `WorldForgeParams` (version-2 recipes; version 1 has no blend, pinned hashes unchanged), persisted in the recipe like `bridgeMaxSpanU`.
4. *API:* `WorldForgeParams.wallTierU` (u, clamp 0..6 — 0 = no walls, 6 = twice the tested height, beyond it nothing is sourced) and `rockCutMaxU` (u, clamp 0..96, 0 = every cut beyond the wall refuses under D33(b)); echoed in `params` by `worldforge_plan`; `ColumnSample.earthwork{wallTopY, rockTopY}` (the planned `earthwork{cutY, fillY, retaining}` field) read by `generateChunk`; the plan JSON reports per road the walled length, the rock-faced length and the deepest cut (part of the identity hash, like refusals); under D33(b) a cut beyond `rockCutMaxU` is a `refused_demands` reason. Defaults change nothing for version 1; the version-2 fixtures' plan hashes are not pinned.
5. *Visual test:* "works" = (L2, Mountains 424242 and the baseline) at every single-road cut deeper than 8.4 u the emitted cross-section perpendicular to the road has: a wall band of ⌈0.67 h⌉ columns at the road edge whose top is grade + min(cut − 8.4, wallTierU) with the wall material, the rock-face column(s) above it at natural − 8.4 with `Stone` on the exposed voxels, and the bank above at ≤ 0.70 over the 12 u shoulder (the `CutBanksStandAtTheReposeSlope` measurement, extended to every depth — today it SKIPS cuts beyond 8.4 u, the red); 0 cuts deeper than wallTier + rockCutMax (under D33(b): refused and re-routed, the refusal count reported; under (a): reported as unprecedented); (L3) `RealizedRoadsAreStepWalkable` and `GradedRoadsAreStepWalkableOnMountains` still green (a wall never intrudes on the road width — asserted: no wall column inside the half-width); (L4) Release on a Mountains lab (CivLab is the baseline, max cut 11.1 u → walls of ≤ 2.7 u exist there too; a `MountainLab` variant of CivLab with seed 424242 is the small world for the look). Red tests: `EarthworkTest.DeepCutsAreWalledNotCliffed` (Mountains: today 0 wall columns and N banks steeper than repose beyond 8.4 u — the red is the count), `EarthworkTest.WallNeverIntrudesOnTheRoad`, `EarthworkTest.CutsBeyondThePrecedentAreRefused` (D33(b)) or `...AreReported` (D33(a)), the seam test extension. Prediction written first: on Mountains 424242 the 5 254 vertices deeper than 6 u fall into ≈ 1 500 walled (8.4–11.4 u), the rest rock-faced; under D33(b) the demands whose line needs a cut beyond 39.4 u (wall + precedent) are refused — predicted ≤ 6 of 22 roads affected (the 80 u cut is one stretch), measured, not inferred. Rig vs shipped: the fixtures ARE the shipped recipes (civ true, siteCount 8); the only delta is Debug.
**Owner decisions:** D33 (cuts beyond the precedent: recommended (b) refuse/re-route as an edge), D34 (one tier, no terracing: recommended), D35 derived (no decision unless the look is wrong). Verdict: NEEDS WORK — D33/D34, then the red tests above. **RULED 2026-10-28: D33 (b), D34 one tier, D35 as derived (owner: "recommendations") → M4b-4 builds from the red tests.**

**M4b-4 BUILT 2026-10-28 → 29.** Record moved verbatim to [`WorldForgeV2BuildLog.md#r020`](WorldForgeV2BuildLog.md#r020) (12 KB).

**M4b-4 AUDIT 1 (2026-10-28, solution-auditor on the tree after the first green + the partial L4, running EarthworkTest itself.** Record moved verbatim to [`WorldForgeV2BuildLog.md#r021`](WorldForgeV2BuildLog.md#r021) (2 KB).

**M4b-4 AUDIT 2 (2026-10-28, solution-auditor on the tree after the first follow-up, running four tests itself.** Record moved verbatim to [`WorldForgeV2BuildLog.md#r022`](WorldForgeV2BuildLog.md#r022) (2 KB).

**M4b-4 AUDIT 3 (2026-10-29, solution-auditor on the tree after GREEN 14 and the final-engine mutations.** Record moved verbatim to [`WorldForgeV2BuildLog.md#r023`](WorldForgeV2BuildLog.md#r023) (2 KB).

**M4c AUDIT 3 (2026-10-28, solution-auditor on the tree after the second follow-up.** Record moved verbatim to [`WorldForgeV2BuildLog.md#r024`](WorldForgeV2BuildLog.md#r024) (2 KB).


**M2d-4 design check (2026-10-29, the lateral channel blend — run against the generator as built: `FlowField::bake` samples the bed profile from the FULL relief at the cell centre (`base − depth`, then the downstream minimum) while `sampleColumn` multiplies the relief beside a carved channel by a FRACTION that is 0 at the centreline, so the profile and the land beside it disagree by the relief itself — the M4c finding, now MEASURED first by `RiverGeometryTest.ChannelSectionsMeetTheLandAtABoundedSlope` (red, `m2d4_red_green.txt`: a 1 u lateral section along the flow normal at every carved cell outside a basin, the emitted continuous ground per column). Mountains 424242 — 1 981 cells: the rim above the land at the bank on **97.0 % of bank sides, worst 249.6 u** at (7173,−11200); lateral steps steeper than repose AND than 1.3 × the natural ground (the largest biome heightScale, so a real cliff never counts) on 46.6 % of steps / **99.2 % of cells, worst 63.9 u per column**; the natural ground itself beyond 0.70 on 0.7 % of steps, worst 3.1 u — the walls are the shaping's. The Perlin baseline, the control: 38.7 % of bank sides / 34.5 u, 17.6 % of cells / 8.3 u, lips (the bank above the rim) 18.6 % / 4.7 u. Predictions P1, P2, P4 met; P3 missed on the baseline's size (its ridged band carries 30 u of relief beside a channel, not "small"). A creek was fixed the same way on 2026-07 (the swale became an absolute depth: "a fraction is only safe over a wide valley"); rivers kept the fraction): NEEDS WORK — owner decisions D36, D37, then engineering.**
1. *Voxel aesthetic:* no new asset; the section is a column-field shape like the creek swale and the road bank — a flat alluvial floor at the bankfull rim, a valley side at ≤ repose, the natural ground beyond — emitted whole-cube with the bed on the 1/3 u shelf grid as M2d-1 left it; the P1 material rule already paints exposed rock past 0.70, and a cut side at exactly the bound keeps its biome surface. The look to check in the L4: a mountain river in a V-gorge with scree-angle sides and a flat floor, not a slot, and never a floor in the air. Pass.
2. *Chunk independence:* every quantity is per column — the column's own unattenuated ground (base + relief × biome scale + offset, already computed in `sampleColumn`), the nearest carved channel's distance, half-width and the profile at its projection (`nearestChannel` gains `bedProfile` / `fullDepth` at the projection, the interpolation `channelAt` already does), and the floor/side envelope from those; no neighbour's result, no chunk quantity, no cross-chunk lookup. The footprint minimum (D37) is bake-time per river cell, a pure function of the bake and the warp. Pinned by `TerrainRiverTest.DeterministicCarve` (two generator copies agree column for column) and `WorldSheetGeneratorTest`'s tile-vs-whole seam test — the pair M2b/M2d used — and the section test itself runs on `sampleSurface`, the public per-column sampler.
3. *Pipeline:* the hydrology-geometry stage, inside `sampleColumn` where the valley rule sits today (between the relief and the carve) and in `FlowField::bake` (the profile's base); version-2 CARVED channels only — version 1 (table, no profile) keeps the fractional attenuation byte for byte (the pinned hashes 9693137997320541182 / 18197215997962677593, `LegacyTerrainGoldenTest`, `TerrainRiverTest.*`), creeks keep the absolute swale (already bounded). Downstream: the material pass reads the emitted slope (unchanged rule); the flora gate reads `riverOrder` (unchanged); the water runtime pins rivers on the carved bed from the same generator, so where D37 lowers a profile the water follows (D17, shapes only); WorldForge routes on `baseHeightFn` (unattenuated, unchanged) and judges crossings and earthworks on the emitted surface — the M4c crossing rule will now find banks at the rim, so the Mountains bridges should appear (prediction in 5); the M4b-4 banks sample the natural at the reach and are unaffected. Order-independent by construction. Persistence: no new recipe key — the side slope is a derived constant shared with the road repose (D36), `kValleyWidthMul` stays the labelled tunable.
4. *API:* no new route and no default change; the change is observable through `worldforge_plan` — Mountains `refused_demands` with the reason "channel rim N u above both banks" must disappear and `bridges[]` must appear — and through `water_bake_info` (unchanged shape). The only pinned test that moves is the new section test.
5. *Visual test:* "works" = (L2) `ChannelSectionsMeetTheLandAtABoundedSlope` green on both version-2 fixtures: **0 embankments > 1 u** (GROUNDED: natural levees stand a few cm to 3 m above their floodplain, ≈ 20 % of bankfull depth — §10), **0 lips > 1 u, 0 shaping-made walls** (every lateral step ≤ max(0.70, 1.3 × the natural step) + 1/3 u), at every carved cell outside a basin — with the pure section function unit-tested on a synthetic rig inside one chunk (`FlowFieldTest.ValleySectionEnvelope`: one variable, the natural height across a 1 u-stepped section; prediction written first: the floor flat at the rim out to 5 half-widths, then rim + 0.70 × (d − floorHalf) until the natural is lower; control: a natural everywhere below the envelope is returned unchanged); `BedIsMonotoneAlongTheLongestChannel`, the v1 golden, `TerrainRiverTest.*` and the pinned hashes green. (L2, the consumer) `BridgeSpanTest` on Mountains re-judges the 11 refusals on the new section — **prediction written first: ≥ 1 bridge on Mountains 424242 (today 0), the 2 "channel rim above both banks" refusals → 0, the 7 "approach rises" refusals fall (a canyon wall past the band now stands at ≤ 0.70, i.e. ≤ 8.4 u within the 12 u shoulder = the bank limit) but need not vanish** — measured, not inferred. (L4) Release on MountainLab at the two M4c dumps (the 236 u embankment at (−2250,−3835), the slot at (−2523,6026)) and the red's worst sites (7173,−11200) / (7426,−11169): the resident section measured through the API (the M4b-4 L4 #4 script — `terrain_height source=chunks max_y=420` + the voxels per column) and daylight captures with the river in frame before (eb91c676) and after at the same poses. Perf: the column-cost bench at the gate pose and the Release bake time printed before/after — the bake gains the footprint minimum on carved cells only (baseline 1 265 cells × 15 samples), the column gains one profile interpolation inside the band; neither may exceed the run-to-run noise on top of the M2d figures (1.8–2.0 µs per column, already past the M2d gate and recorded). Rig vs shipped: the fixtures ARE the shipped recipes; the only delta is Debug.
**Owner decisions:** D36 (the section: flat floor at the rim, sides at ≤ 0.70, the natural wins — recommended), D37 (the profile from the channel's warped footprint minimum — recommended). Verdict: NEEDS WORK — D36/D37, then the red test above, already red. **RULED 2026-10-29: D36 and D37 as recommended (owner: "recommendations") → M2d-4 builds from the red test.**

**M2d-4 BUILT 2026-10-30.** Record moved verbatim to [`WorldForgeV2BuildLog.md#r025`](WorldForgeV2BuildLog.md#r025) (8 KB).

**M2d-4 AUDIT 1 (2026-10-30, solution-auditor on the tree after chain 4 and the killed L4.** Record moved verbatim to [`WorldForgeV2BuildLog.md#r026`](WorldForgeV2BuildLog.md#r026) (2 KB).

**M2d-4 RESIDUAL 2026-10-30.** Record moved verbatim to [`WorldForgeV2BuildLog.md#r027`](WorldForgeV2BuildLog.md#r027) (5 KB).

**M2d-4 AUDIT 2 (2026-10-30, solution-auditor on the tree after chain 5; raw).** Record moved verbatim to [`WorldForgeV2BuildLog.md#r028`](WorldForgeV2BuildLog.md#r028) (1 KB).

**D38 BUILT 2026-10-30 (owner.** Record moved verbatim to [`WorldForgeV2BuildLog.md#r029`](WorldForgeV2BuildLog.md#r029) (8 KB).

**M4b-4 ANCHOR FOLLOW-UP BUILT 2026-11-02.** Record moved verbatim to [`WorldForgeV2BuildLog.md#r030`](WorldForgeV2BuildLog.md#r030) (4 KB).

**M4 water-aware routing design check (2026-11-02, run against the box routing as built: `WorldForgePlan::bake` marks a routing cell wet where the rim-corrected water level stands 0.1 u above `heightAt` at the cell CENTRE — `heightAt` is `baseHeightFn`, the coarse routing height, not the emitted surface — on the 128 u coarse grid and the 32 u fine band; the realization never grades a column whose rim-corrected level stands above its emitted surface, but still stamps the road class and material on it, so a road routed over a wet shore column vanishes under the water with no deck and no ford; decks exist only for carved channels, fords for creeks; the M4c paragraph lists this as "water-aware box routing below 128 u"): READY — no owner decision.** 1. *Voxel aesthetic:* nothing new is emitted; a road that would have run into standing water detours or is refused with its reason — the existing road, deck and ford shapes, unconditional. 2. *Chunk independence:* the plan is baked once at world level and every column query is a pure function of world position (the road field's existing contract); the new rule touches the bake (which routes) and adds no per-chunk quantity; the pin stays `RoadFieldSeamTest` (chunked vs whole-region road field equal) and `EarthworkFieldsIdenticalAcrossGenerators`. 3. *Pipeline:* the structures stage (roads) after hydrology; it consumes the bake's own callbacks — `surfaceY` (the emitted pre-road surface) and `levelAt` (the rim-corrected level) — the SAME pair the realization tests, so the plan's judgement and the emission agree by construction; `heightAt` stays the router's cost height. Order-independent: a deterministic bake from deterministic callbacks; version 1 does not enter the demand path (pinned hashes untouched); version-2 plan hashes MOVE wherever a road crossed standing water (a refusal or a detour is part of the plan's identity) — none are pinned in tests, the logged MountainLab / Mountains hashes are re-logged. No recipe key: the 0.1 u wet margin is the existing constant. 4. *API:* the plan JSON's `refused_demands[].reason` gains the text "a line crosses standing water at (x,z): N columns under a level of L u"; the `[CROSSINGS]` log line and `worldforge_plan` gain `water_rerouted` / `water_refused` counts (integers, columns in u); nothing tunable, nothing clamped, no default changes. 5. *Visual test:* "works" = (L2, the real output) on Mountains 424242, the Perlin baseline and MountainLab's bake: 0 road columns (road class > 0, no deck, river order 0) whose rim-corrected water level stands above their emitted surface, measured by a 1 u walk of every road's centreline and half-width band through `sampleSurface` + `waterLevelAt` — the RED is that count today, measured FIRST (it may be 0 on these worlds, in which case the rig below is the red); the RIG: a `WorldForgePlanTest`-style synthetic bake (a flat plain with one 32 u hydrology cell holding a pond whose level is set between the cell's corner height and its centre height on a gentle slope, so the centre is dry and a third of the cell's columns are wet; one track demand between two pinned points whose straight line crosses that shore corner) — prediction: before, the route runs straight and N > 0 of its columns are wet; after (the fine-grid cell wet when any of its 5 samples is under the level, and the 1 u scan blocking + re-routing), the route detours around the cell and N = 0; the CONTROL: the same rig with the level set below the corner height routes straight before and after with N = 0 (the metric reacts to the water, not to the detour code). The rig differs from shipped defaults in a flat plain (no grade caps bite) and a single demand; the 0.1 u margin and class widths are shipped. L4: a MountainLab shore capture only where the L2 finds a case. **Rule to build:** (a) the fine band's cell is wet if ANY of its four corners or its centre is under the level, tested with `surfaceY` (the emitted surface) rather than the coarse `heightAt` (DESIGN: conservative at the shore; the coarse grid keeps its centre test — it only picks the corridor band); (b) after `decideCrossings` and `judgeCuts`, `judgeWater`: a 1 u walk of the realized centreline; a column with `levelAt > surfaceY + 0.1` that `channelAt` does not hit closes the 3×3 cells around it (`blockAround`) and the demand is routed again, else refused with the reason — the M4c machinery; (c) the count in the plan JSON and the log.

**M4 WATER-AWARE ROUTING BUILT 2026-11-02.** Record moved verbatim to [`WorldForgeV2BuildLog.md#r031`](WorldForgeV2BuildLog.md#r031) (6 KB).

**M3c design check (2026-11-02, the D24 revisit — run against `CivPlan::bake` as built: the targets per tier are the hexagonal central-place ceiling, ⌊land km² / (0.866 s²)⌋ cumulative down the ladder, and the pick is greedy by score with the hard spacing against every site of its tier or higher; on a plain where every cell is viable it reaches ~70 % of the ceiling because a score-sorted sequential fill packs like a square lattice at best): NEEDS WORK — one owner decision, D24 itself.** 1. *Voxel aesthetic:* no asset; sites are positions. 2. *Chunk independence:* the civ plan is baked once on the macro sheet (a 128 u cell field), pure per cell; a relaxed pick reads only the sheet — no chunk quantity; the pin stays `CivPlanStressTest.FourTimesTheContinentKeepsEveryInvariant` (every invariant at every site at scale) and the box plan's seam tests. 3. *Pipeline:* the civ stage before corridors and the box plan; a fuller ladder means more sites → more demands, more roads, every version-2 plan hash, census, L4 ledger and the baseline's 157-site history diff MOVE (the reason D24 deferred it). Order-independent by construction if the relaxation is deterministic (seeded, fixed iteration order). No recipe key beyond `civDensityScale`; the ladder's spacings stay. 4. *API:* `worldforge_civ` already echoes count / target per tier; a relaxed pick adds nothing but the counts rising; the plan JSON's sites move. 5. *Visual test:* "works" = (L2, the synthetic fertile plain) every tier with a target of five or more seats ≥ 90 % of its hex ceiling (DESIGN: a lattice on integer cells beside the strait cannot seat the last few); the RED is `CivPlanStressTest.LadderIsMetOnTheFertilePlain` — measured 2026-11-02 on the greedy pick: city 3/3, town 6/6 (100 %), village 13/18 (72 %), hamlet 38/56 (68 %), outpost 112/162 (69 %); the real worlds REPORTED, not asserted (real terrain has unviable cells): the baseline 154 sites — village 14/14, hamlet 37/42 (88 %), outpost 97/120 (81 %); Mountains 156 sites — village 14/14, hamlet 40/42 (95 %), outpost 96/122 (79 %): nearer the ceiling than the plain, so the gap on the real worlds is 5–21 % at the hamlet and outpost tiers. The rig is the M3a stress plain (64 × 64 cells, Y 50, a sea strait); the control: the all-ridge continent still seats 0 (viability, not packing, is the limit there). **The rule that would turn it green (not built — D24):** after the greedy pick per tier, a seeded relaxation — move each site toward the centroid of its empty surroundings among viable cells of its tier's cap while keeping every spacing, then insert a site wherever the largest empty circle among viable cells exceeds the tier's spacing, repeat to a fixed point — a Lloyd-style anneal that approaches hexagonal packing on open ground and leaves rugged ground alone; the pick stays by score where cells differ. **OWNER DECISION (D24 revisited):** (a) keep the greedy pick (the 70 % stands as recorded; the red test is deleted or left red by design), or (b) build the relaxation to meet ≥ 90 % on the plain — accepting that every version-2 civ plan, road network, plan hash and the baseline's history ledger move once, and that site counts on the real worlds rise (the baseline's 157 toward the ladder). Recommended: (b) after M5's design check, as its own commit with the ripple re-measured, since the targets are the plan's own definition of the ladder and a pick that reaches 70 % of its own target is a standing gap. Until ruled: (a).

**M4c DECK FOLLOW-UP BUILT 2026-11-02.** Record moved verbatim to [`WorldForgeV2BuildLog.md#r032`](WorldForgeV2BuildLog.md#r032) (3 KB).

**D27 RULED + BUILT 2026-11-03.** Record moved verbatim to [`WorldForgeV2BuildLog.md#r033`](WorldForgeV2BuildLog.md#r033) (4 KB).

**M3c BUILT 2026-11-03/04 (D24 ruled (b) by the owner; red → green in `m3c_red_green.txt` through FOUR mechanisms, the 90 % bound never retuned).** Record moved verbatim to [`WorldForgeV2BuildLog.md#r034`](WorldForgeV2BuildLog.md#r034) (11 KB).

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

**M5 design check (2026-11-02, run against the settlement pipeline as built — `SettlementLayout::subdividePlots` partitions a W×D rectangle into a cols×rows grid of plots with street corridors; `selectBuildablePlots` picks flat cells on a `BuildabilityMap`; the tier presets are data (`resources/settlement_program.json`: street widths, toft depths, setbacks, public features, every value with provenance); `SettlementWalkability` is the composed L3 gate; buildings are axis-aligned (D5); the M0 gates — blocking walkability, lot dressing after buildings, water unbuildable — hold): NEEDS WORK — three owner decisions before the first red is written, and six increments named.** 1. *Voxel aesthetic:* the map is vector data; what reaches voxels is unchanged — axis-aligned buildings from the structure forge, paved ways, fences, walls as coursed masonry (the M4b-4 wall tier is the precedent), terraces/podiums as whole-cube ground treatment; nothing new is authored in full cubes. Unconditional. 2. *Chunk independence:* the map is baked per site, headless, from the meso heightfield (a sheet query, not chunks) and stored in the ledger; every voxel-time read is `roadAt`-style (a pure query of the baked map at a world column); no chunk quantity enters appearance. The pin: `SettlementMapTest` asserts the map from `plan()` twice is identical and the realized columns equal the map's rasterized stand-in (the M1 seam-test shape); the existing `RoadFieldSeamTest` covers ways that leave the site. 3. *Pipeline:* the structures stage after roads (M4); consumes the civ site (tier, era, population state from M3b), the M4 roads entering the site (the gates), the hydrology (water unbuildable, the river inside a town → an M4c deck), the biome (materials, canopy margins); it REPLACES the grid layout for every tier (the plan says "for each site") — the hamlet and village presets become the planner's scenario data, so the M0 lots-and-dressing contract must survive on the new map. It feeds M6 (interiors) and M7 (dressing). Order-independent: a deterministic headless bake per site (seeded growth, fixed iteration order); the site's era and tier persist in the civ ledger (M3b), the program presets in the recipe's era key. Risk named: the interest-map growth connects each building by road immediately with the M4 cost + reuse — inside a site that is the box plan's `gradeBlendAt` field or a town-local cost field; the plan must say which (a town-local field reusing `CorridorGraph`'s cost terms is the simpler, stated here as the recommendation). 4. *API:* `worldforge_map` / the ledger gain `nodes[]`, `ways[]` (tags as listed in the contract, widths in u, grades as rise/run), `areas[]` (parcel, lot{typology, stories, orientation ∈ 4, ground}, landuse, footprint polyominoes); `worldforge_minimap` draws them; the realization reads the same record. Every field: unit in the comment, omitted = unchanged on re-plan; the perf gate (map bake < 2 s Release per city) is a measured number per plan. Caps: 400 lots per site (the plan's stress cap) clamped at the planner with the reason written. Defaults: none change until the planner replaces the grid; the pinned version-1 plan hashes and the box plan are untouched (the map is a per-site record, not plan identity) — stated as the contract, to be pinned by `WorldForgeStressTest.VersionOnePlanHashesArePinned` staying green. 5. *Visual test:* "works" = (L2) `SettlementMapTest`: every lot touches a way; no two lots overlap; no way crosses a building footprint; every parcel inside the hull; every lot within its tier's slope cap; the map identical from `plan()` twice; `CityAxesFollowTerrain` (the principal ways' bearings correlate with the contour direction on the hills seed — today's grid ignores terrain: RED by construction) and `WallIsNotTheSiteRectangle` (the hull's vertex count and its deviation from the bounding rectangle — today's footprint IS the rectangle: RED). (L3) the existing walkability gate on the rasterized map before voxels and on the realized voxels. (L4) a 200-lot city on the hills seed, Release, residents on, with the stress rig: 400 lots, a 15° slope, a river through the site (the deck inside the walls), a coast. The test world is `CityLab` (one meso tile, a gentle hill + a creek; the flat `StructGenTest` stays for the forge); the control is the flat tile, where the terrain-following assertions must NOT fire (no contour to follow, the grid is right there). The rig differs from the shipped defaults in its single tile and a fixed seed; the perf gate is measured on it and on the hills seed. **Increments (the recommendation):** M5a the headless planner's skeleton + the map as data + the minimap draw + `SettlementMapTest`'s invariants, with interest-map growth for ONE building type on the hamlet tier (the smallest scenario) and the town-local road connection; M5b parcels by anisotropic conquest from the frontage; M5c rings with purpose and ground treatment; M5d the wall as a routed hull with gates; M5e squares, markets, civic and castle precincts; M5f the second era (D9) at the hamlet tier with one typology. Each with its own design check at the gate, red first. **OWNER DECISIONS:** (D39) the second era to build as the hook's falsification — the Roman insula grid or the Edo machiya (D9 says pick one that breaks the burgage assumptions; the insula breaks the frontage-from-typology rule, the machiya the toft-depth rule — recommended: the Roman insula, whose grid also exercises the planner's non-organic branch); (D40) the interest-map bells: Emilien 2012 publishes forms only (§10: ~150 parameters per village type in an unpublished encyclopedia) — the parameters come from our own grounded program data where a source exists and are labelled REASONED where not; the owner says whether a REASONED bell set is acceptable for M5a or whether grounding research precedes it (recommended: proceed with the forms and the program's grounded dimensions, every bell parameter labelled, a grounding pass per tier as its own task); (D41) the planner's scope — every tier from the first increment, or town/city only with hamlet/village keeping the grid until M5c (recommended: every tier, so the M0 contract is re-proven once, not twice). Until ruled: nothing of M5 is built.

**M5a BUILT 2026-11-05.** Record moved verbatim to [`WorldForgeV2BuildLog.md#r038`](WorldForgeV2BuildLog.md#r038) (9 KB).

**Owner rulings 2026-11-05 on the M5a open items (the nine put as recommendations, all taken):** M5b begins with its own design check, reported before anything is built; M5a-1 is reproduced headless BEFORE M5b (done — the next paragraph); the work stays on `feat/worldforge-v2` until M5 closes; `WaysStayOutOfWater` stays red with its STATUS header until M5b grows the street tiers from the map; the hairpin pin stays red (not an M5 concern); the cut bank's repose is RELATIVE to the natural (D42); the hamlet rect is derived from the program's band and the toft dimensions inside M5b; hamlet 5 beside the sea is a site-picker finding for M3 (the planner found two connectable lots in a rect that is mostly water — the picker scores water as a bell "near, not in" over the footprint window and did not see the rect's wet half), not blocking; the micro-level material probe of the paving is folded into M5b's L4.

**M5a-1 CLOSED 2026-11-05 (chains 74-81 and L4 run 10 in `m5a_red_green.txt`).** Record moved verbatim to [`WorldForgeV2BuildLog.md#r035`](WorldForgeV2BuildLog.md#r035) (7 KB).

**M5b design check (2026-11-06, run against the tree at a9f131df — the planner `SettlementMapPlanner.cpp`: cluster tiers by interest-map growth, the street tiers emitted from `chooseStreetAxis` + `planMainStreetLayout` / `planCityLayout` as built (`planStreetTiers`, 152-227: the layout's buildability drop keeps PLOTS off water, its streets run straight across it — the owner-ruled red `WaysStayOutOfWater`); the realizer's consumers of the axis-aligned street rects: the StreetPaver unit (`planStreetPaving` grades a rect's centreline and broadcasts LEVEL across its width, `StreetPaver.h`), the street sweep (`sharedRoadBand`), the wall's gates (`planTownWall(rect, layout.streets, …)`, the city's `walls.enabled`), the village well on `msl.mainStreet`'s verge, the response's `main_street`; the hamlet's ways paved by the paths unit as a ribbon of the AGENT's half-width (`hw = abox.halfWidthMicro`, `pathsForMapWays` a terrain path per node pair); the parcel consumers of `layout.plots[i].rect`: clearing (740), the terrace per parcel (777, a median grade + 4-cube skirt), the fences (1241-1300, four `FenceRun`s at the rect's edges, the gate on the street side), the yard props; the site rects literal in `WorldForgePlan::tierPreset` (hamlet 40 × 32 REASONED, village 80 × 48, town 140 × 60; `site.width/depth` are IDENTITY in `planHash` — `toJson` 2158-2160) and the civ ladder's city and outpost mapped to town and hamlet at `WorldGenerator.cpp:701` (the box plan has no city rect: the city program is reachable only by a direct `build_settlement`); the natural footprint derived in FOUR copies — `naturalFootprint` (planner 35-47), `naturalRect` (realizer 871-885), `planMainStreetLayout` (290-293) and `planCityLayout` (SettlementLayout.cpp ~508-521) — each taking `width_max` for the short side, which the forge's own proportion check refuses for the manor_hall (20 × 17 at 1.18 below 1.50, chain 86)): NEEDS WORK — three owner decisions before the first red is written; the scope split in two legs and every red test named.** **Scope as checked.** The plan's increment list names M5b "parcels by anisotropic conquest from the frontage"; the owner's ruling of 2026-11-05 reads M5b as the increment that "grows the street tiers from the map" (`WaysStayOutOfWater` waits on it) and folds in the hamlet rect derived from the band, the micro-level paving probe in M5b's L4, and the manor_hall footprint finding. Both are M5b, as two legs under this one check — **M5b-1 the street tiers grown from the map** (the street graph as ROUTED polylines + the growth on every tier + one ribbon paver for every way at its width + the site rects derived from the program + the one natural-footprint function) and **M5b-2 parcels by anisotropic conquest** (every tier, the parcel as a cell mask the realizer's four parcel units consume). M5b-1 first: two red pins wait on it, every later increment (rings, the routed wall, the squares) stands on the street graph, and the ribbon paver is the prerequisite of any street-tier L4. **The grounding read for this check (Emilien, Bernhardt, Peytavie, Cani, Galin, "Procedural Generation of Villages on Arbitrary Terrains", The Visual Computer 28 (2012) 809-818, §5, the LIRIS PDF read 2026-11-06):** §5.1 ROAD CONQUEST — the source S_i of parcel P_i is the projection of the building seed on the closest road; P_i propagates both ways along the road until it collides with a neighbouring parcel or reaches a maximum distance from S_i (the frontage); §5.2 a corner pass (a junction's corner goes to the parcel that arrived first; seeds that lose road access are suppressed); §5.3 ANISOTROPIC CONQUEST — every road cell of P_i is a source with a fund c_max, the sources spread on the grid until the summed cost reaches the fund; the cost of a free cell is Σ λ_i c_i(d, n) with n the road's normal at the source and the λ_i from the (unpublished) encyclopedia per village and building type; the conquest cost is the Euclidean distance for fields and |max(p·n, p·t)| (Chebyshev in the road frame) for houses — "quasi-quadrilateral parcels"; water, wall and road cells cost +∞ (the parcel never crosses them, the road's curvature shapes it); the slope cost is quadratic in the DIRECTIONAL gradient so spreading up the main slope is hard (fields read the terrain); §5.4 simplifies the contour to straight lines (our cube grid already is one: the polyomino is the record, D5). FORMS GROUNDED; every value ours, labelled (the D40 rule): the frontage = the typology's natural dimension along the way + 2 × setback (GROUNDED: the burgage principle, Tait 2009 via `room_program.json` width_max, the program's `plot.frontage: from_typology`); the fund = the drawn toft depth in [plot.depth_min, plot.depth_max] (the program's DESIGN DECISION, disclosed there) in the house metric, so on a flat, open site the conquered parcel is EXACTLY today's toft rect (the control); the slope weight λ_s REASONED — chosen so a step at the TooSteep gradient (the relief cap 6 over the 13-cube window, 0.46) costs twice a flat step (λ_s ≈ 4.7), stated at its definition with a sensitivity print; the house metric Chebyshev, the field metric Euclidean (fields are M5c's farmland areas, not M5b's). 1. *Voxel aesthetic:* nothing reaches voxels in a new idiom. The ways become polylines of cube cells paved by the hamlet's ribbon (microcube paving on a `planTerrainPath` grade) at the tier's width instead of the agent's; the parcels are cell masks — the terrace, clearing and props iterate cubes as today and the fence runs along the mask's boundary edges with the same micro-precise `FenceRun` stamps (a corner post at every vertex, the gate on the frontage run). Buildings stay axis-aligned (D5). Unconditional: `legacy_layout` is the A/B control, never a quality tier. 2. *Chunk independence:* the planner's inputs are the two injected samplers (the generator's natural ground and water, pure functions of world position — M5a) and the program; the conquest's slope term reads the same height sampler; the realizer's terrace and paver read the resident ground as today. No chunk quantity enters the map or the parcel; no cross-chunk lookup is added. The pin: `MapIsOrderIndependentOfItsSamplers` extended to every tier and to the parcel masks (two samplers of one field in different touch orders, the map byte-identical, parcels included) — order independence given the samplers, as audit E3 named it; chunk independence proper stays the sampler's property, re-measured at L4 by the resident probe of the realized way cells. 3. *Pipeline:* the structures stage after roads (M4); consumes the arriving roads as entries (the box plan trims the inter-settlement road at the rect edge, `WorldForgeBuildService.cpp:34-46`), the hydrology through the water sampler, the biome's materials, the program. What it disturbs, named with its treatment: (a) `layout.streets` loses the street tiers' rects — the StreetPaver unit is replaced for every map build by ONE ribbon paver (`pathsForMapWays` with a per-way width: a terrain path per node pair, the ribbon ±width/2 perpendicular to travel, the same `pavedCols` / `sharedRoadBand` / `sharedStamp` so the door spurs, the sweep and the fence straddle rule are untouched; the StreetPaver stays for `legacy_layout`); (b) the wall's gates come from the way cells that meet the site edge (`planTownWall` takes the edge crossings as 1-cell rects of the way's width — "gate width follows the STREET"); (c) the village well moves to the primary way's middle node's verge; (d) the response's `main_street` becomes the primary way's bounding box plus `primary_way`; (e) the walkability gate's street → door route starts at the door MARKER, not on a street rect (1925-1929) — unaffected; (f) the stand-in L3 walks free space — unaffected. The primary way of a street tier is ROUTED on the growth's 1 u cost grid (the M4 corridor cost, water blocked, the reuse discount) from the first entry to the farthest edge point, or to the second entry when two roads arrive (a through-road), at `street.main_width`; the city's second axis from the primary's middle node to the two side edges; the market stays a rect area at the crossing node (the square unit unchanged; M5e reshapes it). Lots then seat by the same growth on every tier with the road-access bell preferring the primary at the setback, and a lot whose door-approach cell already lies on a way (within half its width + 1) FRONTS that way with no new lane — the rows emerge from the bell, the lanes from the connections. A creek THROUGH the site: the primary way refuses the far side (no dry route) — the in-town deck is M5d/e's, stated. Order-independent: one deterministic bake per site; the conquest a multi-source Dijkstra over the site grid with ties by lot id then cell index. Persistence: the program by era key (M5a); the site rects derived from the program (`tier.buildings.max` × the mean toft area over the typology weights, at the max setback, over a fill factor measured by the red test below, DERIVED) enter `planHash` as identity → D43. 4. *API:* the map record gains `MapLot.parcel` (a list of settlement-local cube rects [x, z, w, d] whose union is the conquered mask; `rect` stays the nominal toft and the parcel's bound; absent in an old ledger = the rect), `MapWay.nodes` is already the polyline; the ledger, `worldforge_map` and `worldforge_minimap` (parcels and polyline ways drawn) carry them; `main_street` + `primary_way` as above; `targetLots` on every tier. Units cubes; unchanged = re-plan byte-identical. Clamps with their reason at the site: the lot cap 400 (exists); the fund ≤ `plot.depth_max`; the derived site rect ≤ the box plan's `minSpacing` / 2 on each side (a site may not exceed its own spacing) and ≥ the literal preset. Defaults that change: the street tiers realize from the grown map (the as-built main-street map stays as the control in `planSettlementMapAsBuilt`, now covering the street tiers; `SettlementLayoutTest`'s pins on `planMainStreetLayout` stand); `WaysStayOutOfWater` goes green and its STATUS header is removed in the same commit; the version-1 and civ hash pins per D43. 5. *Visual test (red first, predictions in `pm_chainN.txt` hashed by the chain's START line):* M5b-1 — (a) `WaysStayOutOfWater` green on four tiers (the red of chain 70); (b) `CityAxesFollowTerrain`, the street tiers' form of `WaysAvoidTheSteepBand`: the bench rig at the village (80 × 48), town (140 × 60) and city (160 × 120) sizes — the routed primary way's steepest cube step ≤ 1 while the as-built axis's (the control) is ≥ 4, and on a new diagonal-ridge rig the primary way's mean |grade| below the as-built's; (c) `MapInvariantsHoldOnEveryTier` and the stand-in L3 on the grown street tiers; (d) `TheStreetTiersSeatTheirBand`: ≥ `buildings.min` on the hill and the target on the flat per tier, the as-built counts printed beside; (e) `SiteRectsSeatTheBand` RED on the literal presets: on the flat open field the hamlet 40 × 32 seats the target for every seed in 1..20 — predicted to FAIL on the seeds whose target is 6 (the L4 sites seated 5 / 3 / 2); the derived rect seats it for every seed, the measured fill factor recorded as the constant; (f) `EveryTypologysNaturalFootprintPassesTheForge` RED on the manor_hall (the one function `RoomProgram::naturalFootprint` replaces the four copies; the short side at most length / proportion_min, at least width_min); (g) `WayRibbonTest` (L2 + L3): a width-5 polyline over the flat pad, with a diagonal leg, paves every column within ±2 cubes of the centreline and none beyond ±3, the TraversalProbe walks it end to end; L4 on CivLabE0 (Release, the exe's build log hashing the files): the CONTROL first (village 1 at (−7944, −9928) through `legacy_layout`), then a `worldforge_build` of the villages and the town from their grown maps — the gate 100 % walkable, the micro probe folded in from M5a (`/api/world/scan_micro` over the primary way's node cells: the top micro material is the tier's street material on ≥ 95 % of the sampled cells, the ribbon's width in micro columns = width × 9 ± 9 — the first material evidence of the paving), the bake ms per site from `bakeMs` against the plan's gate (< 2 s Release per city — on the town, and on the city if D45 maps it). M5b-2 — (h) `ParcelsEqualTheToftOnTheFlat` (the control: every parcel mask == its rect, exactly); (i) `ParcelsShrinkAcrossTheSlope` on the 5 % hill: every parcel ⊆ its rect, its extent across the slope ≤ the rect's, the summed relief over the parcels < over the rects — RED by construction (no parcels today), with the PLANNER control λ_s = 0 giving parcel == rect on the hill (the measure must discriminate on a planner output, audit E2's rule); (j) `ParcelsAreDisjointAndDry`: every cell in at most one parcel, no way cell, no water cell; (k) on the live-site rig's village arm (a hillside, surface 80..104): the terrace units' summed cut + fill over the parcels below the rects', the fence runs along the mask boundaries with every gate on the frontage run, the gate 100 %; L4: the same village from its map with parcels, the fences and ribbons in frame at the stated pose, the gate 100 %. Each red prints its measured numbers (steps, counts, segments). Test worlds: the headless rigs of `SettlementMapTest` (the hill, the flat control, the bench, a new diagonal ridge), the live-site rig on CivLabE0's village 1 and its town, the L4 world CivLabE0 — the plan's `CityLab` is still unbuilt and is not needed for M5b (stated deviation: it lands with the city tier's first L4, D45). Rig deltas: single synthetic tiles, fixed seeds, no flora, Debug; the L4 the 8-site world on Release. Perf, REASONED from the M5a stress (136 lots on 200 × 160 in 6.8-7.2 s Debug at stride 1, ≈ 50 ms per lot): the city at 48 lots ≈ 2.5 s Debug; the conquest one Dijkstra over ≤ 19 200 cells, milliseconds; the gate is the measured Release number. **OWNER DECISIONS:** (D43) the derived site rects and the hash pins — (a) every recipe derives, the version-1 and civ hashes re-pinned in the commit (the D10 precedent); (b) civ-enabled recipes derive and version-1 keeps the literal presets (recommended: the CivLab worlds are the live sites, no pinned version-1 plan moves, the re-pin of any civ-enabled box-plan hash stated loudly if one exists); (c) a recipe key. (D44) what a house parcel IS — (a) the conquered subset of the nominal toft (the growth, the sociability bell and the M5a invariants untouched; on the flat equal to today's rect, on a slope shortened across it; recommended for M5b-2) or (b) the full tessellation (parcels conquer the free land between tofts up to the fund; the growth's blocking rect becomes the footprint + setback and the sociability bell is re-stated) — deferred to M5c with the fields under (a). (D45) the city tier — with D43 (b) the civ ladder's cities can map to the city PROGRAM (today city → town, `WorldGenerator.cpp:701`, because the box plan had no city rect): recommended map it in M5b-1 and gate its first L4 on the measured Release bake (above 2 s the city stays mapped to town and the finding is recorded; the 200-lot city of the M5 contract is M5e's with the squares). Until ruled: nothing of M5b is built. **Owner rulings 2026-11-06 ("go with your recommendations"): D43 (b), D44 (a), D45 as recommended — all three taken; M5b-1 begins red first (chain 88, `pm_chain88.txt`).**

**M5 sequencing amendments (rev 4, 2026-11-09 — from the M5b-1 live-gate findings, chains 93-118 in `m5b_red_green.txt`).**
The first village and town through their grown maps blocked "street → door" routes for nine separate reasons, and every one
sat at the same seam: the planner, the forge and the realizer each inferred the DOOR, the PAD LEVEL and the APPROACH on their
own. §13.5 already asked for "the map carries the door position" and §13.6 for "pad level per lot from the map; prepare_pad
reads the same level"; M5a built neither. Amendments, in order:
1. **M5b-1 closes with the lot's approach as map data** (a short follow-up leg, before M5b-2): `MapLot` gains the door's wall
   cell + outward normal (predicted by the same deterministic autofill the forge runs — built as a realizer-side prediction in
   chain 111, to be lifted into the planner), the pad level (the way's graded level at the door: `padCubeForStreet`, a stated
   DESIGN DECISION of chain 101/105) and the approach cells with their target levels (`planDoorstepLevels`). The realizer then
   READS them instead of deriving them from a snapped marker and the chunk-side paving stamp.
2. **Why before M5b-2 and M5c:** parcels by conquest and ring ground treatment both move lot ground; M6 needs the pad and the
   way grade as a pure function of the map inside `sampleColumn`. Today's seat reads the ways unit's paved stamp from chunks at
   build time — correct for the orchestrated build, not liftable into generation as it stands.
3. **A sloped headless rig joins `SettlementMapTest` / the build-service tests** (the M5 stress row gains it): one synthetic
   field with a way CUT a cube below a lot, a way FILLED a cube or two above one, a frontage along which the way climbs ≥ 1.5
   cubes, and a setback-0 lot against the band — the four shapes that only the 10-17 minute live-site arms caught. The flat
   headless rebuild reproduced none of them.
4. **Owed at M5b-1's closure** (register O9, O14, O15): the sweep's named M0 test (DONE 2026-11-10), the `CityBench` ladder
   (NOT run — needs the 4090), the Release bake at 200 lots (MEASURED, gate not met at 233 lots).
5. **The second era's position is D46** (open).

**Design check of amendment 1, 2026-11-13 - NEEDS WORK, then proceeding on D47-D49 as proposed.** Found against the
keys: the seat's appearance depends on BUILD-TIME state in three places - the pad level is read from the ways unit's chunk
stamp, the threshold offset is MEASURED from earlier buildings of the same style in the same build (`sharedPadOffset`,
12 until one is measured: the first building of a 0.5-floor style is seated 2 micro low - order-dependent), and the
doorstep levels come from chunk scans of each approach cell. The door is already a pure prediction but lives in the
realizer. The leg, in increments: (A) the threshold above the pad as a pure forge function of (style, substructure),
the map lot pins its style, `sharedPadOffset` removed; (B) the door's wall cell + normal on `MapLot`, predicted in the
planner, the realizer compares the forge's door (D48); (C) the way profile on `MapWay` (D47), `pad_y` and `threshold`
on the lot from it; (D) the approach cells with target levels on the lot, the doorsteps unit imposes them; (E) the
sloped headless rig of amendment 3 and L4 run 13. Fields, units: `door [x, z]` settlement-local cubes + `door_normal`,
`pad_y` world cube, `threshold` world micro (standing level), `approach [{x, z, level}]` world micro, `style`. Tests
named: `SettlementMapTest.EveryLotCarriesItsDoorPadAndApproach`, `.ApproachIsIdenticalUnderGeneratorAndChunkSamplers`,
`SettlementBuildServiceTest.SeatIsIndependentOfBuildOrder`, `.RealizedDoorEqualsTheMapsDoor` (every typology x 4
orientations x seeds). Stated in advance: the four shapes of the sloped rig were fixed live in M5b-1, so that rig is a
PIN on today's code, not red-before-green; the bake time (O15, already over its gate at 233 lots) grows by one autofill
per lot and the way grading - measured per increment.
**M5b-2 design re-check (2026-11-14, against the tree at 13f2d9e1; the check of 2026-11-06 and D44 (a) stand - this
records what M5b-1 / M5b-1a changed under it): READY, on three design decisions PROPOSED (D50-D52), proceeding as
with D47-D49.** *What the tree is now.* A lot's `rect` is frontage x depth with the footprint at the FRONT (`lotShape`,
SettlementMapPlanner.cpp:60-72: the setback strip, the footprint, then the back yard of depth - setback - dDim); the
growth blocks the whole rect (:507). The parcel has FIVE consumers, one more than the first check counted: clearing
(SettlementBuildService.cpp:876), the terrace unit (:943, the rect's median + a 4-cube skirt), the fences (:1710,
`planParcelFenceRuns(x, z, w, d)` - a rect only), the yard props (:1746, `planYardProps`), and - new in M5b-1a - the
PLANNER's copy of the terrace rule (SettlementMapPlanner.cpp:662-), from whose terraced ground the ways are graded and
every seat, threshold and approach is derived. So a parcel mask moves map data that M5b-1a pinned: on a slope the
terrace region shrinks, the median may move, and with it way levels, pads and approaches. On the flat nothing moves
(the control). *1. Voxel aesthetic.* Unchanged from the first check; one addition: a Dijkstra mask on integer terrain
is ragged, and a ragged fence line is a foreign idiom - D51 regularizes the mask to a per-column depth profile.
*2. Chunk independence.* The conquest reads the injected height sampler only (the same one the growth reads); no
chunk quantity. Pin: `MapIsOrderIndependentOfItsSamplers` extended to the parcel masks. *3. Pipeline.* Inside the
planner the conquest runs AFTER the growth (it needs every rect and way cell) and BEFORE the terrace / way grading /
seat pass, so the planner's terrace and the realizer's read the same mask; the realizer's terrace unit takes the
mask's cells for the median and the skirt runs off the mask's boundary. Order-independent: one multi-source Dijkstra
per lot inside its own rect (D44 a: parcel inside rect, rects disjoint - so lots cannot contend and no tie-break
between lots exists); ties inside a lot by cell index. *4. API.* `MapLot.parcel`: a list of settlement-local cube
rects [x, z, w, d] whose union is the mask (row runs merged); absent = the rect (old ledgers); echoed by the ledger,
`worldforge_map`, `worldforge_minimap` (the mask drawn inside the rect's outline). Clamp: fund <= the lot's drawn
depth. Defaults that change: every map build's parcels on sloped ground; `legacy_layout` keeps rects. Pins that must
hold byte-identical: the version-1 and Mountains plan hashes (the map is out of `planHash`), every flat-rig map.
*5. Tests, red first, predictions in `pm_chainN.txt`.* (h) `ParcelsEqualTheToftOnTheFlat` - mask == rect on every
tier; (i) `ParcelsShrinkWithTheSlope` - NOT on the 5 % hill as first written: with D52 a 20-deep toft on 5 % loses
nothing (computed: 19 steps x 4.7 x 0.05^2 = 0.22 of a cell, under the half-cell slack), which is the intended
behaviour, so the 5 % hill becomes a SECOND control (mask == rect, predicted) and the red runs on increment E's
rolling field and on a 20 % plane (computed: 3 to 4 of 20 rows lost where the slope runs along the depth, none
where it runs along the frontage); the planner control lambda_s = 0 gives mask == rect on both; (j)
`ParcelsAreDisjointAndDry`; (l) NEW `EveryParcelHoldsItsFootprintAndFrontage` - footprint + setback strip inside the
mask, the mask 4-connected, inside the rect; (m) NEW `FenceRunsFollowTheParcelBoundary` (L2) - the traced runs cover
every boundary edge of the mask once, a post at every vertex, exactly one gate and it on the frontage run; given a
rect mask the output equals `planParcelFenceRuns` (the control); (n) NEW the JSON round-trip with `parcel`, and a
record without it reads back as the rect; (k) the live-site arms: the terrace units' cut + fill summed over masks
against the same build with lambda_s = 0, the fences on the mask boundary, the gate counts, and - because the map
moves - M5b-1a's seat / approach agreement counts re-measured with every difference from run 19 listed; L4 run 20 on
CivLab with the control first. Printed, not gated: the count of lots whose frontage row is not adjacent to a way cell
(O21 (i), the seatless lots - carried, not solved here) and the bake's per-phase ms (O15 asks for a profile before the
lot count grows). Stress: the 136-lot growth rig with parcels (count), the rolling field (extent), lambda_s x 0, 1,
4.7, 20 (the sensitivity print), a 2-cell-deep degenerate rect, a lot wholly on a 46 % slope (the fund's floor: D50).
Not in this leg: fields and the tessellation (M5c, D44 b), yard props re-planned for a short back yard beyond
dropping the ones outside the mask (counted).

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

> **As built (rev 4, 2026-11-09).** The table and the recipe sketch below are the 2026-10-14 INTENT. Built: `worldforge/plan`
> (returns `civ`, `corridors`, crossings, refused demands, clamped params), `GET /api/worldforge/civ` (+ MCP `worldforge_civ`),
> `worldforge/map` (summarizes `site_maps`), `worldforge/minimap` (tiers, routes, lots, ways), `worldforge/status` (`site_list`,
> the ledger with each site's map), `worldforge/build`, `worldforge/focus`, `worldforge/apply`, `terrain_height?source=`,
> `allow_unwalkable`, `legacy_layout`. NOT built: `worldforge/sheet`, `site/{id}/map`, `site/{id}/ledger`, `worldforge/realize`
> (the alias), `worldforge/hydro`. The recipe keys as shipped are flat on `WorldForgeParams` / `SheetParams`, not the nested
> sketch: `sheet{continentCells, mesoSteps, erosionSteps/Dt/K/M/UpliftMax, upliftMask}`, `civ`, `civEpochs`, `civShockEpoch`,
> `civShockFraction`, `civAbandonShare`, `civDensityScale`, `civSpacingKm`, `civSlopeCap`, `sitePins`, `gradeCap[4]`,
> `routeReuseFraction`, `bridgeMaxSpanU`, `bridgeSingleSpanU`, `pierSpacingU`, `fordMaxDepthU`, `wallTierU`, `rockCutMaxU`,
> `era`, runoff; `derivedSiteRects` is set by the civ path and never serialized (D43). There is no `cutMaxU` / `fillMaxU` /
> `tiersPer100km2` / `beachBandU` / `settlement.density` key.

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
| Road-grade JUNCTION SEAM (`WorldForgePlan.cpp` junction reconciliation + `sampleColumn` nearest-road grade) | measured 2026-10-22 on the re-routed Mountains 424242 network (13 roads): 3 of 9 917 centreline steps were 2 cubes where roads 0-4 and 1-4 overlap near site 4 (grade 109.51 → 108.66 between adjacent columns); the vertex-only reconciliation keeps the two profiles equal at projected vertices but the surface takes the NEAREST road's grade, and between reconciled vertices the two interpolations can still disagree where the nearest road switches | latent on every network; the shipped version-1 fixture passes only because its 10-road network has no such switch | DEFECT (v2 road work, M4): replace nearest-road grade by a weight-blended grade over every corridor touching the column (continuous by construction), with the Mountains step test re-run on the 13-road network as the red |
| Agent step-up 4/9 m (`AnimatedVoxelCharacter.cpp:1611`, clamp 0.34–0.70) | engine physics; real stair risers: IRC R311.7.5.1 max 7¾ in = 196.85 mm ([Viewrail code guide](https://resources.viewrail.com/code-compliance/stair-code/tread-depth-and-risers)); UK Approved Document K private stairs max 220 / 200 mm ([BWF](https://www.bwf.org.uk/question-of-the-week/what-are-the-current-recommendations-for-the-rise-and-going-of-stairs/)) — **F4b GROUNDED** | keep 4/9 as the *unclimbable* floor; never use it as a comfort criterion | GROUNDED (engine) / CONVENIENCE vs real stairs |
| Embankment side slope | angle of repose, Pocket Ref via [Wikipedia](https://en.wikipedia.org/wiki/Angle_of_repose): soil 30–45°, dry sand 34°, gravel+sand 25–30°, crushed stone 45°, wet clay 15°; a built slope is flatter by a safety factor. **F4b: AASHTO/NJDOT 1V:3H–1V:4H figures are vehicle run-off-recovery thresholds, the WRONG FRAME for a pre-industrial bank — do not use them**; a geotechnical repose table (USACE EM 1110-2-1913 or NAVFAC DM-7) is still to be read. `materials.json` has NO repose column (friction is a contact coefficient) | per-material repose table to be added to `materials.json` with the Pocket Ref citation; built slope = repose − safety factor | REASONED (safety factor unsourced) |
| Max cut depth / fill height before a retaining wall | **Grounding pass 2026-10-28 (grounding-auditor; full table in `m4b4_design_check` below):** repose of scree 35° = 0.70 GROUNDED (Scottish Geology Trust Wasdale geoguide; Québec 1961 talus study); dry-stone batter 1:6–1:10 GROUNDED as a practitioner rule (Fine Homebuilding) and 10 % on Inca terrace walls (ASCE Machu Picchu landmark page); retaining-wall base ≈ 0.67 × height for a loaded wall GROUNDED-weak (UK conservation handbook, walls ≤ 1.8 m); a single dry-stone tier tested at 2.5 m (Colas/Morel/Garnier full-scale trials, abstract only) — no stated maximum height anywhere; tiered walls only in modern segmental guidance (bench ≥ 2 × the lower wall, CMHA/Murata — NOT period, NOT dry-stone); the only deep road cut with a source is the Via Appia at Pisco Montano, Terracina: a **36 m vertical rock cut**, unwalled (120 Roman feet; a CXXVIII mark suggests 38 m) — alpine Roman and medieval cut maxima NOT FOUND. **Derivation stands:** repose bank up to 12 u × 0.70 = 8.4 u; a dry-stone tier of ≤ 3 u above it (REASONED from 2.5 m tested / 1.8 m handbook); the rest a rock face up to the 36 u precedent; beyond that nothing supports the cut (D33) | STRUCK → grounded in parts, derivation + D33 |
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
| Valley-side slope bound (M2d-4) | Frontiers Earth Sci. 2021 10.3389/feart.2021.684365 (read): threshold hillslope angles 30 ± 5°, 82 % of slopes inside it, basin modes 26–33°, the Olympics / Southern Alps / E. Himalaya quoted at the same band (second-hand); Larsen & Montgomery 2012 via the UW release (snippet): erosion rises sharply above ≈ 30°; Burbank et al. 1996 (abstract): hillslopes "steep and essentially independent of erosion rate" — NO angle given, the "32°" often quoted is unverified (the Nature paper unread) | 0.70 = 35°, the top of the band and = the grounded scree repose; the natural wins where it is steeper | REASONED (0.58 = 30° would be the modal value) |
| River above the adjacent land (the residual embankment bound, M2d-4) | PMC7780550 *Natural levee evolution* (read): levee height "from a few centimeters up to 3 m", modelled 0.4–2.1 m, ≈ 20 % of bankfull depth; tens of metres only on the Amazon / Yellow / Mekong | asserted ≤ 1 u on every carved channel outside a basin | GROUNDED (conservative; 100–250 u embankments are outside every source) |
| Floodplain cross-section shape (M2d-4) | none read for the shape; NetMap floodplain mapping (web): a floodplain lies ≈ 2 × bankfull depth above the channel and is flat, 4 × = terrace/toeslope; the 5 × width multiple stays NEEDS-RESEARCH (row above) | a flat floor at the bankfull rim across `kValleyWidthMul` × the width, then the side | REASONED (flat) · NEEDS-RESEARCH (the transition) |
| Gorge side slope (a water gap, M2d-4 → D38) | Haviv, Enzel, Whipple, Zilberman, Matmon, Stone, Fifield 2010, JGR 115 F03028 (the abstract read): "Vertical knickpoints (waterfalls)", the subcaprock "vertically jointed" — a waterfall face is vertical; Iowa DOT Design Manual 200F-8 Rock Cut Slopes (2026 update, read): good limestone / dolomite / cemented sandstone "nearly vertical", 0.5H:1V for normal rock excavation, 0.25:1 with pre-splitting, shale no steeper than 2H:1V | kRockFaceSlope = 4.0 per u (76°): the steepest standard rock-cut inclination short of vertical; the natural face is vertical, so the bound is conservative; the penalty slope (4.0 − 2 × 0.70) / 2 = 1.3 | GROUNDED to an engineering standard (borrowed: a road-cut inclination, not a measured gorge wall) / DERIVED (the penalty); unexercised by the section test (0 face steps), exercised only by the ablation |
| Cascade entrenchment (M2d-4 residual, D38 — BUILT) | the measured cause was not the cascades: a source's floor wrapping behind its head + the exact lever (m2d4_red_green.txt "D38 RULED" onward) | the floor stops at the head (the along-distance rises at 0.70), a per-station profile lowered TO the demanded level with knees graded at 0.70, the cliff allowance (above) for pairs across a cascade; Mountains max base lowering 133 → 20.8 u at a fixed point in 10 rounds | REASONED (the along-slope = the hillslope bound) / DERIVED |
| Route reuse fraction (M4a, D27): a cell already on the built network costs this fraction of a fresh cell | Tobler 1993 hiking function (via the Wikipedia article, the movecost R package docs and arXiv 2303.16065): off-path walking speed = 0.6 x on-path, so a path cell costs 0.6 of a fresh cell in TIME; US Army Heritage Center marching-rates bibliography: 2.5 mph over roads vs 1 mph cross-country for trained infantry (0.4; the primary field manual not opened — verify); Helbing, Schweitzer, Keltsch, Molnár 1997 (active-walker trails): the comfort ratio is a free parameter, no value; Louf, Jensen, Barthélemy 2013: a cost-benefit weight, no reuse term; Irmischer & Clarke 2018 and Verhagen & Jeneson's network friction: NEEDS-RESEARCH (the multipliers not retrieved) | routeReuseFraction = 0.5 since 2026-11-02 (D27 ruled: the 0.4–0.6 travel-time bracket's midpoint, REASONED; was 1/3 Veloren 2026-10-22 → 2026-11-02); the mapping from a travel-time ratio to the router's combined build-and-travel cost is the REASONED step; the grade penalty is applied on top (Tobler's 0.6 is flat ground) | REASONED inside a GROUNDED bracket (0.4–0.6); the value itself is the owner's (D27) |
| Floodplain width vs bed slope (M2d-4, built) | no source read for the law; the floor's half-width `halfWidth × kValleyWidthMul × (1 − bedSlope / 0.70)` — a floodplain is a feature of a gentle reach, a torrent is confined (the general confinement relation; no coefficient read) | the floor shrinks linearly to a V at the side slope; 5 × stays the maximum | REASONED (a coefficient needs a source: NEEDS-RESEARCH) |

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
9. **Discipline enforcement.** RESOLVED 2026-10-14: the Stop-hook is wired and tested (§15 F2). Kept as a
   numbered risk so the list's numbering does not move.

---

## 13. Code-contact inventory — what each milestone actually touches (second pass, 2026-10-14)

Everything below is from reading the code, not the docs. `file:line` is the working tree on
`feat/worldforge-v2` at rev 2. A milestone is not planned until its row here is complete.

> **Rev 4 note (2026-11-09).** The `file:line` citations are the rev-2 tree (2026-10-14) and are STALE for M1-M5; 13.1-13.4
> describe work that is built (the build log is the record). Still live as written: **13.6 (M6) and 13.7 (M7)**. In 13.5:
> "lot extraction: largest axis-aligned rect inside the parcel polygon" is SUPERSEDED (D44: a parcel is a cell mask, the
> conquered subset of the nominal toft; lots are placed by the growth, not extracted); "polyline paver" is BUILT as the
> ribbon paver (`planWayPaving`, M5b-1); "map carries the door position; spur re-derived after realization" is NOT built in
> the map and is the first M5 sequencing amendment (§5, before M6); "location ↔ lot ↔ uuid link in the ledger" is not built.

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

**Labs as built (rev 4, 2026-11-09) — the table above is the 2026-10-14 specification.** Built and used:
`WorldForgeBaseline` / `WorldForgeBaselineV2` (the baseline recipe + a `world.sheet` block), `StructGenTest`, `StructGenHills`,
`WorldSheetLab`; `CivLab` AS BUILT is the WorldForgeBaselineV2 recipe + `worldforge.civ: true` (NOT the painted river + massif
sheet specified above — the "predict where the city lands" run was never made); `CivLabE0` = CivLab at `civEpochs 0` (the M5
L4 world: 1 town, 2 villages, 3 hamlets in its box, no civ city); `MountainLab` = CivLab's recipe on Mountains 424242 (M4b-4,
M2d-4, D38 L4s). NOT built: `RiverLab`, `RouteLab` (their L4s ran on CivLab / MountainLab with the fixtures as the rigs) and
`CityLab` (needed by the M5 city L4, M6 and M7 — register O16).

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
| F8 | `feat/water-rethink` merged into `main` by the owner; `feat/worldforge-v2` rebased or merged onto it before M1 | owner | **DONE 2026-10-15** (main 489caee2, merged into this branch 70f40163 - the M0 L4 run 5 record) |
| F9 | Doc consolidation list (§1 "doc rot") confirmed complete as part of M0's definition of done | agent | **DONE 2026-10-14**: `WorldForge.md` (apply row = live apply; Known gaps folded + V2 pointer; M3 numbers re-measured; header pointer), `WorldModel.md` step 1 DONE, `StructurePipelineGaps.md` 2026-09-25 marked stale, `StructureGenerationV2.md` P8–P10 superseded note, `LargeWorldScalePlan.md` far-terrain default, structure README ChimneyForge shipped, `CLAUDE.md` V2 pointer + post-grading numbers; the MCP descriptions were fixed in row 10. `WorldRenderV2Plan` M1/M2 table left (no milestone table found by its stated shape). |
| F10 | Owner sign-off on this plan as a whole (after F3–F7 are folded in) | owner | **SIGNED OFF 2026-10-14** (owner: "lets get to work"). M0 started the same day on `feat/worldforge-v2`; its two generator-side rows (earthworks, ford) remain parked until F8 (water merge) and the Roman-paper verification. |

Order: F3 (M0 first) → F4 → F5 → F6 → F7 → F10, then M0 begins. F3 for M1–M8 is run when each
milestone is next, not all up front, so each design-check sees the code as it then is.
