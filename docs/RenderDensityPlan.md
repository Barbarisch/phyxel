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

### The load-bearing HYPOTHESIS — a suspected ~3× rasterization amplifier on EVERY face
> **NOT YET MEASURED.** The code chain below is verified by reading, and the ~3× is arithmetically
> consistent with it, but the actual overdraw factor and any FPS win are UNCONFIRMED until D0 runs the
> `PIPELINE_STATISTICS` counter and D1 measures an A/B delta. Treat this as the top hypothesis to test
> in D0, not a measured fact.

Each visible face is one `InstanceData` (one `faceID`), but the draw is
`vkCmdDrawIndexed(36, faceCount)` (`RenderCoordinator.cpp:404`, `VulkanDevice.cpp:1832`) — **36
indices = a whole cube, 12 triangles** (`VulkanDevice.cpp:1074-1087`). The vertex shader positions
every vertex from the **per-instance `faceID`** + only `vertexID` bits 0-1 (`static_voxel.vert:
62-161`; `vertexID` is a per-vertex input, binding 0 = 8 corner ids). So all 36 indices collapse onto
the instance's single face quad: **~36 VS invocations for a 4-corner quad, and ~3× redundant
rasterization of the identical quad at equal depth** (12 tris; back-facing half culled → ~6
front-facing = 3× the 2 needed; early-Z can't help — same depth/plane). The index/vertex buffers date
from a per-*cube*-instance design (`VulkanDevice.cpp:1831` comment still says "36 indices per cube")
that became per-*face* without shrinking the draw. If the arithmetic holds at runtime this multiplies
a **heavy fragment shader** (below) ~3× on every face, at every density. **Prime suspect (unmeasured —
D0 confirms or refutes). Density-independent. No visual change to fix.**

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

## 2d. D0 RESULT (measured 2026-07-08 — [`docs/evidence/renderdensity_baseline.txt`](evidence/renderdensity_baseline.txt))

**The §2 "load-bearing hypothesis" (main-pass 3× overdraw) is REFUTED as the dominant cost. The wall
is the SHADOW PASS.** On the dense 377k-face scene (Release, GpuProfiler timestamps, 3 stable reads,
FPS 30.97 / frame 34.8 ms):
- **Shadow Pass 24–26 ms (~75% of the frame).**
- Scene Pass 6.5 ms total — **Static Geometry (the 377k-face main pass) only 4.70 ms**, Grass 1.48.
- Static-Geometry pipeline stats: `input_primitives` 451,512 = **~12 triangles/drawn-face → the
  36-index amplifier IS real at runtime**; but `frag_invocations` 3.06M ≈ one 1440p screen → the main
  pass is **NOT overdraw-bound** (the ~3× coplanar quads are mostly back-face-culled/degenerate).

So the main-pass amplifier is a minor cost; the shadow pass (a second full chunk traversal every
frame, **distance-culled only — no frustum/occlusion**, `RenderCoordinator.cpp:960-1006`, same 36-index
draw) dominates. It almost certainly draws far more chunks than the 20 the frustum-culled main pass
draws. **D1 is redirected to the shadow pass (below).**

## 3. Increments (each buildable / verifiable / revertible behind a toggle)

### D0 — Measure the wall (no behaviour change; the load-bearing step) — ✅ DONE (see §2d)
Split the ~32 ms across passes with the existing `GpuProfiler` timestamps, on the dense 377k-face
scene: Shadow vs Scene(Static Geometry) vs SSAO vs OIT vs Post vs UI — and **add a profiler scope
around the reflection pass** so a mirror's cost is visible. Add a **`VK_QUERY_TYPE_PIPELINE_STATISTICS`
query** (fragment-shader-invocations + primitives) around the Static Geometry draw to measure actual
**overdraw** (fragment invocations ÷ pixels covered) — this directly tests the 3× amplifier + sub-pixel
hypotheses. Record to `docs/evidence/renderdensity_baseline.txt`: where the frame time goes, and the
overdraw factor. **Choose D1's target from these numbers, not §2's hypothesis.**

### D1 — Cut the SHADOW PASS (the wall, per D0) — TOP LEVER, redirected by measurement
**D1a diagnosis DONE (2026-07-08, evidence file):** the shadow pass draws **138 chunks vs 20 visible**
(distance-cull only), **231,997 instances → 2,783,964 primitives (12 tris/face → the 36-index
amplifier), frag_invocations = 0 (DEPTH-ONLY → purely primitive/vertex-bound)**. So the winning lever
is clear: **the 6-index quad** (12→2 tris/face = 6× fewer primitives + VS) hits the exact bottleneck,
no visual change, no caster-drop risk. Light-volume frustum culling (138→fewer) is a complementary
second lever. **(NB: the pipeline-stats queries add sync overhead — gate them OFF before D1 A/B
timing so they don't pollute the delta.)**

**D1 6-index quad RESULT (measured 2026-07-08, evidence file):** implemented behind `s_quadDraw`
(`POST /api/debug/quad_draw`); pipeline-stats now gated OFF by default. **Pixel-identical ON vs OFF**
(winding correct for all faces), **primitives cut exactly 6×** (shadow 2.78M→464k, static 451k→75k).
Main pass **4.7→3.1 ms** (fragment work 3.06M→1.9M). BUT the **shadow pass did NOT change** (~28 ms) —
refuting D1a's projection. `frag=0` meant depth-only, but the shadow cost is **depth-FILL** (invisible
to that counter) and 6× fewer coplanar tris don't reduce shadow-map texel coverage. So the quad is a
correct, keep-it win for the main pass, but **not** the shadow fix. → **D1b below.**

### D1b — shadow-map resolution test: DONE, resolution is only a PARTIAL lever
**Measured (evidence file):** 4096²→2048² (4× fewer texels) cut the shadow pass only **~30%**
(28→20 ms), not ~4×. So it's only partially fill-bound; a **~20 ms floor** remains that is neither
primitives (quad: no effect) nor mostly texels. With 138 shadow draws vs 20 visible (~0.24 ms/draw),
the floor points to **draw-call / per-chunk-bind count**. Reverted 2048²→4096² (modest gain, quality
loss, wrong lever). → **D1c**.

### D1c — light-frustum cull the shadow pass: DONE, can't help (chunks are in-volume)
**Measured:** a correct AABB-vs-light-ortho cull (`s_shadowFrustumCull`, default ON) only drops
**138→131 chunks (~5%)**, NO shadow-pass change. The fitted shadow volume legitimately contains ~131
chunks, so culling can't reduce the draw count. Loss-free (shadows intact), kept ON, but not the fix.

### Shadow characterization — three levers exhausted; the wall is structural
primitives (D1: no effect) · resolution (D1b: ~30%) · cull (D1c: no effect). Decompose 2048²:
**~17 ms FIXED + ~11 ms fill** @ 4096². The 17 ms fixed is most consistent with **per-draw GPU
overhead across ~131 draws** (~0.13 ms/draw). Remaining fixes are structural, none a quick toggle:
1. **Batch the shadow chunk draws** (merge → fewer draw calls) — attacks the ~17 ms floor; biggest
   suspected win, biggest change.
2. **Shadow update cadence** (render every N frames, reuse) — ~2× amortized, near-free; needs care on
   sun/camera motion.
3. **Shorter shadow distance / cascade** — smaller volume → fewer chunks + less fill; quality or
   complexity tradeoff.

Then cut the dominant factor:
- **Frustum + (optionally) occlusion cull the shadow pass** — it currently distance-culls only
  (`RenderCoordinator.cpp:980-984`); if it's drawing far more chunks than are visible, this is the win.
- **Shadow-map resolution / cascade / update-frequency** — if it's fill/resolution-bound, lower res
  or update shadows every N frames.
- **The 6-index quad (old D1, now folded here):** draw each face as one quad (vtx `{0,1,2,3}`, indices
  `{0,1,2,1,3,2}`, `drawIndexed(6, faceCount)`) — D0 confirmed the 36-index/12-tri amplifier is real,
  and the shadow pass is **geometry/primitive-bound**, so 12→2 tris/face = 6× fewer primitives should
  help it most. Also speeds the main + reflection passes (same draw). Behind a toggle; pixel-compare
  identical (same quads).
Each sub-lever behind a toggle, measured A/B on the dense scene (target: the ~25 ms shadow pass down).

### D2 — Main-pass polish (deprioritised by D0 — it's only 4.7 ms)
Occlusion culling for the main pass (implemented, OFF by default) is a smaller win now that the main
pass is cheap; still worth enabling for interior scenes. The 6-index quad's main-pass benefit is minor
(main pass isn't fill-bound). Sequence after D1.

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
