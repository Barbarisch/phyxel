# WorldForge — world-scale settlement & road planning

**Status (2026-08-16): M0 + M1 + M2 BUILT and Release-L4-verified** — 37 unit tests
(red-before-green) across `WorldForgePlanTest` / `WorldForgeRecipeTest` / `RoadFieldSeamTest` /
`WorldForgeBuildFlowTest`+`WorldForgeLedgerTest`; live proof on the canonical test world:
roads verified voxel-by-voxel (gravel band scan at the planned centerline) and
`worldforge_build` realized **3/3 sites** (town 7 buildings + 2 villages, one lot failure
surfaced honestly), idempotent re-run, plan hash `5102375980102752933` stable across process
restarts, DB regeneration, and Debug/Release. **M3 (stress axis + TraversalProbe road walk)
remains.** Plan of record: `~/.claude/plans/compressed-twirling-star.md`; user decisions
settled 2026-08-16. ⚠️ Operational: verify builds on **Release** (Debug streams ~7 chunks/min
in forest and a full 48-slot generation queue mimics the recorded pump-death bug); never
teleport the player into unstreamed terrain — use `worldforge_focus`. Worldforge settlements
ship **without residents** in V1 (remote-site residents fall through evicted chunks and are
not persisted — `docs/StructurePipelineGaps.md` 2026-08-16).

WorldForge is the world-scale planning layer that sits between the hydrology bake and
per-column generation: it decides **where settlements go** (scored siting against
relief/water/biome), **how roads connect them** (slope-averse routing on the hydrology
grid), and — via the `worldforge_build` job (M2) — drives the existing
`SettlementBuildService` to realize each site. It fills the P8–P10 territory named in
`docs/structure-generation/StructureGenerationV2.md` at region scale (V1: 3–8 settlements
across a few km²).

## Architecture

```
WorldGenerator::rebuildCoarseModel()
  coarse model → hydrology bake (HydrologyMap/FlowField/WaterBodyIndex)
              → WorldForgePlan bake             engine/src/core/WorldForgePlan.cpp
                 = pure f(worldSeed, WorldForgeParams, heightAt, hydrology, biome probe)
                 immutable, shared_ptr → worker-generator copies share it (m_hydro contract)
        │
        ├─ M1: sampleColumn reads plan->roadAt(x,z) → ColumnSample.roadClass
        │      (the riverOrder pattern): road surface material + flora/fauna gates.
        │      Roads are a pure function of world position — seam-free, exist in
        │      never-visited terrain (WorldRenderV2 P-DERIVED).
        │
        └─ M2: worldforge_build async job → per site: residency focus → grounding →
               SettlementBuildService::plan(seed = site.seed) → ledger checkpoint.
```

Key decisions (user-settled 2026-08-16):
1. **Settlement realization = orchestrated bake job** (lazy on-stream-in is a later phase).
2. **Inter-settlement roads = generation-time field** (never runtime paving between towns).
3. **V1 scale = region** (3–8 settlements; the plan format must scale later).

Requires **`world.streaming: true`** — hydrology and the plan live on the streaming
generator. Heightmap-sourced and Flat worlds have no hydrology bake and therefore no plan
(surfaced as an error; logged gap).

## Params (`WorldForgeParams`, persisted in the world recipe)

Stored in `world.db world_meta["recipe"]` under `"worldforge"`; a **disabled block writes
no key**, so legacy recipe JSON is byte-identical
(`WorldForgeRecipeTest.LegacyRecipeDisabledAndKeyless`). game.json `world.worldforge {…}`
seeds the recipe on first load (presence implies `enabled` unless it says otherwise); the
stored recipe wins on every later load, like all recipe fields.

| Param | Default | Clamp | Meaning |
|---|---|---|---|
| `siteCount` | 5 | 3–8 | settlements to site |
| `regionRadius` | 2048 | 512–8192 | world units from the hydrology-region centre |
| `minSpacing` | 400 | 64–2·radius | hard minimum between site centres |
| `maxSpacing` | 1500 | ≥ minSpacing | soft: farther-than-this candidates score ×0.5 |
| `sitePins` | [] | — | user-pinned centres, seated verbatim first |

Clamps are echoed (the plan JSON carries the clamped params).

### Recipe-seed authority (prerequisite fix, shipped with M0)

`applyRecipe` now **adopts `recipe.seed`** (0 = unowned → keep the constructor seed), and
the loader re-reads `generator.getSeed()` before configuring streaming. Before this,
editing game.json's seed silently re-seeded an existing world — a drift the plan (keyed on
the world seed) could not survive. Pinned by `WorldForgeRecipeTest.RecipeSeedAuthority`
(shown red first).

## Algorithms

**Siting.** Candidates = hydrology cells (128 u production / bake-grid geometry generally)
within `regionRadius` of the region centre. Rejected outright: wet cells, cells below
`seaLevel+2`, cells on an order≥3 channel. Score =
`0.45·relief + 0.30·water + 0.25·biome`:
- *relief*: stddev of the full rendered surface over a 5×5 sample of a village footprint
  window, scored to 0 at sd 8 — the `analyzeSite` footprint-window-relief notion.
- *water*: distance to nearest channel (any order) or water-body bbox; 0.25 inside 40 u
  (flood plain), 1.0 to 300 u, exp decay (scale 400 u) beyond.
- *biome*: resolved surface material (with the generator's physical overrides): Grass*/Dirt
  1.0, Sand/SnowGrass 0.5, Snow/Stone 0.15, other 0.7.

Selection is greedy argmax with hard `minSpacing` / soft `maxSpacing`, deterministic
tie-break by cell index; pins seat first, verbatim. Tiers by score rank: best = town, next
two = villages, rest = hamlets. Non-pinned positions refine on an 8×8 grid of 16 u offsets
minimizing footprint relief (offsets that leave the region, get wet, or violate
`minSpacing` against the other sites' current positions are skipped —
`SitesRespectMinSpacing` caught exactly that drift). Site seed =
`WorldForgePlan::siteSeed(worldSeed, pos)` — the canonical settlement seed (closes the
"settlement seed is caller-supplied" gap; same derivation idiom as structure floorplans).

**Routing.** A* per site pair (≤28 pairs) on the bake-cell grid, 8-connected. Step cost =
`len · (1 + 15·grade)²` (grade = rise/run); standing-water cells impassable; +30 per
order≥3 channel cell (bridge-worthy crossing), +10 per order 1–2 cell (fordable creek),
+5 shore-adjacent (keeps smoothed lines off wet cells). Network = MST over pair path costs
plus any direct edge whose cost beats the tree detour by 1.4× (classic detour-factor
relaxation). Road class = min of endpoint tier ranks. Centerlines: cell path → 2 Chaikin
passes → 16 u resample → trimmed at each endpoint's footprint edge. Crossings: one record
per contiguous run of channel cells along the line, labeled with the run's max order.

**Road query.** `plan->roadAt(x,z)`: O(1) — an 8 u raster over the network bbox stores the
nearest segment index per cell (uint16 + class byte, ~3 B/cell, capped 2048² cells);
exact point-segment distance against that segment ±1 refines. This is the per-column hook
`sampleColumn` uses (M1).

**Per-class road spec** (grounded in `resources/settlement_program.json` street entries):

| class | name | material | width (cubes) | from |
|---|---|---|---|---|
| 1 | track | Dirt | 3 | hamlet (unpaved rural way; width = lane_width) |
| 2 | road | Gravel | 5 | village main street |
| 3 | highway | Cobblestone | 6 | town main street |

## Determinism contract

Same (worldSeed, params, terrain/hydrology inputs) → **byte-identical `toJson()`** and
`planHash()` (FNV-1a over the dump). There is deliberately **no process-wide plan
memoization** (unlike the hydro bake): the bake is ~100 ms and its identity depends on
biome tuning the `HydroBakeKey` cannot see — a wrongly shared cache is a worse failure
than a duplicate bake. This also makes `PlanDeterminism` (two independent generators) a
genuine comparison, not a shared-pointer tautology. During the bake the generator's own
plan pointer is reset, so biome probing sees no roads — the plan cannot feed back into its
own siting.

## API

| Route | Command | Notes |
|---|---|---|
| `POST /api/worldforge/plan` | `worldforge_plan` | pure preview bake; `{}` → the applied plan |
| `POST /api/worldforge/apply` | `worldforge_apply` | persist into the recipe; **refuses** if world.db has saved chunks unless `force:true`; `restart_required:true` — worker generator snapshots only pick the plan up on the next project load |
| `GET /api/worldforge/status` | `worldforge_status` | enabled/applied/hash/counts (+ ledger, M2) |
| `GET /api/worldforge/map?step=4` | `worldforge_map` | ASCII map: `~` water, `r` order≥3 river, `=` road, `T/V/H` sites |

MCP tools of the same names mirror these. All multi-settlement realization (M2) is an
async job — never `queueAndWait` (the 5 s window already loses single terrain-settlement
responses, `docs/StructurePipelineGaps.md`).

## Grounding notes (REASONED items — no direct historical dataset)

- Score weights 0.45/0.30/0.25, relief-sd cap 8, water bell 40/300/400 u: REASONED.
  Relief dominates because unbuildable sites refuse at realization; near-water-not-in-water
  is the classic siting driver.
- Slope aversion 15, crossing penalties 30/10, shore 5: REASONED via the implied detour
  horizon (a few dozen cells ≈ a few km at 128 u cells — bridges are normal; roads prefer
  fewer/narrower crossings).
- Detour factor 1.4: the classic route-directness threshold.
- Hamlet footprint 40×32: REASONED smaller than the smallest L4-tested village (80×48);
  village/town footprints are the L4-tested sizes (80×48, 140×60).
- Tier presets/materials/widths: grounded in `resources/settlement_program.json`.

## Validation (ValidationLedger rows pending)

`tests/core/WorldForgePlanTest.cpp` — L2 invariants on real bake output over a synthetic
dendritic-valley fixture (ocean outlet + inland lake + order-3 stem, self-checked by
`FixtureTerrainIsHydrologicallyViable`); all shown red against a stub bake first.
Determinism, siting invariants (dry/spacing/region/clamp), connectivity (union-find),
roads-avoid-standing-water, continuity, crossings, pins, roadAt, honest degradation
(all-ocean region → 0 sites). `tests/core/WorldForgeRecipeTest.cpp` — recipe round-trip,
legacy byte-identity, seed authority (red first), generator wiring determinism.

## M3 stress results (2026-08-17) — MILESTONE COMPLETE

Live (Release, `WorldForgeStress` project, 8 sites / 2048 u): **8/8 sites built** (town 8
buildings + villages 4 & 10 + five hamlets; 4 lot failures surfaced honestly; 40,953 job
units), idempotent re-run queued 0, live plan hash == the unit-test bake's hash.
**Delete-DB voxel identity:** the canonical world's DB was deleted and regenerated (4th
independent generation); the road cross-section scan matched 2026-08-16 **cell-for-cell (88/88
Gravel columns identical)** — the only delta was 9 corner columns unloaded during the earlier
mid-streaming scan (coverage, not content). Remaining niceties: a full TraversalProbe
agent-walk (width/obstacles) — the 1 u step-profile below covers grade; road-arrival street
orientation (logged gap).

### Stress measurements (`WorldForgeStressTest`)

- **8 sites / 2048 u radius (V1 maximum, canonical seed):** all 8 seat with every pairwise
  invariant intact (spacing/dry/connectivity); tier mix 1 town + 2 villages + 5 hamlets;
  18 roads, bake 285–365 ms (over the 250 ms target, under the 1 s test bound — noted);
  plan hash `8322367767805600407`, byte-identical across independent generators.
- **Mountains world (seed 424242, 8 sites):** seats all 8 honestly on genuinely dry,
  non-river land; 10 roads.
- **Road walkability (draped roads, measured at 1 u steps along every centerline):**
  canonical world **0/1320 steps unclimbable (0.0%)**; mountains world **45/6496 (0.69%)**.
  The slope-averse router does most of the grading gap's work — the gap stands (those 45
  steps are real cliffs a character cannot climb), but V1 roads are ~99.3% step-walkable
  even on mountain terrain. Bounds pinned at 6% / 50% so only real regression fails.

## Bridges — V1 SHIPPED 2026-08-17 (placer #44)

Every order≥3 crossing bakes a `WorldForgeBridgeSpan`: endpoints marched off the
**carve-accurate** channel onto the banks (3 consecutive dry 1 u steps + 2 u shoulder), flat
deck at the higher bank's REAL `surfaceY`. `ColumnSample.bridgeDeckY` makes `generateChunk`
emit a Wood plank deck — the one thing generation ever places above the surface, pure
per-column like the road field. Crossing DETECTION also moved to the warp-accurate
`channelAt` at 2 u steps: the raw `FlowField` cell line sits ~a channel-width off the carved
bed, and 16 u point sampling straddled the ~5 u channel — **both defects caught red** by
`CrossingsGetBridgeSpans` / `BridgeDeckEmittedOverOrder3Channel`. L4: the deck scanned
voxel-by-voxel spanning a carved gorge on the canonical seed (68 Wood cubes, 5 wide, banks
met flush at y=65 over a bed at y≈52) + in-ravine screenshot; `BridgeVis` project pins the
scene. **Note: adding bridges to the plan JSON changes every plan hash** — existing
realization ledgers correctly flag stale on re-run (the designed drift guard).
**Railings + piers SHIPPED 2026-08-20:** deck-EDGE columns (true lateral distance in
`(halfWidth-1, halfWidth]`, span INTERIOR only) raise a 2/3-voxel WoodPlanks subcube
parapet at `deckY+1` (the creek-bed-shelf pattern — sub-voxel per the detail rule); spans
≥ 2× `kPierSpacing` (24 u) get solid Stone piers from the carved bed to under the deck at
interior stations every ~12 u, and pier columns emit NO water span (the pier displaces the
water). Both derived per query in `bridgeAt` — **no baked data changed, plan hashes are
unchanged** (the mountain fixture re-baked to the identical pre-pier hash), so existing
ledgers stay valid. Red-first: `BridgeRailsGuardDeckEdgesAndPiersReachTheBed` (≥80%
edge-rail coverage per side, zero walkway intrusions, physically a 2-subcube-tall shelf
and not a full cube) + `BridgePiersStandSolidOnALongSpan` (solid at EVERY level, mountain
8-site fixture). The walkway-intrusion assertion caught a real defect mid-implementation:
clamped segment distance wrapped the rail band around the span endpoints as an arc — a
parapet ACROSS the bridge entrance; rails now use unclamped lateral distance.
**L3 agent walk + abutment ramps SHIPPED 2026-08-20:** `BridgeCrossingIsAgentWalkable`
(the M3-owed L3) drives a TraversalProbe character-box bank-to-bank over the deck (1-cube
hop, the M3 climbability bound), a STRICT walker (engine auto-step only) end-to-end
between the parapets, and a sensitivity control that overlays a parapet-height wall
across the walkway and must break — proving the test can catch a rail intrusion. Two
emission changes fell out: (1) the deck used clamped segment distance, which grew a
floating ~2.5 u deck DISC beyond each endpoint — decks are now strictly span-interior;
(2) span ends whose bank sits genuinely below the deck get a stepped Stone **abutment
ramp** (1 cube per 2 u, ≤ `kRampLength` 8 u, filled only where the ramp line clears
terrain) so the deck is mountable ALONG THE ROAD LINE — pinned by
`BridgeAbutmentRampStepsTheLowBankUp` on the mountain gorge fixture (red with
kRampLength=0), since the canonical fixture's "low bank" is a one-column lip under the
deck end where no ramp applies. Provenance note, recorded honestly: the crossing test's
first red was a TEST bounds bug (vertical bound below the approach terrain), not the
mount step — the agent crosses even without the ramp by detouring over bank terrain; the
ramp's own test is what pins the feature. Ramps/rails/piers are all query-derived: plan
hashes unchanged throughout (mountain fixture re-baked identical).
Still open (logged): channels wider than 96 u get NO deck (surfaced in the bake log, never
a half-bridge), decks are flat (no arc), parapets have no openings/posts rhythm.

## Road grading — SHIPPED 2026-08-20 (supersedes both the drape gap AND the abutment ramp)

Every road bakes a **slope-limited grade profile** (per centerline vertex): the lower
envelope of the real emitted surface with ascent capped at `kMaxRoadGrade` (0.5/u) in both
walk directions, junction-reconciled (where two corridors overlap, both profiles take the
lower height — the emitted surface is single-valued, so disagreement was a measured 2-cube
step at a mountain junction), then clamped to **two-sided bridge-deck pins**: vertices
between the banks sit exactly at deckY, with a fill cone (max) raising low banks and a cut
cone (min) lowering high rims, both at the grade limit. `sampleColumn` pulls corridor
`surfaceY` to the interpolated profile (3 u shoulder blend; carved channels / below-sea /
standing-water columns never grade) — cut and fill both fall out of moving `surfaceY`
before emission. **The same-day abutment ramp is REMOVED** — deck pins subsume it (once
grading lifts an approach, the ramp condition never fired). Invariant, pinned red-first on
the steepest fixture (`GradedRoadsAreStepWalkableOnMountains`, Mountains 424242 8-site):
consecutive 1 u centerline walk-surface steps (graded ground, or deck over a channel)
never exceed 1 cube — progression 45 tall steps ungraded → 3 (short-span pin miss +
junction disagreement) → 1 (high rim 5 above deck) → **0/6656**. planHash changes (roads
carry a grade summary in toJson): pre-grading ledgers correctly flag stale. **L4 look
(Release, BridgeVis fresh-streamed):** the roadway descends its slope as evenly spaced
1-cube terrace steps between grass-bank shoulders — an engineered cutting, not draped
terrain — `docs/evidence/road_grading_{terraces,cutting}.png`.

## Known gaps (V1 non-goals, logged)

- **Road grading**: roads drape the terrain surface at generation time; no cut/fill.
- **Road-to-street fusion**: roads stop at the settlement footprint edge.
- **Live apply**: `worldforge_apply` is restart-required (worker generator snapshots are
  not refreshed mid-session; a live apply would seam chunks).
- **Roads at distance**: no far-LOD tier shows roads beyond chunk residency (P-DERIVED
  gap; noted for `docs/LodTierLedger.md`).
- **Heightmap/Flat worlds**: no hydrology bake → no plan.
- **Lazy realization on stream-in**: later phase; V1 is the orchestrated build job.
- **Preview feedback**: a `worldforge_plan` preview on a world with an applied plan can
  deviate microscopically in biome scores near existing roads (the biome probe sees the
  stamped road material). Fresh bakes (the flow that matters) reset the plan first and
  cannot self-feed.
