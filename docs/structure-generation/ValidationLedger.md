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
PASS/FAIL **matrix** + a pass rate — the number you sign off on. It carries a NEGATIVE CONTROL
(stairless multi-story → unreachable) so the checks have teeth. **Current: 11/12** (only the control
fails). v1 layers: `build` (L1), `floors` (L2 room-has-floor), `reach` (L3 every floor reachable via
`TraversalProbe`). Later placers ADD cases + layers (furniture/KI-2, finer floor scan, doors, paths…).

> Note: the harness moves several rows' *current* layer up — multi-story **reach (L3) and floor
> continuity (L2) are now corpus-validated**, not single-test. The status (D/P/M) column below is the
> stale placer-spec tag; trust *current layer* + the harness.

---

## Building-scale placers (01–37) — the live set

| # | placer | status | required | current (cite) | score U/S/Sc | gap / red test owed |
|---|--------|:--:|:--:|---|:--:|---|
| 01 | analyze_site | P | L2 | L1 (runs; no test asserts the site facts) | 0/2/1 = **3** | assert grade/slope/water match a known terrain fixture |
| 02 | prepare_pad | M→P | L2 | **L2** (`PlanPadLevelIsMedian` + runtime cut/fill) | 1/2/1 = **4** | ✅ met (median-seat); add a footprint-leveled invariant |
| 03 | place_foundation | P | L2 | L1 (no test) | 1/1/1 = **3** | L1→L2: supports full footprint, correct depth, no gaps |
| 04 | place_floor | P | L2 | **L2** (`FloorIsContinuousOverFootprint`) | 2/2/1 = **5** | mostly met; gate "holed only at the stairwell" |
| 05 | generate_room_layout | M | **L3** | L2-topology (`UnreachableRoomFails`,`OverlappingRoomsFail`,typology) | 2/2/1 = **5** | gen missing; L2→L3 every room agent-navigable, not just graph-linked |
| 06 | place_exterior_walls | P | L2 | **L2** (`WallsAreThinNotFullCube`,`ExteriorWallExists…`) | 1/1/2 = **4** | met-ish; gate no-gap continuity across chunk seams |
| 07 | place_interior_walls | P | L2 | L1 (`NoInternalVoxelOverlap` covers overlap only) | 1/2/1 = **4** | L1→L2: partitions don't *seal* a room (would make it unreachable) |
| 08 | cut_openings | P | L2 | **L2** (`FrontDoorIsCarvedThroughTheWall`) | 2/2/1 = **5** | met-ish; sills/reveals/lintels still P |
| 09 | place_doors | M | **L3** | L2-topology (`ShortDoorFails`,`ExteriorPortalOffPerimeter…`) | 2/2/1 = **5** | **L2→L3: a character-box passes through the opening** (TraversalProbe) |
| 10 | place_windows | M | L1 | L0 | 0/0/1 = **1** | L0→L1: present, correctly placed on a wall |
| 11 | place_ceiling / intermediate_floor | P | L2 | L1/L2 (`CeilingAndRoofExist…`) | 2/2/2 = **6** | inter-story floor continuous + holed at the stair, **per story** |
| 12 | place_stairs | D | **L3** | **L3** ✅ (`AgentCanClimbSwitchbackToTopFloor` + gate + clearance) | 2/2/2 = **6** | ✅ **EXEMPLAR — fully met** (geometry + gate + traversal, auditor PASS) |
| 13 | place_roof | P | L2 | L1 (`HonorsRoofPitchDegree`; KI-1 hover ungated) | 0/0/1 = **1** | L1→L2: eave rests flush on the wall top (KI-1) |
| 14 | place_chimney | M | L1 | L0 | 0/0/0 = **0** | L0→L1 |
| 15 | place_trim | M | L1 | L0 | 0/0/0 = **0** | decorative — L1 |
| 16 | place_furniture | D | L2 | L2-partial (`FurniturePlacerTest` ×4; **KI-2** cross-story overlap ungated) | 1/2/2 = **5** | **per-story floorY invariant: no overlap/stack across stories (KI-2)** |
| 17 | place_fixtures | P | L2 | L1 (via the furniture map) | 1/1/1 = **3** | L1→L2: function-defining fixtures present + non-overlapping |
| 18 | place_lights | M | L1 | L0 | 0/1/1 = **2** | L0→L1: light coverage per room |
| 19 | place_clutter | M | L1 | L0 | 0/0/1 = **1** | decorative |
| 20 | place_entry | P | **L3** | L1 (step logic) | 2/2/0 = **4** | L1→L3: the entry threshold is step-up traversable |
| 21 | zone_parcel | M | L2 | L0 | 1/1/1 = **3** | L0→L2: parcels don't overlap, fit the site |
| 22 | place_fence | M | L2 | L0 | 0/1/1 = **2** | L0→L2: encloses, no gaps a character slips through |
| 23 | place_boundary_wall | M | L2 | L0 | 0/1/1 = **2** | L0→L2 |
| 24 | place_path | M | **L3** | L0 | 2/2/1 = **5** | **L0→L3: walkable path connects entry ↔ gate (TraversalProbe)** |
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
| 36 | stack_stories | M | **L3** | L2 (`StacksMultipleStories`,`TenStoryTower…`; gen hard-codes story 0) | 2/2/2 = **6** | gen missing; reachability of every floor (stair traversal already L3) |
| 37 | place_attic | M | L2 | L0 | 1/1/1 = **3** | L0→L2 (L3 if made accessible) |

## Settlement / Subterranean / Fantasy tiers (38–59) — future, all currently **M / L0**

Validate when each lands; required layer noted so the plan is set up front.

- **Settlement (38–49):** layout placers → **L2** (no overlap, fit): `site_settlement` 38, `subdivide_plots`
  40, `zone_districts` 41, `place_town_wall` 42, `place_public_spaces` 43, `populate_plots` 45,
  `compose_compound` 46. Circulation → **L3** (traversable/connected): `lay_street_network` 39,
  `place_bridges` 44, `link_subterranean` 49. Dressing → **L1**: `place_signage` 47, `dress_street_life` 48.
- **Subterranean (50–57):** traversable spaces → **L3**: `excavate_subterrane` 50, `carve_sewer_network`
  51, `place_crypt` 52, `excavate_dungeon` 53, `place_mine` 54, `connect_underground` 55,
  `place_secret_passages` 56. **57 `validate_crawlability` is itself an L3 validator** (a TraversalProbe
  pass over the dug network) — build it as the gate for this whole tier.
- **Fantasy (58–59):** narrative/dressing, not physical → **L1/data**: `author_world_bible` 58,
  `apply_setting_overlay` 59.

---

## Prioritized backlog (required > current, by score)

The actual work queue — the placers shipping below their required depth, highest-leverage first.
**Multi-story reach (36) + floor continuity (11) are now corpus-green** (harness 11/12), so they drop
off the top; the real frontier is *automatic interiors* and the remaining usability holes:

1. **05 generate_room_layout (5)** — currently rooms are HAND-AUTHORED (gen missing). A town can't be
   hand-authored — this is the biggest end-goal blocker. Build the generator; gate L3 navigable rooms;
   add multi-room cases to the harness.
2. **16 place_furniture (5)** — KI-2: per-story floorY, no cross-story overlap. Add a furniture layer
   to the harness (needs the FurniturePlacer in the loop).
3. **09 place_doors (5)** — L2 → L3: character-box passes the opening (harness reach already exercises
   the ground-floor entrance; extend to interior doors).
4. **24 place_path (5)** — L0 → L3 walkable path entry ↔ gate (parcel scale).
5. **35 place_basement (5)** — L0 → L3 cellar reachable via down-stair (harness already does basement
   substructure; add cellar rooms + a down-stair case).
6. **13 place_roof (KI-1)** — eave-flush L2 (low score: visible, not traversed — cosmetic-correctness).

Done to required depth: **12 place_stairs (L3)**, and multi-story **11/36** circulation (corpus L2+L3).

**12 place_stairs is the worked exemplar** of a row reaching its required layer (L3, red→green, auditor PASS). Every backlog item closes the same way: write the red test at the required layer, watch it fail, fix, green, audit.
