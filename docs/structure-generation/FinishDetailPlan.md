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
mm). L4 in-engine settlement check: 4 varied buildings built via /api/settlement/build (visual
screenshots still owed — the no-project editor camera is wedged, note below). REMAINING in P1:
reveals + thresholds as distinct elements, exterior arches.
**Session note:** the no-project editor ignores /api/camera and orbit-screenshots (viewport
camera never syncs — likely the project-selector state); use a --project launch for visual
verification.
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
