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

## Current audit (2026-06-25) — 6 of 7 non-conforming

The real audit's full verdict is **pinned** in `RealLibraryAuditReportsKnownGaps` — regenerating an
asset (or a new one drifting) flips a status and fails that test, prompting an update here.

| type | template | archetype | status | action |
|------|----------|-----------|--------|--------|
| bed | bed_single | bed_single | ✅ ok | — (the only conforming one) |
| barrel | barrel | _(none)_ | **no_canon** | add a grounded `barrel` archetype to `object_dimensions.json` (cooper's cask ~0.5 m dia × 0.9 m) |
| counter | counter | counter_kitchen | **no_metrics** | recompute the `.metrics.json` sidecar for `counter.voxel` |
| bench | bench_wood | bench | **no_checkable_dims** | the `bench` canon has only `seat_top`/`seat_depth`/`length_min` — add overall `height`/`width`/`depth` (or accept it's unmeasurable) |
| chest | chest_closed | chest | **out_of_tolerance** | actual 1×1×1 cube vs canon coffer 1.2 w × 0.55 d × 0.7 h — regenerate as a proper chest shape |
| fireplace | fireplace | hearth | **out_of_tolerance** | actual 3×2×1 vs hearth canon 1.5 w × 1.2 h × 0.6 d — 2× oversized, regenerate |
| table | table_wood | table_dining | **out_of_tolerance** | depth 1.0 vs canon 0.84 (minor; tol 0.05) |

## How placement is affected

Footprint-aware placement (Part 1) reserves the **actual** `.metrics` extent. So a non-conforming
asset is placed at its real (wrong) size — e.g. the oversized fireplace reserves a 3×2 footprint. The
conformance flag is the signal to fix the asset so its real size matches the grounded intent; once
regenerated, placement footprints become correct automatically.

## Caveats / known limits

- **Axis convention**: the width/depth comparison assumes the **+z-facing** authoring convention
  (z-extent = depth). An asset authored facing a different axis could be mis-compared on width vs
  depth. A cube (chest) is wrong regardless; oriented assets need the convention to hold.
- **Feature dims vs bounding box**: canon archetypes carry feature dims (seat_top, leg_inset, …) that
  this check ignores — it only compares the **overall bounding size** (height/width/depth/length),
  the same set `AssetValidator` measures.
- This is a **library-level** audit (per asset), not per-build; the test is the tracker. A runtime/MCP
  surface (so a session can ask "which furniture is non-conforming?") is a noted follow-up.
