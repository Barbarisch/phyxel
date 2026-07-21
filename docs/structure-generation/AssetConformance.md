# Furniture Asset Conformance — the regenerate list

Tracks which furniture **templates** are not dimensionally valid against grounded canon
(`resources/object_dimensions.json`), so we know which assets to regenerate. Produced by
`checkFurnitureConformance` (engine `core/FurnitureConformance.h`); the live audit is
`tests/core/FurnitureConformanceTest.RealLibraryAuditReportsKnownGaps` (prints the table, asserts the
structural gaps as teeth). Re-run that test to refresh this list.

Each finding: **ok** (measured AND conforms) · **no_metrics** (template has no `.metrics.json`
sidecar) · **no_canon** (no grounded archetype to measure against) · **no_checkable_dims** (the canon
archetype declares only *feature* dims like `seat_top`/`length_min`, no overall-size key, so the
bounding box can't be measured — NOT a pass) · **out_of_tolerance** (actual dims drift from canon ±
tol). Only **ok** counts as conforming.

## Current audit (updated per `tests/core/FurnitureConformanceTest.cpp` `RealLibraryAuditReportsKnownGaps`) — 0 non-conforming

The real audit's full verdict is **pinned** in `RealLibraryAuditReportsKnownGaps` — regenerating an
asset (or a new one drifting) flips a status and fails that test, prompting an update here.
Since the 2026-06-25 snapshot below, `barrel` gained a grounded archetype, `counter` gained a
`.metrics.json` sidecar, `bench` gained overall `height`/`depth` canon fields, and `table_wood` was
regenerated — the pinned test now asserts **all** tracked types are `ok`, including a much larger
set added since (inn depth: `tavern_bar`, `bar_stool`, `back_bar`, `tavern_table`; lighting:
`candle_stand`, `wall_lantern`, `chandelier`; tableware: `mug`, `bottle`; smithy: `forge_hearth`,
`anvil`, `bellows`, `tool_rack`).

| type | template | archetype | status | note |
|------|----------|-----------|--------|------|
| bed | bed_single | bed_single | ✅ ok | — |
| chest | chest_closed | chest | ✅ ok | regenerated → coffer 1.22 w × 0.56 d × 0.67 h |
| fireplace | fireplace | hearth | ✅ ok | regenerated → hearth 1.56 w × 1.22 h × 0.56 d |
| barrel | barrel | barrel | ✅ ok | archetype added (cask 0.89 h × 0.56 dia, thebarrelmill.com) → cask 0.89 h × 0.56 dia vs canon 0.88/0.56 |
| counter | counter | counter_kitchen | ✅ ok | `.metrics.json` sidecar added → worktop 0.89 h × 0.56 d vs canon 0.9/0.6 |
| bench | bench_wood | bench | ✅ ok | canon gained overall `height`/`depth` → bench 0.44 h × 0.44 d vs canon 0.45/0.4 |
| table | table_wood | table_dining | ✅ ok | regenerated → table 0.78 h × 0.89 d vs canon 0.75/0.84 (was out_of_tolerance, depth 1.0 vs 0.84) |

This list is no longer exhaustive of the tracked types — see the test file for the full current
set (20 types as of this audit). Re-run `RealLibraryAuditReportsKnownGaps` to refresh.

Regeneration note: `tools/regen_furniture.py` authors the `.voxel` at microcube resolution AND writes
the matching `.metrics.json` from the **same** emitted bounds, so geometry and sidecar can't drift.
The chest keeps its `# part: lid hinge=back_top axis=x` kinematic annotation.

## How placement is affected

Footprint-aware placement (Part 1) reserves the **actual** `.metrics` extent. So a non-conforming
asset is placed at its real (wrong) size — e.g. the fireplace template used to reserve an oversized
3×2 footprint before it was regenerated. The conformance flag is the signal to fix the asset so its
real size matches the grounded intent; once regenerated, placement footprints become correct
automatically. (All tracked assets currently conform — see the audit above.)

## Caveats / known limits

- **Axis convention**: the width/depth comparison assumes the **+z-facing** authoring convention
  (z-extent = depth). An asset authored facing a different axis could be mis-compared on width vs
  depth. A cube (chest) is wrong regardless; oriented assets need the convention to hold.
- **Feature dims vs bounding box**: canon archetypes carry feature dims (seat_top, leg_inset, …) that
  this check ignores — it only compares the **overall bounding size** (height/width/depth/length),
  the same set `AssetValidator` measures.
- This is a **library-level** audit (per asset), not per-build; the test is the tracker. A runtime/MCP
  surface (so a session can ask "which furniture is non-conforming?") is a noted follow-up.
