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
| 7 | Distance tiers: far-LOD pyramid (tier 3), far terrain heightmap tiles (4), tree LOD meshes + cards (5, 6), structure proxies (7), grass/foliage (8a, 8b) | `LodTierLedger.md` | The block LOD pyramid replaces tier 2/3 for blocks. The §9a numbers say full-detail 1/8 m terrain and forest need it within tens of metres. Tiers 4-8 must keep their handoffs (residency gates) to the new near tier | pyramid levels 1/8 → 1 m built (F2.13: half-solid rule, surface material, chunk-independent; whole region at 1/4 m −3.2..−5.2 ms, at 1/2 m −4.4..−6.8 ms); selection by size on screen + dithered cross-fade done (F2.14: per fragment, 0 holes / 0 fattening; P=8 −3.3/−3.5 ms from above, neutral at the ground; default off); coarse casters built (F2.14b: one-level lag vs the main view, 0.055 % px; −1.2/−2.4 ms from above at P=8, +0.2..+1.2 ms elsewhere, default off); sprig → leaf-mass handoff built (F2.14c: no holes, but the mass looks wrong — cutout card texture on solid faces — and saves ≤ 0.7 ms; default off; mass appearance owed); levels 2-16 m built (F2.14d: region pyramid, exact chunk by chunk; top within ½ cell; P=8 0 px, no saving in a 128 m region, +0.1-0.2 ms at P=32; 32 m+ needs a flags re-layout; ground under coarse canopy cells owed); GPU level build done (F2.14e: exact, 0.58 ms per chunk area, region level time 11.7 → 2.4 s); leaf-mass look done (F2.14f: own pipeline shaded as cards, luma 74.7 → 106.2 vs the cards' 107.9, mass error halved; +0.14/+0.16 ms at the ground; handoff still off); two pyramids built (F2.14g: no hidden tops, holes at P=32 gone, but farther from level 0 at P=8 — off; found: canopy-interior faces cut sprig overdraw 2-3.7 ms); leaf-fill knob built (F2.14h: no fill closes the canopy gap — it is sub-voxel shading; off; its whole-frame A/B owed, voided by a shared GPU) |
| 8 | Face direction culling (skip faces that point away from the camera as a group) | not done today (faces are camera-convention wound and back-face culled per triangle) | NEW: keep faces sorted by direction per block/chunk so up to 3 of 6 groups are skipped before the vertex stage | main view done (F2.15: direction groups per 4-block cluster in the cull pass, 0 px, −0.30..−0.45 ms at 4 of 5 poses, neutral at the fifth; default on); shadow casters by light direction built (F2.15b: −0.85..−1.4 ms but leaks at leaf-adjacent trunk faces; closed casters fix it at +1-2 ms — off; F2.15d: two cheaper closures measured, neither closes the leaks chunk-exactly, reach 24 only −0.1..−0.5 ms — off, closed for now); lower command count done (F2.15f: ordered compacted + merged commands via indirect count, −0.07..−0.23 ms at 4 of 5 poses, neutral at the fifth; default on) — DONE |
| 9 | Incremental rebuild | today a chunk rebuild costs 2-13 ms | Only changed blocks rebuild (F3 gate) | **F3 gate CLOSED 2026-11-16 (F3.5 second run): every brush size 1-8 anywhere incl. chunk faces / edges / corners 0.11-0.42 ms median request to lists ready, exact under 1,000 verified edits + 10 reloads (F3.5a: only the chunks that change are re-planned / assembled; F3.5b: incremental connectivity). Earlier: cost met in F3.1 (2026-11-16): a 1 m edit 0.53 ms median request to lists ready (rows over the runs near the footprint, the flood only when a block's sight bit flips, assemble in place)**; built and exact (F2.16: slice/row delta of the run merge + sprig cells + block connectivity + cluster order; 0 mismatches in 214 live verified edits up to 4 m across chunk borders, 3D view identical to a full rebuild); cost NOT met — CPU 1 voxel 1.05 / 1 m 4.5 / 4 m 48 ms (whole-width rows), device rows exact but their synchronous readback waits 10-20 ms for the GPU at the uncapped operating point; CPU default, device opt-in for item 10; next-frame re-upload ~9 ms owed to item 10 |
| 10 | Face pool memory (region arenas) | `ChunkArenaSystem` | Face pool sub-allocated per block / chunk | done (F2.17: per-chunk spans of the region arena allocator, device-local, edits copy only their spans + rewrite the cull table in place; next-frame upload 8.7-14.5 → 0.16-0.18 ms; whole frame −2.3..−3.8 ms at all 5 poses (shadow passes: no more PCIe reads); 0 px; pool image 1.6x the live faces; default on) |

Known defect that gets worse with more merging: **T-junction cracks at merge borders** (`RenderOptimization.md:489`).
Cross-block merging creates more T-junctions, so F2 has to handle it, not inherit it. **Handled (F2.18):** each quad
grows by 1/16 px on screen in its own plane — crack px 532 → 1 over the 5 poses, whole frame neutral, default on.

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
- (F2.19: the 1/8 m ground had dirt stripes along contour lines here, visible in this run's B captures; fixed in F2.19.)
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

**F2.14d 2026-11-14 — §4.6 item 7 continued: levels above 1 m (2, 4, 8, 16 m) from the region pyramid.**
- Design: a chunk's 1 m margin covers its own levels only up to 1 m (a level reads one cell around each cell). Levels
  4..7 come from the REGION: every chunk's exact 1 m level is cut to its inner 32³ cells and pasted, all channels
  (count, surface, both votes), into one region level (`lodCut`/`lodPaste`/`lodEmpty`, Mesher.h); `lodDown` runs on the
  region; each chunk's level-k cut is meshed with its neighbours from the region grid (outside the region = the
  residency edge, air). `voxfound_preview {"lod_levels":n}` now takes n ≤ 7; runtime `{"lod_max":n}` caps the levels
  the selection uses (the A/B, no rebuild). 7 is the deepest the 3-bit maxLevel of the flags word holds: level 8
  (32 m, one cell per chunk) and the levels above chunks need a flags re-layout — not in this step.
- Red first: `LodLevelsAboveTheMarginComeFromTheRegionExactly` (chunk by chunk with the clipped 1 m margin, pasted,
  then the region pyramid == the pyramid of the region built whole, every channel, levels 3..7; a level-7 slab still
  fits the 8 B record inside a 32 m chunk: 256-voxel extents, +side face at 255) — red on stubs. `lodTopStats` (the
  view from straight above: per column the top non-card cell, its error vs a finer level in coarse cells, material
  shares, columns whose top is under a card cell) pinned by `LodTopStatsMeasureTheViewFromAbove` — red on a stub.
  CPU 50/50.
- Measured vs the prediction (`voxfound_f214d_prediction.txt`, written before any code):
  - Faces, whole gate region (L3 = 9,325): L4 2,342 / L5 539 / L6 167 / L7 103 (predicted 2.5-3.5k / 0.8-1.2k /
    250-450 / 80-180: WRONG low at 4-8 m). Solid volume vs L0: −1.6 / −3.3 / −6.8 / +0.6 % (held, ±5 / ±15 %).
    Region pyramid + meshing: 0.26 s on the ~38 s region build (held, < 1 s).
  - Unit, one 32 m forest area (`MeasuredLodLevelsOnAGeneratedForest` now runs to level 7): volume 0.93 / 0.92 /
    0.63 / 1.25 and the old share gate fails from 4 m — WRONG; with 8×8 columns at 4 m and 2×2 at 16 m a column
    moves by up to half a cell and thin trunks vanish. An assert of top error ≤ 0.5 cell there failed too (0.53 / 0.56
    at 4 / 8 m, max 4.75 cells = vanished trunk tops). Kept as printed measurements; the gate is the whole region:
  - Look gate on the whole region (`lod_top_view` in the build response), level k vs the region's 1 m level: mean
    top error 0.48 / 0.37 / 0.31 / 0.49 cells (held, ≤ 0.5). Bare terrain (flora off): max error EXACTLY 0.5 at every
    level (the half-solid rule's bound). Shares (≥ 5 %) within 10 points at 2-8 m (held); at 16 m Stone 0 → 11.7 %
    (FAIL). Dug in: every one of those Stone columns is a hidden top (hidden 8.2 % / 11.7 % at 8 / 16 m = the Stone
    share): a coarse CARD cell right above holds the ground surface and the leaves, so the ground cell below covers
    no surface (volume vote: Stone) and the card cell hides its top face — that top is never drawn.
  - Coverage gate (`voxfound_f214d_gate.json`, lod_max 3 vs 7, today's world hidden, wind 0, GI off; controls 0.000 %
    everywhere): P=8 0 px at all 5 poses (held: no level ≥ 4 is picked in this region). P=16 ≤ 0.51 % px change,
    gained ≤ 431 px, lost ≤ 4. P=32: overview_south 9.2 % (gained 13,805), top_down 3.8 %, east_low 1.1 %. ALL
    16,821 gained px and 765 of 838 lost px lie within P/2 px of the coverage outline (`voxfound_f214d_outline.json`):
    the region's cut edge seen against the void, moved by up to half a coarse voxel
    (`voxfound_f214d_os_P32_AB_gained.png`). "0 fattening" was wrong as a PIXEL claim at P ≥ 16: the half-solid rule
    bounds the shift (≤ P/2 px), it does not make it 0. The other 73 px are holes under the canopy: the hidden ground
    top above, where the level-0 sprigs leave a gap (`voxfound_f214d_os_farlost.png`).
- A fix tried and REVERTED: meshing coarse levels with card cells as air (they draw nothing there). Red-first test
  green, it covered the holes — and the look gate against the LEVEL-0 REFERENCE (`voxfound_f214dc_ref.json`, over the
  pixels the two meshings differ) put it farther from level 0 at 9 of 10 pose/P points (P=8: 0.1-6.6 → 38-67 mean
  error; only overview_south P=32 improved, 60 → 28). It exposed the branches inside the canopy and the hidden cells'
  Stone, and the "holes" it covered at P=8 were background level 0 shows too, through sprig gaps — a coverage metric
  against the hidden preview cannot tell those apart; a reference render can. (Level-1 faces went 127k → 288k.)
  Owed: a coarse card cell that holds the ground surface should draw that ground (a non-card surface vote), gated
  against the level-0 reference. It only shows at P=32 here.
- Whole frame, lod_max 7 − lod_max 3 at the same P (Release, perf_harness 8 pairs × 5 poses,
  `voxfound_f214d_ab_P*.{json,jsonl,txt}`): P=8 every CI includes 0 (|median| ≤ 0.08 ms; identical work, held);
  P=16 every CI includes 0 (top_down +0.28 median); P=32 overview_south +0.09*, east_low +0.21*, canopy_close +0.10*
  (* CI excludes 0), the others include 0. Held (≤ 0.3 ms): in a 128 m region the 1 m level is already ~9k faces, so
  coarser levels cannot save; the extra band (two levels drawn where they meet) costs a little at P=32. Their win is
  at the streaming radius and beyond (§3.5a point 3), with the far tiers' handoff.
- Default: selection stays OFF (`lod_auto`); a build without `lod_levels` > 3 is unchanged.

**F2.14e 2026-11-14 — §4.6 item 7 continued: the GPU port of the level build.**
- Built: `LodBuilderGpu` + `shaders/voxfound_lod.comp`: levels 1..n of a chunk's 1 m margin area on the device, one
  thread per coarse cell, no ordering between threads. Pass 0 = level 1 straight from the voxel bytes (lodBase fused
  in: a voxel's surface = it touches air inside the grid); pass 1 = level k+1 from level k (lodDown's cell step: count
  sum, surface and volume votes with lowest-material ties, the half-solid rule); pass 2 = lodDown's neighbour step
  (reads only count / surf / votes, writes mat). Every level's channels are concatenated in one buffer each and read
  back as LodGrids. The region build uses it for each chunk's levels 1-3 (`{"lod_gpu":bool}`, default true; false =
  the CPU reference); the region pyramid (levels 4-7) stays CPU (0.16-0.29 s per region).
- Red first: `VoxFoundLodBuilderGpu` (tests/integration; random content at 5 sizes with up to 9 materials, a thin
  surface layer that makes the neighbour step decide every top cell, the engine forest chunk area — 272³ voxels,
  87 plants — and refusals), red on a stub, then exact on the first build: every channel of every level equals the
  CPU lodBase + lodDown (2.87 M cells on the forest area). GPU 17/17, CPU 50/50.
- Measured vs the prediction (`voxfound_f214e_prediction.txt`): device time 0.58 ms per chunk area (held, < 5 ms).
  Whole gate region (32 chunks, lod_levels 7, `voxfound_f214e_check.json`): identical faces and solid volume at every
  level; level time 11.74 s (CPU) → 2.41 s (GPU; 34 ms of it on the device, the rest host copies and unpacking the
  read-back channels) — predicted < 1.5 s, WRONG; region request 38.5 → 30.5 s — predicted ~24 s, WRONG: terrain +
  flora generation is 23-25 s of it, now the dominant cost. Pixels: the GPU build vs a CPU rebuild 0 px at
  overview_south P=8 (CPU built twice: 0 px; the session's first capture was 6 px off by one colour level from both).
- Not a frame-time item (a build path); next lever for build time is the generation itself, which is not in item 7.

**F2.14f 2026-11-14 — §4.6 item 7 continued: the leaf-mass appearance (owed by F2.14c).**
- Built: `shaders/voxfound_leafmass.frag`, its own pipeline pair (`RenderPipeline::ensureVoxFoundLeafPipelines`:
  `voxfound_face.vert` / `voxfound_face_mdi.vert` + this, the preview's state and layout). The coarse leaf mass is
  shaded EXACTLY as `foliage.frag` shades a leaf card: albedo = the leaf texture's opaque average (16 taps at mip 1,
  the texels the cards' alpha cutout keeps), up-facing probe ambient, sun 0.7 × shadow × sun gate with no N·L,
  backlit transmission, lamps, × 0.91 (the mean card shade), no aerial haze; voxel.frag's LOD discard kept. Leaf-mass
  clusters are ordered last and drawn by a second multidraw with its own origin table (a second set-1 descriptor set);
  the direct path draws every leaf range after every solid range. Runtime A/B `{"lod_leaf_look":bool}` (default
  true). `voxel.frag` is untouched (byte-identical .spv). LightingPipeline.md gains the receiver row.
- Red: the F2.14c look failure, re-measured in the same session with the new look off (B0): top_down P=8 49.18 % px
  changed vs the cards (A), luma 107.85 → 74.67; overview_south 20.60 % (`voxfound_f214f_gate.json`, controls 0.000 %).
- Green (B): top_down luma 106.20 (prediction ±6: held); mean error on the leaf-mass pixels vs the cards 90.9 → 35.8
  (top_down), 73.1 → 35.4 (overview). Pixels changed vs the cards 24.26 % / 10.93 % — predicted < 20 / < 10 %: WRONG.
  The rest is SHAPE, not colour (`voxfound_f214f_zoom_top_down_P8.png`): the half-solid ½ m mass is blockier and
  thinner than the card canopy (cards overhang their cells), so the darker understory shows in its gaps. Owed: the
  coarse canopy's shape (its extent vs the cards'), gated against the cards.
- Two traps found on the way, both fixed:
  - The first build put this shading in a BRANCH of `voxel.frag`. Old vs new `voxel.frag` in separate sessions
    (old/new/old/new; no hot reload), handoff off so no leaf mass is drawn at all (`voxfound_f214f_shader_sessions.json`):
    canopy_close 23.75 / 24.71 old vs 25.21 / 25.33 new, forest_floor_open 43.28 / 42.28 vs 43.63 / 43.96 — every
    preview fragment (and today's static world, which shares the shader) paid ~0.5-1 ms. Hence the own pipeline.
  - Then the multidraw and the direct draws differed by 81 px (look + handoff on only): where two kept levels'
    coplanar faces tie in depth the first drawn wins, and the direct path interleaved solid and leaf per chunk (chunk
    order decided the winner). Same order in both paths now: 0 px in all 8 combinations (`voxfound_f214f_paths.json`).
- Whole frame, B − B0 at P=8, 8 pairs × 5 poses (`voxfound_f214f_ab_P8_pipe.*`): overview −0.015, top_down −0.002
  (CIs include 0), forest_floor_open +0.14*, east_low +0.16*, canopy_close +0.02* ms (* CI excludes 0). Prediction
  (≤ 0.1) WRONG at the two ground poses: the card model per mass fragment (16 taps, two shadow maps, the lamp loop).
- Default: the handoff stays OFF (`lod_sprig_level` 0): it saved ≤ 0.7 ms from above and cost at the ground (F2.14c),
  and the shape gap remains. The leaf look is the default whenever the handoff is built.

**F2.14g 2026-11-14 — §4.6 item 7 continued: coarse levels from two pyramids (the ground under coarse canopy cells).**
- Built: `VoxFound::lodSplitCards` splits a chunk's area into a SOLID grid (card voxels → air) and a LEAF grid; each
  gets its own pyramid (GPU, and its own region pyramid for 2-16 m). A coarse level meshes the solid pyramid alone
  (leaves never hide ground or trunk faces, and a ground cell's surface vote sees the ground); the leaf pyramid is
  meshed only where the leaf mass is drawn (F2.14c handoff). Level 0 unchanged. Build `{"lod_split_cards":bool}`
  (default FALSE, see below); `{"lod_card_ab":true}` also builds the other meshing as an alternate level set
  (`Preview::levelsAlt`) and `{"lod_cards_split":bool}` swaps it at runtime (interleaved A/B).
- Red first: `CoarseLevelsFromTwoPyramidsKeepTheGroundUnderTheCanopy` (ground + a canopy sharing its level-3 cell:
  every column keeps a grass top face, no card material in the solid pyramid, only card material in the leaf
  pyramid), red on a stub; CPU 51/51.
- Measured vs the prediction (`voxfound_f214g_prediction.txt`):
  - Top view (`lod_top_view`, region): hidden tops 3.9-11.7 % → 0 at every level, Stone 8.2 / 11.7 % → 0 at 8 / 16 m,
    mean top error 0.48 / 0.35 / 0.23 / 0.39 cells, every share within 6.5 points at 2-16 m (held; fixes F2.14d's 16 m
    share failure).
  - Faces at ¼ / ½ / 1 m: 295,769 / 54,173 / 11,102 vs 127,280 / 32,796 / 9,325 (+132 / +65 / +19 %; predicted ±30 %:
    WRONG) — canopy-interior branches are half-solid at ¼ and ½ m and get faces level 0 hides under its leaves.
  - Look gate against the LEVEL-0 REFERENCE (`voxfound_f214g_ref.json`): holes more than P/2 px from the reference
    outline at P=32 overview_south 273 → 0, top_down 19 → 0, east_low 17 → 12, forest_floor_open 8 → 8; but over the
    pixels the two meshings differ, two pyramids are closer to level 0 at only 2 of 10 points (P=32 overview_south and
    canopy_close; predicted ≥ 8: WRONG). At P=8 one pyramid matches level 0 almost exactly (mean error 0.1-16), two
    pyramids show the interior branches (34-66).
  - Whole frame, two − one pyramid (`voxfound_f214g_ab_P*`): P=8 overview +0.06 (n.s.), top_down +0.24*,
    forest_floor_open −2.32*, east_low −1.30*, canopy_close +0.74*; P=32 −0.06 / −0.08 (n.s.) / −1.61* / −2.64* /
    +0.39* ms (predicted |Δ| ≤ 0.3: WRONG both ways). Dug in (scopes): the preview's faces cost +1.0-1.2 ms and the
    SPRIGS save 2.0-3.7 ms — the canopy-interior faces, drawn before the sprigs, fill depth inside the canopies, so the
    sprigs behind them fail the early depth test. An opaque core inside a canopy is a lever on the largest ground-pose
    cost (sprig overdraw); not taken here because the core is visible through sprig gaps.
- Default: ONE pyramid. Two pyramids fix the holes only where they show (P=32) and move P=8 away from level 0. Kept,
  off, for the canopy-shape step (a leaf threshold on the leaf pyramid alone). Trap found on the way: an API call
  carrying a `lod_*` key and `show_preview` only applies the `lod_*` branch (the first gate run measured a hidden
  preview: A = B = background) — send them separately.

**F2.14h 2026-11-14 — §4.6 item 7 continued: the coarse canopy's shape vs the cards (a leaf-fill knob).**
- Built: `VoxFound::lodLeafMass` thresholds a LEAF pyramid level (F2.14g's leaf voxels only) at fill t: a cell is leaf
  mass when it covers ≥ t × 8^k leaf voxels (½ = the half-solid rule; lower = fatter, as cards overhang their cells).
  Build `{"lod_leaf_fill":t}` (0 = off, the default: the F2.14c/f mass); the solid geometry keeps its meshing.
  `{"lod_alt":{"split_cards":bool,"leaf_fill":t}}` builds a second variant and `{"lod_alt_on":bool}` swaps it.
- Red first: `LeafMassFillThresholdsTheLeafPyramid` (3/8, 8/8 and 1/8 covered cells at fills ½, ¼, ⅛; the volume vote
  for a cell with no surface), red on a stub; CPU 52/52.
- Gate vs the cards (F2.14f rig, P=8; `voxfound_f214h_gate.json`, `voxfound_f214h_blocks.json`):
  - px changed (> 24 levels) top_down / overview_south: old mass 24.3 / 10.9 %; fill ½ 24.5 / 11.0; ¼ 24.1 / 11.6;
    ⅛ 25.7 / 13.3 — predicted 15-20 % at ¼: WRONG, no fill improves it. Luma (cards 107.9): 106.2 / 105.8 / 114.5 /
    119.4. Lower fill fills the gaps (coverage lost 471 → 56 px) and fattens against the sky (gained 30 → 507).
  - At the scale a level-2 voxel covers at P=8 (8×8 px blocks): blocks > 12 apart, top_down 33.5 / 34.0 / 40.8 /
    49.3 %, overview 13.3 / 13.3 / 20.4 / 27.2 % — lower fill is WORSE. The remaining gap is sub-voxel SHADING, not the
    outline: a mass is a uniformly lit surface where the card canopy has internal shade (leaves shadowing leaves,
    gaps onto darker layers).
  - Faces (levels 2 / 3): 142,231 / 31,112 → 160,480 / 31,229 (½), 165,515 / 36,077 (¼), 167,032 / 35,581 (⅛) —
    predicted ×1.3-2.5: WRONG (×1.17): most ½ m and 1 m leaf cells are already nearly full.
- Whole frame, fill ¼ − the old mass at P=8 (`voxfound_f214h_ab_P8.*`, rerun with the GPU free; two earlier runs
  were void, another application shared the GPU): overview +0.11*, top_down +0.22*, forest_floor_open −0.08 (n.s.),
  east_low +0.17*, canopy_close +0.18* ms (held, ≤ 0.3).
- Default: fill 0 (off). The handoff stays off: its remaining look gap is the canopy's internal shading, which neither
  the colour (F2.14f) nor the outline (this) closes. That would need shading the mass by its own depth into the canopy
  (an occlusion term from the leaf pyramid) — recorded, not taken: item 7's part list is done.

**F2.15 2026-11-14 — §4.6 item 8: face direction culling.**
- Built: `clusterFaces` orders each cluster's faces by (cell, direction), so a cell's six direction groups are
  contiguous sub-ranges; `splitByDirection` cuts the clusters into those groups over the SAME face buffer, and every
  Level keeps both lists (`clusters`, `clustersDir`). The cull pass (`voxfound_cull.comp`, direction in bits 28-31 of
  the cluster word) drops a group that cannot face the camera: +axis faces lie on planes in [lo, hi] of the axis, so
  they can face it only if cam > lo − 1 cm (−axis: cam < hi + 1 cm); CPU reference `faceGroupMayFaceCamera`.
  `{"dir_clusters":bool}` / `{"dir_cull":bool}`, both default ON. Main view, multidraw path; shadows (their own light
  direction) and the direct path are unchanged.
- Red first: `DirectionGroupsNeverHideAFaceThatFacesTheCamera` (random content, 40 cameras inside and outside: no face
  that faces the camera is dropped, groups behind are), red on stubs; GPU `ClusterCullDropsDirectionGroupsLikeTheReference`
  (6,000 random clusters, 2,345 dropped, 0 different from the CPU; written after the implementation). CPU 53/53, GPU
  18/18.
- Measured vs the prediction (`voxfound_f215_prediction.txt`; gate region, LOD off):
  - Pixels (`voxfound_f215_gate.json`): per cell vs per direction vs per direction + test: 0 px at all 5 poses (held).
  - Commands 2,385 → 10,889 (×4.57, held); faces reaching the vertex stage with the test 55-62 % (predicted 45-60 %).
  - Whole frame, per direction + test − per cell (`voxfound_f215_ab_AC.*`): overview −0.45*, top_down −0.38*,
    forest_floor_open −0.01 (n.s.), east_low −0.31*, canopy_close −0.30* ms. The extra commands alone
    (`voxfound_f215_ab_AB.*`): +0.09* / +0.07 / +0.68* / +0.10 / +0.26* — at forest_floor_open they eat the whole gain
    (predicted ≤ +0.3: wrong there); from above the win is twice the predicted ±0.2.
- Default ON. Next levers inside item 8: the shadow casters (cull by the light's direction), and fewer commands
  (direction groups only where a cell's faces span the camera plane, else the whole cell).

**F2.15b 2026-11-14 — §4.6 item 8 continued: shadow casters by the light's direction (off).**
- Built: the per-cluster shadow multidraw (F2.12 `shadow_mdi`) can upload the per-direction clusters, and the cull
  pass gets a light mode: for a directional light every face of one direction faces it or not at once, so a group
  casts only if dot(normal, toLight) > −1e-4 (`faceDirFacesLight`; `toLight` = −sunDirection from the mid-cascade
  fit). For a closed surface the nearest face along a light ray always faces the light. `{"shadow_dir_cull":bool}`
  (with `shadow_mdi`), default OFF. Plus CLOSED casters, opt-in (`{"closed_casters":true}` at build,
  `{"shadow_closed":bool}`): each chunk meshed with its leaf voxels as air, appended to the face buffer with their own
  direction clusters.
- Red first: `LightFacingFacesAloneGiveTheSameNearestHit` (random closed content, 3,600 rays along 12 light
  directions: 0 differ), red on a stub; GPU light mode = the CPU on 6,000 clusters. CPU 54/54, GPU 18/18.
- Measured (`voxfound_f215b_*`, `voxfound_f215c_*`; prediction `voxfound_f215b_prediction.txt`):
  - Main faces + light test: casts 62.7 % of the faces (predicted 45-55 %). Whole frame vs the default per-chunk
    casters: −1.28* / −1.40* / −0.85* / −1.36* / −1.12* ms (overview, top_down, forest_floor_open, east_low,
    canopy_close). But LEAKS: 3-261 px > 8 levels brighter vs `shadow_mdi` alone (94-97 % of the changes brighter),
    at trunk bases: a trunk face next to a leaf voxel is not meshed (cards hide their neighbours), so a ray entering
    there was stopped only by a back face (`voxfound_f215b_canopy_close_diff.png`).
  - Closed casters fix those (canopy_close 218 → 12 px, top_down 22 → 4) but are ×2.31 the faces (1,109,093; predicted
    +10-40 %: wrong — the branches inside the canopies) and cast 621k after the light test: whole frame vs the default
    +1.07* / +0.95* / +1.63* / +1.94* / +1.81* ms. (forest_floor_open keeps 216 brighter px that are not leaks: the
    light/shadow edge on sunlit trunk sides moves when the trunk's own back faces stop casting.)
- Default: OFF. Owed: a cheaper closure — only the leaf-hidden faces a light ray can actually reach.

**F2.15d 2026-11-14 — §4.6 item 8 continued: a cheaper caster closure (measured; both variants fail, off).**
- Goal: close F2.15b's light leaks (shadow casters by light direction) for less than the closed casters' ×2.31 faces.
  Predictions written before code: `voxfound_f215d_prediction.txt`.
- Entry seam (`casterSeamGrid`, `{"caster_seam_depth":D}` at build): add only the leaf-hidden solid faces whose leaf
  is within D leaf voxels of air. Red first: `CasterSeamAddsExactlyTheLeafHiddenFaces` (main ∪ seam(∞) = leaves-as-air
  mesh, no overlap; D = 1 exact). Measured: 58k (D=1) / 119k (D=2) faces, canopy_close leaks 218 → 188 / 160 px
  (closed: 12). WRONG: sprigs sit only on a hashed fraction of the air-touching leaves, so a canopy's inside has no
  cards and light crosses it freely; leaf depth does not bound a ray. The leak: a ray enters a trunk through a
  leaf-hidden face inside the canopy and used to be stopped by the trunk's low away-side faces.
- Exit faces (`casterExitGrid`, `{"caster_exit_reach":R}`): keep the main faces of solids within R solid steps of a
  solid with a card neighbour, drawn as AWAY groups (dir code 6-11, `kAwayGroup`: cast only when direction d does NOT
  face the light; cull shader + `faceDirFacesLight`). Red first: `ExitFacesCloseTheLightTestNextToLeaves` (3,600 rays:
  all main faces = light-facing main + away exit faces, 0 differ; the light test alone 402 leaks; reach 2 = brute-force
  BFS); GPU `ClusterCullDropsDirectionGroupsLikeTheReference` with away groups, 0 wrong. CPU 56/56, GPU 18/18.
  - Exact only for unbounded R, which keeps EVERY main face (480,187: trunks connect to the ground). Chunk-exact only
    for R ≤ 7 (the 8-voxel area margin, StorageLab `areaMarginVoxels`).
  - Leaks (px > 8 brighter vs `shadow_mdi`), canopy_close: R=3 164, R=7 127, R=24 2, unbounded 0 at all poses;
    forest_floor_open R=24 77 (`voxfound_f215e_gate_*`, `voxfound_f215e_canopy_close_exit7_leaks.png`).
  - Whole frame, R=24 + light test vs the default casters (8 pairs × 5 poses, `voxfound_f215e_ab_reach24.*`):
    −0.27* / −0.10 / −0.51 / −0.19* / −0.20* ms (overview, top_down, forest_floor_open, east_low, canopy_close;
    * = CI excludes 0). Predicted −0.5..−1.2: wrong — the 413k cast faces (vs 301k with the light test alone) eat half
    the saving.
- Default: OFF. `caster_exit_reach` 0, `caster_seam_depth` 0 (not built); `shadow_dir_cull` off. R = 24 would need a
  ~32-voxel generation margin (~+38 % generation per chunk) for at most 0.5 ms and still leaks. Shadow light-direction
  culling is closed for now: the main mesh is not a closed surface under leaves, and no cheap local rule closes it.

**F2.15f 2026-11-15 — §4.6 item 8's last part: a lower command count (compacted, merged commands; default ON).**
- Built: the main-view cull pass (`voxfound_cull.comp`) can APPEND only the visible clusters instead of zeroing the
  culled ones' fixed commands, merging consecutive visible clusters of the same chunk, level and multidraw whose faces
  are contiguous (`mergeVisibleRuns`, CPU reference), in cluster order: two dispatches over n/64 workgroups (run heads
  per workgroup, then offsets + write). Drawn with `vkCmdDrawIndexedIndirectCount` (the Vulkan 1.2 `drawIndirectCount`
  feature is now queried and enabled at device creation; without it the old path runs). The compacted commands, their
  chunk origins (read through `gl_DrawIDARB`) and the counts live in per-frame-slot buffers; `RenderPipeline` keeps a
  cache of set-1 descriptor sets keyed by (table, origins, offset) so they are never rewritten per frame. Within a cell
  the direction groups are now ordered −x, −y, −z, +x, +y, +z (`faceDirRank`): 4 of the 8 camera octants become one
  contiguous window (`{"dir_order":"axis"}` at build keeps the old order). `{"mdi_compact":bool}` at runtime, default ON.
- Red first: GPU `CompactedCommandsDrawTheSameFacesAsPerClusterCommands` (5,000 random clusters, 6 chunks, 3 levels,
  solid/leaf split, broken face ranges: the compacted commands = the CPU reference in order, the (face, origin) pairs =
  the per-cluster path's, and the per-cluster commands survive a compacted pass), red on a stub, then red again for
  each correction below; CPU `OctantOrderAndMergeRule`. CPU 57/57, GPU 19/19.
- Measured (`voxfound_f215f_*`; prediction `voxfound_f215f_prediction.txt`), corrected three times by measurement:
  - The first version appended into the per-cluster command buffer, so the uncompacted path drew garbage afterwards
    (`voxfound_f215f_shared_buffer_bug_D_vs_U.png`): own buffer now, pinned by the GPU test.
  - Atomic append (any order): +1.04* / +0.73* ms at east_low / canopy_close, in the faces pass itself (scrambled draw
    order) → ordered compaction. In ONE workgroup the cull cost 0.36 ms → two dispatches, +0.015 ms.
  - Merge block 32: +3.26* ms at forest_floor_open (reproduced: +3.42), all of it in the leaf-card pass right after
    the faces, while −0.83* at canopy_close; cause not found. Block 1 / 8 / 16 at those two poses: −0.10* / +0.02,
    −0.22* / −0.08*, −0.25* / 0.00 → block 8 (`ClusterCullGpu::kMergeBlock`).
  - Final, whole frame compacted − per-cluster (8 pairs × 5 poses): overview −0.23*, top_down −0.15*,
    forest_floor_open −0.15*, east_low 0.00, canopy_close −0.07* ms; none worse. Commands that draw 2,471 / 2,521 /
    1,787 / 3,227 / 1,794 (per-cluster: 6,182 / 5,143 / 3,143 / 5,570 / 2,074 of 10,889 walked). Predicted ≤ 2,385
    at every pose (2 of 5) and −0.4..−0.8 ms at forest_floor_open: both wrong. Pixels vs per-cluster 37–79 px > 8
    levels, the size of each mode's own repeat (44–108 px); faces drawn identical.
- Default ON. Item 8 closes here: main-view direction culling with a lower command count; shadow light-direction
  culling stays off (F2.15d). Open, recorded: why merged draws of 32 clusters cost the following leaf-card pass 5 ms at
  one pose.

**F2.16 2026-11-15 — §4.6 item 9: incremental rebuild (exact; CPU rows by default; device rows opt-in).**
- Built: `planRunsDelta` / `meshRunsPlan` (Mesher): after a box edit only the slices that can change are touched
  (dir + [lo−1, hi], dir − [lo, hi+1] per axis); faces whose rectangle grown by one meets the edit footprint are
  removed, and their rows plus the footprint rows (whole width) are re-meshed, keeping only heads whose grown rectangle
  meets the footprint — exact by construction. Sprigs: cells in [lo−1, hi+1] recomputed (`updateSprigs`).
  Connectivity: the edited blocks' blocking bits, then the 32³-block flood (`updateBlockBlocking` +
  `connectivityFromBlocking`). Clusters: the delta is applied inside the clusterFaces + direction-group order
  (`applyClusteredDelta`). `RegionEdit` keeps each chunk's 1/8 m grid (dim + material), border planes, faces, groups,
  sprigs and blocking bits; an edit on a chunk border updates the neighbour's border plane and re-meshes its plane
  slice. API: region build `{"editable":true}` (refused with coarse levels / closed casters / seam: owed), then
  `{"edit":{"lo","hi"|"sphere","material","verify","device","replace_all"}}`; `"verify"` rebuilds the touched chunks
  fully with FaceBuilderGpu and counts mismatches. Device rows: `voxfound_faces.comp` row-list mode (job 5) over
  resident chunk grids (`FaceBuilderGpu::rowsDelta`, 8 LRU slots, dirty-box uploads), on a compute-only queue
  (`VulkanDevice::getAsyncComputeQueue`, family 2 here).
- Red first: CPU `IncrementalRebuildEqualsAFullRebuild` (4 awkward grids, cards, 300 box / sphere / border-plane-only
  edits: faces = meshRuns as sets + coverage, groups and cells, sprigs, blocking, connectivity, after EVERY edit);
  GPU `RowsDeltaEqualsTheCpuPlan` (12 grids, 240 edits, resident hits and misses, final faces = meshRuns). Both red
  on stubs. CPU 58/58, GPU 20/20.
- Measured (`voxfound_f216_*`; prediction + every correction in `voxfound_f216_prediction.txt`):
  - Exact live: 0 face / sprig / connectivity mismatches in 214 verified voxel-changing edits (153 CPU, 31 device,
    30 mixed) up to 4 m, up to 6 chunks per edit. Pixels after 20 visible edits vs `replace_all` (a full rebuild of
    the same voxels): the 3D view identical (97 px > 8 levels, all in the status bar's FPS digits; control 53 px).
  - Cost per edit, CPU executor (median, p90): 1 voxel 1.05 (1.36) ms, 1 m 4.5 (6.6), 2 m 13.2 (21.9), 4 m 48 (69).
    Predicted 0.3 / 0.3-1.5 / — / 3-12: WRONG. Rows are whole-width slices read through `faceAt` (~8 ns per cell);
    the 1-voxel cost is mostly the connectivity flood (0.77 ms). A 1 m edit sits inside today's 2-13 ms per chunk
    instead of ≥ 3x below it. Rows re-meshed: 0.11 % of a chunk for 1 m (predicted ≤ 1 %: held).
  - Device rows (the stop line's answer): kernel 0.1-0.3 ms, but the synchronous readback waits for a GPU timeslice
    — 0.5-0.7 ms with an idle GPU, 10-20 ms at the uncapped operating point, even on the dedicated queue (the queue
    alone was predicted to fix it: refuted). CPU stays the default; `{"device":true}` is item 10's starting point.
  - After an edit, the drawn lists' reassembly (2.5 ms) + whole re-upload (~9 ms, cluster/cull tables dominate) on
    the next frame: predicted 5-15, held — item 10 removes it.
  - Steady-state frames run the same code (nothing per frame was added), so no whole-frame A/B.
- Bugs found and fixed on the way:
  - Pre-existing since F2.15f: `RenderPipeline`'s set-1 descriptor cache was keyed on `VkBuffer` handle values; a
    revision change destroys and re-creates the per-command origin tables, and a recycled handle hit a set still
    pointing at freed memory → `VK_ERROR_DEVICE_LOST` (5 of 6 fresh engines; the Crash Diagnostic Layer showed the
    invalid read in freed memory of the table's size). `forgetVoxFoundSets()` wherever those buffers are freed: 0/3.
  - A CPU edit left the device's resident grid stale; a later device hit re-meshed old voxels next to its box (12 face
    mismatches live) → `forgetResident`; the gate's "mixed" triples pin it.
- Item 9 is built and exact; its cost goal is NOT met (a 1 m edit 4.5 ms, inside today's range). Owed, in plan order:
  item 10 (face pool: rows recorded into the frame on the device, faces written in place — no readback, no wait, no
  whole re-upload), then a narrower CPU row (the footprint's blocks, not the whole width) if the CPU path stays.
  Also owed: edits with coarse LOD levels / casters / seam (each needs its own update), the connectivity flood per
  edit (0.4-1.5 ms), the 545 MB dense edit store (BlockStore is F3).

**F2.17 2026-11-15 — §4.6 item 10: the face pool (region-arena spans per chunk, device-local; default ON).**
- Built: `FacePool` (core/voxfound) places each chunk's faces, cell clusters and direction clusters in spans of
  `Graphics::ChunkArenaAllocator` (the region arena: 768 B alignment, retire queue), one block per array so each array
  stays ONE device buffer. The pool image IS `Preview::Level` (gaps after spans, count-0 cluster slots), so every
  draw path keeps working. `update()` rewrites only the changed chunks: in place when they fit their span, else a new
  span with the old one retired and its cluster slots zeroed in the same update; it returns the dirty index ranges.
  `RegionEdit::assemble(pv, leafTex, &rebuilt)` uses it and `Preview` carries the dirty ranges of the last revision
  step. Renderer (`{"face_pool":bool}`): the faces live in a DEVICE-LOCAL buffer filled by copies recorded at the
  start of the frame (`recordVoxFoundPoolUpdate`, before the shadow pass) behind barriers — queue order, no device
  wait; a one-step pooled revision copies only the dirty face ranges and rewrites the main cull table in place
  (`ClusterCullGpu::beginUpdates` + `recordUpdate`: cluster words, firsts, origin words, every slot's command words).
  `voxfound_cull.comp`: a count-0 cluster is never visible (the empty slots).
- Red first: CPU `FacePoolUpdatesOnlyWhatChanged` (400 random count changes over 6 chunks: drawn ranges and slots =
  the lists, no overlap, unchanged chunks never move, and the previous image + the dirty ranges = the new image);
  GPU `PartialUpdateEqualsAFullUpload` (50 rounds of in-place range rewrites cull exactly like a fresh upload:
  per-cluster commands, compacted commands, origins, counts); GPU `SeveralUpdatesInOneCommandBuffer` (below). All
  red on stubs. CPU 59/59, GPU 22/22.
- Bug found by the first live gate (device lost after the first pooled edit; the Crash Diagnostic Layer showed the
  frame's first copy never completing): `recordUpdate` replaced its staging when a later call in the SAME frame needed
  more room, destroying the source of copies already recorded. Fixed with a per-slot cursor (calls append; a full
  buffer is retired until the slot's next `beginUpdates`), pinned by the third test.
- Measured (`voxfound_f217_*`; prediction `voxfound_f217_prediction.txt`):
  - Exact: in the 3D viewport after 20 visible edits uploaded partially, pool vs the same image uploaded whole
    (host-visible) vs a fresh layout of a full rebuild: 0 / 0 / 0 px at forest_floor_open and overview_south, drawn
    counts identical; after 200 more edits still 0 px vs a fresh layout.
  - Next-frame upload after an edit: 0.16-0.18 ms median (p90 ≤ 0.53) for 1 voxel .. 4 m, vs 8.7-14.5 ms today
    (predicted ≤ 0.5 / ≤ 1: held). The CPU reassembly (2.5 ms) is unchanged (owed).
  - Steady state, whole frame, device-local pool − today's host-visible buffer (8 pairs × 5 poses): −2.34 / −2.67 /
    −2.98 / −3.77 / −3.06 ms (overview_south, top_down, forest_floor_open, east_low, canopy_close), every CI excludes 0.
    Predicted −0.1..−0.8: wrong, 3-10x more. It is the shadow passes (−2.2..−3.6 ms): the casters' 36-index instanced
    draw re-reads each face record per vertex, and from host memory that went over PCIe. Main view −0.25..−0.75 ms
    at 3 poses.
  - Memory: the pool image is 1.63x the live faces after layout, 1.57x after 300 edits (predicted ≤ 1.3 / ≤ 1.5:
    wrong; growth, slack and alignment compound). 6.3 MB vs 3.8 MB here.
- Default ON. Owed, recorded: the CPU reassembly per edit (2.5 ms: the level image is still copied whole into the
  Preview); a tighter slack policy; the shadow MDI's cull table still re-uploads whole (it is off); the sprigs are
  still re-copied whole (0.15 ms). Re-measure owed: F2.12's shadow MDI and F2.15b/d's light-direction culling were
  judged with host-visible faces, where every extra caster read cost PCIe bandwidth.

**F2.18 2026-11-15 — T-junction cracks at merge borders (the F2 list after item 10; default ON).**
- Measured first: in debug view 11 (flat grey per fragment) on the gate region with flora off, the run merge's quads
  leak single sky pixels -- 185 / 190 / 69 / 1 / 87 at the 5 poses (repeat: identical), scattered, not on chunk lines,
  visible even straight down.
- Built: `{"tjunction":0|1|2, "tj_px":f}` (voxfound preview; the pushed word sits in the 4 free bytes of the graphics
  push-constant range). 1 = exact sums in `voxfound_face_mdi.vert`: (origin − camInt) + local first (exact), camFrac
  last, so a world point shared by two chunks is bit-identical. 2 = exact sums + each quad grown in its own plane: per
  corner, outward along u and v by f px / (that axis's screen speed perpendicular to the edge it moves), clamped to half
  a voxel, none behind the camera — every edge moves out by exactly f px on screen and covers a neighbour's T-vertex
  sliver. UVs are a function of the grown position, so overlapping coplanar neighbours shade the same. f = 1/16 px.
- Red first: knob plumbed with the shader stubbed — levels 1 and 2 left every count unchanged.
- Measured (`voxfound_f218_*`; prediction `voxfound_f218_prediction.txt`): level 1 changes nothing (0 px — the cause is
  the T-junctions, not chunk rounding); level 2: 532 → 1 crack px over the 5 poses (the 1 is a skyline notch, not a
  crack). Shaded flora scene: 0.20 / 0.35 % of the viewport changes (predicted ≤ 0.1 %: wrong in number) — 81-92 % are
  the 1-3 px cracks themselves, the rest the region edge moving < 1 px; controls 0 px. Whole frame 8 pairs × 5 poses:
  −0.015 / +0.011 / −0.003 / −0.080 / −0.060 ms, every CI includes 0 (neutral, as predicted). Shadow map: ≤ 2 leak px
  on bare terrain over 5 poses — not changed. CPU 59/59, GPU 22/22.
- Default ON. Not covered: the direct per-chunk path (`voxfound_face.vert`, only with mdi off) and the shadow casters.

**F2.19 2026-11-15 — the F2 exit gate re-run (F2.8's comparison with items 1-10 + T-junctions on).**
- Method as F2.8 (VoxFoundGate scene; A = today's store, B = the same chunks at 1/8 m, every knob at its default; one
  session, Release; perf_harness 8 pairs × 240 GPU frames × 5 poses), plus the first pixel diff (§4.3: debug tonemap,
  wind 0, GI off, controls) and a voxel-surface mask from debug view 11. Prediction `voxfound_f219_prediction.txt`.
- **Found by the pixel diff, fixed:** the 1/8 m ground had dirt stripes along every contour line (also in F2.8's
  captures, unnoticed). `buildTerrainArea` counted material layers from floor(h + 0.5), so where the smooth ground had
  just crossed a whole metre the top voxels counted depth 1 (Dirt). Now the layers count from the metre that holds the
  column's top voxel: the top voxel is always the surface material, as today's top cube is, and the layer boundaries
  stay on metre planes. Red: `TopVoxelOfEveryColumnIsTheSurfaceMaterial` (2,738-10,104 of 65,536 columns wrong per
  area) → 0. A first fix (depth counted continuously) also gave 0 but pushed bare-terrain detailed blocks per column
  to 2-4 (boundaries cutting blocks) and was replaced. Detailed per column now 1.06 / 0.96 / 0.75 / 1.46 / 2.30.
- **Whole frame (the valid run; a first run contended by WoW was voided):** B − A = −2.10 / −2.07 / −8.61 / −7.05 /
  −9.37 ms (overview_south, top_down, forest_floor_open, east_low, canopy_close), every CI excludes 0. A 7.2 / 7.3 / 49.0
  / 44.5 / 29.1 ms, B 5.1 / 5.2 / 40.1 / 37.4 / 19.7. Predicted B at the ground poses 4-8 ms: wrong — F2.18's numbers
  were taken with GI off. With GI on the leaves' per-fragment probe ambient dominates in both stores (forest_floor_open:
  sprigs 24.4 → 0.97 ms with GI off, today's cards 32.7 → 1.65; wind changes nothing). That is the next frame-time
  lever at the ground, and it belongs to lighting, not to the store.
- GPU memory: today 4.63 MB (quads 16 B + cards 8 B); new 5.63 MB fresh (1.22×), ~8 MB once the pool has edit slack
  (1.7×). Bytes per frame (new): 11.3-13.4 MB at the 5 poses (F2.8: 15.1); both shadow cascades still take all 478k
  faces (the region lies inside them). Today's per-pass counts are not exposed, so no per-frame comparison.
- Pixels: controls 0 px on the new store (today's store 14-15,659 px frame to frame even at wind 0). Today vs new:
  46-68 % px differ; where both draw a voxel surface 23-49 %. All of it traced in the diff map to the leaves (cards vs
  sprigs), the shadow rings they cast, trunk templates (1/9 m vs 1/8 m), and the 1/8 m ground steps — content gaps
  (FC) and the intended resolution, no store defect left. The plan sets no numeric pixel threshold.
- **Verdict: the F2 performance condition holds at all 5 poses** (whole frame faster, memory and bytes in the same
  tier). The pixel condition is measured and every residual is attributed; closing it numerically needs the FC
  content (grass, fern / shrub_flat / tree_apple on 1/8 m, one leaf set) — owed to FC, not F2.
- CPU 60/60, GPU 22/22. Evidence `voxfound_f219_*`, `f219_*.png`.

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
| **F2** | GPU face building (hidden-face culling, merging in and across blocks) + drawing through today's lighting; the §4.6 render optimizations rebuilt on blocks (culling, occlusion, indirect draws, shadow casters, block LOD pyramid, direction culling); T-junction handling | the same engine-generated scenes that run today run on the new store in the same performance tier (whole frame, GPU memory, bytes per frame), measured at 5 poses; pixel diff within the gate. **F2.19 re-run 2026-11-15: performance holds at all 5 poses (new store 2.1-9.4 ms faster whole frame; memory 1.2-1.7×; 11-13 MB per frame); pixels measured, every residual traced to FC content gaps; contour-stripe defect found and fixed** | a scene that works today doesn't, and we can't find and fix why |
| | **F2.0 CPU reference mesher, 2026-11-11** (`voxfound/Mesher`, `VoxFoundMesherTest`): culled faces, greedy merged across the whole area, greedy limited to 8-voxel blocks; `coverageErrors` proves a merge covers every exposed face exactly once. Light is not in the merge key yet, so every count is a LOWER bound. **Predictions written before the first run** (bare Perlin 32 x 32 m footprint): culled 70k-200k; greedy 3k-15k; today's 1 m cubes (proxy) 0.5k-3k; greedy / proxy 3-10x; per-block / unbounded 1.5-3x; the building shell at 1/8 m within ±30 % of 1/9 m. Measured: see §9a | | |
| **F3** | Edits with brushes; codec v3 + converter (§9b) | edit cost per block ≪ today's 2-13 ms chunk rebuild; save/load round-trips exactly. **F3.1 2026-11-16: a 1 m brush 0.53 ms median from request to lists ready (p90 0.75; F2.17's path 7.1 ms), 0.096 ms per block at 4 m, exact; F3.2a: the edit store is the BlockStore, 545.5 → 62.9 MB, exact; F3.2b: brushes 1/2/4/8 + picking over the API, a 1 m brush 0.50 ms, pick 0.006 ms; F3.3: codec v3 + world.db, save / load round-trips exactly (voxels, lists, pixels, bytes), only edited chunks saved; F3.4: PXB2 converter (cubes exact, thirds / ninths by volume, no material dropped from a cell), a saved city drawn through the new store; F3.5 first run: exact under 1,000 edits + 10 reloads, edit cost met inside a chunk but NOT on chunk borders (0.69-0.90 ms); F3.5a: a border edit costs what an interior one does (exact; corner 1.00 -> 0.48 ms uncontended), but a full-block edit was 0.61-0.72 ms of which the connectivity flood was 0.35; F3.5b: incremental connectivity (exact, floods 112/128 -> 0/128 edits), a one-block brush 0.28-0.36 ms median anywhere; F3.5 second run (size and kind independent): exact under 1,000 verified edits + 10 reloads, 0 px, byte-identical re-save, every (size, kind) median ≤ 0.42 ms (a one-block brush 0.26-0.42) — CLOSED 2026-11-16; owed into later rows: the thin-part occupancy rule (converter), the small-leaf look (FC), edit store growth (1.34× over 1,000 edits)** | — |
| **F4** | Fracture core, CPU reference: bonds, patterns, pieces, anchoring, crumbs. Then the GPU port, equal to the reference. **F4.1 2026-11-17 (§9c): the CPU reference — all 8 §8 rig predictions pass (the edge hit only trivially), equal to the independent checker, deterministic, conserving per material, nested in energy, under 300 random hits; pictures for review. F4.2 (§9c): anchoring through the block graph is exact under 400 random hits; on an engine-generated building the worst search is 8,558 nodes (8.3 % of its voxels), the median 1,055; found that the generator's stair treads and guard are unbonded on both grids (logged). F4.3 (§9c): the GPU port equals the integer reference in every channel — 18/18 rig cases, 600/600 random hits, first run; 1.5-1.8 ms device per break on a 131k-voxel wall at 0-1,981 pieces (21× the CPU at ~1,000; flat, because it covers the whole grid). Exit test met. The owner reviewed the crack pictures 2026-11-17 ("cracks look good"): CLOSED** | the fracture-lab rigs pass their predictions; conservation and determinism exact | the patterns don't read as real cracks to the owner (video review) |
| **F5** | Bodies: extraction, GPU simulation with voxel collision, drawing with one transform, the rest/bake-in hybrid. **F5.1 2026-11-17 (§9d): extraction, CPU reference — loose pieces leave the world as bodies (grid, mass, centre, inertia from voxels; the hit's momentum shared) and crumbs; exact conservation voxel for voxel on every rig and 300 random hits and over 20 hits in a row; mass properties equal the closed form and a brute-force sum to 1e-9; momentum conserved; 5.1 ms for 947 bodies + 1,407 crumbs on the large wall. F5.2a (§9d): the CPU reference body solver — the engine's AVBD extended to voxel bodies (full inertia, symmetric voxel-on-voxel contacts, any size); 11/11 rig tests after 7 recorded run-1 fixes; momentum at 8 iterations MISSED its prediction (4.7 % of m v in an impact tick, 0.7 % at 32); a blast's 21 pieces fly, pile and sleep with ≤ 8.5 mm overlap. Owed: hard-contact fires 273-563 per run. F5.2b (§9d): the GPU port (headless) passes every F5.2a rig's assertions on its first run, tracks the reference within 0.33 mm on R1/R2/R4, and two runs are bit-equal; cost MISSED: 8-19 ms per tick, a dispatch floor (~310 per tick), on a GPU shared with WoW and another engine. F5.3 (§9d): bodies drawn with one transform -- faces built once (8 B record + body index), one draw, poses camera-relative per frame, cast into mid + near; at rest the draw equals the preview path (15 of 185,000 px above a ±1 control floor -- the 0-px control MISSED); face build 18.5 ms and 1.19 ms main draw at 947 bodies MISSED (≤ 15 / ≤ 0.3), cost follows faces, no per-body cull yet. F5.4 (§9d): the rest / bake-in hybrid -- asleep bodies over the budget, outside the keep radius, bake farthest first as a voxel-to-cell bijection (rigid near axis rotations, else resample); conservation EXACT after every tick (blast; 916-body wall) and live; pop near 40.8 % of the box MISSED (2-30), far 0.038 of near; rigid share 33 % MISSED (≥ 80: pieces rest leaning); bake 38 / 677 ms at 916 MISSED (≤ 10 / 40); a whole-area rebuild of 72 ms per bake pass. F5.5a (§9d): the GPU solve fused into 17 dispatches per tick, bit-equal; the profile shows the cost is the body-body narrowphase (100 of 109 ms per tick at 1,025 bodies), not the dispatches (F5.2b's diagnosis was wrong); cost MISSED at every rung; the stress found a piece tunnelling through the ground. F5.5a2: the body-body narrowphase one workgroup per pair, bit-equal at every sample, 108.7 → 10.4 ms per tick at 1,025 bodies (still over the 2 ms proposal, levers left); the tunnelling fix: a fast corner's contact from its swept path, CPU and GPU -- pieces at 15-60 m/s rest on the ground and on a 1-voxel slab (all fell through before), 0 through the ground on the ladder. F5.5a3: the world test one workgroup per awake body, bit-equal, 11.0 → 7.1 ms per tick at 1,025 bodies (≤ 7 MISSED narrowly). F5.5a4: the primal per body on a 32-thread group (deterministic, not bit-equal to the serial sum), 7.1 → 3.7 ms at 1,025; still over the 2 ms proposal with a broadphase lever left. F5.5b: the GPU solver live in the lab (async, bit-equal to the waited step; 0.003 ms CPU per frame; real time in a light world, not in the forest) + the per-body cull (0 px; faces −89-93 % but the body pass follows pixels, not faces: 0-23 % saved); MISSED a pair asleep 102 mm deep, the bake frame 64-105 ms. F5.5c: the bake levers -- plans exact at half / a fifth of the cost, a per-pass cap, the lab world as a RegionEdit edited per baked body (faces = a full rebuild), the GPU edit staged and bit-equal to a reload, one body-face copy per frame in flight: the whole rest call 1.9 ms max live (F5.5b's path 77-113 ms, same engine, a GPU shared with WoW); MISSED plan cost, region-vs-area edge pixels (131 / 24 / 20 px), the backlog in one run, frame p99. F5.5d, the exit live at 110 / 282 / 994 bodies: conservation exact at every rest call (0 of 4,892 checks), 0 through the floor, 0 refused; CPU per frame 0.016 ms and the hitch 1.73 ms within budget; OVER with levers left -- every body awake 13.5 ms per tick (not following the count: 7.7 at 110; to be profiled), draw at the peak 1.45-1.75 ms, real time 45-57 ticks per second; pop near 39 %, far 0.003 x near at 4x the pile's size (MISSED at the predicted 24 m); found and fixed: the "60 s frame" was the lab's penetration measure on every request. Not the stop condition: F5 open. F5.5e, the awake pile's tick profiled (a measure, no lever): the pile is the cost -- headless 8.4 / 10.3 / 15.0 ms = live (no WoW: 7.9 / 9.8 / 14.1); a settled pile makes 4x a falling one's candidates (80 % capped); not one tick per submit, not the frame's share, not the path choice; the world test leads at every rung, the broadphase 17-28 % (not the next lever); the biggest tenth of the bodies alone costs 63-71 % of the tick -- each pass waits for its busiest bodies. F5.5f, the world group split (an instrument, bit-equal): the item tests are 96-98 % of it, run twice per tick (count, then write), the cap <= 5 %; the busiest body ~3,000 items on one workgroup. F5.5g, each world item tested once (staged, then copied; bit-equal): the world group -33-41 % headless / -36-38 % live, the tick only -8 % (contended; MISSED 10-20); the busiest body still on one workgroup. F5.5h, a big body's items over up to 16 workgroups (bit-equal, 0 fallback). F5.5i: F5.5h's shader had slowed the whole pipeline 25-40x (a Cand t[4]; fixed); clean, the world group is under 1/10 of the awake tick and the live awake tick -24-28 % vs F5.5e. F5.5j, HARD_APPLY's items over up to 16 workgroups (bit-equal): hard_apply -53-67 %, the tick -11-18 %; found an uncached pipeline compile of ~1 min. F5.5k, the pairs split: the corners tested twice are 77-89 % of the group, BROAD 5-20 %; F5.5l, each pair corner once (bit-equal): the pairs group -37-43 %, the tick -6-9 %. F5.5m, the primal split: the sums
and the shared tree are 60-80 % of it, the dispatch floor <= 10 %. F5.5n, a lean primal: slower, rejected. **F5 CLOSED
2026-11-21 by the owner's decision at ~8.1-8.8 ms per tick for ~1,000 awake (2 ms proposal MISSED, recorded); next
F6** | 100 → 1,000 bodies; conservation; pop measured | body cost at 1,000 above budget with no lever left |
| **F6** | MatSim on the world grid (liquids, powders, crumbs). **Started 2026-11-21 (§9e), broad strokes: F6.1 the 1/8 m
lattice, F6.2 the fill channel in the world, F6.3 crumbs as powder, F6.4 particles write back, F6.5 live. DONE in broad strokes 2026-11-21: MatSim's invariants on
1/8 m world voxels (CPU + GPU parity), a growing box equal to one box over the world, rubble conserved per material,
the hybrid settling back, live in the bodies lab; MISSED the live cost (CPU reference: 4-6 ms small, 25-250 ms on the
large wall) and water pooling/drawing on an open floor** | MatSim's tests on world voxels | — |
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

## 9b. F3: edits with brushes, codec v3 + converter

The F3 row's exit test has two halves: **edit cost per block ≪ today's 2-13 ms chunk rebuild** (§4.6 item 9's goal,
not met in F2.16) and **save/load round-trips exactly**. "≪" is read as F2.16 read it: ≥ 3x below the low end of
today's range, so a 1 m brush (one block) costs ≤ 0.67 ms on the main thread, from the request to the drawn lists
ready, plus the next frame's upload. Measured live on the F2 gate region, exact (verify) at every step.

Items, in order (each: prediction, red test, build, measured on/off):
- **F3.1 Edit cost.** What F2.16/F2.17 left owed: rows re-meshed over the whole slice width (3.8 ms of a 1 m edit's
  4.5), the 32³-block connectivity flood on every edit (0.4-0.8 ms), and the reassembly that copies the whole level
  image and every chunk's lists into the Preview (2.5 ms).
- **F3.2 Brushes and the edit store.** (a) the edit store becomes the `BlockStore` (F1) instead of a dense 16.7 MB
  grid per chunk (545 MB for the gate region), with the F3.1 edit cost kept; (b) place / break with brushes of 1, 2,
  4, 8 voxels per side on the voxel grid (§1), in voxel coordinates over the API, and picking that returns a voxel
  position and face (§7 item 4's interface; wiring the editor's keys to it is F7).
- **F3.3 Codec v3** (§3.6): per chunk the directory, then the detailed blocks run-length encoded; save / load
  round-trips voxel for voxel; only chunks that differ from the generator are saved (§3.5a point 7).
- **F3.4 Converter** from PXB2: a cube → one 8³ block (exact); subcubes / microcubes resampled (§7a).
- **F3.5 The F3 exit gate**: both halves measured on the gate region; stress per CLAUDE.md (1,000 edit cycles,
  every brush size, chunk borders and corners, save / load after each batch).

**F3.1 2026-11-16 — edit cost (done: a 1 m brush 0.53 ms to lists ready; F2.17's path 7.1 ms on the same edits).**
- Built (each exact by construction, pinned by F2.16's 300-edit `IncrementalRebuildEqualsAFullRebuild` + three new
  tests, red first):
  - Rows: `meshRunsPlan` evaluates the face mask lazily. Per head row it walks only the runs that touch the footprint
    grown by one, and the head / stack tests read only the cells [u-1, u+w] of their rows.
    `RowRebuildReadsOnlyNearTheFootprint`: 60 x 1 m brushes on engine terrain, delta = F2.16's whole-row executor
    (kept as `meshRunsPlanWholeRows`), cells read 4.43 % of whole rows.
  - Connectivity: `updateBlockBlocking` reports whether a blocking bit changed; no change = no flood
    (`ConnectivityFloodRunsOnlyWhenABlockingBitChanges`: change report exact, the flood equal to F2.10's rule on random
    bits).
  - Assemble: each chunk's pool input is kept between edits (only rebuilt chunks recomputed); the Preview's drawn
    arrays are written in place over the pool's dirty ranges; sprigs live in per-chunk spans with slack (x1.5 after an
    overflow, as the face pool), so a count change rewrites one span. `RegionEditAssemblesOnlyWhatChanged`: two 256³
    chunks, 40 edits across the border, drawn arrays = the level image, bytes written <= dirty ranges + the rebuilt
    chunks' sprig spans.
  - A/B switch `{"edit":{..., "legacy":true}}` = F2.17's path.
- Measured live (3 runs, gate region, F2.16's 200-edit generator, every edit verified, F3.1 / legacy in ABBA order;
  `voxfound_f31_*`; prediction `voxfound_f31_prediction.txt`):
  - Exact: 0 face / sprig / connectivity mismatches in 153 verified edits per run; after 20 visible edits 0 px vs a
    full rebuild at forest_floor_open and overview_south, controls 0. After 200 more edits 1 px (a material edge, same
    draw counts; draw order of F2.18's overlapping grown quads, not attributed by experiment).
  - Request to lists ready, F3.1 vs legacy (median / p90, ms): 1 voxel 0.29 / 0.75 vs 3.1; **1 m 0.53 / 0.75 vs
    7.1**; 2 m 1.0 / 1.3 vs 16.9; 4 m 6.2 / 7.8 vs 59.7 (0.096 ms per block). 1 m median in 3 runs 0.53 / 0.62 /
    0.53. Rows for 1 m 0.12 ms (was 3.8-4.1); assemble 0.17 / 0.31 (was 2.4-2.6); next-frame upload unchanged.
  - Wrong predictions: a bit-row flood (0.184 ms) cost the same as the stack flood (0.185) in one session, so it was
    dropped; the flood stays ~0.16 ms when a block's sight bit flips (predicted <= 0.1). 1 m p90 0.75 (predicted
    <= 0.67).
- Verdict, the F3 row's first half: met at the median. A 1 m brush (one block) is 3.8x below the low end of today's
  2-13 ms per chunk; a 4 m brush is ~20x below per block. The 1 m p90 (0.75 ms) is 2.7x below 2 ms but above the 0.67
  line. Owed: incremental connectivity (the 0.16 ms flood); the per-chunk face span copy (`FacePool::write` copies a
  rebuilt chunk's whole span).

**F3.2a 2026-11-16 — the edit store is the BlockStore (545.5 → 62.9 MB on the gate region, exact).**
- Built: `RegionEdit::Chunk` holds a `BlockStore` (F1) built once from the region build's grid
  (`BlockStore::fromGrid`, whole blocks at a time) and collapsed after every edit that touched it; the dense grid is
  gone. The incremental rebuild reads the store through the same code as before, templated on a reader
  (`meshRunsPlan`, the whole-row reference, `updateSprigs`, `blockBlocking` / `updateBlockBlocking`; a uniform block
  answers its blocking bit from the directory entry). Paths that need a dense grid expand it on demand
  (`RegionEdit::dense`, `BlockStore::toGrid`): verify, `replace_all`, the opt-in device rows.
- Red first: `BlockStoreReadersEqualTheDenseOnes` (80 edits on engine terrain with leaf blobs and real border planes:
  every reader equal to the dense one after every edit; the store round-trips). CPU 64/64, GPU 22/22.
- Measured live (F3.1's gate and edit generator, the same 200 edits; `voxfound_f32a_*`, prediction
  `voxfound_f32a_prediction.txt`): 0 mismatches in 153 verified edits; 0 px vs a full rebuild at two poses. The edit
  store is 62.9 MB after the build vs 545.5 MB (8.7x), 73.5 MB after 400 edits. Region build +1.0 s.
- Edit cost, request to lists ready (median): 1 voxel 0.36, **1 m 0.66** (F3.1's gate ≤ 0.67 still held, by 1.5 %),
  4 m 7.2 ms. Paired per edit against F3.1: the store adds +0.05 ms at 1 m and +1.6 ms at 4 m. The cause is the
  brush writing voxel by voxel through `set()` and collapsing afterwards (4 m: set +0.74, collapse +0.24).
- Owed to F3.2b (brushes): whole-block writes, so a block a brush covers becomes one directory entry.

**F3.2b 2026-11-16 — brushes and picking (a 1 m brush 0.50 ms to lists ready, exact).**
- Built:
  - Brushes (§1): a cube of 1, 2, 4 or 8 voxels whose corner is a multiple of its size (`brushCell`), place or break
    over the API: `{"edit":{"brush":{"at":[x,y,z],"size":s},"material":...}}` (world voxels; other sizes refused).
  - Whole-block writes: `BlockStore::fillBlock` writes a block a box edit covers entirely as one directory entry.
    Box edits go through `RegionEdit::applyBox`; spheres stay per voxel.
  - The edit path collapses with slack (`collapse(0.25)`: free pool slots reused before compacting; F1's
    `collapse()` unchanged).
  - Picking (§7 item 4's interface): `RegionEdit::pick`, a voxel DDA across the chunk stores that skips uniform air
    blocks whole. It returns the first solid voxel, the face entered, the distance and the brush cells to break and
    to place. API `{"pick":{"origin":[m],"dir":[...],"max_m":m,"size":s}}`.
- Red first: `FillBlockEqualsVoxelWrites`, `BrushEditsAreExactAndAligned` (200 brushes and unaligned boxes across the
  chunk border; store and lists = a full rebuild), `PickFindsTheFirstSolidVoxel` (400 rays = brute force: voxel, face,
  distance). CPU 67/67, GPU 22/22.
- Measured live (`voxfound_f32b_*`, prediction `voxfound_f32b_prediction.txt`): 0 mismatches in 313 verified edits
  (F3.1's 200 generator edits + 160 brushes aimed by picks from the 5 poses); 0 px vs a full rebuild after 20 picked
  brushes at two poses.
  - Request to lists ready (median): brush 1 / 2 / 4 / 8 voxels 0.19 / 0.21 / 0.24 / **0.50 ms** (p90 0.88 at
    size 8). The generator's 1 m box 0.55 ms and 4 m box 5.4 ms (F3.2a: 0.66 / 7.2): collapse 0.24 → 0.004 ms,
    set 1.04 → 0.47.
  - Pick 0.006 ms median (max 0.02) up to 95 m; every hit entered through a face with air in front of it.
  - The small brushes came in below the predicted ranges (their rows are short on a picked surface).
- Verdict: brushes and picking work over the API, exact, at 0.50 ms for one block. Wiring the editor's keys to them
  is F7 (§7 item 4).

**F3.3 2026-11-16 — codec v3: save / load round-trips exactly; only chunks that differ from the generator are saved.**
- Built: `BlockStore::encodeV3` / `decodeV3` ('PXF3' v1: dims, a chunk-local palette, the directory RLE in block
  order, then each detailed block's voxels RLE; canonical, ≤ 255 materials per chunk, every field bounds-checked).
  `remapMaterials` maps names to the region palette. World.db table `voxfound_chunk_blobs` (WorldStorage
  save / load / delete / list; today's PXB2 `chunk_blobs` untouched until F7). `RegionEdit`: a chunk an edit changes
  is marked edited; `loadChunk` writes a saved chunk through the exact incremental rebuild, block by block where it
  differs. API: `{"save":true}` writes the edited chunks; a region build with `{"load_saved":true}` loads them over
  the generator.
- Red first: `CodecV3RoundTripsExactly`, `CodecV3RejectsMalformedBlobs`, `SavedChunkReloadsExactly`. The round trip
  caught a real decoder bug: the dims were read in one constructor call, whose argument order is unspecified, so
  MSVC swapped x and z on non-cubic stores. CPU 70/70, GPU 22/22.
- Measured live (`voxfound_f33_*`; prediction `voxfound_f33_prediction.txt`):
  - Round trip: 240 edits, save, then a rebuild with `load_saved`. 32 of 32 chunk voxel hashes equal; 0 px reloaded
    vs before the save and vs a full rebuild (two poses); re-saving is byte-identical for 32 of 32 chunks.
  - One edited chunk saves one blob; the other 31 stay the generator's.
  - Blobs: 7.6 MB for the 32 chunks (62.9 MB in the store, 545.5 MB dense). Ground chunks 62-230 KB, canopy chunks
    7-830 KB. Encode 2-10 ms and decode ~3.6 ms per chunk; loading all 32 into the region takes 1.5 s.
  - The size and time predictions were wrong: everything came in smaller and faster than predicted, and the upper
    chunks are canopy, not sky.
- Verdict: the F3 row's second half (save / load round-trips exactly) holds.

**F3.4 2026-11-16 — the converter from PXB2: old worlds convert; cubes exactly, thirds / ninths by volume.**
- Built: `convertPxb2` (StorageLab) reads today's chunk blob (v1/v2) and writes a 256³ store.
  - Present cubes become their 8³ block.
  - A cell with subcubes / microcubes becomes a 9³ grid of ninths, resampled to 8³ by volume per material (air
    counted; ties to the solid material, then the lower palette index).
  - Then §7a step 4's "no detail vanishes": a material the vote dropped from a cell gets the voxel where it has the
    most volume.
  - Tint, state and water spans are not carried (counted).
  - `forEachStoredChunk` builds a region from stored blobs (a ring of neighbours for the border planes). A region
    build with `{"source":"world_db"}` draws a saved world through the new store.
- Red first: `ConverterCubesAreExact`, `ConverterResamplesByVolume` (equal voxel for voxel to a reference written
  apart in the test), `ConverterRejectsMalformedPxb2`, `ConverterKeepsEveryMaterialOfACell` (549 of 758
  cell-materials lost on the vote alone → 0). CPU 74/74, GPU 22/22.
- Measured live on a copy of CityBench_C25 (a generated city saved in world.db: 64 chunks, 25 buildings;
  `voxfound_f34_*`, prediction `voxfound_f34_prediction.txt`):
  - Volume: Stone / Dirt +0.00 %; Grass, StoneBricks, Leaf within 0.3 %; mid materials ±1-12 %.
  - Thin trims lose volume: glow −21 %, Metal −31 %, LogBirch −86 % (one-ninth-thick slabs). Predicted ±25 %, and
    after the rule −50..+100 %: wrong for LogBirch.
  - Convert 7.5 ms per chunk.
  - Pixels vs today's store drawing the same saved world: 12 / 18 / 37 % at three city poses. In the diff maps the
    cube surfaces (walls, streets, building walls) are identical; the differences are today's grass blades, leaf
    bushes and the 1/8 m roof courses.
  - Found by eye: planters (4 leaf microcubes per metre cell) are lush today and nearly bare here. The converter now
    keeps their leaves as voxels, but the sprig model draws ~7 % of exposed leaf voxels while today draws a card per
    microcube. That is the leaf model's gap (FC), not the converter's.
- Owed: an occupancy rule for thin parts (§7a step 4's nearest-edge mapping) if thin trims must keep their extent;
  small leaf features' look (FC).

**F3.5 2026-11-16 — the F3 exit gate, first run: exact under 1,000 edits and 10 reloads; edit cost fails on chunk
borders.**
- Run: the gate region, 1,000 verified brush edits (500 aimed by picks, 500 on chunk faces / edges / corners) in 10
  batches; after each batch save → rebuild with `load_saved` → every chunk hash equal; editing continued on the
  reloaded region (`voxfound_f35_*`, prediction `voxfound_f35_prediction.txt`).
- Exact at every step:
  - 0 mismatches in 1,000 edits;
  - 10 of 10 reloads with all 32 chunk hashes equal;
  - 0 px vs a full rebuild at two poses;
  - the reloaded region's save byte-identical to the save it came from.
  So save / load round-trips exactly holds.
- Edit cost: the script confounded size and kind. Sizes 1 and 4 were always interior picks, sizes 2 and 8 always
  border edits; recorded as a script defect. Measured, request to lists ready (median, ms):
  - interior 0.18 (size 1), 0.21 (size 4);
  - one-block brush (size 8) on a chunk face 0.69, an edge 0.82, a corner 0.90 (2 / 3 / 4 chunks rebuilt).

  The cost scales with the chunks an edit touches. Inside a chunk a one-block brush is 0.50 ms (F3.2b), but at a
  corner it is 2.2× below today's 2 ms, not ≥ 3×.
- Also: the edit store grows 62.9 → 84.6 MB over 1,000 edits (1.34×; predicted ≤ 1.3×). Blobs 6-8 MB; save
  0.13-0.2 s; reload 26 s.
- **Owed before F3 closes:** the per-touched-chunk overhead of a border edit (each chunk pays its own plan, row
  executor, cluster update and pool span), then F3.5 re-run with size and kind drawn independently.

**F3.5a 2026-11-16 — the border edit overhead: a border edit now costs what an interior one does (exact).**
- Diagnosis (`voxfound_f35a_profile.py`; contended, used for the phase split only):
  - a size-8 brush is block-aligned, so it edits ONE chunk; the other 1-3 were re-planned only because their border
    plane was written;
  - each of those paid three passes over its whole face list (the plan's scan, the cluster update's copy, the pool's
    span rewrite, ~300 KB) whatever changed;
  - assemble grew 0.11 → 0.46 ms, plan 0.03 → 0.16, cluster 0.02 → 0.14 from 1 to 4 chunks.
- Changes, all exact by construction:
  - (A) a neighbour is re-planned only when one of its border bits flips;
  - (B) a re-planned chunk whose face delta is empty as a set, whose sprigs and connectivity are unchanged, keeps its
    lists and is left out of the assemble (`Stats.changed`; verify still rebuilds every re-planned chunk);
  - (C) the plan examines only the faces of direction groups whose box meets the edit box grown by 2;
  - (D) the cluster update copies an untouched group as one block.
  - A/B switch `{"legacy_border":true}`.
- Red first (`voxfound_f35a_red.txt`):
  - BorderEditsTouchOnlyTheChunksThatChange: today re-plans and assembles both chunks for a Stone → Dirt / an air
    border brush;
  - PlanThroughGroupsEqualsTheFullScan: today examines all 58,578 faces.
  - Green: CPU 76/76, GPU 22/22; a box ≤ 8 now examines 276 faces on average.
- Live A/B (`voxfound_f35a_ab.py`, ABBA, 4 × 120 seeded size-8 edits): 480 verified, 0 mismatches. Request to lists
  ready, median, old → new, CONTENDED (WoW was running):

  | Edit position | Old (ms) | New (ms) |
  |---|---|---|
  | Interior | 0.71 | 0.70 |
  | Chunk face | 0.90 | 0.70 |
  | Chunk edge | 1.21 | 0.71 |
  | Chunk corner | 1.00 | 0.52 |

  Chunks assembled went from 1/2/3/4 to 1/1/1/1.
- Uncontended (launcher only, checked before and after; same script, ABBA): 480 verified, 0 mismatches. Old → new
  median:

  | Edit position | Old (ms) | New (ms) |
  |---|---|---|
  | Interior | 0.68 | 0.66 |
  | Chunk face | 0.91 | 0.61 |
  | Chunk edge | 1.14 | 0.72 |
  | Chunk corner | 1.00 | 0.48 |

  The border premium is gone. Wrong vs the prediction:
  - interior 0.66, not 0.45-0.55 (these seeded edits are all full blocks that flip their blocking bit, so every one
    floods; F3.2b's 0.50 was picked edits);
  - the edge 0.72 > 0.67.
- **Owed before F3.5's second run:** incremental connectivity. The flood is 0.35 ms of a full-block edit's ~0.65 ms,
  so the F3 line (≤ 0.67 ms for a one-block brush) is not met for a block that flips. Then F3.5 again (size and kind
  independent; its prediction is appended to `voxfound_f35_prediction.txt`).

**F3.5b 2026-11-16 — incremental connectivity: a one-block brush ~0.3 ms to lists ready anywhere (exact).**
- `BlockConnectivity` (Mesher.h) keeps the flood's answer incrementally. It holds:
  - a component label per block, with union-find over the labels;
  - per root, the mask of chunk faces the component touches;
  - a count of roots per mask; `connect()[f]` = the OR of the masks present that have bit f.
- How each kind of change is handled:
  - A block that opens merges its neighbours' components and never floods.
  - A block that closes is decided by a local test in its 3×3×3 neighbourhood: its open neighbours must still be
    connected without it, and every chunk face it touched must still be touched by a reached cell.
  - The chunk floods only when that test cannot decide.
  - Several flips in one edit are applied one at a time.
  - A/B switch `{"legacy_connect":true}`.
- Red first (`voxfound_f35b_red.txt`): IncrementalConnectivityEqualsTheFlood on a stub flooding every update
  (1,031 / 1,031).
- Green:
  - 4,500 random updates equal to the flood at every step (three densities, flips on faces / edges / corners,
    boxes, shuffled order);
  - a brush-like sequence floods 2 times in 1,031 updates;
  - CPU 77/77, GPU 22/22.
- Live A/B (`voxfound_f35b_ab.*`, F3.5a's script and seeds, ABBA; no WoW, another session's engine running, noted):
  480 verified, 0 mismatches. Connectivity 0.35 → 0.003 ms; edits that flooded 112 / 128 → 0 / 128; labels 4.2 MB for
  the region. Request to lists ready, median old → new:

  | Edit position | Old (ms) | New (ms) |
  |---|---|---|
  | Interior | 0.65 | 0.32 |
  | Chunk face | 0.62 | 0.31 |
  | Chunk edge | 0.70 | 0.36 |
  | Chunk corner | 0.49 | 0.28 |

- Next: F3.5's second run.

**F3.5 second run 2026-11-16 — the F3 exit gate holds; F3 closes.**
- Run: the first run's method, with size and kind drawn independently (`voxfound_f35_gate_run2.*`, prediction appended
  to `voxfound_f35_prediction.txt`). Only my engine running, no WoW. A first attempt crashed at the pixel step (a
  relative screenshot path); the script was fixed and the whole run repeated.
- Exact at every step:
  - 0 mismatches in 1,000 verified edits;
  - 10 / 10 reloads with all 32 hashes equal;
  - 0 px vs a full rebuild at two poses;
  - the re-save byte-identical (31 / 31);
  - by eye, no chunk line in the 4 × 4 overview.
- Edit cost, request to lists ready, median (ms):

  | Brush size | Picked | Chunk face | Chunk edge | Chunk corner |
  |---|---|---|---|---|
  | 1 | 0.13 | 0.11 | 0.23 | 0.14 |
  | 2 | 0.13 | 0.23 | 0.12 | 0.32 |
  | 4 | 0.18 | 0.18 | 0.20 | 0.23 |
  | 8 (one block) | 0.26 | 0.26 | 0.34 | 0.42 |

  A one-block edit at its worst (a chunk corner) is 4.8× below today's 2 ms low end; reloaded regions edit at the
  same cost.
- Also: the edit store grows 62.9 → 84.4 MB (1.34×); blobs 5.7-7.7 MB; save 0.12-0.21 s; reload 25.7 s.
- **F3 closed.** Owed into later rows:
  - the converter's thin-part occupancy rule;
  - the small-leaf look (FC);
  - edit store growth.

## 9c. F4: fracture core

### F4.1 CPU reference (2026-11-17)

`fractureReference()` in `voxfound/FractureLab` implements §6.3-6.5 on a plain voxel grid.
- Model and predictions were written before the code: `docs/evidence/voxfound_f41_prediction.txt`.
- Red: the 8 rig predictions of §8 failed on the F0 stub (`voxfound_f41_red.txt`).
- Lengths are in metres, so the same rules run on world voxels (1/8 m) and item voxels (1/64 m, §6.8).

**The model:**
- **Stress.** s(d) = E / (1 + (d / 0.25 m)²), where d is the distance from the hit.
- **Bond strength.** min(toughness) × k × (1 − 0.5 · damage / 255), where k is:
  - 0.3 between parts (mortar, glue);
  - 4 for a bond across wood fibres;
  - 1 otherwise.
- **Where a bond can be cut.** Only on a weak plane:
  - a part or material boundary;
  - a crack-pattern boundary.

  A bond is cut where s ≥ its strength. Nothing else is ever cut.
- **Patterns:**
  - **Brittle and fibrous:** intersected jittered-grid Voronoi levels (1 m / 2^k). The local cell size is
    h(d) = 0.5 m · (1 + d / 0.25 m) / cbrt(E/T). Fibrous cells have the same volume, 4× longer along the grain.
    Integer seeds hashed from the event seed and the absolute cell.
  - **Glass:** polar cells — round(4·cbrt(E/T)) sectors × rings at ×1.7.
  - **Parts:** a part whose cbrt(volume) ≤ h at its centroid moves whole, so its cracks run along its mortar or
    joints.
- **Pieces.** Union-find over the uncut bonds.
  - A piece is anchored if it holds a fixed voxel.
  - Loose pieces under 4 voxels are crumbs (a tier knob); the rest are bodies.
  - Damage is added everywhere, cut or not.
- **Nested by construction.** For brittle and fibrous materials without prior damage, more energy cuts a superset,
  so loose matter never decreases. Glass is not nested.

**Results (14/14 green on the second run).** The first run had one red test, the beam. It came from a code mistake,
not tuning: "stretched" fibrous cells had 4× the volume of h³, so the pattern could not enter a 2-voxel beam. A
second mistake made a whole chair count as crumbs: the crumb threshold had been written in m³, but the plan's rule is
"< N voxels". Both are recorded in the prediction file; nothing else changed between runs.

| Rig (§8) | Prediction | Measured |
|---|---|---|
| Stone wall, centre, E/T 1.5-80 | loose matter never decreases; more and smaller loose pieces at 80 than at 2 | loose voxels 0,0,0,0,11,16,33,280,672,744; at 80: 21 bodies (712 voxels) + 22 crumbs (32 voxels), 408 voxels stay anchored |
| Stone wall, edge vs centre, E/T 5 | edge ≥ centre | 0 ≥ 0 — **holds only trivially** (40 vs 78 cuts, nothing loose at E/T 5) |
| Brick wall, E/T 8 | > 80 % of cuts between bricks | 398/398 (100 %, as estimated); at E/T 30, 688/710 (97 %) as bricks start to split |
| Beam, grain along x, E/T 8 | cuts in y + z > 3× cuts in x | x 2, y + z 14 (ratio 7). My own estimate of y + z ≥ 15 missed by 1 |
| Glass pane, E/T 20 | ≥ 6 shards over ≥ 6 of 8 sectors | 34 loose (14 bodies, 20 crumbs), 8 of 8 sectors |
| Column + lintel, E/T 30 | every lintel voxel loose; the column base anchored | holds; the lintel comes off whole, the column shatters around the hit |
| Chair (1/64 m), E/T 8 | > 80 % of cuts between parts; ≥ 2 pieces | exactly 6 pieces (seat, back, 4 legs), 108/108 cuts at joints |
| Control, E/T 0.5 | nothing cut; damage rises | 0 cuts; damage 107 at the hit voxel (my estimate of 128 assumed d = 0) |

**Every hit, exactly:**
- the result equals the independent checker (pieces and anchoring);
- two runs are identical;
- conservation holds per material (anchored + bodies + crumbs = before).

**Stress (300 random hits over all rigs, E/T 0.3-100):** all of the above held at every hit, plus:
- the crumb knob changes kinds, never pieces;
- 250 nesting checks held: twice the energy cuts a superset;
- repeating a blow on the damaged grid cut a superset every time, and cut more in 138 of 300.

**Cost.** The CPU reference takes 0.03-0.71 ms per rig hit (Release). It is not the runtime path.

**Pictures.** `docs/evidence/f41_*.png`, `f41_stone_sweep.gif`, `f41_stone_e80_apart.gif`.
- Provenance: drawn by `voxfound_f41_pictures.py` (PIL, no engine) from the reference's per-voxel dump
  (`VOXFOUND_FRACTURE_DUMP`).
- What they show:
  - an exploded isometric view, with loose pieces pushed toward the viewer and crumbs white;
  - a face-on map from the hit side: black lines are cracks between pieces, red lines are cut bonds that free
    nothing.
- By eye:
  - the bricks come out whole along the running bond;
  - the chair comes apart at its joints;
  - the stone wall cracks in a cross at E/T 5 and breaks into irregular slabs and crumbs at 30-80;
  - the beam splits lengthwise near the hit;
  - the glass shows a core of crumbs inside wedge-like shards, but at 1/8 m a 3 m pane is only 24 voxels across,
    so the radial look is coarse.

**Owed:**
- **The owner's review.** The F4 stop condition is "the patterns don't read as real cracks". These stills are not
  yet the video of real breaking, which needs bodies (F5).
- **The anchoring search size at scale (§3.5a.6).** Search the block graph first, and measure the worst case on the
  wall, the column and a whole engine-generated building.
- **A stronger edge-hit rig.** At E/T 5 nothing comes loose, so the edge-vs-centre prediction holds only trivially.
- **GPU-parity details.** The Voronoi is integer-exact. The stress compares, the level choice and the glass angle
  are floating point, so the GPU port must match them or move them to integers.

### F4.2 Anchoring at scale (2026-11-17)

This answers §3.5a point 6: after a cut, is a piece still bonded to something fixed, without flooding the town?
- Prediction: `voxfound_f42_prediction.txt`, written before the code.
- Red: a stub that frees nothing failed every hit that frees a piece.

**The method.**
- **`BlockSummary`** is what the store keeps per block (§3.2): the solid voxels' components inside each 8³ block,
  and which components hold a fixed voxel.
- **`findLoose(grid, summary, cut)`** works like this:
  - It searches voxel by voxel only in dirty blocks, i.e. blocks holding an endpoint of a cut bond.
  - Every other block is one node per component. Nodes connect through the solid voxel pairs on shared block faces.
  - It runs a breadth-first search from each endpoint of a cut bond.
  - It stops at the first fixed node, or a node already known to be anchored.
  - A search that runs out without stopping is a loose piece.

**The rigs:**
- the wall and the column (from §8);
- a whole building from the engine's generator: `StructureRealizer::realizeShell` on the 1/8 m grid, the F2 9×7 m
  two-story cottage, with its bottom layer fixed. 102,536 solid voxels, 519 blocks, 561 block components.

**Results.** 400 random hits at E/T 0.3-100: **exact** — 0 mismatches with the reference's loose pieces.

| Rig | Hits (freeing) | Nodes visited, median | Worst | Worst as % of solid voxels | ms median / worst |
|---|---|---|---|---|---|
| Stone wall (6 blocks) | 100 (43) | 233 | 1,000 | 86.8 % | 0.03 / 0.10 |
| Column + lintel (4 blocks) | 100 (64) | 79 | 112 | 100 % | 0.01 / 0.02 |
| Building (519 blocks) | 200 (95) | 1,055 (1.0 %) | 8,558: 8,457 voxel nodes + 326 component nodes | 8.3 % | 1.2 / 2.7 |

- **The wall and the column:** on rigs of 6 and 4 blocks, the block graph cannot help, as predicted.
- **The building:** the worst search is 12× smaller than flooding the building.
  - Voxel nodes near the cut dominate the worst case. The block graph crosses the rest in ≤ 326 nodes.
  - The 1.2 ms median missed my ≤ 1 ms estimate. The cause is setup, not search: each call allocates per-voxel
    arrays over the whole 331k-cell grid and scans the dense cut array for seeds. The engine's path takes its seeds
    from the break and keeps its buffers.

**Found: the generator's stair isn't bonded to the building.**
- 11 treads touch each other only along an edge, and touch no wall.
- The stairwell guard touches nothing.
- Together that is 1,168 voxels on 1/8 m. The same building on today's 1/9 m grid has the same defect: 13 pieces,
  2,000 voxels.

Under §6.5 these parts are loose before any hit. The comparison therefore excludes pieces that were loose before
the hit, unless the cut touches them; they are counted and printed.
- Logged in `StructurePipelineGaps.md` (2026-11-17).
- Owed to the generator:
  - a stair carriage;
  - guard posts;
  - an L2 invariant: every voxel of a realized shell is bonded to its foundation.

**Still owed in F4:**
- the owner's review of the F4.1 pictures (done 2026-11-17: "cracks look good");
- the GPU port, equal to the reference (F4.3);
- the incremental store-kept summary (like F3.5b's `BlockConnectivity`), measured inside a streamed town at F8.

### F4.3 The GPU port (2026-11-17)

`FractureGpu` with `shaders/voxfound_fracture.comp` runs one thread per voxel, in passes:
1. pattern ids;
2. cut bits and damage;
3. connected components over the uncut bonds (hook and pointer jumping, repeated until a round changes nothing; the
   converged label is the component's minimum voxel index).

The host numbers pieces by ascending root, and `finishPieces` is shared with the reference. Because the reference
numbers pieces by their first voxel, the piece ids are equal, not merely equivalent.
- Prediction: `voxfound_f43_prediction.txt`.
- Red: a stub failed all 3 tests (619 failures).

**Step 1: the reference became integer arithmetic.** Exact equality needs it: GLSL float division and transcendentals
are not correctly rounded, and the device has no 64-bit ints, so the shader emulates u64 on `uvec2`.
- A shared host prelude (`planFracture`) computes the per-hit scalars and tables:
  - E/T and cbrt;
  - the quantized energy, toughness and hit point;
  - per-part "moves whole";
  - glass ring thresholds and sector-boundary vectors.
- Every per-bond and per-voxel rule is then an integer compare.
- Every F4.1 rig number was unchanged. Glass's x/y cut counts swapped (the sector origin now follows the grid axes),
  with the same shards.

**Results (first run, no fixes; only the Battle.net launcher running):**
- **Exact:** 18/18 rig cases and 600/600 random hits (300, each repeated on its damaged grid) are equal in every
  channel: cut, damage, piece, anchored, size, kind.
- **Cost per break** on a 32×16×0.5 m wall (131k voxels, base fixed, a hand-built shape like the §8 rigs):

| E/T | Loose pieces | GPU device ms (rounds) | Host ms | CPU reference ms |
|---|---|---|---|---|
| 8 | 0 | 1.75 (10) | 3.8 | 14.2 |
| 50 | 38 | 1.82 (9) | 3.8 | 14.8 |
| 150 | 122 | 1.76 (10) | 3.9 | 16.9 |
| 1,000 | 936 | 1.65 (9) | 3.6 | 34.2 |
| 2,500 | 1,981 | 1.47 (9) | 3.9 | 51.5 |

  At ~1,000 pieces the GPU is 21× faster than the CPU reference. But my prediction that cost would rise with the cut
  region was wrong: it is flat, because every pass covers the whole grid (the id pass computes every pattern level
  for every voxel).

**Levers, owed when breaks run inside the world (F5/F8):**
- run the passes only over the stressed box (where s can reach the weakest bond);
- compute ids only where a bond needs them;
- number pieces on the GPU instead of the O(voxels) host pass.

**F4 status.** The exit test is met:
- every §8 rig passes its prediction (the edge hit only trivially);
- conservation and determinism are exact;
- the GPU equals the reference.

The owner's review of the crack pictures is the remaining stop condition. The generator's unbonded stair is owed to
the structure generator.

**F4 closed 2026-11-17.** The owner reviewed the pictures: "cracks look good". The live check (things breaking and
falling in the engine) moves to F5, where loose pieces become bodies.

## 9d. F5: bodies

F5's increments, in the order of the F5 row:
1. F5.1 extraction (CPU reference);
2. F5.2 GPU simulation with voxel collision (against the world, then body against body);
3. F5.3 drawing with one transform;
4. F5.4 the rest / bake-in hybrid (D6), conservation and pop measured;
5. F5.5 the exit stress: 100 → 1,000 bodies.

### F5.1 Extraction (2026-11-17)

`extractBodies` (`engine/*/core/voxfound/Bodies.*`) runs after a break (§6.5 step 3):
- every voxel of a loose piece leaves the world grid; anchored pieces stay untouched (the hit's damage applied);
- a **Body** piece becomes a small grid (its bounding box) with an origin in world voxels. Its voxels keep material,
  part and damage. Mass, centre of mass and the inertia tensor come from the voxels: each voxel a uniform cube,
  density per material (handbook values, kg/m³: Stone 2600, Bricks 1900, Wood 700, Glass 2500);
- a **Crumb** piece's voxels become crumbs: position, material, part, damage. Counted, never deleted; F6 turns them
  into powder;
- the hit's momentum (`Hit::impulse`, N·s along the hit direction) is shared over the bodies with the stress falloff
  of §6.4 (weight = mass / (1 + (d / 0.25 m)²), d to the body's voxel nearest the hit) and applied at that voxel, so
  a body also spins. This split is a modelling choice; what is tested is that momentum is conserved. Crumbs get no
  momentum yet.

Prediction: `voxfound_f51_prediction.txt`. Red: a stub failed all 5 tests. Run 1, no fixes (`voxfound_f51_run1.txt`):
- **Conservation, exact.** 6 rigs × 10 energies (557 bodies, 494 crumbs) and 300 random hits (1,491 bodies, 1,037
  crumbs): every loose voxel is in exactly one body or crumb at the same position with the same material, part and
  damage; every anchored voxel is unchanged; per-material counts equal. Two runs are bit-equal.
- **Churn.** A stone wall hit 20 times in a row, each on the world the last extraction left: exact after every hit
  (988 of 1,152 voxels end up as 31 bodies and 66 crumbs).
- **Mass properties.** A free 8×4×2 stone box: 325.000 kg, I = 8.46354 / 28.77604 / 33.85417 kg·m², equal to the
  closed form to 1e-9. Every body equals an independent brute-force sum to 1e-9.
- **Momentum.** The bodies' momenta sum to the hit's to 1e-9, and I·ω = r × Δp on every body.
- **Cost** (CPU, the F4.3 large wall): 0.48 ms (59 bodies, 87 crumbs), 2.44 ms (468 / 651), 5.09 ms (947 / 1,407),
  inside the 1-6 ms estimate. WoW was running during this run, so the figures are an upper bound; reported, not gated.

### F5.2a The CPU reference body solver (2026-11-17)

**What exists.** The engine's GPU solver (`GpuParticlePhysics`, `solver_*.comp`) runs AVBD, but every body is one
box with sphere inertia and it can't run headless. So `BodySimRef` (`engine/*/core/voxfound/BodySim.*`) uses the same
algorithm and constants and extends them to §6.7's bodies:
- a body is its F5.1 voxel grid with the full inertia tensor;
- contacts are voxel-on-voxel and symmetric: X's surface corners are tested against Y's cells and Y's corners against
  X's, with the world as a static Y;
- body-body broadphase is on world AABBs (any size), with a per-pair cap of 8 and every dropped candidate counted;
- the engine's hard-contact pass is included, extended to body-body.

It runs in double precision, Gauss-Seidel per colour, deterministically. Prediction: `voxfound_f52_prediction.txt`.
Red: a stub failed all 8.

**Run 1: 3 of 8 passed.** One rig was mis-specified (R3's slab started about 20° from its narrow face and stood on
it). The amended assertion and an intended R3b were written before run 2. The contact model needed seven fixes, each
recorded with its numbers:
1. Sample points are inset 0.001 voxel into their own solid.
2. A sample contacts only through an exit facing its own face. Side-face samples on edges had taken 6 of a pair's 8
   slots, and a 5-box stack sank 1.30 m.
3. The capped contact set persists from tick to tick.
4. Exits are searched over 26 neighbours. Jagged fracture faces gave 11 cm false depths and deep points.
5. Speculative contacts use faces only, and the alignment slack is 0.05 voxel. Both stopped a sliding box tipping.
6. A body-body impact starts soft (0.2 × the reduced mass / dt²).
7. The hard-contact pass is included, with the world pass last.

**Run 2: 11 of 11 pass.**

| Rig | Measured |
|---|---|
| R1 free fall | 7.5e-14 m from the closed form |
| R2 flat drop | rests 0.19 mm deep, asleep by 3 s, no drift |
| R3 / R3b slab | flat on a face within 0.05°; R3b on its large face |
| R4 slide | 0.238 m vs Coulomb 0.255 m (−6.5 %), no tip |
| R5 stack of 5 | 2.1 mm, all asleep |
| R6 head-on | momentum **MISSED** "1e-6": 4.7 % of m·v at 8 iterations, 0.72 % at 32. AVBD conserves momentum only when converged. The 8-iteration gate (5 %) was set after the measurement and says so. |
| R7 lintel | 4 bodies asleep by 3.4 s, 3.75 mm, none deep |
| R8 broken wall | 2.1 / 0.7 mm. But the pieces barely moved, because 20 N·s spread over about 3 t of rubble is small. |
| R8b blast (8,000 N·s) | pieces fly at up to 7.8 m/s, pile, all 21 asleep by 4 s, 8.5 mm, none through the ground |

**Owed:**
- the hard-contact clamp fires 273 times (R7) and 563 times (R8b); the engine's settle bench wants 0 after the impact
  window;
- the cap dropped about 35k candidates in R8.

CPU reference cost: 0.2-1.1 ms per tick mean, 10.9 ms worst, at 21 bodies (WoW was running).

### F5.2b The GPU port (2026-11-17)

`BodySimGpu` + `shaders/voxfound_bodies.comp` run the same tick headless, in float. The GPU version differs from the
reference in five places, all written in the shader's header:
- candidates go to a per-body scratch;
- a world surface corner is taken once, by a canonical-owner rule;
- each body's constraint list is sorted by key, so every sum is order-independent;
- colours come from Jones-Plassmann rounds;
- body-body hard-contact pushes are computed per body and summed.

Prediction: the F5.2b section of `voxfound_f52_prediction.txt`. Red: a stub failed all 9.

**Run 1, no solver fixes.** The GPU was shared with WoW and another session's engine.
- **Every rig passes the reference's own assertions.** R1 is 7.6e-5 m from the closed form (float). The rest: R2
  0.15 mm; R3/R3b flat; R4 0.238 m; R5 2.05 mm; R6 4.7 % / 0.66 %; R7 5.6 mm; R8 0.9 / 0.6 mm; R8b 7.4 mm with all
  21 asleep.
- **Trajectories:** within 0.33 mm of the reference on R1, R2 and R4.
- **Determinism:** two runs are bit-equal, 630 of 630 samples.
- **Capacity:** nothing overflowed, no body was left uncoloured, and at most 8 colours were used.
- **Cost MISSED:** 8-19 ms per tick against 0.5-3 ms.
  - It does not follow the body count: 7 bodies, all asleep, cost 8.1 ms. The floor is about 310 dispatches and
    barriers per tick (32 colours × 8 iterations).
  - Levers, for F5.5: dispatch only the colours used, fuse passes, skip sleepers.

### F5.3 Drawing with one transform (2026-11-18)

§6.7 "Drawing" as written: each body's faces are built once (`BodyDraw` `buildBodyFaces`: `meshRuns` on the body's grid,
the CPU reference the GPU face builder equals) into the §4.2 8 B record + a body index (`BodyFace`, 12 B). All bodies are
drawn in ONE draw by `shaders/voxfound_body.vert`, with one pose per body (rotation + translation made camera-relative in
double on the CPU each frame, 64 B). The same buffer feeds the caster `voxfound_body_shadow.vert` (mid + near cascades).
- **Texture (§4.3):** a pure function of where the voxel was when it broke off, so a piece carries its texture. The
  engine's other moving voxels turn "varied" off, which would pop stone's tile at the break. Instead the body shader puts
  the REST-frame world-face coordinates in `texCoord` and one `voxel.frag` line takes the varied tile from them (flags
  bit 16 with the LOD bit clear; LightingPipeline §9).
- **Live:** `POST /api/debug/voxfound_bodies` (`editor/src/VoxFoundBodiesLab.cpp`) puts a §8 lab rig on a drawn floor,
  breaks it, extracts it and moves the pieces with the CPU reference solver. RIG DIFFERENCE: the shipped path is the GPU
  solver, unusable live until F5.5's dispatch levers. Project `VoxFoundBodies`; harness `tools/voxfound_f53_gate.py`.

Prediction: `voxfound_f53_prediction.txt`. Red: a stub failed all 5 unit tests.

**Unit (run 2, 5/5; run 1's one failure was my hand expectation in D3, not the code).**
- Faces exact on 1,150 bodies (0 coverage errors) and built once over 300 ticks.
- At rest the transform is exact (0 m with a whole-metre camera). Moved and turned up to 180 degrees, it is within
  3.3e-7 m of the double-precision pose.
- **MISSED:** building faces for 947 bodies takes 18.5 ms against 2-15.

**Live (Release; the GPU was not shared this time).**
- **G1, the draw is invisible at rest:** bodies vs the same voxels through the preview path differ in 15 of 185,000
  pixels above the control's noise floor (0.008 %, prediction ≤ 0.01 %). A 0.5-degree mutation moves 121,071 pixels.
  **MISSED:** the control was not 0 px. 20 % of the box flickers by 1 level even with effect time frozen; the source is
  unexplained.
- **G2, the eye check:** a blast sequence goes to the owner (`voxfound_f53_seq_stone_wall_blast_*.png`). The pieces fly,
  turn, keep their texture and shadows, and pile. The solver's impact depths reach 50 mm mid-flight (5.5 mm at rest),
  which is F5.2's owed hard-contact item, not the draw.
- **G3, shadows:** a piece in flight darkens 15.3 % of the frame against cast off; hidden bodies change 5 px.
- **G4, cost:**
  - 28 bodies: 0.033 ms main, 0.002 ms per cascade, whole frame within noise.
  - **MISSED** at 947 bodies (54,439 faces): 1.19 ms main against ≤ 0.3, and 0.26 + 0.31 ms shadow. The cost follows
    the faces (about 22 ns each), and nothing is culled per body yet.
  - Levers, for F5.4 / F5.5: frustum and light cull per body, fewer faces (greedy instead of runs), and baking asleep
    bodies back into the world (F5.4).
  - CPU poses: 0.0125 ms per frame at 947.
- **Found:** the MCP engine launch dies silently because this session's PATH lacks the build's `python312.dll` (uv
  CPython 3.12). Launch with it prepended.

### F5.4 Rest and bake-in, the hybrid (2026-11-18)

D6 as written: a body stays a body while it is near the player or recently moved. When the body budget is exceeded,
bodies asleep long enough and outside the keep radius are baked into the world grid, farthest first
(`BodyRest`: `chooseBakes`, `planBake`, `applyBake`, `restPass`; `BodySimRef::remove`).
- **The bake is a bijection, voxel to world cell.** Material, part and damage are carried, and no solid cell is
  written over. Two paths:
  - **rigid:** the rotation is within 10 degrees of an axis rotation, so the piece keeps its exact shape, shifted by at
    most half a voxel per axis;
  - **resample:** each voxel takes the nearest free cell to its true centre (its own cell first, then a greedy pass
    within 2 voxels).
  A voxel with nowhere to go refuses the whole body, which is counted. Bodies touching a baked one wake after the pass
  and settle on the new cells.
- **Tier knobs (§6.9):** body budget, keep radius, rest ticks.
- **Live:** the F5.3 route takes `rest` / `bake_now`. A bake rewrites the preview area and compacts the body faces by
  copying ranges (faces are still built once per body). Responses carry a per-material ledger.

Prediction: `voxfound_f54_prediction.txt`.
- **Red:** stubs failed 5 of 6. R1b passed against the stub because nothing baking conserves trivially; my test was
  weak and gained a "most bodies bake" assertion.
- **Run 1:** found my own design error. Waking neighbours mid-pass made later candidates ineligible, so one pass baked
  164 of 916; they now wake after the pass.

**Unit (run 2, 6/6).**
- **Conservation exact after every tick:** blast, 900 ticks, 24 bakes; large wall, 916 bodies, 200 ticks, then a final
  pass (879 baked in all, 0 refused, 0 overwritten).
- **Policy:** exact.
- **Shape:** a rigid bake is exact. A resample at 30 degrees moves voxels 0.36 voxel on average (at most 0.58).
- **Shift over the blast:** mean 0.35 voxel (4.4 cm), max 1.24 voxels.
- **Repeatability:** deterministic, and `remove` leaves a survivor bit-equal.
- **MISSED, rigid share:** 33 % against ≥ 80 %. Pieces rest leaning in the pile (14-56 degrees), not on a face.
- **MISSED, bake cost at 916 bodies:** 38 ms rigid (≤ 10) and 677 ms resample at random rotations (≤ 40). The cost is
  the other-body cell test. Levers for F5.5: a neighbour cell set per plan, a cap on bakes per pass.
- **Rig limit:** a piece thrown past the bounded lab grid is refused and counted.

**Live (Release; the GPU was shared with another engine for part of the run).** The stone wall blast settles, then
every asleep body bakes (27 of 28: 8 rigid, 1 refused).
- **Conservation:** the ledger is equal before and after the bake.
- **Pop, near (6 m), MISSED:** 40.8 % of the box differs above the ±1 control floor (2-30 % predicted). By eye, leaning
  pieces become stepped blocks, and the stone tile is re-chosen by the new cell. The live max shift is 22 cm (1.8
  voxels).
- **Pop, far (4x along the same ray):** 0.038 of the near pop's pixels. This is why D6 bakes only far pieces. The far
  control had 1,445 noisy px.
- **Cost:** the bake takes 2.9 ms. The whole-area preview rebuild after it takes 72-75 ms, a hitch on every pass that
  bakes. Lever: F2's incremental region edit.
- **Owner (2026-11-18), on `voxfound_f54_near_bodies.png` / `_baked.png`:** "not really much difference between
  those two images". So the eye check passes even at 6 m. The 40.8 % pixel count overstates the pop: it counts
  the re-chosen texture tile and centimetre shifts, which do not read as a change. My "plainly visible" was wrong.
  Not shown by the pair: the moment of the switch in motion.

### F5.5 The exit stress, 100 → 1,000 bodies (2026-11-18)

The F5 row's exit: 100 → 1,000 bodies, conservation, pop measured; stop if body cost at 1,000 is above budget with no
lever left. No document fixed the budget, so the prediction proposes one (PerformanceTiers: a share is a proposal): at
1,000 bodies at the 120 tier, GPU simulation ≤ 2 ms per tick with every body awake, draw ≤ 0.5 ms, CPU ≤ 1 ms per frame,
a baking frame ≤ 4 ms of hitch. Order: the owed levers first, each its own commit and prediction section, then the exit
live (`voxfound_f55_prediction.txt`).

**F5.5a, the GPU solver's dispatch floor.** A second pipeline from the same source (`voxfound_bodies.glsl` →
`voxfound_bodies.comp` and `voxfound_bodies_fused.comp`, local size 1024) runs every Jones-Plassmann colour round in one
dispatch, and each iteration's primal over every colour in one dispatch, with workgroup barriers between the steps.
That makes 17 solve dispatches per tick instead of about 290. The per-pass code is shared, so the two paths are
**bit-equal** (0 of 630 samples differ, every counter equal). New: a per-pass device-time profile
(`BodySimGpu::setProfile`).
- **Red:** the stub showed the surprise first. With NO solve at all, a tick on the ladder still cost 60-110 ms.
- **The profile corrects F5.2b's diagnosis.** The floor is not the dispatches; their least-contended cost is about
  0.3 ms. The cost is the body-body narrowphase:
  - `pairs` + `hard_body` are 40 of 45 ms at about 100 bodies and 100 of 109 ms at 1,025;
  - both run one thread per body over every other body and test voxel corners serially, so a big piece next to many
    small ones keeps one thread busy for tens of ms.
  - F5.2b's 8 ms all-asleep floor does not reproduce: all asleep costs 0.08-0.11 ms per tick now.
- **Run 1, my design error:** a fused DUAL over 30k constraints in one workgroup made the solve slower at 1,000 bodies.
  It stays a wide dispatch.
- **Run 2:** one workgroup still loses with many bodies awake (solve 7.3 vs 3.4 ms at 1,000; 1.2 vs 1.8 at 100). The
  path is therefore a cost choice per `step()`: fused while the last step ended with ≤ 128 awake bodies. It is
  bit-equal either way.
- **Run 3:** every VoxFound GPU test, 36/36.
- **MISSED:**
  - cost at every rung: 44.5 / 85.3 / 112.3 ms per tick for 105 / 293 / 1,025 bodies, against ≤ 0.8 / 1.5 / 3.0;
  - the blast at 6.7 ms (≤ 0.5);
  - colours 13 / 17 / 20 (≤ 16).
- **Found by the stress:** at 1,025 bodies one 4-voxel piece **tunnels into the ground** and falls forever, on both
  paths. It is the solver's own rule, shared with F5.2a's reference: a fast small piece gets deeper into y < 0 than the
  escape search reaches, so it gets no contact and no push. Owed in F5.5 with its own prediction.
- **Memory:** the candidate scratch is 336 MB at 1,025 bodies (per body). The narrowphase lever makes it per worker
  group.

**F5.5a2, the narrowphase in parallel.** The two hot passes the profile named now run one workgroup per body pair.
- **Pairs:** found once, by the awake bodies only, then one 64-thread workgroup per pair.
- **Candidates:** each thread counts its chunk of the pair's corner list, a workgroup scan gives offsets, and each
  writes at its offset. That is the serial loop's order, so the cap picks the same contacts.
- **The cap:** bucket winners run one thread per bucket; each spread pick is a workgroup argmax with first index on
  ties.
- **Hard contact:** each pair is tested once, not twice. The deepest point is stored and hashed, and each body sums its
  share over j ascending as before.

Results (`setNarrowParallel(false)` is the control):
- **BIT-EQUAL on the first run** at every sample of every rig (0 of 10,250 at 1,025 bodies) with every counter equal.
- **Cost, ms per tick, parallel vs serial:**
  - the blast: 1.60 vs 6.72;
  - 105 bodies: 5.2 vs 44.3;
  - 293 bodies: 6.8 vs 83.9;
  - 1,025 bodies: 10.4 vs 108.7.
  Pass at every rung; `pairs` + `hard_body` are 1.3 ms at 1,025.
- **MISSED:** the tail with 5 of 1,025 still awake costs 1.6 ms (≤ 1). Its floor is the passes still serial per body.
- **Still over the 2 ms budget at 1,025 awake, with levers left:**
  - the solve, 3.2 ms;
  - the world test in parallel, `world` 2.4 + `hard_apply` 2.3 ms;
  - a broadphase better than "each awake body against all".

**The tunnelling fix.** The contact rule (F5.2a's escape, CPU and GPU alike) looks only at a corner's 26 neighbour
cells. A corner more than one voxel inside solid got no contact at all.
- **Red:** every piece thrown at 15 m/s or more (2 voxels per tick) fell through the ground forever, and a 1-voxel slab
  stopped nothing.
- **The fix:** a corner that moved at least a voxel this tick takes its contact from its path.
  - A 3D DDA runs from the corner's tick-start position; the first solid face it enters gives the normal, and the
    depth is how far past that face's plane the corner now is.
  - The path wins over the end point's escape.
  - A corner that started the tick resting a hair inside a surface takes that surface, deepened by its move into it.
- **Run 1 had the rule wrong.** A per-tick CPU-vs-GPU trace showed a slab-resting start and a far-face escape pushing
  down.
- **Results:**
  - every 1x1x4 and 2x2x2 piece at 5-60 m/s rests on the ground, and on the slab, CPU and GPU;
  - 0 bodies through the ground on the ladder;
  - deep corners at 1,025 bodies drop from 8,839 to 36;
  - CPU 121/121 and GPU 39/39, with the bit-equalities intact;
  - cost +5 % at 1,025 bodies; the blast's +59 % is the contacts its fast pieces were missing.
- Bodies against bodies are unchanged; pieces are thick enough.

**F5.5a3, the world test in parallel.** `K_WORLD` and `HARD_APPLY`'s world test run one workgroup per awake body. A
body's items are its corners, then the world cells in its box. They are split in order (count, scan, write), so the
same cap picks the same contacts; `HARD_APPLY` reduces the deepest world point in serial order. The serial pass is the
same per-item function in a loop (the control).
- **BIT-EQUAL on the first run** at every sample (0 of 10,250 at 1,025 bodies), every counter equal; GPU suite 40/40.
- **Cost, ms per tick:**
  - the blast: 2.54 → 0.74;
  - 105 bodies: 5.4 → 2.2;
  - 293 bodies: 6.8 → 3.5;
  - 1,025 bodies: 11.0 → 7.1.
- **MISSED narrowly:** 7.1 ms at 1,025 (≤ 7), `world` 0.70 ms (≤ 0.5), the mostly-asleep tail 1.05 ms (≤ 1).
- **Largest cost left at 1,025: the solve, 3.7 ms.**

**F5.5a4, the primal in parallel.** The split profile put the solve at 1,025 bodies almost all in the primal (3.48 of
3.7 ms): each colour waited for its slowest body, a big piece summing hundreds of contacts in one thread.
- **The lever:**
  - after the one-dispatch colouring, a counting sort makes per-colour body lists and indirect dispatches;
  - each body's sum runs on a 32-thread group with a fixed shared-memory tree, in its own pipeline
    (`voxfound_bodies_primal.comp`, eight bodies per workgroup);
  - two forms call that same function: indirect per colour when many bodies are awake, one workgroup for every
    colour when ≤ 128 are.
- **Not bit-equal to the serial primal,** stated before the run: the sum is reassociated. The gate is determinism and
  every rig's assertions.
- **Run 1, my design error:** without the one-workgroup form, 256 indirect dispatches made small scenes worse.
- **Run 3, my error:** the indirect stage on every barrier doubled the serial path's asleep tick.
- **Run 4 (41/41 GPU):**
  - two runs bit-equal, and both forms bit-equal;
  - every rig's assertions hold;
  - the blast is 0.48 ms, 105 bodies 1.33 ms, **1,025 bodies 3.67 ms** (was 7.05), with the primal 0.81 ms.
- **Still over the 2 ms proposal at 1,025 awake, with levers left:** a broadphase better than "each awake body against
  all" (`pairs` + `hard_body` 1.5 ms), and the world box's empty cells.

**F5.5b, the GPU solver live in the lab and the per-body cull (2026-11-19).** The lab moved bodies with the CPU
reference since F5.3; the exit has to run the shipped path.
- **What it builds:**
  - the GPU tracks `asleepTicks` with the reference's rule (F5.4's "recently moved");
  - `BodySimGpu::stepBegin` / `stepPoll` record the ticks and the read-back in one submit that the frame never waits
    on, bit-equal to `step()` + `read()`;
  - the lab takes `{"solver":"gpu"|"cpu"}`. GPU is the default; the CPU reference stays as the control;
  - live poses arrive one frame behind the solve, and the rest policy runs once per read;
  - a bake reloads the GPU solver with the edited world and the survivors (warm start dropped);
  - the per-body cull tests each body's grid box against the camera frustum and each cascade's light frustum, then
    draws runs of consecutive kept bodies (`{"cull":bool}`, a cost knob, default on).
- **Headless (red first):**
  - `asleepTicks` matches the reference rule every tick;
  - the async step is bit-equal (0 of 58,697 samples differ);
  - GPU + rest keeps the ledger exact after every pass, with reloads of 7-17 ms;
  - the cull is conservative (0 of 60,316 boxes with a point in view culled).
  - Suites: GPU 44/44, CPU 122/122.
- **Live** (Release; two worlds: the lab's forest project, with 43 ms frames before any body, and the flat
  CharacterTestbed at 5.2 ms):
  - both rigs pile and sleep, 0 below the floor, the ledger exact at every sample, and two runs bit-equal;
  - the solver's CPU is 0.003 ms per frame (median), and real time holds in the flat world (59.5-60.8 ticks per
    second);
  - the cull changes 0 px in the viewport.
- **Found on the way:**
  - the large wall could not run in the lab (the preview's 256-voxel limit), so the pad is now clamped per axis;
  - a status call stalled the main thread for more than 20 s at about 1,000 bodies (the CPU penetration measure), now
    optional;
  - two pixel-gate errors of mine: wind running, then the editor's FPS text. The gate now holds the clocks and
    compares the viewport.
- **MISSED:**
  - the wall's penetration at rest: 102 mm, one pair of pieces asleep 0.8 voxel inside each other. The CPU reference
    peaks at 9-37 mm on the same rig. Owed: an overlapping pair must not sleep;
  - real time in the forest (25-37 ticks per second), because of that world's frame;
  - the body pass does not follow faces: 89-93 % of the faces culled saves 0-23 % of 0.4-1.0 ms. The cost is the
    covered pixels, so F5.3's "22 ns per face" was one pose's number. The casters save nothing here (the wall lies
    inside both cascades);
  - CPU cull 0.10-0.13 ms (≤ 0.1);
  - the bake frame: 64-105 ms of CPU (preview rebuild plus GPU reload), the F5.5c levers;
  - the "1,025 awake" live rung never happened: the lab's wall has about 105 of 994 awake after one second.

Next in F5.5: F5.5c (the bake levers, an incremental preview edit and an incremental GPU edit instead of the reload),
then F5.5d. Owed: the overlapping pair asleep (with F5.2's hard contact), the broadphase.

**F5.5c, the bake levers (2026-11-19).** F5.5b's bake frame was 64-105 ms against the 4 ms hitch proposal.
- **What it builds:**
  - the other-body cell test asked once per cell (a box test first, a sphere grid per pass for the near bodies and the
    wake, the resample's claims in the plan's box, each voxel's U nearest candidates). Plans are exact:
    `BakeParams::cellSet`, with F5.4's test kept as the A/B;
  - a per-pass cap (`RestParams::cap`, a tier knob);
  - the lab's world kept as a `RegionEdit` on the engine's 32 m chunk grid (`areaRegionChunks`), with each baked body
    written through `RegionEdit::applyCells`, F3's exact incremental rebuild (`{"preview":"region"|"area"}`);
  - `BodySimGpu::edit`: the changed cells and the survivors' records, staged and recorded by the next command buffer,
    bit-equal to a reload (`{"gpu_edit":bool}`);
  - the renderer keeps one body-face copy per frame in flight (F5.3 waited for the whole device on every change).
- **Headless (red first):**
  - plans exact: 0 differences over 916 + 914 bakes and 676 blast plans;
  - the cap holds every pass;
  - the region's faces equal a full rebuild at every check (0 mismatches);
  - the GPU edit is bit-equal to a reload (0 of 40,068 samples).
- **Live** (CharacterTestbed, the large wall's 994 bodies, cap 4; the GPU was shared with World of Warcraft
  throughout). The whole rest call is 0.94-1.17 ms median and 1.87-1.90 ms max, against 56-73 ms median and 77-113 ms
  max for F5.5b's path on the same engine. The ledger is exact at every sample and the GPU verify finds 0 mismatched
  chunks.
- **Found on the way:**
  - the waited GPU edit cost about 14 ms behind the frame;
  - F5.3's face upload waited for the device on every bake;
  - `localhost` costs about 2 s per API call on this machine.
- **MISSED:**
  - plan cost 19.6 / 121 ms (≤ 10 / 40; was 37.5 / 665);
  - the CPU reference's worst pass 5.7 ms: its warm-start re-key, while the rest of the pass is ≤ 0.3 ms;
  - region vs area preview: 131 / 24 / 20 px above ±1, single pixels on silhouettes and the wall-floor line, with no
    line along a chunk plane;
  - the backlog drained 596 of 930 in 30 s in the last run (7 ticks per second on the shared GPU; earlier runs drained
    all of it);
  - frame p99 99 ms (≤ 25; the control was 80). One unexplained ~60 s interval at a run's second frame, in both paths.

Next in F5.5: F5.5d (the exit, live 100 → 1,000, conservation, pop, whole frame). Owed: the overlapping pair asleep
(with F5.2's hard contact), the broadphase, the edge pixels between the two preview paths.

**F5.5d, the exit live (2026-11-19).** The F5 row's exit test, run on the shipped path at three rungs: the lab's large
wall at E/T 250 / 650 / 2800, which gives 110 / 282 / 994 bodies live. Conditions: CharacterTestbed, Release, cap 4,
budget 64; World of Warcraft shared the GPU through every run.
- **What it builds (lab only):**
  - a ledger check after every rest call (`{"ledger_every":true}`), with its red: a planted wrong reference counts as
    bad;
  - `{"wake_all":true}`: every body awake on the pile, pushed through `BodySimGpu::edit`;
  - solver poll diagnostics, timing for every rest call, and the time of the run's largest frame;
  - `tools/voxfound_f55d_gate.py` (ladder | seq | rung | wake | pop | control).
- **Conservation, exact:** 0 mismatches in 4,892 checks after every rest call across the rungs. 0 bodies below the
  floor, 0 overwritten, 0 refused. The backlog reaches the budget in 3.7 / 11.3 / 31-33 s. The GPU verify finds 0
  mismatched chunks.
- **Within budget at 1,000:**
  - CPU per frame: 0.016 ms, the sum of medians;
  - the hitch: 1.73 ms max, every pass incremental.
- **Over budget at 1,000, each with a lever left:**
  - **Every body awake on the pile:** 13.5 ms per tick (7.7 at 110 bodies, 9.5 at 282; 0.2-0.26 ms all asleep). The
    cost does not follow the body count, and these numbers don't explain it. The next increment profiles it before
    any lever.
  - **Draw at the peak:** 1.45-1.75 ms (main + both casters), and the whole GPU frame +1.3 to +2.1 ms over the same
    pile baked into the world. After the backlog it is 0.05 ms (once 0.38).
  - **Real time:** 45-57 ticks per second at 1,000. A step is found done 36-47 ms after its submit and carries at most
    4 ticks.
- **Pop at 1,000:** near (6 m) 39 % of the box. Far 0.003 × near at 96 m, 4× the pile's size. The predicted far, 24 m,
  measured 0.50 × near **(MISSED)**: the 32 m wall still fills the frame there.
- **MISSED:**
  - the X2 ranges (every rung) and real time at 1,000;
  - the solver path's p95 (2.5-2.8 ms, the frames that record a step);
  - the draw at the peak (≤ 1.3);
  - the whole-frame control: with the rig cleared, the testbed's foliage shows where the lab's floor covered it
    (10.0 vs 2.7 ms), so the baked pile replaces it as the control.
- **Found and fixed:**
  - **F5.5c's "unexplained ~60 s interval":** every lab request without `"pen":false` ran the CPU penetration measure
    on the main thread, the gates' own `run` toggles included (0.7 s at 110 bodies, 60 s at 994). It now runs only on
    a status request. Re-run: largest interval 99 ms.
  - **`as_world`** left the hidden bodies casting shadows.
  - **My gate error:** `gpu_timing` is a history, so run 1's draw pairs mixed modes. The gate now waits out the window.
- **Verdict:** over the proposal with levers left, so not the F5 stop condition. F5 stays open.

Next in F5.5: F5.5e, the awake pile's tick (profile first, then the broadphase). Owed: the overlapping pair asleep (with
F5.2's hard contact), the ticks-per-step cap / a step that does not wait behind the frames, the edge pixels between the
two preview paths, the plan cost, the CPU reference's remove.

**F5.5e, the awake pile's tick, profiled (2026-11-20).** A measure, no lever. F5.5d's 7.7 / 9.5 / 13.5 ms per tick
with every body awake did not follow the body count; four candidates were written down with the measure that separates
each.
- **What it builds:**
  - a headless test, `F55eAwakePileProfile`, on the lab's grid built the lab's way (floor of voxels, pad, air). It
    settles the pile until every body sleeps, then wakes every body through `edit()` from one snapshot per condition;
  - the lab's `{"profile":bool}`: the per-pass profile on the synchronous ticks, plus candidates, capped and pairs in
    the status;
  - `tools/voxfound_f55e_gate.py`.
- **Red:** a wake on the CPU only leaves the GPU's pile asleep. Through `edit()`, every body is awake after the tick.
- **The pile is the cost (H1):** headless costs 8.4 / 10.3 / 15.0 ms, 1.1× the live X2. The same grid **falling**
  costs 2.4 / 3.8 / 6.2 ms.
  - A settled pile makes 4× the contact candidates: about 150 per body at every rung, and the cap drops 80 % of them.
  - Live today, without WoW: 7.9 / 9.8 / 14.1 ms, so F5.5d's numbers were not WoW's.
- **Not the cost:**
  - one tick per submit (H2): 0.97-1.04× the 30-tick submit;
  - the frame sharing the GPU (H3): live / headless 0.81-1.14 per group;
  - the path choice (H4): after `edit()` the automatic path is per-colour from the first tick.
- **The profile at 994:** world 4.7, pairs 2.7, hard_apply 2.4, primal 2.1, hard_body 1.7, sort 1.1 ms.
  - The world test leads at every rung.
  - Pairs + hard_body are 17 / 18 / 28 %, so **the broadphase is not the next lever**.
  - World, hard_apply, sort and primal are 77 / 77 / 67 %.
- **Why the count does not matter:** the world group grows 1.7× from 110 to 994 bodies while its summed work grows 10×.
  - A follow-up, stated as one after the profile, woke only the tenth of the bodies with the most corners: 6.0 / 6.5
    / 10.4 ms, 63-71 % of every body awake, from a quarter of the corners. The other nine tenths alone cost 7.7 / 10.0
    / 12.6 ms.
  - The costs do not add. Each pass waits for its busiest bodies, because a body's work stays in one workgroup or one
    thread: the world items and the cap, the constraint sort, the primal, the hard world point.
- **MISSED:**
  - E3's leader share: the world test at 110 is 49.1 % (≥ 50), and at 994 the pairs do not lead;
  - E4: the forced fused path is +19 / +59 % (≤ 10 %);
  - E6 as written: the leader follows no summed measure.
- **Conditions:** WoW was not running. Headless run 1 shared the GPU with another session's engine. Run 2 was
  discarded: a contention burst of 246 ms per tick, source not found.

Next in F5.5: F5.5f, the busiest body's work on a settled pile. First split the world group's time between the items
and the cap, then choose its lever, with its own prediction. Owed: the overlapping pair asleep (with F5.2's hard
contact), the ticks-per-step cap / a step that does not wait behind the frames, the edge pixels between the two
preview paths, the plan cost, the CPU reference's remove; the broadphase (not the leader, F5.5e).

**F5.5f, the world group split (2026-11-20).** A measure, no lever. The world group is one dispatch: per awake body,
one workgroup counts its items, writes the candidates, then caps them to 8.
- **The instrument:** `BodySimGpu::setWorldSplit`. With the profile on, four diagnostic copies of the pass run before
  the real one. Each stops after one more stage (count, write, bucket winners, spread picks) and is timed as its own
  group. The copies write only scratch, which the real pass rewrites, and they touch no counters.
  - **Bit-equal:** 60 ticks with the split on vs off, 0 bodies differ and every counter is equal.
  - **Red:** the loud copy shows in the counters while the poses stay equal.
  - The lab takes `{"world_split":true}`; the gate takes `--split`.
- **The items are the cost: 96-98 % of the world group, headless and live.**
  - The count is 48 %, and the write's increment is another 48-50 %. The write re-runs every item's test to place its
    candidates, so every item is tested twice per tick.
  - The busiest body has 2,900-3,300 items (corners + world-box cells) on one 64-thread workgroup. That is about 50
    escape searches per thread, twice.
  - HARD_APPLY tests the same items a third time, at the post-solve pose (1.4 / 1.9 / 2.5 ms).
- **The cap is not the cost:** bucket winners 1-3 %, spread picks ≤ 1 %, emit ≤ 2 %, although it drops 80 % of the
  candidates.
- **The busiest body's world candidates:** 8.8 / 12.7 / 33× the mean (11-33 per awake body). F5.5e's "~150 per body"
  counted the body pairs too.
- **MISSED:**
  - F2: the items' share (45-70 % predicted);
  - F3: the bucket winners (20-45 %);
  - F4: half the bodies over the cap (14-30 %).
- **Suites:** GPU 47/47, CPU 125/125.

**F5.5g, each world item tested once per pass (2026-11-20).** The lever F5.5f pointed at.
- **The lever:** `BodySimGpu::setWorldOnce` (default on). Each world-pass thread stages its candidates (`kStage` = 48,
  a region past the bodies' scratch) while it tests its items, then copies them to its serial offset after the scan:
  the same slots, the same order, the same drops. A thread with more than `kStage` re-tests its own items (counted:
  `worldRedo`). The lab takes `{"world_once":bool}`; the gate takes `--once on|off`.
- **Bit-equal:** 60 ticks once vs twice, 0 bodies differ and every counter is equal at every rung. Red: a test-only
  scramble (the copy in reversed order) moves 110 / 276 / 954 bodies. The parallel-vs-serial world test still 0.
- **The predicted `kStage` = 16 MISSED:** the world group -27 / -32 / -27 % (35-50). Fallback threads were only
  0.09-0.30 % of the slots, but they sit on the busiest bodies, which set the pass's time. **48:** 0 fallback, the
  world group -33 / -38 / -41 % headless, -36 / -38 / -37 % live. Cost: 27 MB of stage at 110 bodies, 126 MB at
  >= 512.
- **The tick MISSED:** -8 % headless (10-20 predicted), -12 / +5 / 0 % live (>= 8). The world group is about a third
  of the tick, and the busiest body's ~3,000 items still sit on one 64-thread workgroup.
- **Conditions:** another session's engine held the GPU at 94-100 % throughout; ticks ran ~2x F5.5e's clean values.
  Five GPU suite tests fail device-time guards only, and fail the same with the lever off (the control): to re-run on
  a quiet GPU.

**F5.5h, a big body's world items over more than one workgroup (2026-11-20).**
- **The lever:** `BodySimGpu::setWorldSpread` (default on). The world pass is three dispatches:
  - units: each awake body's chunks, min(16, ceil(items / 256)), with a prefix;
  - chunks: one workgroup per chunk tests its items once and writes them in order into the body's own scratch region
    at the chunk's slot;
  - finish: per body, the chunks are moved left in place to the serial layout, then capped and emitted.
  A chunk that doesn't fit flags its body, which runs F5.5g's pass instead (`spreadFallback`). The lab takes
  `{"world_spread":bool}`; the gate takes `--spread on|off`.
- **Bit-equal:** 60 ticks on vs off, 0 bodies differ and every counter is equal, at every rung, in three runs. Red: a
  scramble (each chunk reversed) moves 110 / 276 / 954 bodies. The serial world pass vs the spread path: 0 samples
  differ. 0 fallback bodies, headless and live.
- **Cost: NOT measured clean.** The GPU ran ~25-40x slow, with another session's engine running; F5.5g's untouched
  pass took 160-240 ms per world group against 4.3-6.6 ms an hour before. Contended ratios: world group -66 to -75 %
  headless and -60 to -73 % live, tick -38 to -43 % headless and -31 to -36 % live. These are MISSED high against
  30-60 % and 5-15 %. They are what a time-sliced GPU does to one long workgroup, not the clean effect.
- Five suite tests fail device-time guards only (the same five as F5.5g, where the lever-off control failed them
  too). The CPU suite's one failure is a wall-time guard (3.36 vs 3.0 ms).

**F5.5i, the re-profile on a quiet GPU -- and F5.5h's regression found and fixed (2026-11-21).**
- **F5.5h's cost record was wrong.** It blamed a contended GPU. The control, run first on an idle GPU, showed F5.5h's
  shader made the WHOLE shared pipeline 25-40x slow: F5.5g's untouched pass took 79-118 ms, against 2.6-3.6 ms with
  F5.5g's committed shaders swapped in. The cause was worldFinish's `Cand t[4]` (320 B per thread, live across a
  barrier). Every pass pays the pipeline's register budget.
- **The fix:** a chunk moves 64 items per round with one Cand per thread. Bit-equal; the scramble still red; 0 fallback.
- **Measured clean** (quiet suite 49/49, the five device-time guards pass again; live with no other engine):
  - world group, spread vs one group: -45 / -51 / -65 % headless;
  - world group, F5.5e -> now: 2.75 / 3.68 / 4.73 -> 1.04 / 1.28 / 1.39 ms, from ~1/3 of the awake tick to under 1/10;
  - live awake tick vs F5.5e's: 6.0 / 7.1 / 10.8 vs 7.9 / 9.8 / 14.1 ms (-24 to -28 %).
- **Not shown:** a second, older regression. The bisect that suggested one ran beside another engine; it is void.
- **What's left**, four groups of similar size, live: HARD_APPLY 21-26 % (the third test of the same world items; it
  leads or is second at every rung), pairs 16-23 % (leads at 994), primal 16-22 %, hard_body 7-13 %.
- **Rule from it:** all passes share one pipeline's register budget, so a shader change is first measured on an
  UNTOUCHED pass (the control).

**F5.5j, HARD_APPLY's world items over more than one workgroup (2026-11-21).**
- **What "not tested a third time" could mean:** HARD_APPLY tests at the post-solve, pushed pose, so the world pass's
  results cannot be reused bit-equal, and a cull by a motion bound is not provably safe (the escape searches and the
  fast-corner sweep are not 1-Lipschitz in the pose). So the third test stays, but it leaves the busiest body's single
  workgroup, F5.5h's way. A safe cull stays open, proof first.
- **The lever:** `BodySimGpu::setHardSpread` (default on). Three dispatches replace pass 25: push + chunk counts at
  the pushed pose (one group), each chunk's deepest point (pass 25's function with a run-time flag), then per body the
  chunks reduced in order (first on ties). The lab takes `{"hard_spread":bool}`; the gate takes `--hard-spread`.
- **Bit-equal:** 0 bodies differ and every counter is equal at every rung, on the woken pile and on the pile sunk 4 cm
  (866-40,571 hard pushes). Red: a stub knob (J3 fails), a chunk-0-only scramble (110 / 274 / 937 bodies).
- **Measured on a quiet GPU:** hard_apply -54 / -66 / -67 % headless (MISSED high at 282/994 against 40-65), -53 / -64
  / -63 % live; the tick -11 / -17 / -16 % headless, -13 / -18 / -14 % live (MISSED high at 282). Untouched passes
  within 5 % (the control). Suite 50/50.
- **Live round 1 was void for the tick:** in its "on" runs the groups the lever cannot touch grew too (primal +60 %),
  with the engine's frame 5.8 -> 8.8 ms. Round 2 is the record.
- **Found on the way:** the driver's pipeline compile. A separate chunk pass (a fourth inlined `worldItem`) did not finish
  compiling in 10 min; the world chunk loop with a run-time mode took > 5 min. An uncached compile of F5.5i's shader
  takes 57 s, the shipped form 59 s. A shader change now also times its compile.
- **What's left at 994, live:** pairs 2.5-2.6 ms (26 %), primal 2.0, hard_body 1.5-1.8, world 1.0-1.1, sort 1.0,
  hard_apply 0.9. At 110 and 282 the primal leads.

**F5.5k, the pairs group split (2026-11-21).** A measure, no lever (F5.5f's way). The group is BROAD (one thread per
awake body against every body) and NARROW (one workgroup per pair: every corner of both bodies escape-tested to count,
then again to write, then the cap and the emit).
- **The instrument:** `BodySimGpu::setPairsSplit`. With the profile on, BROAD gets its own group (`p_broad`), and three
  quiet diagnostic copies of NARROW stop after the count, the write and the cap (`p_count` / `p_write` / `p_cap`); the
  copies process every pair, write only scratch and touch no counter. Bit-equal (60 ticks, 0 bodies differ, every
  counter equal); red: the loud copy's candidates differ. Overhead 0.99 x. The lab takes `{"pairs_split":bool}`; the
  gate takes `--pairs-split`. Uncached driver compile 60.6 s.
- **The corners tested twice are the cost:** count 38-43 %, write 39-46 %, together 77-89 % of the group (headless),
  live at 994 36 + 37 %.
- **BROAD grows with scale:** 5 / 12 / 19 % (0.05 / 0.13 / 0.47 ms), 20 % live at 994 -- its loop is awake x bodies.
- **The cap and the emit are small:** 4-6 % (MISSED low at 282 / 994 against 5-20).
- **Suite 51/51** on a quiet GPU.

**F5.5l, each pair corner tested once (2026-11-21).** The lever F5.5k pointed at.
- **The lever:** `BodySimGpu::setPairsOnce` (default on). NARROW's count stages each thread's candidates (F5.5g's
  per-thread stage; the world pass is done with it) and copies them to the serial offset after the prefix: the same
  slots, order and drops. A thread past the stage re-tests its corners (counted: `pairRedo`, 0 on these piles). The lab
  takes `{"pairs_once":bool}`; the gate takes `--pairs-once`.
- **Bit-equal:** 0 bodies differ and every counter is equal at every rung. Red: a stub knob (L3 fails), the scramble
  (110 / 274 / 946 bodies).
- **Measured:** the pairs group -43 / -41 / -38 % headless, -38 / -40 / -37 % live; the tick -7 / -9 / -9 % headless,
  -6 / -7 / -9 % live. The copy costs 2-3 % of the group. Untouched passes within 4 %. Uncached driver compile 69 s.
- **Live round 1** carried two doubled timed ticks with normal profiles, so a second round ran; it is the record.
- **Suite:** 51/52 as first run -- `F55bGpuRestConservesAndReloads`'s one-sample reload guard at 21.3 ms against 20
  (an upload path this change does not touch; 19.7 in F5.5k's suite, 16.7-18.2 alone); the re-run 52/52. The guard's
  margin inside the suite is owed.
- **Corrected after the commit:** the watchers missed another session's test process (one overlapped the last 13 s of
  the re-run; earlier overlap unknown). A quiet green re-run confirmed L3 (-39 / -39 / -36 %) and L4 (-7 / -9 / -9 %).
  A third suite was contended (other tests, then WoW); its quiet part failed the reload guard again (21.0 ms), owed as
  an alternated measure. F5.5k's split test now pins the two-test pass (its `cp > cnt` was thin in the once mode). A
  quiet suite is owed before the next lever.
- **The owed measures, done:** a quiet suite 52/52 (the reload 18.4 ms). The reload guard A/B (the lever off vs on,
  alternated, 8 each, alone): median 16.67 vs 16.86 ms, +0.19 -- F5.5l did not move it. Inside the suite the reload
  reads 1.5-3 ms above alone on either side of the lever; the guard stays as set.
- **What leads live now:** the primal at every rung (1.4-1.8 ms); at 994 the pairs group 1.6-1.7 (BROAD about 0.5 of
  it) and hard_body 1.4-1.5 follow.

**F5.5m, the primal split (2026-11-21).** A measure, no lever.
- **The instrument:** `BodySimGpu::setPrimalSplit` -- with the profile on, one iteration's primal as four quiet copies
  in the tick's form (q_floor / q_sum / q_red / q_solve); the write is the rest. Bit-equal at every rung; red: the loud
  copy changes every body, a stub records nothing. The lab takes `{"primal_split":bool}`; the gate `--primal-split`.
- **Measured (one iteration, headless 110 / 282 / 994):** sums 35 / 39 / 41 %, tree 39 / 30 / 29 %, solve 19 / 19 /
  23 %, the dispatch floor 1 / 9 / 8 %, the write <= 5 %. Live agrees (sums 32-41, tree 31-43, solve 11-24 %).
- **MISSED:** the floor hypothesis (>= 40 % predicted: the 32 colour slots cost ~0.5 us each); the solve at 110 (19 vs
  20-45 %); hard_apply +5.4 % at 994 (untouched).
- **Suite:** 53/53 quiet. Uncached driver compile 22 s.

**F5.5n, the lean primal -- REJECTED (2026-11-21).** The symmetric 21 + 6 terms and a one-step column reduction
instead of the tree: deterministic and both forms equal, but **slower** (the primal group +14 / +16 / +23 %; +5 / +21 /
+20 % with the triangle's index made constant) and F5.5a4's D2 at 2800 went to 29.8 mm (the tree: 8.5). Not dug into,
by the time-box. Committed once for the record (2cca1aaf) and reverted; the tree stays.

**F5 CLOSED (2026-11-21, the owner's decision).** F5 is closed at today's cost, not at its proposal: the owner chose
broad strokes first -- move on to F6-F8 and take scaling issues as they bite.
- **Works:** extraction, the GPU solver (live, async, bit-equal where stated), one-transform drawing, the rest/bake-in
  hybrid; conservation exact at every check; nothing through the floor.
- **MISSED, recorded:** the awake pile at ~1,000 bodies costs ~8.1-8.8 ms per tick against the 2 ms proposal (from 109
  ms at F5.5a); the primal leads at every rung (1.4-1.9 ms), then pairs, hard_body, sort.
- **Open, carried as known issues (not blocking):** an overlapping pair asleep 102 mm deep; the bake's plan cost;
  region-vs-area edge pixels; the uncached pipeline compile (~20-70 s per shader change); item 8 (direction culling);
  the CPU reference's remove; the ticks-per-step cap.
- **Levers measured and left** (if bodies become the bottleneck): the primal's sums and tree (60-80 % of it, F5.5m),
  BROAD (~0.5 ms at 994), the dispatch floor (<= 10 %).

## 9e. F6: MatSim on the world grid

**The approach (the owner, 2026-11-21): broad strokes first, then fine-tune.** Each step has its prediction before the
code and its red, but no lever ladders: a scaling problem is taken when it bites. Steps (the plan's section 5):
- **F6.1, MatSim on the 1/8 m lattice.** The cell sizes become 1, 1/2, 1/4, 1/8 m (a lattice parameter; 1/9 stays the
  default until F7 moves the consumers). Fill units nest by 8. Occupancy comes from a VoxFound `VoxelGrid` (1/8 m
  voxels). MatSim's invariants re-run there: exact ledger, exact remap, determinism, sleep = dense, repose, a U-tube
  levels, watertight walls, the GPU bit-equal to the CPU.
- **F6.2, the fill channel in the world.** Liquid and powder live in world voxels (`VoxelGrid` + the store get a fill
  channel); MatSim steps the awake blocks of the world, not a separate box; the world's edits wake it.
- **F6.3, crumbs are powder.** F4/F5's crumbs enter the simulation as powder voxels: break a wall, the rubble slides
  into piles; conservation exact from the wall through the bodies and the crumbs to the piles.
- **F6.4, the particles write back.** MatSim's fast-motion particles (the hybrid) settle into world voxels.
- **F6.5, live.** The lab in the engine: a wall breaks, rubble piles, water pours and pools against the pile, seen and
  measured (L4). Exit: MatSim's tests pass on world voxels; the live scene runs.

**F6.1, MatSim on the 1/8 m lattice (2026-11-21).** `MatSim::Config::lattice` = 9 (the default: the old lab,
unchanged) or 8 (cells of 1, 1/2, 1/4, 1/8 m; FULL 131,072 / 16,384 / 2,048 / 256 -- nesting by 8). Occupancy from a
VoxFound grid through `VoxFound::matSimOccupancy` (outside the grid = unknown = refused). On 1/8 m-voxel worlds: the
ledger exact every tick at every size and material, the remap 1-2-4-8-4-2-1 exact and bottom-up, determinism, sleep =
dense, a one-voxel diagonal wall watertight, sand at repose (0.676 vs 0.675), a U-tube level within 5 mm, and the GPU
bit-equal to the CPU with no GPU change. The old lattice's 33 CPU + 10 GPU tests pass unchanged. Red: a stub, all 7 fail.
Evidence `voxfound_f6_prediction.txt`, `voxfound_f6_headless.txt`.

**F6.2, the fill channel in the world (2026-11-21, CPU reference).** `VoxelGrid::fill` holds liquid and powder in open
voxels (one MatSim cell each); `VoxFound::WorldFluid` steps it in a box around the fill (+ 2 blocks), growing it before
fill reaches a face, writing back after every call; `wallsChanged()` / `setFill()` take edits. **The box is invisible:**
equal, voxel for voxel, to one box over the whole grid (dam break with 4 grows, a mixed scene, a wall broken mid-run);
the ledger exact; a settled world evaluates nothing. Found on the way: MatSim's head field is state carried across
ticks, so a re-made box ran differently (equal with pressure off) -- the growth now carries the pointers and head words
over. Owed: a box holding trapped contents does not grow; the GPU path and the store's codec come with F6.5.

**F6.3, crumbs are powder (2026-11-21, CPU reference).** MatSim gets a rubble powder per world material (id 64 + m,
45 deg for all -- one angle, broad strokes); `VoxFound::crumbsToPowder` turns F5.1's crumbs into one full rubble cell
each (all or nothing). On hand-built stone and brick walls broken at E/T 650 and 2800: every material conserved exactly
(wall = solid + bodies + rubble) at every check, the rubble slides from ~25 voxels up to ~3-4 and rests by tick
121-280, none over air.

**F6.4, the particles write back (2026-11-21, CPU reference).** `WorldFluid::Params::hybrid` runs MatSim's hybrid
frame (wake, PBF step, settle, cell tick) over the world box; the box grows for particles too and the ledger counts
them. A water column released in a 6 m channel at 1/8 m: 1,266 cells became particles and all settled back into world
voxels by 7 s, the ledger exact every tick, none outside the box, the water level at exactly the volume level.

**F6.5, live (2026-11-21).** The bodies lab turns a broken rig's crumbs into rubble in its world (`{"rubble":bool}`,
default on), steps it with WorldFluid at 60 Hz while running, takes `{"pour":...}` (water), and draws fill by a rule of
world position (at least half a cell = its material; water drawn as Glass, a stand-in) through the region preview's
incremental edit. Live in CharacterTestbed: the ledger exact at every rest call (bakes over fill included), drawn =
the rule (0 mismatched voxels; the red, the edit off, 63), the rubble piles at the wall's base and rests in ~3 s.
**MISSED:** poured water spreads to a ~1 cm film on the open floor that the rule does not draw, resting at ~27 s; cost
-- 5.7 ms p95 while rubble moves, 4.1 ms per frame once quiet (the whole box scanned + ticked), 25-250 ms on the large
wall (the CPU reference over 536 k cells). **F6 is done in broad strokes**; the cost is the scaling issue to take when
it bites (the GPU WorldFluid; skipping quiet boxes), and a contained pour + drawing thin water are owed.

## 9f. F7: moving the engine over

The plan's section 7 order, in broad strokes (the owner, 2026-11-21): F7.1 the read interfaces on the new store, F7.2
generators writing it, F7.3 drawing behind a switch, F7.4 editing, F7.5 destruction, F7.6 saving / LOD / undo / the
voxel API, F7.7 deleting the old cube / subcube / microcube paths. Predictions: `voxfound_f7_prediction.txt`.

**F7.1, the read interfaces on the new store (2026-11-21).** `VoxFound::WorldReads` answers `getVoxelTypeAt`,
`hasVoxelAt`, `subVoxelFloor` and `occupiedMicro` from BlockStores (one per engine chunk; one 8^3 block = one old cube)
with the old semantics on 1/8 m voxels (WorldReads.h); `ChunkManager::setVoxFoundReads` routes the four to it, the
consumers unchanged; `NPCManager::buildNavGrid` takes its bounds from the routed chunks. Equal to the brute-force rule on
every cube and micro cell; NPCs path through a framed door of new-store voxels with no old chunks loaded (the wall
without a door blocks). MISSED narrowly: typeAt 50-52 ns (<= 50). Not yet: queryAABB (physics), stateAtMicro /
phxOccupancySolid (lighting), the other chunkMap walkers.

**F7.2, the generators write the new store (2026-11-21).** `VoxFound::WorldStore`: a BlockStore per engine chunk + one
palette, with bulk writes -- `fillBox` (covered blocks as one entry), `paint`, `paintCanvas` (the realizer's 1/8 m
canvas; 1/9 m refused), `fromRegion` (terrain + flora through F2's `forEachRegionChunk`) -- and `reads()` for F7.1.
Exact against dense references across chunk borders; the hall house realized at 1/8 m painted cell for cell across a
chunk border; NPCs walk between its rooms on the new store (sealed doors block); generated surface chunks voxel for
voxel. Owed: part ids, carving a building's interior from terrain, the settlement / furniture / item placers.

**F7.3, drawing behind a switch (2026-11-21).** `voxfound_preview {"world_switch":bool}`: on, today's world is hidden,
the editable region drawn, and ChunkManager's reads routed to the region's own stores (`RegionEdit::chunkStore`) --
what is drawn is what the engine reads. On the F2.19 gate scene: the switch draws exactly F2.19's B (0 px at 5 poses);
the section 4.3 gate today vs new 46-68 % px, F2.19's numbers, every residual already attributed to content; all 4,096
sampled reads equal the region (142 differ unrouted -- the red); NPC navigation builds on the switched region (a
229-node path). OWED: the cost (D5) and a timing guard's re-run, on a quiet GPU (another session's tests held it).
Not moved: physics and light occupancy, the other chunkMap walkers.

**F7.4, editing on the switched world (2026-11-22).** `IWorldEditor` + `VoxelInteractionSystem::setWorldEditor`: with
the switch on, the player's hover is the region's pick and break / place are its brushes (cube / subcube / microcube =
8 / 2 / 1 voxels; placing in front of the hit face; the material provider's material), through F3's edit path. Live:
break and place a cube, a subcube, a micro -- every edit verified equal to a full rebuild, the engine's reads follow,
0.44-0.68 ms per action. **Found:** an edited hole renders black -- the ambient probe field still traces today's hidden
chunks (section 4.4's occupancy from block masks is owed; it bites as soon as the switched world is edited).

**F7.3b, the light occupancy from the new store (2026-11-22, section 4.4).** `VoxFound::lightOccupancyOf` builds the
lighting pool's chunk blob from a BlockStore (cube solid / empty from its block; mixed cubes' micro bits sampled as the
engine's reads sample them); `RenderCoordinator::setVoxFoundOccupancy` feeds the switched region's chunks (per-chunk
edit revisions) instead of today's. Found first: with today's chunks feeding it, almost the whole switched ground was
black in ambient (today's 1/9 m ground sits above the 1/8 m one). Now: equal to the reads' sampling on every micro cell;
ground ambient 1.1 -> 33.5, an edited hole 55.9; GI off 0 px changed; the switch off restores today's pool.
LightingPipeline.md R8 + section 9 updated. **Cost (measured after, quiet):** the switch itself costs nothing on the GPU
(the A/B with today's occupancy: every pose within noise -- F7.3's D5 PASS); the new occupancy costs +5.9 / +7.2 / +9.8
ms at the three ground poses (GI on; the probe field now traces the drawn ground) -- the price of the right ambient,
not predicted; levers left if it bites (the switch is not the default yet).

**F7.5, destruction on the switched world (2026-11-22).** `DamageSystem::setDestructionOverride`: with the switch on,
blasts / spells / chops run the bodies lab's "world" rig on a window of the region -- F4.1 fracture, F5.1 extraction,
BodySimGpu bodies, F6.3 rubble, F5.4 bake -- every change written into the region incrementally. Live: a blast (E/T 400)
makes 318 pieces and 362 crumbs, the pieces fall back into their crater and bake back; ledger exact at every check, the
drawn world = the simulated one, today's chunks untouched. Found and fixed: a bake over 32 bodies took a full-rebuild
branch that world mode must not use. Not yet: part ids, bodies in the light occupancy, soil as its own material.

**F7.6a, saving the switched world (2026-11-22).** `save_world` also writes the switched region's edited chunks (F3.3's
codec v3); switching on loads them (`{"load_saved":false}` skips). An edited switched world (a brush, a blast and its
bake) comes back after a relaunch with every chunk's store hash equal. MISSED: every chunk differing from the generator
is rewritten per save (31 chunks, 208 ms), and loading the whole saved region at the switch takes ~0.7 s. Found: the
gate scene's world.db holds F3.3's saved test edits -- comparisons with F2.19 must pass `load_saved:false`.

**F7.6b, undo / redo on the switched world (2026-11-22).** `Core::VoxFound::EditHistory`: per edit the box's materials
before and after; undo / redo write back only the differing voxels through `RegionEdit::applyCells`. Every switched
region edit records; `/api/undo` and `/api/redo` use it first. Live: 32/32 chunk hashes exact after undo and redo, 20
steps deep exact at every step, the cap and redo-clearing as today's stack, ~0.3 ms per undo (an edit's cost). Blasts
are not recorded (stated).

**F7.6c, the voxel API on the switched world (2026-11-22).** `place_voxel`, `remove_voxel`, `place_subcube`,
`fill_region` (hollow, replace) and `clear_region` route to the region edit path, JSON unchanged: metres are cubes = the
size-8 brush, a subcube the size-2 brush, `"units":"voxel"` (+ `"size"`) the 1/8 m grid; `GET /api/world/voxel` answers
from the region (solid voxels of the block, its material). Live: exact voxel counts (512 / 24,576 / 22,528 / the
replace:false air count to the voxel), every call one undo entry back to the start hashes. Not routed: the batch place,
copy / paste / named snapshots.

**F7.6d, LOD tiers 2/3 on the switched world (2026-11-22).** Editable regions now build coarse levels 1..3 (the default
variant); every edit marks the chunks whose 1 m-margin area it meets, refreshed one per frame and spliced into the
levels; `assemble` keeps the drawn LOD selection instead of dropping to level 0. Live: refresh = build exactly
(interior), the edit visible at level 3, refreshed chunks exact after edits and undo, the selection survives. MISSED:
~200 ms per chunk refresh (levers: build the area on the device, or refresh only the edited block columns); levels
4-7 and the leaf variants stay refused with editable.

**F7.8, the physics occupancy on the switched world (2026-11-22, section 7 step 1's owed part).**
`VoxFound::storeBoxes` answers queryAABB from a BlockStore (a uniform block = one 1 m box, mixed blocks = z-runs);
`VoxelOccupancyGrid::setExternalQuery` + `VoxelDynamicsWorld::setGridOverride` put the region's chunks over today's while
switched. Live: overlapsTerrain = the region at 4,000 / 4,000 probes (red 3,799); a character rests on a dug pit's floor
and on a placed pillar's top, and falls when the pillar is cleared under it. GPU debris still reads today's pool.

**F7.9a, the generation rate (2026-11-22; section 3.5a's sprint budget, F1's unmeasured stop condition).** Needed for a
default switch: a switched world larger than one region must stream. `buildTerrainArea` looks a material up once per
(metre column, surface metre, metre) instead of per voxel; `forEachRegionChunk` builds chunk areas, their cut and border
on worker threads (a generator copy each), in order. Exact (unit + all 32 gate hashes). 1.4 -> 9.6 chunks/s at 4
workers (the gate request 26.5 -> 6.1 s); more workers are slower (allocation). MISSED: >= 10 narrowly, the 15/s sprint
budget (levers: buffer reuse, no part/fixed channels for terrain, a flora template cache, GPU generation).

**F7.9b, the switched world streams (2026-11-22).** `{"stream":true}`: when the camera leaves the region's inner chunks,
the region is rebuilt around it -- edited chunks kept in memory (codec v3) and loaded back through F7.6a's exact path;
save_world writes them. Edits survive shifts exactly; a 256 m walk stays on the world. Found: `flora_window:"bounded"`
makes a chunk's trees depend on the region box -- streaming requires `"open"`. A shift is a 4.5-7.2 s hitch (the whole
region regenerated): next, keep the staying chunks and build the entering ones in the background.

**F7.9c, background incremental shifts (2026-11-22).** `{"stream_bg":true}` (default): only the entering slab is
generated (4 threads, off the main thread), one entering chunk is prepared per frame, and one frame swaps it in
(`RegionEdit::rebox` / `makeChunk` / `insertChunk`; staying chunks never regenerated; the axis recentred alone).
Exact against a fresh build with the same edit (32/32 chunks: voxels, faces, sprigs); a 256 m walk at sprint speed stays
on the world. MISSED narrowly: a 278 ms prepare slice (<= 250); coarse LOD levels are dropped by a shift.

**F7.7a, what still uses the old paths (2026-11-22, measured).** 55 engine/editor files (394 sites) name `Subcube` /
`Microcube`; 15 files walk `chunkMap`. Deleting needs the writers moved first: template placement, structures /
settlements, the water span grid, the outliner, the editor's residency hash. Order: the placers (F7.10+), then the
walkers, then the deletion.

**F7.10, templates on the switched world (2026-11-22).** A static `spawn_template` paints the template's 1/8 m version
(`resources/templates_g8`) into the region as one undoable edit (`voxFoundPaintCells`, the shared placer path):
exact cells (147 / 147, 325 / 325), rotation, snap to the region's ground, undo exact. Buildings are not seated yet.

**F7.11, structures on the switched world (2026-11-22).** `StructureBuildService::Deps::canvasSink`: StructureForge
realizes the shell on the 1/8 m grid and, after validate_realized, the editor's sink seats it on the region's ground
(tree matter is not ground), excavates the box above the floor, clears tree matter above the footprint and paints it as
one undoable edit. Live: a hall house, 121,826 cells exact, a character stands on its floor, undo exact. Found: region
trees are voxels the old vegetation gate never sees. Not yet: furnish / emit (furniture, items, lights, nav), settlements.

**F7.12, settlement houses on the switched world (2026-11-22).** `SettlementBuildService::Deps::canvasSink` is forwarded to
every house: a 2 x 2 village's four houses land in the region (exact, standable, one undo each). Found: a success
response with an empty "error" key made the settlement re-roll every lot. Not yet: the settlement's own ground works
(clearing, streets, paving, fences, wells, pads) still write today's chunks.

**F7.13, the old-grid writers through a hook (2026-11-22).** `ChunkManager::setVoxFoundWrite` / `setVoxFoundClearBoxes`
(set while switched): `StructureGenerator::place` / `removeVoxels` and `ChunkManager::clearChunkCellsBulk` (the
settlement's five clear loops) write the region; thirds / ninths become 1/8 m voxels by centre sampling
(`VoxFound::placementVoxels`, an exact partition). Live: today's world untouched by the village (0 / 315 vs 50). Found:
this settlement mode lays no streets; the clearing exceeds the 16 M undo limit; the clearing cost 35 s -> 2 s as one
masked edit; the orphaned-canopy sweep is a 37 s frame on the switched world (next lever).

**F7.14, tree matter from the switched world (2026-11-22).** `ChunkManager::setVoxFoundTreeCell`: DamageSystem's tree
checks ask the region (a Leaf* / Log* voxel in the cube's block) instead of scanning today's chunks. The canopy sweep
now finds the region's orphans (a floating leaf cube swept) and the village job takes 6.6 s (41 s).

**F7.15, furnished buildings on the switched world (2026-11-22).** `{"furnish":true}`: the whole structure pipeline
runs with its voxel writes hooked -- now also ObjectTemplateManager's static stamps, PlacedObjectManager::clearRegion and
the forge's vegetation gate -- so furniture, clutter, item props, lights and nav come with it (the shell centre-sampled
from 1/9 m). Same 20-fixture plan as today's build, 19/20 fixtures in the region, today's world untouched. Found: five
writers into today's world (placed-object removal, the vegetation gate, ...). The default stays the exact 1/8 m shell.

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
