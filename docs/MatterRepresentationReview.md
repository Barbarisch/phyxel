# Matter representation review — how voxels are stored and drawn, and what to change

**Status: REVIEW, rev 2, 2026-11-10. Nothing built. For an owner decision.**

**Owner, 2026-11-10:**
- *"we are rethinking at a fundamental level how the engine functions ... dynamic voxels do not scale nearly as well
  ... think at scales in the millions."*
- Then: *"i am not married to the cube/subcube/microcube arch ... redesigning it to work better or as good as it is
  right now seems like a tall order ... we need to read the code very very very carefully before i sign off."*

**How this was read:**
- Four read-only audits ran in parallel, each citing file:line and separating [READ] from [INFERRED]:
  - static storage and render;
  - every consumer of the representation;
  - every path that draws moving voxels;
  - the lighting / shadow contract.
- I then re-checked their load-bearing claims against the source myself (marked ✔ below).
- Rev 1 of this document (commit `4a6cafcb`) was written before the audits and got facts wrong: it said static
  geometry is uploaded once into GPU memory (it lives in host-visible memory), and its proposal was premature. This
  revision replaces it.

---

## Part A — How it works today

### A1. Storage of static voxels

| Level | How stored | Bytes | Source |
|---|---|---|---|
| Cube (1 m) | palette store: a u16 palette index + a state byte per cell, 32³ per chunk; ~64 B for a chunk that is all one value | **3 B per cube** ✔ | `ChunkVoxelStore.h:81-90` |
| Subcube (1/3) | one heap object per voxel, own `std::string` material name, ivec3 positions, physics leftovers, found through nested `unordered_map`s | ~120 B + map nodes | `Subcube.h:72-88`, `ChunkVoxelManager.h:247-248` |
| Microcube (1/9) | the same, with three ivec3 positions ✔ | **~136 B measured** + map nodes | `Microcube.h:82-99`; PerfProgram2026-09.md ~750 |

- A cube costs 3 B for a cubic metre; a microcube costs ~136 B for 1/729 of one. **Per unit volume, fine detail
  costs ~33,000x more memory than coarse.**
- Measured on an engine-generated town (1 town, 4 buildings): 553,149 microcubes = 75 MB heap floor. Surface
  chunks run ~5.9 MB each (Debug).
- There is no tree. A cell is either a cube in the store or a set of sub/micro objects; `subdivideAt` deletes the
  cube and pushes 27 subcube objects.
- Removing a subcube is a linear scan of the chunk's list.
- Only fine voxels carry tint and state; cubes cannot.

### A2. Persistence

- SQLite `chunk_blobs` holds format "PXB2": palette, cube run-length encoding (z-minor), then one record per
  sub/micro voxel (5-9 B each).
- No general compression (LZ4/zstd was planned and deferred).
- Only visible voxels are written.
- The disk format is the cube/subcube/microcube hierarchy itself.

### A3. Meshing and drawing of static voxels

- **Meshing runs on the main thread**, budgeted per frame. Off-thread meshing is parked.
  - Measured 2-4 ms typical, 13 ms worst per chunk; 12.6 ms for a chunk with ~8.8 k microcubes.
  - Cubes are greedy-merged within a 32x32 slice. Fine voxels are merged only inside their parent cube.
  - Fine-face meshing does material lookups by string name, per face.
  - The mesher still computes per-corner light (16 samples per face) that is constant since the light field was
    deleted. Dead work.
- **The face record is 16 B** (`InstanceData`, `Types.h:82-97`):
  - 5+5+5 bits of chunk-local position, a 3-bit face, a 2-bit level (cube / sub / micro / LOD);
  - two base-3 slot codes for the sub/micro position;
  - merge extents, texture, flags, tint.
  - The hierarchy is encoded in the GPU record itself, decoded by `static_voxel.vert`, `shadow.vert` and
    `debug_voxel.vert`.
- **Chunk buffers are host-visible memory, not device-local** ✔ (`ChunkArenaSystem.cpp:83-84`). On a discrete GPU
  the vertex stage probably reads them across PCIe [INFERRED].
- Uploads are a `memcpy` into the live span.
- **Main pass: a depth prepass, then shading, each one draw per visible chunk** ✔. Not multidraw: GPU-driven
  multidraw is used only for the mid shadow cascade. CLAUDE.md overstates it.
- Measured cost centres for fine detail:
  - GPU geometry: ~0.01 ms.
  - The real costs are per-pixel light marching on those pixels (removing micro indoors saved 7.4 ms, 5.5 ms of it
    marching), CPU RAM, and main-thread re-mesh hitches.
- Measured results:
  - greedy merge: 25 buildings 1.76 M → 270 k faces, 27.5 → 135-145 FPS;
  - one tavern ~13.8 k faces at ~460-497 FPS.
  - **The static draw path is not the problem.**

### A4. Drawing of moving voxels

| Path | Bytes per face | Faces per voxel | Built by, when | Culling | Transform | Cap |
|---|---|---|---|---|---|---|
| Static (reference) | 16 | merged | CPU, on edit | neighbour + merge | none | — |
| GPU debris | 64 | always 6 (384 B) ✔ | GPU, every frame | **none** ✔ | pos + quat in every face | **10,000 pieces** ✔ |
| Kinematic (doors, furniture, felled trees) | 48 | ≤6, adjacency | CPU at add; **one spawn re-uploads all objects' faces** | lattice | per object (good) | 131,072 faces |
| Fine items | 48 | greedy-merged boxes (~0.02 faces / cell) | CPU at spawn | merge | per object | 6 simulated at once |
| Characters | 44 per box | all 6 | CPU, cached blob + bones per frame | per character + LOD | per bone | 262,144 boxes |
| MatSim view | 64 | neighbour-culled | CPU, every dirty frame, **after downloading the whole grid** | neighbour | none | 393,216 faces |

- **The only GPU-resident path is debris, and it is capped at 10 k pieces** ✔ (`solver_shared.h:45`). It draws all
  60 k faces three times a frame (main + near + mid shadows) ✔, with no culling ✔.
- **Nothing that moves is in the occupancy pool** ✔. The pool that lighting, GI, debris collision and water read is
  built only from the chunk map (`RenderCoordinator.cpp:894`). Moving objects don't occlude point lights, aren't
  seen by GI, and debris doesn't collide with them.
- The baked light carried by debris and kinematic records is never read by `voxel.frag` ✔ (dead data).
- Per-object transforms already exist for kinematic objects and items. The "one transform per body" idea is
  half-built, not new.

### A5. Who depends on the representation (blast radius)

**Narrow read interfaces exist and most simulation/AI code uses them** ✔. All are world-coordinate predicates or box
lists, most at micro resolution:
- `occupiedMicro`, `hasVoxelAt` / `getVoxelTypeAt` / `subVoxelFloor` (`ChunkManager.h:385-393`);
- the pool's `stateAtMicro` (WaterCore, MatSim);
- `phxOccupancySolid` + DDA in `occupancy.glsl` (every lighting, GI, debris, water shader);
- `VoxelOccupancyGrid::queryAABB` → `OccupiedBox` (rigid-body physics);
- `MicroCanvas` (structure generation paints in micro cells).

These cover navigation, AI, spawners, water, MatSim, lighting/GI, debris contact and physics.

**Deeply coupled, would need rewriting under a storage change:**
1. the face mesher + `InstanceData` packing + 3 vertex shaders (+ committed `.spv`);
2. the blob codec (the disk format is the hierarchy; versioning exists, so migration is possible);
3. `DamageSystem` carving (~40 direct calls; cube → 27 sub → 27 micro);
4. raycast and placement (`VoxelLocation` leaks a `Chunk*` plus slot fields);
5. LOD tiers 2/3 (walk all three lists, persisted pyramids);
6. snapshots, undo, copy/paste and the voxel HTTP contract (C/S/M in the JSON);
7. structure placement's inline `z + y*32 + x*1024` indexing;
8. **16 files that iterate the public `chunks` / `chunkMap`** ✔;
9. **34 source files that name `Subcube` / `Microcube`** ✔.

All of them also assume 32³ chunks and 5-bit local coordinates. **Writes have no narrow interface:** generators,
flora, structures, damage and persistence call `Chunk::addCube / addSubcube / addMicrocube / subdivideAt` directly.

### A6. What any new way of drawing voxels must satisfy (contract, `LightingPipeline.md` + code)

- **Rendering:** forward shading, no G-buffer. Colour R16G16B16A16F linear; reverse-Z, infinite far.
- **Depth.** It must write true hit depth.
  - No shader in the engine writes `gl_FragDepth` today ✔. A ray-marcher would be the first, and loses early-Z
    unless it uses conservative depth.
  - Glass (OIT) and all four water shaders (refraction, absorption, shoreline, screen-space reflection) read the
    depth.
- **Shading.** It must reproduce `voxel.frag` per pixel:
  - face normal, texture, flags, tint, state;
  - shadow coordinates, PCSS on mid ∪ near, `phxSunGate`, `phxAmbient`;
  - visibility-traced point/spot lights, aerial perspective, debug views 1-19.
  - Best done by moving `voxel.frag`'s body into a shared include.
- **Textures.**
  - The sub-tile UV math reduces to a pure function of world position (a table per face).
  - **Mip trap:** a marcher's 2x2 pixel quad can straddle voxel edges, so it needs analytic texture gradients or
    every edge blurs.
- **Shadows.**
  - Cast into the near and mid cascades, depth-only.
  - Rasterizer depth bias likely does not apply to shader-written depth, so the bias must be reproduced by hand.
  - Both sides of the silhouette.
- **Occupancy.** Its matter must be packed into the occupancy pool, or it leaks light, is invisible to GI and to
  debris.
- **Gates.**
  - `lighting_doc_check` needs a receiver-matrix row; `shader_manifest`, `build_shaders.bat`, committed `.spv`;
  - `ambient_model_check`;
  - pixel gates against today's raster at the same pose.

### A7. Doc errors found (to fix separately)

- CLAUDE.md:
  - says `InstanceData` is 24 B with 3 lighting words (it is 16 B);
  - says GPU-driven multidraw is "default ON" (only the mid shadow cascade).
- `static_voxel.vert` says 8 B.
- `KinematicVoxelManager.h` says 40 B (48).
- LightingPipeline.md:
  - §5 says the moon is × skyVis² (the code is unsquared);
  - §2 says probe blend 0.30 (code 0.05).
- `ChunkVoxelStore.h:23` calls the store a mirror (it is the authority).
- Three different orders for the 3x3x3 index (shader, blob, mesher).

---

## Part B — What this means

1. **The cube/subcube/microcube design is a storage problem, not a rendering problem.**
   - Rendering of static voxels is good and measured.
   - The waste is that a microcube is a 136 B heap object with a string, against 3 B for a whole cube. That costs
     RAM (75 MB for one town's fine detail) and main-thread re-mesh time (string lookups, hash sets, per-voxel
     objects).
2. **The materials vision collides with this most directly.**
   - A Noita-like world means simulations write many fine voxels per tick into the world.
   - Today one edited chunk costs a 2-13 ms main-thread re-mesh, and every fine voxel is a heap allocation. MatSim
     works only because it lives in its own box beside the world.
   - A world that simulations can write needs: fine voxels as dense data (bytes in a brick, not objects); edits that
     dirty a brick, not a chunk; meshing (or drawing) that keeps up at simulation rates.
3. **Moving voxels are expensive because nothing moving is stored as a grid.**
   - Every moving path is per-face records; the only GPU-resident one is capped at 10 k.
   - Nothing moving enters the occupancy pool, so moving matter can't occlude light or be collided with.
4. **The narrow read interfaces are the good news.** Most of the engine asks "is this micro cell solid?" and already
   goes through ~6 world-coordinate functions. A storage change can sit behind them. The deep coupling is in about 9
   places, all listed in A5.

## Part C — Options, with drawbacks

| | What changes | Gain | Drawbacks / risk | Size |
|---|---|---|---|---|
| **0. Keep as is** | nothing | no risk | fine detail stays ~33,000x dearer per volume; simulations can't write the world; moving matter capped at 10 k pieces, outside the occupancy | — |
| **1. Dense fine storage** | Replace per-microcube heap objects with a per-cube record: a 729-bit occupancy mask + 1 B palette index per micro cell, only for cubes that hold fine detail. The same layout the GPU occupancy pool already uses. Keep every existing accessor (`getMicrocubeAt` etc.) on top. | RAM per microcube ~136 B → ~1-2 B (estimate); meshing without strings or hash sets; brick-level dirty marks become possible | the accessors that hand out `Microcube*` pointers (DamageSystem, raycast, editor) need a value or handle type instead; codec untouched at first; a lot of careful plumbing, but no visual change at all | medium |
| **2. One micro-resolution store** | Option 1 taken through: the world is micro cells in bricks, and "cube" / "subcube" are just bricks that are uniform. The hierarchy becomes compression, not structure. | removes the three-way split everywhere; tint/state for all sizes; the format a simulation can write | touches all 9 deep consumers + the disk format (versioned migration); a 6-index LOD/merge rethink | large |
| **3. March grids for moving matter** (rev 1's idea, now with the contract known) | Moving/simulated matter is held as GPU grids and drawn by ray-marching inside rasterized boxes, beside today's raster path | per-pixel cost, not per-voxel; no per-face records; sim grids drawn in place | first `gl_FragDepth` writer (early-Z); analytic gradients; manual shadow bias; must join the occupancy pool; reflection pass; lighting gates; overlap cost unknown | large, but isolated from the static path |
| **4. Re-architect everything** (new chunk size, all marched, no raster) | everything | — | rewrites lighting, shadows, 8 LOD tiers, grass/foliage, every gate; throws away a measured, working static renderer | **not recommended** |

Options 1 and 3 are independent: 1 is storage, 3 is drawing. Option 2 is where 1 leads if its data says go on.

## Part D — Recommendation

Three steps, each measured before the next:

1. **D1, measure the waste.** No code change: a probe of the engine-generated town (553 k microcubes).
   - Heap bytes per fine voxel by owner (objects, strings, map nodes).
   - Re-mesh ms per chunk, split by phase.
   - Exists partly in PerfProgram2026-09.md; redo on today's code.
2. **D2, Option 1 behind the existing accessors.**
   - **Exit test:** pixel-identical frames at fixed poses (debug tonemap), every test green, the blob codec
     round-trips unchanged.
   - **Exit numbers:** RAM and re-mesh ms on the same town.
   - **Kill:** less than a 5x RAM win, or no re-mesh improvement.
   - This has value even if nothing else is done, and it is the step that makes a simulation-writable world
     possible.
3. **D3, Option 3's first gate, in the MatSim lab only.**
   - Draw the device CA's grid by marching, against today's faces at the same pose.
   - Acceptance is the A6 contract.
   - **Kill:** slower than today's faces, or not matched after the gradient / shadow-bias work.

Option 2 and any static-path change are decided only with D1-D3 data in hand, in their own plan with the design-key
gate.

**What I am not proposing:** replacing the static renderer, changing the chunk size, or dropping the
cube / subcube / microcube API before the data says so.

## Decisions for the owner

1. Is the framing in Part B right: the hierarchy's cost is storage (RAM, re-mesh), not rendering?
2. Approve D1 (measure only)?
3. After D1: D2 (dense fine storage, no visual change) before D3 (marching moving matter), or the other way round?
