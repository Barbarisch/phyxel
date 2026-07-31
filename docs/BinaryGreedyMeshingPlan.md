# Binary Greedy Meshing for Subcube/Microcube Faces — Implementation Plan

> **Status: ✅ SUPERSEDED — THE WORK SHIPPED.** (Header corrected 2026-07-29; it read "PLANNED —
> not started" long after the fact.) Fine (subcube/microcube) greedy merging was built and landed as
> **Increments 1–4a**, tracked in [`RenderOptimization.md`](RenderOptimization.md)`:139-315`, and has
> been **default ON since 2026-07-07** — `ChunkRenderManager::s_fineGreedyMerge = true`
> (`ChunkRenderManager.cpp:62`); per-face path still reachable via
> `POST /api/debug/fine_merge {"enabled":false}` for A/B. Measured (Release, hash-verified binary,
> `docs/evidence/inc5_heavy_scene_fps_release.txt`): 9 taverns **639,585 → 53,219 faces (12.0×),
> 41.5 → 206.5 FPS**; 16 taverns **1,126,856 → 92,438 (12.2×), 26.4 → 180.4 FPS** — ~5–8× recovery on
> face-bound scenes. Re-mesh cost +~11% (~10 ms/chunk) on an already-stuttering rebuild path.
>
> **So the "#1 known issue" this plan was written to attack is substantially retired.** Two defects
> the merge introduced or exposed remain OPEN and are the live follow-ons:
> **T-junction cracks at merge borders** (`RenderOptimization.md:489`) and cross-cube **microcube**
> merge (Increment 4b, parked). The crack defect is now shared with the LOD-seam problem — see
> [`ContinuousLodPlan.md`](ContinuousLodPlan.md) §2.5, which proposes solving both with one skirt
> mechanism.
>
> **Original status (2026-07-02): PLANNED — not started.** This is item #1 of
> [`EngineAdvancesResearch.md`](EngineAdvancesResearch.md) and the reopening of the **parked
> Phase 2** of [`RenderOptimization.md`](RenderOptimization.md) (attempted 2026-06-28, reverted,
> post-mortem preserved in commit `4c3182d`). It attacks the project's **#1 known issue**: one
> furnished v2 tavern = **412,298 visible faces → ~49 FPS** (empty world: 80 faces, 357 FPS)
> because subcube/microcube faces are emitted one instance per face, unmerged
> (`RenderOptimization.md:26-33`).
>
> **⚠️ Coordination:** engine sources are under concurrent edit in other sessions (at plan-writing
> time the working tree was on branch `structure-gen-placement-defect-fixes` with an untracked
> `external/bullet3/`). Before starting: `git status` / ask the user which files are in flight, and
> do this work on its own branch. **Files this plan will touch:**
> - `shaders/static_voxel.vert`, `shaders/shadow.vert`, `shaders/debug_voxel.vert`
>   (+ recompiled `.spv` via `.\build_shaders.bat`)
> - `engine/src/graphics/ChunkRenderManager.cpp`, `engine/include/graphics/ChunkRenderManager.h`
> - `engine/include/core/Types.h` (`InstanceDataUtils` packers; the `InstanceData` struct itself
>   ONLY if the non-recommended Option B is chosen — then also `engine/src/core/Types.cpp` and
>   `engine/include/vulkan/VulkanDevice.h`, see §4.1)
> - a new unit-test file under `tests/`
> - `docs/RenderOptimization.md` (record results)
>
> **Known overlap:** [`ShaderMathRedundancyPlan.md`](ShaderMathRedundancyPlan.md) also edits
> `static_voxel.vert` (its Increment 1 rewrites the `gl_Position` line). Different lines, same
> file — whichever plan lands second must rebase and re-run `build_shaders.bat`.
>
> **Rollback:** Phase 2 was already reverted once. Every increment below is independently
> buildable, independently revertible, and gated behind a static toggle
> (`s_fineGreedyMerge`, same pattern as `s_smoothLighting` at `ChunkRenderManager.h:44`)
> so a live A/B comparison and an instant behavioral revert are always available.

## 1. Background

### 1.1 The problem (measured)

| scene | visible faces | FPS | source |
|-------|--:|--:|--------|
| empty flat world (1 chunk) | 80 | 357 | `RenderOptimization.md:29` (DEBUG build) |
| + one furnished v2 tavern | 412,298 | 49 | `RenderOptimization.md:30` |
| tavern after Phase 1 hidden-face culling | 55,068 (isolated contribution) | — | `RenderOptimization.md:51`, commit `4c3182d` |
| 20-building settlement + paths | 3,424,612 | ~15 | `RenderOptimization.md:14` |

Cube faces are already greedy-merged (commit `e916d1e`, "~6x fewer faces") — bare terrain is only
7,892 faces (`RenderOptimization.md:13`). Sub/micro faces are **not** merged: every visible
subcube/microcube face is its own 24-byte instance. Measurement source:
`GET /api/render/stats` → `total_visible_faces` (`RenderOptimization.md:17-19`), exposed as the
`get_render_stats` MCP tool.

### 1.2 What already shipped (Phase 1)

Commit `4c3182d` (2026-06-28) added **hidden-face culling** for sub/micro faces:
`buildSubMicroOccupancy()` builds `m_subOcc`/`m_microOcc` hash sets once per rebuild
(`ChunkRenderManager.cpp:684-716`), and the sub/micro builders skip faces whose neighbour cell at
their own resolution is provably solid (`rebuildSubcubeFaces` at `:772-788`,
`rebuildMicrocubeFaces` at `:930-948`). Result: 412,298 → 55,068 faces (7.5×). Chunk-boundary
neighbours are treated as exposed (conservative). This plan **keeps** that culling logic (as the
correctness oracle and the fallback path) and adds *merging* on top.

### 1.3 The reverted Phase 2 attempt — forensics

What is known (the code was **never committed** — `git log --all -S rebuildFineFacesMerged` hits
only the doc text in `4c3182d`; the five stashes are old unrelated GitHub-Desktop stashes; no
branch carries it):

> "Wider `InstanceData.mergeData` + `static_voxel.vert` scaleLevel==3 branch +
> `rebuildFineFacesMerged` greedy mesher. Merged quads rendered the magenta fallback texture
> (per-face path was correct; index delivery & geometry verified fine; forcing a valid index in
> C++ AND in-shader still magenta → frag-side, scaleLevel==3-specific, likely the world-space UV ×
> texture-array sample). Backed out to keep the tree green." — `RenderOptimization.md:55-60`

The exact root cause is **unrecoverable** from history. Ranked hypotheses, grounded in the current
code, that the diagnostic increment (Increment 1) must discriminate between:

1. **Stale SPIR-V.** CLAUDE.md warns `glslc` does not track `#include` dependencies —
   `voxel.frag` must be manually recompiled. If the "in-shader force" was edited but the `.spv`
   the engine loaded was stale, the force silently never applied, which exactly matches the
   observed "forcing a valid index in-shader still magenta". Cheapest to rule out; check first.
2. **Dual-struct / attribute-offset mismatch.** `InstanceData` exists TWICE — `Phyxel::InstanceData`
   (`engine/include/core/Types.h:82-96`) and `Vulkan::InstanceData`
   (`engine/include/vulkan/VulkanDevice.h:44-`), with a "MUST also be mirrored" warning at
   `Types.h:89-91`. Pipelines pull attribute descriptions from the Vulkan one
   (`RenderPipeline.cpp:29-30, 481-482, 1062-1063, 1628-1629, 1722-1723`;
   `ShadowMap.cpp:249-250`). A "wider mergeData" field added to one struct but not the other (or
   not to every pipeline's attribute list) shifts `textureIndex`/`flags` fetch offsets → garbage
   index → the fragment fallback path (`voxel.frag:96-98`: `layer >= count` → placeholder layer =
   the magenta checkerboard).
3. **NaN/degenerate UV in the new branch.** `voxel.frag` samples with **explicit gradients**
   (`textureGrad`, `voxel.frag:124-125`); a scaleLevel==3 branch that left `uv` (or the
   `dFdx/dFdy` inputs on the `varied` path) undefined/NaN yields undefined sampling.

Design consequence adopted by this plan: **do not introduce a scaleLevel==3 path and do not widen
the struct** (both were load-bearing in the failed attempt). Reuse the existing scaleLevel 1/2
decode paths and store extents in bits that are provably zero today (§4.1), so that *unmodified
data renders byte-identically* and the change is incrementally testable.

## 2. Ground truth — the current encoding and mesher (read before editing; line numbers drift)

### 2.1 `InstanceData` — 24 bytes, defined twice

`Phyxel::InstanceData` (`engine/include/core/Types.h:82-96`, comment "Total: 24 bytes" at `:92`)
and mirror `Vulkan::InstanceData` (`engine/include/vulkan/VulkanDevice.h:44-51`), 7 vertex
attributes (`Types.cpp:72`, `VulkanDevice.h:62`):

| field | size | contents | source |
|---|---|---|---|
| `packedData` | u32 | bits 0-4 X, 5-9 Y, 10-14 Z (chunk-local cube pos); 15-17 faceID; 18-19 scaleLevel (0 cube / 1 sub / 2 micro / 3 reserved); 20-25 + 26-31: **cube** → merged extents (sizeU-1, sizeV-1, `packCubeFaceDataSized` `Types.h:278-289`); **sub/micro** → 3×3×3-encoded grid positions (`Types.h:233-240`) | `Types.h`, `static_voxel.vert:56-72` |
| `textureIndex` | u16 | bit 15 = resolution class (512/1024 array), bits 0-14 = layer | `voxel.frag:93-94` |
| `reserved` | u16 | bit0 emissive, bit1 transparent, bits2-9 alpha, bit10 mirror, bits11-14 damage, bit15 varied — **all 16 bits booked** | `Types.h:85`, `ChunkRenderManager.cpp:225-228` |
| `light` | u32 | bits 0-15 = 4 per-corner skylight nibbles. **Bits 16-31 never read** (shader mask `static_voxel.vert:337`) | |
| `light2` | u32 | bits 0-23 = corner0/1 block RGB. **Bits 24-31 never read** (`static_voxel.vert:341-343`) | |
| `light3` | u32 | bits 0-23 = corner2/3 block RGB. **Bits 24-31 never read** | |
| `tint` | u32 | bits 0-23 tint RGB, bits 24-31 per-voxel state | `Types.h:89`, `static_voxel.vert:320-326` |

Note: `VoxelRenderPipelines.md:76` still says "20 bytes" — stale since the tint word was added
(commit `ed76534` / `a32a970`); the struct comment at `Types.h:92` ("Total: 24 bytes") is the
truth. `RenderOptimization.md` still says "8 B" in places — twice stale.

**Key free-bit finding (verified against the shader, 2026-07-02):** `light` bits 16-31 (16 bits)
and `light2`/`light3` bits 24-31 (8+8 bits) are written as zero by every current producer
(`rebuildCubeFaces` packs sky into bits 0-15 at `ChunkRenderManager.cpp:613-624`; the sub/micro
builders write `skyV * 0x1111` and `rgb12 | rgb12<<12` at `:869-879`) and are masked off by every
consumer (`static_voxel.vert:337,341-343`; `shadow.vert` reads no light words at all;
`debug_voxel.vert` — verify, it decodes only `packedData` at `:33-40`). **32 spare bits exist
without widening the struct.**

### 2.2 Subdivision factors (from code, not guessed)

- Subcube = 1/3 cube per axis (`static_voxel.vert:149`), 3×3×3 per cube (`encodeGrid3x3x3`,
  `Types.h:243`). Chunk-wide subcube grid = **96³** (`ChunkRenderManager.cpp:771`).
- Microcube = 1/9 cube per axis (`static_voxel.vert:164`), 3×3×3 per subcube. Chunk-wide
  microcube grid = **288³** (`ChunkRenderManager.cpp:928-929`).
- UV per cell: subcube face = 1/3 of the parent-cube texture tile (`static_voxel.vert:247`),
  microcube = 1/9 (`:272`), positioned by face-dependent flip tables (`:252-264` sub,
  `:276-308` micro).

### 2.3 How the cube greedy merge works today (the pattern to match)

`rebuildCubeFaces` (`ChunkRenderManager.cpp:164-679`): per face direction, per 32-slice, build a
32×32 visible-face mask, then rectangle-merge width-first/height-second (`:629-658`).
**Merge key** = `(textureIndex << 16) | reserved-with-damage` (`:563-565`) **plus** identical
packed light words **plus** the face must be light-uniform (all 4 corners equal within
`s_mergeTolerance`) — non-uniform AO/gradient faces stay 1×1 (`:636-642`). Extents are stored as
size-1 in `packedData` bits 20-31 (`packCubeFaceDataSized`); the shader scales the quad along the
face's u/v axes (`static_voxel.vert:135-145`) and sets `uv = baseUV * vec2(sizeU, sizeV)`
(`:239-243`) — the sampler's REPEAT wrap tiles the texture across the rectangle (each material is
a full layer of a `sampler2DArray`, so wrapping is safe — no atlas bleed). The u/v axis mapping
per face (`:541-543` in the mesher = `:138-143` in the shader) is load-bearing; `shadow.vert:64-71`
replicates it ("MUST match static_voxel.vert or shadow casters collapse to 1x1").

### 2.4 What the sub/micro path emits today

One `InstanceData` per visible face, no merging: `rebuildSubcubeFaces`
(`ChunkRenderManager.cpp:741-885`), `rebuildMicrocubeFaces` (`:887-1043`). Their light is **flat
by construction** — a single value sampled at the *parent cube's* neighbour air cell, replicated
to all 4 corners (`:869-879`) — so unlike cube faces, lighting never varies *within* one parent
cube's fine faces, and the "uniform corners" merge precondition is automatically satisfied.
Per-face `tint` (with state in the high byte) is per-voxel (`:847`), and flaming/smoldering state
swaps `textureIndex` to the ember surface (`:843`) — both must be in the merge key.

## 3. The technique — binary greedy meshing (research digest, fetched 2026-07-02)

- **[cgerikj/binary-greedy-meshing](https://github.com/cgerikj/binary-greedy-meshing)** (v2):
  occupancy as a 64×64 array of `uint64_t` columns (1 bit per voxel); face culling via shift/AND
  produces per-direction visible-face masks ("cull 64 faces at a time", 62×62 mask arrays — 64³
  chunk *including* 1-voxel neighbour padding → 62³ usable); merging combines runs of set bits
  ("merge 64 faces at a time"), keyed by voxel type ("original voxel types are looked up to check
  whether two faces can be merged"). Measured: **50-200 µs per chunk, avg 74 µs single-threaded /
  108 µs pooled on a Ryzen 3800X**. Constraint: **v2 dropped baked AO** (v1.0.0 branch has it) —
  attribute-per-face data fights the pure-bitmask formulation; implementations handle attributes
  by *keying* merges on them (splitting runs), exactly like Phyxel's cube path already does.
- **[Vercidium/voxel-mesh-generation](https://github.com/Vercidium/voxel-mesh-generation)**:
  run-based merging on 32³ chunks; claims **"~20% more triangles than greedy meshing and runs
  ~390% faster"** (this is the origin of the "~3.9× faster / ~20% more tris" line in
  `EngineAdvancesResearch.md:13-14`); 0.527 ms/chunk avg on a Ryzen 5 1600.
- **[Exile meshing writeup](https://thenumb.at/Voxel-Meshing-in-Exile/)**: 8-byte packed vertices,
  one instanced 4-vertex triangle strip per face (Phyxel's static path is the same shape: one
  instance per quad, `VoxelRenderPipelines.md:69-93`); merged quads span up to whole chunks with
  UVs that "signal texture repetition across combined faces" — i.e. UV > 1 + wrap, the same
  mechanism Phyxel's merged cube path uses.

**Fit to Phyxel:** chunk = 32³ cubes → a cube column is one `uint32_t`; a subcube column (96 cells)
is 2×`uint64_t`; a microcube column (288 cells) is 5×`uint64_t` (4.5 rounded up). Dense per-chunk
occupancy masks (derived: grid³/8 bytes): cube 4 KB, subcube 96³/8 = **110,592 B ≈ 108 KiB**,
microcube 288³/8 = **2,985,984 B ≈ 2.9 MiB**. The 108 KiB sub mask is trivially a reused scratch
member (the established pattern for ~190 KB scratch: `ChunkRenderManager.h:217-222`, commit
`6ee51a1`). The 2.9 MiB micro mask is affordable on the 64 GB target rig but its zero/build cost
is a **MEASURE DURING INCREMENT 4** item; Increment 2's within-cube formulation avoids dense
chunk-wide micro masks entirely.

## 4. The hard problems — options and recommendations

### 4.1 Encoding: merged fine quads need grid position AND extents

For sub/micro faces, `packedData` is fully booked (32/32 bits: position 15 + face 3 + scale 2 +
two 6-bit grid codes), and — critically — the grid codes ARE the quad's origin at fine resolution,
so **the origin needs no new bits**. Only the extents (sizeU-1, sizeV-1) need a home.

| option | mechanics | cost | risk |
|---|---|---|---|
| **A (recommended): repack spare lighting bits** | Store `(sizeU-1)` in `light` bits 16-23 and `(sizeV-1)` in bits 24-31 (8+8 bits → max extent 256 cells; a 288-cell micro run splits into 256+32 — one extra quad, negligible). §2.1 proves these bits are written 0 and read never, today. | Zero bytes. Instance buffer stays 24 B: 412,298 × 24 B ≈ 9.9 MB worst-case (derived); post-cull tavern 55,068 × 24 B ≈ 1.3 MB. Changes: sub/micro packers + 3 shaders. `shadow.vert` must add the `location = 4` input (currently declares only 0-2, `shadow.vert:3-5`; the pipeline already supplies all 7 attributes via `ShadowMap.cpp:249-250`, so declaring it is legal). | Lowest. Existing/unmerged instances have zeros there → decode as 1×1 → **byte-identical output for all current data**. Both `InstanceData` definitions untouched → hypothesis-2 failure class impossible. |
| B: widen struct 24→28 B (`uint32 mergeData`) | New attribute (7→8) | +4 B/instance: 412k faces → 11.5 MB (+1.65 MB, derived). Must update BOTH structs (`Types.h:82` + `VulkanDevice.h:44`), BOTH attribute-description functions, and every pipeline that consumes them (5 sites in `RenderPipeline.cpp` + `ShadowMap.cpp:249` + any reflection/mirror/transparent variants), plus 3 shaders. | This is the failed Phase 2 shape; the dual-struct mismatch surface is exactly where hypothesis 2 lives. Reject unless A's 8-bit extents ever prove insufficient (they don't: max needed is 9 bits only for a full 288-run, handled by splitting). |
| C: second instance stream/pipeline for merged fine quads | New vertex format + pipeline permutations (main, shadow, reflection, mirror, transparent) | Most code, 2 draws/chunk | Highest; also re-derives everything the static path gets for free. Reject. |

**Recommendation: A.** Guard rails: add `static_assert(sizeof(InstanceData) == 24)` beside both
definitions, and a comment in both light-word fields claiming bits 16-31/24-31 for merge extents.

### 4.2 Sub-tile UV correctness across a merged quad

A merged run of fine cells spans multiple sub-tiles of the parent texture. Two sound approaches:

1. **Linear UV + sampler wrap (full generality).** The UV of a fine face is
   `cellOriginUV + baseUV * cellScale` where `cellScale` = 1/3 or 1/9
   (`static_voxel.vert:267,309`). For a merged run, replace `baseUV * cellScale` with
   `baseUV * extent * cellScale`, slope **signed** per axis because the flip tables
   (`:252-264`, `:276-308`) make some faces' UV run opposite to the world axis (e.g. face 2 uses
   `2-subZ`). Runs crossing a parent-cube boundary continue past 1.0 (or below 0.0) and the
   REPEAT sampler wraps into the next tile — which is exactly correct because the neighbouring
   cube's texture restarts there, and it is exactly the mechanism the merged **cube** path already
   ships (`static_voxel.vert:240-243`). Mip gradients across the wrap are already handled:
   `voxel.frag` samples with explicit `textureGrad` (`voxel.frag:100-125`).
2. **Restrict merges to same-parent-cube runs (de-risked subset).** Max merge 3×3 (sub) or 9×9
   (micro) cells; UV stays inside [0,1] of one tile — no wrap, no sign games beyond the existing
   per-cell table. Caps the win at 9:1 (sub) / 81:1 (micro) per cube face, which is already the
   bulk of the reduction for furniture and per-cube-patterned walls.

**Recommendation: ship 2 first (Increments 2-3), extend to 1 (Increment 4) with pixel-diff
evidence.** This ordering exists because the UV wrap is precisely where the Phase 2 post-mortem
points ("world-space UV × texture-array sample") — prove the encoding + within-tile path green
before touching wraparound. Caveat for approach 1: the `varied` flag (hash-rotated tiles,
`voxel.frag:106-121`) recomputes UVs per-fragment from world position, so it is
merge-transparent — but it is also **cube-path-only today** (`voxel.frag:87`, "static cube path
only"); no fine-path work needed.

### 4.3 Per-corner lighting words

The cube path only merges faces whose 4 corners are identical (uniform) and whose packed light
words match exactly (`ChunkRenderManager.cpp:636-642`) — light differences **split merges**, no
re-derivation is attempted. **Match it.** For fine faces this is nearly free: their light is flat
per face and constant per (parent cube, direction) by construction (§2.4), so within-cube merges
(Increments 2-3) can key on the light words and will essentially never split; cross-cube runs
(Increment 4) split at cube-resolution light gradients, which is the *correct* visual behavior
(same as cube faces at light gradients). No lighting re-derivation anywhere in this plan.
Full merge key for fine faces: `(textureIndex, reserved, tint-word incl. state byte, light,
light2, light3)` — grounded in §2.4 (tint `:847`, state-swapped texture `:843`, reserved
`:854-855`).

### 4.4 Mesher cost at fine resolution

Grids per §2.2: 96³ (27× the cube cell count) and 288³ (729×). A naive dense sweep of 288³ is
23.9M cells (derived) — unacceptable per edit. Mitigations, in the order the increments adopt
them:

- **Sparsity first:** iterate only *occupied parents* (the chunk already stores explicit
  subcube/microcube vectors — `rebuildAllFaces` signature, `ChunkRenderManager.h:75-83` — and
  hash occupancy sets, §1.2). Within-cube meshing (Increments 2-3) touches only cubes that
  actually contain fine voxels: per parent cube per direction, a 9×9 (micro) or 3×3 (sub) plane
  mask per depth slice — each row fits in a u16, a whole 9×9 slice in two u64s. This is the
  binary-meshing idea applied at the natural sparse granularity, with zero dense chunk-wide
  allocation.
- **Dense bitmask columns only where they pay:** cross-cube runs (Increment 4) build
  per-direction slice masks from `m_subOcc`/`m_microOcc` (or directly from the voxel vectors) —
  subcube level dense (108 KiB scratch, reused member per `ChunkRenderManager.h:217-222`
  pattern); microcube level either dense (2.9 MiB scratch — MEASURE) or restricted to slices
  that contain micro voxels (a 288-entry dirty-slice bitset makes empty slices free).
- **Shift/AND culling** replaces the per-face `subCellSolid`/`microCellSolid` hash probes
  (`ChunkRenderManager.cpp:724-739`) in the merged path: visible = `occupied & ~(occupied
  shifted by 1 along the face normal)`, 64+ cells per op (cgerikj §3). The existing hash-probe
  path stays intact as the `s_fineGreedyMerge=false` fallback.
- Budget context: cube meshing already runs per edit within interactive budgets (destruction
  re-mesh batching, commit `d97d0c8`: 5.4 s → 113 ms for bulk edits). The fine mesher must not
  regress that: **MEASURE re-mesh ms during Increments 2-4** on the tavern chunk (log around
  `rebuildAllFaces`).

## 5. Increments

Each increment: buildable alone, verifiable alone, revertible alone (toggle + small diff).
Per CLAUDE.md, every one ends with the full runtime loop: `stop_engine` → `build_shaders.bat` (if
shaders touched; remember the `voxel.frag` manual-recompile caveat) → `build_project` →
`launch_engine` → scenario → evidence → `stop_engine`.

### Increment 0 — Baseline + red tests (no engine-code change)

1. Build the furnished-tavern scene (StructGenTest project per `structgen-test-project` memory;
   pristine-reset, then `POST /api/structure/build` — engine generator, per the provenance rule).
2. Record: `get_render_stats` (FPS, frame ms, `total_visible_faces`), **two fixed camera poses**
   captured via `get_camera` (one exterior, one interior showing furniture + wall textures +
   lighting gradients), `screenshot` + `get_visual_diagnostic` at each. Save poses in the results
   log — all later comparisons reuse them.
3. **Red unit test** (new file under `tests/`, near existing render-ish tests): feed
   `ChunkRenderManager::rebuildAllFaces` a flat one-cube-thick slab of N×N cubes fully filled
   with microcubes, same material; assert the +Y face-instance count ≤ N² (one merged quad per
   cube face is the Increment-2 bar; ≤ small-constant is the Increment-4 bar). Today it emits
   81·N² micro top faces (9×9 per cube, §2.2) → the assertion **must fail now**; commit it
   failing-but-skipped or with the bound documented, per the red-before-green discipline.
4. Also record a Release-build FPS datapoint if practical (`RenderOptimization.md:104-109`
   requires one before "holds up" claims). Otherwise mark: MEASURE DURING INCREMENT 5.

### Increment 1 — Encoding spike: one hand-forged merged quad (kills the Phase 2 mystery)

Smallest possible slice of the risky part — **no mesher yet**:

1. Shader side: in `static_voxel.vert`, `shadow.vert` (add the `location=4 inLight` input),
   and `debug_voxel.vert` (verify whether it needs extents; the cube merged path's precedent at
   `debug_voxel.vert:132` decides), decode `sizeU = ((inLight >> 16) & 0xFF) + 1`,
   `sizeV = ((inLight >> 24) & 0xFF) + 1` for scaleLevel 1/2, scale the quad along the existing
   per-face u/v axes, and set `uv = cellOriginUV + baseUV * extents * cellScale` with the signed
   slope from §4.2 (within-tile only for now).
2. C++ side: a temporary debug hook (behind the toggle) that replaces one known subcube face pair
   in the test chunk with a single 2×1-extent instance.
3. Verify at L4: the merged quad renders the two cells' correct sub-tiles, correct shadow, no
   magenta. **If magenta appears, run the §1.3 hypothesis ladder** (freshly compiled .spv first;
   then RenderDoc/`get_visual_diagnostic` on attribute fetch; then UV values). Do not proceed to
   Increment 2 until the single quad is pixel-correct.
4. Regression half: with the toggle off (all extent bits zero), pixel-compare both baseline poses
   — must be identical (the §4.1 backward-compat invariant, this is its falsifiable test).

### Increment 2 — Within-cube microcube merging (binary, sparse)

1. In `rebuildMicrocubeFaces` (or a sibling `rebuildMicrocubeFacesMerged` selected by the
   toggle): group micro faces by (parent cube, faceID, depth slice); build 9×9 bit plane masks;
   cull via shift/AND against the occupancy (still consulting `microCellSolid` for the
   cross-parent boundary cells); rectangle-merge runs keyed per §4.3; emit one instance per
   rectangle via a new `packMicrocubeFaceData` + extents-in-light packer (`Types.h`
   `InstanceDataUtils`).
2. Green the Increment-0 unit test at the ≤N² bar. Add the **coverage invariant** test: the
   merged rectangles exactly partition the per-face path's face set (same cells, no overlap, no
   loss) — comparing against the Phase-1 path output makes the old path the oracle.
3. Runtime: tavern face count via `get_render_stats` (expect a large drop — furniture and micro
   detail are 9×9-per-cube-face bounded; record actual), pixel-compare both poses (sub-tile
   texture pattern must be unchanged), FPS delta. MEASURE: re-mesh ms.

### Increment 3 — Within-cube subcube merging

Same shape as Increment 2 at the subcube level (3×3 plane masks per cube face-direction slice).
Unit test: flat slab of subcube-filled cubes → ≤ one quad per cube face. Runtime: tavern
(subcube walls dominate its geometry — `RenderOptimization.md:31`), same evidence set.

### Increment 4 — Cross-cube runs + full binary culling (the UV-wrap increment)

1. Extend merging across parent-cube boundaries per §4.2 approach 1 (UV continues past the tile,
   REPEAT wraps), extents capped at 256 (§4.1). Merge key comparisons now split at
   light/tint/material boundaries between cubes — assert that in the unit tests (a two-cube run
   with different tints must NOT merge).
2. Replace the hash-probe culling with dense bitmask shift/AND at subcube level (108 KiB scratch
   member); micro level dense-or-dirty-slice per §4.4 — MEASURE both variants' mesher time on
   the tavern chunk and keep the winner.
3. This increment owns the highest visual risk (texture wrap at cube seams). Evidence: close-up
   screenshots straddling a cube boundary on a long same-material subcube wall + micro path,
   compared against toggle-off captures of the same poses.

### Increment 5 — Stress, scale, and closing the loop (MANDATORY, per the stress-test rule)

Scaling axis = fine-face density; push it and assert invariants at every step:

- **Worst-case all-micro chunk:** a full 32×32 flat micro slab region (or the largest the build
  API tolerates) — assert face count collapses per the unit-test bounds, no holes
  (`scan_region`/visual), FPS + mesher ms recorded. Build it via engine placement APIs
  (`fill_region`/structure routes), not hand math.
- **Merge-defeating checkerboard:** alternating materials per micro cell over a large plate —
  merging must degrade gracefully to *exactly* the per-face path's count (no wrong merges, no
  extra instances) and remain visually identical to toggle-off. This is the honest bound: binary
  meshing buys nothing here by design (cgerikj §3 keys merges on type; ~20%-more-triangles-type
  tradeoffs only apply to *mergeable* content).
- **Chunk-boundary straddle:** the same tavern built straddling a chunk seam (the y=31→32 class
  of bug from the 10-story-tower lesson in CLAUDE.md) — cross-chunk faces are conservatively
  unculled/unmerged today; assert no seam artifacts.
- **The settlement scene:** re-run the 3.4M-face settlement (`RenderOptimization.md:9-19`
  recipe) — the ≥10× face-count drop + material render-ms drop is the shipped validation bar
  (`RenderOptimization.md:104-109`). Take the **Release-build** measurement here.
- Re-measure the empty world (357 FPS baseline) to prove no regression on merge-free scenes.
- **Solution-auditor pass before any "fixed/works" claim**, per the standing gate. Update
  `RenderOptimization.md` (retire the "PARKED" note, record numbers) and the stale struct-size
  mentions found in §2.1.

## 6. Verification summary (MANDATORY — per CLAUDE.md, none of this is optional)

1. **Baseline first** (Increment 0): faces/FPS/poses/screenshots on the real tavern via
   `get_render_stats` BEFORE any change.
2. **Red-before-green:** the Increment-0 unit test shown failing on the current per-face path;
   every increment's claim backed by a check that failed before it.
3. **Pixel comparison at fixed camera poses** after every increment (identical-or-within-noise on
   texture pattern, lighting gradients, shadows — shadows explicitly, since `shadow.vert` decodes
   the same bits).
4. **Toggle A/B:** `s_fineGreedyMerge` off must reproduce the pre-plan image byte-for-byte at any
   point in the sequence.
5. **Stress phase** (Increment 5) with invariants asserted at every step, Release datapoint
   included.
6. **Solution-auditor** before "done". A fix is not done until the engine runs it (`launch_engine`
   → scenario → evidence), not when it compiles.

## 7. Explicit non-goals

- **Cross-chunk sub/micro occlusion or merging** — boundary faces stay conservative-exposed
  (Phase 1's stated deferral, commit `4c3182d`). Runs stop at chunk borders.
- **Distance LOD** (micro→sub→cube collapse) — `RenderOptimization.md` plan item 3, separate.
- **GPU-driven rendering / compute culling** — `EngineAdvancesResearch.md` #2, sequenced after
  this.
- **Kinematic/dynamic pipelines** — furniture-as-kinematic (`KinematicFaceData`) and GPU debris
  emit all 6 faces unmerged by design (`VoxelRenderPipelines.md:15`); out of scope.
- **Lighting re-derivation at merged-quad corners** — merges split at light boundaries instead
  (§4.3), matching the shipped cube behavior.
- **Shader micro-optimizations** on this path — that is `ShaderMathRedundancyPlan.md`'s job.
- **Device-local instance memory / buffer-capacity shrinking** — see Appendix A; real but
  separate.

## Appendix A — Instance-buffer pooling (EngineAdvancesResearch.md item #7 answer)

**Chunk instance buffers are already reused across rebuilds, not freshly allocated.** Each chunk
owns a persistent `ChunkRenderBuffer`: created once, host-visible + persistently mapped
(`ChunkRenderBuffer.cpp:70-120`, mapping at `:113`); a rebuild just `memcpy`s the new face vector
into the existing mapping (`ChunkRenderManager::updateVulkanBuffer`,
`ChunkRenderManager.cpp:1045-1058`). Reallocation happens only when face count exceeds capacity,
growing to 1.5× required (`ChunkRenderBuffer.cpp:122-124`), with the use-after-remap hazard
already handled (pointer refetched after `ensureBufferCapacity`, `ChunkRenderManager.cpp:1050-1053`).
So the "vertex pooling" advice from item #7 is substantially already in place per-chunk. Two real
(out-of-scope) observations: (1) memory is HOST_VISIBLE|HOST_COHERENT (`ChunkRenderBuffer.cpp:103-104`),
not device-local — every frame's vertex fetch crosses PCIe/BAR; (2) capacity never shrinks and the
floor is `DEFAULT_BUFFER_CAPACITY = 25000` instances (`ChunkRenderBuffer.h:23`) × 24 B = 600 KB
per chunk even for near-empty chunks — after this plan slashes face counts, a shrink-on-rebuild or
smaller floor becomes cheap memory back. Both are candidates for a later pass, not this one.
