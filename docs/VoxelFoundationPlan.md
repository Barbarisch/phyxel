# Voxel Foundation Plan — one voxel size, GPU-first, with real fracture

**Status: PLAN, 2026-11-10. Nothing built.** Branch `feat/matsim-lab`, a moonshot branch: nothing is off the table.
Inputs:
- `MatterRepresentationReview.md` (four code audits of today's engine);
- `MaterialSimPlan.md` (the GPU cell simulation, measured);
- `DestructionSystemV2.md` (today's damage model);
- `CollisionProxyPlan.md`.

## 0. Owner decisions so far (2026-11-10)

> *"maybe the engine needs to do away with the cube/subcube/microcube arch ... standardize to one size of voxel ...
> the same size as the current microcube ... have chunks store the new 'microcube' only, and come up with
> optimizations around that, so that it becomes easy to render and store 'groups' of voxels."*
>
> *"if we can design this to be fully optimized for the gpu then we need to do this. so if 8x8x8 is better at a
> memory level then so be it."*
>
> *"we need to get way way smarter about how things break ... 'fracturing' along realistic crack lines that can
> result in 'pieces' being broken off of a bigger item even if that means breaking a 'block' in a few pieces."*

Decided:
- **D1: one voxel size**, replacing cube / subcube / microcube.
- **D2: designed for the GPU first.**
- **D3: 8x8x8 blocks.**
- **D4: fracture along crack lines into pieces**, not single voxels.
- **D5: the voxel is 1/8 m (12.5 cm)** (owner, 2026-11-10: *"yes go with 1/8 m"*). So 8 voxels = 1 block = 1 m,
  and a chunk is 32 m, the same as today. Chosen over 1/9 m because everything then nests in powers of two and
  lines up with whole metres. The cost is converting content built on thirds and ninths of a metre (§7a).

- **D6: resting pieces use the hybrid** (owner, 2026-11-10: *"go with C"*). A piece stays a body while it is near
  the player or recently moved, and is baked into the world grid when it is far away and the body budget is full
  (§6.7).
- **D7: no collapse under weight in v1** (owner: *"do your suggestion"*). v1 uses anchoring only. Structural load
  (§6.6) is decided after the owner sees the fracture-lab videos.
- **D8: level of detail is variable, with many steps** (owner: *"LOD should be variable with many many many
  steps"*). There is no single detail radius. The level is chosen continuously by size on screen, with every
  power-of-two level from 1/8 m to kilometres (§3.5a point 3).

Still open: none.

## 1. Words used in this plan

Engine terms where they exist; each new term defined once. **These are the settled terms (owner, 2026-11-10).**

**The three levels:**

| Level | Made of | Voxels per side | Voxels in total | Size |
|---|---|---|---|---|
| **voxel** | — | 1 | 1 | 1/8 m = 12.5 cm |
| **block** | 8 x 8 x 8 voxels = 512 voxels | 8 | 512 | 1 m |
| **chunk** | 32 x 32 x 32 blocks = 32,768 blocks | 256 | 16,777,216 | 32 m |

Below a world voxel, items have their own finer grid: an **item voxel** is 1/64 m (≈ 1.6 cm), an 8 x 8 x 8 split of
a world voxel.

| Term | Meaning |
|---|---|
| **voxel** | The one and only world voxel, 1/8 m. Has a material. |
| **block** | 8x8x8 voxels, exactly 1 m. The unit of storage, editing, drawing and simulation. Never visible to the player, like chunks. |
| **uniform block** | A block whose 512 voxels are all one material (air, rock, a plain wall). Stored as one entry. |
| **detailed block** | A block with mixed contents. Stored voxel by voxel. |
| **chunk** | 32x32x32 blocks, 32 m, the same size and coordinates as today's chunk. Unit of streaming, saving and generation. Never visible. |
| **item voxel** | 1/64 m, the grid items are built and broken on (§6.8). Replaces today's 1/81 m fine-voxel grid. |
| **brush** | How many voxels one place/break action covers: 1, 2, 4 or 8 voxels per side (12.5 cm, 25 cm, 50 cm, 1 m). |

**Retired terms:**
- **cube, subcube, microcube:** gone as storage. A 1 m brush does what placing a cube did. Subcube (1/3 m) has no
  exact equivalent on the 1/8 m grid.
- **MatSim "brick"** (its 8x8x8 cell group) is a **block**. "Brick" now means only the `Bricks` material or a
  building part.
- **MatSim "cell"** is a **voxel** once the simulation runs on the world grid (§5). "Cell" stays only for the lab's
  coarser test sizes.
- **"dynamic voxel", "debris", "kinematic group"** for things that move: all become **bodies** (below).

| Term | Meaning |
|---|---|
| **bond** | The connection between two voxels that share a face. Breaking things = cutting bonds. |
| **piece** | A set of voxels still bonded to each other after a break (a connected group). |
| **anchored** | A piece still bonded, through other voxels, to the ground or to something fixed. Anchored pieces stay put. |
| **body** | A piece that came loose. It moves as one rigid object: one position and one rotation for the whole piece, its voxels in a small grid of its own. |
| **crumbs** | Pieces too small to be worth a body (a few voxels). They become loose powder cells (sand-like, simulated by the cell rules) or short-lived particles. Never lost. |
| **crack pattern** | Where cracks run in a break: a set of surfaces in world space, chosen from the material and the hit. Bonds that a crack surface crosses are cut. |
| **part** | A building element the generator knows about (one brick, one plank, one beam). Voxels remember their part, so bonds between parts can be weaker, like mortar lines. |

## 2. Goals and non-goals

**Goals:**
1. Detail at 1/8 m anywhere, with storage as cheap as today's cubes where nothing is detailed.
2. **Today's look kept exactly** where content is the same. A 1 m stone block (8x8x8 voxels) looks like today's
   stone cube, pixel for pixel (§4.3). This holds for content on whole metres. Content on thirds is re-made (§7a), so
   its shape changes slightly and it is judged by eye, not by pixel diff.
3. Everything that changes (edits, simulation, breaking) works per block, on the GPU, without whole-chunk rebuilds
   on the main thread.
4. Breaking produces **pieces along crack lines** that depend on the material and the hit. Crumbs only where they
   make sense.
5. Matter is conserved: a break moves voxels, never deletes them. Tested exactly, like MatSim's ledger.
6. Scales to millions of voxels per scene and hundreds of simultaneous pieces. Budgets measured, not assumed.

**Non-goals (v1):**
- metal that bends;
- cracks smoother than a voxel (cracks follow voxel faces, so they are jagged at 12.5 cm);
- full structural stress analysis (§6.6 phases it in later);
- changing the lighting model (the new path must satisfy today's contract, `MatterRepresentationReview.md` A6).

## 3. Storage

### 3.1 Per voxel

| Channel | Size | When stored |
|---|---|---|
| material | 1 B (palette index per chunk; 2 B if a chunk ever needs > 255 materials) | always, in detailed blocks |
| tint | 3 B | only for blocks that use tint (a sparse channel) |
| state (wet, charred, burning, ...) | 1 B | sparse channel |
| damage | 1 B (accumulated damage toward breaking) | sparse channel, only in hit blocks |
| part id | 2 B (which brick / plank / beam) | sparse channel, only in generated structures |
| fill | 4 B (liquid / powder amount, as MatSim cells) | only in blocks holding liquid or powder |

A detailed block of plain material costs 512 B + a 64 B solidity mask (512 bits). Optional channels exist only in
blocks that need them.

### 3.2 Per block
- A **directory entry**, 8 B: uniform/detailed flag, the uniform material or a pointer into the detailed-block
  pool, which optional channels it has, and a "changed this tick" mark.
- A **solidity summary** for skipping empty space: lighting, physics and ray casts can skip a whole block in one test.

### 3.3 Chunk size
- 32 x 32 x 32 blocks = 256 voxels per side = 32 m, the same as today's chunk. Chunk coordinates, database keys,
  streaming distances and WorldForge's chunk arithmetic don't change.
- The directory is 32,768 entries x 8 B = 256 KB per resident chunk. A chunk of all air or all rock collapses to one
  entry, as today's store does.

### 3.4 Where it lives
- **The GPU holds the authoritative copy** for resident chunks: a pool of detailed blocks plus chunk directories.
  Edits, simulation and fracture run there.
- **The CPU keeps a mirror** for its own readers (physics, navigation, AI, saving). It is updated from the list of
  changed blocks each frame: an asynchronous read-back of only those blocks.
- The narrow read functions found by the audit answer from the mirror: `occupiedMicro`, `hasVoxelAt`, `queryAABB`,
  `stateAtMicro`, `phxOccupancySolid` on the GPU side.
- **Risk:** a CPU reader sees the world one or two frames late. Every reader must tolerate that, or ask the GPU
  directly. Audited per reader in phase F7.

### 3.5 Estimates (to be measured in F1)
- Terrain surface: ~1-3 detailed blocks per column, so ~1-3 k detailed blocks per 32 m chunk column ≈ 0.6-2 MB.
  Today: ~5.9 MB per surface chunk (Debug).
- One generated town's fine detail, 553 k microcubes = 75 MB today. Estimate: ~1-2 MB.

### 3.5a Scale: big worlds, memory bandwidth, still vs moving matter, physics

This section was added after owner review on 2026-11-10: *"memory bandwidth needs to be considered. static and
dynamic needs to be considered. physics needs to be considered."*

Machine context:
- RTX 4090: 24 GB, ~1 TB/s of video memory bandwidth.
- PCIe 4 x16: ~25 GB/s in practice.
- Today's residency: chunks load within 256 m and unload at 352 m. The far tiers reach 2-4 km (`LodTierLedger.md`).

All numbers below are **estimates** until F1 and F2 measure them.

**1. Four kinds of matter, and what each costs.**
Principle: **floats are paid per object, never per voxel.** The one exception is particles, which are capped and
short-lived.

| Kind | Examples | Stored as | Memory | Cost per frame |
|---|---|---|---|---|
| still grid matter | terrain, walls, built-in furniture | blocks | 0 B per voxel in uniform blocks, 1 B in detailed ones | drawing only; no simulation |
| grid matter that moves in place | water, sand, rubble, fire | the same blocks, plus a 4 B fill channel | +4 B per voxel, only where present | simulation, in proportion to awake blocks; sleeping blocks cost 0 |
| bodies | broken pieces, doors, moving furniture | one transform (~48 B) + its own packed blocks + faces built once | a 1 m³ piece ≈ 2 KB | physics, in proportion to the surface voxels of awake bodies; drawing |
| particles | spray, splashes, fast water | position + velocity in floats, ~32-48 B each | a capped budget | in proportion to the count; they return to the grid when they settle |

For comparison, today's debris costs 6 faces x 64 B = 384 B per moving voxel, with a 10 k cap. A 1 m³ piece
(512 voxels at 1/8 m) would cost 196 KB and 5 % of the cap. The estimate for the same piece as a body is ~2 KB.

**2. Memory capacity: how big a world fits.**
- **Dense storage is impossible.**
  - One chunk is 16.7 M voxels = 16.7 MB at 1 B each.
  - A 256 m radius is ≈ 200 chunk columns x ~3 surface chunks = 600 chunks ≈ 10 GB.
  - So collapsing uniform blocks is mandatory, not an optimisation.
- **With uniform blocks:**
  - A surface chunk is ≈ 2 k detailed blocks x 576 B ≈ 1.2 MB, plus a 256 KB directory.
  - 600 chunks ≈ 0.9 GB. Forests (leaves) and towns are more detailed, so budget 1-3 GB for today's 256 m radius.
  - That fits on 24 GB; the 8 GB tier is tight.
- **Measured in F0 on bare engine terrain** (`VoxFoundStorageLabTest`, 2026-11-10). Each area is one 32 x 32 m
  chunk footprint, from 8 m below the lowest ground to 8 m above the highest, generated by `WorldGenerator` and
  sampled at 1/8 m by `buildTerrainArea`:

  | Area | Height | Detailed blocks per block column | Estimated store | Dense | Smaller by |
  |---|---|---|---|---|---|
  | Perlin seed 7 (0,0) | 20 m | 1.06 | 0.79 MB | 10.5 MB | 13.3x |
  | Perlin seed 7 (512,-256) | 18 m | 0.98 | 0.73 MB | 9.4 MB | 13.0x |
  | Perlin seed 12345 (-1024,768) | 17 m | 0.86 | 0.64 MB | 8.9 MB | 13.8x |
  | Mountains seed 7 (0,0) | 29 m | 1.42 | 1.07 MB | 15.2 MB | 14.2x |
  | Mountains seed 7 (2048,1024) | 37 m | 2.25 | 1.63 MB | 19.4 MB | 11.9x |

  - The prediction was 1-3 detailed blocks per column. Bare terrain came in **lower** (0.86-2.25), so that prediction
    was wrong on the low side. The upper bound, which the budget relies on, held.
  - **Correction (2026-11-11):** the test binary runs in `build/tests`, where `WorldGenerator`'s CWD-relative
    `resources/biomes.json` load finds nothing, so the table above used the built-in biomes. Re-measured with the
    shipped biomes, bare terrain is about the same (Perlin s7 (0,0) 1.06; s12345 0.76; Mountains 1.42 / 2.21).
- **Measured with flora (2026-11-11, `MeasuredFloraCostOnShippedBiomes`).** Shipped `biomes.json`; the engine's
  `planFlora`; trees stamped from the `tree_forge --grid 8` library (generator output); area tall enough for the
  tallest stamped tree. Pooled plants with no grid-8 generator yet (gen_tree bushes, hand-made fern/apple/flower,
  "voxel matrix" shrubs) are not stamped, so undergrowth is a **lower bound**.

  | Area | Planned (stamped) | Detailed per column, bare → flora | Estimated store per chunk footprint |
  |---|---|---|---|
  | Perlin s7 (0,0) | 17 (8) | 1.06 → 2.08 | 0.91 → 1.51 MB |
  | Perlin s7 (512,-256) | 19 (10, all trees) | 0.97 → 2.27 | 0.84 → 1.61 MB |
  | Perlin s12345 (-1024,768) | 3 (2) | 0.76 → 0.76 | 0.71 MB |
  | Mountains s7 (0,0) | 17 (8) | 1.42 → 2.40 | 1.19 → 1.77 MB |
  | Mountains s7 (2048,1024) | 0 | 2.21 | 1.60 MB |
  | **Perlin s7 forest (3200,96)** | 127 (73) | 1.05 → **5.84** | 0.96 → **3.78 MB** |
  | **Perlin s7 forest (8160,96)** | 100 (66) | 1.03 → **6.04** | 0.95 → **3.90 MB** |

  - Prediction (written before the run): a footprint with ≥ 10 trees has 4-10 detailed blocks per column and
    ≤ 6 MB. **Held in both forests (59 and 65 trees). The lower bound was wrong** for the sparse footprint with
    exactly 10 trees (2.27 per column). The upper bounds, which the budget relies on, held everywhere and stay
    asserted. (The first run's gate also counted all stamped plants rather than trees; corrected to match the
    written prediction. It made no difference here: that footprint's 10 plants are all trees.)
  - Forest at ~3.9 MB per footprint over a 256 m radius (~200 footprints) is ~0.8 GB: inside the 1-3 GB budget,
    before undergrowth, structures and water.
  - At these numbers the directory is about 20 % of the store (164 KB of 0.79 MB). That supports measuring the
    two-level directory in F1.
- **The directory is large:** 256 KB per chunk x 600 = 150 MB of mostly uniform entries. A two-level directory (one
  entry per group of 4x4x4 blocks first) is measured in F1.
- **Full voxel detail cannot reach 1 km:** 16x the area is 15-50 GB. Beyond the detail radius the world is drawn
  from coarser levels (point 3).

**3. Levels of detail: variable, with many steps (D8).**
- **The levels.** A mip pyramid over the voxels, with one level per power of two:
  - 1/8, 1/4, 1/2 m inside a block, then 1 m (one value per block);
  - then 2, 4, 8, 16, 32 m inside a chunk;
  - then 64 m up to ~4 km above chunks, replacing today's separate far tiers where they can share it.
  - That is about 16 steps from 12.5 cm to 4 km. Each level holds 1/8 of the data of the level below, so the whole
    pyramid costs ~14 % more than the finest level alone.
  - Powers of two are the only steps that nest exactly: every coarse voxel is exactly 2x2x2 of the next finer level.
    Finer steps (for example 1.5x) would need resampling and break exactness. Smoothness between steps comes from
    blending instead (below).
- **No fixed distances.** The level is chosen per block region (never per chunk) by how big a voxel looks on screen,
  aiming at a target number of pixels per voxel. So the level adapts by itself to resolution, field of view and
  zoom.
- **Variable by budget.** The pixels-per-voxel target is the performance-tier knob. A controller raises it when GPU
  memory or frame time is over budget and lowers it when there is room. Detail fills whatever the machine can afford,
  and on a 24 GB card it reaches far.
- **No visible steps.** Neighbouring levels are cross-faded (dithered per pixel over a band, as tiers 5 and 8b already
  do), so a change of level never pops. Gates for each level: no holes where levels meet, and no "fattening".
  - Fattening means coarse levels drawing solids bigger than they are. It is one of the two reasons today's tier 2 is
    off; the other is grass.
- **Simulation and physics use their own radius, not the LOD.** Physics, liquids and fracture run only at full
  detail near characters. Far levels are only drawn.
- **What it replaces.** The pyramid replaces tier 2 (resident chunk LOD, currently off). It feeds tier 3 (far-LOD
  chunks) and the structure and tree proxies (tiers 5 and 7), which today build their own pyramids.
  `LodTierLedger.md` is updated when this is built.
- **The chunk rule holds:** LOD changes cost and how things look by distance, never by chunk.

**4. GPU memory bandwidth per frame.**
- **Bytes per face are not the bottleneck.** Today's measured 4-settlement scene has 270 k faces. At 12 B x 4 passes
  (main view + 3 shadow cascades) that is ≈ 13 MB per frame, ≈ 0.8 GB/s at 60 fps: under 0.1 % of 1 TB/s.
- **The number of faces is the bottleneck.** Measured today: 1.76 M unmerged faces ran at 27.5 fps; 270 k merged
  faces ran at ~140 fps.
  - Merging only inside a block (at most 8x8 voxels per face) would multiply faces on large flat surfaces.
  - Example: a flat 32 m wall is a few faces today, but at least 1,024 when merged only per block.
  - So **merging across blocks is required** (§4.1).
  - F2 gate (corrected 2026-11-11, owner): **a world that works today must still work.** Generate the same scene with
    the engine's generators and run it on the new store. Whole-frame time, GPU memory and bytes moved per frame must
    stay in the same performance tier. If it doesn't, find out why and fix it. Face count is one input to that cost,
    not the gate.
  - Keep today's optimizations in the new face record. Today a face is 16 B (`InstanceData`): a 15-bit position
    relative to its chunk (5+5+5), face bits, texture, flags, merge extents, tint. At 1/8 m a 32 m chunk is 256 voxels a
    side, so a chunk-relative position needs 8+8+8 bits. A block-relative one needs 3+3+3 bits plus a per-block base
    the shader looks up. §4.2 targets 8-12 B. Every render optimization today's renderer has must be rebuilt on the
    new store (§4.6).
- **Lighting and GI rays** read 1 bit per voxel (the 64 B solidity mask per block) and skip uniform blocks whole.
  Their cost follows the number of rays.
- **Simulation** reads 4 B per awake voxel per pass. Measured: 1.49 M awake cells stepped in 0.03-0.11 ms.

**5. Traffic between CPU and GPU (PCIe).**
- **Streaming in:**
  - Sprinting at 10 m/s with a 256 m radius brings in ~5 chunk columns per second ≈ 15 chunks ≈ 20 MB/s. That is
    small for PCIe.
  - **The real limit is generating 16.7 M voxels per chunk.** Generators must write whole uniform blocks directly,
    never voxel by voxel, or run on the GPU, in which case nothing is uploaded at all.
  - F1 measures chunks generated per second at 1/8 m against today.
- **CPU mirror read-back:** changed blocks only. A large explosion touching 10 k blocks ≈ 6 MB, sent once ≈ 0.25 ms of
  transfer, arriving 1-2 frames late.
- **CPU mirror size:** the CPU doesn't need the whole detail radius. Physics, navigation and AI only need the world
  near characters.
  - The mirror radius is its own knob, for example 64-128 m around the player and each active NPC group.
  - That is ≈ 1/4 to 1/16 of the GPU's copy.

**6. Physics at scale.**
- **Characters and navigation (CPU):**
  - Characters collide against the mirror at 1/8 m.
  - `NavGrid` already uses 1 m cells, which match blocks, so block solidity summaries can feed it directly.
- **Bodies (GPU):**
  - Cost follows the surface voxels of awake bodies. Sleeping bodies cost only drawing and memory.
  - Without a rest policy, every break adds bodies that never go away: a demolished town becomes thousands of bodies
    forever.
  - That makes §6.7's rest policy a scaling decision, not a cosmetic one.
- **Simulation radius:**
  - Liquids, powders and fire are stepped only inside an active radius (a tier knob).
  - Outside it, blocks sleep and their state is saved.
  - Far water that must keep flowing (rivers) comes from the existing hydrology bake, not the voxel simulation.
- **The anchoring search can blow up.** After a cut, "is this piece still connected to the ground?" can mean
  searching a whole town, because everything connects through the terrain. It is bounded like this:
  - search the block graph first, which is 512x smaller than the voxel graph;
  - stop as soon as the search reaches terrain or a block already marked anchored;
  - refine voxel by voxel only near the cut.
  - F4 measures the worst-case search size on the wall and column rigs, plus a whole-building rig.
- **Budgets:** today's 10 k debris cap becomes a body budget and a crumb budget, both tier knobs.

**7. Saving big worlds.** Only chunks that differ from what the deterministic generator would produce need saving.
An untouched chunk is regenerated from the seed.
- **Checked in F0: today's store mostly works this way.**
  - Streaming eviction (`ChunkStreamingManager.cpp:471-488`) and `save_world` (by default) save only dirty chunks.
  - Exceptions: `save_world {"all": true}` and the WorldForge checkpoint (`Application.cpp:15715`) call
    `saveAllChunks`, which saves every resident chunk, pristine ones included.
- **A saved chunk is stored whole and wins over the generator** from then on. Once saved, a chunk no longer follows
  generator changes. The new codec keeps this rule.

### 3.6 Saving
- Codec v3: per chunk, the directory, then detailed blocks run-length encoded. Optional channels follow.
- Old worlds: a converter from PXB2. A cube converts exactly to one 8x8x8 block. Subcubes and microcubes don't land
  on the 1/8 m grid and are resampled (§7a). Generated worlds are regenerated instead, which is exact.

## 4. Drawing

### 4.1 Faces built on the GPU, per changed block
- A compute pass turns each changed block into faces. Hidden faces are dropped, and same-material faces are merged
  inside the block (8x8 slices).
- A second pass merges across block borders within a chunk. **This is required:** face count drives frame time
  (§3.5a point 4), and per-block merging alone would multiply faces on large flat surfaces.
- Faces go into a GPU face pool and are drawn with GPU-driven indirect draws. No CPU meshing, no main-thread hitch.

### 4.2 Face record (target ≤ 8-12 B)
- block-local position 3+3+3 bits;
- face 3 bits;
- merge extents 3+3 bits;
- material / texture index;
- tint (sparse-channel index);
- flags.
The block's position comes from a per-block table, not from each face.

### 4.3 Today's look
- Texture coordinates become a pure function of world position. The audit showed today's cube / subcube / microcube
  UVs already reduce to one (`MatterRepresentationReview.md` A6), so an 8x8 patch of stone voxels shows today's 1 m
  stone tile exactly. The texture doesn't depend on voxel size.
- `varied` materials keep their per-cube hash, now per block, which is exactly today's cube, so the look is
  unchanged.
- **Gate:** per-pixel diff against today's render at the same poses (debug tonemap, LightingPipeline trap 1).

### 4.4 Lighting and shadows
- Unchanged contract (`MatterRepresentationReview.md` A6): forward shading, the shadow-caster rules, the occupancy
  for light visibility and GI.
- The GPU occupancy pool is built from block solidity masks, not from the CPU grid. Then **everything in the world
  grid, including what simulation moves, finally occludes light**.
- Bodies (§6.5) must also enter the occupancy pool, or loose pieces leak light. Today no moving thing does.

### 4.5 Later
Ray-marching blocks instead of faces stays an option, decided on data (`MatterRepresentationReview.md` Option 3).

### 4.6 Render optimizations: carry over or rebuild (owner, 2026-11-11)
The new store does not inherit today's optimizations for free. Every one below has to be rebuilt on blocks, or
consciously dropped with a measured reason. Sources: `RenderCoordinator.cpp` (culling levels), `LodTierLedger.md`
(8 tiers), `RenderOptimization.md` (culling + merge campaigns), `NearShadowCascade.md`.

| # | Today | Where today | On the new store | Status |
|---|---|---|---|---|
| 1 | Hidden-face culling (cube, subcube, microcube; across chunk borders) | `ChunkRenderManager`, RenderOptimization Phase 1 | Per block, from the 64 B solidity masks: a face survives only if the neighbour voxel is air. Block and chunk borders read the neighbour block's mask. Uniform blocks: all-solid interior blocks and all-air blocks emit nothing without a voxel loop | **GPU done inside one chunk** (F2.1, `FaceBuilderGpu`, exact vs CPU); uniform-block skip done (F2.6); chunk-border neighbours done (F2.7) |
| 2 | Greedy face merging (cube; sub/micro fine merge, default ON) | `ChunkRenderManager` `s_fineGreedyMerge` | §4.1: in-block, then across blocks. **Consequence for §4.2:** a cross-block quad is up to 256 voxels long, so the extents need 8+8 bits, not 3+3 | **GPU done** (F2.1): run merge across the whole chunk, one thread per slice row |
| 3 | Distance + frustum culling per chunk | `RenderCoordinator` LEVEL 1/2 | Same per chunk, plus per block AABB in the GPU draw-building pass | per chunk done (F2.9, measured: −0.6/−0.85 ms at ground poses, 0 px); per block owed with item 5 |
| 4 | Occlusion culling (chunk visibility graph, BFS bound 512) | `RenderCoordinator::applyOcclusionCulling` | Rebuilt from block solidity; can work at block granularity, finer than today | chunk walk on block-solidity connectivity done (F2.10, 0 px, no cost; the gate forest has nothing to occlude — win owed to a scene with interiors); block-granularity walk owed |
| 5 | GPU-driven multidraw (default ON) | NearShadowCascade / draw batching | The face pool is drawn with indirect draws from the start | main view done (F2.11: GPU cull pass 0.01 ms + one indirect multidraw, 0 px, neutral at 4-block clusters; per-block clusters measured worse); shadows owed (item 6) |
| 6 | Shadow cascades (near 40 / mid 420 / far 1600), caster culling to the light frustum, the 36-index shadow draw (M5) | `RenderCoordinator`, `ShadowMap.cpp` | Same cascades over the face pool. The M5 winding result still binds | cascades + 36-index casting done (F2.4); caster culling done (F2.12: today's per-chunk rule + the light frustum, default on, −0.40 ms where it drops chunks, 0 px); per-cluster light-frustum multidraw built and measured slower (+0.09..+0.36 ms), selectable, off |
| 7 | Distance tiers: far-LOD pyramid (tier 3), far terrain heightmap tiles (4), tree LOD meshes + cards (5, 6), structure proxies (7), grass/foliage (8a, 8b) | `LodTierLedger.md` | The block LOD pyramid replaces tier 2/3 for blocks. The §9a numbers say full-detail 1/8 m terrain and forest need it within tens of metres. Tiers 4-8 must keep their handoffs (residency gates) to the new near tier | pyramid levels 1/8 → 1 m built (F2.13: half-solid rule, surface material, chunk-independent; whole region at 1/4 m −3.2..−5.2 ms, at 1/2 m −4.4..−6.8 ms); selection by size on screen + dithered cross-fade done (F2.14: per fragment, 0 holes / 0 fattening; P=8 −3.3/−3.5 ms from above, neutral at the ground; default off); coarse casters built (F2.14b: one-level lag vs the main view, 0.055 % px; −1.2/−2.4 ms from above at P=8, +0.2..+1.2 ms elsewhere, default off); sprig → leaf-mass handoff built (F2.14c: no holes, but the mass looks wrong — cutout card texture on solid faces — and saves ≤ 0.7 ms; default off; mass appearance owed); levels > 1 m, GPU level build owed |
| 8 | Face direction culling (skip faces that point away from the camera as a group) | not done today (faces are camera-convention wound and back-face culled per triangle) | NEW: keep faces sorted by direction per block/chunk so up to 3 of 6 groups are skipped before the vertex stage | new, to be measured |
| 9 | Incremental rebuild | today a chunk rebuild costs 2-13 ms | Only changed blocks rebuild (F3 gate) | owed |
| 10 | Face pool memory (region arenas) | `ChunkArenaSystem` | Face pool sub-allocated per block / chunk | owed |

Known defect that gets worse with more merging: **T-junction cracks at merge borders** (`RenderOptimization.md:489`).
Cross-block merging creates more T-junctions, so F2 has to handle it, not inherit it.

**F2.1 built 2026-11-11 — the GPU face builder** (`voxfound/FaceBuilderGpu`, `shaders/voxfound_faces.comp`):
- Algorithm: **run merge** (`meshRuns`). Per slice row, maximal runs of one material; a run stacks onto the identical
  run (same start, width, material) in the previous row. Rows are independent, so one GPU thread per row with no
  ordering, and the result is a pure function of the voxels. Sequential greedy can't be split that way.
- Face record: the 8-byte `PackedFace` (chunk-relative 8+8+8-bit position, axis, direction, 8+8-bit extents,
  material, 8 bits free for tint/flags). Today's record is 16 B.
- Exactness: GPU = CPU reference as a set on hand rigs, random content at awkward sizes (1 voxel; sides 5, 7, 13, 17,
  33, 65, 256), both Outside modes, two engine-generated Perlin terrain windows, and a 64³ checkerboard (786,432
  faces, overflows the starting buffer, regrows). Red first: the CPU tests failed on the stub; 3 of 5 GPU tests failed
  on the stub (the other 2 compared an empty stub with an empty stub). Mutation check: `v > 0` → `v > 1` in the kernel's
  head test was caught by 2 tests.
- Measured: run merge gives **fewer** quads than greedy on Perlin s7 (1,763 vs 1,983). GPU time for a whole
  32 x 20 x 32 m window (256 x 160 x 256 voxels): 5.1 ms (s7), 1.7 ms (s12345). That is a naive kernel: every row
  reads every voxel byte from global memory, including all-air and all-solid blocks. Levers, in order: skip uniform
  blocks via the solidity masks, then rebuild only changed rows (item 9).

**F2.2 first light 2026-11-11 — 1/8 m voxels drawn through today's lighting** (`POST /api/debug/voxfound_preview`,
`shaders/voxfound_face.vert` + today's `voxel.frag`, `RenderPipeline::ensureVoxFoundPipeline` = the static pipeline's
state with a different vertex shader and instance layout, `RenderCoordinator::renderVoxFoundPreview` in the main scene
pass). Content provenance: the engine's `WorldGenerator` sampled at 1/8 m by `buildTerrainArea`, plus the engine's
flora plan stamped from `resources/templates_g8/nature`. Faces come from `FaceBuilderGpu`. Placed on a flat 3x2x3-chunk
lab world (Debug build, 1600x900, RTX 4090 rig).

| Scene (32 x 32 m) | voxels | solid | faces | GPU face build | GPU draw (median / p90, n=240) | whole GPU frame |
|---|---|---|---|---|---|---|
| Perlin s7 (0,0), bare | 256 x 64 x 256 | 2.29 M | 1,890 | 5.9-15.6 ms | 0.078 / 0.093 ms | 2.9 ms |
| Perlin s7 forest (8160,96) + flora (84 trees stamped, 16 names missing from the library) | 256 x 256 x 256 | 3.17 M | 80,552 | 2.4 ms | 0.32 / 0.68 ms | 2.46 ms |

The outer box faces are counted (`outside: air`), so these are dioramas. Eye check (`docs/evidence/voxfound_f22_*.png`):
- Terrace steps 1/8 m high across the slopes; grass top and side textures tile at 1 m across merged faces, as §4.3
  predicts; trunks with 1/8 m stepped root flares.
- Not there yet, as expected: the faces cast no shadows (the shadow passes draw chunk instances only, §4.6 item 6);
  no grass blades (§4.6 item 7, that tier reads chunk voxels); leaves were drawn as raw leaf voxels — WRONG, fixed in
  F2.3 (leaves are cards).
- Observed, not explained yet:
  - (corrected 2026-11-12) the earlier "near-black trunk faces" claim was overstated. In
    `voxfound_f22_forest_closeup.png` the right trunk's root flare and the trunk at the right edge are dark but
    textured — consistent with ordinary sun-facing-away faces lit by ambient only. No pixel values were read and no
    today's-renderer control was captured, so neither "near-black" nor the §4.4 occupancy cause is established.
    Check: same tree, same pose, today's path vs preview, compare pixel values on those faces.
  - thin dark horizontal slits across a bush — EXPLAINED in F2.3: F2.2 drew leaf voxels as solid cubes, but the
    engine never draws leaves as cubes (leaf cards, below). The slits were the leaf cubes' own faces.
- Draw instance was 12 B in this stopgap (PackedFace + a host-looked-up texture word). Done in F2.5: the draw instance is
  the 8 B record and the texture comes from a table on the device.

**F2.3 2026-11-12 — leaves are today's leaf CARDS, not voxels.** My F2.2 mistake: I drew leaf voxels as solid faces
without reading how the engine draws leaves. How today works (`ChunkRenderManager.cpp` ~960/1396/1561/1695,
`FoliageRenderPipeline`, `foliage.vert/.frag`): materials with `billboarded` (Leaf, LeafBirch, LeafSpruce, LeafJungle,
LeafAutumn) stay voxels in the store and in occupancy (they hide the wood inside a canopy) but **emit no faces**; each
EXPOSED leaf cell emits one 8 B `FoliageInstanceData`, which `foliage.vert` fans into `cardsPerVoxel` (5) cutout cards
in hashed 3-D orientations, card size and jitter in metres; `foliage.frag` carves a leaf silhouette and lights it with
the shadow map (the canopy self-shadows through the foliage shadow pass) + the probe ambient (`phxAmbient`, which traces
the micro occupancy). At distance (chunk LOD ≥ 1) the cards hand off to solid coarse leaf mass
(`RenderCoordinator::renderFoliage`); the new LOD tiers must keep that handoff (§4.6 item 7).
- Built: `CardSet` + card-aware meshers (`Mesher.h`: a card voxel draws no faces but still hides its neighbours);
  `foliageSprigs` (exposed card cells, thinned by today's `foliageCellHash01` on the ABSOLUTE 1/8 m cell → seam-free);
  the GPU face builder's 4th pass (axis-3 records in the same 8 B buffer); `shaders/voxfound_foliage.vert` (= `foliage.vert`
  with a 1/8 m position decode, same push constants/layout/`foliage.frag`); `FoliageRenderPipeline::renderVoxFound`;
  preview `leaves: "cards"|"voxels"`, `sprigs_per_m2`, `templates:[{name,x,z}]` (stamp a named 1/8 m library tree);
  `POST /api/debug/foliage` now also reports today's `foliage_instances` + `foliage_distinct_sprigs`.
- Red/green: 6 new CPU tests failed on stubs, then pass; 2 new GPU tests (random content with cards {2,255}, negative
  world origin, both thresholds, both Outside modes; the F2.2 forest with flora) equal the CPU reference exactly.
  Mutation: dropping `isCard` from the kernel's `faceAt` is caught (leaf faces appear only on the GPU).
- **Density is NOT today's 0.25 knob.** Measured on `forge_oak_m` (same tree in both libraries), live: today emits
  4,536 card instances = **3,847 distinct sprigs**; the gap is micro leaves, which emit one instance each, UNTHINNED, at
  their parent subcube centre with the same hash seed — identical overlapping cards (a waste today, worth fixing on its
  own). My first metric definition (0.25 per (1/3 m)² → 2.25/m², `kSprigsPerM2`) gives 1,152 sprigs on the 1/8 m oak;
  7.5/m² gives 3,772 ≈ today's distinct count. Eye check at one pose: both densities read as a full canopy, so the
  visible difference is not density. Default stays 2.25/m² until the lit comparison below decides it.
- Forest (F2.2 spot, 84 trees): solid faces 78,780 → **35,861** with leaves as cards, + 7,484 sprigs (8 B each).
- Eye check (`docs/evidence/voxfound_f23_oak_*_vs_today.png`, today's grid-3 oak left, 1/8 m oak right, same pose):
  with `leaves:"voxels"` the 1/8 m canopy shows the blocky slits; with cards the silhouette matches today's. **Still
  different: the new canopy is flat pale green, today's has dark lobes.** Cause, from `foliage.frag`: today's canopy is
  in the shadow map (foliage shadow pass) and in the micro occupancy the ambient traces; the preview's leaves are in
  neither, so every card gets full sun and full-sky ambient. That is §4.6 item 6 + §4.4, now visible.
- NEXT (done in F2.4 below): shadow casting, then re-capture with pixel values, settle the density.

**F2.4 2026-11-12 — the preview casts shadows; sprig density settled by measurement.** (§4.6 item 6, partly.)
- Built: `voxfound_face_shadow.vert` + `ShadowMap::ensureVoxFoundPipeline` (the chunk caster's layout and state, the
  36-index both-windings draw under back cull, M5) — faces cast into mid ∪ near like chunks; `voxfound_foliage_shadow.vert`
  (`foliage_shadow.vert` with the 1/8 m decode, diff = decode only) + `FoliageRenderPipeline::renderShadowVoxFound` —
  sprigs cast into mid like today's cards. Preview `{"stats":true}` reports what the last frame drew in each pass;
  `{"cast":{"faces":bool,"sprigs":bool}}` switches casters for diagnosis. `LightingPipeline.md` §0.2 + §9 updated.
- Caster matrix, oak, fixed pose (today's control constant at mean 166.7 in all 8 captures): the 1/8 m canopy p50
  is 209 with no casters or faces only, 50 with sprigs only or both — **all of the canopy shading comes from the
  sprigs**, none from the solid faces, and density (1,152 vs 3,772 sprigs) barely moves it.
- **Trap I fell into:** the first A/B pose (camera at x 98 between the trees) saw today's oak from its right and the
  new oak from its left, so one showed its sunlit side and the other its shadowed side: "new canopy too dark (p50 50
  vs 180)" was the POSE. Fixed rig (`f24_species.py`): each tree from the SAME pose relative to its own trunk centre
  (height 17 + 0.67 H, distance 1.73 H), canopy-only box. Read A/B numbers only from that rig.
- Same-relative-pose results (canopy luma: pixels / mean / p50):

  | Tree | today (grid 3) | 1/8 m @ 2.25 | @ 4.5 | @ 7.5 |
  |---|---|---|---|---|
  | oak_m | 58,216 / 130.6 / 130.5 | 55,032 / 128.9 / 113.4 | **57,631 / 131.9 / 129.5** | 58,996 / 134.4 / 144.5 |
  | birch_m | 48,578 / 158.4 / 187.1 | 44,154 / 158.2 / 186.0 | **47,657 / 160.5 / 189.0** | 49,017 / 163.3 / 192.0 |
  | spruce_m | 32,080 / 133.0 / 155.0 | 24,738 / 118.2 / 134.1 | 26,968 / 122.6 / 143.1 | 28,023 / 126.1 / 146.4 |

  **`kSprigsPerM2` = 4.5** (was the derived 2.25). Spruce stays short at every density: its 1/8 m template is a
  smaller tree (6.5 x 6.25 m footprint vs 8 x 7 m on grid 3) — a template-generation difference to fix in the
  library (§7a), not in the cards.
- Not done, and not needed for this match: the preview's voxels are still NOT in the occupancy the ambient reads
  (`phxAmbient` sees open sky around them). The canopy matched without it; it will matter for interiors and dense
  cover, and the real fix is ambient over the new store (§4.4), not resampling 1/8 m into today's 1/9 m pool.
- Forest (F2.2 spot, 84 trees, `voxfound_f24_forest_cards_shadows.png`): 36,778 solid faces + 15,579 sprigs, all
  drawn in the main, mid, near (faces) and mid (sprigs) passes; dappled canopy shadows on ground and trunks.
- Tests: CPU 7/7 + GPU 7/7 on rebuilt exes (forest sprigs 14,716 at 4.5/m², GPU = CPU exactly).

**F2.5 2026-11-12 — the draw instance is the 8 B face record.** (§4.2, §4.6.)
- `voxfound_face.vert` reads the `PackedFace` as-is (binding 1, stride 8, locations 1-2). The texture word
  (index | flags << 16) comes from `faceTex[material * 6 + faceID]`, a storage buffer at **set 1** of the preview's own
  pipeline layout (`RenderPipeline::ensureVoxFoundPipeline`: set 0 = the shared scene set, set 1 = the table, the static
  push-constant range). Set 0 stays bound with the static layout (compatible); only set 1 is bound with the new one
  (`bindVoxFoundFaceTex`). The shared set 0 was NOT grown: it serves every scene shader and is rewritten per frame.
- `material` is the area's PALETTE index, so the table is per palette: 256 × 6 words = 6 KB, always full size, so no
  8-bit material reads past it. The buffer is made once and never reallocated (its descriptor is written once, so a
  recycled handle cannot alias it). Per-chunk palettes later: one buffer of palettes and a per-chunk base (a dynamic
  storage-buffer offset avoids touching the push range).
- The shadow caster drops the unused third attribute (stride 8).
- Bytes: forest with cards 36,778 faces → 294,224 B drawn (was 441,336); leaves as voxels 80,552 → 644,416 B (was
  966,624). Plus the 6 KB table.
- Check (`rec8_shot.py`, seed-7 forest at (8160, 96), camera (116, 26, 58) yaw -90 pitch -22, leaves as voxels so
  nothing in the box sways): box x 500-1196, y 45-670 of the 1600 × 900 frame.
  - Control (two shots, same 12 B build): 0 differing px. (The left of the viewport is lab-world grass in the wind.)
  - 12 B build vs 8 B build: **0 differing px.** All 80,552 faces drawn in main, mid and near (`stats`).
  - Mutation (shader reads face slot `faceID ^ 1`): 36.6% of the box differs, so the comparison can fail.
  - Cards mode (sprigs sway): 14.4% differs before vs after against a 14.9% same-build control; by eye identical.

**F2.6 2026-11-12 — uniform-block skip in the face builder.** (§4.6 item 1, the in-chunk half.)
- A block pass (`voxfound_faces.comp` job 4, one invocation per 8³ block, before the row passes) writes two flags per
  block over its in-grid voxels: EMITS NOTHING (every voxel air or a card material) and HIDES EVERYTHING (no air).
  A face at p exists only if p is solid, not a card, and q across it is air, so a row jumps to the end of p's block
  when p's block emits nothing or q's block (q in the grid) hides everything. Output is unchanged; the CPU reference
  is untouched. `FaceBuilderGpu::setUniformBlockSkip` (default on) / `lastSkippedSpans`.
- Flags are computed on the device from the voxels, not kept as stored solidity masks yet (§3 storage keeps them
  later; the kernel only needs the two bits).
- Exactness: every GPU test now runs with the skip on (9/9). New rig `UniformBlockSkipSkipsAndStaysExact`: 37×70×19
  (all far-edge blocks partial), stone / random band / air, a card block in air and one touching stone, lone air and
  stone voxels on block corners, both Outside modes, with and without cards; skip off reports 0 spans, on > 0.
  Red first: stub `lastSkippedSpans` = 0 failed the 4 skip-on cases.
- Mutations: "hides everything" ignoring air → 13,166 faces missing (caught). A block pass reading only 7 layers per
  block PASSED the first rig; the corner voxels were added for it and now catch it (6 faces of the corner stone
  voxel missing).
- Device time, best of 5 (`UniformBlockSkipTiming`): terrain 256×160×256 1.064 → 0.641 ms (7.48 M spans skipped);
  forest 256³ 0.914 → 0.391 ms (11.4 M spans). Same records both ways. Includes the block pass.
- Live: the forest preview (leaves as voxels) with the skip on: 80,552 faces, 0 differing px against the F2.5 shot.
- CAVEAT (found in F2.7): absolute device times move several-fold between sessions on this machine. Later the same
  day the F2.6 kernel measured terrain 3.0 → 2.4 ms and forest 0.98-1.47 → 0.43-0.81 ms on the same exe. Only
  on/off pairs from ONE run compare; the skip's win held in every run (terrain ~20-25%, forest ~45-60%).

**F2.7 2026-11-12 — chunk borders: border faces are decided by the real neighbour voxels.** (§4.6 item 1, done.)
- `Neighbours` (Mesher.h): the solidity of the one-voxel layer outside each grid face, one bit per voxel, 6 planes,
  ≤ 48 KB per chunk. Only solidity: a border voxel never emits a face in this chunk, it only hides or exposes ours
  (cards count as solid, as everywhere). `Edges` is built implicitly from an `Outside` mode or from `Neighbours`, so
  every mesher, `foliageSprigs`, `coverageErrors` and `FaceBuilderGpu::build` take either; all-air planes reproduce
  Outside::Air and all-solid planes Outside::Skip exactly (test). `subGrid` / `neighboursOf` cut a chunk and its
  planes out of a larger grid. GPU: the planes go to binding 4 (`dim.w == 2`); a plane set sized for another grid is
  refused.
- The CLAUDE.md rule "chunks must not be visible", pinned: the region meshed as chunks (each with its Neighbours) has
  EXACTLY the unit faces and sprigs of the region meshed whole. Merging can differ at chunk borders (a quad cannot
  cross one), which is cost, not looks, so the comparison is per unit face.
  - CPU (`ChunksAreInvisible`): a 50×40×37 world (3-material heightfield, caves, leaf voxels, a floating slab across
    borders) in 16³ chunks with remainders, beyond-region air and skip: 11,068 / 6,555 unit faces, equal. Without
    neighbours (the F2.1 build per chunk): 17,752 / 6,277 — the borders were visible.
  - GPU (`ChunksAreInvisible`): the seed-7 forest in 96³ chunks (27 chunks, 147,391 unit faces, 14,716 sprigs) and a
    70×45×33 synthetic world in 24×16×20 chunks, beyond air and skip: union of the GPU chunk builds == the whole
    region (CPU, no chunks), and each chunk GPU == CPU with the same Neighbours.
  - Red first: with the neighbours ignored (stubs) both CPU tests and the GPU test failed (forest sprigs 16,894 vs
    14,716: border leaf cells wrongly exposed).
- Cost: the kernel reads the planes only for voxels on the grid border; same-exe timing of the F2.6 kernel vs this
  one: terrain 2.40-2.42 vs 2.55 ms, forest 0.43-0.81 vs 0.50 ms (within the run-to-run spread).
- Not yet: the live preview still builds one area as one grid; chunked building (and where the planes come from in
  a streaming world: the neighbour chunk's store, or "unknown = solid" until it streams in) lands with the
  whole-frame A/B.

**F2.8 2026-11-13 — the first F2 gate run: the same generated scene, today's store vs the 1/8 m store, whole frame.**
- Built:
  - `VoxFound::forEachRegionChunk` (StorageLab.h): a region of whole 32 m engine chunks, generated one chunk at a time
    (terrain + flora over the chunk and a 1 m margin), each with its border solidity (`Neighbours`). One region palette.
  - The preview draws a list of chunk ranges, each at its own origin, in the main pass and in the shadow casters
    (`Preview::chunks`). `voxfound_preview {"region":{"from":[cx,cy,cz],"to":[...]}}`.
  - A/B switches in one session: `{"hide_world":true}` stops today's world drawing and casting (chunks, cards,
    grass); `{"show_preview":false}` keeps the preview on the device but undrawn.
- "Chunks must not be visible", pinned on generation (`RegionChunksEqualTheRegionGeneratedWhole`): the forest
  (8 chunks, 142 trees) generated chunk by chunk equals it generated whole, voxel for voxel and border for border.
  Red first: trunk cells (LogJungle) where the whole region has air, and 64,415 of 1,572,864 border bits wrong — trees
  sat at a height that depended on the chunk. `stampFlora` read the ground by scanning the area's
  own column (clamped at the area edge, after earlier trees were stamped). It now takes the ground from the terrain
  function at that world column, and stamps overlapping trees in a fixed world order.
- Same generator on both sides: the preview applies the loaded world's stored recipe (as `GameDefinitionLoader`
  does) and plants inside today's bounded window (`decorateFlora`, 8-column inset; `planFlora(rect, inset)` is exactly
  the plants inside the inset rect). Check: 166 planned plants on both sides. Content gap, owed to the library (§7a):
  `fern`, `shrub_flat`, `tree_apple` have no 1/8 m template, so those plants are missing on the new side.
- Two harness traps found and fixed on the way (both made earlier pose checks unreliable):
  - The editor launched WITHOUT `--project` stays on the project launcher. Then `update()` never runs: API and
    rendering work, but the camera sync and simulation do not. That was the "`/api/camera` desync". Measure with a
    project (here `VoxFoundGate`, whose `game.json` is the scene).
  - `GET /api/camera` returned the input manager's camera, not the render camera, so `perf_harness.py`'s pose
    read-back could pass for a pose the frame was not drawn from. It now returns the render camera (input values as
    `input_*`).
- **The run** (Release, RTX 4090, 1600 × 900, IMMEDIATE present). Scene: `VoxFoundGate` game.json, WorldGenerator
  Perlin s7 with flora, static region chunks x 254..257, y 0..1, z 2..5 (128 × 128 m forest). A = today's store
  (`GameDefinitionLoader`); B = the same chunks at 1/8 m (480,349 faces + 225,471 sprigs, built in 24.6 s, 41 ms of
  GPU face building), today's world hidden. `perf_harness.py`, 8 interleaved pairs × 240 GPU frames per pose,
  pose read-back verified. Evidence `docs/evidence/voxfound_f28_*`.

  | Pose | today (A) | 1/8 m (B) | B − A (95% CI) |
  |---|---|---|---|
  | overview_south (8192, 70, 230) | 7.09 ms | 7.88 ms | +0.75 [+0.70, +1.26] |
  | top_down (8192, 140, 128) | 7.14 | 7.81 | +0.65 [+0.46, +0.88] |
  | forest_floor_open (8200, 29.5, 150) | 47.95 | 43.84 | −4.07 [−4.32, −3.89] |
  | east_low (8275, 35, 128) | 45.48 | 42.24 | −3.20 [−3.26, −2.93] |
  | canopy_close (8160, 38, 100) | 29.83 | 25.13 | −4.74 [−4.97, −4.51] |

  (A first `forest_floor` pose had the camera inside a trunk in both stores; dropped, replaced by `forest_floor_open`.)
- **Same tier at all 5 poses.** Where the new store costs more, why (per-scope medians):
  - Its own faces: main view 0.3-5.0 ms vs today's chunk geometry 0.12-2.1 ms (+ today's depth prepass 0.2-0.35).
    No frustum culling yet (§4.6 item 3) and no depth prepass: all 480k faces go through the vertex stage every frame.
  - Its shadows: +1.7-2.2 ms. All 480k faces are drawn into mid AND near, with no caster culling (item 6).
  - The near poses are dominated by LEAF CARDS in both stores: today's cards 16-35 ms, the new sprigs 9-29 ms. That is
    why both run at 20-35 fps there. Today's card count is 421,848 instances for 389,092 distinct sprigs (the micro-leaf
    duplicates, F2.3); the new side draws 225,471.
  - Not like for like, in B's favour: no grass blades (today 0.2-1.3 ms) and the three missing plant templates.
- Memory and bytes: today 78,299 chunk quads × 16 B = 1.25 MB + 421,848 cards × 8 B = 3.37 MB; new 480,349 faces × 8 B
  = 3.84 MB + 225,471 sprigs × 8 B = 1.80 MB + 6 KB table (1.22× today). Faces are 6.1× today's quads here (the §9a
  proxy predicted far more; today's trees are already 1/9 m). The preview's buffers are host-visible (total GPU memory
  moved 2 MiB), so a device-local face pool may change its draw cost; owed with item 10. Per frame the new store reads
  every record in every pass (faces × 3 passes + sprigs × 2 = 15.1 MB); today culls per chunk.
- Next levers, in order of the measured cost: frustum culling per chunk/block for the faces (item 3) and caster culling
  for the shadows (item 6), then the depth prepass; then the scene grows (streaming radius, settlements).

**F2.9 2026-11-13 — §4.6 item 3, the per-chunk part: today's distance + frustum test on the preview's chunks.**
- Built: `RenderCoordinator::voxFoundVisibleRanges` = today's LEVEL 1 + 2 (`renderStaticGeometry`): inclusion
  distance from the chunk centre, then the camera frustum against the chunk's 32 m box, before the main-view face and
  sprig draws. Shadow casters are not culled yet (item 6). `voxfound_preview {"chunk_cull":bool}` (default on),
  stats `ranges_drawn` / `ranges_culled`.
- Prediction written before the run: overview poses change ≤ 0.1 ms; ground poses: face + sprig scopes −30-60%;
  0 px change.
- Pixels (same pose, today's world hidden, wind 0, GI probes off — the probes re-trace stochastically every frame, which
  alone changed 22-58% of pixels by 1-14 levels between two shots): control 0 px, cull on vs off **0 px** at
  forest_floor_open (12 of 31 chunks culled) and canopy_close (10 culled). Red check, done AFTER green (my order was
  wrong): the box shifted one chunk in x changes 45% of the pixels at forest_floor_open (control 0 px); canopy_close
  did not see that mutant.
- Whole frame, cull off vs on (Release, perf_harness 8 pairs × 240 frames, `docs/evidence/voxfound_f29_ab.jsonl`):

  | Pose | off | on | on − off (95% CI) |
  |---|---|---|---|
  | overview_south | 8.03 ms | 7.98 | −0.05 [−0.16, +0.05] |
  | top_down | 7.83 | 7.75 | −0.12 [−0.19, +0.14] |
  | forest_floor_open | 44.87 | 44.34 | −0.57 [−0.83, −0.28] |
  | east_low | 42.74 | 42.67 | −0.04 [−0.23, +0.02] |
  | canopy_close | 26.04 | 24.83 | −0.85 [−1.56, −0.61] |

- **The prediction was wrong at the ground poses.** Culling 20 % / 33 % of the faces cut the face scope by 7 % / 14 %
  (4.99 → 4.66 ms, 4.95 → 4.25 ms) and the sprigs by 0.5 % / 6 %. Faces behind the camera were already cheap: they
  are clipped right after the vertex stage. The cost is in what IS in view (overdraw and fragment work), so the next
  levers for the main view are the depth prepass (today has one, the preview not) and occlusion (item 4), not more
  frustum precision. In a scene this small (31 chunks, all within ~180 m), chunk culling has little to remove; it
  scales with the scene, which is why it stays on.
- Not done in item 3: the per-block AABB test in a GPU draw-building pass. That pass is item 5 (indirect draws) — they
  are built together.

**F2.10 2026-11-13 — §4.6 item 4: occlusion, rebuilt from block solidity.**
- Built (Mesher.h): `blockFaceConnectivity` = today's `Chunk::computeVisibilityMask` (which chunk faces see each
  other through the chunk) computed over 8³ BLOCKS: a block blocks sight only if all 512 voxels are solid and not
  see-through (leaf cards, materials with alpha < 0.99, today's rule); flood over the passing blocks. Block level is
  conservative (one air voxel opens its block), so it can only connect more than voxels would, never cut a view.
  `occlusionReach` = today's walk (`applyOcclusionCulling`): breadth-first from the camera's chunk, same 512 m bound,
  same 20,000-node budget (over budget keeps everything), absent / air chunks pass sight. Each region chunk stores its
  connectivity (`PreviewChunk::connect`), empty chunks included (a solid chunk with no faces still blocks); the walk
  runs after the frustum test. `voxfound_preview {"occlusion":bool}` (default on, like today), stats `ranges_occluded`.
- Red first: both new tests (`BlockConnectivityFollowsTodaysRule`: all air, all solid, a one-block wall, a wall with
  one air voxel, a see-through wall; `OcclusionWalkHidesWhatIsBehindASolidChunk`: solid / open / detour / one-way /
  over budget) failed on stubs that connect everything; green after: mesher tests 21/21.
- Prediction written before the run: 0 chunks occluded on the gate forest at all 5 poses (sight crosses the open
  forest through sky chunks; no chunk is solid through). Measured: 0 occluded everywhere, `chunks_fully_blocking` 0,
  0 px on vs off (control 0 px), whole frame within noise at all 5 poses (largest |median| 0.12 ms, every CI includes
  0; `docs/evidence/voxfound_f210_ab.jsonl`).
- So item 4 is built and costs nothing measurable, but this scene cannot show a win. Today's occlusion only wins in
  caves and interiors, and the 1/8 m generators can't make either yet: `buildTerrainArea` is a heightfield (no Caves
  type), and structures at 1/8 m are FC work (§7a.3). Its win is measured when the gate scene grows a settlement.
- The on-screen overdraw F2.9 found (trees in front of trees) is finer than any chunk test. That is the block-level
  part of items 3/4, in the GPU draw-building pass (item 5), and the depth prepass.

**F2.11 2026-11-13 — §4.6 item 5 (indirect draws) with item 3's per-block part: the GPU draw-building pass.**
- Built:
  - `clusterFaces` (Mesher.h) groups each chunk's faces by the cell of `clusterBlocks`³ blocks holding their origin
    voxel, each with the exact union box (`faceBox`). `boxInFrustum` is the CPU reference test.
  - `ClusterCullGpu` + `shaders/voxfound_cull.comp`: one indirect command per cluster; each frame one thread per
    cluster writes its `instanceCount` (face count, or 0 when its chunk was rejected by the CPU's F2.9/F2.10 decision or
    its box is outside the frustum). Boxes and planes are relative to the region's anchor (small floats).
  - `shaders/voxfound_face_mdi.vert`: the face shader with the chunk origin per draw (`gl_DrawIDARB` → a static table
    of whole-metre origins; camera split into whole metres + rest, so nothing large is subtracted in float).
  - ONE `vkCmdDrawIndexedIndirect` over all clusters. No indirect-count: `VulkanDevice` only checks the 1.2 API
    version and never enables the `drawIndirectCount` feature, so culled clusters are zero-instance commands.
  - `voxfound_preview {"mdi":bool}` (default on), `"cluster_blocks"` at build (default 4, below), stats `clusters`,
    `clusters_drawn`, `cluster_faces_drawn`. Shadow casters still draw per chunk (item 6).
- Red first: `FaceBoxMatchesTheDrawnQuad`, `ClustersKeepEveryFaceAndBoundIt`, `BoxInFrustumIsThePVertexTest` failed on
  stubs; `ClusterCullEqualsTheReference` (GPU, 5,000 random clusters, 7-chunk mask, two real cameras) failed with the
  dispatch stubbed out (3,623 / 3,055 commands wrong). Green: 0 wrong, 0 within 1 mm of a plane; GPU tests 12/12.
  (My first cluster-test area sat inside solid rock — 6 faces — a test-input error, fixed to span the ground.)
- Pixels (wind 0, GI probes off): MDI vs per-chunk draws 0 px at all 5 poses (control 0 px), 4-block clusters.
- Measured on the gate scene (Release, perf_harness 8 pairs × 5 poses each; `docs/evidence/voxfound_f211_*`):
  - First version, 1-block clusters, buffers HOST-VISIBLE: +0.8 to +2.1 ms. The cull scope read ~1 ms. Two causes,
    both fixed: the cluster and command buffers lived in host memory, and a global barrier before the dispatch made
    it wait for every earlier indirect draw (the mid-shadow multidraw). After: the pass costs **0.008-0.010 ms**.
  - Device-local, by cluster size (MDI − per-chunk, whole frame):

    | cluster | clusters | faces drawn at canopy_close (vs 321,539 per-chunk) | whole frame |
    |---|---|---|---|
    | 1 block (the plan's unit) | 26,090 | 167,303 | +0.2 to +1.0 ms, worse at all 5 poses |
    | 2 blocks | 8,807 | — | −0.06 to +0.26 ms |
    | 4 blocks | 2,385 | 189,602 | −0.10 to +0.06 ms, neutral (3 CIs include 0) |

  - So per-block culling removes up to 48 % of the faces and still loses: the per-command cost of 26k tiny draws
    exceeds what the dropped faces cost. Same lesson as F2.9: faces outside the view were already cheap; the time is in
    what is on screen. **Default 4-block clusters**: no cost, the GPU-driven structure the plan wants from the start
    (item 5), and the base for the shadow-caster culling next (item 6). Per-block (1) stays selectable for re-measuring
    when the scene or the GPU changes.

**F2.12 2026-11-13 — §4.6 item 6: shadow-caster culling of the preview.**
- Built:
  - `clipPlanesZeroToOne` (Mesher.h): the six planes of a zero-to-one clip matrix (a cascade's ortho light matrix,
    which maps CAMERA-RELATIVE world) moved into any frame, here the region's anchor (small floats).
    `casterBoxKept` = today's chunk caster rule (centre within the cascade's fitted radius + caster margin: 160 mid,
    48 near) AND the box inside the light frustum.
  - Per chunk (`voxfound_preview {"shadow_cull":bool}`, default on): faces into mid and near, sprigs into mid (plus
    today's card rule, the foliage radius from the camera; box +2 m for card overhang).
  - Per cluster (`{"shadow_mdi":bool}`): the F2.11 cluster pass run once per cascade with the light planes and the
    per-chunk decision as its mask (36-index commands, M5), then one indirect multidraw per cascade.
    `voxfound_face_shadow.vert` reads the per-draw chunk origin (relative to the anchor) from the shadow layout's own
    set 0 when `useChunkDataSsbo` = 1 (`ShadowMap::voxFoundOriginSet`); the per-chunk path is unchanged.
  - Stats: `shadow_ranges_mid/near`, `shadow_clusters_mid/near`, `shadow_cluster_faces_mid/near`.
- Found in today's code, not reused: today's light-frustum chunk test (`s_shadowFrustumCull`, default OFF) compares
  ABSOLUTE chunk boxes with the camera-relative light matrix, so switched on it would cull the wrong chunks far from
  the origin.
- Red first: `LightPlanesMatchTheClipVolume` (20,000 points vs the clip volume of a matrix built like
  `fitShadowVolume`) and `CasterBoxFollowsTodaysRuleAndTheLightBox` failed on stubs; green: 0 wrong, mesher tests
  26/26, GPU tests 12/12.
- Prediction (written before the run, scratch `f212_prediction.txt`): per chunk, 0 mid ranges dropped, 0-30 % near,
  whole frame within noise; per cluster, near faces −20-50 %, whole frame −0.1 to −0.6 ms at the ground poses.
- Measured culling (stats): mid drops nothing at any pose (the region is inside the mid reach and light box).
  Near, per chunk: 7 of 32 chunks dropped at overview_south, 1 at top_down, 0 at the ground poses; per cluster, near
  faces 250k (−48 %) / 448k (−7 %) / 468-480k (0-3 %) of 480k. The near fit is the 40 m slice's bounding sphere
  + 48 m margin (radius ~99 m, reach ~147 m), so with the camera inside the 128 m region its map covers all of it.
- Pixels (viewport box; the full frame differs only in the status-bar counters): no culling vs per chunk vs per
  cluster equal to the control at all 5 poses (0-5 px, max 1 level, the same pixels as the shot-to-shot control).
  Sensitivity: face casters off changes 7.4 % / 9.8 % of the viewport at overview_south / canopy_close (control 0).
- Whole frame (Release, perf_harness 8 pairs × 240 frames; `docs/evidence/voxfound_f212_*`):

  | Pose | per chunk − none | per cluster − per chunk |
  |---|---|---|
  | overview_south | **−0.40 [−0.44, −0.16]** (near scope 1.21 → 0.47 ms) | −0.02 [−0.33, +0.22] (near scope 0.47 → 0.65) |
  | top_down | −0.00 [−0.17, +0.20] | **+0.09 [+0.04, +0.13]** |
  | forest_floor_open | +0.01 [−0.13, +0.05] | **+0.36 [+0.21, +1.51]** |
  | east_low | −0.01 [−0.02, +0.07] | −0.26 [−0.69, +0.00] |
  | canopy_close | −0.02 [−0.10, +0.09] | **+0.29 [+0.21, +0.35]** |

- Both predictions wrong in part: per chunk saves 0.40 ms where it drops chunks (predicted noise); per cluster loses
  at 3 poses. At overview_south the cluster path draws FEWER near faces (250k vs 337k) for MORE time (0.65 vs
  0.47 ms), and the mid scope grows while culling nothing: 2,385 36-index commands cost more than 32 direct draws
  (the cull pass itself is < 0.05 ms). Same lesson as F2.11, stronger in the shadow pass.
- **Defaults: per-chunk caster culling ON; per-cluster shadow multidraw OFF** (selectable for re-measuring when the
  scene grows beyond the cascades or the per-command cost changes, e.g. with indirect-count).
- What this does not fix: the preview still casts ~480k faces into BOTH maps at the ground poses (~1.9 ms near +
  ~2.5 ms mid). That is caster VOLUME inside the light box, which only a coarser caster (LOD, item 7) or a tighter
  near fit removes, not culling.

**F2.13 2026-11-13 — §4.6 item 7, first part: the block LOD pyramid (§3.5a point 3), levels 1/8 → 1 m.**
- Built (Mesher.h `LodGrid`, `lodBase`, `lodDown`, `lodScaleFace`):
  - Levels nest exactly: a level-k voxel covers 2^k level-0 voxels a side, and its `count` of level-0 solid voxels is
    the sum of its 8 children. Solid when the count is at least HALF of the 8^k level-0 voxels covered, so a coarse
    voxel is never drawn solid while mostly air (no fattening by construction; a majority of majorities would allow
    down to (1/2)^k — pinned by `LodLevelIsHalfSolidAtLevelZero`).
  - Material = the one seen from outside (below). Card (leaf) materials stay cards.
  - Faces of level k go into the same 8 B record in level-0 units (`lodScaleFace`: position × 2^k, a +side face on the
    cell's last level-0 voxel, extents × 2^k ≤ 256). No shader change.
  - Engine: `RegionChunk::area` exposes the chunk's 1 m margin area, so a level's border voxels are its real
    neighbours up to the 1 m level. `voxfound_preview {"lod_levels":n}` builds levels 1..n of every chunk and keeps
    them all (`Preview::levels`); `{"lod_draw":k}` draws level k for the whole region without a rebuild (a debug view
    to see and measure each level; selection per block region by size on screen, the cross-fade and the 2 m+ levels
    are F2.14+). Leaf sprigs and occlusion connectivity stay at level 0.
- Material rule, found by eye then measured (forest spot, area of up-facing faces per material):
  - By volume (first version): the 1 m level showed DIRT on 39 % of the ground seen from above (1/8 m: 6 %): the
    dirt under a 1/8 m grass layer outvotes it.
  - By the cell's own exposed level-0 voxels (second): still 39 %. Cause: the grass layer usually lies in the cell
    ABOVE, which is mostly air; the solid cell below covers only buried dirt.
  - Final: own exposed voxels; if none, the exposed voxels of its AIR face neighbours at the same level; if none
    (buried), the volume vote. 1 m: dirt 100 m² / grass 712 m² vs 1/8 m 72 / 749. The volume vote reads its own
    channel and the neighbour step reads only carried surface data, so a level's materials depend on one cell around
    each cell (inside the 1 m margin). Gate: every material with ≥ 5 % of level 0's up-facing area keeps its share
    within 10 points at every level (red on both earlier rules).
- Red first: `LodLevelIsHalfSolidAtLevelZero`, `LodLevelsAreChunkIndependentAndScaleExactly` (a part built with the
  engine's 1 m margin equals the whole cut, every level; coarse meshes cover exactly; scaled boxes = boxes × 2^k),
  `LodLevelKeepsTheMaterialSeenFromOutside`, the share gate in `MeasuredLodLevelsOnAGeneratedForest`; all red on
  stubs or the earlier rule; CPU 40/40, GPU 12/12.
- Prediction (scratch `f213_prediction.txt`, before any code ran) vs measured, whole gate region:

  | Level | faces | × L0 (predicted) | solid volume vs L0 (predicted ±3 %) |
  |---|---|---|---|
  | 1/8 m | 480,349 | 1 | 1 |
  | 1/4 m | 127,280 | 0.265 (0.25-0.45, held) | +0.7 % |
  | 1/2 m | 32,796 | 0.068 (0.08-0.20: fewer) | +0.3 % |
  | 1 m | 9,325 | 0.019 (0.02-0.08: at the edge) | −0.2 % |

  Building levels 1-3 adds 15 s to the 32-chunk region build (CPU reference; the GPU port is owed).
- Looks (same pose, wind 0, GI off, `docs/evidence/voxfound_f213_*.png`): the ground keeps its materials at every
  level. At 1 m, trunks thinner than half a metre thin out or vanish (they fill less than half a cell) and trunk
  bases read as pale blocks. Up close every coarse level is visibly blocky, as expected: that is what the selection
  by size on screen (F2.14) is for; these are its gates.
- Whole frame, the WHOLE region drawn at level k vs level 0 (Release, perf_harness 8 pairs × 5 poses,
  `docs/evidence/voxfound_f213_ab_L*.jsonl`; every CI excludes 0):

  | Pose | 1/4 m − 1/8 m | 1/2 m − 1/8 m | 1 m − 1/8 m |
  |---|---|---|---|
  | overview_south | −3.19 | −4.38 | −4.29 |
  | top_down | −3.19 | −4.71 | −4.55 |
  | forest_floor_open | −5.20 | −6.77 | −5.91 |
  | east_low | −5.04 | −6.37 | −6.55 |
  | canopy_close | −4.27 | −5.63 | −6.54 |

  Most of it is the shadow passes: the preview's face casters cost ~4.5 ms at 1/8 m (mid ~2.5 + near ~1.9) and
  ~0.8 ms at 1/4 m, ~0.3 ms at 1/2 m, ~0.08 ms at 1 m — caster VOLUME, as F2.12 found. Past 1/2 m the gain
  flattens (1 m saves about what 1/2 m saves): what is left is the sprigs, the sky and the rest of the frame. The main-view face scope falls 1.2-2.2 ms at
  the ground poses. Prediction 4 held in direction (shadows fall most; the sprigs, unchanged, still dominate the
  ground poses).
- Not done here, owed to F2.14: choosing the level per block region by size on screen (pixels per voxel, the tier
  knob), the dithered cross-fade between levels with the "no holes where levels meet" gate, coarse shadow casters
  chosen from the light's view, the leaf sprigs and grass at distance (tier 8 handoff), levels above 1 m (need a
  wider margin or the region pyramid), and the GPU port of the level build.

**F2.14 2026-11-13 — §4.6 item 7 continued: the level by size on screen + the dithered cross-fade.**
- Design (Mesher.h `lodLevelAt`, `lodPickLevel`, `lodKeeps`, `lodLevelsForBox`, `lodFlagBits`):
  - The level is continuous: `L(d) = log2(d) + c`, `c = log2(8P/f)`, where P is pixels per voxel (THE tier knob) and
    f = ½·height·proj[1][1] (1086.4 px in the harness window, 45° FOV). Level k's voxel is P px wide at L = k.
    Level 1 starts at d = f/(4P): 272 m at P=1, 136 m at P=2, 68 m at P=4, 34 m at P=8.
  - Per FRAGMENT (`voxel.frag`, flags bits 16-31: level, max level, on bit, 8-bit code for c): distance from the
    fragment's world position, and a 4×4 Bayer value u of the pixel. `pick = floor(L + ½(u − ½))` ramps the share of
    the coarser level across a band of ½ level; a fragment of level k is KEPT when
    `pick(L − ¼) ≤ k ≤ pick(L + ¼)` (slack ¼ level). Looks depend on world position and pixel only, never on the chunk.
  - The slack makes both levels draw where they meet, so the band overlaps instead of leaving a gap: the coarser
    level is half-solid by construction (F2.13) and the finer one draws over it, so neither a hole nor fattening
    appears in the overlap (gate below). At most two levels are kept at any fragment (pinned).
  - Cost: per cluster (GPU cull pass, level in `lo.w` bits 24-27, 128 B push with the camera and c) and per chunk on
    the direct path, `lodLevelsForBox` bounds the levels any fragment of the box can keep (nearest point − slack at
    the lowest Bayer value, farthest corner + slack at the highest). Only those levels' faces are drawn.
  - Preview: `voxfound_preview {"lod_auto":true,"lod_px":P}` concatenates all levels (`Preview::selectLod`); the level-0
    clusters/chunks stay as they were, so shadows and occlusion use level 0 (coarse casters are the next part).
- Red first: `LodLevelIsSizeOnScreen`, `LodPickRampsAcrossTheBandWithoutAPop`, `LodLevelsNeverLeaveAHoleWhereTheyMeet`
  (exact partition at slack 0, no hole with slack, a hole when slack is removed = the test sees one, ≤ 2 levels),
  `LodBoxRangeCoversEveryFragmentInIt`, `LodFlagBitsLeaveTheStaticFlagsAlone`, and on the GPU
  `ClusterCullPicksLevelsLikeTheReference` (4,000 clusters: 1,939 drawn, 205/752/783/199 per level, 0 differ from the
  CPU reference). All red on stubs; then CPU 45/45, GPU 13/13. (One test bug on the way: the ramp sweep started at
  k − 1, inside the band of switch k − 1, and saw a third level; the sweep is k ± ½.)
- Prediction (scratch `f214_prediction.txt`, before any code) said "level 1 from ~110 m at P=1". WRONG by about a
  factor 2.5: I used the wrong focal length; level 1 starts at f/(4P) = 272 m. So **P=1 is a no-op in this region**
  (its far corner is ~260 m): every pose draws exactly the level-0 faces.
- Faces drawn in the main view (off → on; `[L0, L1, L2, L3]`):

  | Pose | off | P=2 | P=4 | P=8 |
  |---|---|---|---|---|
  | overview_south | 477,779 | 588,344 | 224,914 [70k,127k,28k] | 59,875 |
  | top_down | 446,740 | 564,823 | 148,478 | 38,894 |
  | forest_floor_open | 287,263 | 287,511 | 345,567 | 170,288 |
  | east_low | 439,593 | 493,019 | 379,347 | 132,894 |
  | canopy_close | 189,602 | 193,086 | 216,873 | 124,155 |

  Inside the band BOTH levels draw, so faces RISE where most of the view sits in a band (P=2 everywhere; P=4 at the
  ground poses). That overlap is the price of the no-hole rule, and it is overdraw, not extra pixels.
- Gates (5 poses × P 2/4/8, wind 0, GI off, world hidden; per pixel against an empty frame E and the four single-level
  frames L0..L3; `docs/evidence/voxfound_f214_gate_rescored.json`):
  - **0 holes** (empty in the mix, covered in EVERY single level) and **0 fattening** (covered in the mix, empty in
    every single level), every pose and P.
  - First scoring used one threshold and flagged 1-14 "holes" / 0-13 "fattening" px. Dug in (`f214_where.py`): every
    one was a pixel within ~10 of the background in every frame (mix = one level, within 2 colour steps), straddling
    the threshold. Rescored with a margin: empty ≤ 10, covered > 30, in between not scored.
  - The scorer works: a synthetic 6×6 hole is found (36 px every time), and it finds REAL empties where coarse levels
    lose thin parts vs level 0 (up to 162 px at overview_south P=8). Controls E/E2 0 px, mix/mix repeat 0.0 %.
  - Changed vs level 0 (> 24/255): P=2 0-0.08 %, P=4 0.04-0.65 %, P=8 0.22-1.93 % of pixels.
- Looks (`docs/evidence/voxfound_f214_sheet_*.png`, L0 | P=4 | P=8 at the same pose): P=4 cannot be told from level 0 at
  overview_south and east_low. P=8: distant trunks and trunk bases slightly blockier; no seam or band visible.
  Shadows identical (casters are level 0).
- Whole frame, B − A (A = level 0 everywhere, B = by size on screen; Release, perf_harness 8 pairs × 5 poses × 240
  frames, `docs/evidence/voxfound_f214_ab_P*.jsonl`; * = CI excludes 0):

  | Pose | P=2 | P=4 | P=8 |
  |---|---|---|---|
  | overview_south | +2.55* | −1.28* | −3.27* |
  | top_down | +0.76* | −1.39* | −3.52* |
  | forest_floor_open | −0.77 | +1.64 | +1.04 |
  | east_low | +0.60 | −0.52 | −0.42 |
  | canopy_close | −0.55 | +2.62 | +0.88* |

  As the face counts say: P=2 costs where the view is mostly band (overlap, nothing coarse yet); P=8 saves 3.3-3.5 ms
  from above. At the ground poses the main-view faces were never the cost (the sprigs and the level-0 shadow casters
  are, F2.13), so nothing moves there; canopy_close pays a little for the band overdraw.
- **Default: OFF** in the preview, and P is not yet a tier: the from-above win only pays off with the coarse casters
  (next), and the band overdraw at canopy_close is a cost to cut first (e.g. a narrower band where P is large).
  Candidate tier values once casters follow: P=4 (no visible change) and P=8 (slightly blocky far trunks).
- Not done here, owed to F2.14 next: coarse shadow casters chosen from the light's view (measured alone), the leaf
  sprigs and grass at distance (tier 8 handoff), levels above 1 m, the GPU port of the level build.

**F2.14b 2026-11-13 — §4.6 item 7 continued: coarse shadow casters.**
- Design (`voxfound_face_shadow_lod.vert/.frag`, `ShadowMap::ensureVoxFoundLodPipeline`, own layout: set 0 = chunk data,
  92 B push, vertex + fragment; `voxfound_preview {"lod_casters":true}` with lod_auto):
  - Each CASTER fragment runs the main view's keep test at its own camera distance (Bayer of the shadow texel), so a
    caster is drawn at the level the camera sees it at.
  - The light's view: L is floored at `lodTexelFloor` = log2(8 × texel), the level whose voxel the shadow map cannot
    tell from 1/8 m. Measured texels: mid 0.113 m (floor −0.15), near 0.043 m (floor −1.54), so the floor is INERT in
    this window (predicted). It matters only for coarser maps or larger cascades.
  - **Casters lag the main view by one level (`kLodCasterLag` = 1).** Found by eye at top_down P=8 (first version, no
    lag): a regular grid of shadow dots on open ground, 0.22 % of the viewport. Cause: the dither picks per screen
    pixel in the main view and per shadow TEXEL in the caster; inside a band a texel could hold the coarse ground
    (half-solid, it may stand above the fine one) where the screen shows the fine one, which then shadows itself. Rule
    now: no caster texel is ever coarser than the finest level any pixel can show there (and never more than one level
    finer). Pinned by `LodCasterIsNeverCoarserThanTheSurfaceItShadows` (red at lag 0, failing at L = 0.52, the band case).
  - Cost bound per 32 m chunk (`lodLevelsForBox` with c − lag and the floor). A chunk whose box allows one level keeps it
    at every fragment, so it is drawn with the plain caster pipeline (no discard, early depth); only band chunks pay
    for the per-fragment test. The per-cluster shadow multidraw is not LOD'd (with lod_casters on it is not used).
- Red first: `LodCasterFloorIsTheShadowTexelAndTheBoxCoversIt` (stub floor), `LodCasterIsNeverCoarserThanTheSurfaceItShadows`
  (lag 0); then CPU 47/47, GPU 13/13.
- Prediction (scratch `f214b_prediction.txt`, before code): floor inert — held. "From above −1.5 to −2.5 ms at P=8,
  ground −0.5 to −1.5, P=4 half of that" — WRONG at the ground and at P=4 (below).
- Gate (5 poses × P 4/8, main view already by size on screen in both A and B, so only the casters differ;
  `docs/evidence/voxfound_f214b_gate.json`): B vs A changes ≤ 0.055 % of the viewport (overview_south P=8; was 0.217 %
  with the stipple before the lag), mostly darker, scattered single pixels on trunk edges; controls 0.000 %; face casters
  off entirely changes 0.84-5.98 % (the rig sees the casters). Eye check (`voxfound_f214b_sheet_*.png`): no pattern.
- Faces cast (mid; near is similar): P=8 overview_south 480k → 282k, top_down 480k → 368k; but at the ground poses and at
  P=4 casters RISE (e.g. 480k → 606k): a chunk inside the band draws both levels whole. With the lag, level 1 starts at
  68 m (P=8) / 136 m (P=4), where much of this 128 m region is band.
- Whole frame, B − A ms (Release, perf_harness 8 pairs × 5 poses × 240 frames, `voxfound_f214b_ab_P*.jsonl`; * = CI
  excludes 0). v1 = every LOD caster through the discard shader; v2 = single-level chunks on the plain pipeline:

  | Pose | P=4 v1 | P=4 v2 | P=8 v1 | P=8 v2 |
  |---|---|---|---|---|
  | overview_south | +2.60* | +0.71* | −1.19* | −2.43* |
  | top_down | +2.47* | +0.56* | −1.02* | −1.16* |
  | forest_floor_open | +0.34 | +0.23* | +0.92* | +1.01* |
  | east_low | +0.57* | +1.16* | +0.41* | +0.45* |
  | canopy_close | −0.06 | +0.78 | +0.99* | +0.41* |

  The win exists only where most casters are past the lagged band (from above at P=8). Elsewhere the two-levels-per-band-
  chunk overlap and the discard cost more than the coarse levels save. The lever that would change this is a finer cost
  bound than the 32 m chunk (per cluster, CPU ranges or the light-frustum multidraw of F2.12, which then measured slower
  per command) — not taken here.
- **Default: OFF** (`lod_casters`). Kept selectable: it is the right shape for big views from above; it becomes a
  candidate once the band overlap is bounded per cluster, or with levels above 1 m where whole chunks leave the band.

**F2.14c 2026-11-14 — §4.6 item 7 continued: leaf sprigs hand off to coarse leaf mass (tier 8b's handoff).**
- Design: build knob `lod_sprig_level` h (1..lod_levels, default 0 = off). Levels ≥ h build leaf cells as SOLID faces
  (no card set: the half-solid coarse leaf mass, as today's chunk LOD does, `LodChunkMesh.cpp:43`), kept LAST in each
  chunk's range and in their own tagged clusters, so `{"lod_sprig":false}` drops them from the draw lists without a
  rebuild (the A/B switch; false = the F2.14 state). Sprigs belong to levels < h: kept where the main view keeps a level
  below h, `pick(L − ¼, u) < h ⇔ u < 2(h + ½ − L)` (`lodSprigKeeps`/`lodSprigFade`), fed per sprig to foliage.frag's
  existing `vFade` dither (same Bayer table and indexing) by `voxfound_foliage.vert` (FoliagePush +lodC +lodHandoff,
  80 B; foliage.frag untouched). Cost: chunks (box + 2 m card overhang) wholly past h skip their sprigs; faded-out cards
  collapse to zero area. Sprig and face casters unchanged.
- Red first: `LodSprigsHandOffToLeafMassWithoutAHole` (threshold = keep test; for every pair of Bayer values the sprig
  or the mass is kept, also with the two L 0.03 apart; sprigs gone past h + ¾, mass absent before h − ¾); red on stubs;
  CPU 48/48, GPU 13/13.
- Built (h = 2, same region): level-2 leaf mass 142,231 faces, level-3 31,112 (prediction "< 30 k" at level 2 — WRONG
  ×5: the 1/8 m canopy is dense enough that most ½ m cells are half solid).
- Gate (5 poses × P 4/8, `voxfound_f214c_gate.json`): coverage holds — canopy pixels lost ≤ 1,207 of 431 k (overview
  P=8), gained ≤ 155, controls 0.000 %. **Looks FAIL:** at P=8 the far canopy is darker and blockier (top_down 49 % of
  pixels change, mean luma 108 → 75; overview 20.6 %) and the dither band reads as a crosshatch on mid-distance trees
  (`voxfound_f214c_sheet_*_P8.png`). Dug in: NOT shadows — with every preview caster off the gap stays (131.7 → 103.9);
  the mass is the leaf CARD texture (a cutout sprite) drawn on solid faces by voxel.frag
  (`voxfound_f214c_leafmass_zoom_top_down_noshadow.png`: tiled leaf pattern with dark gaps), while cards get
  foliage.frag's shading. Today's tier 2 has the same mass and is OFF by default; there is no look to copy.
  At P=4 the band starts at ~114 m and only 1.0-1.3 % of pixels change from above.
- Whole frame at P=8, B (handoff) − A (none), 8 pairs × 5 poses (`voxfound_f214c_ab_P8.*`, all CIs exclude 0):
  overview_south −0.20, top_down −0.71, forest_floor_open +0.61, east_low +0.73, canopy_close +0.51 ms. Prediction
  (−3 to −6 from above, −1 to −4 at the ground) WRONG: from above the whole frame is ~6 ms and the sprigs were not most
  of it; at the ground the camera is inside the region so few sprigs leave, and the mass faces add cost.
- **Default: OFF** (`lod_sprig_level` 0). Owed before it can ship: a leaf-mass appearance that matches the card canopy
  at that size (opaque, the cards' average colour and shading; a voxel.frag/receiver change with its own gate), then
  re-measure. Grass: nothing to hand off yet — the preview draws no grass blades (F2.2).

Each item is measured on/off on the same generated scene, whole frame. An item only counts as done when its own on/off
difference is measured.

## 5. Simulation on the world grid

- MatSim's cells become world voxels with a fill channel: liquids and powders live in the world, not in a separate
  box.
- MatSim's cell sizes change from 1, 1/3, 1/9 m to 1, 1/2, 1/4, 1/8 m. Its exact split/merge then works by halving,
  and every measurement on this branch made at 1/9 m is re-run at 1/8 m. A 1/8 m grid has (9/8)³ = 1.42x fewer
  voxels per m³ than 1/9 m.
- Its GPU rules run on awake blocks only. Measured: 1.49 M awake cells stepped in 0.03-0.11 ms.
- Particles for fast motion (the hybrid) write back into world voxels when they settle.
- Crumbs from breaking (§6) are powder in this simulation: rubble that slides into piles.

## 6. Breaking and fracture

### 6.1 What we want (owner, §0)
- Hitting a wall knocks out **a piece shaped like real damage**: a chunk of stone with a jagged edge, a run of bricks
  along their mortar, a split plank.
- A strong hit shatters it into several pieces. A weak hit chips a little off near the impact.
- **A single block may break into several pieces**; pieces don't follow block lines.
- Loose dust and rubble appear too, but as a by-product, not as the main result.
- The same system breaks world structures and items.

### 6.2 Today, and what carries over (`DestructionSystemV2.md`)
- Kept:
  - the energy vs toughness model (a hit carries energy; each material has a toughness);
  - per-material break profiles in `materials.json`;
  - tool affinity (axe vs pick);
  - the cut-detection analyzer (`CrossSectionAnalyzer`);
  - "cheap at rest" fragment retirement;
  - capped collision proxies (`CollisionProxyPlan.md`).
- Replaced: `brittleS1` / `brittleS2`, which picked "break into subcubes" vs "into microcubes". With one voxel size,
  fineness comes from the crack pattern's piece size instead (§6.4).

### 6.3 The model in one line
**A break cuts bonds along a crack pattern inside the damaged region. Then whatever is no longer bonded to anything
anchored comes loose as pieces.**

### 6.4 Crack patterns (where cracks run)
A crack pattern is a set of cells in world space, seeded deterministically from the hit and the world position.
Bonds between voxels in different cells are cut. The pattern is built from three ingredients:

1. **The impact pattern.** Cells clustered around the hit point: small near it, larger farther away. This is the
   fracture-pattern approach of Müller, Chentanez & Kim, SIGGRAPH 2013: patterns placed at the impact location,
   applied locally, so large objects break partially. Shape by material class:
   - **brittle** (stone, brick, concrete): irregular cells, sizes spread wide;
   - **glass:** radial cracks from the impact plus concentric rings, giving shards;
   - **fibrous** (wood): cells stretched along the grain, so it splits along the length and snaps across it only
     at high energy.
2. **The material grain.** A direction stored per material, or per part for wood (the plank's long axis, known to
   the generator). It stretches the pattern.
3. **Part boundaries.** Bonds between voxels of different parts are weaker, like mortar. A brick wall breaks into
   bricks and half-bricks, not arbitrary blobs. A beam separates at its joint before it snaps.

**Energy sets the size of the result:**
- the damaged region's radius grows with energy / toughness;
- the cell size near the impact shrinks with energy / toughness.
A light hit chips a few voxels; a heavy one shatters. This replaces brittleS1/S2.

**Progressive damage:** hits below the breaking threshold add to the damage channel. Bonds through damaged voxels
get weaker, so repeated blows open the same cracks. Hairline cracks are drawn where bonds are weakened but not cut.
`voxel.frag`'s crack field already draws cracks; it would read the damage instead of today's cube damage bits.

### 6.5 After the cut: pieces, anchoring, bodies, crumbs
1. **Find pieces.** Label the connected groups of voxels in the affected region and its surroundings
   (connected-component labelling on the GPU).
2. **Anchoring.** A piece is anchored if it is still bonded to the ground (bedrock / terrain) or to a fixed structure
   outside the region. Anchored pieces stay in the world grid. This is the collapse rule: knock out the column under
   a lintel and the lintel comes loose.
3. **Loose pieces:**
   - **bodies** (≥ N voxels): voxels copied into a body grid; mass, centre and inertia computed from the voxels
     (density per material). The body gets the impulse from the hit.
   - **crumbs** (< N voxels): become powder cells in the world simulation (rubble, dust) or short-lived particles.
     Counted, never deleted.
4. **Conservation test:** voxel counts per material before = after (world + bodies + crumbs), exactly, at every step.

### 6.6 Structural load (later phase, not v1)
- v1 uses anchoring only: connected or not.
- v2 adds load: each bond carries the weight above it, and an overloaded bond cracks. A sagging beam snaps, a
  cantilever breaks at its root.
- Decided (D7): not in v1. Decided after the owner reviews the F4 fracture-lab videos, because it is expensive and not
  needed for the first look.

### 6.7 Bodies: moving, colliding, coming to rest
- **Simulation:** bodies run on the GPU solver. Today's GPU debris already collides against the micro-resolution
  occupancy pool. A body is many voxels:
  - against the world: its surface voxels are tested against world solidity;
  - against other bodies: bounding boxes first, then voxel overlap.
  - Per-body contact count is capped (`CollisionProxyPlan.md`).
- **Drawing:** a body is a small block grid with one transform. Faces are built once when the body is made, drawn
  with the body's transform; the record is the §4.2 face record + a body index. No per-voxel float transform.
- **Rest (the rotation problem).** A body that stops at an angle can't go back into the axis-aligned world grid
  without resampling. Options:
  - (a) keep it as a body forever (Teardown): cost grows with every break;
  - (b) at rest, voxelize it into the world grid: volume-preserving resampling, conservation-tested, but the shape
    shifts by up to half a voxel;
  - (c) **hybrid (decided, D6):** keep it as a body while it is near the player or recently moved; bake it into the
    world grid when it is asleep and the body budget is exceeded, farthest first.
  - Gate: conservation exact, and the visual pop measured.
- **Gathering:** a resting piece is still the gatherable aftermath (`DestructionSystemV2.md` (D)).

### 6.8 Items
- Items are already their own grids (today's fine items: 1/81 m, kinematic). They move to **item voxels**, 1/64 m,
  so one world voxel splits into exactly 8x8x8 item voxels.
- The fracture model doesn't depend on the voxel size: it works on any grid. An item breaks with the same patterns
  and bonds, at its own resolution, into pieces that are smaller items.

### 6.9 Determinism, tiers, budgets
- **Deterministic:** patterns are seeded from the event and the world position, so the same hit gives the same pieces.
  Tests compare exactly.
- **Tier knobs change cost, never which bonds break.** Knobs: the crumb threshold N, the body budget, the bake-in
  distance, the crumbs-as-powder vs particles choice.
  - Which voxels end up loose is identical across tiers; only how loose matter is simulated changes.
  - Rule R2 holds for the break itself.

## 7. Moving the engine over (the consumers)

From the audit (`MatterRepresentationReview.md` A5), in order:

1. **Read interfaces.** Re-implemented on the new store: `occupiedMicro`, `hasVoxelAt`, `getVoxelTypeAt`,
   `subVoxelFloor`, `queryAABB` → `OccupiedBox`, `stateAtMicro`, `phxOccupancySolid`. Navigation, AI, spawners,
   water and physics then work unchanged. Tested against a dense reference of the same 1/8 m content (1/9 m
   content can't equal 1/8 m content voxel for voxel, so the old store is the baseline for cost only).
2. **Generators.** World, flora, structures. They write through a new bulk-write API (fill a region, paint
   voxels, set part ids), not per-voxel calls. `MicroCanvas` already paints at voxel resolution; it exports
   directly.
3. **Drawing** (§4), behind a switch, with the pixel-diff gate.
4. **Editing:** placing and breaking with brushes of 1, 2, 4 or 8 voxels per side; picking returns a voxel position,
   not a `Chunk*`.
5. **Destruction** (§6) replaces `DamageSystem`'s carving.
6. **Saving** (codec v3 + converter), **LOD tiers 2/3** (a block mip pyramid), **snapshots / undo / the voxel API**
   (JSON in voxel coordinates; cube and subcube become brush sizes).
7. **Delete** `Subcube`, `Microcube`, the C/S/M face packing and the old mesher. The 16 files that walk `chunkMap`
   go through the new interfaces.

## 7a. Converting content to the 1/8 m grid

Content on whole metres converts exactly: one cube becomes one block. Content on thirds (subcube, 1/3 m) and ninths
(microcube, 1/9 m) does not, because 1/3 m is 2.67 voxels.

### 7a.1 F0 inventory: templates (measured 2026-11-10)

From `python tools/voxel_foundation_inventory.py` (raw per-template data:
`docs/evidence/voxel_foundation_inventory.json`). Provenance is read from each template's header and the catalog's
`method` field. A template converts exactly only if it has no microcube or fine-item lines and its subcubes fill
whole cubes.

**238 templates: 9 convert exactly, 229 don't.** Of the 229: **186 come from generators**, 25 are hand-authored, 10
are imported, 8 have unknown provenance.

| Made by | Templates | Exact | Not exact | Lines C / S / M / fine |
|---|---|---|---|---|
| `gen_items.py` (items, 1/81 m) | 52 | 0 | 52 | 0 / 0 / 0 / 44,699 |
| `gen_tree.py` | 37 | 2 | 35 | 143,652 / 58,426 / 80 / 0 |
| `tree_forge.py` | 35 | 0 | 35 | 49,669 / 377,305 / 420,524 / 0 |
| `regen_furniture.py` | 34 | 0 | 34 | 0 / 0 / 11,877 / 0 |
| hand-authored (`/model`) | 27 | 2 | 25 | 2 / 527 / 0 / 0 |
| unknown | 13 | 5 | 8 | 1,199 / 159 / 177 / 0 |
| `structure_pipeline.furniture` | 13 | 0 | 13 | 0 / 4 / 3,737 / 0 |
| `vox_import.py` | 10 | 0 | 10 | 0 / 0 / 4,074 / 0 |
| building shells (multi-resolution export) | 9 | 0 | 9 | 9,453 / 51,503 / 7,306 / 0 |
| `structure_pipeline.doors` | 4 | 0 | 4 | 0 / 98 / 2,549 / 0 |
| "generated from voxel matrix" (tool unknown) | 4 | 0 | 4 | 0 / 87 / 1,822 / 0 |

Unknown provenance and not exact: `architecture/door_wood`, `door_wood_wide`, `trapdoor`, `waystone`,
`furniture/bench_great`, `stool_low`, `weapons/axe`, `sword_fine`.

What this means:
- **Templates are mostly a generator job.** 186 of 229 regenerate once their generator emits 1/8 m (or 1/64 m for
  items).
- **The hand work is small:** 25 hand-authored templates (527 subcube lines in total), 8 unknown, plus the building
  shells and "voxel matrix" ones until their tool is found.
- **Imports re-import** from their source `.vox` at the new scale, where the source files are present.

### 7a.2 F0 inventory: code (measured 2026-11-10)

From a read-only sweep of the code (an Explore agent's report; two of its claims spot-checked and confirmed:
`AgentProfile.h` `kAgentHeightMicro = 17` and step-up 4, and `WaterCoreManager.cpp` cell-size menu
{1, 1/3, 1/9, 1/27, 1/81}). Approximate site counts.

**Totals:**
- about 270 C++/GLSL files and about 4,500 sites;
- about 130 Python tools and scripts (about 70 of them water/MatSim tuning scripts);
- 99 test files.

**This is far larger than "5 structure-generator files".** The 3/9 grid is not only content: it is in the engine's
units.

| Subsystem | ~Sites | What it assumes | Kind of change |
|---|---|---|---|
| Structures and settlements (~20 files: `StructureForge`, `StructureRealizer`, `SettlementBuildService`, `FurniturePlacer`, `HearthForge`, `StructureGenerator`, `FenceBuilder`, `TowerForge`, `StairPlanner`, ...) | ~1,000 | dimensions in 1/9 m units; trim, roof courses, stair risers in 1/3 steps | design change, visual review |
| Core storage (`Chunk`, `ChunkVoxelManager`, `Subcube`, `Microcube`, ...) | ~900 | three nested levels with 0..2 indices | replaced by the block store (F1) |
| Editing and interaction (engine + editor) | ~600 | placing and breaking by level | API redesign (brushes) |
| Physics, collision, raycasting | ~250 | collision shapes per level; cube → sub → micro ray descent | API redesign |
| Navigation and agents (`AgentProfile`, `NavGraph`, `PathPlanner`, `TraversalProbe`, ...) | ~250 | agent height 17/9 m, step-up 4/9 m, risers in ninths | **design change: gameplay numbers must be re-picked** (for example 15/8 m height, 4/8 m step) |
| Micro occupancy (lighting, GI) | ~200 | 729 bits per cube, world micro = world x 9 | layout change; 512 bits per block is simpler |
| Persistence (`WorldStorage` SQL `CHECK < 3`, `ChunkBlobCodec`, `.voxel` C/S/M parser) | ~200 | the C/S/M format | codec v3 + converter (§3.6) |
| Damage and debris | ~180 | shatter into 27 or 729 pieces | replaced by fracture (§6) |
| Instance packing + voxel shaders (`Types.h`, `static_voxel.vert`, `shadow.vert`, `dynamic_voxel.vert`) | ~150 | cube-local positions decoded with %3, /3, /9 | replaced by the §4.2 face record |
| LOD (`TemplateLodChain` cell sizes {3,6,9,13,18,27} micros, ...) | ~120 | coarse levels in micro units | replaced by the block pyramid (§3.5a) |
| Water (`WaterCoreManager` cell menu, `kRecordTile` 72 cells) | ~120 + ~70 scripts | power-of-3 cell sizes, constants tuned at 1/9 m | menu becomes powers of 2; tuned constants re-measured |
| MCP / HTTP API | ~100 | subcube/microcube in tool parameters and docs | API redesign; skills and external tools depend on it |
| Texture sub-tiles (`SUBCUBE_UV_SCALE`, `crack.glsl` `kCrackCell = 1/3`), roof crops | ~70 | 1/3 and 1/9 texture windows | replaced by world-position UVs (§4.3); crack cell and roof crops redone |
| Python asset/structure pipelines (`template_writer.py`, `detail.py`, `forge_core.py`, ...) | ~400 | the C/S/M format, 1/9 units | mostly mechanical once the format changes |
| MatSim, fine items, `MicroCanvas` | ~120 | 1, 3, 9 cells per metre; 1/81 m items | mostly mechanical |

What this means:
- **Most of the heavy subsystems are replaced by the plan anyway**, not converted: core storage, instance packing,
  damage, LOD, texture windows.
- **The real conversion cost is in two places:**
  - the structure/settlement code, ~1,000 sites of 1/9 m units with authored proportions;
  - navigation, where the agent's height and step-up are gameplay numbers.
- **A recommended shortcut for those two:** introduce a world-length unit type (metres, or integer 1/8 m units)
  and move them to it once. Changing `* 9` to `* 8` in place would silently change every proportion.

### 7a.3 Generators first (owner, 2026-11-11)

Owner: "we dont need every template converted over. they can be regenerated. later. more importantly is fixing up
any generation templates in the first place." So FC converts **generators**; the template library is regenerated
from them later, and hand-made templates wait (re-authored or resampled only when needed).

**Generator map (2026-11-11, from reading the code; C++ side read by an Explore agent, five citations spot-checked
and confirmed).** For each: its internal model, its choke point (where it turns into voxels), and what is hard.

| Generator | Model before voxels | Choke point | Hard part |
|---|---|---|---|
| `tools/tree_forge.py` (all `forge_*` flora, the live biome pools) | skeleton in **metres** (continuous), capsules/spheres | `forge_core.rasterize_*` + `emit()` | nothing: one scale factor. **Done 2026-11-11** (`--grid 8`, below) |
| `tools/gen_tree.py` | sub-space (1/3 m) integer grid | its own `emit()` | marked DEPRECATED (superseded by tree_forge) but still makes the pooled `bush_round`/`bush_leafy_*`. **Retire**: replace with tree_forge bush presets |
| `ProceduralTree` (C++, "procedural" flora mode) | 1/3 m integer grid, a port of the deprecated gen_tree | `emitTemplate` (`ProceduralTree.cpp:288`) | it ports the deprecated algorithm. **Port tree_forge to C++** on the new grid instead of converting it |
| Flora stamping (`ObjectTemplateManager::decorateChunk`) | whole-metre anchors from `planFlora` | `stamp` lambda (`ObjectTemplateManager.cpp:548`) | none: anchors are metres; only the template format changes |
| Terrain (`WorldGenerator::generateChunk`) | per-column metre heights + float `bedHeight` | `generateChunk` | creek beds quantised to thirds (`creekLayers`, `:1534`), bridge parapet subcubes (`:566`). The 1/8 m form follows `bedHeight` directly (the storage lab already does) |
| Structures: `MicroCanvas` | integer **1/9 m** cells | `MicroCanvas::exportVoxels` (`MicroCanvas.cpp:110`), then `StructureGenerator::place` (`:793`) | the canvas is one choke point, but its callers hold ~300 sites of `*9`, `/3`, `%3` and design constants authored in ninths (below) |
| `StructureRealizer`, `StructureForge`, `HearthForge`, `TowerForge`, `FenceBuilder`, `StairPlanner`, `StreetPaver`, `SettlementBuildService` | rooms/footprints in metres, converted `*9` at each use; styles in metres via `lround(m*9)` | the canvas, plus three private stampers (`emitMicro`, TowerForge `emitRange`, sign brackets) | **no shared length type**: raw ints named `...Micro`; six private floor-division helpers (`fl9`, `fdiv9`, `floorDiv9`, `fdiv`, `fdivq`, `floorDiv`) |
| `tools/regen_furniture.py`, `gen_items.py`, `gen_door.py`, `gen_seat_variants.py` | **authored integer designs** in ninths (furniture), 1/81 m (items), thirds (doors) | their writers | dimensions are hand-picked cell counts (`SEAT_Y = 6` → 7/9 m): re-express in metres, then rasterize |
| `tools/vox_import.py`, `asset_pipeline/template_writer.py` | a voxel matrix + a target scale | `res` / `write_template` | one scale parameter |

**Design constants authored in thirds/ninths (must be re-picked, not scaled):** roof pitch in subcubes per metre of
run (`StructureRealizer.cpp:760`), eave and underside snapped to subcube rows (`:688`, `:775`), quoin legs 4/3
micro (`:650`), style thicknesses 0.111/0.222/0.333/0.444/0.667 m (`resources/structure_styles.json`), stair
`MIN_TREAD`/`COMFORT_RISER` = 2 micro, character step-up 4 micro, guard height 8 micro (`StairPlanner.h`), tower
treads 3 micro and door clearance 27 micro (`TowerForge.cpp`), hearth bodies 14x11x5 micro etc. (`HearthForge.cpp`).
Natural 1/8 m picks: thicknesses 1/8, 2/8, 3/8, 4/8 m; riser 2/8 m; step-up 4/8 m (agent profile, §7a.2).

**Bug found on main while reading:** TowerForge's `emitRange` (`SettlementBuildService.cpp:1240`) silently skips any
micro span that is not 3-aligned (`++y`). A silent drop; logged here, not fixed on this branch.

**Template format on the new grid:** the format the engine already defines for fine items, `# grid: N` + `V` lines,
plus `B x y z sx sy sz Mat` boxes (written by `tools/voxgrid.py`, greedy-merged, deterministic). N = 8 for world
content, 64 for items. The live loader accepts only 27/81 today; the lab reads grid 8 (`loadGridTemplate`).

**Steps:**
1. **Inventory (F0): done** (§7a.1-7a.2).
2. **Shared emitter + first generator: done 2026-11-11.** `tools/voxgrid.py` (grid-agnostic canvas, B/V emit,
   parser); `tree_forge.py --grid 8` (same skeleton and random stream; every cell 1/8 m; leaves in 2-cell = 1/4 m
   clumps, the nearest step to today's 1/3 m subcube leaves, a look choice to review by eye). Legacy output stays
   byte-identical (tested). Prediction "wood volume within 20% of legacy" held: 0.92x on oak h13 (legacy shells
   over-fill). The 1/8 m forge library (35 trees, generator output) is in `resources/templates_g8/nature`:
   8.4 MB against 22 MB for today's C/S/M forge library. Tests: `tools/test_voxgrid.py` (8).
3. **A C++ length type** for the structure code (integer 1/8 m units with metre conversions) and one floor-division
   helper; move `MicroCanvas` and its callers onto it in one pass, re-picking the constants above. Then the
   structure validation suite (L2/L3) must pass on the new grid.
   **3a done 2026-11-11 (the building shell).** Design change from "one pass": the grid is a **value**, not a
   constant, so today's game keeps running on 1/9 m while the 1/8 m path is proven (risk 1, "both paths until F7").
   - `core/StructureGrid.h`: `perM` (legacy 9, foundation 8), `m(metres)`, `cubeOf`, the one `floorDiv`, and the
     re-picked constants with the metres each stands for: step-up 3/8 m (0.375, chosen under today's controller's
     0.444 so 1/8 m stairs stay climbable by the character that exists; the new controller's own value is F7's),
     comfort riser and minimum tread 2/8 m, guard 8/8 m (meets IRC 0.914 m, which 8/9 = 0.889 did not), agent
     15/8 m tall, 2/8 m half-width.
   - `MicroCanvas(cellsPerM)`; the C/S/M export refuses a non-9 canvas loudly. `realizeShell(program, style, grid)`:
     every literal 9 in it is the grid (none left). The roof is the one place the old code used thirds as design:
     on 1/9 m it keeps subcube rows and pitch in subcubes per metre; on 1/8 m rows are single cells and pitch is
     cells per metre, so the grounded angle is held to 1/8 m (50° → 10/8 = 51.3°; 40° → 7/8 = 41.2°). Quoin legs
     450 x 300 mm → 4 x 2 cells. `planStair(..., grid)`.
   - Prediction, written before the code: on 1/8 m the 20-case BuildingHarness gives every case the same verdict
     per layer as on 1/9 m. **Held**: 17 pass, the 3 BAD cases fail exactly their layer (red first on a stub
     that ignored the grid: 3/3 new tests failed). Roof (11), quoin (5) and window-infill (3) suites now run on
     both grids: 38/38. Legacy: the 20 pinned `ForgeParityDigests` and the 4 `CanvasDigestTest` digests are
     byte-identical.
   - Not done (each refuses or stays legacy, never silently wrong): ~~hearths~~ (done 2026-11-11, step 4b
     below), furniture placement and the furnish pass
     (`StructureForge`), `TowerForge`, `FenceBuilder`, `StreetPaver`, `SettlementBuildService`, the validators
     (`RealizedStructureValidator`, `BuildingProgramValidator`) and `deriveLocations`.
4. **Re-express the authored-integer Python generators** (furniture, items, doors) in metres.
   **Furniture done 2026-11-11 (`regen_furniture.py --grid 8`, all 34 assets).** The designs stay authored in
   ninths; the generator maps every authored box edge b to the nearest cell edge round(b·8/9), so touching parts
   keep touching. A fill or a cleared hole never drops below one cell, and anchor heights are measured on the
   emitted cells. Output: `resources/templates_g8/{furniture,architecture}` as `# grid: 8` B/V, with no
   `.metrics.json` yet (the characterizer reads C/S/M only).
   - **Tests** (`tools/test_regen_furniture_grid.py`, red first; `set_grid` did not exist):
     - legacy output is byte-identical to the unmodified generator (digest over all 68 files);
     - on grid 8, extents are within one cell of legacy, the material set is equal, the chest's parts are kept,
       and every seat, surface, work and lie anchor rests on a solid top.
   - **Measured on 1/8 m:**

     | Item | 1/8 m | Legacy | Canon |
     |---|---|---|---|
     | Seats | 0.50 m | 0.444 m | 0.45 m (the 1/8 m steps either side are 0.375 and 0.5) |
     | Tables | 0.75 m | 0.778 m | 0.75 m |
     | Bar | 1.125 m | 1.111 m | — |
     | Bed lie surface | 0.625 m | 0.556 m | — |

     The bed's 1/9 m sheet layer has no 1/8 m cell of its own, so it gets a full cell.
   - **Eye check:** offline isometric contact sheet, legacy vs 1/8 m at the same pixels per metre. The engine
     cannot load grid 8 yet. Everything reads the same, except details spaced one ninth apart, which edge
     mapping cannot keep:
     - tool rack: 4 tools become 3 (two merge);
     - keg: the hoops blur;
     - forge hearth: the coals shrink to 1 cell;
     - chair: the back opening drops from 2/9 to 1/8 m;
     - wardrobe: one knob sits in the door seam.

     These need their details re-designed in metres (a follow-up, per asset). The generator change is done.
   - **Found: the committed furniture library has drifted from its generator** (before this change). Running the
     unmodified generator rewrites 41 files:
     - every `.metrics.json` is schema v1, against the characterizer's v2;
     - `anvil`, `bed_single`, `chopping_block`, `oven_bread`, `table_wood` and `tavern_table` lack the
       `# interaction_point:` header line the generator has written since A4 (2026-09-30), so the engine,
       which reads points from the header, does not see their surface, work or lie points.

     Logged, not fixed here: it changes live assets on the 1/9 m path.

   **Doors done 2026-11-11 (`gen_door.py --grid 8`).** Re-designed in metres, not mapped from thirds:
   - a 1 x 2 m (wide: 2 x 2 m) leaf one cell deep;
   - one-cell (0.125 m) Log stiles and rails, near a real ~0.11 m stile; today's are 1/3 m;
   - a centred mid rail (the legacy rail sits off-centre: row 3 of 6 subcube rows), a centre mullion on wide doors;
   - a Gold knob one cell proud, under the rail on the latch side, with the handle point at its centre.

   Output: `resources/templates_g8/architecture`. Found: the script still wrote to the template root, but the
   doors moved to `architecture/` in the 2026-08-07 taxonomy, so a re-run would have made root strays with
   colliding stems. Fixed: legacy now writes to `architecture/`, and its output equals the committed files.
   Tests: `tools/test_gen_door_grid.py` (red first).

   **Items done 2026-11-11 (`gen_items.py --grid 64`, all 52).** The items are authored cell by cell at 1/81 m
   (the flat projected boards at 1/27 m), with tints doing the fine colour work. So the authored canvas is kept
   whole and resampled at emit time, not mapped box by box:
   - **Occupancy:** each authored cell covers the output cells between its edges, mapped to the nearest
     1/64 m edge. Runs keep their extent exactly. A cell with no output edge of its own keeps one cell only
     when it is a one-cell-thick part along that axis, placed in the cell that holds its centre.
   - **Content:** each output cell holds the authored cell nearest its centre, independent of painting order.
   - **No detail vanishes:** a material+tint the content rule dropped everywhere (the quiver's one-cell
     fletching caps, the candle's two-tone flame) recolours the cell at its centre. If that cell holds the
     last cell of another detail, it takes the nearest empty face-neighbour instead.
   - **Grip:** the output cell holding the authored grip centre, snapped to the nearest occupied cell when the
     legacy grip is on the item (the jug's strap).

   Output: `resources/templates_g8/{items,weapons,test}` + `templates_g8/items_manifest.json`, B/V lines with
   tints, 143 KB against 1.3 MB legacy. The catalog is not touched.

   **Tests** (`tools/test_gen_items_grid.py`, red first):
   - legacy output equals the unmodified generator's (digest over 54 files; the committed item library equals
     its generator, 53/53 files);
   - on grid 64: extents within one cell, exactly the same material+tint set, and grip within one cell of
     legacy, on a solid cell exactly when legacy's is (the bowl and lantern are held over a hollow on both grids).

   **Eye check** (offline contact sheet): the weapons, tableware and lights read the same. Losses:
   - fork: the tines merge, since 3 tines + 2 gaps need 5 cells and the head is 4 cells at 1/64 m;
   - lantern: the dark corner posts mostly give way to the glass panes;
   - goblet: the rim crenellation smooths out;
   - sword: the gilt pommel cap becomes steel. The check works per material+tint, and the same gilt survives
     on the guard tips, so it can't see this loss.

   The 1/81 m designs ask for detail the 1/64 m item grid can't resolve in these spots. They need re-design
   in 1/64 m terms.
   Pre-existing: the legacy writer saves headers in cp1252 (the em dash is byte 0x97), not UTF-8.

   **Seat variants done 2026-11-11 (`gen_seat_variants.py --grid 8`); step 4 complete.** These are fit-policy
   seats, so they were re-picked in metres to the nearest cell, not mapped:
   - `stool_low`: 0.625 x 0.375 x 0.625 m (legacy 0.667 x 0.333 x 0.667);
   - `bench_great`: 2.0 x 1.375 x 1.375 m (legacy 2.0 x 1.333 x 1.333).

   Against the SeatFit band, the great bench at 1.375 m is still a legal seat for the ogre (legs 1.58) and the
   goliath (legs 1.27), and is still refused for a human (it sits over 0.20 m above a ~0.9 m leg). Smallfolk leg
   lengths are not recorded anywhere, so the stool's 0.375 m is not checked against them.

   Found, the same drift as the doors: the script wrote to the template root, the files live in `furniture/`,
   and the writes ran **on import**. Running the test's first import dropped two root strays, which were
   removed. Fixed: writes now run under `__main__`, into `furniture/`, and legacy output equals the committed
   files. Tests: `tools/test_gen_seat_variants_grid.py` (red first).

   **4b. Hearth bodies in C++, done 2026-11-11** (`HearthForge::bodyOf(type, grid)`). The 1/8 m shell used to
   refuse a vented fixture; it now paints one. The three bodies are re-picked from the canon metres
   (`object_dimensions.json`), never the ninth counts reused:

   | body | legacy 1/9 m (w x h x d) | 1/8 m | notes on 1/8 m |
   |---|---|---|---|
   | fireplace | 14 x 11 x 5 = 1.556 x 1.222 x 0.556 m | 12 x 10 x 5 = 1.5 x 1.25 x 0.625 m | jambs 0.25 m, so the opening is 1.0 x 0.625 m (legacy 0.889 x 0.667) |
   | forge_hearth | 9 x 9 x 7 | 8 x 8 x 6 = 1.0 x 1.0 x 0.75 m | work surface 0.75 m; firepot 0.25 x 0.375 m, 0.125 m deep (canon pot 0.23 x 0.33 x 0.11) |
   | oven_bread | 14 x 12 x 12 | 13 x 11 x 11 = 1.625 x 1.375 x 1.375 m | coal floor 0.75 m; chamber 0.875 m square, 0.375 m clear above the coals; mouth 0.375 x 0.25 m (canon 0.63 x 0.45 m dome = 0.28 m) |

   - **Same everywhere:**
     - the cube footprints (2x1, 1x1, 2x2), so siting does not move;
     - the flue (3 x 3 cells, now 0.141 m², at least 1/10 of each opening on both grids);
     - the stack (5 x 5);
     - the fuel pile: the same 2-billet bed plus a crossing log.
   - **Ridge clearance:** `ridgeClearance(g)` is the smallest cell count that clears 2 ft. That is 6 on 1/9 m
     (0.667 m) and 5 on 1/8 m (0.625 m).
   - **Code:** one paint routine for both grids. The feature sizes (slab, jamb, lintel, deck, opening spans) are
     named `Body` fields; on legacy they hold the original literals, so the legacy canvas is byte-identical (the 20
     `ForgeParityDigests`).
   - **The forge has no centre cell.** Its body is an even 8 cells wide, so the flue sits 1/16 m off centre,
     under the hood.
   - **Tests** (red on a stub with no 1/8 m bodies: 4/4 failed):
     - `HearthForgeTest.FoundationBodiesArePickedFromTheCanonMetres`, `RidgeClearanceIsTheSmallestCountClearingTwoFeet`
       and `FlueAreaServesTheFireboxOpeningOnBothGrids`;
     - `BuildingHarness.FoundationGridPaintsMetreHearth`, which replaced `FoundationGridRefusesNinthAuthoredHearth`;
     - `BuildingHarness.HearthBodiesSitFlushOnEveryWallOnBothGrids` (3 types x 4 walls x 2 grids). It checks
       that the cell behind the breast is wall, the fire anchor is in air, the flue is open from the throat to the
       cap, and the cap is exactly `ridgeClearance` above the ridge. A seeded defect (the oven fire anchor moved
       into the coal row) failed all 4 oven cases.
   - **Eye check:** offline renders of the C++ canvas (`PHYXEL_HEARTH_DUMP`, then the contact-sheet renderer).
     The engine can't load the grid yet.
   - **Found while testing, and fixed: KI-5i** (`structure-generation/BuildKnownIssues.md`). On every grid, a
     piece that is not a whole number of cubes deep floated off +x/+z walls. That covers 60 of 81 committed
     templates; the wall lantern stood 0.78 m off a +z wall. Fixed in the shared anchor (`flushAnchorMicro`).
   - **Still legacy:** siting (`StructureForge` passes ninth wall thicknesses to `siteAllStories`). On 1/8 m
     the realizer re-derives the pose with its own cells and re-applies the routing gate. The furnish pass is
     still 1/9 m only.
5. **Retire** `gen_tree.py` (bush presets in tree_forge) and port tree_forge to C++ for procedural flora.
   **Bushes done 2026-11-11.** Finding: gen_tree's "height 2" bushes are not 2 m: `bush_round` is 5.3 x 3.3 x 4.3 m
   (27 m³ leaf, no wood), `bush_leafy_s` 5.0 x 3.3 x 5.7 (32 m³), `bush_leafy_m` 5.0 x 4.3 x 5.7 (50 m³). Replaced by
   `forge_bush_round` / `_leafy_s` / `_leafy_m` (tree_forge `bush` preset, `canopy_r` 2.5-2.8 m) sized to them:
   5.3 x 3.3 x 5.3 (32 m³), 6.0 x 3.3 x 5.7 (32 m³), 5.7 x 4.3 x 6.3 (47 m³), baked on both grids; all 16 biome
   references swapped. Eye check: asset editor (port 8120), old vs new at the same pose, same size and read; the
   forge versions are lumpier and leafy_m shows a short trunk stub (owner review welcome). No live biome references
   gen_tree output any more; the remaining non-forge live flora (`fern`, `shrub_flat`, `shrub_round`,
   `bush_flower`, `tree_apple`) is hand-made/imported (step 7). Flora cost re-measured with the bushes on the grid:
   forests 3.90 and 3.95 MB per chunk footprint (+3 %), 6.05-6.13 detailed blocks per column; only `fern` still
   missing there.
6. **Terrain:** creek beds and the bridge parapet follow `bedHeight` at 1/8 m.
7. **Hand-made templates:** later, by resampling (volume per material within one voxel) or re-authoring, reviewed
   by eye.
8. **Lint:** a check that fails if any generator still writes on a third or ninth grid.

Exit: every template loads on the new grid; the lint passes; the structure generator's validation suite (L2/L3 in
`ValidationLedger.md`) passes on the new grid; screenshots of the whole library reviewed.

This work runs alongside F1-F3 and must finish before F7 switches the default.

## 8. Test harnesses (built before the features they test)

Every rig is small, inside one chunk, one variable at a time, with a written prediction and a control (CLAUDE.md).

**Storage / draw lab (F1-F2):**
- A 64 x 32 x 64-voxel-block area generated by the engine's own generator: terrain + a building + trees.
  Hand-placing is not acceptable as a substitute.
- Measured on old vs new: memory, read-interface answers (equal on every query), pixel diff at 5 fixed poses, frame
  time, edit cost.

**Fracture lab (F4-F5):**

| Rig | Prediction | Measured |
|---|---|---|
| Stone slab, hit at centre and at an edge, energy swept 10 steps | piece count rises and piece size falls with energy; an edge hit chips the edge | piece count / size histogram per energy; conservation |
| Brick wall (parts) | breaks along mortar: > 80 % of cut bonds lie between parts | cut bonds between vs within parts |
| Wooden beam (grain along x) | splits along x: crack area aligned with the grain > 3x across it | crack orientation histogram |
| Glass pane | radial + concentric shards | shard angle distribution |
| Column under a lintel, column removed | lintel unanchored → becomes a body; the wall beside it stays | anchoring correct per voxel |
| A chair item | breaks into item pieces at its joints | pieces, conservation |
| Control | a hit below toughness: nothing cut, damage channel rises | exact |

Every rig is checked for:
- conservation (exact);
- determinism (two runs equal);
- GPU = CPU reference (equal, as MatSim's parity tests);
- GPU ms per break at 10 / 100 / 1,000 pieces;
- a video for the owner.

## 9. Phases (each: prediction, red test, build, stress, owner sees it)

| Phase | What | Exit test | Stop if |
|---|---|---|---|
| **F0** | Decide §13; write the two harness skeletons; the content inventory (§7a step 1). **Done 2026-11-10:** §13 decided; inventory in §7a.1-7a.2; save check in §3.5a point 7; skeletons `engine/*/core/voxfound/` (`VoxelGrid`, `FractureLab`, `StorageLab`) + `VoxFoundFractureLabTest` (4 checker tests green, 8 predictions red until F4) + `VoxFoundStorageLabTest` (3 green incl. measured terrain stats, 3 red until F1) | — | — |
| **FC** | Content conversion (§7a), **generators first** (§7a.3), alongside F1-F3. **Progress 2026-11-11:** generator map (§7a.3); `tools/voxgrid.py` grid emitter; `tree_forge --grid 8` (legacy byte-identical) and the 1/8 m forge library in `resources/templates_g8/nature`; `StructureGrid` + the building shell on 1/8 m (BuildingHarness verdicts equal on both grids, legacy byte-identical; step 3a); gen_tree bushes retired (step 5); `regen_furniture --grid 8` (34), `gen_door --grid 8`, `gen_items --grid 64` (52), `gen_seat_variants --grid 8`: **step 4 complete**; hearth bodies on 1/8 m (4b) and the KI-5i +side wall-flush fix; next: furniture placement in C++, the other structure generators | lint passes; structure validation suite green on the new grid; library screenshots reviewed | — |
| **F1** | New store (CPU reference, then GPU) + read interfaces; convert the generated area; measure the §3.5a budgets (memory per chunk by biome, directory size, chunks generated per second, mirror read-back). **Progress 2026-11-11:** CPU `BlockStore` (directory + detailed pool, `collapse()` per edit batch with pool compaction, solidity summary) equal to the dense reference on terrain and under 200 batches of random brush churn (5 tests red on the stub, then green); flora cost measured (§3.5a) | memory ≥ 5x better on the town; every read-interface query equal; a 256 m radius fits in ≤ 3 GB | memory win < 5x; generation too slow to keep up with a sprint |
| **F2** | GPU face building (hidden-face culling, merging in and across blocks) + drawing through today's lighting; the §4.6 render optimizations rebuilt on blocks (culling, occlusion, indirect draws, shadow casters, block LOD pyramid, direction culling); T-junction handling | the same engine-generated scenes that run today run on the new store in the same performance tier (whole frame, GPU memory, bytes per frame), measured at 5 poses; pixel diff within the gate | a scene that works today doesn't, and we can't find and fix why |
| | **F2.0 CPU reference mesher, 2026-11-11** (`voxfound/Mesher`, `VoxFoundMesherTest`): culled faces, greedy merged across the whole area, greedy limited to 8-voxel blocks; `coverageErrors` proves a merge covers every exposed face exactly once. Light is not in the merge key yet, so every count is a LOWER bound. **Predictions written before the first run** (bare Perlin 32 x 32 m footprint): culled 70k-200k; greedy 3k-15k; today's 1 m cubes (proxy) 0.5k-3k; greedy / proxy 3-10x; per-block / unbounded 1.5-3x; the building shell at 1/8 m within ±30 % of 1/9 m. Measured: see §9a | | |
| **F3** | Edits with brushes; codec v3 + converter | edit cost per block ≪ today's 2-13 ms chunk rebuild; save/load round-trips exactly | — |
| **F4** | Fracture core, CPU reference: bonds, patterns, pieces, anchoring, crumbs. Then the GPU port, equal to the reference | the fracture-lab rigs pass their predictions; conservation and determinism exact | the patterns don't read as real cracks to the owner (video review) |
| **F5** | Bodies: extraction, GPU simulation with voxel collision, drawing with one transform, the rest/bake-in hybrid | 100 → 1,000 bodies; conservation; pop measured | body cost at 1,000 above budget with no lever left |
| **F6** | MatSim on the world grid (liquids, powders, crumbs) | MatSim's tests on world voxels | — |
| **F7** | Move the consumers over (§7), default switched | full test suite green; L4 live checks | — |
| **F8** | Scale and stress: a whole town, 100 simultaneous breaks, 1,000 edit/break cycles, crossing chunk borders | invariants hold at every step | — |

## 9a. F2 measurements (face counts)

First run 2026-11-11, `VoxFoundMesherTest` (Debug), CPU reference mesher, engine generator content, 32 x 32 m
footprint at 1/8 m, outer boundary not counted (`Outside::Skip`). Light is not in the merge key: every count is a lower
bound. "Today proxy" = the centre voxel of each metre, greedy-merged within 32 m (what today's 1 m cube terrain draws,
minus the light splits).

| Scene | culled | greedy (whole area) | per-block (8) | today proxy | greedy / proxy | per-block / greedy |
|---|---|---|---|---|---|---|
| Perlin s7 (0,0), bare | 70,327 | 1,983 | 3,838 | 55 | 36x | 1.94x |
| Perlin s12345 (-1024,768), bare | 66,562 | 839 | 2,003 | 18 | 47x | 2.39x |
| Mountains s7 (2048,1024), bare | 114,554 | 45,048 | 51,443 | 1,047 | 43x | 1.14x |
| Perlin s7 (0,0) + flora | 163,297 | 17,048 | 24,318 | 813 * | 21x * | 1.43x |
| Perlin s7 forest (8160,96) + flora | 640,406 | 106,123 | 134,468 | 4,565 * | 23x * | 1.27x |
| Building shell 9 x 7 m, 2 stories | 1/9 m: 77,484 / 1/8 m: 61,608 | 1/9 m: 404 / 1/8 m: 401 | — | — | 0.99x (1/8 vs 1/9) | — |

\* not a fair "today": today's trees are already 1/9 m micro templates, which the 1 m proxy flattens to cubes.

**Against the predictions:**
- culled 70k-200k: **held** (66k-115k bare).
- greedy 3k-15k on bare Perlin: **wrong, better** (0.8k-2.0k). Mountains 45k is outside it the other way.
- today proxy 0.5k-3k: **wrong** (18-55; gentle Perlin is only 2-4 one-metre levels in 32 m).
- **greedy / today 3-10x: FALSIFIED — 36-47x on bare terrain.** I predicted linear 8x scaling. Wrong model: a
  slope cut into terraces has 8x as many contour lines at 1/8 m, and each contour is 8x as many cells long; greedy
  rectangles pay per step along each contour, so merged faces scale near 8² on smooth relief, not 8.
- per-block / unbounded 1.5-3x: held on gentle ground (1.9-2.4x), **wrong on rough ground and flora** (1.1-1.4x —
  little there merges past 1 m anyway).
- building shell within ±30 % of 1/9 m: **held** (0.99x). Walls and floors are axis-aligned slabs; grid size doesn't
  change their rectangle count.

**What this means for F2** (rewritten 2026-11-11 after the owner's correction; the first version treated "no more
faces than today" as the goal, which was never the requirement):
- These counts are cost inputs, not a pass/fail. The gate is in §3.5a point 4 and the F2 row: the scenes that run
  today must still run, in the same tier, measured whole-frame on the GPU path. If one doesn't, find out why and fix it.
- The numbers say where to look first when that is measured. Smooth relief at 1/8 m has many more terrace steps, so
  its merged face count grows close to 8² over 1 m cubes. Then forests (106k faces per 32 x 32 m).
- The building shell costs the same at 1/8 m as at 1/9 m.
- Merging across blocks is worth 1.1-2.4x.
- Owed: today's real face count on the same ground. The proxy has no light splits and flattens micro trees.

## 10. Performance tiers

The knobs that scale cost:
- the bake-in distance and the body budget;
- the crumb threshold and crumbs-as-powder vs particles;
- the simulation's awake radius;
- the merge-across-blocks pass;
- (later) marching vs faces.

Outcomes that never depend on the tier:
- which voxels exist;
- which bonds break;
- which pieces form.

## 11. Risks

1. **It is the engine's world layer.** Weeks of work. It must keep the game running throughout: a switch, both paths
   until F7.
2. **The CPU mirror lags 1-2 frames** (§3.4). Readers that can't tolerate it need GPU queries. Audit in F7.
3. **Voxel-vs-voxel body collision on the GPU** is new and is the likeliest cost problem. F5's 1,000-body stress
   decides.
4. **Crack realism is judged by eye.** The harness gives numbers (orientation, piece sizes, part-boundary share); the
   owner's video review is the real gate.
5. **Rotation at rest** (§6.7): baked pieces shift by up to half a voxel; kept pieces cost memory.
6. **The world simulation and fracture share the grid.** Concurrent writes in one block need an order (simulation
   then fracture, per tick).
7. **Content and unit conversion** (§7a, sized in F0).
   - Templates: 229 of 238 don't convert exactly, but 186 of those regenerate from generators.
   - Code: about 4,500 sites in about 270 files assume 3 or 9 per metre. Most of them are in subsystems this plan
     replaces anyway.
   - The real risk is the ~1,000 structure/settlement sites in 1/9 m units and the navigation gameplay numbers
     (agent height 17/9 m, step-up 4/9 m). A careless `* 9` → `* 8` silently changes every proportion.

## 12. What stays as it is

- items as their own kinematic grids (their grid moves from 1/81 m to 1/64 m, §6.8);
- chunk size (32 m) and chunk coordinates;
- characters;
- far terrain and far-tree LOD (tiers 4-6, independent of chunks);
- the lighting model and its gates;
- the HTTP API's shape (coordinates change units; cube and subcube become brush sizes).

## 13. Owner decisions (all decided 2026-11-10)

1. **Voxel size: 1/8 m** (D5). Rejected:
   - 1/9 m: it keeps today's content exact, but leaves blocks at 0.89 m and chunks at 28.4 m, off whole metres.
   - 1/9 m with 36-block chunks (32 m): it fixes the chunks only.
2. **Resting pieces: the hybrid** (D6, §6.7 c).
   - Rejected: keep every piece as a body forever (the cost grows with every break).
   - Rejected: always bake a piece in at rest (it shifts by up to ~6 cm and can't be picked up as a piece).
3. **No collapse under weight in v1** (D7). Revisited after the fracture-lab videos.
4. **Level of detail: variable, with many steps** (D8, §3.5a point 3). There is no fixed detail radius. The level is
   chosen per region by size on screen, with ~16 power-of-two levels from 12.5 cm to ~4 km, cross-faded between
   levels. A budget controller sets how much detail the machine can afford.

Build order (not an owner question): F0, then F1 (storage). The fracture rules (F4) are proven on a plain voxel grid
alongside F1, because they don't depend on the new storage.
