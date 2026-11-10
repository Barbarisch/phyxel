# Matter representation review — static vs dynamic voxels, at the scale of millions

**Status: REVIEW + PROPOSAL, 2026-11-10. Nothing built.**

**Owner, 2026-11-10:** *"we are rethinking at a fundamental level how the engine functions ... a lot of its
optimization comes from how it handles static voxel rendering ... chunk-relative positions, no rotation ... dynamic
voxels do not scale nearly as well ... we are trying to render voxels using several floating point numbers ... take
a long careful look at that very fundamental core ... you have to think at scales in the millions."*

**Inputs:**
- Code read on `feat/matsim-lab`, cited by file.
- Measurements from `MaterialSimPlan.md` §6i–6m.
- The engine's earlier surveys, which reach the same place from the rendering side:
  - `LargeWorldScalePlan.md` §5.1–5.2 (Teardown-style palette bricks for sub/micro detail; John Lin's "several
    formats, explicit converters");
  - `RayTracingPlan.md` (brickmap / Tree64 tracing, edit costs);
  - `ContinuousLodPlan.md` (the density wall, measured).

---

## 1. What the engine does today (measured / read)

| Path | Record | Bytes | Per | Lives where | Rebuilt when | Cap |
|---|---|---|---|---|---|---|
| Static chunk voxels | `InstanceData` (`core/Types.h:82`): 15-bit chunk-local position, face id, texture, tint, light, merge extents | **16** | visible (greedy-merged) **face** | GPU, uploaded once | on edit (chunk remesh) | none in practice |
| GPU debris | `GpuParticle` (`GpuParticlePhysics.h:42`) 96 B + solver body 208 B + 6 × `DynamicSubcubeInstanceData` (`Types.h:208`) 64 B | **~690** | debris cube | GPU | every frame (compute expand) | **10,000 pieces** (60,000 face slots) |
| Kinematic (doors, furniture) | `KinematicFaceData` (`KinematicVoxelManager.h:24`) | **48** | face, object-local | GPU | on build | — |
| MatSim view (lab) | `DynamicSubcubeInstanceData`, built on the host (`MatSimLab::rebuildFaces`) | **64** | visible face of a cell or particle | built on the CPU, uploaded | every stepped frame | the view capped near ~65 k particles before face culling (§6i) |
| MatSim sim state | cell 4 B (material + fill); GPU particle 32 B (pos + units, vel) | 4 / 32 | cell / particle | GPU **and** host: copied both ways every tick | every tick | — |

**What actually makes the static path cheap.** Ranked by size of effect (estimates, from the measured scenes):
1. **It is uploaded once.** It changes only on edit. A per-frame path moves its whole size every frame, so this is
   the difference between ~0 and everything.
2. **It draws surfaces, not volumes.** Hidden faces are culled and coplanar faces are merged. Measured on Perlin
   hills with 25 buildings: 269,618 faces merged vs 1,764,780 un-merged, 6.5x fewer, ~5x the FPS
   (`ContinuousLodPlan.md` M4). A solid 9³ microcube block has 729 voxels but at most 6 merged faces.
3. **The position is implicit in the grid and packed into a few bits.** 16 B against 64 B is 4x, which is real, but
   the smallest of the three.

**Why dynamic matter doesn't scale.** It loses all three at once:
- it moves its records every frame;
- it draws every face of every piece;
- it carries a float transform per piece: a quaternion, a vec3 scale and a vec3 position per FACE record.
On top of that, MatSim keeps ownership on the host, so the whole state crosses PCIe every tick. That is why §6l found
60 % of the 1/9 m flood tick in host-side bookkeeping, not in the simulation.

## 2. The scale arithmetic

- **At 1/9 m, 1 m³ is 729 voxels.** A furnished room at microcube detail runs to hundreds of thousands. A flood of
  350 m³ is 257 k cells or particles. Anything that "looks right" at this engine's detail is in the millions.
- **Per-face float records, per frame:** ~1 M visible faces × 64 B = 64 MB/frame, 3.8 GB/s at 60 fps, before the
  CPU builds them. Not viable.
- **Per-face packed records, per frame:** 1 M × 16 B = 16 MB/frame. Better, but still a rebuild every frame.
- **One byte per voxel in GPU-resident bricks, touched only where it changed:** a fully dynamic 1/9 m sim box of
  1.5 M cells is 1.5–6 MB resident, and ~0 bytes cross per frame. The renderer's cost scales with screen pixels, not
  voxel count. This is the Teardown figure (1 B/voxel at 10 cm), and it is the one regime that holds at millions
  (`LargeWorldScalePlan.md` §5.1 D).

The conclusion is not "pack the dynamic floats tighter". It is: **don't give individual voxels their own transforms
or faces at all.**

## 3. Proposal: one representation for matter — GPU-resident voxel grids with a transform

Everything is a grid of 1-byte material voxels, held in bricks with occupancy masks. A grid has one transform.

| Kind | Transform | Who writes it | Today |
|---|---|---|---|
| **World grid** (terrain, buildings) | identity, chunked | edits | static chunks (raster faces) |
| **Body grids** (debris clumps, furniture, doors, a falling tree) | one rigid transform per BODY | physics writes the transform; edits write voxels | per-piece transforms (debris), per-face local records (kinematic) |
| **Sim grids** (MatSim boxes: liquids, powders, gas) | identity | the CA writes cells in place on the GPU | cells on the GPU, copied to the host, re-faced on the CPU |
| **Free points** (spray, flying liquid) | none: splatted into a sim grid's density each frame | the particle solver | per-face 64 B instances |

- **Rotation belongs to the body, never the voxel.** A 1,000-voxel debris clump costs one 32 B transform. Today it
  is ~1,000 × 690 B. Noita's model is the same (N5: bodies made of tracked pixels; `NoitaGapAnalysis.md`).
- **Free points never become render records.** They write density into the grid, and the grid is drawn: as a
  surface for liquids, voxels for powders. Their count is bounded by simulation cost, not render cost.
- **Drawing grids:** rasterize each brick's or grid's bounding box and DDA-march its volume in the fragment shader
  (the Teardown hybrid, already the adopted endgame for sub/micro detail in `LargeWorldScalePlan.md` §5.2.2).
  - Cube-scale terrain can stay on today's raster path. The hybrid is incremental: no big-bang renderer swap.
  - Lighting stays as `LightingPipeline.md` §0 models it. Marched fragments write depth and normals like raster
    ones. Shadows need the volumes in the shadow passes (Teardown does this).
- **Sim and render share memory.** The device CA (§6l) already keeps cells on the GPU in 8³ bricks with dirty marks.
  Those cells *are* the render volume. No meshing, no download, no host copy for drawing.
- **The physics occupancy pool converges too.** GPU debris already collides against a shared micro occupancy pool on
  the GPU (`DynamicVoxelPhysics.md`). Occupancy bitmasks per brick serve both collision and empty-space skipping.

**How it fits §6m (resolution by activity):**
- the near ring is particles splatted into a fine sim grid;
- the wave ring is a column height field, drawn as columns, the same as grid columns with a fill level;
- the rest ring is a coarse grid.
One renderer for all three. Cell size becomes a grid property, not a separate code path.

## 4. Honest risks

1. **It touches the engine's core.** The lighting receiver matrix, three shadow cascades, eight LOD tiers, greedy
   merge, grass and foliage all assume raster faces. That is why this is a hybrid added beside the raster path, gated
   by measurements, not a rewrite.
2. **Ray-marching cost is per pixel × overlapping volumes.** Many overlapping dynamic grids (a debris shower) could
   cost more than faces. Unknown on this engine; the prototype must measure it.
3. **Brick size.** The engine's scales nest by 3 (1, 1/3, 1/9 m), so a cube is a 9³ microcube brick (= 3³ of 3³).
   MatSim bricks are 8³, and bitmask hardware likes 4³ / 8³ (Tree64). This is a real design choice, to be measured,
   not assumed.
4. **Shading equality.** A marched voxel must look like a rasterized one: textures per face, sub-tile UVs, tint.
   Teardown uses palette colour, not textures. Our textured sub-tiles need a decision: sample the texture at the hit
   face's UV (feasible) or accept a palette look for dynamic matter (an aesthetic call for the owner).
5. **Two formats during the transition.** Accepted (John Lin's note): several formats, explicit converters.

## 5. Plan: prototype on the MatSim lab first (small, measurable, reversible)

Each step has a prediction, a measurement, and a kill condition. It runs in the MatSim lab, which already has the
GPU-resident grid, the flood rig, the video tool and the per-phase timers. No engine core is touched until R2 passes.

- **R0, baseline (measure, no code change).** The current MatSim view on the 1/9 m flood rig:
  - faces built per frame, CPU ms to build them, bytes uploaded, GPU ms to draw;
  - the same for GPU debris at 10 k pieces.
  These are the numbers to beat.
- **R1, march the sim grid directly.**
  - A fragment shader DDA-marches the device CA's cell buffer: box raster + per-brick occupancy skip.
  - Drawn as Ice-textured cubes matching today's look (texture sampled at the hit face).
  - Measure: GPU ms at 1080p/1440p for the flood box, bytes per frame (target 0 from the host), and the view's CPU
    build (target 0).
  - Equality: screenshots of the same pose and tick, today's faces vs marched, per-pixel diff (debug tonemap,
    `LightingPipeline.md` trap 1).
  - **Kill: marching the 1.5 M-cell box costs more GPU ms than today's faces at the same pose.**
- **R2, splat the particles into the grid.**
  - The particle solver writes density into a fine grid each frame; the grid is drawn by R1's marcher.
  - The particle download stays only for the host phases that still need it; moving those to the GPU is the §6l
    "Next".
  - Measure: the 257 k-particle flood frame, host↔GPU bytes per tick.
- **R3, a body grid with one transform.**
  - One rigid debris clump as a small grid + transform, marched; against today's per-piece path.
  - Stress: 10 → 1,000 → 10,000 clumps; the overlap cost.
  - **Kill: overlap cost beats per-face cost at 1,000 clumps.**
- **R4, decide the static world (owner).** Only with R1–R3 data in hand: move sub/micro static detail to marched
  bricks (`LargeWorldScalePlan.md` §5.2.2), or keep raster + greedy merge. This is where the engine core changes, and
  it is a separate plan with its own design-key gate (`FeatureDesignKeys.md`).

**Decisions for the owner:**
1. Do you accept the hybrid direction (raster terrain + marched grids for everything that changes), with R1 as the
   first gate?
2. Should marched dynamic matter show the material textures (more work, matches today), or is a palette colour per
   material acceptable for liquids, powders and debris?
3. Order: R1 before §6m's P0 harness, or interleaved? My recommendation is R1 first. It is small, it tells us
   whether "millions" is reachable for anything that moves, and §6m's rings would all be drawn through it.
