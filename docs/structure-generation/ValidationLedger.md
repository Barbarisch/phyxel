# Validation Ledger — structure-generation placers

The forward-looking map of **what validation each placer owes vs. what it has**. Complements
`BuildKnownIssues.md` (bugs found) — this tracks the *depth of proof* per placer so validation is a
planned, prioritized deliverable, not an afterthought. Standing rule (see `README.md`): a placer is
not "done" until its **current layer ≥ required layer**, proven **red-before-green** and confirmed
by the `solution-auditor`.

## How to read a row

- **Required layer** — set by what the output is *used for* (the ladder below), not by effort.
- **Current layer** — what real validation exists *today*, each cited to a test (so it's auditable).
  `L0` = none.
- **Score** = `USED + SILENT + SCALE` (0–2 each, max 6) — the priority. Higher = validate first/deepest.
  - **USED** 0 decorative · 1 occupied/seen · 2 traversed/operated by a character
  - **SILENT** 0 loud/visible failure · 2 invisible until use
  - **SCALE** 0 fixed · 2 stacks/repeats/crosses a boundary
- **Gap** — the action to close `current → required`, i.e. the red test owed.

### Layer ladder
`L1` artifact exists/right shape (realizer + `occupiedMicro`) · `L2` structural invariant measured on
the real output (no overlap, continuous, fits, clearance — `BuildingProgramValidator`, canvas scans) ·
`L3` functional agent simulation (`TraversalProbe` — a character can *use* it) · `L4` live-engine runtime.

### Build status (from the placer specs): **D** done · **P** partial · **M** not-built.

## The validation harness (the sign-off instrument)

`tests/core/BuildingHarnessTest.cpp` runs the pipeline across a **corpus** (1/2/3/5/10 stories,
small→large footprints, switchback & straight stairs, slab/crawlspace/basement) and prints a per-layer
PASS/FAIL **matrix** + a pass rate — the number you sign off on. It carries NEGATIVE CONTROLS
(stairless multi-story → unreachable; sealed room → unreachable) so the checks have teeth. **Current:
12/14** (only the two controls fail). Layers: `build` (L1), `floors` (L2 room-has-floor), `reach`
(L3 every floor reachable), `rooms` (L3 every room reachable from the entrance — `n/a` for single-room
cases, not a false "ok"). All L3 via `TraversalProbe`. The `rooms` layer (auditor-PASS, red→green)
immediately caught + drove the fix of a real bug: **interior doorways were impassable** (the carve
left a ~1-micro wall sliver) — now fixed. Later placers ADD cases + layers (furniture/KI-2, finer
floor scan, paths…).

> Note: the harness moves several rows' *current* layer up — multi-story **reach (L3) and floor
> continuity (L2) are now corpus-validated**, not single-test. The status (D/P/M) column below is the
> stale placer-spec tag; trust *current layer* + the harness.

---

## Building-scale placers (01–37) — the live set

| # | placer | status | required | current (cite) | score U/S/Sc | gap / red test owed |
|---|--------|:--:|:--:|---|:--:|---|
| 01 | analyze_site | P | L2 | **L2+L4** ✅ (`SiteAnalysisTest`: buildability = footprint RELIEF; classes asserted on synthetic fixtures + **real Perlin/Mountains terrain** (98% vs 60% buildable); relief=0 red-confirmed. **LIVE (L4):** `build_settlement` terrain mode via runtime `groundTopAt` ChunkManager scan — FLAT world `buildable=1.0` vs HILLS `buildable=0.926` (live log), auditor PASS) | 0/2/1 = **3** | ✅ met + glue live |
| 01b | seat_structure (settlement) | P | **L4** | **L4** ✅ (terrain-mode per-building seating `by=groundTopAt(centre)`; **recorded red-before-green** seating invariant `|seatY−ring_median|≤2`: RED `seat_flat` terrain-blind max dev 3.0 FAIL, GREEN max dev 1.0 PASS, same plots; `scripts/verify_terrain_seating.py`, evidence in `scripts/seating_evidence/`) | 1/2/1 = **4** | ✅ floor seats on local ground. Owed (Phase 3): cut/fill flush across footprint (slope corners can clip/float) |
| 02 | prepare_pad | M→P | L2 | **L2** (`PlanPadLevelIsMedian` + runtime cut/fill) | 1/2/1 = **4** | ✅ met (median-seat); add a footprint-leveled invariant |
| 03 | place_foundation | P | L2 | L1 (no test) | 1/1/1 = **3** | L1→L2: supports full footprint, correct depth, no gaps |
| 04 | place_floor | P | L2 | **L2** (`FloorIsContinuousOverFootprint`) | 2/2/1 = **5** | mostly met; gate "holed only at the stairwell" |
| 05 | generate_room_layout | M | **L3** | **L3** ✅ (typology-driven purposed rooms + **NON-RECT winged layouts**; `RoomLayoutTest` tiling/bays/purpose→furniture + `TypologyHouseTraversalTest` (walks service→hall→solar, sealed-door teeth) + **`WingedLayoutTest`: L-plan union is non-rect (notch uncovered), connected, realized L-house walkable wing-to-wing + notch has no floor**, auditor PASS) | 2/2/1 = **5** | ✅ real walkable houses, now incl. **L-plan non-rectangular footprints** (`generateWingedLayout`, `footprint_shape:"L"` → runtime L-house verified: notch empty by voxel-query + screenshot). Remaining: T/U/cross shapes + per-wing gable roof (non-rect roof is flat cap today); multi-story; settlement variety wiring |
| 06 | place_exterior_walls | P | L2 | **L2** (`WallsAreThinNotFullCube`,`ExteriorWallExists…`) | 1/1/2 = **4** | met-ish; gate no-gap continuity across chunk seams |
| 07 | place_interior_walls | P | L2 | L1 (`NoInternalVoxelOverlap` covers overlap only) | 1/2/1 = **4** | L1→L2: partitions don't *seal* a room (would make it unreachable) |
| 08 | cut_openings | P | L2 | **L2** (`FrontDoorIsCarvedThroughTheWall`) | 2/2/1 = **5** | met-ish; sills/reveals/lintels still P |
| 09 | place_doors | M | **L3** | **L3** ✅ (`DoorPlacerTest`: gate-min door walkable + sealed/cat-flap teeth; ties door_too_short to head-room on real voxels, auditor PASS) | 2/2/1 = **5** | ✅ met — character-box passes the carved opening; gate ⟺ physical passability |
| 10 | place_windows | M | L1 | L0 | 0/0/1 = **1** | L0→L1: present, correctly placed on a wall |
| 11 | place_ceiling / intermediate_floor | P | L2 | L1/L2 (`CeilingAndRoofExist…`) | 2/2/2 = **6** | inter-story floor continuous + holed at the stair, **per story** |
| 12 | place_stairs | D | **L3** | **L3** ✅ (`AgentCanClimbSwitchbackToTopFloor` + gate + clearance) | 2/2/2 = **6** | ✅ **EXEMPLAR — fully met** (geometry + gate + traversal, auditor PASS) |
| 13 | place_roof | P | L2 | L1 (`HonorsRoofPitchDegree`; KI-1 hover ungated) | 0/0/1 = **1** | L1→L2: eave rests flush on the wall top (KI-1) |
| 14 | place_chimney | M | L1 | L0 | 0/0/0 = **0** | L0→L1 |
| 15 | place_trim | M | L1 | L0 | 0/0/0 = **0** | decorative — L1 |
| 16 | place_furniture | D | L2 | **L2** ✅ (KI-2 + **footprint-aware**: pieces reserve real cube footprints from `.metrics.json`, fit + no overlap + no doorway-block; red-confirmed door-wall teeth; `footprintFromExtents` pinned; auditor PASS) | 1/2/2 = **5** | ✅ per-story floorY + footprint fit/overlap met. Remaining: scan zero overlap among SPAWNED objects in a real house (integration test); +z-facing assumption documented-not-verified; counter has no `.metrics` (→1×1, feeds 16c) |
| 17 | place_fixtures | P | L2 | L1 (via the furniture map) | 1/1/1 = **3** | L1→L2: function-defining fixtures present + non-overlapping |
| 16a | furniture_asset_coverage | D | L2 | **L2** ✅ (`FurnitureCatalogTest`: every emittable type maps to a loadable template; teeth + on-disk L2 + red-first chest gap, auditor PASS) | 2/2/2 = **6** | ✅ **silent-drop killed** — `FurnitureCatalog` single source + `validateFurnitureCoverage` flags missing assets by room (`asset_gaps`), not a buried skip count |
| 16c | furniture_dim_conformance | D | L2 | **L2** ✅ (`FurnitureConformanceTest`: per-asset dims vs `object_dimensions.json` canon ±tol; no vacuous "ok"; real audit pins all 7 statuses, auditor PASS) | 1/2/1 = **4** | ✅ **regenerate-list** — `checkFurnitureConformance` flags drift/no_metrics/no_canon/no_checkable_dims. Current: 6 of 7 non-conforming ([`AssetConformance.md`](AssetConformance.md)). Remaining: regenerate the assets; +z-axis caveat; runtime/MCP surface |
| 16b | fixture_semantics (session-edit step 1) | D | L2 | **L2** ✅ (`FixtureLabelTest` ordinal red→green + `PlacedObjectMetadataTest` persistence round-trip, auditor PASS) | 2/1/2 = **5** | ✅ each fixture tagged `metadata.fixture`={structure,room,purpose,purpose_index,type,story} + returned in `response.fixtures` — addressable ("2nd bedroom's bed"). Next: 16c `adjust_furniture` intents |
| 18 | place_lights | M | L1 | L0 | 0/1/1 = **2** | L0→L1: light coverage per room |
| 19 | place_clutter | M | L1 | L0 | 0/0/1 = **1** | decorative |
| 20 | place_entry | P | **L3** | L1 (step logic) | 2/2/0 = **4** | L1→L3: the entry threshold is step-up traversable |
| 21 | zone_parcel | M | L2 | **L2** (parcel = plot; yard = plot − building; non-overlap from selectBuildablePlots spacing) | 1/1/1 = **3** | ✅ met-ish via plots; explicit yard region is implicit (the setback ring) |
| 22 | place_fence | M | L2 | **L2+L3 + GROUNDED** ✅ (`planParcelFence`+`ParcelFenceTest`: encloses, gate-only entry, sealed→unreachable teeth. **`planFenceProfile`+`FenceProfileTest`: THIN grounded typed fence from the canon — picket ~0.11 m thick (NOT a 1 m cube wall), 0.9 m tall, posts @1.8 m; picket gaps / privacy solid / post-rail open**, red-before-green; **thinness MEASURED from emitted cell `w`-extent, not the echoed `thickMicro` field** — `ThickProfileMeasuresThick` teeth: a `thickMicro=9` profile measures 9 (>2), so the ≤2 bar is falsifiable; auditor PASS) | 1/1/1 = **3** | ✅ fences are now real: thin picket built from `object_dimensions.json` (fence_picket/privacy/post_rail) as posts+rails+pickets at MICROCUBE res, gate faces the path; wired into `build_settlement` (runtime 12 parcels / 24960 picket µcubes / 8-micro tall, screenshot). Owed: per-parcel type (croft→paling, manor→post-rail); gate precisely on the path edge; fence µcubes add to the render-density (TODO #40) |
| 23 | place_boundary_wall | M | L2 | L0 | 0/1/1 = **2** | L0→L2 |
| 24 | place_path | M | **L3** | 🟡 **L3 proven; L4 visible+mostly-walkable** | 2/2/1 = **5** | `planStraightRamp`/`planSwitchback`/`planSettlementPaths`/**`planTerrainPath`** L3-proven (`PathPlannerTest`+`SettlementPathsTest`; `TerrainPathHugsHillWhereStraightTunnels` tf cut ≤9 vs linear ≥30; teeth+negatives, red-before-green). **`build_settlement` terrain-following grader + LEVEL/FILL paving:** live hills 19/19 edges, **21745 Cobblestone microcubes paved (15758 level caps + 5987 fill), 1454 cut cells (~6%) owed**, stamp 1309 ms — network VISIBLE (screenshot), level/fill cells walkable. **Microcube perf (DEBUG, first density datapoint):** placement ~60 µs/µcube; render ~61 ms/frame (~15 FPS, STUTTER) at 20 subcube buildings + 21745 path µcubes + terrain (10 visible chunks) — RENDER is the bottleneck. Owed: cut removal at the 6% transitions (removeMicrocube); runtime walkability probe over stamped chunks; Release perf + render opt (greedy-merge) before pushing density. |
| 25 | place_garden | M | L1 | L0 | 0/0/1 = **1** | L0→L1 |
| 26 | place_farm | M | L2 | L0 | 1/0/1 = **2** | L0→L2: plots fit, don't overlap structures |
| 27 | place_outbuildings | M | L2 | L0 | 1/1/1 = **3** | L0→L2 (recurse the building gates per outbuilding) |
| 28 | place_livestock_pens | M | L2 | L0 | 0/1/1 = **2** | L0→L2: enclosed |
| 29 | place_yard_props | M | L1 | L0 | 0/0/1 = **1** | decorative |
| 30 | register_systems | P | data | L1 | 0/2/1 = **3** | assert every door/location/light reference resolves (no dangling) |
| 31 | place_fortifications | M | L2 | L0 | 1/1/1 = **3** | L0→L2 |
| 32 | place_graveyard | M | L2 | L0 | 0/0/1 = **1** | L0→L2 layout |
| 33 | apply_seasonal_state | M | L1 | L0 | 0/0/1 = **1** | dressing |
| 34 | excavate_basement | M | L2 | L2 (runtime cellar-dig verify) | 1/1/1 = **3** | encode the runtime check as a unit invariant (void at depth) |
| 35 | place_basement | M | **L3** | L0 | 2/2/1 = **5** | **L0→L3: cellar rooms reachable via the down-stair** |
| 36 | stack_stories | P | **L3** | **L3** ✅ (`autofillRoomLayout` GROWS to typology stories + generates a connecting switchback stair per floor; `TavernUpstairsTest.CharacterWalksFromTaproomUpIntoGuestChamber` walks ground→stair→upper chamber, `WithoutStairUpstairsIsUnreachable` teeth; auditor PASS) | 2/2/2 = **6** | ✅ gen no longer hard-codes story 0 — multi-story is GENERATED + reach-proven. Owed: per-typology story count beyond the inn; gallery upstairs |
| 37 | place_attic | M | L2 | L0 | 1/1/1 = **3** | L0→L2 (L3 if made accessible) |

## Settlement / Subterranean / Fantasy tiers (38–59) — future, all currently **M / L0**

Validate when each lands; required layer noted so the plan is set up front.

- **Settlement (38–49):** layout placers → **L2** (no overlap, fit): `site_settlement` 38,
  **`subdivide_plots` 40 — ✅ L2** (`SettlementLayoutTest`: plots no-overlap + street-gap + fit + min;
  street[] artifact tested; auditor PASS), `zone_districts` 41, `place_town_wall` 42, `place_public_spaces`
  43, **`populate_plots` 45 — ✅ L2** (one building/plot inset by yard, composed-world non-overlap, setback=0
  boundary; auditor PASS), `compose_compound` 46. Circulation → **L3** (traversable/connected):
  `lay_street_network` 39, `place_bridges` 44, `link_subterranean` 49. **Inter-building walkability — ✅ L3**
  (`SettlementTraversalTest`: a TraversalProbe walks the street into EVERY building's interior on a
  composed occupancy; sealed-building teeth; auditor PASS) — a generated settlement is *navigable*, not
  just non-overlapping. Dressing → **L1**: `place_signage` 47, `dress_street_life` 48. **`build_settlement`
  — ✅ engine-side** (`POST /api/settlement/build`: one call drives subdivide+populate, queues a build per
  plot; runtime-built a 4-house hamlet, 0 overlapping bboxes; `RealizerStaysWithinPlotFootprint` encodes
  no-spill; auditor PASS). *OPEN: HTTP-route e2e test; MCP tool; the queued builds run as a synchronous
  next-frame batch (stalls the frame for a big settlement) — spread over frames is a follow-up.*
  **Terrain-aware settlement (the deployer on real terrain) is the next major arc — roadmap +
  per-phase validation in [`TerrainAwareSettlement.md`](TerrainAwareSettlement.md): Phase 0 flat
  stress → Phase 1 `analyze_site` buildability map (keystone) → 2 terrain placement → 3 walkable
  paths → 4 terrain stress.**
- **Subterranean (50–57):** traversable spaces → **L3**: `excavate_subterrane` 50, `carve_sewer_network`
  51, `place_crypt` 52, `excavate_dungeon` 53, `place_mine` 54, `connect_underground` 55,
  `place_secret_passages` 56. **57 `validate_crawlability` is itself an L3 validator** (a TraversalProbe
  pass over the dug network) — build it as the gate for this whole tier.
- **Fantasy (58–59):** narrative/dressing, not physical → **L1/data**: `author_world_bible` 58,
  `apply_setting_overlay` 59.

## Functional typology library (the "various structures" track)

A town is its **functional** buildings, not a scatter of identical houses. Typologies are data in
`resources/room_program.json` (bay-driven, grounded) + a furniture recipe per room *purpose*
(`FurniturePlacer::recipeFor`) + a type→template map (`FurnitureCatalog`). Required depth: **L3**
(interior navigable — a character can enter and reach each room), since these are *used* spaces.

- **Residential (shipped):** croft · longhouse · hall_house · manor_hall — L3 via
  `TypologyHouseTraversalTest` + the harness `rooms` layer.
- **`tavern` — ✅ L3 + GROUNDED + MULTI-STORY** (first non-residential typology): ground-floor public
  **taproom** (2 bays) + kitchen + service/storage end, **+ upstairs guest chambers** (`stories`=2,
  `upper_purpose`=bedchamber). `TavernTypologyTest`: canon grounded + taproom recipe places a **bar**
  (`tavern_bar`/`tavern_table` resolve, coverage gate green) + **L3 `CharacterWalksTaproomToKitchen`**
  with sealed teeth. `TavernUpstairsTest`: **L3 `CharacterWalksFromTaproomUpIntoGuestChamber`** (ground
  taproom → generated stair → guest chamber) + `GuestChambersInterconnect` + `WithoutStairUpstairsIsUnreachable`
  teeth. Dimensions grounding-auditor PASS (room program from the medieval-inn record; 4-bay frame an
  honest DESIGN DECISION by analogy to the grounded hall_house; width_max=7 from Rufford Old Hall;
  stories=2 from the New Inn, Gloucester). solution-auditor PASS.
  - **Silent furniture drop — ✅ FIXED (found by the runtime pass):** the placer capped at ONE piece per
    wall and dropped the overflow with no record ("0 skipped" lied) — the taproom shipped missing its
    bar_stool/bench/candle_stand/fireplace. Now `furnish()` PACKS along walls (multiple per wall) + takes
    an `unplaced` out-param (honest: any no-fit is reported, never dropped); handler returns
    `fixtures_unplaced` + WARN-logs. `PlacementReportTest` ×4 (full-furnish, packs>5, nothing-vanishes
    teeth, back-compat); RUNTIME-confirmed taproom 3→7 fixtures (engine "placed 19", was 15), back_bar now
    adjacent to the bar. solution-auditor PASS.
  - **Furniture conformance — ✅ ALL 16 grounded (0 non-conforming):** regenerated the 5 that drifted —
    `table_wood`/`tavern_table` (were oversized/no-canon), `counter` (had no metrics), `barrel` (no canon
    → grounded 53-gal cask, modern-standard disclosed), `bench_wood` (no checkable dims → added bounding
    canon). `FurnitureConformanceTest` audit went 5→0 non-conforming; both auditors PASS. The taproom's
    tables + kitchen counter are no longer placeholders.
  - **Asset depth (the "necessary extras") — IN PROGRESS:** mugs + bottles ✅ — `mug` (treen tankard,
    period-appropriate) + `bottle` (750 ml, glass flagged mild-anachronism) micro-floor props, grounded
    + conformance **ok**, both auditors PASS. **NEW surface-placement path** `FurniturePlacer::placeSurfaceClutter`
    (distinct in-footprint cells, on the surface TOP, no overflow, deterministic — `SurfaceClutterTest` ×4
    incl. overflow teeth); build_structure handler scatters mugs/bottles on table tops. NOT runtime-verified
    (handler clutter pass not screenshot-checked); latent edge: a table type missing from the footprint map
    falls back to a 1×1 surface (auditor-noted follow-up).
  - lighting fixtures ✅ — `candle_stand`
    (floor candelabra, placed in the taproom now), `wall_lantern`, `chandelier` — deterministic
    MICROCUBE builds with **`glow` emissive flames**, grounded (sconce mount 60-72″, chandelier dia
    ~½ table width cited; period-appropriate — pricket stands/horn lanterns/candle coronas), all three
    conformance **ok**, both auditors PASS. **CAVEAT: these are emissive FIXTURES (glowing objects), NOT
    room illumination** — `glow` self-lights only. Actual point-light lighting (place_lights #18, the
    32-light `MAX_POINT_LIGHTS` budget) is an unbuilt systems follow-up. wall_lantern/chandelier are
    cataloged assets awaiting wall/ceiling placement.
  - bar + stools ✅ — deterministic MICROCUBE
    builds (`tools/regen_furniture.py` gen_bar/gen_back_bar/gen_bar_stool, conformant by construction,
    `.metrics.json` from emitted bounds): **`tavern_bar`** (toe-kick counter + overhang + Log rail,
    1.11 m / 42″), **`back_bar`** (3 shelves of Glass bottles — "shelves behind & above"), **`bar_stool`**
    (tall backless + footrest + seat anchor, 0.78 m / 30″). Grounded in `object_dimensions.json`
    (ergonomic heights cited; honestly POST-MEDIEVAL/fantasy-tavern); `FurnitureConformanceTest` pins all
    three **ok**; taproom recipe now places the full bar ensemble. Both auditors PASS. **NOT yet
    runtime-screenshot-verified** ("good looking" is a visual claim). Materials: none new (Wood/Log/Glass);
    mugs/tankards may want Pewter/Ceramic later (recorded, not made).
  - **Owed:** runtime visual check of the taproom; mugs/bottles-as-clutter, lighting (lanterns + point
    lights), varied tables/chairs, private party rooms; per-asset L2 placement test (bar against wall, stools
    fronting it, no nav block); add `tavern` to the `build_settlement` typology palette (#38);
    gallery/corridor upstairs (today the landing is room 0 of a linear plan).
- **Owed typologies (the work ahead):** smithy/forge (anvil+furnace fixtures), market/shop, temple/
  shrine, well/fountain, barn/stable, town hall, mill, gatehouse. Each: grounded canon + room-purpose
  recipe + L3 interior nav, red-before-green, both auditors.

---

## Prioritized backlog (required > current, by score)

The actual work queue — the placers shipping below their required depth, highest-leverage first.
**Multi-story reach (36) + floor continuity (11) are now corpus-green** (harness 11/12), so they drop
off the top; the real frontier is *automatic interiors* and the remaining usability holes:

1. **24 place_path (5)** — L0 → L3 walkable path entry ↔ gate (parcel scale).
2. **35 place_basement (5)** — L0 → L3 cellar reachable via down-stair (harness already does basement
   substructure; add cellar rooms + a down-stair case).
3. **13 place_roof (KI-1)** — eave-flush L2 (low score: visible, not traversed — cosmetic-correctness).

Done to required depth: **12 place_stairs (L3)**; **09 place_doors (L3)** — `DoorPlacerTest` proves a
character-box crosses the carved opening on real voxels and ties `door_too_short` to physical head-room
(sealed + cat-flap negative controls, auditor PASS); **16 place_furniture (L2)** — KI-2 per-story
floorY; **16a furniture_asset_coverage (L2)** — `FurnitureCatalog` single source + `validateFurnitureCoverage`
flags any furniture a room needs but the catalog can't supply (mapped + loadable), named by room; killed
the silent `chest` drop (red→green, auditor PASS). *Remaining: the v1 `StructureGenerator.cpp` fixture map
+ the spec-path fixtures are a SEPARATE drifting copy not yet under the gate; no runtime e2e forces an
`asset_gaps` response.* multi-story **11/36** circulation (corpus L2+L3);
**05 generate_room_layout** — BSP generator BUILT + harness-validated (tiling, navigable rooms, L3
`rooms` layer with a generated negative control, auditor-PASS) AND WIRED into the build handler via
`autofillRoomLayout` (empty-rooms stories auto-fill; unit + harness enforced, auditor-PASS). *Remaining
for 05: typology-aware autofill (footprint must match the declared typology, else the gate warns
`footprint_too_wide`); multi-story autofill doesn't auto-add stairs; the Application.cpp call site is
runtime-observed only — an e2e test driving the build command is a noted gap.*

**12 place_stairs is the worked exemplar** of a row reaching its required layer (L3, red→green, auditor PASS). Every backlog item closes the same way: write the red test at the required layer, watch it fail, fix, green, audit.
