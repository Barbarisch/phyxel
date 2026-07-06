# Finish & Detail Plan — applying the tree-forge lessons to structures

_Status: PLAN (2026-07-05). User-driven: structures read blocky/full-cube across sessions while
trees now read fine-grained. This plan translates what made tree_forge succeed into the
structure pipeline. Nothing here is implemented yet._

## Diagnosis — where the "everything is full cubes" feeling actually comes from

Audit of the current realizer (2026-07-05):

- **The bulk geometry is NOT the problem.** `StructureRealizer` already paints in micro space:
  exterior walls are micro-thick slabs (style-driven thickness), floors/ceilings are micro-thin
  slabs, the vernacular roofs step at subcube (3-micro) bands. `MicroCanvas` is the C++ twin of
  the tree pipeline's `forge_core` (micro canvas + greedy coarsening export + `ResolutionReport`).
- **The FINISH layer was designed but never built.** That's the blockiness:
  - Openings are carved as raw full-cube holes — `StructureRealizer.cpp:207` fills 9-micro boxes
    to air. No jambs, no lintel, no sill, no threshold, no reveals. Placer #8 `cut_openings` is
    marked **P (gaps only)** in StructureGenerationPlacers.md.
  - Placer #15 `place_trim` (baseboards, casings, quoins, exposed framing, string courses) is
    specced in the placer table and the quality checklist (C5, D5, D8, E7, H1, H6, H7) but has
    **no implementation**.
  - `MicroCanvas::chamferEdge` exists with **zero callers** ("course/frame detailers arrive with
    the P3 openings/finish pass" — P3 never arrived).
  - Facades are featureless cube-aligned planes: no plinth course, no corner quoins, no eave
    cornice, no story articulation. A 1 m² texture tile grid on a flat plane reads "Minecraft"
    regardless of how thin the wall slab is.
- Out of scope but noted: the BlockSmith `build_building` path and the `City` worldgen type are
  separate, genuinely full-cube sources; they are not fixed by this plan.

## The tree-forge lessons, translated

| Tree lesson (proven 2026-07-05) | Structure translation |
|---|---|
| Resolution is per-VOXEL by visual need: coarse interior, fine surface shell (+2.4% prims) | Mass stays cube-aligned (greedy-merged, cheap); every visible EDGE and FEATURE — opening perimeters, corners, courses, eaves — carries sub/micro finish |
| **Detail by default** — no flags, no "hero tier"; the old `round_trunk` flag was deleted | The finish pass runs UNCONDITIONALLY in every realize; there is no "high-detail" option to forget |
| Red-before-green with structural invariants on real output (`test_thick_trunk_has_subvoxel_shell`) | L2 finish gates measured on the real `MicroCanvas` (below), shown failing on today's output first |
| Perf economics measured, not assumed (shell = +2.4%; giants = +40k faces, flagged) | Trim is sparse (edges/features, not surfaces); measure S/M face growth per building vs the 412k-face tavern datum; budget it. Greedy-merge (#40) later raises the ceiling for free |
| One shared substrate (`forge_core`) with reusable primitives | Grow `MicroCanvas` a small DETAILER vocabulary all placers share (below); `chamferEdge` finally earns its keep |

## Plan (phased; each phase = red test → implement → stress → auditors, per standing rules)

### P1 — finish `cut_openings` (placer 8: P → full) — **CORE DONE 2026-07-05**
Shipped red-before-green: `tests/core/FinishForgeTest.cpp` (jamb/lintel/proud-sill invariants on
the real canvas, shown failing 7/7 against the raw-hole realizer, + a clear-passage guard) →
framing implemented in `StructureRealizer`'s carve pass (exterior doors + windows; jamb 1 micro,
lintel 2 micro, window sill ledge 1 micro proud of the facade; trim material = style `trim`
layer with a contrast fallback so frames read against the wall). All 49 structure tests green;
`FrontDoorIsCarvedThroughTheWall` re-pinned to the clear span. Grounding: GroundingGaps #8
(frame stock stylized on the 1/9-m grid; door CLEAR width 778 mm genuinely matches real 762-813
mm). **2026-07-06 update:** GroundingGaps #8 re-classified by a grounding-auditor pass — the 1-micro
jamb is GROUNDED for humble vernacular styles (the 38–50 mm comparison was the wrong reference
class); see `TrimGrounding.md`. REMAINING in P1: reveals + thresholds as distinct elements,
exterior arches.

**L4 CLOSED 2026-07-06 (this machine, StructGenTest --project world, pristine DB):**
`POST /api/settlement/build {"position":{"x":0,"y":16,"z":0},"width":52,"depth":36}` →
success, 4 buildings (longhouse ×3 in stone_keep/stone_manor/timber_cottage + croft in
stone_manor), houses_1–4 registered, furniture 8/8/8/5 fixtures 0 skipped. Evidence captured:
- **Framed doors visually confirmed** on house_4 (stone surround: contrasting jambs + lintel,
  clear passage to interior — `screenshots/screenshot_20260706_102911_878.png`) and house_1
  (thick-wall stone reveal — `..._102940_610.png`); house_3's wood-framed door confirmed by
  micro-scan (door cells Wood:33/36 = jamb+threshold remnant vs raw-hole 0; head cell 148 vs
  wall 162 = lintel band).
- **Tavern** (`POST /api/structure/build` v2, house_5): framed entry door (same 36/148
  signature), **hanging sign physically proud of the facade** (Log+Metal+WoodWalnut cells in
  the neighbor-cube plane). **CORRECTION 2026-07-06:** the "glazed window" reported here
  earlier was MISREAD — the Glass+WoodWalnut cells at (4,17,42) are a back-bar shelf with
  glass bottles standing against the wall (furniture), not a window. NO auto-filled typology
  generates windows (see finding 1 below; `room_program.json` and `RoomLayout.cpp` contain
  none) — P1's window framing is only exercised by hand-written programs (FinishForgeTest).
- **Unit-level red/green independently reproduced** on this machine by a solution-auditor
  (reverted realizer → exactly 7 failing framing assertions → restored → 18/18 green).

**Findings logged during the L4 run (new work items):**
1. **Longhouse + croft programs define NO WINDOWS** — all 4 settlement buildings are
   windowless; P1's window sill/lintel framing is only exercised by tavern-class typologies.
   Windows per typology (with grounded counts/sizes) is program-model work, pre-P2.
   **→ CLOSED 2026-07-06 (same session):** grounding-auditor pass (cross-passage topology
   GROUNDED: Dartmoor longhouse/Byre-dwelling/Hall house + EH Wharram Percy; humble window
   count 0–2 front-wall GROUNDED via NOSAS Beauly + Herefordshire; window SIZE remains
   NEEDS-RESEARCH — shipped 1×1 cube labeled STYLIZED in room_program.json sources; shuttered
   air only, Glass gated post-1558). Implemented: `room_program.json` `entrance`/
   `entrance_between`/`entrance_opposed`/`windows` specs + `RoomLayout` autofill (opposed
   cross-passage door pair at the grounded bay boundary, front-wall windows every story, byre
   excluded, blocked slots shift). Red-before-green `OpeningsLayoutTest` (6 tests incl. the
   autofill→realize geometry link); solution-auditor PARTIAL→gaps closed (JSON-revert red
   reproduction REAL; geometry-link test added at its request; L4 scans reported-by-main-agent:
   opposed door signatures both long walls of house_1, 2 front windows, byre/back blank).
2. **Entry door is always on the west gable** for all 4 settlement buildings — no orientation
   variety, and doors don't face the street. Door orientation should come from the plot's
   street frontage. **→ CLOSED 2026-07-06 (same session, two steps):** (1) doors moved to the
   grounded cross-passage long elevation; (2) STREET-FACING shipped — `BuildingProgram.front`
   wall hint, `streetSideForPlot` (prefers the FACING shared street — another plot across it —
   over the perimeter ring; pure geometry, non-grid-safe), wired through `build_settlement`.
   Red-before-green ×2 (`FrontHintFlipsTheEntranceToTheStreetWall`,
   `PlotsFrontTheSharedStreetFacingEachOther`), 243/243 sweep, auditor PASS (both reds
   independently reproduced via neutralized-code rebuilds; L4 log-verified: house_1/2 bboxes
   flipped z6–12 → z7–13 between the naive and facing runs while row-1 stayed put — the two
   rows now front the middle street facing each other;
   `screenshots/screenshot_20260706_144338_933.png` is the street-level view). Scope note: a
   gable-wall hint is intentionally ignored for cross-passage typologies (grounded placement
   wins); shop typologies (street = narrow burgage frontage) not yet hint-aware.
3. **Sill 1-micro proudness is not resolvable at L4 cube-granularity** (`scan_micro` reports
   per-cube counts; the proud ledge lives inside the wall cube's micro space). The invariant is
   held at L2 by FinishForgeTest on the real canvas. An L4 micro-precision probe would need a
   micro-level query endpoint.
4. **Vernacular roof visual quality:** the stepped wood-band roofs read as "stacked lumber
   piles" at settlement scale (esp. the tavern's tall roof mass) — a finish/read issue for the
   roof family, adjacent to P2.
5. `POST /api/structure/build` returns a spurious `"Request timed out waiting for game loop"`
   after 5 s for large builds that actually succeed — same queueAndWait-timeout gap as the
   asset-editor reload entry in `StructurePipelineGaps.md`.
6. Tavern width>7 correctly triggers `footprint_too_wide` (cruck-span limit) as warn-but-allow.

**Session notes:** the no-project editor ignores /api/camera and orbit-screenshots (viewport
camera never syncs — likely the project-selector state); use a --project launch for visual
verification. `launch_engine` (MCP) deadlocks the engine at boot on this machine too
(undrained stdout pipe) — launch phyxel.exe directly with stdout→file and drive via HTTP.
`scan_micro` counts: full cube = +27, each subcube/microcube = +1; detect openings by
**per-column signature diffing**, never by naive occupancy thresholds (framed doors read as
HIGH counts, thin micro walls as low ones).
Framed openings instead of raw holes: **jambs** (sub-thick posts either side), **lintel** (sub
beam over the head, checklist D5), **sill** (protruding sub course under windows, sloped top via
`chamferEdge`, E7/H6), **threshold** (micro step at doors), **reveals** (opening sides show wall
thickness, not paper edges). Materials from StyleProfile (timber lintel on wattle styles, stone
on ashlar). **Red test (L2, fails today):** for every opening on the real canvas, frame cells
exist on all four perimeter sides and the sill protrudes ≥1 micro proud of the facade plane.
Grounding: jamb/lintel/sill sections need real dimensions → entries in GroundingGaps.md before
regen (period joinery sources).

### P2 — implement `place_trim` (placer 15)
Facade articulation, style-driven: **plinth course** (protruding base course, chamfered top),
**corner quoins** (D8; alternating sub blocks on ashlar/stone styles), **string courses**
(between stories), **eave cornice/fascia + bargeboards** (ties into the vernacular roof family),
**exposed timber framing** (sub-thick posts/rails/braces over infill on timber styles — the
single highest-impact medieval read). All primitives added to `MicroCanvas` as reusable
detailers: `frameOpening`, `courseBand`, `quoinCorner`, `timberBrace`. **Red test (L2, fails
today):** facade-relief metric — sample exterior wall faces; the fraction lying on a single flat
plane must drop below a threshold (today it is ~100%); corners on quoin-bearing styles must have
alternating protruding cells. C5 gates ornament by wealth tier from the brief.

### P2.5 — roof slope resolution (roof_forge; added 2026-07-06, user-reported) — **GABLE CORE DONE 2026-07-06**
Shipped red-before-green: `tests/core/RoofForgeTest.cpp` (slope-surface step metric on the real
canvas, shown failing "12 vs 2" against the cube-stepped rasterizer; coverage + eave-flush guards
green before/after) → micro-slice rasterization in the realizer's gable pass (rise pitch/3 micro
per micro of run; underside snapped to the subcube grid so `VoxelCountIsReasonable`'s 20k budget
holds — the naive both-faces-micro version cost 21,894 and FAILED it). 215/215 structure tests
green. Auditor PASS (independent revert→red→re-apply→green reproduction; budget file untouched).
L4: settlement + tavern rebuilt in-engine — smooth gabled thatch reads
(`screenshots/screenshot_20260706_112319/112345/112409_*.png`; illustrative, the unit test is the
proof). Scene datum: 5 furnished buildings = 209,481 visible faces @ 84–96 FPS (Debug, RTX 4090).
**HIP DONE 2026-07-06 (same session):** red-before-green `HipRoofSlopesUpFromAllFourEaves`
(failing "35 vs 35" centre==eave on the flat fallback) → hip branch in the realizer (surface
height = distance-to-nearest-footprint-edge × pitch/3, micro-stepped, subcube-snapped underside,
no gable-end walls), 219/219 suite green, auditor PASS (independent disable→red→restore→green
reproduction; L4 re-run reproduced the stone_manor bbox rise to y-max 23/22; close-up
`screenshots/screenshot_20260706_115522_681.png` shows four-sided tile hips).
**OVERHANG DONE 2026-07-06 (same session):** both roof passes extend the slope plane
`lround(roof.overhang*9)` micro past the eave walls (hip: all four sides; gable verge excluded —
bargeboard NEEDS-RESEARCH) as a thin 3-micro sheet (1 + max slope step = the watertightness
minimum, derived). Red-before-green `EavesOverhangTheWalls`; 260/260 sweep; auditor PARTIAL →
the one defect it found (mojibake em-dashes from a PS5.1 encoding round-trip in
structure_styles.json) fixed + diff-verified clean. **Value provenance corrected:** the 0.4/0.6 m
style values were UNCITED — now FLAGGED modern-analog (300–450 mm, odonnellroofingco via
TrimGrounding) in the style sources; stone_manor's 0.6 EXCEEDS that range (inherited, needs the
vernacular citation or a trim to 0.45). L4: house_3 bbox grew z-max 28→29 (the eave axis only —
matches the code), earlier runs byte-identical at 28; street view
`screenshots/screenshot_20260706_153334_677.png`.
REMAINING in P2.5: roof shell thickness is still the legacy pitch+1-subcube vertical depth (real
thatch coat depth NEEDS-RESEARCH before thinning — the attic dividend below); non-rectangular
(L-shape) footprints still get the flat cap; gable verge/bargeboard overhang.

**P2.6 — footprint composition & L-plan roofs (user concern, 2026-07-06) — ROOF CORE DONE
2026-07-06 (same session).** `decomposeRoofRanges` (2 nested bands → main range + full-depth
cross-wing) + a composed gable rasterizer (pointwise MAX of per-range micro-stepped height
fields — the valley emerges where they cross, watertight by construction; overhang only over
exterior ground; gable-end walls only on exterior ends). Red-before-green `LPlanMainRangeHasARidge`
+ `LPlanWingHasAPerpendicularRidge` ("ridge 33 vs eave 33" on the flat cap); auditor PASS — it
proved the rectangular path CELL-IDENTICAL to the old rasterizer (voxel breakdown 19944 C=0
S=3114 M=16830 byte-equal on both) and reproduced the red by disabling only the 2-band path.
⚠ budget watch: the cottage sits at 19,944 of the 20,000 voxel cap (56 headroom). L4: L-plan
hall house built in-engine (`screenshots/screenshot_20260706_155713_119.png` — two thatch
ridges + valley, notch as yard). REMAINING in P2.6: T/H plans (3+ bands → still flat cap);
hip-on-L renders as intersecting gables (disclosed); quoins skip L-plan corners (rectangular
gate); wealth-tier cross-wing frequency grounding (Brunskill). Historical framing
(to be grounding-audited before implementation): humble-tier vernacular (croft/longhouse) really
WAS plain-rectangular — one-room-deep ranges set by the timber bay system (cf. excavated village
plans, Wharram Percy). Higher-status plans grew by **composition of rectangular ranges** — open
hall + cross-wing(s) = L/T/H plans, rear outshots, courtyard inns — NOT organic blobs. So the
authentic variety mechanism = multiple intersecting rect ranges per building, scaled by wealth
tier. Engine today: `pickBuildingVariant` already deals ~1/3 "L" footprints (V5), but L-plans
can't READ as such because a non-rectangular footprint's roof falls back to the flat cap. Work:
(1) roof over an L = decompose into two rect ranges, gable each (reusing the micro-stepped
rasterizer), meet in a **valley**; red test = an L-plan realize has two ridge lines + no flat cap
+ watertight valley (no through-hole on the real canvas); (2) typology mapping — cross-wing
frequency/size per status tier (NEEDS grounding: Brunskill on hall-house plan types); (3) rooms
already fill the L per `autofillRoomLayout` (verify, else program work).
**The defect:** the gable pass (`StructureRealizer.cpp` pass 5) honors the grounded `pitch_deg`
but rasterizes it **horizontally cube-quantized** — the slope loop advances one full CUBE (1 m)
of run per step, rising `pitch` subcubes. A 50° thatch roof = 1.33 m rise per 1 m tread: metre-
wide stair-steps, the single chunkiest surface on every building (user: "cube level slopes …
look particularly bad"). Compounding: `timber_cottage` resolves roof material to `Wood` planks,
so the giant treads read as stacked lumber (a `Thatch` material exists; 50° pitch IS thatch).
**The fix (tree-forge lesson, applied to the roof plane):** keep the roof's interior mass
coarse, rasterize the slope SURFACE micro-stepped — advance the tread per micro (1/9 m) or per
subcube (1/3 m) of run instead of per cube, emit() re-coarsening the solid wedge interior. At
1-micro treads a 50° plane rises ~1.2 micro per micro of run — visually smooth at any normal
view distance; underside (visible from inside/eaves) gets the same treatment. Roof material per
style needs a grounding check (thatch/tile/slate per pitch band, cf. `vernacular_materials.json`).
**Red test (L2, fails today):** slope-surface step metric on the real canvas — max vertical
discontinuity between horizontally-adjacent roof-surface cells ≤ 2 micro on a pitched roof
(today: 4 subcubes = 12 micro at 50°); plus eave-flush stays intact (no regression on
checkRoofEaveFlush). Perf: measure S/M face growth vs the cube-stepped roof (expect the
trunk-shell economics: surface-only cost).

**Attic dividend (user, 2026-07-06):** the gable pass already leaves a hollow triangular void
above the ceiling (the roof is a `pitch+1`-subcube shell, gable ends are thin walls) — but the
shell is ~3–6× thicker than a real roof build-up (50° thatch: ~1.07 m perpendicular vs ~300 mm
real), and its underside is metre-stepped. P2.5 thins the shell to a grounded thickness and
smooths the raking underside, growing the void and making it read as a room. Making it a
FUNCTIONAL attic is separate **program-model work** (post-P2.5, leans on shipped generative
multi-story + auto-stair): a loft story in `room_program.json` for typologies that historically
had one (storage/sleeping lofts), ladder or stair access, a gable-end window/vent, and an
**L3 gate** — a character can physically enter and stand in the loft, with headroom asserted
against a GROUNDED vernacular loft figure (NEEDS-RESEARCH: historic loft headroom — do not
assume the modern 1.98 m).

### P3 — interior finish (smaller, after P1/P2 prove the loop)
Baseboards/casings (checklist), ceiling beams (sub joists under floor slabs — visible from
below, huge interior read), stair nosings + railings (`place_stairs` is already L3; railings
must not break `TraversalProbe`).

### P4 — stress + perf gate
Full furnished tavern + 10-story tower + a settlement block with P1-P3 on: `ResolutionReport`
C/S/M mix per building, `get_render_stats` faces vs the 412k→49FPS tavern datum, budget recorded
in ValidationLedger. If trim blows the budget, it thins by DISTANCE-TO-PLAYER tiering at
placement time — never by removing the pass.

## Validation ledger entries (to add when work starts)
- `cut_openings` — required L2 (frame invariant), current L1.
- `place_trim` — required L2 (relief metric), current L0.
- Interior finish — required L3 where it touches traversal (railings), else L2.

## Why this will read as dramatic as the trees did
The trees didn't get better because voxels got smaller everywhere — they got better because
detail concentrated exactly where the eye reads shape: silhouettes, edges, surfaces. Buildings
are the same: a framed window with a sill shadow, a chamfered plinth, corner quoins, and beam
ends under the eaves change the read of the WHOLE building while touching <5% of its cells.
The mass stays cube-cheap; the finish carries the look. Same math that made the trunk shells
cost +2.4%.
