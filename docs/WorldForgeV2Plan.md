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
| D18 | **Sites are chosen at MACRO resolution over the whole continent and refined locally** (owner, 2026-10-22: "i will go with your recomendations"): the 512 u sheet (base, continentalness, climate, drainage area, filled surface, receiver graph) is the suitability field for all ~300 sites; a site inside the meso box is snapped to its best meso cell by the existing scoring, sites beyond it keep their macro position until the meso bake reaches them (planned, not realized or routed — stated) | the meso box is 32 km, the continent 102 km; the full-continent meso bake is ~10× (≈ 300 MB, ~35 s Release) and is a later measured increment | grow the meso box first |
| D19 | **Tier targets are derived from Christaller's ladder inside the Domesday anchor** (owner, 2026-10-22): spacing city 62 / town 36 / village 21 / hamlet 12 / outpost 7 km (the √3 ladder, §10 F4), density per tier ≈ 1 / (0.866·s²) ⇒ ≈ 0.03 / 0.09 / 0.26 / 0.8 / 2.4 per 100 km² ≈ 3.6 per 100 km² total (≈ 360 on the 100 km continent; Domesday's ≈ 10 per 100 km² is the ceiling, T2's 300 the "gameplay-thinned" target) — recipe fields, DESIGN inside grounded anchors | one ladder, one anchor, no hand-typed counts | hand-picked counts per tier |
| D20 | **M3a sites first, M3b history second** (owner, 2026-10-22): M3a delivers the sited, tiered, deterministic plan with the spacing and hierarchy invariants; the growth simulation, ruins and the history ledger follow as M3b with their own design check | "300 sites" means live sites until M3b defines founded/abandoned | history inside M3 |
| D21 | **History never changes a site's tier** (owner, 2026-10-22, took the recommendation): tiers stay the ladder's at founding; `state` (live / declining / abandoned) and the population index modulate realization density only | the spacing and hierarchy invariants M3a proved stay provable | a declined city realized as a town |
| D22 | **A ruin in M3b is a site state + map glyph only** (owner, 2026-10-22): the ruin DRESSING is a structure-forge typology for M5/M7 | keeps forge work out of a siting milestone | M3b dresses ruins |
| D23 | **Default arc: 12 epochs × 25 years, one shock at epoch 9 of −45 %, `civEpochs` shipped at 0** (owner, 2026-10-22): history is off until an engine run shows the ruins read well; at 0 the plan is M3a bit for bit | the England 1086 → 1377 anchors (§10 F4g) | history on by default |
| D24 | **The blue-noise / annealed site pick is DEFERRED** (owner, 2026-10-22): the greedy pick's ~70 % of the hex ceiling stands; revisit as its own increment (M3c) after M3b | keeps the history diff readable | inside M3b |
| D25 | **M4 in three increments** (owner, 2026-10-22, took the recommendation): M4a continent corridor graph on the macro sheet + box realization re-routed along the corridors; M4b per-class grade caps + the blended grade field + switchbacks + derived earthworks; M4c bridges/fords/tunnels as edges | three reds, three mechanisms | one L increment |
| D26 | **Per-class grade caps 0.05 / 0.08 / 0.15 on version-2 recipes** (owner, 2026-10-22): the shipped 0.5 is a MISMATCH and stays only on the pinned version-1 path; the cost (longer routes, switchbacks, cuttings on mountain seeds) is accepted | the grounded sources in section 10 | a relaxed track cap |
| D27 | **Reuse fraction 1/3, labelled REASONED** (owner, 2026-10-22): shipped and measured against the V1 MST-plus-detour network; a grounded value researched in parallel | Veloren; no source yet | keep the V1 detour rule |
| D28 | **Roads beyond the hydrology box are stamped from the macro corridors at macro fidelity** (owner, 2026-10-22): material + drape, no grading, no crossings; the box is where a route gains grade, earthworks and bridges | continuity across the continent without a moving or full-continent bake | a moving bake; the full-continent meso bake |
| D30 | **Tunnels DEFERRED** (owner, 2026-10-27, took the recommendation: no medieval precedent at our scale; a mountain route is pitched or walled) | Furlo (38 m, AD 77) the one Roman road tunnel of note | bored tunnels |
| D31 | **The grounded span rule** (owner, 2026-10-27): single timber span ≤ 6 u, piered ≤ 25 u OVERALL (audit: shoulders included), piers every ≈ 10 u; wider channels uncrossable for routing; version 1 keeps 96 u | Sherway, Hemington, Aldwincle; §10 | decking any width |
| D32 | **Fords = creek crossings** (owner, 2026-10-27): channel depth < 0.5 u = the M2b-2 class split; deeper water is bridge-or-nothing | NWS 0.15 m moving water; the class split | fords to the pin depth |
| D33 | **Cuts beyond the rock-cut precedent — RULED 2026-10-28 (owner: "recommendations" → (b)).** a walkable road across a cliff cuts up to 80 u on the Mountains world; the only sourced road cut is 36 m (Pisco Montano, vertical rock). Options: (a) realize as a rock face up to 36 u and REPORT deeper cuts as unprecedented (the D29 acceptance, now visible as rock); (b) make a cut beyond the precedent a refused EDGE like a crossing (the M4c lazy check: realize → judge → close the cells → re-route, else refuse the demand with the reason); (c) cap the envelope and let the road be unwalkable there (rejected by the M0 walkability gate). **Recommended: (b)** — the M4c machinery exists, a demand that can only be served by an unprecedented cut is honestly refused or goes around, and the Mountains refusal count becomes the measure of the terrain, not the road | one data point (36 m) | (a) |
| D34 | **One dry-stone tier, no terracing — RULED 2026-10-28 (owner took the recommendation).** a dry-stone retaining tier is supported to ≈ 2.5–3 m; terraced tiers exist only in modern segmental guidance with benches ≥ 2 × the lower wall, which stacks at a slope of ≈ 0.43 — SHALLOWER than the 0.70 repose bank, so a second tier never beats widening the bank. Recommended: one tier ≤ 3 u at the road edge, the repose bank above it over the 12 u shoulder, the remainder a rock face (D33). Alternative: cosmetic terraces (tiers with benches) for the look | 2.5 m trials; CMHA 2H rule (modern) | terraces |
| D35 | **Wall section — DERIVED, for the record (M4b-4):** thickness ⌈0.67 × tier height⌉ u (the loaded-wall base ratio; 1 u up to 1.5 u of wall, 2 u up to 3 u), batter one subcube (1/3 u) set back at the top course ≈ 1:9 (inside the 1:6–1:10 rule), material `Cobblestone` (the settlement's dry_stone), the rock face `Stone`; not an owner decision unless the look is wrong | handbook ratios | 1 u walls |
| D29 | **Rough-mountain roads: pitched roads accepted now, retaining-wall earthworks next** (owner, 2026-10-27, took the recommendation): the class caps are a ruling grade for favourable ground; on rough ground a road climbs in short pitches up to the walkable 0.5 with cuts bounded at the class floor except at cliffs, every figure reported per plan; the cut faces beyond the repose shoulder become dry-stone retaining walls in the next M4 increment (M4b-4); fine-resolution routing stays off | the pitched length is sub-128 u roughness, not slope (M4b-3 diagnostics) | fine-resolution routing (5-6 min Debug per box plan, no gentler line found) |
| D36 | **The valley section on version-2 carved channels: a flat alluvial floor at the bankfull rim across `kValleyWidthMul` × the channel width, then a valley side rising at ≤ 0.70 per u, the natural ground winning wherever it is lower or itself steeper (a cliff the mountain has stays); replaces the fractional relief attenuation, which on 100–300 u of relief makes walls of 10–64 u per column** — RECOMMENDED (M2d-4 design check, 2026-10-29) | 0.70 = 35°, the top of the 30 ± 5° threshold-hillslope band (Frontiers Earth Sci. 2021, 10.3389/feart.2021.684365, read) and = the GROUNDED scree repose the roads use — REASONED as a bound; the flat floor REASONED (a floodplain is a flat landform), the 5 × width stays the labelled tunable | alternatives: 0.58 (30°, the modal value — wider valleys, more cut); keeping the fraction with an absolute depth cap (the creek fix) cannot hold mountain relief |
| D37 | **The bed profile's base per CARVED cell = the minimum of the unattenuated ground over the channel's WARPED footprint (5 points along the cell's polyline × the centreline and ± half-width, each at the column under the warp; DESIGN), not the cell-centre point sample** — the embankment fix: the rim then never stands above a natural column inside the footprint by construction, the residual bounded by the sampling and asserted ≤ 1 u; creeks never fill, so they keep the point sample; the downstream minimum envelope stays — RECOMMENDED (M2d-4 design check, 2026-10-29) | natural levees: a few cm to 3 m above the floodplain, ≈ 20 % of bankfull depth (PMC7780550, read) — the 1 u bound GROUNDED-conservative | alternative: keep the point sample and clamp the fill — a river cannot be refused, so a clamp leaves the embankment standing at the clamp height |
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

**M3a BUILT 2026-10-22 (D18–D20 settled by the owner; red → green, `m3a_red_green.txt`).** `CivPlan` (new, `engine/{include,src}/core/CivPlan.*`) sites over the WHOLE continent at the macro cell (512 u, D18): per-cell suitability = 0.30 relief (slope vs the tier's cap) + 0.30 water (a bell on the chamfer distance to sub-sea / basin / ≥ 0.5 km² river cells) + 0.20 food (climate) + 0.10 trade (coast / river proximity) + 0.05 forest + 0.05 mining, × treeline hostility, viable ≥ 0.15; tier targets DERIVED per tier from the ladder's spacing and the land area (density 1/(0.866·s²) per tier, cumulative, D19 — `civSpacingKm = {62, 36, 21, 12, 7}`, `civSlopeCap = {0.05, 0.05, 0.08, 0.15, 0.15}`, `civDensityScale`); greedy per tier by score, spacing enforced against every site of the same or a higher tier; `sitePins` seated as towns or REFUSED with the reason; the box plan (the V1 `WorldForgePlan` the realizer builds) is seeded by the civ sites inside the hydrology box (city/town → town, village → village, hamlet → hamlet via the new `pinTiers`; outposts wait for M4's routes) and the V1 3–8 `siteCount` clamp lifts to 256 when `civ` is on. Baseline world (Perlin 20260816, Debug): **157 sites over 7 927 km² of land in 72–79 ms** — city 2/2, town 5/5, village 13/13, hamlet 38/43, outpost 99/123 (the two low tiers degrade honestly: no viable cell left at the ladder's spacing, each shortfall a logged refusal); 0 spacing violations over 157² pairs; adjacent-tier ratios 2.60 / 2.92 / 2.61 (K = 3 ladder, inside [2, 4.5]); no site in water, in a basin or above its slope cap; bit-identical across two generators; a pin on the macro sea refused with its reason; the box plan = exactly the 6 civ sites inside the box. 7 tests red on the stub build → 7/7 green (`CivPlanTest`). Not yet: the `worldforge_plan` / `worldforge_map` echo of civ tiers (L4), CivLab, M3b history. Two version-1 drifts surfaced by the fixtures on the way and fixed in the same commit (M2c correction above): the rim corrector now version-2 only, `carved` stripped from the plan hash, version-1 hashes pinned.

**M3a L4 2026-10-22 (Release, `CivLab` = `PhyxelProjects/CivLab`: the WorldForgeBaselineV2 recipe + `worldforge.civ: true`, fresh DB; `m3a_l4_green.txt`, raw responses `m3a_l4_green_{plan,civ}.json`, PNGs `m3a_l4_green_minimap_{continent,box}.png`).** The API now carries the plan: `worldforge_plan {}` returns the box plan (6 sites: 1 town + 5 hamlets, 7 roads, 4 bridges, `params.civ: true`, `params.siteCount: 6` — the clamp lifted) AND `civ` (157 sites, 7 927 km², **30 ms** on Release); the box plan's six sites are, position for position, the six non-outpost civ sites inside the box (checked from the raw JSON). New `GET /api/worldforge/civ?step=` / MCP `worldforge_civ`: the civ plan plus a 100×100-char map of the whole macro sheet (sea / basin / land / the box corners / C-T-V-H-O glyphs). `worldforge_minimap` draws the civ sites over the whole continent (city magenta-red, outpost grey). RED on the Debug exe of 0027e94b (`m3a_l4_red.txt`): the engine log shows the civ plan baked but `/api/worldforge/civ` returned an empty body (route absent) and the plan response had no `civ` key — the Debug game loop also wedged on the version-2 world (every queued call timed out), the known Debug-on-large-worlds limit, so the red is the route-absent + key-absent fact, not a timing one. Known limit seen in the continent PNG: the minimap renders water from the meso bake, so outside the hydrology box the sea the civ map marks as `~` is drawn as land (the full-continent bake row in §9 owns this). Not yet: M3b history.

**M3a STRESS 2026-10-22 (`CivPlanStressTest`, synthetic macro sheets, red → green `m3a_stress_red_green.txt`).** Six rigs: an all-sea continent (0 land, 0 targets, 0 sites, 20 pins refused as water), an all-ridge continent (slope 0.4 everywhere: 2 621 km² of land, real targets, ZERO sites, one named shortfall per tier that had a target), a flat fertile plain at `civDensityScale` 10, a 400×400 sheet (41 838 km², 606 sites, 329 ms Debug, every invariant at every site), a 4×4 sheet (nothing derived, no crash) and 300 pins of five kinds. The red found **two real defects and one wrong definition**: (1) 50 pins on one cell seated 50 towns on top of each other (`town 150/6`) and 96 pins inside the town spacing seated silently — now refused with "is already a site" / "is within the town spacing of pin (x,z)", and a higher tier keeps a pinned lower tier's own spacing so the pinned site's invariant holds too; (2) `civDensityScale` scaled the TARGETS at the unchanged spacing, so ×10 placed 167 sites against 172 at ×1 — the spacing is what the density is: the ladder's spacings now shrink by √scale and the targets follow from the same formula (×10 → 1 470 sites, spacings /3.16, 0 violations); (3) the all-ridge rig's "5 shortfalls" assumption was wrong (2 621 km² derives no city). **Property recorded, not a defect:** on a flat plain where every cell is viable the greedy pick reaches ~70 % of the hex-packing ceiling per tier (village 13/18, hamlet 38/56, outpost 112/162; the ×10 plain 21/31 … 912/1 622) — the target is the ceiling (density 1/(0.866 s²)), greedy-by-score packs like a square lattice; a blue-noise or annealed pick is the lever if the owner wants the ladder met in full (M3b candidate). The baseline world is unchanged by the fixes (157 sites, same tiers).

**M3a AUDIT 2026-10-22 (solution-auditor, verdict FAIL overall on the first pass; `m3a_red_green.txt` "AUDIT FOLLOW-UP").** The auditor accepted the two version-1 drift fixes and the stress phase as REAL, found the civ-API red meaningless (a Debug game-loop timeout, not a route-absent red — downgraded: the API has a Release green only), and found the one unproven invariant: "no site in water" had only ever been checked against the macro sheet the planner scored from, while 101 of 157 sites sat in macro river cells. The real-terrain test it asked for (`CivPlanTest.NoSiteIsInWaterOnTheRealTerrain`: every site's centre column sampled, surface vs sea level / the rim-corrected level inside the box / carved and creek beds; mutation red shown — sea cells counted as land → 95 of 158 wet) then found a REAL defect on the shipped code: **3 of 157 sites stood at exactly sea level on the real terrain, one of them a box hamlet** — the meso relaxation and the M2e coast shaping pull a near-shore column below its macro base. Fix: a macro cell is siting land only above the same 2 u coastal strip the box plan's candidate scan already refuses (REUSED, not a new constant), and civ-seeded pins are refined on the box plan's 16 u grid like any scored site (D18's local refinement; `WorldForgeParams::pinRefine`, owner pins stay verbatim). Baseline after the fix: **154 sites over 7 750 km²** (the strip removed one town cell): city 2/2, town 4/4, village 14/14, hamlet 37/42, outpost 97/120; wet 0, on a bed 0, worst margin 2.0 u; 40/40 across the civ, stress, pinned-hash, recipe and plan fixtures. The L4 figures above (157 sites, 6 box sites) are the pre-fix numbers; the L4 was re-run on Release after the fix (`m3a_l4_green2_*`). **Second catch the same hour:** cross-checking that re-run's raw JSON showed all five box sites still AT their macro centres — the refinement had been a silent no-op (every candidate rejected by the box plan's 2048 u `regionRadius`, which civ seeds lie far outside), so the "refined positions" claim of c772277f was false. Red: a new assertion in `TheBoxPlanIsSeededByTheCivSites` (the 16 u offset grid never contains the centre itself, so an unmoved site = unrefined) failed 0 of 5; fix: the radius bounds the candidate SCAN, never a pin's refinement; green 5 of 5 moved, farthest 79.2 u (the grid's corner), 34/34; Release L4 re-run again (`m3a_l4_green3_*`, moves cross-checked from the raw JSON).

**M3b design check (2026-10-22, the history ledger — run against D20 and the M3a code as built): NEEDS WORK — three owner decisions, then engineering.**
1. *Voxel aesthetic:* M3b draws nothing itself; a ruin is a site STATE that the structure forge will dress later (an unroofed, part-collapsed typology authored in subcubes/microcubes like every other building — the forge's job, not M3b's). Pass, provided D22 keeps the ruin dressing out of M3b.
2. *Chunk independence:* the history is one more global bake on the civ plan (pure in sheet + params + seed; no column reads it except through the plan), so every quantity stays a function of world position; equality test = `CivPlanTest.DeterministicAcrossGenerators` extended over the history fields, plus `CivHistoryTest.EpochsZeroIsM3aBitForBit` (the `epochs: 0` plan must equal today's plan site for site — the pinned contract that M3b cannot move M3a's output). Pass by construction.
3. *Pipeline:* stage = civ plan, after siting, before M4's routes (the economy edges M4 reuses are an OUTPUT of the history). Model (Dwarf Fortress / Songs of Syx zero-player sim, §11): N epochs over the sited plan; per epoch each live site's population index grows with its suitability and its trade (coast / large river / later a route), a FOUND step seats new hamlets/outposts on free viable cells inside the spacing (the ladder stays the ceiling), one SHOCK epoch cuts every population index, sites below viability after it DECLINE then are ABANDONED → `ruin`; provinces = each city's/town's macro-cell Voronoi by cost distance (the economy graph M4 routes). **Grounding (§10 F4g, this check):** the arc is England 1086 → 1377: population ≈ 1.75–2.25 M at Domesday → ≈ 4.7–6 M at the 1300 peak → ≈ 2.5–3 M at the 1377 poll tax (Russell / Hatcher / Broadberry-Campbell-van Leeuwen; the shock is a −45 % to −50 % step) — so the default is 12 epochs of 25 years with the shock at epoch 9 (≈ 1348) [REASONED from the anchors]; deserted medieval villages 2 263 (Beresford & Hurst 1968 gazetteer) → 2 811 (1977 map) against ≈ 13 000 Domesday places ≈ **20 % of ever-founded villages abandoned** (GROUNDED as the ledger's control number; the Hull *Beresford's Lost Villages* project maps 2 222 today); planted towns 171 in England 1066–1370 (+ 84 Wales, 124 Gascony; the 1967 review of Beresford's *New Towns of the Middle Ages*) over ≈ 130 000 km² ≈ **0.13 town foundings per 100 km² per 300 years** (GROUNDED: town-tier founding is RARE; hamlet/outpost founding carries the growth). Persistence: the history is derived (never stored), the recipe carries `civ.epochs` + `civ.shockEpoch`; the realizer reads a site's `era`/`state` through the existing `SettlementProgram` era hook (M0 row "era reaches buildings"), so an abandoned site resolves to the `ruin` state the forge must own (D22).
4. *API:* `civ.sites[]` gain `founded`, `peak`, `state ∈ live|declining|abandoned`, `population` (index, unitless, echoed), `history[] {epoch, event ∈ founded|grew|declined|shock|abandoned, value}`; `civ.provinces[] {seat, cells, members[]}`; `worldforge_civ` draws ruins as lowercase glyphs; `worldforge_minimap` hollow grey squares. Params: `civ.epochs` (int, clamp 0..64; **0 = no history = M3a's plan bit for bit**, the pinned default until the owner flips it), `civ.shockEpoch` (clamp −1..epochs, −1 = none), `civ.shockFraction` (clamp 0..0.9, default 0.45 GROUNDED above). Defaults change nothing today (`epochs` 0); flipping the default re-pins `CivPlanTest` in the same commit.
5. *Visual test:* "works" = on the baseline world with `epochs 12`, the abandoned fraction of ever-founded village-and-below sites lands in 15–25 % (the DMV control), no live site breaks a spacing or water/slope invariant, every ruin was once live (its ledger shows found → … → abandoned), provinces partition the land cells, and the plan is deterministic. L2 `CivHistoryTest` (red first: `AbandonedFractionMatchesTheDesertedVillageRate`, `EveryRuinHasAFoundedEvent`, `LiveSitesKeepEveryM3aInvariant`, `ProvincesPartitionTheLand`, `EpochsZeroIsM3aBitForBit`); stress rigs on the synthetic sheets (`epochs 64`, shock at epoch 0, all-sea, ×10 density, 300 pins — the ruin fraction bounded, no site founded twice); L4 on `CivLab` (the civ map with ruins + the minimap, raw JSON archived). Rig vs shipped: only `epochs`/`shockEpoch` differ.
**RULED 2026-10-22: D21 no (tiers stay), D22 yes (state + glyph only), D23 yes (12 × 25 y, shock at 9, −45 %, `civEpochs` 0 shipped), D24 blue-noise pick deferred → verdict READY; M3b builds from the red tests above.**

**M3b BUILT 2026-10-22 (red → green, `m3b_red_green.txt`; L4 `m3a_l4_m3b_*`).** The ledger rides `CivPlan::bake` after the pick: cities/towns/villages stand at epoch 0 (the Domesday places), hamlets and outposts are founded in score order over the epochs before the shock (the growth that filled the land); every live site grows ×(1 + 0.114·(0.5 + score)) per epoch (GROUNDED: ≈ 2.0 M → ≈ 5 M over 8.5 epochs = ×1.114 at a mid score); the shock cuts every population index by `civShockFraction`; the marginal `civAbandonShare` of each village-and-below tier declines at the shock and is abandoned the next epoch; cities and towns never abandon (DESIGN); provinces = every land cell to its nearest city/town seat (8-neighbour Dijkstra over the macro grid). Params `civEpochs` (0..64, **0 shipped** = M3a bit for bit), `civShockEpoch` (−1 none; 0 clamps to 1, epoch 0 is the founding), `civShockFraction` (0..0.9), `civAbandonShare` (0..1) — recipe round-trip, echoed by `worldforge_plan`; `worldforge_civ` draws ruins as lowercase glyphs, the minimap as grey squares. **The prediction written before the first run FAILED and is recorded:** "post-shock viability against the plan's own `kMinViable` lands the baseline in the 15–25 % deserted-village band" gave 0 of 148 at −45 % and 148 of 154 at −90 % — the greedy pick seats only the better cells, so a fixed floor is never a 20 % band. The abandonment rate is therefore an INPUT (`civAbandonShare` 0.20, GROUNDED as a rate from 2 811 deserted villages of ≈ 13 000 Domesday places) applied to the lowest-suitability sites (Beresford's marginal land); **a mechanism that derives the rate from the shock is NEEDS-RESEARCH** — today the ruin count does not change with the shock size (31 at −45 % and at −90 %). Baseline, 12 epochs: 154 sites → 123 live, 31 abandoned (3 villages, 8 hamlets, 20 outposts = 20.9 % of village-and-below, 0 seats), every ruin's ledger founded → grew… → shock → declined → abandoned, 6 provinces partitioning all 29 565 land cells with all 154 sites members, the site set and tiers identical to `civEpochs 0`, deterministic across generators, 64-epoch / shock-at-0 / 90 % / no-shock rigs clean. **Second red from the first engine run's raw JSON:** the abandoned hamlet at (1280, −8960) had been seeded into the box plan as a LIVE hamlet — `RuinsAreNotSeededIntoTheBoxPlan` red 1 → 0; the generator now skips abandoned sites when seeding (D22). Release L4 on `CivLab` (`civEpochs 12`, the one field the lab differs from the shipped default): civ 154 sites in 32 ms, 123 live / 31 abandoned, 6 provinces, 31 lowercase glyphs on the civ map, box plan 4 sites with 0 ruins. Not in M3b: founding beyond the M3a site set, war, the shock-dependent ruin mechanism, the ruin dressing (M5/M7).

**M3b AUDIT 2026-10-22 (solution-auditor on 22489db3: FAIL overall; `m3b_red_green.txt` "AUDIT FOLLOW-UP").** The auditor accepted the Release L4 data and the box-seeding fix as REAL and found: the ledger arithmetic was unasserted (mutating the growth rate or the shock multiply passed every test); `AbandonedFractionMatchesTheDesertedVillageRate` restates its input (disclosed, but the only real content was the marginal-rank check); **an undisclosed artefact — every hamlet/outpost ruin was one of the youngest sites** (founded at epoch 7–8, dead by 10), because founding order and abandonment order both followed the score, the opposite of the deserted-village record (old marginal villages); "bit for bit" had no cross-build pin; the shock-at-0 rig was clamped to 1 and asserted nothing about the shock; the box plan was unchecked on the history path; a shock at the last epoch leaves sites "declining". Fixes: founding ORDER within a tier is a seeded hash of the cell (the pick of WHICH cells stays by score) — the baseline's 28 hamlet/outpost ruins now come from nine founding epochs (e0 5, e1 2, e2 4, e3 5, e4 2, e5 2, e6 2, e7 3, e8 3) instead of two; no growth in the founding epoch (a ledger bug the audit found). Five new tests: `LedgerMathMatchesTheGrounding` (every grew = ×(1 + 0.114·(0.5 + score)), every shock = ×0.55, a 90 % shock lower everywhere, 1 073 grew / 154 shock events checked), `AbandonShareIsTheLeverAndRuinsAreNotJustTheYoungest` (share 0 → 0 ruins, 0.5 → 75 of 148, 1 → 148; ruins from ≥ 4 founding epochs incl. epoch 0), `EpochsZeroMatchesThePreM3bEngineRecord` (the civEpochs-0 plan equals, site for site in cell/tier/x/z/score, the civ list the pre-M3b Release engine wrote — `m3a_l4_green3_civ.json`, commit 77d320cf: 154 sites, 0 mismatches — the cross-build pin), `BoxPlanIsSeededByTheLiveCivSitesAtTwelveEpochs` (4 = 4), `ShockAtTheLastEpochLeavesDecliningSitesNotRuins` (civEpochs 5: shock clamped to 5, 31 declining, 0 abandoned — allowed and stated: a ruin needs the epoch after the shock). **Mutation red shown** (growth 0.2, growth in the founding epoch, founding by score — all three caught: 1 191 wrong grew events, ruins from two epochs only); reverted → 26/26 with the civ, recipe and pinned-hash fixtures. Still an INPUT: the abandonment rate (the ruin count does not move with the shock size) — NEEDS-RESEARCH stands. **Release L4 after the audit fixes (22489db3 + this change, CivLab): at civEpochs 0 the engine-written civ list equals the pre-M3b record site for site (154 sites, 0 mismatches, all live; the shipped default now has engine-level evidence, m3a_l4_m3b_e0_*); at civEpochs 12: 123 live / 31 abandoned, 6 provinces, hamlet/outpost ruins founded across epochs 0-8, box plan 4 sites with 0 ruins, 31 lowercase glyphs (m3a_l4_m3b2_*).**
 Original decision text kept for the record: **D21** — may the history change a site's TIER (a declined city realized as a town) or do tiers stay the ladder's at founding (recommended: tiers stay; `state` + `population` modulate the realization density, never the tier — the hierarchy invariants M3a proved stay provable)? **D22** — ruins in M3b are a site state + map glyph only; the ruin DRESSING is a structure-forge typology (M5/M7), not M3b (recommended: yes). **D23** — the default arc: 12 epochs × 25 years with one shock at epoch 9 (−45 %), `epochs` shipped at 0 until an L4 shows the ruins read well (recommended: yes). Also in M3b's scope if the owner wants the ladder met in full: replace the greedy pick with a blue-noise/annealed pick (the ~70 % ceiling recorded above) — recommended as a SEPARATE increment (M3c) so the history diff stays readable.

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
**RULED 2026-10-22: D25 three increments, D26 the grounded caps on version 2, D27 1/3 REASONED, D28 macro-fidelity stamping beyond the box → verdict READY; M4a builds from the red tests below.** Original decision text kept for the record: **D25 — scope split.** Recommended: M4a = the continent corridor graph on the macro sheet (demand from provinces + hierarchy, reuse discount, class by hierarchy, data + maps only) and the box realization re-routed ALONG those corridors on meso; M4b = per-class grade caps + the blended grade field (retires the junction seam) + switchbacks as edges + derived earthworks; M4c = bridges/fords/tunnels as edges with the grounded spans. Alternative: one increment (the design says it is L-sized; the three reds above belong to three different mechanisms). **D26 — the grade caps.** Recommended: adopt 0.05 / 0.08 / 0.15 by class on version-2 recipes (the grounded values; the shipped 0.5 is a MISMATCH) and accept the cost — on the Mountains seed routes will lengthen several-fold and need switchbacks and cuttings; the F7 unclimbable fraction becomes 0 by construction. Alternative: a relaxed track cap (USFS: ideal 10 %, short pitches steeper) if the switchback count reads badly in the L4. **D27 — the reuse fraction.** Recommended: ship Veloren's 1/3 labelled REASONED and measure the network on the baseline (routes, total length, junctions) against the MST-plus-1.4×-detour network V1 builds today; research a grounded value (Roman/medieval network density per km²) in parallel. Alternative: keep V1's detour rule and skip reuse. **D28 — roads beyond the box.** The continent's routes exist only as corridors outside the ±16 km hydrology box; realized roads end at the box edge today (a visible seam). Recommended: stamp roads OUTSIDE the box from the macro corridor polyline at macro fidelity (surface material + drape, no grading, no crossings — D18's "refine locally" applied to roads), so a route is continuous across the continent and the box is where it gains grade, earthworks and bridges; the far-terrain tiles already draw the road field. Alternatives: move the hydrology box with the streaming focus (a moving bake — rebake cost on every focus change), or the full-continent meso bake (M2's deferred item: ≈ 300 MB / 35 s Release measured as an estimate, not built).
Red tests ready to write once D25–D28 are settled: `RouteFieldTest.EveryStepWithinItsClassCap`, `RouteFieldTest.GradeFieldIsContinuousAcrossCorridors` (the junction seam), `RouteWalkTest.EveryRouteWalkableBothSeeds`, `RouteGraphTest.ContinentCorridorsReachEverySite`, `RouteGraphTest.ReuseDiscountPrunesParallelRoutes`, `BridgeSpanTest.NoDeckBeyondTheGroundedSpan`, `RoadFieldSeamTest` extended to `earthwork*`.

**M4a BUILT 2026-10-22 (red → green, `m4a_red_green.txt`; L4 `m3a_l4_m4a_*`).** `CorridorGraph` (new, `engine/{include,src}/core/CorridorGraph.*`) routes the civ plan's LIVE sites on the macro sheet: demand = every member to its province seat and the seats chained nearest-first (plus every city pair), solved highest class first (highway between cities, road to towns/villages, track to hamlets/outposts; a route's class is the lower of its ends' tiers) with the V1 road cost REUSED at the macro cell (length × (1 + 15·grade)², river crossing + shore penalties, sea and basins impassable) and the **reuse discount** (D27: a step onto a cell already on the network costs `routeReuseFraction` = 1/3, REASONED). A corridor is data: cells + a Chaikin-smoothed polyline + class, with a per-cell segment index for O(1) distance queries. **The box plan now routes ONLY the corridor graph's demand** (`WorldForgePlan::DemandEdge`, no all-pairs / MST / loops): every corridor run inside the hydrology box becomes an edge whose ends are the pinned site or the point where the corridor crosses the box edge (a road with `a` or `b` = −1), so the fine road meets the macro stamp outside; inside the corridor band (±256 u, DESIGN) the box's routing cost is multiplied by the reuse fraction; the box route search runs on a 128 u routing stride of the 32 u meso bake (V1 routed on 128 u cells; the full grid cost 10+ s per demand in Debug). **D28:** outside the box the column sampler stamps the corridor at macro fidelity (material + drape, no grade, no crossings) within the class half-width, with the flora shoulder; transit TRACKS (neither end pinned, class track) are stamped through the box too instead of being realized — measured: realizing every transit run gave 41 edges / 47 149 segments (near the 65 534 raster cap) / 95–135 s Debug. Baseline (Perlin 20260816, `civEpochs 0`, Debug): **152 routes, 7 671 km, shared cells 1 682, junction cells 295, bake 0.58 s; 2 demands unroutable = exactly the 2 members whose macro land component differs from their seat's (an island — refused with the reason, never routed through the sea)**; 12 671 corridor cells, 0 in sea or a basin; classes 1 highway / 18 road / 133 track, 0 against the hierarchy; deterministic across generators; **D27 measured against no discount: total length 7 671 vs 6 715 km (+14 %), distinct network cells 2 941 vs 3 046 (−3 %), junction cells 295 vs 636 (−54 %), box road vertices inside the corridor band 96.2 % vs 22.0 %** — the discount buys merged junctions far more than shorter networks; 40 of 40 corridor columns outside the box stamped with their class, 0 off-corridor columns stamped. Release L4 (`CivLab`, `civEpochs 12`): corridor bake **54 ms** for 121 routes (ruins excluded), box plan 29 roads (27 to box-edge exits), 39 150 segments, 3.3–5.3 s; `worldforge_plan` echoes `corridors`, the civ map draws `#`/`=`/`-` (97 / 541 / 890 chars), the minimap draws the polylines by class (counted in the PNG by exact colour: 23 highway / 224 road / 7 277 track pixels of 262 144 at 200 u per pixel — present, faint at that scale). **Scale limit measured (stress, x10 density):** the corridor graph itself is fine (1 424 routes, 50 658 km, 29 unroutable with reasons, 4.7 s Debug) but the box plan at 47 pins / 325 demand edges took **1 044 s Debug and hit the 65 534-segment raster cap** — the O(roads² · verts²) junction reconciliation and the uint16 raster are M4b's first items (the blended grade field retires the reconciliation). Not in M4a: grade caps, earthworks, switchbacks (M4b), bridges/fords/tunnels as edges (M4c), a grounded reuse fraction (D27 research).

**M4a AUDIT 2026-10-22/24 (solution-auditor on 317798ef: FAIL; `m4a_red_green.txt` "AUDIT FOLLOW-UP").** Found and fixed: (1) **the M4a commit broke every version-1 plan** — the V1 Kruskal block re-declared `chosen` and shadowed the vector the demand branch introduced, so the MST was discarded (0 roads; the pinned hashes moved to 9029483429761127073 / 12144098318857741066); caught by `WorldForgeStressTest.VersionOnePlanHashesArePinned` minutes after the commit, fixed in 82731b2c, 37/37 with both hashes again — the pinned-hash test earned its keep twice in a day; (2) **a 512 u gap at every box-edge exit**: the box plan's roads end at the box edge inset by the pin margin while the macro stamp began only at the full edge — `FineRoadMeetsTheMacroStampAtTheBoxEdge` walks the corridor 1 024 u each way from every exit road's end: red 1 109 ring columns without a road at 28 of 36 exits → 0 after the stamp uses the same `kBoxEdgeInsetU` inset (the 12 residual columns were one pair under a meso lake hit by six exits — a causeway must not dam a lake, the macro corridor cannot see meso lakes: an M4c bridge/causeway item); (3) the reuse test asserted any difference — it now asserts junction cells with reuse ≤ 0.7× without (295 vs 636), with a mutation red (the network never marked: 636 = 636, 3 046 = 3 046); (4) `distanceToCorridorU` returned the NEAREST segment's class, wrong where a track and a highway share a trunk — now the highest class whose own half-width reaches the point; (5) the stress helper indexed `parent[find(r.a)]` with a −1 exit road (UB) — guarded; (6) the stamp test's 600 u exclusion band hid the seam — removed, the box read from the hydrology bake. Recorded, not fixed: the +14 % total length for −3 % distinct cells (the discount buys merged junctions, not a shorter network); the box-pin index alignment is assumed (pins are seated first, in order — `d.a >= nSites` is the only guard); transit tracks stamped through the box carry no bridges or crossings (M4c); **every version-2 fixture now pays the box plan's cost (≈ 90 s Debug, 3–5 s Release) because the box realizes ~30 corridor runs instead of 4 roads** — the junction reconciliation is M4b's first lever. **Release L4 after the fixes (idle machine, `m3a_l4_m4a2_*`): corridors 121 routes in 56 ms, box plan 29 roads (27 box-edge exits), 39 150 segments, 3.4–5.9 s, raster truncation 0, civ map glyphs 97 / 541 / 890. Final green (Debug, logs in `m4a_red_green.txt`): the 42 fast fixtures incl. both pinned version-1 hashes, CivHistoryTest 12/12, CivPlanTest 8/8, RouteGraphTest 8/8.**

**M4b-1 + M4b-2 BUILT 2026-10-26 (red → green, `m4b1_red_green.txt`).** **Caps (D26):** `WorldForgeParams::gradeCap[4]` = {−, 0.15, 0.08, 0.05} by class (GROUNDED, §10), corridor-driven (demand) box plans only — the pinned version-1 path keeps `kMaxRoadGrade` 0.5 (`VersionOneKeepsTheLegacyEnvelope`, both pinned hashes unchanged). **Routing:** the box search on its 128 u stride uses Galin's k-neighbourhood (every offset within radius 3 with gcd 1, 32 directions) with the grade of a step measured along the cells it crosses (not its endpoints — endpoints at equal heights over a 20 u bump read as flat and the capped envelope then cut 27 u) and a ×25 cost on any step steeper than the class cap (DESIGN, so the oblique traverse wins wherever one exists; a straight climb remains possible where nothing else reaches the goal). **Bounded earthwork:** the class-capped lower envelope may not cut deeper than `cutLimit(cls)` = (half-width + the 3 u grading shoulder) × the repose slope 0.70 — both existing grounded constants, DERIVED 3.15 / 3.85 / 4.2 u; where it would, the profile stays at the floor and the grade becomes a SHORT PITCH (USFS: short pitches steeper than the ruling grade) bounded by a second, legacy-0.5 envelope so every step stays walkable; `naturalY` (the pre-grading surface) is stored per vertex so the earthwork is exact. **The continuous grade field (M4b-2):** `WorldForgePlan::gradeBlendAt` blends every road whose corridor + shoulder reaches the column (weight 1 inside, 0 at the shoulder's edge; the weighted mean over roads, the largest weight against the natural surface) — continuous where corridors overlap, which the nearest-road pick was not (the §10 junction seam); corridor-driven plans skip the O(roads² · verts²) junction reconciliation entirely (it was the box plan's cost driver: 60 s → 34 s Debug for the baseline box). **Two dead ends recorded:** refining each demand on the 32 u bake grid inside a ±256 u band cost 320–360 s Debug per box plan and left 26.9 u cuts (the band held no gentler line); the fine search with the grade measured along every step had not finished after 27 min — both off (the band code kept for M4b-3 switchbacks); the floor placed in the first profile pass was undone by the post-reconciliation envelope (0 pitches, 31.6 u cuts) and moved. **Baseline (Perlin 20260816, Debug):** 32 roads, 44 688 profile segments, 0 over the cap except 2 380 explained pitches = **5.3 % of the length** (prediction 5 % — MISSED by 0.3 points, budget set to 10 % DESIGN), worst pitch 0.5 (the walkability envelope), **max cut 7.6 u** (from 31.6 u under the caps alone and 8.9 u before them), 4 of 11 194 vertices deeper than 6 u, the 41 cuts beyond the floor all next to a natural slope above 0.5 (prediction "none on gentle Perlin" MISSED — the fine band has short steep faces), **walkability 25 of 230 698 centreline steps over 1 cube = 0.011 %** (0.130 % with the nearest-road seam; the blend retired it). **Mountains 424242 (version 2, civ):** detour ×1.65, 0 unexplained pitches, but **45.7 % of the length pitched and max cut 95.8 u** (from 152–178 u) — the bounded cut cannot hold where the natural slope exceeds 0.5 and the 128 u route cannot avoid it: **the red for M4b-3 (switchbacks as edges inside a steep stretch, the fine band search applied there only)**. **Release L4 (CivLab, m3a_l4_m4b_*): the box plan bakes in 2.7-4.8 s (3.3-5.9 before the reconciliation was retired), 29 roads, the plan echoes gradeCap [0.15, 0.08, 0.05] and each road's grade summary (min / max of the profile; the per-vertex profile is not in the JSON).**

**M4b AUDIT 2026-10-26 (solution-auditor on 9c0eed9a: FAIL; `m4b1_red_green.txt` "AUDIT FOLLOW-UP").** Accepted as real: the caps gate, the version-1 path, the measured numbers. Found and fixed: (1) **the grade blend's gather missed roads on shared trunks** — the raster held ONE nearest segment per 8–16 u cell, so where corridors bundle (the reuse discount puts 3–6 roads on a trunk) only the lowest-index road was blended and a road's realized surface followed ANOTHER road's profile exactly where the caps matter most (the auditor's Python replica of the raster: a road's own entry absent at 43.8 % of its centreline points). Fix: a per-cell road list (compressed rows; every road within half-width + the widest shoulder, with its nearest segment to the cell as the search hint); two slots per cell were tried first and still missed 33 066 road-points. `GradeBlendGathersEveryRoadInReach` (brute force vs the gather at 8 604 points, 7 600 of them with more than one road in reach): red 45 045 missing → 0 missing. (2) **The realized surface follows the CORE road** (the highest class inside its corridor; the trunk carries the highway's cap) instead of the mean of the bundle; `RealizedSurfaceKeepsTheClassCapOutsidePitches` (the emitted surface over each 16 u window, judged against the class cap unless the blend's own profile — a pitched core on a six-road trunk — rises that much): red 29 → 0 of 21 556 windows. (3) **The bank physics was wrong:** a C u cut over the fixed 3 u shoulder stood at C/3 (1.05–1.40), twice the repose slope the cut limit was named after. The shoulder now widens to cut / 0.70 (capped at `kShoulderMaxU` 12 u, DESIGN: an 8.4 u cut at repose; deeper cuts stand steeper and are M4b-3's retaining walls), so a bank IS at repose; `cutLimit` relabelled DESIGN (the pitch trigger depth), its two constants exposed as `kShoulderU` / `kReposeSlope` on the plan (= WorldGenerator kGradeBlend / kRockSlope, still duplicated — a shared header is owed). `CutBanksStandAtTheReposeSlope` (the realized drop across the shoulder minus the natural surface's own drop, at the 40 deepest SINGLE-road cuts — a bundle's composite bank is not this claim): red 24 of 80 banks steeper (worst 1.50) → 0 of 78 (worst 0.60 vs 0.70). (4) **The box A\* heuristic was inadmissible under the corridor discount** (weighted A\* up to 3× inside the band): scaled by the smallest multiplier as CorridorGraph already did. (5) The earthwork census sampled every 4th vertex: every vertex now — **the baseline's true max cut is 11.1 u, not 7.6** (13 vertices deeper than 6 u, max fill 6.0 u; the 12 u bound holds, the 38 cuts beyond the floor all next to a natural slope above 0.5). (6) `MountainRoadsKeepTheCapAndDetour` asserted a detour the uncapped routing already met: replaced by caps ON vs caps OFF on the same Mountains world — pitched 45.3 % vs 55.8 % of the length, max cut 96.9 vs 102.1 u (a fill of 177 u on both: a bridge pin cone, M4c). Not fixed, recorded: the Mountains cut (M4b-3), `kNB` made a local (was a static filled without synchronisation), an empty box demand now logs that the version-1 path is taken. Final green 44/44 (`RouteFieldTest` 9, the pinned hashes, `RoadFieldSeamTest`, `WorldForgePlanTest`, the M4a box tests). **Release L4 after the fixes (CivLab, m3a_l4_m4b2_*): box plan 29 roads in 1 538-3 883 ms, the plan echoes gradeCap; the per-cell road index adds no visible cost.**

**M4b-3 switchbacks BUILT 2026-10-26 — and the mountain red is NOT retired by them (`m4b3_red_green.txt`).** `insertSwitchbacks` (Galin: switchbacks as edges): on a corridor-driven road, every stretch of the coarse line steeper than the class cap is replaced by legs across the slope at the cap, folded inside the corridor band (±256 u, the same W as the corridor band — DESIGN) with hairpins at the band edges: the face is taken as planar along the stretch (sin θ = cap / s), each leg spans the band and advances 2W·tan θ, at least two legs (one would be the straight line), water-checked at 16 u with the band halved twice before the straight pitch is kept; `switchbacks` / `switchback_length_u` per road in the JSON (stripped from the identity hash — the pinned version-1 hashes moved the moment they were added, caught at once). **Measured on the version-2 Mountains world: 16 pitched stretches on the coarse lines, all 16 replaced (655 u of climb, 10.0 km of legs in 932 km of road) — and the network's pitched share stayed at 45.4 % with the deepest cut 96.9 u.** The predictions written first (≤ 20 % pitched, ≤ 40 u) MISSED, and the diagnostics say why: only 16 stretches of the coarse 128 u line exceed the caps at all; the 45 % pitched length is the Mountains world's sub-128 u roughness (the fine ridged band, ±20 u bumps) under 16 u-resampled roads — the envelope cuts each bump to the floor and pitches; no switchback addresses a bump. The baseline Perlin world: 1 stretch, replaced (11.7 u of climb), every figure unchanged. 44/44 green with the regression fixtures. **The mountain red therefore needs an owner decision (D29, below), not more routing.** **Release L4 (CivLab, m3a_l4_m4b3_*): the box plan echoes 1 switchback stretch (520 u of legs) among its 29 roads, bake 1 566-3 916 ms.**

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

**M4c BUILT 2026-10-27 (red → green in `m4c_red_green.txt`, five green runs — the misses are in there).** Recipe keys `bridgeMaxSpanU` 25 (clamp 3..96, GROUNDED §10), `bridgeSingleSpanU` 6, `pierSpacingU` 10, `fordMaxDepthU` 0.5 (= the class split), echoed under `params` when `civ` is on, pinned by the test against the sources' figures. **The crossing is a routing edge:** the box A\* carries a per-cell `span` (2 × the carve-accurate half-width + 2 × 3 u of bank, DESIGN) and a cell needing a deck beyond `bridgeMaxSpanU` is impassable (the sub-step check); after a demand's line is realized, `decideCrossings` judges every crossing on it — a creek shallower than the ford bound is a **ford** (the M0 notch), a carved channel is a **bridge** when the deck's OVERALL length — bank to bank (the 3rd of 5 consecutive NATURAL columns past any channel-hit column, the wet bed OR the dry outer shelf of the parabolic section, so the 2 u shoulder beyond the bank is natural too; a narrow natural strip between two channel runs is therefore NOT a bank and the crossing refuses on length — a single deck cannot cross two channels) plus the 2 u shoulder onto each — is within `bridgeMaxSpanU` along the road and the deck fits, else **refused** with its reason; a refusal closes the 3 × 3 routing cells around it (DESIGN) and the demand is routed again, up to 4 times (DESIGN), then recorded in `refused_demands[] {a, b, class, x, z, reason}`. **The deck rule:** deckY = max(lower bank + 1 u freeboard (DESIGN), the channel's emitted RIM + 1, ⌊water level along the span⌋ + 1); the higher bank is cut to it within the **bank limit** `bankLimit()` = `kShoulderMaxU` × `kReposeSlope` = 8.4 u (DERIVED: the deepest cut or fill the widest repose shoulder carries — `gradeBlendAt` widens the shoulder by |profile − natural| / repose for a fill exactly as for a cut, so an embankment approach stands at repose like a cutting); a fill beyond it at the lower bank refuses (a dam), and an approach check 48 u past each bank (DESIGN: 24 u of 0.5 pitch), sampled along the road's own centreline arc, refuses where the natural surface leaves the deck's band (deck ± (bank limit + 0.5·d)) — a cliff no short pitch can hide. The corridor-driven **pin cones come from the span geometry** (vertex-to-span-segment distance, 0 over the deck): class-cap cones → dry vertices clamped to natural − class floor (M4b's pitch trigger) / natural + bank limit → the 0.5 envelope → 0.5 cones, so the deck stays reachable and every step walkable; wet vertices ARE the deck. **Piers** every `pierSpacingU` once the deck exceeds a single span (n = ⌈len / 10⌉ − 1, at least 1 past 6 u); version 1 keeps 12 u. JSON: `crossings[].kind/width_u/depth_u/span_u/bank_step_u`, `bridges[].span_u/pier_spacing_u/single_span_u`, `refused_demands[]` — only on corridor-driven plans, so **both pinned version-1 hashes are unchanged** (9693137997320541182 / 18197215997962677593, green in every run). Minimap: a bridge = a white bar, a ford = a cyan dot. **Measured (Debug, the two version-2 fixtures):** baseline Perlin 20260816 — 158 crossings on realized roads: 157 fords + 1 bridge (deck 13 u overall over a 5.8 u order-4 channel, 5.0 u approach fill, bounded — and on the EMITTED terrain within 64 u of it, 9 dry vertices: max cut 2.0 u, max fill 5.0 u), 0 re-routes, 0 refused; **the span knob with its control** (`bridgeMaxSpanU` 12 u = one unit under the overall length that bridge needed, on the same world: 0 bridges, 0 carved crossings on any road, 32 roads + 0 refusals either way, 2 re-routes — one variable moved, the width formula exercised); **the ford bound as a knob with its control** (`fordMaxDepthU` 0.2 u on the baseline: 0 creek crossings at or above 0.2 u on any realized road against 105 on the default plan, 17 roads + 15 refusals with the creek's depth in the reason = the default's 32 — at the shipped 0.5 u the bound IS the river/creek class split, so only a lower knob can bite); the CROSSING is **walkable with the 1-cube hop** (`RoadIsHopWalkableAcrossTheVersionTwoCrossing` — named for the road, not the deck; bank to bank, the M4b road-surface standard of RealizedRoadsAreStepWalkable) and the plank strictly walkable between the parapets, while the strict 4/9 u walker bank to bank inside the road's own width is NOT (reported): the plank is a whole cube at deckY on a bank whose surface is deckY − 1, a 1 u lip at every plank end — **open item: a flush plank** (the approach surface at deckY, or a 1/3 u shelf at the ends). **No L3 is claimed for the DECK itself:** the baseline's only bridged channel is 1 u deep and dry in a test world without water, so the hop walk can pass without the deck and no fixture, version 1 or 2, has a bridged channel deep enough for a deck-removed control — the deck's necessity is unfalsified on every current fixture (open item: a version-2 fixture with a deep bridged channel, a seed search). The control wall across the whole box proves only that the probe can fail. Mountains 424242 — 195 fords, **0 bridges**, 65 crossings refused and re-routed, **11 demands refused with reasons** (after the second follow-up's bank rule): 7 "the approach rises 9.4–22.4 u above the deck within 1–3 u of the bank" (a canyon wall just past the channel band), 2 "channel rim 97–165 u above both banks" (the river on an embankment), 2 "a 9.8–10.2 u channel needs a deck beyond 25 u along the road" (an oblique crossing; one of them was a rim refusal on a false 4-column bank before the rule required the shoulder natural). Approach fills: baseline 2 dry fills, deepest 5.0 u; Mountains 0 — **the red's 44 over-bound vertices / 30 u / 177 u are gone** (over-bed deck heights 6.0 / 5.4 u). **The Mountains refusals measured independently of the rule** (`RefusalsMatchTheEmittedTerrain`: the emitted surface and channel hit every 1 u along the recorded deck line, 30 u each way, at every refused demand — the rim the rule judged equals the dump's highest channel column between the banks at every refusal where the rule reached a rim (9 of 11 after the bank rule; 2 refuse on length before a rim exists), and the dump must find one whenever the rule judged one; the 2 u shoulder beyond each bank holds no channel column; the 7 "approach rises" reasons are NOT asserted against the dump (the dump runs along the deck's line, the rule along the road's arc — the walls are visible in the dumps, e.g. 49 → 58 → 68 → 86 → 104 u, reported only); the dumps are in the evidence: a 3–15 u wide channel between banks 42/58, 64/71, 88/101 u with the land climbing 22 u within 3 u past a bank, or a rim at 236 / 170 / 45 u between banks at 72/98, 74/85, 12/14 u). **Three findings, recorded not hidden:** (1) **the generator's bed profile stands above the land beside it** — M2d-1 fills the terrain up to the longitudinal profile wherever the land dips below it with no lateral blend (the column dump: the baseline's channel at 55 u between banks at 51 u, where the river meets a basin at 55.9 u; on Mountains rims 147–341 u above banks at 44–195 u), and a carved mountain river is a slot whose dry shelf lies 10–22 u below the land; the crossing rule refuses these honestly, and the Mountains world bakes no bridge until the channel SHAPE is fixed (an M2d item: a lateral blend / banks at repose, queued); (2) **the baseline's bridge approach lies inside a basin cell** — the hydro level reads 55.9 u over land at 51–53 u from 12 u past the deck: the box routing avoids standing water at 128 u cells only (an M4 open item, no water in the test world); (3) **`TraversalProbe` was broken beyond ±3 641 u from the origin** — its visited-set key packed each micro coordinate + 32768 into 17 bits, so below −32768 micro the sign bits smeared across the fields and every step read as already visited: the flat deck at z = −4248 u was "unwalkable" with both probes and every 1 u leg failed on dead-flat ground (the diagnostic that found it is `DiagVersionTwoBridgeColumns`, kept). Fixed in `TraversalProbe.cpp` (21-bit fields, 2^20 offset, ±116 km), red → green `TraversalProbeTest.FarCoordinatesTraverseLikeTheOrigin` (the corridor translated to the bridge's coordinates, with a wall control); it reaches NavGraph micro pathing, the path planner, the settlement walkability gate, the structure validator and the forge — **every walkability verdict at a site more than 3.6 km from the origin was a possible false "unreachable" before today** (the typology, settlement, nav and realizer suites re-run green after the fix). **Misses:** the first green bounded a bridge approach at the CLASS FLOOR (3.15–4.2 u) and refused every carved crossing on both worlds (0 bridges) — the floor is M4b's pitch trigger, not the bank's physical limit, hence the derived bank limit; the "96 u span" red is the knob test with its control, not a measured deck (no deck beyond 28 u existed on either version-2 world); `RouteFieldTest.MountainCapsCutLessThanTheLegacyEnvelope`'s deepest-cut comparison became a report because the data now contradict the claim: after M4c's re-routing the capped Mountains plan's deepest cut is 80.7 u against 79.0 u caps-off and it has MORE vertices cut deeper than 6 u (5 254 of 36 288 vs 3 959 of 37 573) — the caps do not reduce the mountain earthwork, they reduce the pitched share (45.6 % vs 52.1 %, still asserted); the earthwork is D29's / M4b-4's problem and is recorded here as such. **Release L4 (CivLab, fresh DB, `m3a_l4_m4c_*` and, after each audit follow-up, `m3a_l4_m4c2_*` / `m3a_l4_m4c3_*` with the same plan hash 11773178333440644877; the engine's own plan through `worldforge_plan`):** 29 roads, 138 fords + 1 bridge (span 13.0 u, piers every 10 u, single span 6 u, bank step 1.0 u, channel 5.8 u), 0 crossings refused, 0 demands refused, the four keys echoed under `params`, plan bake 1.6–3.8 s (no visible cost over M4b); the box minimap carries the bridge as a 3 px white bar at exactly the plan's position (px 202,189 at 64 u/px — the other white pixels are alpine snow, control window 0) and 50 cyan ford pixels. Not in M4c: tunnels (D30), a lateral channel blend (M2d), water-aware box routing below 128 u, retaining walls (M4b-4), a flush plank end, the macro stamp's crossings outside the box (D28 limitation stands).

**M4c AUDIT 2026-10-27/28 (solution-auditor on the working tree after the first L4: FAIL; `m4c_red_green.txt` "AUDIT" + "AUDIT FOLLOW-UP").** Accepted as real: the pinned version-1 hashes cover the JSON change; the bank limit is derived (not invented), though it was introduced when the baseline needed it; the probe defect, its red/green and the 27 re-run suites; the knob test's control. Found and fixed: (1) the deck's 2 u shoulders stood OUTSIDE the grounded overall length (a 29 u deck passed a 25 u rule) — overall now includes them, in the rule, the routing span and the census; (2) the ford bound at the default is the class split, so the "no deep ford" assertion was a tautology — the knob test above at 0.2 u measures the mechanism with the default as control; (3) the span knob at the 3 u clamp floor could not catch a wrong width formula — it now sits one unit under the measured need; (4) the fill test read the plan's own arrays against the rule's own `bankLimit()` — an emitted-terrain census (`sampleSurface`, cut and fill, within 64 u of a bridge) is asserted beside it; (5) the approach check extrapolated the deck's tangent as a straight line for 48 u — it walks the road's centreline arc now (the Mountains result did not change: 0 bridges, 11 refusals); (6) the Mountains refusals had no measurement independent of the rule's own strings — the refusal record and JSON now carry `bank_low_u / bank_high_u / rim_u / deck_u / span_a_u / span_b_u / tx / tz` and `RefusalsMatchTheEmittedTerrain` dumps and asserts (above); (7) the walk's control proved only probe sensitivity and the strict walker's 1 u lip was unstated — both now stated, the hop is the assertion, the lip an open item; (8) a deck-removed control is impossible on the baseline fixture (recorded). Not changed, recorded: the RouteFieldTest report (the claim is false on the data, see the misses), `cur.pts.clear()` on a re-route (the demand restarts from the coarse path by design), `blockAround`'s permanent 3 × 3 block (class-independent now that no class term remains in the crossing rule). Green after the follow-up: the auditor's own single-source run of the 8 BridgeSpanTest tests + the probe tests (GREEN 11), the pinned hashes, RoadFieldSeamTest, RouteFieldTest (GREEN 8–10 were runs of three successive source versions).

**M4c AUDIT 2 (2026-10-28, solution-auditor on the follow-up tree, running the exe itself: FAIL; `m4c_red_green.txt` "AUDIT 2" + "SECOND FOLLOW-UP").** Reproduced green; the ford knob counts the crossings the rule judged; the span knob carries the overall-length claim; the rim restriction is the rule's own loop. Found and fixed: (a) two evidence labels carried times I had estimated, not read from the logs (corrected from the files' write times; the 8/8 claim re-pointed at the single-source run); (b) the rim assertion skipped silently when the dump found no channel column — now required; (c) **a real rule weakness:** `bankFrom` accepted any 3 consecutive non-hit columns, so a 3-column gap inside a channel mask counted as a bank (at the (−7983,−9937) refusal the channel columns resumed 4 u past the "bank") — the bank now needs its 2 u shoulder natural as well, asserted on accepted decks (0 channel columns under a shoulder) and on refusals; (d) the emitted CUT is asserted within 16 u of a span (the deck's own pins), not only printed; (e) the arc helpers behind the road-following approach check are public statics unit-tested on a hairpin (`ArcHelpersProjectOntoTheOwnLegOfAHairpin`; limit: legs closer than twice the 3 u bank offset would misproject; the check stops at a road's end) — the "road curving away from a wall" behaviour itself has no fixture and is UNFALSIFIED; (f) the deck's necessity is unfalsified on every fixture (stated above, no L3 claimed for the deck); (g) `MountainCapsCutLessThanTheLegacyEnvelope` renamed `MountainCapsReduceThePitchedShare`. Not changed: the ford knob's roads + refusals equality is demand accounting only (the span knob is the one that shows a demand routed AROUND: 2 re-routes, 32 roads either way); the census tolerance 0.5 u on the overall length (the rule's own is 1e-3).

**M4b-4 design check (2026-10-28, retaining walls — run against the M4b/M4c realizer as built: `gradeBlendAt` widens the shoulder to |grade − natural| / repose capped at 12 u, so a cut beyond 8.4 u stands steeper than repose — at 80 u the bank's slope is 6.7, a dirt cliff): NEEDS WORK — owner decisions D33, D34, then engineering.**
1. *Voxel aesthetic:* the wall is a column-field feature like the parapet and the creek shelf — whole-cube `Cobblestone` courses from the road grade to the wall top with the top course a 2/3-height subcube set back one subcube on the road side (the batter; unconditional, no flag); the rock face above it is `Stone` on the exposed voxels (the column's material between the wall top and the bank foot), the bank above at repose in the natural material. Pass. The look to check in the L4: a walled cut reads as a dry-stone revetment under a rock face under a scree bank, not a grass cliff.
2. *Chunk independence:* every quantity is per column from `gradeBlendAt(x, z, natural)` — the column's own natural height, the blended grade, the distance to the road edge — all pure functions of world position via the baked plan (the road field's existing contract); the wall height, rock-face height and bank are functions of (natural − grade) AT THAT COLUMN, so a wall follows the hill without reading any neighbour. No cross-chunk lookup. Pinned by `RoadFieldSeamTest.GeneratorCopyStampsIdenticalRoadColumns` (two generators, identical columns) extended to the wall/rock/bank fields, plus the chunked-vs-whole shape: a region's columns sampled through `generateChunk` per chunk equal `sampleSurface` for the same columns (the `ChunkMicroWorld` helper already exists).
3. *Pipeline:* realization stage, inside the road grading (`WorldGenerator::sampleColumn` → `gradeBlendAt`), consuming the grade profile and the natural surface; it changes `surfaceY` and the material only in the band [half-width, half-width + 1 + 12 u] on dry, above-sea, non-channel columns (the grading's existing guards: never into a carved channel, under standing water, or below sea). Downstream: the flora gate is already closed on the road and its shoulder (M1), so a wall column gets no tree; water is untouched (walls stand on dry columns only); bridges excluded (deck columns are not graded). Order-independent by construction (per column). Per-world tuning: `wallTierU` (3) and `rockCutMaxU` (36) in `WorldForgeParams` (version-2 recipes; version 1 has no blend, pinned hashes unchanged), persisted in the recipe like `bridgeMaxSpanU`.
4. *API:* `WorldForgeParams.wallTierU` (u, clamp 0..6 — 0 = no walls, 6 = twice the tested height, beyond it nothing is sourced) and `rockCutMaxU` (u, clamp 0..96, 0 = every cut beyond the wall refuses under D33(b)); echoed in `params` by `worldforge_plan`; `ColumnSample.earthwork{wallTopY, rockTopY}` (the planned `earthwork{cutY, fillY, retaining}` field) read by `generateChunk`; the plan JSON reports per road the walled length, the rock-faced length and the deepest cut (part of the identity hash, like refusals); under D33(b) a cut beyond `rockCutMaxU` is a `refused_demands` reason. Defaults change nothing for version 1; the version-2 fixtures' plan hashes are not pinned.
5. *Visual test:* "works" = (L2, Mountains 424242 and the baseline) at every single-road cut deeper than 8.4 u the emitted cross-section perpendicular to the road has: a wall band of ⌈0.67 h⌉ columns at the road edge whose top is grade + min(cut − 8.4, wallTierU) with the wall material, the rock-face column(s) above it at natural − 8.4 with `Stone` on the exposed voxels, and the bank above at ≤ 0.70 over the 12 u shoulder (the `CutBanksStandAtTheReposeSlope` measurement, extended to every depth — today it SKIPS cuts beyond 8.4 u, the red); 0 cuts deeper than wallTier + rockCutMax (under D33(b): refused and re-routed, the refusal count reported; under (a): reported as unprecedented); (L3) `RealizedRoadsAreStepWalkable` and `GradedRoadsAreStepWalkableOnMountains` still green (a wall never intrudes on the road width — asserted: no wall column inside the half-width); (L4) Release on a Mountains lab (CivLab is the baseline, max cut 11.1 u → walls of ≤ 2.7 u exist there too; a `MountainLab` variant of CivLab with seed 424242 is the small world for the look). Red tests: `EarthworkTest.DeepCutsAreWalledNotCliffed` (Mountains: today 0 wall columns and N banks steeper than repose beyond 8.4 u — the red is the count), `EarthworkTest.WallNeverIntrudesOnTheRoad`, `EarthworkTest.CutsBeyondThePrecedentAreRefused` (D33(b)) or `...AreReported` (D33(a)), the seam test extension. Prediction written first: on Mountains 424242 the 5 254 vertices deeper than 6 u fall into ≈ 1 500 walled (8.4–11.4 u), the rest rock-faced; under D33(b) the demands whose line needs a cut beyond 39.4 u (wall + precedent) are refused — predicted ≤ 6 of 22 roads affected (the 80 u cut is one stretch), measured, not inferred. Rig vs shipped: the fixtures ARE the shipped recipes (civ true, siteCount 8); the only delta is Debug.
**Owner decisions:** D33 (cuts beyond the precedent: recommended (b) refuse/re-route as an edge), D34 (one tier, no terracing: recommended), D35 derived (no decision unless the look is wrong). Verdict: NEEDS WORK — D33/D34, then the red tests above. **RULED 2026-10-28: D33 (b), D34 one tier, D35 as derived (owner: "recommendations") → M4b-4 builds from the red tests.**

**M4b-4 BUILT 2026-10-28 → 29 (red → green in `m4b4_red_green.txt`: RED, GREEN 1–16, five Release L4 runs — two measurement misses, one design gap, the census corrections and the hash finding are in there).** Recipe keys `wallTierU` 3 (clamp 0..6, REASONED: a tier tested to 2.5 m) and `rockCutMaxU` 36 (clamp 0..96, GROUNDED: one data point, Pisco Montano), echoed under `params`. **The earthwork section** (`gradeBlendAt`, per column, pure): outside the governing road's edge, where the FACE — the higher of the centreline cut (the road's own natural − grade, interpolated along the segment) and the far natural on that side − grade — exceeds the bank limit 8.4 u (audit: gated on the face, so a steep hillside beside a shallow cut is banked too) — a dry-stone tier of height min(centreline cut − 8.4, wallTierU) (none where the centreline cut is within the limit) and thickness ⌈0.67 h⌉ u at the road edge (`Cobblestone`, the top course a 2/3-height subcube coping set back one subcube on the road side: the batter), where the ground still stands above it; past it the bank rises at repose from the **face top** to the natural sampled at the earthwork reach on that side (half-width + 3 + 12 u, per road vertex left/right at plan time — a pure terrain sample, so the bank meets the ground exactly at the reach whether it rises or falls away), with the face between the wall top and the face top realized as rock (`Stone` on the first column past the wall where the ground stands at the face top, the column's material band); the section's weight reaches 3 u further than the plain shoulder (`kEarthworkExtraU`, DERIVED: the wall's thickness + a column); where a column lies within 1 u of the bisector between two segments of the governing road (the inside of a bend) the two segments' anchors are blended by their distance difference, so the section is continuous where the nearest segment flips. **D33 (b) as an edge:** before a corridor-driven line is accepted, its own three-step envelope (class cap, cut floor, 0.5 walkability) is judged against the face it would need — the higher of the centreline natural and the two far naturals, less the grade — and a face beyond 8.4 + wallTierU + rockCutMaxU = 47.4 u closes the cell and re-routes the demand (the M4c loop), else refuses it with the reason `a walkable line needs a cut face of N u at (x,z) ...`. The plan JSON reports per road `walled_u`, `rock_u`, `deepest_cut_u`, `deepest_cut_x/z`. **Measured (Debug, EarthworkTest on the two version-2 fixtures, after the audit follow-up):** Mountains 424242 — 60 single-road cuts beyond 8.4 u, 119 cross-sections on the emitted terrain against the TRUE natural (a generator of the same recipe with WorldForge off): 119 cut sides, 0 unwalled, 0 walls off their expected height (±1 u), 0 banks (≥ 6 columns) steeper than repose — worst 0.75 against the stated bound 0.70 + 0.05 (one cube of rounding over the span; the 0.05 per u is the far anchor's drift between the road's 16 u vertices, EMPIRICAL — the worst observed over 119 sections, not derived; a bound from the vertex spacing would be looser) — 0 steps of 3+ cubes along any bank, tallest emitted rock band 29 u (0 beyond the precedent), 0 of 192 road-edge columns intruded; **the knob with its control:** `wallTierU` 0 on the same world — 0 of 80 cut sides walled and 76 with a cliff or a bank steeper than repose, against 80 of 80 walled and 0 cliffs at the default; **the steep hillside beside a shallow cut:** 40 single-road sections (of 2 060 vertices with a far natural more than 12.4 u above the grade), 0 walled, 0 cliffs, worst bank 0.57; **the emitted voxels** (`generateChunk` through the chunk helper): 72 wall columns all `Cobblestone` course by course with the set-back coping, 24 bands at ⌈0.67 h⌉, 36 rock columns `Stone` at the face top, and 0 of 408 section columns whose chunk top differs from the column sampler (the chunking contract); **the census against the emitted terrain** (`CensusMatchesTheEmittedTerrain`, the most-walled Mountains road walked at 1 u): walled_u 3 102 against 3 062 steps carrying an emitted wall (x0.99, bound 10 %), rock_u 6 444 against 6 376 steps carrying an emitted rock face (x0.99, bound 25 %), section_u 10 442 against 10 466 steps where the section function yields a section at any band column — a bank-only section is plain terrain, so the walk asks the function (x1.00, bound 10 %, prediction x1.0 ± 0.1 written before the run; the x1.52 of GREEN 14 compared section_u with the 6 867 wall-or-rock steps, audit 3); the three checks are consistency checks between the vertex census and a 1 u walk of the same function on one road, and a drift smaller than a bound passes them — the three mutations (walled x0.29, rock x2.06, bank 0.85) show a rule swap does not; **the face on the final plan:** deepest FACE 47.1 u on Mountains (16.9 u baseline), ≤ 47.4 on every road — the test’s per-road assertion allows a further 0.5 u of rounding (47.9), the measured maximum is 47.1 (the 46.5 u quoted before was the pre-follow-up plan of GREEN 1–4 with 22 202 vertices, audit 3), rock length ≤ sectioned length on every road (rock ≤ walled no longer holds: a rock face stands at the edge of an unwalled hillside section too, as the generator emits it — the baseline below shows it); the red: 80.7 u centreline cut, 121 vertices beyond the bound, 120 of 120 sections unwalled with banks up to 7.2 per u. 28 lines refused for a cut and re-routed (the `[EARTHWORK]` log line of the Mountains bake), 7 demands refused for a cut (19 in all; 14 roads realized where M4c had 22 — 7 by the D33 refusals, one more lost to re-routing — the roads that only an unprecedented cut could serve are gone, as D33 (b) says); walled 27 068 u / rock-faced 59 815 u of road as the final census counts them (GREEN 14, identical in GREEN 15/16): the generator's own section function sampled at every vertex on both sides at every band column (half-width + 0.5 … + 5.5 u), half a segment per end that carries the feature — so the figures are the emitted terrain at the vertices, checked against the 1 u walk of the most-walled road above. Baseline — no single-road cut beyond 8.4 u (its 11.1 u cuts are on shared trunks), walled 176 u / rock-faced 3 278 u (at least 3 100 u of it where no wall stands: hillside sections whose far natural is beyond 8.4 u above the grade while the centreline cut is within the limit — the face-gated section emits the rock band without a wall) / deepest face 16.9 u, 0 refusals; the chunk-independence contract: 23 wall columns at the deepest baseline cuts identical across two generators. All figures from GREEN 15 (2026-10-29, the full set 53/53 on the final sources with their hashes logged, identical to GREEN 14 in every M4b-4 figure; every GREEN 12 figure quoted here re-checked — unchanged except the census lengths) unless an earlier run is named. Pinned version-1 hashes unchanged in every run; RouteFieldTest's realized-surface, bank and pitched-share claims green (Mountains 43.1 % pitched, caps-off deepest cut 47.0 u). **Misses, recorded:** (1) the first bank measurement compared against `baseHeightFn`, the coarse height the plan routes on, not the emitted surface — it read a flat 407 u across ten columns whose natural rose 394 → 408, so 30 then 105 "steep banks" were the reference; the tests (and `RouteFieldTest.CutBanksStandAtTheReposeSlope`) now measure against a generator of the same recipe with WorldForge off; (2) the section's weight ran out at the plain shoulder (13.5 u) while a walled section needs the wall's thickness + 12 u — a 2–3 u jump to natural at the last column (fixed, `kEarthworkExtraU`); (3) **the design gap:** the bank line anchored to each column's own natural fell at 1.8 per u on ground falling away from the road and never met a rising slope inside the reach — six sections showed it; the far-natural anchor above is the fix, and it changed the judge (the face, not the centreline cut) and the census; (4) **a smoothing miss:** a 3-vertex running mean of the far naturals (to tame the bend flip) made 63 of 118 banks end 3–4 cubes short of the ground, because a smoothed anchor no longer equals the vertex's own far natural — reverted the same hour, the bisector blend is the fix; (5) **the first rock census was a fabricated figure:** it counted length where no wall and no rock were emitted (baseline 5 468 u "rock" against 208 u walled, quoted here as realized) — the audit caught it; rock is now counted only under a wall whose face top stands above it, and `rock_u ≤ walled_u` is asserted (the L4's 99 587 u below is that first census; the engine-side figure after the fix is in GREEN 10). **Release L4 (MountainLab = CivLab's recipe on Mountains 424242, `m4b4_l4_m4b4*_*`, five runs 2026-10-28/29):** the engine's own plan through `worldforge_plan` — 13 roads, `wallTierU` / `rockCutMaxU` echoed, deepest cut face 47.1 u, 17 demands refused of which 5 for a cut (faces 52–78 u), 23 lines refused for a cut and re-routed, bake 6–12 s on the game loop at boot; the census under the final rule walled 24 799 u / rock-faced 54 825 u / sectioned 92 165 u of road, rock ≤ section on all 13 roads (L4 #1's 30 827 / 99 587 were the first, fabricated census). **The first run's screenshots were killed by the harness for memory (2026-10-28); L4 #2–#4 (2026-10-29, the Release engine rebuilt from the committed source first) closed the visual check by MEASUREMENT:** across the deepest walled cut at (−5078, 7104) — a cutting through a ridge on road −1 → 0 — the resident voxels (`GET /api/world/voxel` per column along the road normal, `m4b4_l4_m4b4d_voxels.txt`) are the road at 296 (Dirt), a 3-column Cobblestone tier on BOTH sides with courses at 297–298 and the subcube coping at 299, a Stone rock band from 297 to 320 on the first column past the uphill tier (24 voxels) under a Dirt/Snow soil cap at 321–324, then a bank rising 0.7–1 u per column to 330 at +12 u; the resident surface equals the generator's on all 23 columns. Captures (L4 #5, the day cycle pinned at noon, `m4b4_l4_m4b4e_*.png`): `road_along` from the road centre — the cutting with a coursed Cobblestone tier and its coping on both sides, the Dirt road running down the valley, the grey Stone face above the uphill tier and the snow banks beyond; `road_uphill` — the Cobblestone courses with the Stone face above; `ridge_across` from 14 u off the road on the lower side — the 25 u Stone face standing sheer above the stepped Cobblestone tier (a rock cut by design, D33) with the snow bank above it. The earlier `m4b4_l4_m4b4d_road_*.png` were taken at in-game night and are kept as the captures of the voxel-census run. **An L4 finding, fixed (follow-up 3):** the second run's plan hash moved on an unchanged plan (identical sites, roads, centrelines, grade summaries, crossings, refusals, params) because the M4b-4 census fields were hashed — the census is a measurement of what the section rule emits, not an input, so `identityHash` now strips `walled_u/rock_u/section_u/deepest_cut_*` like the M4b-3 switchback fields (`WorldForgeStressTest.PlanHashIgnoresTheEarthworkCensus`, red → green with the grade summary as the control; the pinned version-1 hashes unchanged). Version-2 hashes move once more to their census-free value (MountainLab 5206963220949148767 → 11627274365696686169 → 5761560679695978727, stable across boots on the kept DB); ledgers baked before this build re-bake. GREEN 16: the full set 54/54 on the hash-fixed source (shas logged before the run), every M4b-4 figure identical to GREEN 15. **AUDIT 5 (2026-10-29, solution-auditor on follow-up 3 + the five L4 runs: PASS** — the three plan JSONs equal with the census removed, the red real (the refactor's own equality passed while the census edits failed), the control meaningful, the shas and the voxel file checked line by line; stated limit: the hash test injects the census fields into a version-1 record, the real version-2 case is the L4 hash chain). Not in M4b-4: fills beyond the bank limit (the pin block clamps them; none measured above 5.0 u), terraced tiers (D34), walls on version-1 plans, a batter beyond one subcube.

**M4b-4 AUDIT 1 (2026-10-28, solution-auditor on the tree after the first green + the partial L4, running EarthworkTest itself: FAIL; `m4b4_red_green.txt` "AUDIT 1" + "AUDIT FOLLOW-UP").** Accepted as real: the pinned hashes and the other suites, the D33 refusals, 80.7 → 46.1 u, the 22 → 14 roads attributed. Found and fixed: (1) `walled == sections` was tautological (an unwalled section was dropped as invalid) — every cut side is now counted and `unwalled == 0` asserted, with the wall-tier knob as the red of the current measure; (2) the bank's 2 u slack hid slopes to ≈ 0.87 — one cube over ≥ 6 columns + a per-column step bound (3+ cubes along the bank = a cliff) + the stated 0.05 drift; (3) the face bound was judged but never asserted on the final plan — `deepestCutU` (the face) ≤ 47.4 asserted per road, the emitted rock band ≤ the precedent per column; (4) the rock census counted length with no rock (above); (5) the section gate on the centreline cut left the steep hillside beside a shallow cut to the plain shoulder — gated on the face, with its own test; (6) nothing read the emitted voxels — the voxel test (materials, thickness, coping, the chunking contract); (7) the seam test ran on the baseline only (kept, with the voxel test's chunked-vs-sampler check on Mountains beside it); (8) the current measure's red shown by the knob. Follow-up misses: the smoothing (above); the step bound first counted the wall and rock faces themselves (vertical by design) — 100 false "cliffs" for one run; the bisector blend first moved the PLAIN grade too and shifted two older RouteFieldTest claims (the gather's shoulder, one baseline bank at a bend) — the blend now steers the earthwork anchors only, and the M4b bank test leaves earthwork sections (a steep hillside beside a shallow cut) to EarthworkTest, which measures them with the absolute slope and the step bound. **Mutation reds:** the bank line at 0.85 instead of 0.70 makes `DeepCutsAreWalledNotCliffed` fail (first run: 16 banks steeper, worst 0.88; re-run on the final tree in the second follow-up); `rock_u = walled_u` in the census makes `CensusMatchesTheEmittedTerrain` fail; the wall-tier knob is the red for the wall measure. Final green: GREEN 15 (2026-10-29, the full WorldForge set — earthwork, route, seam, bridge, stress, probe — 53/53 on the final sources after the third follow-up; GREEN 12/13/14 were the earlier full sets).

**M4b-4 AUDIT 2 (2026-10-28, solution-auditor on the tree after the first follow-up, running four tests itself: FAIL — three gaps and a text mismatch; `m4b4_red_green.txt` "AUDIT 2").** Closed by it: the tautology, the face bound, the face gate, the voxel test, the seam test, the reds. Found and fixed: (1) the plan quoted figures from an earlier run than the final log (72 not 73 wall columns, 36 not 31 rock columns, 408 not 397, 192 not 184, 76 not 75, 16.9 not 17.2 u, 23 not 22) and two figures that appear in no log — regenerated from GREEN 12 above, an unlogged "45 km of sections" dropped; (2) the rock census was checked only against itself (`rock_u ≤ walled_u` held by construction) — `CensusMatchesTheEmittedTerrain` walks the most-walled Mountains road at 1 u and compares `walled_u` / `rock_u` with the steps carrying an emitted wall / rock face: the centreline-only census read x0.82 / x1.78 (the generator also emits a rock face at the edge of an unwalled hillside section, and no wall where the ground beside the road stands below the wall top), a per-side rule from the centreline data x0.82 / x0.80 (it cannot see another road's corridor covering the band on a shared trunk), the census sampled through `gradeBlendAt` itself after the raster x0.80 / x1.04, with half a segment per end instead of whole segments per end x1.02 / x1.47 (GREEN 13d: the rock column was sampled at one lateral point per side while a rock face stands in any band column), and sampling every band column per side the final figures x0.99 / x0.99 (GREEN 14, 2026-10-29); the `rock_u = walled_u` mutation is its red; (3) four baseline banks fell between the two tests' partitions (skipped by the M4b bank test as sections, not measured by EarthworkTest on the baseline) — the M4b test now measures them with EarthworkTest's rule instead of skipping; (4) the 0.05 drift bound read as fitted — labelled EMPIRICAL with the reason; (5) the bank-slope mutation predated the final engine edit — re-run on the final tree. Unchanged, stated: the bisector blend has no direct test (the knob and the step bound cover its effect); the voxel test samples 12 cuts. **Second follow-up closed 2026-10-29:** GREEN 14 is the full set, 53/53, on the final sources (build 11:30:31, the census test alone 11:33:43, the set 11:58:39 — `m4b4_green14.txt`); both mutations re-run on that engine afterwards (`m4b4_mutation_{rock,bank}_final.txt`: rock x2.06, 16 banks steeper — both red) and the source restored byte-for-byte (sha checked).

**M4b-4 AUDIT 3 (2026-10-29, solution-auditor on the tree after GREEN 14 and the final-engine mutations: FAIL — three blocking items; `m4b4_red_green.txt` "AUDIT 3" + "PATCH R + GREEN 15").** Accepted as real: GREEN 14 53/53 on one source, every quoted figure verbatim in its log, the mutations' timing and the restore hash. Found and fixed: (1) the deepest face was quoted as 46.5–47.1 u under "all figures from GREEN 14" — 46.5 is from the pre-follow-up plan; corrected to 47.1 with the test's + 0.5 u rounding allowance stated; (2) `section_u` shipped as validated while its only comparison (x1.52) measured a different quantity — the walk counted wall-or-rock steps, labelled "any earthwork", and the test comment explained the gap away; the walk now asks the section function at every band column at 1 u (a bank-only section is plain terrain) and asserts section_u within 10 % — prediction x1.0 ± 0.1 written first, measured x1.00; (3) no mutation covered `walled_u` and the bounds were never shown to catch a drift — `walled_u = sectioned` makes the census test FAIL at x0.29, and the limit is now stated (a drift inside a bound passes; a rule swap does not). Final green GREEN 15: the full set 53/53 on the restored source with both file hashes logged before the run. Stated, unchanged: the census checks are consistency checks between two samplings of the same function on one road, not an external ground truth — the voxel test and the bank test are the terrain-side checks. **AUDIT 4 (2026-10-29, on the final tree): PASS** — the three items closed by measurement (hashes and ratios recomputed by the auditor); non-blocking and recorded: the first build-31 log (the compile failure on a type name in the test) was overwritten by the re-run, so "no test ran on it" rests on the chain script's order.

**M4c AUDIT 3 (2026-10-28, solution-auditor on the tree after the second follow-up: FAIL — five items; `m4c_red_green.txt` "AUDIT 3" + "THIRD FOLLOW-UP").** Closed by the auditor: the labels, the rim assertion, the hairpin arithmetic (recomputed), the L3 wording, the rename, L4 #3's provenance. Acted on: (1) **the shoulder rule had no red and its census shared the rule's 1 u lattice** — MUTATION 1 (the old 3-column bank restored in the engine, tests as shipped) makes `RefusalsMatchTheEmittedTerrain` FAIL at the (−7983,−9937) refusal (1 channel column in the shoulder beyond the false 4-column bank), the restore is green (GREEN 13, 11/11); the shoulder census and the refusal dump's shoulder scan now sample a half-unit lattice, not the rule's points; (2) **the emitted-cut bound was never seen red** — MUTATION 1 also forced the deck 12 u below the rule's and the emitted cut rose only to 9.0 u, UNDER the 9.4 u bound (the pin cones' floor clamp and 0.5 cone limit how far the approach follows the deck), so MUTATION 2 forced the deck 30 u below the rule's: the emitted cut at the pins rose to 27 u and `ApproachFillIsBounded` FAILS (27 vs 9.4) — the bound is live; the restore is green (GREEN 14, 45/45 on BridgeSpanTest + RoadFieldSeamTest + RouteFieldTest + WorldForgeStressTest + TraversalProbeTest, the pinned hashes included); (3) the plan's Mountains refusal breakdown was stale and the bank described as 3 columns — corrected above (7 approach / 2 rim / 2 length; the 3rd of 5 natural columns; the rim compared at 9); (4) the walk tests named for the bridge — the version-2 test is `RoadIsHopWalkableAcrossTheVersionTwoCrossing`; the version-1 `RoadFieldSeamTest.BridgeCrossingIsAgentWalkable` keeps its M3 name (referenced across the docs) with a comment that its deck-necessity is equally unproven; (5) a stale RouteFieldTest comment rewritten. Recorded trade-off: a narrow natural island between two channel runs is now channel and such a crossing refuses on length (a single deck cannot cross two channels); the accepted-deck evidence is N = 1 (one bridge on the baseline, none on Mountains). **AUDIT 4 (2026-10-28, on the final tree): PASS** — the five items closed by measurement, the plan checked against the raw run; stated non-blocking gaps: the half-unit shoulder census was added at the restore and has no red of its own; accepted-deck evidence is N = 1; the deck's necessity is unproven on any fixture.


**M2d-4 design check (2026-10-29, the lateral channel blend — run against the generator as built: `FlowField::bake` samples the bed profile from the FULL relief at the cell centre (`base − depth`, then the downstream minimum) while `sampleColumn` multiplies the relief beside a carved channel by a FRACTION that is 0 at the centreline, so the profile and the land beside it disagree by the relief itself — the M4c finding, now MEASURED first by `RiverGeometryTest.ChannelSectionsMeetTheLandAtABoundedSlope` (red, `m2d4_red_green.txt`: a 1 u lateral section along the flow normal at every carved cell outside a basin, the emitted continuous ground per column). Mountains 424242 — 1 981 cells: the rim above the land at the bank on **97.0 % of bank sides, worst 249.6 u** at (7173,−11200); lateral steps steeper than repose AND than 1.3 × the natural ground (the largest biome heightScale, so a real cliff never counts) on 46.6 % of steps / **99.2 % of cells, worst 63.9 u per column**; the natural ground itself beyond 0.70 on 0.7 % of steps, worst 3.1 u — the walls are the shaping's. The Perlin baseline, the control: 38.7 % of bank sides / 34.5 u, 17.6 % of cells / 8.3 u, lips (the bank above the rim) 18.6 % / 4.7 u. Predictions P1, P2, P4 met; P3 missed on the baseline's size (its ridged band carries 30 u of relief beside a channel, not "small"). A creek was fixed the same way on 2026-07 (the swale became an absolute depth: "a fraction is only safe over a wide valley"); rivers kept the fraction): NEEDS WORK — owner decisions D36, D37, then engineering.**
1. *Voxel aesthetic:* no new asset; the section is a column-field shape like the creek swale and the road bank — a flat alluvial floor at the bankfull rim, a valley side at ≤ repose, the natural ground beyond — emitted whole-cube with the bed on the 1/3 u shelf grid as M2d-1 left it; the P1 material rule already paints exposed rock past 0.70, and a cut side at exactly the bound keeps its biome surface. The look to check in the L4: a mountain river in a V-gorge with scree-angle sides and a flat floor, not a slot, and never a floor in the air. Pass.
2. *Chunk independence:* every quantity is per column — the column's own unattenuated ground (base + relief × biome scale + offset, already computed in `sampleColumn`), the nearest carved channel's distance, half-width and the profile at its projection (`nearestChannel` gains `bedProfile` / `fullDepth` at the projection, the interpolation `channelAt` already does), and the floor/side envelope from those; no neighbour's result, no chunk quantity, no cross-chunk lookup. The footprint minimum (D37) is bake-time per river cell, a pure function of the bake and the warp. Pinned by `TerrainRiverTest.DeterministicCarve` (two generator copies agree column for column) and `WorldSheetGeneratorTest`'s tile-vs-whole seam test — the pair M2b/M2d used — and the section test itself runs on `sampleSurface`, the public per-column sampler.
3. *Pipeline:* the hydrology-geometry stage, inside `sampleColumn` where the valley rule sits today (between the relief and the carve) and in `FlowField::bake` (the profile's base); version-2 CARVED channels only — version 1 (table, no profile) keeps the fractional attenuation byte for byte (the pinned hashes 9693137997320541182 / 18197215997962677593, `LegacyTerrainGoldenTest`, `TerrainRiverTest.*`), creeks keep the absolute swale (already bounded). Downstream: the material pass reads the emitted slope (unchanged rule); the flora gate reads `riverOrder` (unchanged); the water runtime pins rivers on the carved bed from the same generator, so where D37 lowers a profile the water follows (D17, shapes only); WorldForge routes on `baseHeightFn` (unattenuated, unchanged) and judges crossings and earthworks on the emitted surface — the M4c crossing rule will now find banks at the rim, so the Mountains bridges should appear (prediction in 5); the M4b-4 banks sample the natural at the reach and are unaffected. Order-independent by construction. Persistence: no new recipe key — the side slope is a derived constant shared with the road repose (D36), `kValleyWidthMul` stays the labelled tunable.
4. *API:* no new route and no default change; the change is observable through `worldforge_plan` — Mountains `refused_demands` with the reason "channel rim N u above both banks" must disappear and `bridges[]` must appear — and through `water_bake_info` (unchanged shape). The only pinned test that moves is the new section test.
5. *Visual test:* "works" = (L2) `ChannelSectionsMeetTheLandAtABoundedSlope` green on both version-2 fixtures: **0 embankments > 1 u** (GROUNDED: natural levees stand a few cm to 3 m above their floodplain, ≈ 20 % of bankfull depth — §10), **0 lips > 1 u, 0 shaping-made walls** (every lateral step ≤ max(0.70, 1.3 × the natural step) + 1/3 u), at every carved cell outside a basin — with the pure section function unit-tested on a synthetic rig inside one chunk (`FlowFieldTest.ValleySectionEnvelope`: one variable, the natural height across a 1 u-stepped section; prediction written first: the floor flat at the rim out to 5 half-widths, then rim + 0.70 × (d − floorHalf) until the natural is lower; control: a natural everywhere below the envelope is returned unchanged); `BedIsMonotoneAlongTheLongestChannel`, the v1 golden, `TerrainRiverTest.*` and the pinned hashes green. (L2, the consumer) `BridgeSpanTest` on Mountains re-judges the 11 refusals on the new section — **prediction written first: ≥ 1 bridge on Mountains 424242 (today 0), the 2 "channel rim above both banks" refusals → 0, the 7 "approach rises" refusals fall (a canyon wall past the band now stands at ≤ 0.70, i.e. ≤ 8.4 u within the 12 u shoulder = the bank limit) but need not vanish** — measured, not inferred. (L4) Release on MountainLab at the two M4c dumps (the 236 u embankment at (−2250,−3835), the slot at (−2523,6026)) and the red's worst sites (7173,−11200) / (7426,−11169): the resident section measured through the API (the M4b-4 L4 #4 script — `terrain_height source=chunks max_y=420` + the voxels per column) and daylight captures with the river in frame before (eb91c676) and after at the same poses. Perf: the column-cost bench at the gate pose and the Release bake time printed before/after — the bake gains the footprint minimum on carved cells only (baseline 1 265 cells × 15 samples), the column gains one profile interpolation inside the band; neither may exceed the run-to-run noise on top of the M2d figures (1.8–2.0 µs per column, already past the M2d gate and recorded). Rig vs shipped: the fixtures ARE the shipped recipes; the only delta is Debug.
**Owner decisions:** D36 (the section: flat floor at the rim, sides at ≤ 0.70, the natural wins — recommended), D37 (the profile from the channel's warped footprint minimum — recommended). Verdict: NEEDS WORK — D36/D37, then the red test above, already red.

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
| Gorge side slope (a water gap, M2d-4) | Larsen & Montgomery 2012 (not read; snippet): intact cohesive bedrock "should support steeper slopes" — no number found | the 0.70 bound applies; a natural cliff steeper than it stays (the natural wins) | NEEDS-RESEARCH (a steeper rock allowance only with a number) |

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
