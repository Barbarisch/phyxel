# Subcube-Resolution Character Collision — Scope & Plan

**Goal (user directive, 2026-07-16):** "I just don't understand why the collision shape
of the chunk and the visual rendered shape is not the same thing." Retire the
*collision-doesn't-match-what-I-see* bug class for the player character — the standing
demand behind task #9 (flare standoff), the notch-entry standoff, the fallen-log foot
hover, and the leaf-wall feel. Collision must be **derived from the same voxel truth as
the render**, at subcube (1/3 m) granularity, with differences below player perception.

## Ground truth (measured 2026-07-16, not assumed)

The scoping assumption "the occupancy grid is a 1 m bitmap" was **wrong**. Measured
reality (`engine/include/physics/VoxelOccupancyGrid.h`):

- `VoxelOccupancyGrid` is already **hierarchical**: dense 32³ cube bitset (4 KB) +
  `m_cubeSubdiv` marker bitset + sparse per-cube 27-bit **subcube masks** + sparse
  per-subcube 27-bit **microcube masks`. `queryAABB` returns exact sub-voxel boxes.
- `findGroundY` and `overlapsTerrain` are built on `queryAABB` → **already sub-voxel
  accurate** wherever the grid data is right.
- Character-vs-dynamic-body queries are **exact vs oriented boxes** as of 86d2d85
  (`overlapsAnyBody` via solver OBB test; `groundHeight` via per-box local down-rays).
- The **character contact probe** (`engine/src/scene/VoxelContactProbe.cpp`, header
  comment: *"Cube-grain"*) reasons at whole-cube granularity by design: cardinal-snapped
  directions, 1 m column walking (`scanFaceTop` kStep=1.0), chest-height column probes,
  ledge/mantle classification in cube units.
- Foot-IK samples **static terrain only** — it has no knowledge of dynamic bodies
  (fallen logs), hence the visible ~0.3 m boot hover when standing on a tilted log.

## The four real gaps (ordered by evidence strength)

### P1 — AUDIT grid maintenance — **COMPLETE 2026-07-17: CLEAN BILL**
Instrument shipped: `GET /api/debug/occupancy_cell?x=&y=&z=` (grid masks vs chunk
content in one response; `Chunk::getOccupancyGrid()` accessor added). Audited:
- fresh forge-oak flare cell (49,17,15): 11 subcube bits == 11 content subcubes,
  positions identical;
- kerf-carved stump cell (18,17,43): 24 filled bits = 15 solid + 9 subdivided;
  EFFECTIVE solids == content's 15 subcubes exactly; 204/204 micros match per-slot;
- carve-emptied neighbors: all clear.
ZERO query-visible phantoms. `queryAABB` honors both subdivision levels
(code-verified). The character's horizontal blocking (`overlapsTerrain`) is
queryAABB-based → **already subcube-exact**; body blocking OBB-exact since 86d2d85.
VERDICT: the grid-maintenance-desync hypothesis is dead. The `VoxelRaycaster
RESOLVE` warnings concern the ChunkVoxelManager targeting maps (left-click aiming),
not the physics grid — tracked separately. The original #9 "flare standoff" is
likely already resolved by honest data + 86d2d85; needs a hands-on walk-up to close.

#### (original P1 hypothesis, kept for the record)
The format supports fine data; the question is whether every write path fills it
correctly. Evidence of desync: recurring `VoxelRaycaster [RESOLVE] ... didn't hit any -
position should probably be marked EMPTY` warnings; the live flare standoff despite
`overlapsTerrain` being queryAABB-based.
**Measurements first (red before any fix):**
1. New debug dump (extend `/api/debug/body_boxes` or a sibling `occupancy_cell`
   endpoint): for a given cell, return the grid's cube bit, subdiv bit, subcube mask,
   micro masks — compared against chunk content.
2. Audit a fresh tree's flare cell (known content: 11 subcubes, layers 5/4/2), a
   kerf-carved cell (micros + heartwood), and a template-spawned structure wall.
3. Audit every write path: `createChunkPhysicsBody` / `forcePhysicsRebuild` bulk build;
   the `addCollision/removeCollision/updateNeighborCollisions` callbacks behind
   `ChunkVoxelManager` edits (kerf subdivision/removal chain); `clearCellsBulk`;
   `buildChunkPhysicsInRegion`.
**Exit criterion:** for the audited cells, grid masks == chunk content exactly; the
character can stand *inside* a kerf notch mouth and approach a flare to capsule-contact
with visible wood (L3 probe assertion + live L4).

### P2 — Character contact probe at subcube grain
Upgrade `sampleVoxelContact` / movement-solve reasoning from 1 m to 1/3 m where feel
depends on it: forward-face detection (approach distance), step-up/glide heights
(subcube stairs = 1/3 m risers), ledge classification. Keep cube-grain for
mantle/ladder (1 m semantics are gameplay-correct there).
**Red tests:** probe-level — a subcube-thick wall must register the forward face at its
actual surface (not the cell boundary); a 1/3-step must glide, a 2/3-step must step,
per `m_maxStepHeight`. Live: walk up a subcube staircase; approach a flare.

### P3 — Foot-IK + grounding on dynamic bodies
The 5-ray `groundHeight` gives the capsule an honest support height; the visual gap
remains because (a) max-of-rays perches the capsule on the highest corner contact of a
tilted narrow log, (b) foot-IK plants against terrain only.
Work: feed body-surface heights into the foot-IK sampler; consider insetting the corner
rays (±0.15 instead of ±0.25) to reduce corner-perch float.
**Measure first:** the actual hover magnitude across log tilts (drop-probe map on a
felled log vs render surface) before and after.

### P4 — Leaf policy (deliberate mismatch, decided not discovered)
Leaves currently occupy like solid cells (walking into a bush = wall). Proposal:
leaf-only content writes NO character-blocking occupancy (or a parallel "soft" mask),
so crowns are waded through; canopy stops being invisible walls or stand-on surfaces.
Consequence (accepted?): characters cannot stand on treetops. This is a design choice
for the user to ratify before implementation.

## Explicit non-goals
- Rigid-body solver fidelity (already box-merged + OBB-correct; fine as-is).
- Render-side changes (renderer is the source of truth being matched).
- Crown breakup on impact (task #13 — separate feature; trunks lying flat will reduce
  the P3 surface area, so #13 may ship first or in parallel).

## Validation ledger (CLAUDE.md discipline)
| Increment | Contract | Required depth | Red test |
|---|---|---|---|
| P1 audit | grid masks == chunk content | L2 (mask diff) | audited flare/notch cells show mismatch pre-fix |
| P1 fix | walk to visible wood | L3 probe + L4 live | approach-distance assertion fails pre-fix |
| P2 probe | face at true surface | L3 | subcube-wall face assert |
| P3 IK | boot-to-bark gap ≤ ~0.1 m | L4 measured | hover map pre/post |
| P4 leaves | wade through crowns | L4 + design sign-off | n/a (policy) |

## Perf notes
- Fine-grain queryAABB cost is already paid by rigid bodies today; character adds a
  handful of column queries/frame — no new hot loop.
- The grid's sparse maps grow with carved terrain; watch per-chunk map sizes on heavily
  chopped areas (measure during P1 audit).

## Standing bugs adjacent (tracked, not in this plan)
- Task #13 crown breakup + fallen-tree collision polish.
- Task #16 F9 sapling threshold measurement.
- SceneIntegrationTest order-sensitive failures (pre-existing, April code).
