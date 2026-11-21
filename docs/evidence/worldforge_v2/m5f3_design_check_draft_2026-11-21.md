# M5f-3 design check — DRAFT (2026-11-21, against the tree at b80b3856)

A proposal, not a ruling. Produced by a read-only planning agent (nothing built, nothing run); condensed here by the
main session. Row ids (I1, T7, S1 …) are `era_reference/roman.md`; "R-A" is section A of
`m5f_insula_research_2026-11-21.txt`. The agent numbered its decisions D74–D80; D74 was taken the same day by the
NPC-drop question, so they are **D75–D81** here and in the plan.

**Verdict:** READY for M5f-3a (a Roman TOWN tier on a GRID, populated by the shipped strip building). READY after it
for M5f-3b (two forge capabilities, fixture-falsified, no Roman number). NEEDS WORK for M5f-3c (the taberna as data).
M5f-4 (the multi-storey insula) stays BLOCKED.

## 1. The code as built (read, not run)

| # | Finding | Where |
|---|---|---|
| 1 | Morphology is a free string: stored unvalidated; the planner branches only on `== "cluster"`, anything else is grown as a street. A typo silently plans a main street. | `SettlementProgram.cpp:62`; `SettlementMapPlanner.cpp:1580-1584` |
| 2 | The build service has its own switch (`mainStreetMode = semi_organic or main_street`); a new value falls out of the street pipeline. | `SettlementBuildService.cpp:417-418` |
| 3 | The shipping planner lays one routed spine, optionally through-middle, a market, and for `semi_organic` ONE cross axis. Every further way is a per-lot lane or path. | `SettlementMapPlanner.cpp:394-510, 596-605, 759-821` |
| 4 | `blocks.min/max` is parsed but used only by the legacy control layout. There is no grid and no block in the shipping path. | `SettlementProgram.cpp:123-124`; `SettlementLayout.cpp:408-429` |
| 5 | A way's band is a disc of radius `lround(width)/2`: odd widths are exact (5 paints 5, 9 paints 9; 6 paints 7). | `SettlementMapPlanner.cpp:355, 376-377` |
| 6 | Zero-gap rows already exist as data (`plot.side_gap`, setback). | `SettlementMapPlanner.cpp:65, 711`; `settlement_program.json:229-238` |
| 7 | No wall shared between two BUILDINGS exists: one lot = one footprint = one forge call. | `SettlementBuildService.cpp:1430-1445` |
| 8 | Exterior walls are not 1 m: thickness is style data in fractional cubes (roman_strip 0.222 exterior, 0.556 plinth). | `StructureRealizer.cpp:174, 262`; `structure_styles.json:147` |
| 9 | Storey height is a literal 3 cubes on every settlement build; the room program has no storey-height field. | `SettlementBuildService.cpp:1408, 1437`; `StructureRealizer.cpp:244` |
| 10 | Every auto-placed exterior door is 1 wide, 2 high (literals, five sites). | `RoomLayout.cpp:120, 131, 198, 213, 315` |
| 11 | Multi-storey exists (inn gallery plan or `single`), stairs in a fixed 2-cube well; one exterior entrance rule per typology. | `RoomLayout.cpp:23-41, 536-629` |
| 12 | Site rects: a roman entry exists for the hamlet only; a test PINS that no roman town tier exists. | `WorldForgePlan.cpp:86-98`; `SettlementMapTest.cpp:2396-2402` |

## 2. Order

| Step | What | Verdict |
|---|---|---|
| M5f-3a | Roman `town` tier, morphology `grid`, typology `strip_building` (shipped) | READY |
| M5f-3b | The forge reads exterior-opening width and storey height from the typology (fixture era) | READY after 3a |
| M5f-3c | `taberna` typology as data, abutting row | NEEDS WORK (D77, D78, re-read T1 / T2 / T7) |
| M5f-4 | Multi-storey insula | BLOCKED |

Why the grid first, with strip buildings: D39 ruled the insula because its grid exercises the planner's non-organic
branch — and that branch does not exist (findings 3-4); every grid input is page-read; strip buildings ARE attested
inside gridded provincial towns (Augusta Raurica Insula 50, Ladenburg, Wimpfen — the shipped typology's own sources),
whereas nothing read places an insula in a small provincial town; no new building means a red can only be the planner.

## 3. M5f-3a — the Roman town on a grid

A new morphology VALUE (`grid`) reusing the spine + crossroads branch, plus one step: streets parallel to each axis at
every `block + street width`, routed edge to edge and registered as ways before the growth. Two code touch points
(`planGrowth` after `:605`; the service switch at `:417-418`). An unknown morphology string must REFUSE.

Contract (flat rig, 8 seeds): exactly 2 primary ways at a right angle; every other pre-growth way axis-aligned and
straight; centre-line spacing between parallel streets == block + street width; 0 lanes or paths; every lot inside one
block; lots >= `buildings.min`. On terrain streets may bend round water; deviation recorded, not asserted.

| Field | Value | Label and source |
|---|---|---|
| `morphology` | `grid` | GROUNDED as a form (Timgad, Carthage, Silchester — R-A; Ostia castrum, F10) |
| `blocks` | 40 x 40 | DESIGN pick inside a GROUNDED range 20.45 m (Timgad) to 75.72 m (Pompeii VIII.4). Nearest read value Silchester Insula IV module 39.83 m — re-open the footnote: whether the module includes the street is not stated. Alternative 36 (Carthage 35.28 m). |
| `street.main_width` | 9 | DESIGN inside GROUNDED 8.5-9.8 m (S1, S2, S3). Italian-city evidence, not provincial — stated. |
| grid streets | 5 | DESIGN inside GROUNDED 3.0-6.1 m (S1); Via dei Teatri 6.11 m |
| `street.material` | Cobblestone | DESIGN stand-in (S7: polygonal basalt at Pompeii) |
| `plot.depth` | 20 | DESIGN: half a block. Departs from the hamlet's grounded 33-40 and Wimpfen's 50-65 m — stated. |
| side gap / setback | 0 / 1 | as the roman hamlet (DESIGN) |
| pavement / kerb | NOT BUILT | S5 vs S6 conflict unresolved |
| site rect | derived by measurement | as M5f-2b: the smallest rect seating the band on 20 flat seeds x 1.25 (REASONED) |

Red tests: `SettlementMapTest.ARomanTownIsPlannedOnAGrid` (predicted red: primaries 1, grid streets 0, lanes > 0);
`SettlementProgramTest.AnUnknownMorphologyIsRefused` (predicted: a plan for `"grdi"`);
`SettlementMapLiveSiteTest.ARomanTownIsBuiltOnItsGrid` (predicted red at the service switch). Controls: the same data
with `main_street` reads NOT a grid; medieval city and roman hamlet lines identical by diff. Pins that move: only
`SettlementMapTest.cpp:2399-2402`, deliberately. No plan hash moves. Validation L2 + L3; an L4 look owed.

Risks: the router does not promise straight lines off flat ground; the site rect may collide with the world plan's site
spacing; lots on a block's un-fronted faces may turn side-on; bake time on a larger site.

## 4. M5f-3b — the forge reads opening width and storey height (fixture only)

A typology may declare `entrance_width` and `story_height` (whole cubes); absent = today's 1 and 3, byte-identical. The
door the planner predicts and the door the forge builds must be the same cells. Red:
`PeriodAxisTest.ATypologysOpeningAndStoreyHeightAreBuilt` (fixture width 3, height 4; predicted "opening 1 wide, storey
3"). Control: the medieval tavern voxel-identical before and after. No number enters shipped data. Risk: the stair
planner assumes about a 3 m rise.

## 5. M5f-3c — the taberna row (NEEDS WORK)

One shop = one lot; a row is emergent. `bays 1`, `bay_length 6`, width 4-5, one room, `entrance_width 3`.

| Number | Value | Label and source |
|---|---|---|
| depth | 6 | GROUNDED range 5.8-6.7 m (T1, T2), DESIGN quantisation |
| width | 4-5 | GROUNDED range 3.77-5.43 m (T1, T2) |
| opening width | 3 | GROUNDED: mean 2.93 m (T7) |
| opening height | 2 | NO SOURCE |
| storey height | 3 or 4 | 4 = GROUNDED proxy (I12, I13); 3 = the standing departure |
| exterior wall | 0.444 | GROUNDED as a legal cap (I20, Vitruvius), REASONED as applied to a shop |
| roof | — | NO SOURCE (I36) |

Preconditions: T1, T2 and T7 came through truncated quotes and must be re-opened before a number is typed. The
mezzanine is reached by a ladder (T13): not walkable, so NOT BUILT. Nothing read places a taberna row in a provincial
small town.

## 6. Party walls — no new mechanism

Abutting rows are data already; walls are style thickness; 0.444 (Vitruvius) is representable. Both forms (shared wall;
no party walls) are recorded in one Ostian block. A true shared wall stays unbuilt until an insula needs it.

## 7. The insula — BLOCKED

Unsourced and form-defining: storey height above the first floor (I15), stair width (I29), medianum and flat plan
(I30), courtyard (I33), roof (I36), upper windows (I26), canonical footprints (I8), any insula in a small provincial
town. Forge limits: one entrance rule, inn-plan upper floors, fixed stair well, 3-cube storeys. A labelled-DESIGN shell
is allowed by the letter of the labelling rule and refused by D68; recommendation: wait (Packer 1971, Hermansen 1982,
DeLaine, Stevens 2005).

## 8. Owner decisions — all PROPOSED

| Id | Decision | Options | Recommendation |
|---|---|---|---|
| D75 | The next Roman increment is a TOWN on a grid built of strip buildings; `grid` is a new morphology value; an unknown morphology refuses | (a) as stated; (b) wait for a taberna or insula | (a) |
| D76 | Grid numbers | (a) block 40, main 9, grid streets 5, no pavement; (b) block 36; (c) provincial widths 7 / 3; (d) rectangular 36 x 72 with 36-deep plots | (a), after re-opening the Silchester footnote |
| D77 | Italian (Ostia / Pompeii) shop evidence in the roman town, labelled PROXY: ITALIAN | (a) allow, labelled; (b) keep the town provincial, tabernae in a later Italian tier | a taste and honesty call: (a) only if a mixed-region era is acceptable, else (b) |
| D78 | The taberna's roof, which no source gives | (a) flat, labelled NO SOURCE; (b) the strip building's gable per shop; (c) block until sourced | (c) strictly; decide after a look at a 3-shop fixture |
| D79 | The forge takes `entrance_width` and `story_height` from the typology; the mezzanine is not built | (a) as stated; (b) keep 3-cube storeys | (a) |
| D80 | No shared-wall mechanism; rows are abutting buildings | (a) as stated; (b) build a shared wall now | (a) |
| D81 | The insula stays blocked; no DESIGN-labelled insula ships | (a) wait; (b) a DESIGN 16 x 24, 4-storey shell | (a) |

Proposed open item O41: morphology strings are unvalidated in the loader and switched on separately in the planner
and the build service.

## 9. Not verified in this pass

Nothing built or run. Not checked: how the realizer paves a way of a new highway kind; whether a 1-bay, 1-room typology
passes the forge's program gate; the Stöger and Britannia sentences behind section 6 and the 39.83 m module.
