# Continuous LOD — a unified, Nanite-inspired scaling layer for every subsystem

> **Status: DESIGN ONLY — no code, no branch.** Written 2026-07-29 in response to the goal
> "a continuously scaling LOD system that works for all systems: static voxels, dynamic
> voxels, water, shadows." This doc decides the *architecture* and the *order*; it does not
> claim anything works. Every measured number below is cited to code or an existing doc;
> everything unmeasured is labelled **HYPOTHESIS** per the `RenderDensityPlan.md` convention.
>
> **Companion docs (read these, don't duplicate them):**
> [`LargeWorldScalePlan.md`](LargeWorldScalePlan.md) (Phases 3–5 + the §5 field survey — the
> single best source here) · [`FarRepresentationProviders.md`](FarRepresentationProviders.md)
> (compositing vs provision) · [`RegionArenaPlan.md`](RegionArenaPlan.md) (shipped
> prerequisite) · [`RenderOptimization.md`](RenderOptimization.md) (the greedy-merge campaign
> + two open visual defects) · [`RenderDensityPlan.md`](RenderDensityPlan.md) ·
> [`RayTracingPlan.md`](RayTracingPlan.md) (the endgame this plan deliberately does not enter).
>
> **Decisions already taken by the user (2026-07-29), binding on this plan:**
> 1. **The cube is the atomic LOD unit.** Sub/microcube detail becomes an *appearance of a
>    cube* (a palette brick), not a rung on the LOD ladder. The ladder is power-of-two from
>    the cube up. (§2.1)
> 2. **Design doc before code.** This file is that deliverable.

---

## 0. Ground truth (verified by code read 2026-07-29 — cite before editing; lines drift)

### 0.1 Three doc headers are STALE. Do not plan against them.

| Stale claim | Reality (verified) |
|---|---|
| `CLAUDE.md`: "**#1 known issue:** render density — 412k faces → ~49 FPS; fine faces aren't greedy-merged; fix deferred" | **Shipped and default-ON since 2026-07-07.** `ChunkRenderManager::s_fineGreedyMerge = true` (`ChunkRenderManager.cpp:62`). Measured: 9 taverns 639,585 → 53,219 faces (12.0×), 41.5 → 206.5 FPS; 16 taverns 1,126,856 → 92,438 (12.2×), 26.4 → 180.4 FPS (`RenderOptimization.md:374-400`) |
| `BinaryGreedyMeshingPlan.md`: "Status: PLANNED — not started" | Superseded; the work landed as Increments 1–4a tracked in `RenderOptimization.md:139-315` |
| `RenderDensityPlan.md`: the ~3× rasterization amplifier is the top unmeasured suspect | **Fixed.** `VulkanDevice::s_quadDraw = true` (`VulkanDevice.cpp:1083`); `chunkIndexCount()` returns 6, not 36, for the main pass (`VulkanDevice.h:320`) |

**Consequence for this plan — scoped honestly (tightened 2026-07-29 after a solution-auditor FAIL
on an earlier "substantially retired" phrasing):** the greedy-merge **mechanism** is shipped and
default-on, and it recovers 5–8× FPS **on synthetic flat multi-tavern grids** — an explicit
face-bound *proxy*. What was never re-run is the scenario the "#1 known issue" actually named: the
single furnished tavern (412,298 faces / 49 FPS), where the source itself says FPS is *noise* at
that scale, and the documented worst case — the **3.4M-face Perlin-hills settlement — "remains not
run"** (`RenderOptimization.md:352,376-380`). So:
- **Do not justify this plan with "the density wall is solved."** It is *probably* much better; that
  is not the same as measured.
- The plan's justification stands on its own without it: **render distance, the measured 75% shadow
  cost, and subsystem unification** — none of which depend on the tavern number.
- **M4** (§7b) is the falsifiable test that would settle the density question either way.

> **Action item (do this whether or not the plan proceeds):** correct the three headers above.
> Planning against stale status is how the reverted Phase 5 happened twice.

### 0.2 What already exists and is load-bearing for this plan

| Asset | Where | Why it matters here |
|---|---|---|
| **Region arenas, default ON** | `RegionArenaPlan.md` (A0–A4 shipped 2026-07-18) | 4,693 → 38 allocations at ~4k chunks (127×); 10,609 resident chunks / 12,054 spans in 81 blocks, stable. **This is the prerequisite shape for GPU-driven indirect draw** and it is already done |
| **Greedy merge at all three scales** | `ChunkRenderManager`, `s_fineGreedyMerge`/`s_smoothLighting` both `true` | The squash operator's output can reuse the *same* mesher |
| **Merged-quad encoding in the shader** | `static_voxel.vert:80-84` (cube: 6+6 bits → runs to 64), `:100-103` (fine: light bits 16-31) | A face of arbitrary size already renders correctly. Coarse LOD faces need a *scale multiplier*, not a new pipeline |
| **`scaleLevel` code 3 is FREE** | `static_voxel.vert:76` — bits 18-19, values 0/1/2 used | The natural slot for a coarse LOD face. ⚠️ Also exactly where the reverted Phase 2 died (magenta textures, `RenderOptimization.md:55-60`) — the mystery was later resolved by Increment 1 (`RenderOptimization.md:139`) |
| **Palette + RLE chunk blobs** | `ChunkBlobCodec`, storage v2 | The correct *source* for downsampling. Never downsample the `Cube` array |
| **Watertight far-terrain mesher** | `FarTerrainMesher`, `kBelowSurfaceBias = 0.5f` on every ring | The crack discipline this plan generalises |
| **Far terrain (heightmap provider)** | `FarTerrainManager::Params` — `enabled = false`, `maxDistance = 2048`, `ringSteps{2,4,8}` (tile = 64×step), `maxResidentTiles = 512` (`FarTerrainManager.h:42-46`) | The far tier already exists. It is **OFF by default** and it is 2.5-D (lies about overhangs, ignores edits/structures — `FarRepresentationProviders.md` Axis 2) |
| **Occlusion BFS** | `RenderCoordinator::applyOcclusionCulling:1051`, near-bound `kOcclusionMaxDist = 512` | Field-validated complement (Sodium/Luanti family). Keep |
| **Coarse world model** | `CoarseWorldModel`, `HydrologyMap`, `kSeaLevelY = 16` | Lets terrain LOD be **generated coarse directly**, not downsampled — the field's #1 correction (`LargeWorldScalePlan.md:756`) |

### 0.3 Where the cost actually is today

> **⏩ 2026-08-06: this section is now HISTORICAL — see the C5b supersession box (§5) and
> `docs/RenderDensityPlan.md`'s banner.** The shadow wall is resolved (3 cascades, ~5 ms);
> the per-draw-overhead attribution was disproven at every operating point; the ten-plus
> hardcoded distance systems are now inventoried and ruled in `docs/LodTierLedger.md`.

- **The shadow pass is the measured wall — and its recorded next step is this plan's C2.**
  ⚠️ *Corrected 2026-07-29: an earlier draft of this doc called this a "HYPOTHESIS (unmeasured)".
  It is measured, in detail, in `RenderDensityPlan.md` §2d + D1a–D1c, which I had not read far
  enough into. The correction strengthens the plan; it does not change the phase order.*
  On the dense 377k-face scene (Release, GpuProfiler, 3 stable reads, 34.8 ms frame):
  - **Shadow Pass 24–26 ms ≈ 75% of the frame.** Scene Pass 6.5 ms total; the 377k-face main
    pass only **4.70 ms**.
  - **Three levers are exhausted.** Primitives: no effect. ⚠️ **The recorded REASON has now been
    wrong twice, and the current text is the third attempt.** (1) The code comment at
    `RenderCoordinator.cpp:1241` says the shadow pipeline *front*-culls. (2) I "corrected" that to
    `VK_CULL_MODE_NONE`, citing `ShadowMap.cpp:433` and commit `07ba0a74`. (3) A solution-auditor
    caught that my correction named the **wrong pipeline**, and I verified it: `CULL_MODE_NONE`
    (`ShadowMap.cpp:529`) lives in `buildDepthOnlyPipelineState()`, used only by the
    **character / kinematic / dynamic** shadow pipelines. The **main chunk shadow pipeline**
    — `createPipeline()`, the one `renderShadowPass` actually binds — sets
    **`rasterizer.cullMode = VK_CULL_MODE_BACK_BIT`** at **`ShadowMap.cpp:388`**, unchanged by
    `07ba0a74`.
    **So the chunk shadow pipeline back-culls, exactly like the main pass.** Neither "front-culls"
    nor "CULL_NONE" was ever true of it. That deepens the open question rather than settling it: if
    the winding rules match the main pass, a 6-index single-winding quad *should* work here, yet D1
    measured a ~1.1% pixel break when the quad was applied to all passes. **The real cause of that
    break is unknown and must not be guessed at a fourth time.** M5 must re-derive it empirically.
    Resolution: 4096²→2048² bought only ~30%. Light-frustum cull: `s_shadowFrustumCull`
    drops 138→**131** chunks (~5%) with no timing change — **the fitted volume legitimately
    contains those chunks, so culling cannot be the fix**. Shipped default-OFF
    (`RenderCoordinator.cpp:1172`), kept explicitly as the cascade hook.
  - **Decomposition: ~17 ms FIXED + ~11 ms fill.** The fixed part is most consistent with
    **per-draw GPU overhead across ~131 draws (~0.13 ms/draw)**.
  - `RenderDensityPlan.md`'s own recorded remaining fixes: **"batch the shadow chunk draws
    (merge → fewer draw calls) — biggest suspected win, biggest change"**, shadow update
    cadence, cascades. **That first item is exactly C2** (multidraw/indirect over the already-shipped
    region arenas), and its D4 names **"sub-pixel microcube LOD (collapse distant microcubes to
    their parent sub/cube face)"** — exactly C0. This plan is the continuation of a measured
    investigation, not a new direction.
- **Per-frame O(all resident chunks) CPU scans** — `LargeWorldScalePlan.md:61-67` blocker E.
  Arenas fixed allocation count, not iteration count.
- **AT LEAST TEN independent, hardcoded LOD/distance systems with no shared metric.**
  *(Corrected TWICE on 2026-07-29: a first draft said "four"; a second said "nine" and was still
  an undercount — a solution-auditor found a 10th. **Stop asserting a count.** Before quoting a
  number again, run a real completeness sweep of engine-wide `*Distance*`/`*Radius*` defaults.
  The recurring undercount is itself the finding: nobody knows how many of these there are,
  which is the strongest argument for C1.)*
  C1 must **re-home** these, not invent an eleventh — any of them can mask or fight a new cut.

  ⚠️ **Missed by both earlier drafts:** **`ChunkManager::loadDistance = 256.0f` /
  `unloadDistance = 352.0f`** (`ChunkManager.h:99-100`) — the streaming residency radius. It is
  distinct from `maxChunkRenderDistance` (which only culls what is *already* resident) and is
  arguably more fundamental than any row below, since it decides what data exists at all.

  ⚠️ **`maxChunkRenderDistance` has no single default.** The in-class `1000.0f` cited below is
  the *uninitialized* value; shipping paths override it — `EngineConfig.h:62` (256/320),
  `WorldInitializer.h:98-99` (96/128), `GameSettings.h:50` (256). Quoting one number as "the
  default" is wrong for every real config.

  | System | Mechanism | Default | Where |
  |---|---|---|---|
  | Far terrain | clipmap rings, `ringSteps{2,4,8}`, tile = 64×step | **OFF**, `maxDistance` 2048 | `FarTerrainManager.h:43-45` |
  | **Character render LOD** | drops to a **decimated part set** at 2 thresholds | **ON**, lod1 **35u**, lod2 **80u** | `RenderCoordinator.h:121-126, 405-411` |
  | **Character cull** | not drawn at all beyond distance | ON, **400u** | `RenderCoordinator.h:403` |
  | **Character update LOD** | frame-skipping with banked delta-time | **ON** (`s_lodEnabled = true`) | `AnimatedVoxelCharacter.cpp:27` |
  | NPC update gate | `fullCharacterTicks` accounting | ON | `NPCManager.h:163-166` |
  | Chunk render distance | frustum + distance cull | `maxChunkRenderDistance` 1000u | `RenderCoordinator.h:444` |
  | Occlusion BFS | visibility graph, near-bound | OFF by default, bound 512 | `RenderCoordinator.cpp:1051` |
  | Shadow cull | distance sphere + 160 margin | ON (light-frustum cull OFF) | `RenderCoordinator.cpp:1177+` |
  | Grass / foliage / water | independent radii; player-following sim region | ON | `setGrassParams`, `setFoliageParams`, `WaterManager` |

  ⛔ **RETRACTED — "far terrain's 2/4/8 ring steps are compatible with §2.1's ladder."** That was
  a **superficial numeric coincidence**, not a finding. `ringSteps` is a **2-D heightmap
  column-sampling stride** (`FarTerrainMesher.cpp`, `sampleSurface(x + i*step, …)` → one
  quantized `surfaceY` per column); §2.1's ladder squashes **3-D voxel occupancy + coverage +
  material**. Different dimensionality, no shared representation, no conversion, and zero code
  links them. Both being powers of two is the *only* thing they share. Treat far-terrain
  reconciliation as **unanalysed work**, not as compatibility already achieved.

  🚨 **THERE IS NO REGRESSION PROTECTION ON ANY OF THESE SYSTEMS.** A grep of `tests/` for every
  identifier above returns **zero hits** except `FarTerrainMesherTest`, which only checks
  tile-geometry math at a caller-supplied step — it never exercises ring/distance *selection*.
  Concretely: flipping `s_shadowFrustumCull` to `true`, setting `m_charLod1Distance` to 3500, or
  breaking `lodForDistanceSq`'s comparisons would leave **all 3080 tests green**. So "the suite
  passes" says *nothing* about LOD regressions.
  **This changes C1's shape: C1 must FIRST add characterization tests pinning the current
  behaviour of each system, then re-home them.** Re-homing without those tests is unverifiable by
  construction — exactly the kind of unfalsifiable change this plan's discipline exists to prevent.

  **The character thresholds are genuinely world-unit** (auditor-confirmed by tracing
  `distSq = glm::dot(rel, rel)` → `lodForDistanceSq`, with zero FOV/viewport terms anywhere in
  `RenderCoordinator.cpp`), so C1's motivation is grounded. The character
  thresholds (35/80/400) were tuned independently and are in *world units*, not screen-space, so
  they do not respond to FOV or resolution at all — the exact defect C1's shared metric fixes.

### 0.4 Two OPEN visual defects that this plan will amplify if ignored

1. **T-junction cracks at greedy-merge borders** ("dotted lines", `RenderOptimization.md:489`).
   LOD boundaries are T-junctions *by construction*, at much larger scale. **This defect and
   the LOD-seam problem are the same problem** and should be solved once, in the mesher.
2. **Grass/character sub-pixel shimmer** (`RenderOptimization.md:513+`). Any LOD transition
   band that dithers will interact with this. Default sun is noon (worst case).

---

## 1. What Nanite is, and what actually transfers

Nanite is four separable mechanisms. Copying it wholesale is not the goal; copying the two that
transfer is.

| Nanite mechanism | Voxel analogue | Verdict |
|---|---|---|
| **Cluster DAG** — offline edge-collapse simplification + re-clustering, with group-boundary locking so LOD levels are crack-free without stitching | A regular-grid occupancy pyramid. Cells are axis-aligned and nested by construction | ✅ **Transfers, and is strictly easier.** No clustering pass, no boundary locking, no vertex welding — the grid gives us what Nanite has to earn |
| **Screen-space-error cut** through the DAG, evaluated per cluster per view, with monotonic parent ≥ child error | Pick the level at which a cell projects to a target pixel footprint (§2.4). ⚠️ **Monotonicity holds only for the geometric half** — see the caveat below | ✅ Transfers, **with a caveat that changes the selector's design** |
| **GPU-driven pipeline** — persistent culling, two-phase Hi-Z occlusion, visibility buffer, deferred material passes | Identical, over instanced quads instead of triangles. `vkCmdDrawIndexedIndirectCount`, Vulkan 1.2, no mesh shaders needed | ✅ Transfers — but it is **the long pole**. Already scoped as `LargeWorldScalePlan.md` §5.2 item 5 |
| **Software rasteriser for sub-pixel triangles** | Sub-pixel microcube quads are literally this problem — but the field's answer for *voxels* is per-pixel volume traversal (Teardown/Octo/Avoyd), not a software raster | ❌ **Do not port.** `LargeWorldScalePlan.md:876-877` reached this independently. It belongs to `RayTracingPlan.md`, not here |

> **⚠️ The monotonicity caveat (audit finding, 2026-07-29).** Nanite's single-threshold cut is
> correct *because* parent error ≥ child error everywhere. Here, only the **geometric** half of
> that is free: a parent cell is exactly 2× its children, so cell-footprint error is trivially
> monotone. But §2.4's **`appearanceError`** term — the thing that actually keeps a painted door
> or a lamp from dissolving — has **no such guarantee**: neither the OR-occupancy rule nor the
> surface-area material vote makes histogram entropy monotone up the pyramid, so a grandparent
> can be *less* "surprising" than its parent. **Do not assume a clean global cut.** Either prove
> monotonicity for the composed metric in C0, or — the cheaper and recommended route — make the
> selector a **per-branch descent** (walk down from the root, stop when the error test passes),
> which is correct without monotonicity and is how most SSE implementations actually work.

**And one thing Nanite explicitly does not solve: dynamic geometry.** Nanite was static-mesh-only
for years. Dynamic voxels, water, and characters need a *different selection rule*. They share
only the **error metric** and the **submission path** — never the DAG. Designing as if one
mechanism covers all five subsystems is the main way this plan could fail.

### 1.1 "Continuous" — what is honestly achievable

You cannot half-collapse a cube. Voxel LOD is geometrically discrete. Nanite's *perceptual*
continuity comes from three things, and all three have voxel analogues:

| Nanite | Here |
|---|---|
| Very fine cluster granularity (~128 tris) so each transition is tiny and local | **Cut at 8³ brick granularity, not 32³ chunk.** A per-chunk cut will pop visibly (§2.3) |
| The cut is per-cluster, not per-object | Per-brick, so neighbouring bricks sit at different levels simultaneously — the transition is a *scattered boundary*, not a ring |
| TAA dissolves the residual | Dithered cross-fade over a transition band, **or** Lysenko's POP-buffer geomorph for blocky voxels — genuinely vertex-continuous, already bookmarked at `LargeWorldScalePlan.md:855` |

Honest framing for the gate: **"continuous" means no visible pop at any camera speed on the
scripted flight**, not "geometrically continuous". Assert it with an automated frame-to-frame
delta on a fixed flight path, not with an opinion about a screenshot.

---

## 2. The unified model

Five decisions. (1) and (2) are load-bearing — everything else follows from them.

### 2.1 The ladder — power-of-two from the cube up (DECIDED)

```
  appearance tier        │  geometry ladder (the LOD DAG)
  ── per-cube brick ──   │  L0  1 cube      (32³ per chunk)
   9³ palette volume     │  L1  2×2×2 cubes (16³)
   (micro 1/9 + sub 1/3  │  L2  4³          (8³)
    collapse into it)    │  L3  8³          (4³)
                         │  L4  16³         (2³)
                         │  L5  32³         = 1 chunk
                         │  L6+ multi-chunk (far tier — heightmap or coarse volume)
```

**Why the tiers split here.** The existing hierarchy is ternary below the cube (1 → 1/3 → 1/9)
and would need to be binary above it. A DAG tolerates mixed radix; 64-bit child masks, shift
addressing, and every published traversal structure do not. Our 3ⁿ nesting has **no precedent in
any shipping engine** (`LargeWorldScalePlan.md:791`).

Splitting at the cube removes the mixed radix entirely:
- **Above the cube:** pure power-of-two. Cell size = `1 << level`. `chunk / brick` = clean powers.
- **Below the cube:** sub/microcube stop being a geometry tier and become the cube's *appearance* —
  a **9³ palette brick**. 9³ is this engine's real microcube resolution, not a chosen number:
  subcube = 3×3×3 within a cube and microcube = 3×3×3 within a subcube ⇒ 9×9×9 = **729 microcubes
  per cube** (`Types.h:248-250`; `MICROCUBE_SCALE = 1.0/9.0` at `static_voxel.vert:200`).
  "729 bytes" **assumes 1 byte/voxel** — that is Teardown's measured figure (1 B/voxel at 10 cm,
  `LargeWorldScalePlan.md:799`), *not* a measurement of this engine; the brick shape itself is
  `LargeWorldScalePlan.md:876-877`. Our real per-voxel cost must be measured, not inherited.
  Today that appearance is realised as greedy-merged faces (already shipped, already fast). Later
  it can become a raymarched brick with **no change to the LOD ladder** — that is the whole point
  of the split, and it is what keeps `RayTracingPlan.md` reachable without entering it now.

**What this does NOT mean:** nothing about sub/microcube *storage*, *placement*, *physics*, or
*authoring* changes. `SpawnGate`'s resolution-complete solidity, the furniture micro-precision
rule, `scan_micro` — all untouched. This is a rendering/selection decision only.

### 2.2 The squash operator — the reduction rule (the risky part)

Downsampling one level = for each parent cell, reduce its 8 children to `(occupied?, material)`.
Both halves are non-obvious, and **this is where the reverted Phase 5 died.**

**(a) Occupancy rule.** Neither naive option is right everywhere:

| Rule | Failure mode |
|---|---|
| **OR** (any child solid → parent solid) | Closes doors and windows at distance; fattens railings, fences, tree branches into blobs |
| **Threshold** (≥50% solid — ⚠️ `NEEDS-RESEARCH`: 50 is the obvious midpoint, not a sourced convention; the nearest real one is the marching-cubes isovalue. Low priority, since this is the *rejected* option) | Thin walls vanish — and the thinnest wall this engine actually generates is **1 microcube** (§5 C0); interiors leak light; **it can delete the very geometry `SpawnGate` relies on** |

**Recommendation: OR by default, with a per-cell `preserveOpening` escape.** Terrain wants OR
(no openings to lose, and fattening is invisible against a hillside). Structures need openings
preserved — and we already *know* where they are: the structure pipeline emits doors/windows as
first-class plan elements (`AssemblyPlan`, the Claims Ledger work). Feed that down as an
authored hint rather than trying to infer it from voxels. **HYPOTHESIS: authored hints beat
inference here.** C0 tests it both ways before committing.

**(b) Material rule.** Majority-vote by **exposed surface area, not by volume.** A stone-cored,
plaster-skinned wall is mostly stone by volume and reads as *stone* at distance — wrong. What a
viewer sees is the skin. Vote over child faces that were exposed at the finer level.

**HYPOTHESIS (unmeasured):** surface-area voting visibly beats volume voting on a settlement.
C0's L2 test measures it — it is cheap to run both.

### 2.3 Cluster granularity — an OPEN experimental variable, decided by C0

> **⚠️ CORRECTED 2026-07-29 after a grounding audit.** An earlier draft of this section asserted
> an **8³ brick** and justified it with `binary-greedy-meshing` and Tree64. **Both citations were
> wrong, and one contradicted a doc in this repo:**
> - **Tree64 is 4³-branching** (64 children = 4×4×4 — that is where the name comes from), per our
>   own `RayTracingPlan.md:28`. It does not argue for 8³.
> - **cgerikj's binary-greedy-meshing** wants a **64-tall column packed into one u64** (1 bit/voxel)
>   and operates on whole **62³** chunks. That is a meshing bit-packing convenience with no bearing
>   on LOD-cluster edge length.
> - **Sodium's 8×4×8** is a *buffer-grouping* unit (sections sharing one GPU allocation) — which is
>   what our region arenas already implement — **not** a cluster/LOD granularity.
> - **Nanite's ~128 triangles/cluster** is a triangle *count*, dimensionally incommensurable with a
>   voxel edge length.
>
> Stripped of those, the only remaining argument for 8³ was "it divides 32" — grid-convenience,
> exactly what the grounding rule exists to catch. **The number is now an open variable.**

**Constraints that are real** (keep these; they bound the search, they don't pick a value):
- **Chunk-granular cuts pop.** A 32³ chunk at 300u subtends far more than a pixel, so flipping a
  whole one is a visible event. The cut unit must be *finer than a chunk* — this bounds it from
  above, and is the only part of the original argument that survives.
- **Power-of-two, divides 32** → the candidate set is exactly **4³, 8³, 16³** (per chunk: 512, 64, 8).
- Finer = smoother transitions but more draw units; coarser = fewer draw units but visible popping.
  **That trade is measurable and nobody else's published number substitutes for measuring it.**

> ### 📊 SWEEP RESULT (2026-07-29) — `tests/core/LodBrickSweepTest.cpp`
> Evidence: `docs/evidence/lod_c0_brick_sweep_20260729.txt`
>
> Real generator output (`StructureRealizer::realizeShell`, shipped `tavern` typology), through the
> **shipped `squash()`**, with **authored opening hints fed in** (`AssemblyPlan::openings` →
> `preserveOpening`, role `"clear"` reveal boxes only). Two fixtures — the second added because a
> solution-auditor correctly objected that the first was **smaller than the bricks being tested**.
>
> **Fixture A — one tavern (17×9×7 cubes, 776 solid):**
>
> | brick | coarse dims | solid bricks | units/chunk | fattening | over-carve | scale-valid? |
> |---|---|--:|--:|--:|--:|---|
> | 4³ | 5×3×2 | 20 | 512 | 1.65x | 58.4% | **NO** (Z=2) |
> | 8³ | 3×2×1 | 4 | 64 | 2.64x | 86.2% | **NO** (Y=2, Z=1) |
> | 16³ | 2×1×1 | 1 | 8 | 5.28x | 92.4% | **NO** (Y=Z=1) |
>
> **Fixture B — 4×4 tiled block of the same real tavern, streets between (80×9×40, 12,416 solid):**
>
> | brick | coarse dims | solid bricks | units/chunk | fattening | over-carve | scale-valid? |
> |---|---|--:|--:|--:|--:|---|
> | 4³ | 20×3×10 | 348 | 512 | 1.79x | **49.7%** | **yes** |
> | 8³ | 10×2×5 | 45 | 64 | 1.86x | 85.4% | NO (Y=2) |
> | 16³ | 5×1×3 | **0** | 8 | 0.00x | **100.0%** | NO (Y=1) |
>
> ### 🔴 THE FINDING (revised after the scale control — the conclusion survives, the reasoning changed)
> The auditor was right that **none of Fixture A's rows were scale-valid**, so its 58-92% figures
> could not carry a general conclusion. Fixture B fixes X and Z. What it shows:
>
> 1. **At the one scale-valid point (4³, ample X/Z), over-carve is still 49.7%** — the binary carve
>    erases *half* the block's wall. So the mechanism problem is real and is **not** an artifact of
>    a small test subject. My original *reasoning* ("brick ≈ building size") was wrong; the actual
>    cause is that **openings are dense enough that essentially every brick ≥4 cubes contains one**.
> 2. **At 16³ the entire block disappears — 0 solid bricks, 100% over-carve.** Every 16-cube brick
>    contains at least one opening, so the whole settlement is carved away.
> 3. **8³ and 16³ can never be scale-valid for ordinary buildings, and tiling cannot fix it.** A
>    single-story building is ~9 cubes tall, giving Y-dims of 3 / 2 / 1 at 4³ / 8³ / 16³. You would
>    need a ~48-cube-tall building to span three 16³ bricks vertically. **This is a permanent
>    property of building geometry, not a test artifact.**
>
> **Conclusions:**
> - **4³ is the only viable brick size for structures.** 8³/16³ are rejected on vertical-collapse
>   grounds alone, independent of the opening question.
> - **The binary carve must be replaced for coarse levels.** Even at 4³ it costs ~50% of the wall.
>   Openings need sub-brick representation — a per-brick opening *mask*, or §2.1's per-cube
>   appearance brick — not a solid/empty flag. **This is the top C4 design question.**
> - **Terrain is untouched by this** (no openings ⇒ no over-carve), so a separate terrain sweep may
>   well justify larger bricks there. A single global brick size for terrain *and* structures looks
>   unlikely.
>
> **Metric notes (three candidates discarded — do not reinstate):** bbox-relative "hollowness"
> (inflated purely by bbox rounding); "openings retained" (tautologically 100% — `OrPreserveOpenings`
> is *defined* to let openings win); `mat_mismatch` (insensitive at 2 materials — recorded, not used
> to decide). **`fattening`, `over-carve` and the `scale-valid?` guard are what discriminate.**
> Mutation-verified by the auditor: dropping the opening feed, or switching to plain `Or`, takes
> over-carve to exactly 0.0% at every size — so the metric measures the mechanism, not an indexing
> artifact.
>
> **Fixture C — GENERATED TERRAIN** (`WorldGenerator` Perlin, same params as LodBench, 2x2 chunks
> at the surface band = 64x32x64 cubes, 94,991 solid cells, 3 materials):
>
> | brick | coarse dims | solid bricks | fattening | over-carve | mat mismatch | scale-valid? | squash |
> |---|---|--:|--:|--:|--:|---|--:|
> | 4³ | 16×8×16 | 1616 | **1.09x** | 0.0% | 12.7% | yes | 4030 us |
> | 8³ | 8×4×8 | 216 | **1.16x** | 0.0% | 19.1% | yes | 586 us |
> | 16³ | 4×2×4 | 32 | **1.38x** | 0.0% | 26.7% | NO (Y=2) | 85 us |
>
> ### ✅ CONFIRMED: terrain and structures want DIFFERENT brick sizes
> Terrain barely fattens (**1.09-1.38x** vs the tiled block's 1.79-1.86x) and **over-carves 0% by
> construction** — it has no openings. So terrain comfortably tolerates the coarse bricks that
> destroy buildings. **A single global brick constant is the wrong model**; the cut needs a
> content-aware unit (fine near structures, coarse over terrain), which is now a C3/C4 requirement
> rather than an open question.
>
> Two incidental validations: `mat_mismatch` is **sensitive here** (12.7 → 26.7% across sizes, with
> 3 materials) — confirming the metric works and was merely insensitive on the 2-material tavern,
> not broken. And squash cost on a real 131k-cell volume is **~4.0 ms for level 1**, which is the
> figure C3's pyramid-build budget has to live with.
>
> ### ✅ FIXED — the opening mechanism: `OccupancyRule::OrWithOpeningMask`
> The sweep's headline problem is now solved and measured. Instead of an opening BLANKING the
> brick that contains it, `LodCell::openingCoverage` carries the authored void as a **conserved
> quantity** (microcubes) that survives coarsening; the cell stays solid and the renderer decides
> what to do with the mask. A/B on the same tiled settlement block (256,608 microcubes of authored
> opening volume):
>
> | rule | brick | solid bricks | over-carve | opening volume kept |
> |---|---|--:|--:|--:|
> | binary carve (`OrPreserveOpenings`) | 4³ | 348 | 49.7% | 100% |
> | binary carve | 8³ | 45 | 85.4% | 100% |
> | binary carve | 16³ | **0** | **100%** | 100% |
> | **mask (`OrWithOpeningMask`)** | 4³ | **520** | **0.0%** | **100%** |
> | **mask** | 8³ | **90** | **0.0%** | **100%** |
> | **mask** | 16³ | **15** | **0.0%** | **100%** |
>
> **Zero geometry deleted at every brick size, with the opening volume conserved exactly.** Pinned
> by `OpeningMaskNeverDeletesGeometry`, `OpeningVolumeIsConservedThroughThePyramid`,
> `OpeningMaskSurvivesToTheTopOfThePyramid`, and the A/B above (which also asserts the carve rule
> still *does* delete, so the comparison can fail).
>
> **This revises the earlier verdict on 16³.** With the mask, 16³ no longer annihilates the block
> (15 bricks retained, not 0). Its remaining objections are the ones that always applied:
> **fattening** and **vertical scale-validity** (a ~9-cube-tall building cannot span three 16-cube
> bricks in Y). So 16³ is still wrong for structures — but for geometric reasons, not because the
> opening rule destroys them.
>
> **Also fixed here:** `LodCell::coverage` was still being written through a `static_cast<uint32_t>`
> in `squash()` despite the field being widened to 64-bit — silent truncation past ~level 8. Pinned
> by `CoverageDoesNotTruncateAtDepth`.
>
> **Still open for C4:** how the *renderer* consumes `openingCoverage` (alpha/dither a hole, pick a
> punched-through variant, or defer to the appearance brick). The mask makes the information
> available and lossless; presenting it is C4's job.
>
> **Still to run:** a multi-story fixture (to confirm the Y-collapse threshold);
> and a re-run once the opening mechanism is redesigned. True pop *visibility* still needs C4.
>

**C0 deliverable (original wording, now SUPERSEDED by the sweep above):** run the offline squash at
4³ / 8³ / 16³ on a real settlement and record draw-unit count, pop visibility, and squash cost.
Pick with evidence. **Until then no brick constant may be written into engine code** — that
prohibition still stands: the sweep rejected 16³ but did NOT settle 4³ vs 8³, because the opening
mechanism has to change first.

**On the C2-before-C4 ordering — the justification is WEAKER than two earlier drafts of this
section claimed.** *(Corrected twice: once by the grounding audit, again after M1's own retraction —
see §7b. An intermediate version of this paragraph asserted the ordering was "grounded in a measured
per-draw cost", which contradicted §7b elsewhere in this same file. That contradiction is the defect
being fixed here.)*

- The original justification ("~679k bricks at 8³") is void along with the 8³ assumption.
- The replacement justification — `RenderDensityPlan`'s **~17 ms across ~131 shadow draws ≈
  0.13 ms/draw** — is an **inference from a decomposition, not a directly measured per-draw cost**.
  **M1 tried to confirm it and failed**: the run that was supposed to isolate draw count from
  instance count turned out to be pose-invalid (§7b). So the per-draw figure is **neither confirmed
  nor refuted** and must not be cited here as settled.
- What does *not* depend on any of this: even the coarsest brick candidate (16³, 8/chunk) at the A4
  residency of 10,609 chunks (`RegionArenaPlan.md`) is **~85k draw units**, versus ~10.6k today.
  A per-draw-unit **CPU** submission model does not go in a good direction under that multiplier
  regardless of the constant — and blocker E (per-frame O(all chunks) scans) is independently
  documented.

**Net: C2 before C4 remains the recommended order, but on the softer grounds of draw-unit
multiplication + blocker E — not on a measured per-draw constant.** If C0's brick sweep lands on
16³ and a measured per-draw cost turns out to be negligible, this ordering is worth revisiting
rather than treating as settled. **Open until M1 is re-run properly.**

### 2.4 The error metric — one function, shared by everything

Standard projected-size math (derived, not invented):

```glsl
// lod.glsl — the ONLY place a distance cutoff is allowed to be computed.
// px = how many screen pixels a cell of edge `cellSize` covers at `dist`.
float projectedPixels(float cellSize, float dist, float viewportH, float tanHalfFovY) {
    return (cellSize / max(dist, 1e-4)) * (viewportH * 0.5 / tanHalfFovY);
}
// Pick the coarsest level whose cell still projects to >= targetPx (quality knob — see below).
```

**The formula is grounded** (audit-verified by independent derivation): the frustum slice height
at distance `d` is `2·d·tan(fovY/2)`, so pixels-per-world-unit is `viewportH / (2·d·tan(fovY/2))`;
multiplying by `cellSize` gives the shipped expression exactly.

**Two honesty notes the audit forced:**

1. **`targetPx` has no source yet — `NEEDS-RESEARCH`.** An earlier draft said "~1–2 px" with no
   citation; the SSE-family convention is nearer **0.5–1 px**, and I have not verified either
   against a specific reference implementation. Do not ship a borrowed constant. The number that
   matters is **whatever passes C4's no-pop gate on the flight path**, so treat `targetPx` as a
   tunable to be *measured*, and record the value + the reference implementation consulted here
   when it is picked.
2. **This is a cell-footprint metric, not a geometric-deviation metric.** Nanite's SSE measures how
   far the simplified surface *deviates* from the original, in pixels. Ours measures how large a
   cell *is*, in pixels. Using cell size as an error proxy is legitimate and has precedent
   (3D Tiles' `geometricError` works this way), but it is **more conservative and not the same
   thing** — it will hold detail longer than a true deviation metric on flat surfaces, where a
   large cell may have near-zero deviation. Say so rather than implying parity with Nanite. A
   deviation term can be added later; the audit's point is that the doc must not conflate them.

Two required refinements:

1. **Appearance error term.** A cell whose children disagree wildly (a painted door in a stone
   wall, a glowing lamp in a dark room) should stay finer than geometry alone demands. Add a
   per-cell `appearanceError` baked at squash time (e.g. material-histogram entropy + max
   emissive delta) and bias the level selection by it. This is the voxel analogue of Nanite's
   geometric-error-only limitation, and it is cheap because it is precomputed.
2. **Per-consumer target.** The *same* function, different `targetPx`: main view ~1 px, shadow
   casters much coarser, reflection pass coarser still. One metric, N budgets — this is the
   mechanism that makes shadows cheap without a second geometry representation.

### 2.5 The crack rule — a mesher invariant, not a shader feature

**Do not stitch.** The field does not (`LargeWorldScalePlan.md:850`: DH and Veloren render the
far representation as a separate shell behind the near field and let depth win). Two layers:

1. **Within the LOD field — watertight by construction.** When brick A at L1 abuts brick B at
   L2, the coarser brick emits a **skirt** down the shared boundary. This is exactly
   `FarTerrainMesher`'s discipline (`kBelowSurfaceBias`) generalised. It is **L2-testable**: for
   every pair of adjacent bricks at differing levels, no ray along the boundary passes through.
2. **Between the LOD field and the far tier — the existing depth arbiter.** Already shipped and
   proven (`FarRepresentationProviders.md`).

**Fold in the open T-junction defect** (`RenderOptimization.md:489`) here. LOD boundaries are
T-junctions at scale; the three fix options listed there (merge-constraint / matched underlay /
edge skirts) should be decided once for both problems. Skirts probably win because LOD needs
them anyway — but that plan says "do NOT rush", and this plan defers to it.

---

## 3. Applying it to every subsystem

The unifying claim: **all five consume the same metric (§2.4) and the same submission path;
only static voxels consume the DAG.**

| Subsystem | What LOD means | Consumes | Difficulty | Notes |
|---|---|---|---|---|
| **Static voxels** | The brick DAG + the cut | metric, DAG, arenas, indirect | **Hard (long pole)** | The real system. §5 C3–C4 |
| **Shadows** | Same cut, coarser `targetPx` + cascades; later cached VSM pages | metric, DAG, indirect | **Medium** | Largest measured-cost lever (§0.3). A mostly-static voxel world is the ideal case for **cached static shadow pages** — most of the map never changes between frames. Note the 36-index constraint (`RenderCoordinator.cpp:1240`) survives; it is a *winding* requirement, orthogonal to LOD |
| **Water** | Two tiers already half-designed: CA sim region near, **baked water table as a static surface** far (superseded plan, now `docs/Water.md` §6 "render from spans"). Render tier = coarse surface quads on the same ladder | metric only | **Medium** | ⚠️ **Phase B/LOD is the user's other session — hands off** ([[water-system-v2]]). This plan supplies the metric and stops there |
| **Dynamic voxels / debris** | Not simplification — **aggregation**. Distant debris merges into fewer, larger particles, then a dust impostor. Simulation LOD (sleep, coarse solver) matters more than render LOD | metric, indirect | **Medium** | GPU particle path, ~10k cap. Nanite offers nothing here |
| **Characters / NPCs** | Bone-count LOD, then instanced impostors | metric only | **Low** | Ties into existing NPC sim-LOD |
| **Grass / foliage / far terrain** | Already exist with four independent hardcoded radii — just re-home them onto the shared metric | metric only | **Low (cleanup)** | Ships in C1; immediate visible win (consistent fade) at near-zero risk |
| **Physics / nav / audio** | The same ladder could drive collision + NPC sim LOD (occupancy grids already exist) | metric only | — | **Explicitly out of scope.** Named so it is not forgotten, not so it is built |

---

## 4. The direct answer: does every shader need a LOD layer?

**No — and doing that is the failure mode.** Nanite's LOD lives in the data structure and the
selection pass; its shaders barely know. Concretely, here is the *entire* shader-side surface
area of this plan:

1. **A shared include, `shaders/lod.glsl`** — ~50 lines: `projectedPixels()`, the level-select
   helper, and one `lodFade(level, dist)` used by the transition band. This is the only new
   cross-shader concept. ⚠️ `glslc` does not track `#include` deps — every consumer must be
   manually recompiled (`CLAUDE.md`, and the #1 hypothesis in the Phase-2 post-mortem).
2. **`static_voxel.vert`: one new branch, `scaleLevel == 3`** (the free code at `:76`), which
   scales the quad by `1 << lodLevel` and shifts UVs by the same factor. It is structurally the
   *existing* `scaleLevel == 0` merged-cube branch (`:170-178`) with a power-of-two multiplier —
   arbitrary-size faces already render correctly today.
3. **One `float` push-constant/instance bit for the cross-fade**, consumed as a dither or alpha
   in `voxel.frag`.

Everything else — the pyramid, the cut, the culling, the draw list — is **C++ and compute**, not
per-shader work. If a phase of this plan finds itself editing five fragment shaders, that is the
signal it took a wrong turn.

---

## 5. Phases

Each independently buildable, independently revertible, behind a static toggle for live A/B
(the `s_fineGreedyMerge` pattern at `ChunkRenderManager.h:61`). Red-before-green, solution-auditor
on every "works" claim, evidence archived in `docs/evidence/`.

**The ordering constraint:** C2/C3 must precede C4. Bricks multiply draw units 64× (§2.3); doing
the cut before GPU-driven submission converts blocker E into a much worse version of itself.

---

### C0 — Squash-operator prototype (offline, no Vulkan) — **IN PROGRESS (core landed 2026-07-29)**

> **Shipped:** `engine/{include,src}/core/LodBrick.{h,cpp}` + `tests/core/LodBrickTest.cpp`
> (11 tests). Red-before-green observed: the naive rules were implemented first and the two
> tests that matter were shown **FAILING** (`TavernOpeningsSurviveL2` — doorway filled in;
> `SkinMaterialWinsOverCore` — read stone instead of plaster), then fixed to green.
>
> **Model.** A `LodCell` carries **coverage** (solid microcubes, `kFullCoverage = 729 = 9³`),
> not a bool. That is what makes sub-cube-thin geometry representable at cube resolution — the
> thinnest authored wall is **1 microcube** = 81/729 = 11% coverage, so a boolean cell could not
> express it and a ≥50% rule erases it. `OccupancyRule::HalfThreshold` is kept **solely** so
> `HalfThresholdDeletesTheThinnestAuthoredWall` can document why it is rejected.
>
> **Finding 1 — a test that passed for the wrong reason.** The first `TavernOpeningsSurviveL2`
> used a 2×2-cube doorway *perfectly aligned* to the 2×2×2 parent group, so the group contained
> **no solid child at all** and passed against the naive rule trivially. Rewritten to a 1-cube
> (~1 m, real door width) opening that **shares a parent cell with wall**, with `ASSERT`s on the
> solid siblings so the precondition can't silently rot. This is the assumption-confirming-test
> failure mode; it survived my own review and was caught only by asking why a red test was green.
>
> **Finding 2 — openings and watertightness are in DIRECT CONFLICT, by construction.** Carving a
> door makes a coarse cell empty where fine geometry is solid — which is precisely the shape of a
> crack. Resolution: **a deliberate opening is not a crack**;
> `countWatertightViolations` skips cells flagged `preserveOpening`, pinned by
> `PreservedOpeningIsNotCountedAsACrack`. Accepted cost, stated plainly: the hole becomes **one
> coarse cell wide**, i.e. wider than the real door. Preserving the *read* of an opening beats
> preserving its exact width. If that reads badly at distance, the fix is a finer cut near
> openings — not reverting the rule.
>
> **Solution-auditor: VERDICT FAIL (2026-07-29) — it found a real shipped bug.** It
> mutation-tested every named property (each failed appropriately when the implementation was
> broken, with forced relinks to defeat the MSVC stale-exe trap) and confirmed 13/13 + the full
> suite. But:
> - 🐞 **`HalfThreshold` was silently miscalibrated beyond level 1.** `parentFull` was a fixed
>   `kFullCoverage * 8`, correct only for a single squash from level 0; at deeper levels children
>   already carry *accumulated* coverage, so **a 12.5%-solid volume passed a "≥50%" rule**.
>   Fixed: `parentFull = kFullCoverage << (3·(level+1))`. Pinned by
>   `HalfThresholdRejectsSparseVolumeAtDepth` (the auditor independently ran this case against
>   the pre-fix code and captured the failure).
> - 🐞 **The documented tie-break was unguarded.** Flipping `argmax`'s `>` to `>=` broke *zero*
>   tests. Added `MaterialVoteTieBreaksToLowerPaletteIndex`, and **verified by re-running that
>   exact mutation** that the new test now fails (then restored the source and diff-verified).
> - ⚠️ **`coverage` widened `uint32_t` → `uint64_t`**: 729·8⁸ = 1.22e10 overflows 32 bits at
>   about level 8, which is reachable on a real far-LOD pyramid.
> - ⚠️ **Documented, not fixed — surface-vote seam bias.** `exposedFaceWeight` reads neighbours
>   only inside the given volume, so edge cells count as fully exposed. Correct at a true world
>   edge, **wrong at a brick/chunk seam**, where the real neighbour is solid geometry in an
>   adjacent volume. Any skin straddling a seam gets a biased vote once wired to tiled data.
>   Fix later = a 1-cell apron or a neighbour-lookup callback.
> - ⚠️ **Residual trust the auditor flagged:** `countWatertightViolations` trusts
>   `preserveOpening` with no corroboration, so a genuine crack mislabelled as an opening by a
>   future upstream bug would be laundered silently. No test here can catch that, because the
>   flag is always hand-set correctly by the test author.
>
> **Still open in C0:** the **4³/8³/16³ brick-size sweep** (§2.3) — the ladder here is the
> 2×-per-level reduction; choosing the cull/draw unit needs the sweep on real settlement data.
> Also not yet done: driving the operator from real `ChunkBlobCodec` palette data (today's tests
> build synthetic volumes), and the L2 run against a generated tavern.

*(original C0 scoping follows)*
*De-risk the exact thing that killed Phase 5 twice, with zero renderer risk.*

Build `LodBrick` + `squash()` as a pure function over `ChunkBlobCodec` palette data. No GPU, no
pipeline, no shader. Deliverables:
- The occupancy rule (OR + `preserveOpening` hint) and the material rule (surface-area vote), each
  A/B-able against its naive alternative.
- A **watertightness checker** for adjacent bricks at differing levels.
- **The brick-size sweep (§2.3), which is now a C0 deliverable, not a pre-decided constant.**
  Run the squash at **4³ / 8³ / 16³** on a real generated settlement; record draw-unit count, pop
  visibility, and squash cost for each; pick with evidence and record the result in §2.3.

**Required validation layer: L2** (structural invariants measured on real generated output —
`ValidationLedger.md` discipline). Red tests, shown failing on a naive OR/volume-vote implementation:
| Red test | Asserts |
|---|---|
| `TavernOpeningsSurviveL2` | A generated v2 tavern squashed to L2 still has its door and window openings (via a `scan_micro`-style per-column signature diff, not a face count) |
| `ThinWallSurvivesL2` | **No wall thickness the generator actually emits vanishes.** ⚠️ *Corrected 2026-07-29: the draft targeted "a 2-microcube partition", which **no style produces** — 2 micro is the timber_cottage **exterior**, not a partition.* The real authored set (`resources/structure_styles.json:10-11,59-60,108-109`, 1 microcube = 1/9 m ≈ 0.111 m): **1 micro (0.111, timber_cottage interior — the thinnest thing in the engine and the actually-at-risk case)**, 2 micro (0.222, timber_cottage exterior), 3 micro (0.333, stone_manor interior), 4 micro (0.444, timber_cottage foundation), 6 micro (0.667, stone_manor exterior), 9 micro = 1 cube (castle interior), 27 micro = 3 cubes (castle exterior). Test **every one**, not an invented number. The rule that would delete the 1-micro wall is the same one that would let a character spawn inside geometry |
| `SkinMaterialWinsOverCore` | Plaster-skinned stone wall squashes to plaster, not stone |
| `AdjacentLevelsWatertight` | No gap along any L(n)/L(n+1) brick boundary |
| `SquashIsDeterministic` | Same input → bitwise-identical output (required for persistence and for resume) |

**Stress axes** (`CLAUDE.md` standing phase): a 10-story tower (crosses the y=31→32 vertical chunk
seam at every level); a full settlement (repetition + template reuse); degenerate bricks (all air,
all solid, single voxel, 1-voxel-thick plane); the 1:1 Middle-earth world (scale).

**Gate:** all five red tests green; both A/B rules measured on a real settlement with the result
recorded here; auditor VERDICT.

---

### C1 — `LodService`; re-home the existing cutoffs — **✅ NAMED SUBSYSTEMS DONE (2026-07-29)**

> **Shipped:** `engine/include/core/LodService.h` (header-only, the single metric) +
> `tests/core/LodServiceTest.cpp` (**22 tests**, mutation-verified) + `LodBrickTest` (13, C0) + `shaders/lod.glsl`
> (staged, see below) + `POST /api/debug/screen_space_lod` live A/B toggle.
>
> **The metric.** Projected size in pixels, calibrated so that at the **reference config the
> engine's hand-tuned numbers were picked at — 1600x900 (`EngineConfig.h:19-20`), fovY 45 deg
> (`Camera.h:90`) — the correction is EXACTLY 1.0, a provable no-op.** Away from it, everything
> scales together. `updateLodView()` runs once at the top of `drawFrame()`, before any consumer,
> so nothing reads a stale scale.
>
> **C1's four named subsystems - all resolved:**
>
> | Subsystem | Outcome |
> |---|---|
> | **Character LOD (35/80) + cull (400)** | OK Re-homed via `LodService::characterLodLevel`. Was blind to FOV/resolution - auditor-confirmed by tracing `distSq = glm::dot(rel,rel)` with zero projection terms. |
> | **Grass radius (48)** | OK Re-homed via `RenderCoordinator::effectiveVegetationRadius`. The `+27.8` chunk half-diagonal pad is **geometric and deliberately NOT scaled** - shrinking it below the true half-diagonal would reintroduce edge-of-chunk popping. |
> | **Foliage radius (512)** | OK Same path. |
> | **Shadow cull (`cullRadius + 160`)** | **RE-SCOPED - it does not need re-homing, and changing it would be WRONG.** `shadowCullRadius` is the **view-frustum bounding-sphere radius** computed from the real projection matrix (`RenderCoordinator.cpp:1626-1651`), so it is *already* FOV/aspect-aware. The `+48` caster margin and `+160` cull pad are **geometric caster reach** and must stay in world units. The original gate item was based on my misreading of a hardcoded-looking constant. |
> | **Far-terrain horizon (2048 + 512-doubling bands)** | OK Re-homed via `FarTerrainManager::Params::viewScale`, applied in `computeRingsFor()`. Ring *strides* (2/4/8) unchanged - only absolute distances move, so band proportions are preserved. |
>
> **Every one is mutation-verified**, because the first two attempts were not: tests re-derived the
> formula locally instead of calling production code, so **disabling a feature entirely left the
> whole suite green** (caught twice by solution-auditor - on the character path, then again on the
> vegetation path). The fix pattern now used everywhere: the real logic is a **static, public, pure
> function** (`characterLodLevel`, `effectiveVegetationRadius`, `computeRingsFor`,
> `viewScaleVsReference`) that both the render path and the tests call. Each was then re-mutated to
> confirm the test goes red.
>
> **Characterization tests came first**, per the audit finding that these systems had **zero
> coverage**: flipping `s_shadowFrustumCull`, or setting `m_charLod1Distance` to 3500, previously
> left all 3080 tests green.
>
> **Runtime-verified** (`docs/evidence/lod_c1_runtime_ab_20260729.txt`): live A/B on LodBench at
> 1600x900 reports `lod_view_scale: 1.0`, and ON vs OFF gives **identical `visible_chunks` (45)**
> with grass/foliage/static timings within noise - the no-op claim holds on the real engine, not
> just in the formula.
>
> **`shaders/lod.glsl` is STAGED, not integrated - no shader includes it.** Honest finding that
> shrinks section 4's scope: because grass/foliage radii are corrected **CPU-side**, the in-shader
> fade inherits the correction automatically and needs no shader change. A shader-side metric is
> only required where a shader computes its own fade independently (far-terrain ring fade, water
> LOD) - that is C4/C5 work, not C1.
>
> ### 🐞 A real integration bug the unit tests could NOT catch (2026-07-29)
> A solution-auditor pointed out that all my runtime evidence was captured at
> `lod_view_scale == 1.0` — where the correction is **inert by construction** — so it proved
> far terrain *works*, not that the *scaling path* works. Added
> `POST /api/debug/screen_space_lod {"force_scale":N}` to pin a non-1.0 scale and swept it live.
>
> **First sweep FAILED:** far-tile counts were **byte-identical at 0.5 / 1.0 / 2.0**
> (148 resident / 23 drawn every time). Root cause: `FarTerrainManager::update()` only calls
> `refreshWantedSet()` when the **camera** has moved past `kRefreshDistance`. A `viewScale`
> change moves the entire horizon but invalidated nothing, so **a resolution change would have
> silently done nothing until the player happened to walk far enough.** Every unit test passed
> throughout — `computeRingsFor()` was correct in isolation; the *invalidation* was missing.
> Fixed with `m_lastRefreshViewScale`, which forces a refresh when the scale changes.
>
> **Re-test PASSES, with the right magnitude** (`docs/evidence/lod_c1_scaling_path_live_20260729.txt`):
>
> | forced scale | live `lod_view_scale` | `far_tiles_resident` |
> |---|---|---|
> | 1.0 | 1.0 | 148 |
> | **2.0** | **2.0** | **592** |
> | 0.5 | 0.5 | 88 |
> | 1.0 (return) | 1.0 | 156 |
>
> **592 = 148 x 4 exactly** — the horizon radius doubles, so the covered annulus area goes as
> r², which is the physically correct response. The return to 1.0 gives 156 rather than 148
> because resident tiles are LRU-retained and evict lazily; `far_drawn` drifts similarly. Not an
> exact round-trip, and stated as such.
>
> **Lesson (the reason runtime verification is not optional):** a metric can be provably correct
> in unit tests and still be **dead in the engine** because nothing invalidates the cache that
> consumes it. Verifying at the config where a feature is a no-op proves nothing at all.
>
> ### ⚠️ The shipped correction is RESOLUTION-only today, not FOV-aware
> Auditor finding: `updateLodView()` was called with only the viewport height, letting
> `fovYDegrees` default to the reference constant — so the live path never read the camera's FOV,
> despite tests presenting the metric as FOV-aware. Harmless *today* because
> `Camera::getProjectionMatrix()` also hardcoded 45 deg, but it would have silently stopped
> tracking FOV the moment `zoom` was wired into the projection. Fixed by introducing
> `Camera::kFovYDegrees` / `getFovYDegrees()` as the single source of truth, now read by both the
> projection matrix and `updateLodView()`. FOV-awareness is still only *unit*-proven; it cannot be
> exercised end-to-end until FOV actually becomes variable.

> **Explicitly NOT re-homed, with reasons** (so nobody assumes C1 covered them):
> - **Chunk render distance** - four different operative defaults (`EngineConfig` 256/320,
>   `WorldInitializer` 96/128, `GameSettings` 256); needs a config-precedence decision first.
> - **Chunk streaming `loadDistance`/`unloadDistance`** (`ChunkManager.h:99-100`) - residency, not
>   appearance. Scaling it with resolution would change RAM/IO with window size.
> - **Character *update* LOD** (tiers 30/60/120/220) and the **NPC update gate** - CPU-budget
>   simulation LOD, not a render fade; genuinely unresolved which it should track, so untouched.
> - **Occlusion BFS bound (512)** - a cost bound on a graph walk, not a fade.
> - **Water sim region** - the user's other session owns Water Phase B/LOD.

*(original C1 scoping follows)*

### C1 (original scoping) - `LodService` + `lod.glsl`; re-home the existing cutoffs
*Cheap, low-risk, immediately visible. Delivers the "unified" half of the request before any
of the hard work.*

One CPU service + one shader include implementing §2.4. Then convert far-terrain `maxDistance`,
grass `radius`, foliage `radius`, and the shadow `cullRadius + 160` to derive from it. No geometry
changes at all.

**Gate:** bitwise-identical output at the tuned-equivalent settings (the `RegionArenaPlan` A2
discipline); all four subsystems provably fade from one metric (grep: no remaining hardcoded
distance constant outside `LodService`); a resolution/FOV change moves all four consistently —
which is a **latent bug fix**, since today they do not.

---

### C2 — GPU-driven culling: two-phase Hi-Z + `vkCmdDrawIndexedIndirectCount` — **increment 0 DONE**

> ### 🔧 C2.0 — capability foundation (2026-07-29). **The plan's premise was false.**
> C2 is described here as "portable Vulkan 1.2, no mesh shaders", as though 1.2 were the baseline.
> It was not. Measured before touching anything:
> - `appInfo.apiVersion` was pinned to **`VK_API_VERSION_1_0`** (`VulkanDevice.cpp:230`)
> - the device extension list was **`VK_KHR_swapchain` only**
> - **`multiDrawIndirect` was never requested** — it is a core-1.0 *optional* feature, and without
>   it `vkCmdDrawIndexedIndirect` with `drawCount > 1` is **invalid usage**
>
> So `gl_DrawID` (1.1 core) and `vkCmdDrawIndexedIndirectCount` (1.2 core) were both unavailable and
> multidraw was illegal. **C2 was not merely unimplemented — it was unexpressible.** No phase of this
> plan recorded that.
>
> **Shipped:** request the highest API the loader offers, capped at 1.2, with a clean fallback to 1.0
> when `vkEnumerateInstanceVersion` is absent; enable `multiDrawIndirect` and
> `drawIndirectFirstInstance` when supported (the latter is what lets one multidraw select each
> chunk's arena span via `firstInstance`); resolve and log the capability set at device creation.
> Queries added: `supportsMultiDrawIndirect()`, `supportsDrawIndirectFirstInstance()`,
> `supportsShaderDrawParameters()`, `supportsDrawIndirectCount()`, and
> `supportsGpuDrivenSubmission()` for later increments to gate on.
>
> **Measured on this machine** (`docs/evidence/lod_c2_capabilities_20260729.txt`):
> ```
> Instance API version requested: 1.2 (loader offers 1.4)
> C2 capability set: multiDrawIndirect=yes drawIndirectFirstInstance=yes
>                    gl_DrawID(1.1)=yes drawIndirectCount(1.2)=yes deviceApi=1.4
> ```
> **Regression check:** same baked world renders **250,172 faces — identical to the baseline** —
> shadow 3.59 ms, static geometry 3.35 ms, 45 visible chunks, no new validation errors. Raising the
> requested API version changed nothing observable, as Vulkan's backward compatibility guarantees.
> (Two errors in the log are **pre-existing and unrelated**: the known undersized reflection
> descriptor pool, and a missing `phyxel` Python module in this project. Neither touches code I
> changed.)
>
> ### 🔧 C2.0b — CORRECTION to my own C2.0 (same day)
> The first version of C2.0 reported `gl_DrawID(1.1)=yes` **from the API version alone**. That was
> wrong: in Vulkan 1.1 `shaderDrawParameters` is a **feature that must be explicitly enabled**, and
> availability ≠ enabled. A shader using `gl_DrawIDARB` against a device that never enabled it is
> invalid — so my capability report was misleading in exactly the way that would have produced a
> mystifying shader failure in C2.1. Now queried via
> `VkPhysicalDeviceShaderDrawParametersFeatures` + `vkGetPhysicalDeviceFeatures2`, and **enabled**
> through the `VkDeviceCreateInfo::pNext` chain. Re-probed: capability set unchanged
> (all yes), device created cleanly with the feature chain, **250,172 faces — still identical**.
>
> ### 📐 C2.1 DESIGN — settled, with the constraints that actually bind
> Investigating the implementation surfaced three constraints the plan never stated. Recording them
> rather than discovering them mid-rewrite:
>
> 1. **A single global multidraw is impossible.** Each chunk currently needs its own
>    **vertex-buffer bind** (its arena span) and its own **push constants** (chunk origin). An
>    indirect draw can vary neither. But `ArenaSpan::blockId`
>    (`ChunkArenaAllocator.h:27`) means chunks already know which arena block they live in, and
>    chunks sharing a block share a buffer. **So the unit is one multidraw per arena BLOCK**, not
>    one per frame — exactly Sodium's RenderRegion model, and precisely what
>    `RegionArenaPlan.md` §3.3 already lists as its remaining "4.3b multidraw" item. The two plans
>    meet here.
> 2. **The per-chunk origin must move to an SSBO indexed by `gl_DrawIDARB`**, with
>    `firstInstance = span.offset / sizeof(InstanceData)` selecting each chunk's slice. That needs
>    `drawIndirectFirstInstance` *and* `shaderDrawParameters` — both only enabled as of C2.0/C2.0b.
>    The `lightSpaceMatrix` push constant can stay: it is constant across all draws.
> 3. **Face-direction bucketing (`s_faceDirCull`) conflicts with it.** Today a chunk can emit up to
>    6 sub-range draws chosen per-camera on the CPU. Under multidraw those become separate indirect
>    commands (fine) or must be disabled in the GPU-driven path initially (simpler). Decide before
>    implementing; do not discover it at pixel-diff time.
>
> **Expected win, from measured numbers:** the shadow pass draws ~131 chunks; in a LodBench-scale
> scene those live in a handful of arena blocks, so ~131 draws collapse to ~2-4 multidraws. That is
> the ~17 ms of per-draw overhead `RenderDensityPlan.md` attributes to draw count — **though note
> that attribution is itself unconfirmed** (M1 retracted), so C2.1 must be measured, not assumed.
>
> **Recommended target: the SHADOW pass first**, not the main pass. It is 75% of the frame, it is
> depth-only (no material/lighting interactions), and its pipeline has `setLayoutCount = 0`
> (`ShadowMap.cpp:332`) — no existing descriptors to disturb when adding the SSBO.
>
> ### 🟨 C2 RESULT (2026-07-30): the multidraw WORKS; a 40x draw reduction changed NOTHING measurable.
> **This does not falsify C2's premise — it is one negative result against it, on one scene.** Evidence:
> `docs/evidence/lod_c2_multidraw_live_20260729.txt`.
>
> **Unblocked** by fixing the arena alignment: `ChunkArenaAllocator::kAlignment` **256 → 768**
> (`lcm(256, 24)`), with `static_assert`s pinning both properties it must satisfy — 256-byte GPU
> alignment AND 24-byte instance-stride addressability. One brittle allocator test that hardcoded
> `kTestBlock / 512` was made alignment-agnostic; all arena tests green.
>
> **Correctness — verified by pixel diff, after it initially FAILED.** First A/B showed **14.9% of
> pixels differing**. Cause: **`gl_DrawIDARB` restarts at 0 for every `vkCmdDrawIndexedIndirect`
> call**, but the origins array was filled as one global run across all batches, so every batch
> after the first read the wrong origins. Fixed with a per-batch `drawIndexBase` push constant
> (push constants may be re-recorded between indirect calls; vertex bindings may not). Re-tested
> with the sun paused and grass/foliage off, **against an OFF-vs-OFF control**:
>
> | comparison | differing px | >2 delta |
> |---|--:|--:|
> | CONTROL — OFF vs OFF | 106 / 1,440,000 | 82 |
> | TEST — OFF vs ON | **65** / 1,440,000 | **51** |
>
> The test difference is *below* the control: the multidraw reproduces the legacy image within the
> scene's own frame-to-frame noise. **Note the first (14.9%) failure was found only because the
> diff was run — the stats matched perfectly (identical instance counts) while the image was wrong.**
>
> ### ⚠️ Performance — my first claim here was WRONG and is retracted
> I originally reported **"3.5% SLOWER"** from **n=6**. A solution-auditor failed it, correctly:
> the ON series contained 2 spikes (6.9, 10.2 ms) matching this metric's *already documented*
> bimodal signature, the median said −3.5% while the mean said −54%, and n=6 violated the **n≥15**
> floor a *previous* audit had already imposed on this exact GPU scope. Choosing the statistic
> after the fact is how a null became a "regression".
>
> The audit also found a **real bug** behind those spikes: the origin and indirect buffers were
> **single-buffered against `MAX_FRAMES_IN_FLIGHT = 2`** — a write-after-read hazard, rewritten
> from the CPU each frame while the previous frame could still be reading them. The passing pixel
> diff had simply got lucky on timing. **Fixed:** both buffers and their descriptor sets are now
> per-frame.
>
> **Re-measured properly** (`tools/c2_shadow_ab.py`, evidence
> `docs/evidence/lod_c2_shadow_ab_n20_20260730.jsonl`): n=16 per state, **true interleaving**
> (OFF,ON,OFF,ON…), per-sample activation recorded, both statistics reported.
>
> | | median | mean | min | max | spike rate | draws | multidraw calls |
> |---|--:|--:|--:|--:|--:|--:|--:|
> | OFF (legacy) | 3.432 ms | 3.708 | 3.310 | 5.369 | 6% | 362 | 0 |
> | ON (multidraw) | 3.448 ms | 3.545 | 3.351 | 5.083 | 6% | 362 | **9** |
>
> **ON-path activation verified on every ON sample** (9 indirect calls, 0 on every OFF sample) —
> and note the harness first reported these *inverted*, because reading the counters from the
> state-changing call returns the previous frame's stats. Fixed to read them after settling.
>
> **Result: median +0.5%, mean −4.4% — opposite signs, both far inside the OFF noise band. NO
> EFFECT.** The per-frame buffering fix also removed the spike asymmetry (ON was 33% spikes before,
> 6% now — equal to OFF).
>
> ### 🟨 What this does and does not establish
> **Does:** collapsing **362 draw calls to 9 (40×)** produced **no detectable change** in GPU shadow
> time on this scene. Per-draw submission overhead is not a measurable cost here. This is the second
> independent failure to find the effect `RenderDensityPlan.md` attributes ~17 ms to — and unlike
> M1 (a correlation, retracted) this one is a direct **intervention**.
>
> **Does NOT:** prove the premise false in general. One scene, whose shadow pass is ~3.4 ms — not
> the 24–26 ms regime the original attribution came from. A null at 3.4 ms cannot rule out an effect
> at 25 ms. The honest status of "batch the shadow draws" is **unproven, with one negative result
> against it**, not "falsified".
>
> **Consequence for the rest of C2:** compute culling and Hi-Z occlusion should be justified by
> whether they reduce **work** (instances, fragments, fill), not draw calls — because the draw-call
> lever has now been pulled to its limit here and moved nothing.
>
> **What C2 delivered regardless:** a correct, pixel-verified GPU-driven submission path behind
> `s_gpuDrivenShadow` (default OFF), the capability foundation, the arena alignment fix, and a
> measured answer to the question the plan had only assumed.

> ### 🔴 C2.1 IMPLEMENTED, then BLOCKED by an arena alignment contract (2026-07-29)
> Built end to end and it does not work — for a reason worth recording, because it invalidates the
> approach as designed rather than being a bug in it.
>
> **Shipped and working:** `shaderDrawParameters` enabled; `shadow.vert` reads the per-draw origin
> from a `std430` SSBO indexed by `gl_DrawIDARB`, behind a push-constant flag (**0 = legacy, exactly
> the old expression**); `ShadowMap` owns the origin SSBO + a host-mapped indirect command buffer +
> its descriptor set (its layout went `setLayoutCount 0 → 1`); `RenderCoordinator` groups shadow
> casters by arena `VkBuffer` and issues one `vkCmdDrawIndexedIndirect` per group;
> `POST /api/debug/gpu_driven_shadow` A/B toggle, **default OFF**.
>
> **The blocker:** `firstInstance` addresses instances by **stride**, so a chunk's arena span offset
> must be an exact multiple of `sizeof(InstanceData)` = **24 bytes**. Arena spans are
> **`kAlignment` = 256**-byte aligned (`ChunkArenaAllocator.h:48`), and **256 % 24 == 16**. Span
> offsets are therefore generally *not* stride multiples, `offset / 24` **truncates**, and every
> chunk reads the wrong instances. The two paths also differ in how they bind: legacy binds the
> vertex buffer at the exact byte offset with `firstInstance = 0`; the GPU path binds at 0 and
> relies on `firstInstance`. Those are equivalent **only** when the offset is stride-aligned.
> ⚠️ **The pre-guard symptom I reported (shadow 3.3 ms → 11.6 ms with wrong geometry) is NOT
> ARCHIVED** — it was observed in-session and written down as prose, which this repo's own standing
> rule forbids citing. Treat it as **anecdotal, not evidence**. The *mechanism* above is
> independently verified from source (alignment constant, struct size, and the two divergent bind
> paths); the *number* is not reproducible from anything on disk.
>
> **A hard guard now refuses the path** and falls back to the legacy loop rather than render silent
> garbage, logging the offending offset and the fix. Verified: with the toggle ON the guard fires and
> timings are identical to OFF (3.396/3.356 vs 3.359/3.331 ms), faces unchanged at 250,172
> (`docs/evidence/lod_c2_1_shadow_multidraw_20260729.txt`).
>
> **This is a cross-plan constraint neither plan recorded.** `RegionArenaPlan.md` chose
> `kAlignment = 256` for buffer-binding requirements; nothing there anticipated that a future
> multidraw would need span offsets to be *instance-stride* addressable. Options, in order of
> preference:
> 1. **Align arena spans to `lcm(kAlignment, stride)` = 768 bytes.** One-line allocator change,
>    wastes ≤768 B per chunk (negligible against the ~586 KB/chunk buffer floor). **But it changes
>    shipped, gated infrastructure** (A0-A4, default ON, verified at 10,609 resident chunks), so it
>    needs its own re-verification of the A3/A4 gates — not a tail-of-session change.
> 2. **Pad `InstanceData` 24 → 32 bytes** (a power of two divides 256 cleanly). Costs +33% instance
>    memory across every chunk and touches the dual-struct packers. Worse.
> 3. Per-chunk bind at the span offset with `firstInstance = 0` — correct, but then there is no
>    multidraw across chunks, which defeats the entire purpose.
>
> **Recommended: option 1, as a scoped change to `RegionArenaPlan` with its gates re-run.** Until
> then C2.1 is code-complete but inert, and the toggle is safe to leave shipped because the guard
> makes enabling it a no-op rather than a corruption.
>
> **What is NOT verified, stated plainly:** the `useChunkDataSsbo == 1` branch has **never executed
> with real data** — every archived run shows `shadow_multidraw_calls: 0`, because the guard always
> fires. Its correctness rests on source review alone. The missing measurement is a run where the
> guard does *not* fire (a synthetic stride-aligned arena, or after the option-1 allocator fix)
> followed by a pixel/geometry diff against the legacy path. `flag == 0` being byte-identical to the
> old expression IS verified, by direct source comparison.
>
> **Guard coverage added after the audit:** the check is now the pure, public
> `RenderCoordinator::spanIsStrideAddressable(offset, stride)`, pinned by
> `LodServiceTest.C21GuardRejectsStrideMisalignedArenaSpans` — which asserts the real 256/24 case
> fails, that `lcm(256,24)=768` passes, and that a zero stride cannot divide-by-zero.
> **Mutation-verified:** replacing the body with `return true` turns the test red. Before this, the
> guard had zero coverage and deleting it would have left all 3110 tests green.
>
> **Still to do in C2** (the actual culling — none of it started): move the per-chunk origin out of
> push constants into an SSBO indexed by `gl_DrawID` so bindings stop varying per draw; build the
> indirect command buffer; the depth pyramid; two-phase Hi-Z occlusion; `s_gpuDrivenCull` toggle with
> a pixel-identical A/B. **The per-chunk vertex-buffer bind + push constant is the real blocker** —
> an indirect draw cannot vary either, so that indirection has to be removed before a single
> multidraw is possible at all.

*(original C2 scoping follows)*

### C2 (original scoping) — GPU-driven culling: two-phase Hi-Z + `vkCmdDrawIndexedIndirectCount`
*The long pole, already independently justified.*

`LargeWorldScalePlan.md` §5.2 item 5 verbatim. Region arenas (shipped) are the prerequisite shape.
Retires blocker E structurally. Portable Vulkan 1.2 — **mesh shaders stay an optional NVIDIA
backend, never a dependency.**

**Gate:** the A4 stress world (10,609 resident chunks) at ≥ its current 100–124 FPS with the
per-frame CPU chunk scan *gone* from the profile; zero popping across the two-phase boundary
(the whole point of two-phase is no readback and no popping); no new validation VUIDs.

---

### C3 — Persist the pyramid; brick-level cull unit
Build the L1–L5 pyramid at squash time; persist keyed by **(x, y, z, lod)** — godot_voxel's
schema, the cleanest open reference (`LargeWorldScalePlan.md:857`). Own tables, own compression
policy (write-rarely/read-often → LZMA-class), per Distant Horizons. Terrain LOD comes from
`CoarseWorldModel` **directly**; only *edited* chunks and structures are downsampled — the field's
#1 correction (`LargeWorldScalePlan.md:756`).

**Gate:** a settlement is visible in the mid field (which far terrain structurally cannot show —
it only knows generator terrain); pyramid build cost does not regress chunk-edit remesh beyond the
recorded ~40–50 ms/chunk budget (`RenderOptimization.md:409`); destruction churn measured
explicitly (§6).

---

### C4 — The cut — **FIRST WORKING CUT (2026-07-30)**

> **The engine now renders chunks at a chosen LOD level.** This is what the plan exists for.
> `engine/{include,src}/core/LodChunkMesh.*` + `tests/core/LodChunkMeshTest.cpp` (7 tests) +
> `scaleLevel == 3` branches in `static_voxel.vert` AND `shadow.vert` + `Chunk::setLodFaces` +
> `POST /api/debug/lod_level`.
>
> **How it works:** chunk voxels → level-0 `LodVolume` → squash to level N (C0) → emit one quad
> per exposed coarse-cell face, encoded as the previously-**reserved** `scaleLevel = 3` with
> `lodLevel` in bits 20-22. The shaders expand each to a 2^N-cube quad. `shadow.vert` carries the
> identical branch — without it a coarse chunk casts a 1×1 shadow.
>
> **Test anchor:** at level 0 the coarse path must reproduce the fine surface *exactly* (an 8³ box
> → exactly 384 unit faces). Without that identity the coarse path could be arbitrarily wrong and
> still "look plausible". Also pinned: interiors not emitted, hollow shells stay hollow, empty
> chunks invent nothing, cell origins stay cell-aligned, encoding round-trips at every level.
>
> **Live result** (`docs/evidence/lod_c4_cut_live_20260730.txt`, 380 chunks):
>
> | level | cell | faces | vs fine |
> |---|---|--:|--:|
> | 0 (fine, greedy-merged) | 1 cube | 250,845 | — |
> | 1 | 2 cubes | **409,908** | **1.6× WORSE** |
> | 2 | 4 cubes | 104,932 | 2.4× fewer |
> | 3 | 8 cubes | **27,554** | **9.1× fewer** |
>
> Visually confirmed at level 3: hills render as huge stepped blocks, the village collapses to a
> coarse blob, same camera and paused sun.
>
> **Three honest findings:**
> 1. **Level 1 is worse than fine.** The fine path is greedy-merged; the coarse path emits one
>    quad per cell face with **no merging**. Coarse meshing needs greedy merge to pay below level 2.
> 2. **Coarse levels measured SLOWER here (static geom 2.65 → 6.67 ms at level 3 despite 9× fewer
>    faces) — and that is a methodology artifact, not a defect.** The debug route applies one level
>    **globally**, including chunks right in front of the camera, where an 8-cube quad covers
>    enormous screen area. **LOD trades face count for fill area, so it only pays at distance.**
>    This run is itself the argument that the cut must be **distance-driven**
>    (`LodService::levelForDistance`) rather than global.
> 3. `total_visible_faces` does not track the swap — it is computed from `chunkStats` elsewhere.
>    Use the route's `faces_before`/`faces_after`.
>
> ### 🐞 Solution-auditor: FAIL — and it found a defect that would have deleted every structure
> **`volumeFromChunk` was blind to sub/microcube content.** It marked a cell solid only via
> `getCubeAt()`, which reports **full-cube** presence. A cell holding *only* subcubes/microcubes —
> exactly how the generator authors thin walls (`timber_cottage interior_wall` is **one
> microcube**) — read as EMPTY, so the coarse mesh emitted **zero faces** and every structure
> silently vanished at distance. The auditor wrote and ran a probe proving it (0 faces for a
> subcube-only wall).
>
> This directly contradicted **this plan's own §2.1**: sub/micro detail is the cube's *appearance*,
> carried as `coverage`. `LodCell::coverage` exists precisely for this and the first implementation
> never populated it. The screenshots would not have revealed it — distant structures simply would
> not be there.
>
> **Fixed and mutation-verified.** `volumeFromChunk` now folds sub/micro occupancy into coverage
> (1 subcube = 27 microcubes, 1 microcube = 1), by iterating the chunk's own
> `getStaticSubcubes()`/`getStaticMicrocubes()` containers. Red-before-green:
> `MicrocubeOnlyContentIsVisibleToTheCut` and `CoverageReflectsSubCubeFillLevel` were shown failing
> (0 faces / 0 coverage), then green; re-mutating the fix turns them red again.
>
> ⚠️ **Performance footnote worth keeping:** the first, correct-but-naive fix probed all 32,768
> cells × 756 sub/micro slots and took **79 seconds** for ten unit tests. Iterating the chunk's own
> containers is O(actual voxels) and returns it to **130 ms**. A correct algorithm can still be
> unusable.
>
> ### ⚠️ Two claims corrected by the same audit
> - **The level-0 "reproduces the fine surface exactly" anchor is weaker than stated.** One
>   assertion is real (a hand-derived 384 faces, mutation-confirmed); the other compares
>   `LodChunkMesh` to **its own** `fineFaceCount()` — the class against itself. **No test compares
>   the coarse output to the engine's actual greedy-merged fine mesher.** The honest claim is
>   "matches a hand-derived unmerged face set", not "matches the renderer".
> - **The "slowdown is pure methodology" explanation was incomplete.** `setFacesFromLod`
>   deliberately zeroes `m_dirRangeOffsets`, which **disables face-direction bucketing** for every
>   LOD chunk and forces a full unculled draw each frame. That is a real, code-level second cause
>   the write-up omitted. The fill-area argument still stands, but it is not the whole story, and
>   "NOT a defect" remains **unfalsified** until an A/B re-enables bucketing for LOD faces.
>
> **NOT measured: any performance win.** By construction this run cannot show one. The missing
> measurement is distance-driven selection plus a fixed-pose A/B with the coarse geometry actually
> far away. That is the immediate next step, and C1's metric already computes the level.

*(original C4 scoping follows)*

### C4 (original scoping) — The cut
Per-brick level selection over the pyramid; coarse faces via `scaleLevel == 3`; skirts at level
boundaries; dithered transition band.

**Gate:** the `LargeWorldScalePlan.md` Phase 5 gate (scripted 20 km flight, ≥120 FPS Release, no
z-fighting) **plus** an automated **no-pop assertion**: frame-to-frame luminance delta along a
fixed flight path stays under budget (§1.1 — an opinion about a screenshot is not evidence).

---

### C5 — Distance-driven selection — **WORKS; STRIPING DEFECT ROOT-CAUSED AND FIXED (2026-07-30)**

> **The join between C1's metric and C4's cut exists and converges.** `updateChunkLod()` asks
> `LodService::levelForDistance` what cell size still earns its pixels at each chunk's distance,
> then re-meshes changed chunks under a per-frame budget (a full re-mesh is ~40-50 ms, so an
> unbounded budget would turn camera motion into a stutter storm). Hysteresis: a chunk only moves
> when the metric disagrees by a full level. `POST /api/debug/distance_lod`, **default OFF**.
>
> **Measured at the zoomed-out pose** (whole 544-unit world in frame, render distance 2048):
> `chunks_by_level = {1: 21, 2: 359}` — **the level genuinely varies with distance** — total chunk
> faces **250,172 → 122,218 (2.05×)**, and `rebuilt_last_frame = 0` across 20 consecutive polls,
> so the hysteresis holds and nothing thrashes.
>
> **No performance effect measurable** (n=8 interleaved, static-geometry GPU scope): OFF median
> 3.890 / mean 4.444; ON median 4.340 / mean 4.304 → **signs disagree, both inside noise, no effect
> claimable.** That is now the *third* consecutive null: halving the face count changed nothing,
> exactly as collapsing 362 draw calls to 9 changed nothing. **This scene is not geometry-bound**,
> and no amount of LOD will make it faster. Demonstrating the user's goal ("infinite render
> distance, zoom out, little cost") needs a world large enough that face count actually dominates —
> LodBench's 380 chunks is not that world.
>
> ### ✅ THE STRIPING DEFECT — ROOT-CAUSED AND FIXED (2026-07-30)
> Full evidence: [`docs/evidence/lod_c5_striping_fix_20260730.txt`](evidence/lod_c5_striping_fix_20260730.txt).
>
> **Root cause: `ChunkRenderManager::setFacesFromLod()` never updated `numInstances`.** That
> counter was assigned in exactly one place — the end of the fine rebuild
> (`ChunkRenderManager.cpp:246`) — so a coarse swap left the **fine** mesh's count in place while
> the renderer drew `getNumInstances()` instances from the **coarse** buffer:
> - coarse faces **>** stale count → the tail is never drawn → **see-through stripes**;
> - coarse faces **<** stale count → instances past the valid data are drawn from stale memory.
>
> Flat terrain is the worst case because it greedy-merges to very few fine faces: the unit test's
> flat slab merges to **6** fine faces while its level-1 coarse mesh needs **640** — 99% of the
> geometry would go undrawn. **Fix (2 lines):** set `numInstances = faces.size()` and
> `needsUpdate = true` in `setFacesFromLod`.
>
> **My earlier "narrowed to INTER-chunk" conclusion was wrong.** `HeightfieldHasNoHolesFromAbove…`
> passing only proves *one chunk's coarse mesh* has no holes; it says nothing about how many of
> those faces are **drawn**. The truncation is per-chunk but *looks* inter-chunk: `emitFaces`
> iterates x→y→z, so cutting the list drops a contiguous, chunk-aligned slab.
>
> **A second, independent defect** found on the way: `emitFaces` had `if (mat.empty()) continue;`,
> which **dropped a solid cell** whose material failed to resolve. Now falls back skin → bulk →
> `"Default"`. Not the striping cause (this terrain is all full cubes) — it would bite sub/micro
> structures.
>
> **Red→green proof, unit and runtime.** Unit: `SetLodFacesUpdatesDrawCount` failed at
> `numInstances` 6 vs expected 640/160/48. Runtime (L4) on `PhyxelProjects/LodStripe`, a world
> **generated by the engine's own `WorldGenerator`** (`"type":"Flat"`, seed 424242) — *not*
> hand-placed voxels — with the camera pose read back from `GET /api/camera` on both runs
> (`(0,70,110)`, yaw −90, pitch −30) and an identical face count (54,066) at level 1:
> - reverted build → `screenshot_20260730_152234_662.png`: stripe gaps, floating slabs, sky
>   visible through the terrain;
> - fixed build → `screenshot_20260730_152743_876.png`: solid, continuous.
>
> **Level matters when reproducing:** on this world level 1 is the *truncating* case
> (54,066 coarse > 30,799 fine); level 2 (14,350) is the *over-draw* case and visually **hides**
> the bug. Do not conclude "level 2 looked fine" means fixed.
>
> **A third defect, found closing the audit gap:** `POST /api/debug/lod_level` rebuilt meshes but
> never recorded the level, so `updateChunkLod()` saw "already correct" and **skipped every
> chunk** — distance LOD silently did nothing while `chunks_by_level` reported success. Fixed by
> making the desync unrepresentable: `setLodFaces(faces, level)` records the level and
> `rebuildFaces()` resets it to 0. Pinned by `LodLevelTracksTheMeshActuallyBuilt`
> (mutation-verified red).
>
> **The ORIGINAL LodBench scenario is re-measured and clean** (Release build — Debug wedges on
> that 578-chunk world). Same world, same pose `(0,420,620)`, render distance 2048:
> OFF `{0:380}` / 250,172 faces → `screenshot_20260730_161706_538.png`; ON `{1:21, 2:359}` /
> 122,218 faces, stable over 6 polls with levels **and** face count agreeing →
> `screenshot_20260730_161745_158.png` — **solid, no stripes.**
>
> ### ✅ DEFAULT SQUASH RULE FIXED — was unsafe at the levels the renderer reaches (2026-07-31)
> Evidence: [`docs/evidence/lod_squash_default_20260731.txt`](evidence/lod_squash_default_20260731.txt).
>
> The shipped default was `OrPreserveOpenings`, which `LodBrick.h` itself documents as
> *"CATASTROPHIC at brick sizes — measured to erase 49.7% (4³) to 100% (16³) of a settlement
> block. NOT recommended above level 1."* But `updateChunkLod` coarsens to **`maxLevel = 5`**.
> Mechanism: `solid = (cov > 0) && !anyOpening`, with `preserveOpening` propagating upward, so
> **one authored window erases an entire walled room from level 3 on** (red-proven at levels
> 3/4/5; levels 1–2 survive, exactly as documented).
> **Default is now `OrWithOpeningMask`** — the header's own recommendation: keeps the mass solid
> and conserves the opening volume upward. Pinned by
> `LodQuadFootprintTest.DefaultSquashConfigIsSafeAtRendererMaxLevel`.
>
> **This is a no-op at runtime *today*, and that is proven, not assumed:** `volumeFromChunk`
> never authors `preserveOpening`/`openingCoverage`, so both rules reduce to `solid = cov > 0`.
> `BothOrRulesAgreeOnRealChunksBecauseNoOpeningsAreAuthored` asserts byte-identical faces at
> levels 1–5 on a real chunk, and is mutation-verified to fail the moment an opening is authored.
> Its value is removing a landmine **before** the structure path starts marking openings — which
> it must, for buildings to read as windowed at distance.
>
> **`HalfThreshold` was considered and rejected, with a measurement.** The generator's interior
> wall is one microcube thick = 81/729 = **11.1%** coverage, so a ≥50% rule deletes it at the
> first squash — buildings invisible at distance, worse than fattening.
> `HalfThresholdDeletesTheGeneratorWall` pins this so it isn't retried.
>
> ### 🚨 MEASURED DEFECT — isolated thin detail FATTENS without bound (2026-07-30)
> `LodCell::solid()` is `coverage > 0` — a **boolean** — and the default `OrPreserveOpenings`
> rule never dilutes coverage as it propagates up the pyramid. So a lone microcube
> (coverage 1 of 729) survives to the TOP of the ladder and emits a full-size cell quad.
>
> Measured by a standalone probe over `LodBrick.cpp` (solution-auditor, 2026-07-30): one
> microcube in an otherwise-empty 32³ volume stays `solid() == true` at **every level 0–5**,
> and the top-level cell is `cellSizeInCubes() == 32`.
>
> Linear fattening, using the levels the LIVE renderer actually reaches (level 0 is
> special-cased back to `rebuildFaces()`, and `updateChunkLod` caps at `maxLevel = 5`):
> - level 1 (2 cubes) → **18×** — the smallest coarse cell in live use
> - level 5 (32 cubes) → **288×** — a 1-microcube fence rail becomes a whole-chunk solid slab
>
> Unbounded upward: raising `maxLevel` makes it strictly worse, because nothing dilutes.
>
> **An earlier note of mine said "~9×", which was wrong** — it assumed level 0 was in live use
> and framed the defect as small and bounded. It is neither.
>
> **Missing test (this is why the wrong number went unchallenged):** nothing measures the
> emitted QUAD SIZE for an isolated thin voxel at increasing levels.
> `CoverageReflectsSubCubeFillLevel` only checks the coverage NUMBER at level 0, never the
> rendered footprint once coarsened.
>
> **Fix direction:** this is the plan's appearance tier (M2) — consume the fractional coverage
> (shrink/fade the quad, or a coverage threshold that dilutes with level) instead of a binary
> `> 0` test. `HalfThreshold` already exists and dilutes correctly; it is simply not the default.
>
> **Note:** `coverage` is NOT "visually unused" — `squash()` also reads it as the volume-majority
> weight that picks `bulkMaterial` (and thus the rendered material for fully-interior cells).
> The accurate statement is narrower: coverage never scales quad SIZE.
>
> **Still open:** the coarse mesher is **unmerged**, so low levels can emit *more* faces than the
> greedy-merged fine mesh (54,066 vs 30,799 on LodStripe; 410,004 vs 250,845 on LodBench at level
> 1). Greedy-merging the coarse mesher is the next correctness-adjacent perf item.

---

### DEFAULTS FLIPPED FOR LONG-RANGE VIEW (2026-08-01)

Goal set by the user: *"rendering full worlds without a max render distance."* What actually
delivers that is the two FAR tiers, not chunk coarsening — that distinction is the finding here.

| Setting | Was | Now | Why |
|---|---|---|---|
| **`Application::maxChunkRenderDistance`** (editor — **the one that governs**) | **192** | **4096** | It IS the projection far plane. At 192 every far tier is clipped and invisible regardless of its own config. See the trap below |
| `maxChunkRenderDistance` (the 3 non-governing configs) | 256 / 96 / 256 | **4096** | Consistency only — the editor path reads none of them |
| `FarTerrainManager::Params::enabled` | false | **true** | Measured 57 tiles / 71,630 tris / horizon filled / 319 FPS once the far plane reached it |
| `…::maxDistance` / `ringSteps` / `maxResidentTiles` | 2048, `{2,4,8}`, 512 | 4096, `{2,4,8,16}`, 768 | 4 rings: bands 0-512-1024-2048-4096. Ring 4's individual contribution not isolated |
| `RenderCoordinator::s_farLodChunks` | false | **true** | The only tier carrying STRUCTURES/EDITS past residency; now storage-driven, so no reach cap. Has end-to-end runtime evidence incl. a falsification test (`lod_c3_3_far_draw_20260731.txt`), but its own commit records "no audit, no unit test for `getChunksWithLodBlobs`" — **not re-verified in this session** |
| `s_lodMaxLevel` (new) | — | **3** | Bounds the fattening defect (level 5 = 288× on isolated thin detail) |

### 🐞 THE FOUR-CONFIG RENDER-DISTANCE TRAP — and a wrong "regression" I published for an hour

**Corrected 2026-08-01, same day.** An earlier version of this section reported that far terrain
"DOES NOT FILL THE HORIZON" and called it a **suspected regression**, citing 217–236 tiles resident
with only 9–14 drawn and ~13–17k triangles, and enabled-vs-disabled frames that looked identical.

**That diagnosis was wrong, and the measurements were worthless.** Far terrain was never regressed.

**Root cause: there are FOUR independent render-distance settings and I changed the three that
don't matter.** `EngineConfig::maxChunkRenderDistance`, `WorldInitializer::maxChunkRenderDistance`
and `GameSettings::renderDistance` all exist — and **none of them feed the editor.**
`Application` owns its own `maxChunkRenderDistance` (`editor/include/Application.h:483`, then
**192.0f**) and pushes that to RenderCoordinator at `Application.cpp:349`. Since
`maxChunkRenderDistance` *is* the projection far plane, every far-terrain tile past **192 units**
was frustum-clipped. The 9-drawn reading was the far plane, not the far tier.

The same trap explains the July numbers I compared against: those evidence files all say
"render distance 2048" because that session **manually POSTed it**. It was never a default.

**With the far plane actually raised** (`POST /api/debug/render_distance {"distance":4096}`), same
world, pose (16,140,180):

| | far plane 192 | far plane 4096 |
|---|--:|--:|
| far tiles drawn | 9–14 | **57** |
| far triangles | ~13–17k | **71,630** |
| horizon | ends in sky | **terrain to the horizon** |
| FPS | — | 319 |

**Fix applied:** `Application::maxChunkRenderDistance` default 192 → **4096** (with a comment at the
declaration naming the trap), and `FarTerrainManager::Params::enabled` is back to **true**.

**Lessons worth keeping, because both nearly stuck:**
1. **Before concluding a subsystem is broken, verify the thing that would clip it.** The frustum's
   far plane is upstream of every far tier and is invisible in that tier's own stats.
2. **A default that "exists" in a config struct is not the default that runs.** Grep for *every*
   definition of a setting and find which one the running path actually reads.
3. Comparing against archived numbers requires reproducing that run's **setup**, not just its world.

⚠️ **Still true and still unfixed:** a `maxDistance` change does not invalidate the wanted set, so
tiles stay frozen until the camera moves past `kRefreshDistance` — the same defect class C1 fixed
for `viewScale` (`m_lastRefreshViewScale`). A stationary-camera A/B on this system returns
byte-identical stats and reads as "no effect". It produced one invalid comparison here.

---

### ✅ REVERSE-Z — the far plane is gone (2026-08-01)

`engine/include/graphics/DepthConvention.h` is the single source of truth;
`tests/graphics/ReverseZDepthTest.cpp` (7 tests) pins it. The scene projection is now an
**infinite** reverse-Z perspective: near → 1.0, infinity → 0.0, no far term anywhere in the matrix.
Render distance stops being a geometric clip — `maxChunkRenderDistance` survives only as a culling
radius.

**Two defects removed, not one.** The old path was `glm::perspective`, and this build defines no
`GLM_FORCE_DEPTH_ZERO_TO_ONE`, so it emitted **OpenGL [-1,1]** clip depth into a Vulkan **[0,1]**
pipeline. Vulkan clips `z_clip < 0`, so everything between the near plane and ~2× near was being
silently discarded and **half the depth buffer went unused**. `ForwardZClipsGeometryNearTheLens`
pins that so a revert is visible.

**The precision claim, measured** (float32, near 0.1, old far 4096) — smallest surface separation a
32-bit depth buffer can still distinguish:

| distance | reverse-Z | forward-Z | ratio |
|--:|--:|--:|--:|
| 100 | 0.000004 | 0.000707 | 172× |
| 1000 | 0.000033 | **0.381** | 11,389× |
| 4000 | 0.000127 | **3.757** | 29,540× |

Forward-Z could not resolve **a third of a voxel at 1 km, or four voxels at 4 km** — that is the
z-fighting budget distant geometry had. ⚠️ The first version of this measurement started its search
step at 1e-4 and reported the two conventions as *equal at every distance*; the real 11,000× gap was
hiding under the probe's own floor. If you re-measure, start well below the expected answer.

**Scope — the shadow pass stays FORWARD-Z.** It renders to its own attachment with its own
`orthoRH_ZO` light matrix, and `voxel.frag` compares shadow depth directly. `ShadowMap.cpp`'s two
`VK_COMPARE_OP_LESS` and its `1.0f` clear are deliberate; so is its depth-bias tuning
(constant 1.25 / slope 1.75, which is forward-Z tuning).

**🐞 A SECOND four-configs trap, same shape as the render-distance one.** The projection was built
in **five** places. `editor/src/Application.cpp:3955` hand-rolled its own `glm::perspective` +
Y-flip — it feeds `updateCameraFrustum()`, so it would have kept assigning near/far frustum planes
under the old convention while every pipeline moved to GREATER. Also found and converted:
`RenderCoordinator`'s null-camera fallback and `CameraRig::projection()`. All now route through
`Camera::getProjectionMatrix` / `DepthConvention`. (`VoxelRaycaster.cpp:495` still builds its own,
but only to invert it for a ray *direction* and discards z — convention-independent, left alone.)

**🐞 And a third one the test suite caught, not me.** `Utils::Frustum::extractFromMatrix` is shared
by the reverse-Z scene camera, the **forward-Z ortho shadow light matrix**, and OpenGL matrices in
tests. I hardcoded reverse-Z in it, which silently swapped the shadow frustum's near/far planes;
`FrustumTest.OrthographicFrustum_AABB` went red. It now takes a **required `ClipConvention`
argument** — no default, because picking the wrong one must not be silent. (Bonus: the original
implementation used the OpenGL near-plane form in a Vulkan renderer, so its near plane had always
been wrong — harmless only because a 0.1-unit near plane culls nothing.)

**Verified at runtime:** LodTest horizon 373 FPS, near field + grass + trees + depth sorting correct
at 127 FPS; WaterLab canonical vantage 487 FPS with swell, shoreline and depth-graded absorption
intact — which specifically validates the `hasSeabed` flip, since an inverted test paints the whole
horizon as shore foam. Full suite green.
**NOT verified: the mirror/reflection pass** (needs a scene with a Mirror surface) and SSAO
(`ssaoEnabled = false` by default). Both were converted; neither was exercised.

---

### 📋 WHAT THE CUT ACTUALLY COVERS — audited 2026-08-01

Asked directly: does the scaling LOD support trees, structures, grass, and non-terrain content?
**Mostly no.** The cut is static-voxel-only, exactly as §3 intended, but that has consequences worth
stating plainly rather than leaving implied.

**Covered**, because they are static voxels baked into chunks: terrain, structures, and tree
trunks/branches. `volumeFromChunk` folds sub/microcube occupancy into `coverage`, so 1-microcube
walls survive. Past residency: structures ride the far-LOD chunk tier (**only if saved**), terrain
rides far terrain (2.5-D; structurally cannot show a building).

**Not covered** — each has its own hardcoded radius, no coarsening, no shared representation:

| Subsystem | Distance rule today | Coarsens? |
|---|---|---|
| Grass | radius 192 + bespoke per-chunk density LOD | Not on the DAG |
| Foliage / leaf cards | flat radius 512, **binary in/out** | No |
| Characters / NPCs | part decimation 35/80, cull 400 | Part LOD only |
| Kinematic voxels (doors, furniture, fragments) | **no distance bound found** | No |
| GPU debris particles | none | No — C6, unbuilt |
| VFX | none found | No |
| Water | player-following sim region | No — C6, unbuilt |

C1 re-homed several of these radii onto the shared screen-space metric, but that only makes them
*consistent under resolution/FOV changes* — it is unit hygiene, not level-of-detail.

#### 🐞 Two latent defects found by the audit, both fixed here

1. **Foliage had no coarsened-chunk gate** while grass does. Fixed: `renderFoliage` now skips
   `getLodLevel() != 0`, mirroring `renderGrass`.
2. **Leaf voxels were double-represented under the cut.** Leaf materials are `billboarded` —
   `ChunkRenderManager` *omits their solid faces from the fine mesh* and emits cards instead, so a
   canopy has **no fine geometry at all**. But `volumeFromChunk` counts every visible voxel with no
   billboard exclusion, so from level 1 up a canopy becomes a solid mass of Leaf material — and
   with foliage ungated, both drew at once.

   **The resolution is a handoff, not an exclusion.** Keeping leaves in the coarse volume is
   correct: the solid mass *is* the canopy's distant impostor (billboards near, mass far, as the
   field does). Excluding them would make every forest evaporate into bare trunks the moment the
   cut engages. So fix (1) is what makes the inclusion safe. Pinned by
   `LodChunkMeshTest.LeafCanopySurvivesTheCutAsSolidMass` and `LeafCanopyCoarsensLikeAnyOtherMaterial`
   (mutation-verified: excluding leaves turns both red with "distant forests would render as bare
   trunks"), plus a do-not-add-an-exclusion comment at the site.

Both were latent — they bite only when distance-driven chunk LOD is enabled, which is default-OFF.
But #2 would have made forests wrong the moment the cut turned on, and forests are most of a Perlin
world's surface.

**`s_distanceDrivenLod` stays OFF, and the arithmetic is the reason.** It coarsens *resident*
chunks only, and residency is `loadDistance` 256 / `unloadDistance` 352. At the reference config a
1-cube cell hits `s_lodTargetPixels = 8` at **~136 units**, so its entire working window is
~136–352u — beyond that there are no resident chunks to coarsen, and far terrain + far LOD own the
distance. Against that small, C5-unmeasurable win sit the fattening defect and a **direct conflict
with grass**: that band is exactly where the new 192u grass radius lives, and `renderGrass` must
skip coarsened chunks (the OR-occupancy coarse surface sits at or above the fine one, burying the
blades). Enabling it by default would trade visible grass for a face reduction C5's own A/B could
not measure as a speedup. It remains the live A/B toggle it was built as.

**Consequence worth stating plainly:** the horizon is now filled for *generated terrain* (far
terrain, to 4096u and raisable) and for *saved structures/edits* (far LOD, uncapped). It is NOT
filled for unsaved generated **detail** — streaming worlds never save plain terrain, so no pyramid
exists for it (`lod_c3_3_far_draw_20260731.txt` FINDING 1), and far terrain represents it as a
2.5-D heightmap with no overhangs, no caves and no structures. Closing that gap is the
*generated coarse tier* (coarse straight from `CoarseWorldModel` rather than downsampled), which is
unbuilt and is the real centrepiece of "no render distance".

> ### ✅ C5b SUPERSEDED BY THE 2026-08-05/06 CASCADE CAMPAIGN (docs/NearShadowCascade.md)
> Shadows shipped as **three cascades** (near 40u blade-resolving / mid 420u / far 1600u —
> the LOD band casts AND receives, cadenced, coarsest-level casters), not via the C-series.
> Two of this plan's load-bearing shadow questions are now SETTLED empirically:
> - **C2's batching premise is dead at every operating point.** The multidraw path ran with
>   real data (420 draws → 8) and showed NO win — including at an ~11 ms dense-forest
>   regime. (A first "11.1→5.1 ms win" was retracted: the C2.1 block's early return had
>   been silently dropping foliage/character casters — the win was the missing draws.
>   docs/evidence/dense_forest_perf_20260806.txt.) The mid cascade's real cost is
>   **vegetation caster volume** (~5–6 ms of foliage cards at dense poses) — the next lever
>   is foliage caster density/LOD, not submission batching. Multidraw remains default-ON as
>   a correctness-verified, cost-free path.
> - **M5 is answered** (4th time's the charm, empirically): the 6-index quad breaks the
>   shadow pass — 4.785% px diff vs 0.022% control — because face quads are wound for the
>   CAMERA convention; the 36-index both-windings draw is what survives BACK_BIT relative
>   to the LIGHT. 36 stays required; a 6-index + CULL_NONE pipeline (with its own bias
>   re-tune + pixel gate) is the only remaining variant worth trying.

*(original C5b scoping below, superseded)*

### C5b — Shadows onto the cut
Cascades (the `s_shadowFrustumCull` hook at `RenderCoordinator.cpp:1172` was kept for exactly
this), then cached static VSM-style pages.

**Gate:** shadow-pass GPU time down measurably (`GpuProfiler::STATS_SLOT_SHADOW` already
instrumented) with **shadow quality unchanged inside 160u** — pixel A/B at the poses used in the
2026-07-11/12 shadow arc, since that arc's fixes (CULL_NONE casters, world-anchored texel snapping,
normal-offset sampling) are exactly what a careless cascade change would regress.

---

### C6 — Dynamic aggregation; hand the metric to water
Debris aggregation + character impostors. Water consumes the metric only — **Phase B/LOD belongs
to the user's other session.**

---

## 6. Where this dies (name it now)

1. **Destruction churn.** The destruction system forces near-per-frame remesh. Rebuilding a full
   LOD pyramid per break is a trap. Mitigations: dirty only the touched brick chain (log₂ levels,
   cheap); or Vercidium's run-merge for the near field ("~20% more triangles, ~390% faster",
   `LargeWorldScalePlan.md:840`). **This is the top risk and it is on the active branch
   (`destruction-system-v2`).**
2. **Concurrent edits.** This touches `static_voxel.vert`, `ChunkRenderManager`,
   `RenderCoordinator` — files under active edit in other sessions. Needs its own branch and an
   explicit sequencing conversation. The `BinaryGreedyMeshingPlan.md` coordination note is the
   template.
3. **The 64× draw-unit multiplication** if C4 lands before C2 (§2.3). Non-negotiable ordering.
4. **Squash quality is subjective and I cannot self-verify it.** Per [[water-system-v2]]'s
   integrity note: code claims are verifiable; "looks good" is not. Every visual claim in this
   arc needs a test or the user's eyes. C0's L2 tests exist specifically so squash quality is
   *measured*, not asserted.
5. **Scope creep into ray tracing.** §2.1 makes the raymarched-brick endgame reachable
   *without* entering it. If a phase starts arguing about DDA, it has left this plan.

---

## 7. Open decisions (resolve before C3)

| # | Question | Leaning |
|---|---|---|
| 1 | Transition: dither/cross-fade, or POP-buffer geomorph (`LargeWorldScalePlan.md:855`)? | Dither first (cheap, revertible); geomorph if C4's no-pop gate fails |
| 2 | Openings preserved by authored hint (`AssemblyPlan`) or inferred from voxels? | Authored — C0 measures both |
| 3 | Does the far tier stay a 2.5-D heightmap, or become coarse volume (the `FarRepresentationProviders.md` Axis 2 fork)? | Defer. C3's pyramid makes coarse volume nearly free later |
| 4 | Turn `FarTerrainManager::Params::enabled` on by default as part of C1, or keep it debug-route? | Separate decision, separate gate — do not smuggle it in |
| 5 | Fix the T-junction defect and the LOD-seam rule together in one skirt mechanism? | Yes (§2.5), but `RenderOptimization.md:489` says do not rush — decide at C4 |

---

## 7b. Measurement campaign M1–M5 (**M1 RUN + RETRACTED; bench built; M2–M5 PLANNED**)

> ### 🧪 The bench: `PhyxelProjects/LodBench` (built 2026-07-29)
> All future measurement runs go here, not on an improvised scene. Own engine port **8097**.
> Harness: **`tools/lod_bench.py`**. Full docs: `PhyxelProjects/LodBench/README.md`.
> - **Baked** deterministic world (`game.json`, seed 777001, 578 chunks) = the perf gate;
>   **streaming** variant (`game.streaming.json`, same seed/params) for far-terrain/water work.
>   **Numbers from the two are not comparable.**
> - Density ladder as **named poses in ONE world** (`bare` / `tavern` / `village` / `town` /
>   `overview`) so a full ladder runs in one process — no ±20% restart variance.
> - The harness encodes every method defect the M1 audit found: pins **and logs**
>   pipeline-stats/`shadow_cull`/`quad_draw`/`fine_merge`/render-distance; sets free-camera mode in
>   its own call; **re-verifies the pose on every sample and refuses to collect** a point that
>   won't hold; n≥15; reports the high-mode fraction so bimodality can't hide behind a median;
>   archives raw JSONL + generator provenance.
> - **Gotcha pinned by measurement:** with these params the Perlin surface is at **y≈53 → chunk
>   y=1**. Generating chunk y=0 alone yields buried stone (flat plane, `surface_y`=31, ~1,900
>   faces) — this cost two generation attempts. And `get_terrain_height` reads *loaded chunks, not
>   the generator*, so it confidently reports the top of whatever you generated.
>
> **First baseline** (`docs/evidence/lodbench_baseline_20260729.jsonl`, n=15/pose, **0 samples
> dropped**): shadow draws 156→376 across poses, shadow ms 1.93→3.89.
> Fit: `shadow_ms ≈ 8.96 µs × draws + 0.667 ms` (**R²=0.963**); instances fit worse (R²=0.897).
> **But draws and instances are collinear across these poses, so this does NOT separate the two
> mechanisms** — and it predicts **1.84 ms** for the ~131-draw scene that measured **24–26 ms**.
> **The wall remains unexplained.** Until it is, no LOD phase may be justified by a shadow
> per-draw constant.

> ### ⛔ M1 RESULT — **RETRACTED IN PART.** Solution-auditor VERDICT: **FAIL** (2026-07-29)
>
> **Read this box first; the original write-up below it overreached.** An independent
> solution-auditor recomputed everything from the raw JSONL and found that **my own harness had
> recorded `pose_ok: false` on 27 of 33 samples in run 1 — every sample at every distance ≥128 —
> and I never looked at the field I had added for exactly that purpose.** The camera drifted (the
> known free-camera-drift bug), so the headline "clean per-draw test" spans a pose boundary and is
> **invalid**. Verified independently: run 1 `pose_ok` is `True` only at dist 64 and 96; run 2 is
> `True` at all 11 distances.
>
> **What survives (the only claim the evidence supports):**
> > On a flat-plane-plus-7-villages scene at up to **589,563 faces**, on a Release build of
> > `destruction-system-v2`, **no 24–26 ms shadow wall was observed** — max *individual* sample
> > ~12.6 ms, medians ~4 ms at the two n=25 points.
>
> **What is RETRACTED — not established by this data:**
> - ❌ "The 0.13 ms/draw per-draw model did not reproduce." The comparison it rests on is
>   pose-invalid. **Unfalsified, not refuted.**
> - ❌ "Shadow cost tracks instances/fill, not draws." **Contradicted by the very measurement it
>   was meant to explain**: `RenderDensityPlan`'s scene was 377k faces / ~131 draws → 24–26 ms;
>   mine was 589k faces / 187 draws / 325,550 instances → ~4 ms. *More* faces, draws and instances,
>   **6× faster**. An instances/fill mechanism cannot explain that, and I asserted around the
>   contradiction instead of resolving it.
> - ❌ "Flat all the way to 512." n=25 exists at only **2 of 8** plateau distances; the n=3 sweep
>   medians at 224–512 include 7.93 / 7.82 / 10.41 ms, which I did not mention.
> - ❌ "~10–15% of samples spike." The 25 raw samples were never archived, so the figure is
>   uncheckable; the nearest archived proxy (24 plateau-pinned sweep samples) gives **45.8% >6 ms /
>   20.8% >8 ms** — 1.4–3× my stated figure.
> - ❌ Consequently the **C2/C5 re-weighting below is withdrawn.** C2 and C5 keep their *original*
>   justifications; M1 neither strengthens nor weakens either.
>
> **Method defects to fix before re-running (from the auditor):**
> 1. **Reject, don't silently include, `pose_ok: false` samples** — and fix or work around the
>    camera-drift bug first.
> 2. **Pin and log `s_shadowFrustumCull`** and the fitted shadow-volume bounds. Raising render
>    distance also grows the shadow volume (`chunkInclusionDistance = d × 1.5`,
>    `Application.cpp:7917`), so draw count is confounded with volume — the exact confound I
>    claimed to have controlled.
> 3. **n ≥ 15 at every point** where "flat"/"did not reproduce" is claimed. The archived data's own
>    20–46% high-mode rate makes median-of-3 close to a coin flip.
> 4. **Archive raw responses for the scene-generation calls.** The provenance statement below
>    ("entirely engine-generated") is **prose-only** — the `/api/world/generate` and
>    `/api/settlement/build` responses were never written to `docs/evidence/`. Nothing contradicts
>    it, but per CLAUDE.md's provenance rule it is not evidence until archived.
> 5. Re-run on **Perlin-hills terrain with a settlement** (the original scene shape), and reconcile
>    against the 24–26 ms measurement rather than declaring "different operating point".
>
> *Kept below, uncorrected, as the record of what I claimed before the audit.*
>
> ### ⚠️ (SUPERSEDED — see the retraction above) M1 RESULT — the per-draw-overhead model did NOT reproduce
> **Evidence:** `docs/evidence/lod_m1_shadow_vs_drawcount.jsonl`,
> `…_dense.jsonl`, `…lod_m1_noise_floor.txt` (raw API responses, not typed summaries).
> **Build:** Release, `destruction-system-v2`, 2026-07-29. **Scene:** entirely engine-generated —
> `POST /api/world/generate` (Perlin, 15×15 chunks) + 7× `POST /api/settlement/build` (medieval
> villages). Nothing hand-placed. Camera in free mode, pose re-verified at every step.
>
> **The clean per-draw test (run 1, 95,739 faces):** shadow draws **186 → 236 (+27%)** while shadow
> instances stayed flat (95,610 → 95,739, +0.1%). Shadow-pass time **3.187 → 2.87 ms — it did not
> rise; it fell ~10%.** The 0.13 ms/draw model predicts **+6.5 ms**. With medians stable to ~0.1 ms
> (n=25), the measured per-draw cost is **more than an order of magnitude below 0.13 ms/draw**.
>
> **Shadow cost tracks instances/fill, not draws.** Cross-run: 236 draws / 95.7k instances → 2.9 ms,
> versus 187 draws / 325.5k instances → **4.0 ms**. *Fewer* draws and 3.4× the instances cost *more*.
> And once draws+instances pin at 187/325,550 (dist ≥160), raising render distance all the way to 512
> leaves shadow time flat (medians **3.99** @192, **4.08** @512, n=25 each).
>
> **Honest scoping — this does not refute `RenderDensityPlan.md`:** I never reproduced the 24–26 ms
> wall (max median observed: **4.08 ms** at 589,563 faces). That figure came from a 377k-face
> *Perlin-hills* scene; mine is a flat plane plus villages. These are different operating points, so
> the correct statement is **"failed to reproduce on different content"**, not "the earlier
> measurement was wrong". Also unexplained: a bimodal **~10–12 ms spike** in ~10–15% of samples at
> otherwise-constant inputs.
>
> **What this changes in the plan:**
> - **C2's shadow justification weakens.** "Batch the shadow chunk draws" — `RenderDensityPlan`'s
>   top suspected win, which I earlier mapped onto C2 — is **not supported by this data**. C2 still
>   stands on its own (10k+ chunk residency, blocker E), but it should **no longer be sold as the
>   shadow fix**.
> - **C5's justification strengthens, and its shape changes.** If shadow cost is instance/fill-driven,
>   the lever is **fewer/coarser shadow-caster instances** (reuse the LOD cut at a coarser `targetPx`)
>   and **bounding the volume** (cascades) — both of which are geometry reduction, i.e. the core of
>   this plan, rather than submission batching.
> - **The brick-size budget argument must be re-derived.** §2.3 leaned on per-draw cost to bound
>   granularity from below. On this evidence that bound is much weaker than assumed. **C0 still owns
>   the 4³/8³/16³ decision** — now with even less reason to pre-commit.
>
> **Required follow-up before any of this is load-bearing:** re-run M1 on **Perlin-hills terrain with
> a settlement** (the original scene shape) and at 4096² vs 2048² shadow-map resolution, and chase
> the spike. Until then M1 is one scene on one rig.

*Because the audit's top finding was "this number was asserted, not measured", the response is to
measure. All three run on a **Release** build in **one process** (no restarts — restart variance is
±20%, [[render-perf-track]]), at a **verified fixed camera pose** (HTTP camera set is known to
revert — re-read `get_camera` and confirm before every capture). Raw responses archived under
`docs/evidence/` per the standing rule: **never cite a number from an un-archived session.***

**The instrumentation already exists** — this is the D0/D1 rig from `RenderDensityPlan.md`, reused:
- `GET /api/debug/gpu_scopes` → per-pass GPU ms (`scopes[]`), `shadow_chunks_drawn`,
  `shadow_instances_drawn`, `visible_chunks`, and pipeline stats per slot
  (`editor/src/Application.cpp:12402-12437`).
- `GET /api/render/stats` → `total_visible_faces`.
- `POST /api/debug/render_distance` → the independent variable.
- Toggles for A/B: `/api/debug/quad_draw`, `/api/debug/fine_merge`, `/api/debug/occlusion`.
- ⚠️ Pipeline-stats queries add sync overhead — **gate them OFF for timing runs**
  (`setPipelineStatsActive`), per D1a's own note.

### M1 — Shadow cost vs. draw count → **the per-draw constant** (highest value)
Sweep render distance over a dense scene at a fixed pose; at each step record shadow-pass ms,
`shadow_chunks_drawn`, `shadow_instances_drawn`, static-geometry ms, `visible_chunks`, faces, FPS.
Then regress shadow ms on **draws** vs on **instances**.

This is the load-bearing measurement of the whole plan, because it decides three things at once:
1. **Confirms or refutes** `RenderDensityPlan.md`'s inference that the ~17 ms residue is per-draw
   overhead (~0.13 ms/draw across ~131 draws) rather than fill or primitives.
2. **Quantifies C2's win.** If cost ≈ `a + b·draws`, then batching 131 draws → ~1 recovers ≈ `b·130`.
   That converts "biggest suspected win" into a number.
3. **Bounds brick granularity from below** — the auditor's #1 blocker. A measured per-draw-unit cost
   gives an affordable draw-unit budget at 60/120 FPS, which is the real constraint on §2.3's
   4³/8³/16³ choice. (Note this bounds the *CPU-submitted* model; post-C2 the budget changes, which
   is itself the argument for C2 first.)

### M2 — Detail-tier resolvability → grounds `targetPx` and the appearance-tier fade
From the audit-verified formula (§2.4) plus the engine's **actual** FOV and viewport height (read
live, not assumed), compute the distance at which each tier subtends 1 px: microcube (1/9 u),
subcube (1/3 u), cube (1 u). Then verify one point empirically against a screenshot. This replaces
the borrowed "~1–2 px" constant with engine-specific distances at which detail provably stops being
resolvable — the honest input to both `targetPx` and the C0 appearance-tier collapse.

### M3 — Re-baseline this branch
The numbers in §0.1/§0.3 come from binaries built 2026-07-07/08 on other branches. Re-measure
faces / FPS / pass split for: empty world · one generated tavern · a generated settlement — on a
**Release build of `destruction-system-v2`**. Confirms the "density wall is retired" correction
holds on *this* code, and gives the standing perf gate (`LargeWorldScalePlan.md` Phase 6) a current
baseline. **Scenes come from the engine's own generators** (`POST /api/settlement/build`,
`/api/structure/build`) — never hand-placed, per the CLAUDE.md provenance rule.

### M4 — Re-run the 3.4M-face settlement (**settles the "density wall" question**)
*Added 2026-07-29: this is the falsifiable test a solution-auditor named when it failed the
"largely retired" claim in `CLAUDE.md`.* Regenerate the documented worst case — the ~3.4M-face
Perlin-hills settlement (`RenderOptimization.md:14`) — **via the engine's own generator**
(`POST /api/settlement/build`), and measure `total_visible_faces` + FPS at a fixed verified pose on
Release, `s_fineGreedyMerge` ON vs OFF (`/api/debug/fine_merge`). Until this number exists, the only
proven claim is "a synthetic flat multi-tavern grid recovers 5–8×" — **not** that the density wall
is retired. Also re-measure the single furnished tavern (the original 412k/49 FPS case) for
completeness, acknowledging its ±20% noise floor.

> #### ✅ M4 RESULT (2026-08-17) — **the density wall is retired at the settlement operating point, now with the number**
> **Evidence:** `docs/evidence/lod_m4_density_wall.jsonl` (raw per-sample responses, 3 scenes ×
> 3 configs × n=15, **135/135 samples pose-verified** — the M1 failure mode designed out),
> `lod_m4_settlement_build_result.json`, `lod_m4_tavern_build_response.json` (raw generator
> responses, per the provenance rule). **Build:** Release, `main` @ WorldForge M3. **Method:** one
> process per scene, fixed pose re-verified before every capture, merge ON→OFF→ON bracket (the
> second ON block detects drift; it agreed within ±13%, inside the known noise), timing samples
> read only `render/stats` + `engine_timing`; `gpu_scopes` captured once per config after timing.
>
> | scene (engine-generated) | merge ON | merge OFF | reduction |
> |---|---|---|---|
> | Perlin-hills town (5 bldgs — terrain mode honestly dropped to 5 on the hostile hills) | 109,433 faces / **226 FPS** | 427,511 / **112 FPS** | 3.9× faces, 2.0× FPS |
> | **Perlin hills + 4 settlements, 25 buildings** (the original scene's scale) | 269,618 faces / **135–145 FPS** | **1,764,780 / 27.5 FPS** | **6.5× faces, ~5× FPS** |
> | single furnished tavern (flat world) | 13,767 / **~460–497 FPS** | 137,836 / **246 FPS** | 10× faces |
>
> **The 24–26 ms shadow wall REPRODUCED and root-caused:** on the 25-building scene the shadow
> pass is **5.2 ms merged → 25.6 ms un-merged at a CONSTANT 466 draws** (instances 269k → 1.76M).
> That is the historical wall, at its original operating point, and it confirms the M1
> retraction's surviving hypothesis — **shadow cost tracks instance volume, not draw count** —
> and identifies fine greedy merge as the change that retired it.
>
> **Honest caveats:** (1) the historical **3.4M faces is unreachable on today's code** — Phase-1
> sub/micro hidden-face culling (2026-06-28) is unconditional, so merge-OFF peaks at 1.76M; the
> comparison is recipe-equivalent (Perlin seed 7, heightScale 18, settlement overview), not
> byte-equivalent (terrain-v2 also moved the base height; west peaks clip at the y-range top).
> (2) The original single measurement was DEBUG; these are Release — the honest cross-config
> statement is "the un-merged configuration still walls (27.5 FPS / 25.6 ms shadows at
> settlement density) and the shipped default does not (135+ FPS / 5.2 ms)". (3) The terrain-mode
> town honestly degrades to 5 buildings on raw hills; scene 2 reaches the original's 20+ building
> scale with three additional engine-generated villages (all raw build responses archived).

### M5 — Shadow-pass quad re-test under `CULL_NONE` (correctness, not perf)
The recorded reason the shadow pass must use a 36-index draw ("the pipeline front-culls") is stale:
`ShadowMap.cpp:433` has been `VK_CULL_MODE_NONE` since `07ba0a74` (2026-07-12), while the comment
asserting front-culling dates to `3cfe4356` (2026-07-09). Re-run D1's pixel-diff with the 6-index
quad applied to the shadow pass under current code. **Expect no timing win** (D1 measured none; the
pass is depth-fill-bound), so this is a correctness/stale-comment item — but it must be settled
before C5 reasons about shadow geometry, and the two stale comments
(`RenderCoordinator.cpp:1241`, and any sibling in `ShadowMap.cpp`) fixed either way.

> **Not measurable yet, deliberately:** brick-size pop-visibility (needs C0's squash to exist) and
> the LOD transition band (needs C4). M1 bounds the brick choice; it does not settle it. C0 still
> owns the 4³/8³/16³ sweep.

---

## 7c. Audit record — grounding-auditor, 2026-07-29

Run against §2 and §5-C0 before any implementation. **Verdict: NOT safe to implement as written.**
Five blocking findings; all corrected above, each marked inline at the point of correction. Kept
here so the corrections are not silently absorbed:

| # | Finding | Disposition |
|---|---|---|
| 1 | **8³ brick was ungrounded.** Tree64 is **4³**-branching (contradicting our own `RayTracingPlan.md:28`); cgerikj's binary-greedy-meshing wants 64-tall *bit columns* on 62³ chunks; Sodium's 8×4×8 is buffer grouping; Nanite's 128 is a triangle count. None support 8³ | §2.3 rewritten: brick size is now an **open variable measured by C0**. C2-before-C4 ordering re-justified on the measured per-draw cost, which is independent of brick size |
| 2 | **`ThinWallSurvivesL2` targeted a wall the engine never generates.** No style emits a 2-microcube *interior partition*; 2 micro is timber_cottage's **exterior**. The real thinnest is **1 microcube** | §5 C0 test rewritten to cover **every** authored thickness (1/2/3/4/6/9/27 micro), cited to `structure_styles.json` |
| 3 | **`targetPx ≈ 1–2 px` was uncited**; SSE convention is nearer 0.5–1 px | Marked `NEEDS-RESEARCH`; to be measured against C4's no-pop gate (M2), not borrowed |
| 4 | **Monotonicity was proven only for the geometric half.** `appearanceError` has no monotonicity guarantee, and Nanite's single-cut correctness depends on it | §1 caveat added; selector redirected to **per-branch descent**, which does not need monotonicity |
| 5 | **Citation-line errors** — the 9³-brick/Teardown anchor pointed at `:824` (a Sodium bullet) | Fixed to `:876-877` (brick) and `:799` (1 B/voxel); the 1-B/voxel *assumption* now stated explicitly |

**Confirmed grounded** (no action): the `projectedPixels()` derivation; 9³ = 729 microcubes/cube
(`Types.h:248-250`, `static_voxel.vert:200`); the ~40–50 ms/chunk remesh figure
(`RenderOptimization.md:425-428`); the A4 residency baseline (`RegionArenaPlan.md:9-10`).

**Not blocking:** the ≥50% threshold is unsourced, but it is the explicitly *rejected* option.

**I verified findings 1, 2 and 5 against the files directly rather than accepting them** — all three
held, including the Tree64 contradiction with our own doc. (A subsequent solution-auditor
independently re-derived all three and confirmed them — see below.)

### Second pass — solution-auditor on the doc corrections themselves, 2026-07-29

**VERDICT: FAIL.** Run against the edits to `CLAUDE.md`, `BinaryGreedyMeshingPlan.md`,
`RenderDensityPlan.md` and this file. 12 of 14 line-level citations verified exact; **two defects**:

| Defect | Fix applied |
|---|---|
| **`CLAUDE.md`'s "LARGELY RETIRED" was an overclaim** — the cited source explicitly says the 3.4M-face settlement "remains not run" and that FPS at single-tavern scale is *noise*. I replaced a stale overstatement with a fresh one | CLAUDE.md rewritten to separate *mechanism shipped* from *scenario unmeasured*; §0.1 consequence tightened; **M4** added as the falsifiable test |
| **The "shadow pipeline front-culls" justification is false for current code** — `ShadowMap.cpp:433` is `VK_CULL_MODE_NONE` since `07ba0a74` (2026-07-12); the comment I trusted (`RenderCoordinator.cpp:1241`) predates it by 3 days. **I propagated a stale code comment as fact** — the same failure class this doc's §0.1 exists to correct | §0.3 corrected; **M5** added to re-test and to fix both stale comments |

Also flagged and fixed: §7b was labelled "IN PROGRESS" with zero data collected → **PLANNED**.
Confirmed clean: no fabricated measurements anywhere in this doc; every "measured" traces to a cited
prior doc; edits preserved original text verbatim under explicit "Original status" labels.

**Standing lesson (add to the §0.1 list): a code *comment* is not a source.** Both the "#1 known
issue" staleness and this front-cull error came from trusting prose that outlived the code it
described. Cite the line that *does the thing* (`rasterizer.cullMode = …`), not the line that
*describes* it.

---

## 8. References

Field sources are already surveyed in `LargeWorldScalePlan.md` §5 (Nanite-adjacent: Aokana
arXiv:2505.02017; Sodium region rendering; vkguide Ascendant two-phase Hi-Z; Distant Horizons;
Veloren; Teardown; binary-greedy-meshing; Lysenko's POP-buffer method; godot_voxel (pos, lod)
keying). **That survey is not duplicated here** — read it before starting any phase.
