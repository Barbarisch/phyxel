# 50 · excavate_subterrane

> Tier: Subterranean (see Part 8). Part-1 status: **M** (engine-cap missing). Schema: [`README.md`](README.md).

## Job
Carve the **below-grade void** into chunk terrain + remove/backfill earth — the shared capability every
subterranean carver needs.

## Reads
- The subterranean elements to build (sewer routes, crypt, dungeon, mine — from #49 + the program); the SiteReport (water table, rock); Part 8.

## Emits
- Excavated **voids** in chunk terrain (the negative space the carvers fill); spoil handled; shoring on unstable faces; occupancy rebuilt on edited chunks.

## Algorithm
1. For each subterranean element, carve its volume out of the chunk terrain (remove voxels).
2. Handle spoil; shore unstable faces.
3. Rebuild occupancy on the edited chunks.

## Satisfies (checks)
BB1 (actually excavated, not a perched/sealed box), the **core excavation gap**.

## Engine capability needed
- **Terrain excavation** — ❌ MISSING (THE keystone gap, shared with prepare_pad #2 + excavate_basement #34).
- Occupancy rebuild — ⚠️.

## Failure modes
- A "tunnel" that's a box in solid rock with no void (the stub bug at network scale).
- No spoil/shoring; occupancy not rebuilt (fall-through).

## Function testers
- **F1** Each subterranean volume is a **real carved void** in terrain.
- **F2** Spoil handled; unstable faces shored.
- **F3** Occupancy rebuilt on edited chunks.
- **F4** Below the water table → flagged/drained.

## Grounding
- Depths from Part 8 (catacomb 3–25 m, etc.); the capability is shared with #2/#34.

## Open questions
- One excavation engine API serving pad / basement / subterranean — design it once.
