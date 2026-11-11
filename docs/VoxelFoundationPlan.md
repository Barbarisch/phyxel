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

Still open: §13.

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

### 3.6 Saving
- Codec v3: per chunk, the directory, then detailed blocks run-length encoded. Optional channels follow.
- Old worlds: a converter from PXB2. A cube converts exactly to one 8x8x8 block. Subcubes and microcubes don't land
  on the 1/8 m grid and are resampled (§7a). Generated worlds are regenerated instead, which is exact.

## 4. Drawing

### 4.1 Faces built on the GPU, per changed block
- A compute pass turns each changed block into faces. Hidden faces are dropped, and same-material faces are merged
  inside the block (8x8 slices).
- A second pass merges across block borders within a chunk where it pays.
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
- It is decided on data from the F4 harness, because it is expensive and not needed for the first look.

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
  - (c) **hybrid (recommended):** keep it as a body while it is near the player or recently moved; bake it into the
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
   water and physics then work unchanged. Tested by comparing old store vs new store on the same generated area.
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
(microcube, 1/9 m) does not, because 1/3 m is 2.67 voxels. Counted in the repo on 2026-11-10:

| Content | How much | Made by |
|---|---|---|
| Templates with subcube or microcube lines | 177 of 238 `.voxel` files | mostly generators (`gen_tree.py`, `regen_furniture.py`, `vox_import.py`, BlockSmith); the rest by hand, count not yet split |
| Fine-voxel items (`# grid: 81`) | 43 | `gen_items.py`, `vox_import.py --scale fine` |
| Structure generator laid out on thirds | 5 files: `StructureGenerator`, `StructureRealizer`, `StructureForge`, `StructureBuildService`, `SettlementBuildService` | code |
| Roof textures cropped to land courses on the 1/3 m grid | the roof family in `fetch_polyhaven.py` | tool |
| MatSim cell sizes 1, 1/3, 1/9 m | `validCellsPerM` and its tests | code |

The plan for each:
1. **Inventory first (in F0).** List every template as generator-made or hand-made, and list every place the code
   assumes 3 or 9 per metre (`grep` for the constants, plus the subcube/microcube APIs). Output: a table in this doc.
2. **Change the generators**, not their output. Each generator emits the 1/8 m grid (or 1/64 m for items), and the
   library is regenerated. This is exact by construction.
3. **Redesign features that relied on thirds.** For example, roof courses and trim on 1/3 m steps move to 1/4 m or
   3/8 m steps, and their texture crops are re-measured. Each one is reviewed by eye on screenshots, before and
   after.
4. **Resample hand-made templates** with a resampler that preserves volume per material (the count of voxels times
   voxel volume stays within one voxel per material), then review by eye. Any that look wrong are re-authored.
5. **Lint:** a check that fails if any template or generator still writes on a third or ninth grid.

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
| **F0** | Decide §13; write the two harness skeletons; the content inventory (§7a step 1) | — | — |
| **FC** | Content conversion (§7a), alongside F1-F3 | lint passes; structure validation suite green on the new grid; library screenshots reviewed | — |
| **F1** | New store (CPU reference, then GPU) + read interfaces; convert the generated area | memory ≥ 5x better on the town; every read-interface query equal | memory win < 5x |
| **F2** | GPU face building + drawing through today's lighting | pixel diff within the gate at 5 poses; frame time ≤ today | can't match the look, or slower |
| **F3** | Edits with brushes; codec v3 + converter | edit cost per block ≪ today's 2-13 ms chunk rebuild; save/load round-trips exactly | — |
| **F4** | Fracture core, CPU reference: bonds, patterns, pieces, anchoring, crumbs. Then the GPU port, equal to the reference | the fracture-lab rigs pass their predictions; conservation and determinism exact | the patterns don't read as real cracks to the owner (video review) |
| **F5** | Bodies: extraction, GPU simulation with voxel collision, drawing with one transform, the rest/bake-in hybrid | 100 → 1,000 bodies; conservation; pop measured | body cost at 1,000 above budget with no lever left |
| **F6** | MatSim on the world grid (liquids, powders, crumbs) | MatSim's tests on world voxels | — |
| **F7** | Move the consumers over (§7), default switched | full test suite green; L4 live checks | — |
| **F8** | Scale and stress: a whole town, 100 simultaneous breaks, 1,000 edit/break cycles, crossing chunk borders | invariants hold at every step | — |

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
7. **Content conversion** (§7a). 177 of 238 templates, 43 fine items and the structure generator's thirds-based
   layout must move to the 1/8 m grid. Mostly generator changes, but hand-made templates need resampling and review,
   and features designed on 1/3 m steps (roof courses, trim) need redesign. Its size is unknown until the F0
   inventory.

## 12. What stays as it is

- items as their own kinematic grids (their grid moves from 1/81 m to 1/64 m, §6.8);
- chunk size (32 m) and chunk coordinates;
- characters;
- far terrain and far-tree LOD (tiers 4-6, independent of chunks);
- the lighting model and its gates;
- the HTTP API's shape (coordinates change units; cube and subcube become brush sizes).

## 13. Open decisions for the owner

1. ~~Voxel size~~ **Decided 2026-11-10: 1/8 m** (D5). Rejected: 1/9 m, which keeps today's content exact but leaves
   blocks at 0.89 m and chunks at 28.4 m, off whole metres; and 1/9 m with 36-block chunks (32 m), which fixes the
   chunks only.
2. **Body rest policy:** the hybrid (§6.7 c), or always-bake / never-bake.
3. **Structural load in v1** (§6.6), or after the fracture lab's video review.
4. **Start with F1 (storage) and the fracture lab's CPU reference (F4) in parallel?** They don't touch each other,
   and the fracture model can be proven on a plain voxel grid before the new store exists.
