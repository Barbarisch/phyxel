# Claims Ledger — completing the AssemblyPlan so consumers stop re-deriving geometry

> Status: **ACTIVE plan** (2026-07-24). Increments 1 + 2 SHIPPED (auditor PASS each);
> increment 3 next.
> Origin: user direction after the KI-5 defect arc — *"the pipeline/pathway for generating
> structures has some weakpoints… address that at an algorithm/architecture level so that we
> don't get stuck in such time-consuming audit loops."*

## 1. The diagnosis (grounded, not vibes)

Every KI-5 defect (windows on corners, furniture in walls, stairs under furniture, fence
corners, spawn insets) was a **cross-stage consistency failure**, not a stamping-order bug.
The voxel-stamp order already matches construction order (substructure → floor → walls +
openings → stairs → ceiling → quoins → roof; `StructureRealizer.cpp:166-677`). What is
broken is that **the plan is incomplete, so every consumer re-derives geometry privately**:

| Fact | Citation |
|---|---|
| `AssemblyPlan` has **no stairs field**. Stair geometry is planned **three independent times** from the same `ProgStair.rect`: realizer, pre-build validator, and a hand-rolled rect re-scan feeding furniture. | `AssemblyPlan.h:106-124`; `StructureRealizer.cpp:428`; `BuildingProgramValidator.cpp:220,245`; `StructureBuildService.cpp:692-698` |
| Jambs, lintels, sills, quoins and stair treads are painted **only as anonymous materials** into the MicroCanvas — no plan record, no owner. | `StructureRealizer.cpp:300-368` (trim), `:448-488` (quoins), `:400-440` (stairs) |
| `MicroCanvas` cells carry **material only** — no owner/feature tag. | `MicroCanvas.h:138` (`map<ivec3,string>`) |
| `AssemblyPlan::featureAt` cannot say `"stair"`, `"opening"` or `"quoin"`; a stairwell hole in the upper slab classifies as slab. The destruction system consumes this classifier. | `AssemblyPlan.cpp:112-149`; `docs/DestructionSystemV2.md` §featureAt |
| `FurniturePlacer::furnish` never consults the AssemblyPlan or canvas; it re-derives wall/door geometry from `ProgStory` and takes **five side-channels**: `reservedRects` (stairs), `extTMicro`, `intTMicro`, per-axis insets, per-room blocked sets. | `FurniturePlacer.h:104-111`; `FurniturePlacer.cpp:314-456` |
| Window corner-margin (KI-5a) re-invents the quoin zone as a local rule (`ed.lo==0 → sLo=1`) because no model of the corner zone exists to query. | `RoomLayout.cpp:294-311` |
| Our validators each check a **different source of truth**: program (`FixtureInsideShellTest`), canvas (`MicroPlacementOverlapTest`), placed world (`RealizedWorldValidator`) — none the plan. | `tests/core/*`, `RealizedWorldValidator.h:74-155` |

**Consequence:** each re-derivation is an independently-maintained assumption; each drift
between two of them is a KI-ticket plus a multi-round audit loop. The fix class is
architectural: make the plan complete and authoritative, then make consumers *query* it.

## 2. The design — the user's "construction pathway" as data dependency + claims

The build pathway (foundation → external walls → interior walls → doors/windows → stairs →
furniture) is adopted **as a claims discipline**, not a re-ordering:

1. **Each realizer pass RECORDS what it builds into the AssemblyPlan** — stairs, opening
   reveals, quoin/corner zones — at the same moment it paints the canvas. The plan becomes
   the single authoritative anatomy ("who owns this space and why").
2. **Each later stage QUERIES the plan instead of re-deriving.** Furniture asks "what floor
   cells are unclaimed?"; the window placer asks "is this cell inside a corner claim?";
   validators diff plan-vs-canvas-vs-world instead of re-planning.
3. **A claim conflict is a build-time error**, not a runtime defect — the KI-5 class becomes
   impossible by construction (same philosophy as the standing walkability-by-construction
   directive, applied to interiors).

`AssemblyPlan::featureAt` is the natural query surface (already consumed by the destruction
system and the editor overlay) — extend it rather than invent a parallel structure.

## 3. Increments (each auditable; census goldens + existing tests are the behavior locks)

**Increment 1 — stairs become first-class plan data.** *(IN PROGRESS)*
`StairRecord` in `AssemblyPlan` (well rect, story span, y-range, form, well-hole), populated
by the realizer stair pass; `featureAt` learns `"stair"` (fixing the real existing
misclassification of stairwell holes as slab); JSON round-trip; `StructureBuildService`
derives furniture `reservedRects` from `plan.stairs` instead of re-scanning
`program.stories`. Red tests: plan has no stair record today; `featureAt` on a tread cell
returns `""` today. Behavior lock: `StairReservationTest` must keep passing unchanged.
**Scope of equivalence (auditor round 1):** plan-derived rects equal the old re-scan for
adjacent, well-formed stairs (all current typologies); stairs the realizer *skips*
(duplicate, degenerate, non-adjacent) now deliberately reserve nothing — nothing was
built — a divergence pinned with canvas cross-checks in
`AssemblyPlanStairTest.SkippedStairsReserveNothingBecauseNothingWasBuilt`.

**Increment 2 — reveals + corner zones recorded.** *(SHIPPED, auditor PASS round 1)*
Every paint call in the openings pass routes through a `rec()` wrapper recording a
`TrimBox` (role clear/jamb/lintel/sill/ledge/leaf + material, structure-local micro) into
the `OpeningCut`; interior clear bands recorded; quoin pass records 4 `CornerZone` entries.
`featureAt` learns `"opening"` (from *clear* boxes only — trim never drives classification)
and `"quoin"`, fixing carved-doorway-cubes-answer-"wall". Zero canvas change proven two
ways: the `CanvasDigestTest` harness (4 fixtures covering every painting path; sorted
cells + material folded into FNV-64) byte-identical before/after — reproduced
independently by the auditor via selective `git stash push -- engine/` — and by
construction (`rec()` is an unconditional passthrough with identical args at every call
site, which also covers the one branch no fixture reaches: open-shutter leaves, whose
fixtures deterministically hash to closed). Auditor mutations confirmed the
record-vs-canvas cross-checks have teeth: a lying jamb x and a dropped lintel role were
both caught by `AssemblyPlanRevealTest`.

**Increment 3 — furniture consumes the plan.** `furnish` takes the `AssemblyPlan` (+ origin)
and derives doorway blocks, stair reservations, and wall insets from plan records; the five
side-channels become dead parameters and are removed. Locks: `FixtureInsideShellTest`,
`StairReservationTest`, `MicroPlacementOverlapTest`, furniture census unchanged.

**Increment 4 — openings/trim query claims.** `addTypologyWindows` corner margin becomes a
query against corner-zone claims (the KI-5a rule moves from a local re-derivation to the
model). Lock: `window_census.txt` golden byte-identical.

## 4. Non-goals / risks

- **Not a rewrite.** `realizeShell` pass structure, canvas painting, and all placement
  *behavior* stay identical through increments 1-2 (recording only). Behavior changes only
  where a side-channel is replaced by a plan query, guarded by the existing audited tests.
- **Plan JSON growth** — `assembly_plan` metadata is persisted per placed object; stair/
  reveal/corner records are small (boxes, not per-voxel), but measure size on the seed-3
  village before/after.
- **`fixtures`/`lights` plan fields are currently never populated** (`AssemblyPlan.h:112-113`);
  increment 3 is the natural point to start populating `fixtures` from `furnish` output.
