# Render-Density FPS — Diagnosis & Fix Plan

> **Status: PLANNED.** Written 2026-07-08 on branch `render-offthread-mesh`, after the T0→B0→B1
> investigation established that render **density**, not meshing/chunk-update, is the dominant
> recurring frame cost (B0: ~99% of dense-scene "stutters" were the steady ~32 ms/frame cost of
> 377k faces ≈ 30 FPS on an RTX 4090). Companion to
> [`docs/RenderOptimization.md`](RenderOptimization.md) (the microcube greedy-merge / face-reduction
> track). Same discipline: **measure the wall before fixing it** (T0's lesson), red-before-green,
> evidence in `docs/evidence/`, solution-auditor on every "works" claim, each increment behind a
> toggle for live A/B.

## 1. The anomaly

377k visible instanced faces ≈ 750k triangles renders at ~30 FPS on a **4090** — a GPU that pushes
tens of millions of triangles/frame. So the scene is **not geometry-bound**; it is fill/overdraw- or
fragment-bound, and/or paying a hidden multiplier. `docs/RenderOptimization.md` already measured face
count as the FPS driver (639k→53k faces = 41→206 FPS; ~5–8× recovery when face-bound) but did not
measure *why each face is so expensive*. This plan finds and removes the per-face cost multipliers.

## 2. Ground truth (Explore audit + code read 2026-07-08 — cite before editing; lines drift)

### The load-bearing finding — a ~3× rasterization amplifier on EVERY face
Each visible face is one `InstanceData` (one `faceID`), but the draw is
`vkCmdDrawIndexed(36, faceCount)` (`RenderCoordinator.cpp:404`, `VulkanDevice.cpp:1832`) — **36
indices = a whole cube, 12 triangles** (`VulkanDevice.cpp:1074-1087`). The vertex shader positions
every vertex from the **per-instance `faceID`** + only `vertexID` bits 0-1 (`static_voxel.vert:
62-161`; `vertexID` is a per-vertex input, binding 0 = 8 corner ids). So all 36 indices collapse onto
the instance's single face quad: **~36 VS invocations for a 4-corner quad, and ~3× redundant
rasterization of the identical quad at equal depth** (12 tris; back-facing half culled → ~6
front-facing = 3× the 2 needed; early-Z can't help — same depth/plane). The index/vertex buffers date
from a per-*cube*-instance design (`VulkanDevice.cpp:1831` comment still says "36 indices per cube")
that became per-*face* without shrinking the draw. This multiplies a **heavy fragment shader**
(below) ~3× on every face, at every density. **Prime suspect. Density-independent. No visual change to
fix.**

### The fragment shader is genuinely heavy (`shaders/voxel.frag`, ~380 lines)
Per opaque fragment: **2× `textureGrad`** array samples (albedo + normal/rough), doubling to a second
array for the 1024px class (`voxel.frag:124-125`); **full Cook-Torrance GGX PBR** per light
(`:140-176`); **16-tap Poisson PCF** sun shadow (`:189-206`); **≤32 point-light + ≤16 spot-light
loops**, each a full BRDF (`:335-367`); state/damage/emission branches; a cutout `discard`
(`:243`) that **defeats early-Z** where used. Combined with the 3× amplifier and sub-pixel microcube
quads, the pass is fill-bound.

### Hidden per-frame multipliers
- **Shadow pass = a second full chunk traversal** every frame (`RenderCoordinator.cpp:960-1006`),
  also `drawIndexed(36, …)`, culled by distance only (no frustum/occlusion).
- **Reflection pass re-renders the entire visible scene a second time** when a mirror voxel is visible
  (`RenderCoordinator.cpp:1438-1440, 746-759`) — roughly doubles chunk cost — and is **not wrapped in
  a GpuProfiler scope** (invisible to profiling).
- **Sub-pixel microcube quads:** microcube face = 1/81 of a cube face area (`static_voxel.vert:196`),
  many smaller than a 2×2 shading quad → >75% quad-overdraw waste, on top of the 3× amplifier.

### Draw / culling structure (the parts that are already fine)
- N visible chunks = N indexed-instanced draws; pipeline/descriptors/index bound **once** before the
  loop, only a vtx-buffer bind + push-constant per chunk (`RenderCoordinator.cpp:383-406`). CPU
  per-draw cost is low → **not** a draw-call-count CPU bottleneck at current chunk counts.
- **Per-chunk frustum + distance cull**: present, always on (`:343-370`).
- **Occlusion culling**: BFS visibility-graph prune is **implemented but OFF by default**
  (`m_occlusionCullingEnabled`, `:378`; toggle `POST /api/debug/occlusion`). A likely free win in
  interior/dense scenes — untested.

### Measurement tooling that exists / is missing
- `GpuProfiler` = **timestamp scopes** (`GpuProfiler.cpp`), already wrapping Shadow / Scene (nested:
  Static Geometry, Grass, Foliage, …) / SSAO / OIT / Post / ImGui (`RenderCoordinator.cpp:1399-1692`).
  **Gap:** the reflection pass has **no scope**; there is **no `VK_QUERY_TYPE_PIPELINE_STATISTICS`**
  anywhere → fragment-invocation / overdraw is not measurable in-engine yet.

## 3. Increments (each buildable / verifiable / revertible behind a toggle)

### D0 — Measure the wall (no behaviour change; the load-bearing step)
Split the ~32 ms across passes with the existing `GpuProfiler` timestamps, on the dense 377k-face
scene: Shadow vs Scene(Static Geometry) vs SSAO vs OIT vs Post vs UI — and **add a profiler scope
around the reflection pass** so a mirror's cost is visible. Add a **`VK_QUERY_TYPE_PIPELINE_STATISTICS`
query** (fragment-shader-invocations + primitives) around the Static Geometry draw to measure actual
**overdraw** (fragment invocations ÷ pixels covered) — this directly tests the 3× amplifier + sub-pixel
hypotheses. Record to `docs/evidence/renderdensity_baseline.txt`: where the frame time goes, and the
overdraw factor. **Choose D1's target from these numbers, not §2's hypothesis.**

### D1 — Kill the 36-index amplifier (top lever; density-independent, no visual change)
Draw each face instance with a **single quad**: vertex buffer = 4 corner ids `{0,1,2,3}`, index buffer
= 6 indices `{0,1,2, 1,3,2}` (winding matching the current front-face convention), draw
`drawIndexed(6, faceCount)`. The shader already builds the quad from `faceID` + `vertexID` bits 0-1, so
no shader change beyond ensuring winding parity across all 6 faceIDs. Behind a toggle (`s_quadDraw`,
A/B). **Red-before-green:** the D0 overdraw counter drops ~3×, FPS rises on the dense scene, and
pixel-compare vs OFF is identical (same quads, drawn once). Applies to the shadow + reflection passes
too (same draw). Expected the **largest single win**.

### D2 — Turn on the free culling already implemented
Enable occlusion culling by default (or verify why not) and extend the **shadow pass** to frustum +
occlusion cull (it distance-culls only). Measure the visible-chunk / draw reduction and FPS on an
interior/dense scene. Cheap, no new code for occlusion (exists).

### D3 — Reduce per-fragment cost where free
From D0's fragment-cost signal: skip the point/spot-light loops when the scene has 0 dynamic lights
(common); early-out the 16-tap PCF when unshadowed; ensure the cutout `discard` path is only compiled
into the grass/foliage materials that need it (it defeats early-Z for all opaque voxels otherwise).
Each behind a measured A/B. No look change.

### D4 — Face reduction (coordinate with RenderOptimization.md)
Microcube **cross-cube** greedy merge (Increment 4b, parked) + sub-pixel microcube LOD (collapse
distant microcubes to their parent sub/cube face). Face reduction is the RenderOptimization.md track;
this plan owns the per-face-cost multipliers (D1-D3) that make each remaining face cheap. Sequence D1
first (it makes every face ~3× cheaper regardless of count).

### D_stress — Scale + regression
Re-measure FPS + overdraw on: the 412k-face tavern, the 3.4M-face settlement, a mirror-in-view scene
(reflection doubling), and a many-dynamic-lights scene. Assert the D1 win holds at every scale and no
pass regressed. `docs/evidence/`.

## 4. Verification summary (per CLAUDE.md — none optional)
1. **D0** GPU-pass split + overdraw factor on the real dense scene, before any fix.
2. **Toggle A/B** for each increment (OFF = today, byte/pixel-identical where a no-look change).
3. **Pixel-compare** fix ON vs OFF at fixed poses (D1 especially — must be identical).
4. **Release FPS** before/after on the dense scenes — the shipped bar.
5. **Solution-auditor** before any "works"; a fix is not done until the engine runs it.

## 5. Non-goals
- Chunk-update / buffer / meshing costs (done: T0/B0/B1 — not the recurring bottleneck).
- A full deferred renderer / GI rework — out of scope; this is targeted per-face-cost removal.
- Face-count *reduction* internals (RenderOptimization.md owns the greedy-merge mesher); D4 only
  sequences it.
