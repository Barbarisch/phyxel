# Render Optimization for Microcube-Dense Worlds

> Goal (user): lean into microcubes for **"better than Minecraft"** visual fidelity. Open question:
> does the engine hold up at very high microcube counts? This doc records the **measured** answer and
> the optimization plan. Ground every "holds up / too slow" claim in numbers (no vibes).

## Measured baseline (2026-06-27, DEBUG build, RTX 4090 target rig)

A terrain-aware settlement on Perlin hills (StructGenHills, seed 7), camera over the settlement:

| scene | visible chunks | **visible face instances** | frame (render) | FPS |
|-------|:--:|:--:|:--:|:--:|
| terrain only | 11 | **7,892** | — | — |
| + 20 subcube buildings + 21,745 path microcubes | 11 | **3,424,612** | 66 ms (59 ms render) | ~15 (STUTTER) |

The settlement adds **~3.4 MILLION face instances** over bare terrain's 7.9k. Placement is cheap
(~60 µs/microcube); **RENDER is the wall** (≈59 ms/frame). Instrumented via
`RenderCoordinator::RenderStats.totalVisibleFaces` → `GET /api/render/stats` `total_visible_faces`
(from `ChunkManager::ChunkStats.totalVisibleFaces`).

### Single-building datapoint (2026-06-28, DEBUG, post main-merge)

Even ONE furnished subcube/microcube building tanks FPS — measured on the flat StructGenTest world,
same camera:

| scene | visible faces | FPS |
|-------|:--:|:--:|
| empty flat world (1 chunk) | **80** | **357** |
| + one v2 `tavern` (16×7, 2-story, furnished) | **412,298** | **49** |

The tavern's ~65k subcube/microcube voxels become **412k visible faces** in a single chunk (sub/micro
faces are NOT greedy-merged). Confirms this is NOT a lighting-merge or build-perf-fix regression (empty
world is 357 FPS) — it is purely the un-merged sub/micro face count. **This caps build density until #40
(greedy-mesh sub/micro) lands.**

## ✅ Phase 1 shipped (2026-06-28) — sub/microcube hidden-face culling

Before greedy-merge, a more basic gap: subcubes & microcubes had **no face culling at all**
(`rebuildSubcubeFaces`/`rebuildMicrocubeFaces` hardcoded `faceVisible[6] = {true,…}`), so every
sub/microcube emitted all 6 faces even when fully buried (412k baseline = ~65k voxels × 6.3 faces).

**Fix** (`ChunkRenderManager`, uncommitted): a transient leaf-occupancy lookup (`m_subOcc`/`m_microOcc`
`unordered_set`s) built once per `rebuildAllFaces` straight from the voxel hierarchy, reusing the
already-built `m_solidVis` as the cube oracle. The sub/micro builders cull a face when the neighbour
cell at its own resolution (96³ subcube / 288³ microcube) is provably fully solid (coarser fills roll
up). Chunk-boundary neighbours treated as exposed (conservative). No shader/format/UV/winding change —
only buried faces omitted, so zero winding risk.

**Measured (DEBUG, fresh `RenderCullTest` world, remove-and-remeasure) — same v2 `tavern` (16×7,
2-story, furnished, 19 fixtures):** isolated face contribution **412,298 → 55,068 (7.5× fewer)**;
solid walls verified inside + out, textures intact. NOTE: verify on a FRESH world — a test DB with
repeated tavern rebuilds at the same spot produced misleading holey walls (stale/overlapping chunk
state), not a culling bug.

**Phase 2 (greedy-merge) — attempted, reverted, PARKED.** Wider `InstanceData.mergeData` +
`static_voxel.vert` scaleLevel==3 branch + `rebuildFineFacesMerged` greedy mesher. Merged quads
rendered the magenta fallback texture (per-face path was correct; index delivery & geometry verified
fine; forcing a valid index in C++ AND in-shader still magenta → frag-side, scaleLevel==3-specific,
likely the world-space UV × texture-array sample). Backed out to keep the tree green; redo from the
design below in a focused session.

## Baseline 2026-07-03 — shader-math + greedy-mesh campaigns (Increment 0)

Fresh capture for [`ShaderMathRedundancyPlan.md`](ShaderMathRedundancyPlan.md) +
[`BinaryGreedyMeshingPlan.md`](BinaryGreedyMeshingPlan.md). **DEBUG build @ commit `6e9f55e`
(main) + Phase 1 culling**, StructGenTest flat world (pristine reset), one v2 tavern built by the
engine generator: `POST /api/structure/build {"schema":"v2","typology":"tavern","function":"tavern",
"style":"medieval","footprint":[16,7],"stories":[{},{}],"position":{"x":8,"y":16,"z":8}}` —
log-verified: `auto-filled 2 story(ies) -> 6 rooms [typology tavern]`, validation OK, 20 fixtures
placed 0 skipped, sign hung. Structure AABB (8,17,8)–(23,27,14).

| scene | `total_visible_faces` | notes |
|-------|--:|-------|
| empty flat world | 14 | 138.7 FPS at initial camera |
| + v2 tavern | **51,258** | close to the documented post-Phase-1 55,068 (different seed/pos) |

**Fixed comparison poses** (MCP `set_camera` mode:"free" — see methodology warning below):

| pose | position | yaw/pitch | reference screenshot |
|------|----------|-----------|----------------------|
| A exterior NE | (34, 32, 29) | −135 / −25 | `screenshots/screenshot_20260703_084841_802.png` |
| B exterior W (sign + character visible) | (−6, 28, 28) | −37 / −16 | `screenshots/screenshot_20260703_084913_364.png` |
| C interior (furniture + micro walls) | (18.5, 20, 11.5) | −165 / −18 | `screenshots/screenshot_20260703_085015_966.png` |

**⚠ FPS methodology (hard-won, 2026-07-03):**
1. **`POST /api/camera` does NOT reliably hold a free-camera pose** — the camera can revert to
   player-follow (~(22.6,19.2,22.6)) within seconds, silently measuring the wrong view. The MCP
   `set_camera` tool holds indefinitely. Always **verify `GET /api/camera` position in the same
   breath as reading `GET /api/debug/engine_timing`**, or the number is meaningless.
2. **Restart-to-restart FPS variance is ±20% on the target rig** (same shaders, same DB world,
   same verified pose: 140.4 vs 113.6 FPS across two launches). Only **within-run** comparisons
   at verified poses are meaningful at the few-ms scale.
3. World state must be **loaded from the saved DB** (`POST /api/world/save` after building —
   chunk voxels are NOT auto-persisted by the structure build; a force-killed engine loses them.
   Rebuilding the same payload at the same position is deterministic: 51,258 faces every time).

**Shader-math A/B result (2026-07-03, `ShaderMathRedundancyPlan.md` executed):** old vs new
shaders (same binary, same DB world, verified poses): A 142.4→140.4 FPS, C 75.7→72.8 FPS —
**no measurable difference** (within noise). The NVIDIA driver on the RTX 4090 was already
hoisting the per-vertex uniform mat4×mat4 products (the plan's documented caveat). The changes
ship anyway: correct-by-construction, removes reliance on driver heroics, fixes the misdeclared
`static_voxel.vert` UBO block, and eliminates a real per-fragment matrix product in
`character.frag`. Visual identity verified at poses A/B/C by manual comparison (not a pixel-diff
tool) — after-screenshots: A = `screenshot_20260703_090250_111.png`, B = `_091328_493.png`,
C = `_090307_105.png`.

## Root cause — greedy meshing covers cubes but NOT subcubes/microcubes

The static chunk renderer (`ChunkRenderManager`) emits **one `InstanceData` (8 B) per visible face**,
drawn as a quad (`static_voxel.vert`, vertexID 0–3). Face culling removes occluded faces, but coplanar
same-material faces are merged **only for CUBE faces**:

- **Cube faces (scaleLevel 0):** the 8 B instance's bits 20–31 carry the **greedy-merged rectangle
  extents** (`sizeU`, `sizeV` as size-1), so a flat run of cubes is ONE big quad. (That is why bare
  terrain is only 7.9k faces.)
- **Subcube (scaleLevel 1) / microcube (scaleLevel 2) faces:** bits 20–31 instead carry the **grid
  position** (parent subcube + microcube, encoded 3×3×3) needed for the sub-tile UV. There are **no
  free bits for merge extents**, so every subcube/microcube face is its own instance — **no merging**.

So microcube-dense surfaces (path ribbons, subcube building walls) explode: the 3.4M faces are almost
entirely sub/micro faces (terrain cubes merged to 7.9k).

## The encoding constraint (why this is non-trivial)

`InstanceData` is 8 B: position (15 bits) + faceID (3) + scaleLevel (2) + bits 20–31 (12) which are
**already doubly-booked** — merge extents for cubes, grid position for sub/micro. To greedy-merge
sub/micro faces you must encode BOTH grid position AND merge extent → does not fit 8 B. Options:

1. **Wider instance format for merged sub/micro runs** (e.g., a second 4–8 B field or a separate
   instance stream) carrying extent; shader tiles the sub-tile UV across the merged quad.
2. **"Full-tile" UV mode for same-material micro runs** (e.g., paths): a flag that makes each microcube
   show a FULL texture tile (not 1/9), so a contiguous same-material run merges into one quad with a
   normal tiled UV — cheap and ideal for paving/large detail surfaces, at the cost of the sub-tile
   parent-texture look for those runs.
3. **Distance LOD:** collapse microcubes→subcubes→cubes beyond N metres (most micro detail is
   sub-pixel at distance anyway).

## Plan (ranked by leverage / risk)

1. **(highest leverage, medium risk) Greedy-merge subcube + microcube faces** of the same material per
   chunk per face-direction. Needs option 1 or 2 above (encoding). Expected: the 3.4M → tens of
   thousands for this settlement (walls/paths are mostly flat same-material runs).
2. **(cheap, low risk) Full-tile UV merge for same-material micro runs** (paths/large surfaces) — a
   subset of #1 that sidesteps the sub-tile UV problem; big win for paved paths + flat walls.
3. **(medium) Distance LOD** for far chunks.

## Validation (required before claiming "it holds up")

Red-before-green on the SAME settlement scene: record `total_visible_faces` + frame/render ms
**before** (3.4M / 59 ms — the red baseline above), implement, then **after** — assert face count drops
≥10× and render ms drops materially, with the village still visually correct (screenshot diff). Also
take a **Release-build** measurement (Debug Vulkan is much slower than Release; the 15 FPS figure is
Debug). Encode a unit/bench assertion on the meshing pass (a flat NxN same-material micro region →
O(1) merged quads, not N² faces) shown failing on the current per-face path first.
