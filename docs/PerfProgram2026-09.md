# Performance Program 2026-09: point lights and static sub/micro detail

**Status (2026-09-26) — START HERE. The forward guide is [§17](#17-roadmap-from-here-the-guide-updated-2026-09-26).**
- **Goal (user, 2026-09-25):** no fixed budget — as fast as possible without visual loss, measured at real
  load: **~100-building cities** (S-3, §16).
- **Measured + done:** P0a/P0b instrumentation (§10-§11); P1 on the 4090 for S-1 tavern / S-2 town (§12);
  shipped L1 duplicate-emitter merge (§13), P-DP depth prepass (**default ON since 2026-09-27**, §14, §16.13), GI-1/GI-2 probe
  pass (§15), L3a exact light visibility; the P1c city benchmark tooling (§16.8) and the **C-25…C-100 city
  ladder with its growth table** (§16.11); the first city fix — **structure proxies the shader discards are
  no longer drawn, C-100 street −34 %** (§16.12).
- **Where it stands:** C-100 (104 buildings) was 86–109 ms GPU (9–12 fps) before §16.12; street is ~50 ms
  after it. Point lights are **not** a city cost (night ≈ noon). The next levers, in order, are in §17.2.
- **Still owed:** the laptop GPU, the standalone at native resolution, walk-route hitch runs (§16.6 step 3),
  and P0c (I8, I9). Rules that every step follows: §6 gates, §17.4 runbook.

**The question (from the user):** *"it seems like we have performance issues because of too many point
lights and too many static sub/microcubes. We need to not take my word for it."* This plan treats both
as **hypotheses**. It instruments the engine enough to confirm or reject each one per component, then
ranks the fixes by measured cost, with the rule that a fix must not visibly change the image.

---

## 1. What is already known (evidence, not opinion)

| Claim | Evidence | Scope / caveat |
|---|---|---|
| **H-light is TRUE in one scene.** The forward point/spot loop is **79-91% of the frame**. | `RavenmereGapLedger.md` G-18 runs 5-6, rigs `tools/perf_shader_bisect.py` and `perf_light_marches.py`. | ONE scene (Ravenmere town), ONE GPU (**RTX 1000 Ada laptop**), Release. Never reproduced on the 4090 or in the engine's own bench scenes. |
| The cost is the per-light **occupancy march** (`phxLightVisibility` → `phxDdaHitsSolid`, micro-cell DDA up to 512 steps), not the BRDF. | Mode 17 vs 0: the march alone is 237.5 of 263.6 ms. | Same scene and GPU. |
| The march is not expensive per call (~4-9 ns). There are just too many: **31.3 of 32 lights march at 91.6% of pixels, about 59M marches per frame.** The radius and N·L gates cull almost nothing. | Mode 18 (march count per fragment). | Same. |
| **Coincident duplicate lights.** Every emissive **micro**cube registers its own point light at its *parent cube* position, with no dedup. | `ChunkRenderManager.cpp:520-593`. Template counts: `oven_bread` ~65 glow micros, `forge_hearth` ~9, chandelier ~8. Ravenmere logs 143 emissive-voxel lights. | Code-read, not yet counted live. It fills the 32-slot upload budget with identical lights, each paying a full march. |
| Light **selection** is nearest-32 to the viewer. There is no frustum test and no per-tile list. It re-sorts every frame the camera moves. | `LightManager.cpp:191,201,210-274`. | Code-read. CPU cost not measured. |
| Grass and foliage also march per in-radius point light, with **no N·L gate**. | `grass.frag:153-167`, `foliage.frag:160-171`. | Not measured separately (Grass peaked at 15 ms and Foliage at 25 ms in G-18). |
| Ambient probe lookup is the second sink: **15-23 ms** on the laptop GPU. | G-18 run 5 (mode 14). | Same scene and GPU. |
| **H-micro is NOT established.** Rasterizing every triangle and spawning every fragment cost 0.04-0.12 ms (mode 11). | G-18 run 5. | Measured in a **light-dominated** scene. It says nothing about vertex-bound, shadow, CPU mesh, memory or streaming costs of micro-heavy content. |
| The static-geometry density wall was closed by fine greedy merge: 1.76M → 270k faces, 27.5 → ~140 FPS, and the shadow pass tracks **instance volume** (5.2 → 25.6 ms un-merged). | `ContinuousLodPlan.md` §7b M4, `docs/evidence/lod_m4_density_wall.jsonl`. | 4090, Release. Merge is weakest exactly where micro detail lives: **micro merge stops at the cube boundary** (Increment 4b parked), and merges also break on tint/material/state. |
| Micro storage is heavy. Each microcube is a heap object with a **`std::string materialName`**, ~220-250 B including the maps (a full micro'd cube is ~170 KB). Removal is O(n) with an INFO log per call. Meshing is main-thread, 2-4.4 ms per tavern chunk (13 ms worst). | `Chunk.h:76-78`, `ChunkVoxelManager.cpp:1166-1192`, `OffThreadMeshingPlan.md`. | Byte sizes are estimates. **A micro-heavy chunk has never been timed.** |
| Cube faces behind **fully covering** sub/micro neighbours are never culled (culling only works fine-by-coarse). | `ChunkRenderManager.cpp:647-659`. | Code-read. Face waste not counted. |
| No microcube LOD inside the residency radius (256/352 u). | `LodTierLedger.md` tier 1, C5 distance LOD OFF. | Cost at distance not measured. |

**Bottom line going in:** the light hypothesis has a strong prior from one laptop scene. The micro
hypothesis is essentially unmeasured. Its GPU fragment cost looks negligible, but its vertex, shadow,
CPU and memory costs are unknown.

### 1b. First look on the RTX 4090 (2026-09-24, before any P0 instrumentation)

**Setup**
- Release editor at the working-tree HEAD `8a4b73dc` (dirty tree), 1600×900 window, project `M4TavernBench` (Flat world).
- **Engine-generated** tavern: `POST /api/structure/build {"schema":"v2","type":"house","typology":"tavern","style":"timber_cottage","position":{0,16,0},"footprint":[14,7],"stories":[{height:3},{height:3}]}`.
  - The first attempt with `[14,12]` was refused (`footprint_too_wide`, tavern max 7).
  - The raw response is in `perf2026-09/tavern_build.json`. It hit the 5 s API wait, but the build completed.
- Free camera, with the pose read back before sampling.
- Median of 20-25 single-frame `gpu_scopes` reads per cell.
- Raw data: `docs/evidence/perf2026-09/{firstlook,trace_ab}_4090.jsonl`, scripts alongside, pose captures `pose_{interior,exterior}.png`.

**Light census (live, from `/api/lights`):** one tavern registers **29 point lights at only 16 unique positions**. That
includes **8 identical lights at the chandelier** (3.5, 19.5, 6.5) and six more positions with 2 each. So **45% of the
slots are duplicates**, and a single building nearly fills the 32-slot budget.

**Shader bisect, Static Geometry GPU ms (modes via `POST /api/debug/shadow {mode}`):**

| pose | normal | raster (11) | +tex | +shadow | +ambient | +sun | +lights (16) | lights, no march (17) |
|---|---|---|---|---|---|---|---|---|
| interior corridor | **18.7** | 0.03 | 0.07 | 0.35 | 3.59 | 3.51 | 19.18 | 3.85 |
| exterior (M4 pose) | **6.5** | 0.04 | 0.07 | 0.18 | 2.31 | 2.34 | 6.51 | 2.01 |

**Light-trace A/B** (`GET /api/debug/light_occupancy?trace=0|1`, interleaved 1/0/1/0, each 20 samples; the repeats agree within 3%):

| pose | Static on → off | Grass on → off | Foliage on → off | Scene Pass on → off | FPS on → off |
|---|---|---|---|---|---|
| interior | 19.1 → 3.9 | 1.25 → 0.75 | 0.21 → 0.20 | 21.3 → 5.4 | **37 → 96** |
| exterior | 6.4 → 2.4 | 6.0 → 1.6 | 22.3 → 22.2 | 35.2 → 26.7 | 25 → 32 |

**What this establishes (4090, one building):**
1. **H-light reproduces on the 4090.** The per-light occupancy march is **~15 ms of a ~21 ms Scene Pass indoors**
   (~73%). It is the march, not the BRDF (mode 17 ≈ mode 15). Grass pays it too (4.4 ms at the exterior pose).
2. **Rasterisation plus fragment spawn is ~0.03 ms** at both poses, so static sub/micro geometry is not a
   fragment-side cost here, as in G-18. H-micro's other axes (vertex, shadow, CPU, RAM) are still unmeasured.
3. **The ambient probe is the #2 static-pass cost: ~3.2 ms.** GI probe update is another ~4 ms per frame, flat.
4. **A third sink outside both hypotheses: Foliage, 22 ms at the exterior pose.** The camera sits inside a tree canopy
   that fills ~half the viewport (see the capture). The cost is independent of light tracing, so it is leaf-card
   overdraw / fragment cost at point-blank range. It is partly an artifact of the pose, but it matches G-18
   (Foliage 354 ms from a high camera on the laptop). **It must be in the P1 matrix.**
5. `cpuFrameTime` tracks the GPU (26 ms at 37 FPS) while `commandRecordTime` is 0.41 ms, so the frame is **GPU-bound**.

---

## 2. Instrumentation gaps (what we cannot see today)

From the 2026-09-24 inventory (`GpuProfiler`, `PerformanceProfiler`, the `/api/debug/*` routes):

1. **No true GPU frame time and no statistics.** `gpu_scopes` returns ONE frame, about 2 frames stale,
   with no history, median or percentiles. `gpuFrameTime` in `engine_timing` is fake (`deltaTime*1000`).
   `timestampValidBits` is never checked. `GPU_PROFILE_SCOPE` uses `##__LINE__` without an expansion
   helper, so two scopes in one block collide. Sky is unscoped. The **three shadow cascades share one scope**.
2. **Pipeline statistics are broken.** `lastPipelineStats[2]` is indexed by `NUM_STATS_SLOTS=3`, so the
   CHARACTER slot writes out of bounds (`GpuProfiler.h:55,86`, `.cpp:128`). This is a prime suspect
   for the "NVIDIA driver crash" in **G-155**, which has been blamed on the driver but never bisected.
   There are no stat slots for grass, foliage, water or post.
3. **No light counters.** Nothing reports lights registered vs enabled vs uploaded vs dropped, how many
   are coincident duplicates, the per-source split, or how many marches run per frame (mode 18 is a
   visual only).
4. **No per-tier voxel counters.** There are no cube/sub/micro instance counts (stored, meshed, drawn),
   no merged-vs-unmerged split, and no bytes per tier. `total_visible_faces` and `shadow_instances_drawn`
   are world totals that masquerade as per-view counts (**G-156**). `drawCalls` in `engine_timing` is
   actually chunks rendered.
5. **No CPU scopes in the render path or physics:** cull, occlusion BFS, light selection/sort, the
   `updateVfx` emitter hash, uploads, per-pass recording. Meshing is not split by tier.
6. **No memory telemetry:** no VRAM budget (`VK_EXT_memory_budget`), no RAM per tier.
7. **No GPU debug labels** (`vkCmdBeginDebugUtilsLabelEXT`), so an Nsight Graphics / RenderDoc capture
   shows unlabeled passes. We cannot get warp-stall or occupancy detail on the shader that costs 90%.
8. **Harnesses are one-offs.** The Ravenmere rigs hard-code another machine's paths, and the M4 jsonl has
   no producer script in the repo. There is no shared, pose-verified, interleaved-A/B sampler.

---

## 3. P0: instrumentation to build

Each item is small and independently shippable. Every counter or timer must be **validated against a
control** before any number from it is trusted, as §6 of this document requires.

| # | Item | Design notes | Validation (red → green) |
|---|---|---|---|
| **I1** | **GPU timer history + whole-frame GPU time** | A ring of the last N=240 frames per scope. **The key is scope path + occurrence index, not name.** The same name appears more than once per frame ("Character Shadows" is listed twice under Shadow Pass, once per cascade), so a name key would average two different passes. **Stale frames are rejected.** Today readback uses `VK_QUERY_RESULT_64_BIT` only and keeps the previous results on `VK_NOT_READY`, so a naive ring would record the same frame repeatedly and narrow the CI falsely. Read back with `VK_QUERY_RESULT_WITH_AVAILABILITY_BIT`, tag every sample with the frame index it was recorded in, and drop repeats; the count of dropped repeats is reported as `stale_skipped`. `GET /api/debug/gpu_timing?frames=N` returns median / p90 / p99 / mean / n per scope. Add a frame-bracketing timestamp pair (`TOP_OF_PIPE` at the first command, `BOTTOM_OF_PIPE` at the last) as the real `gpuFrameMs`. Check `timestampValidBits`. Fix the `__LINE__` macro. Add scopes: Sky, **Shadow Near / Mid / Far** individually, and Light Select (CPU). | An empty scope reads ~0. A known-cost dummy pass (a fullscreen quad N times) scales linearly. Children sum to the parent within 2%. |
| **I2** | **Pipeline stats: fix, then trust** | Fix the slot-2 OOB. Add slots for Grass, Foliage and Far Terrain. Report `frag_invocations / covered pixels` = **overdraw**. **G-155 is not assumed fixed by this.** It has only ever been seen on the laptop, in Ravenmere. The only dump in `crashes/` on the 4090 (`crash_20260923_214826`) is an unrelated `std::hash<std::string>` access violation in `phyxel.exe`, not `nvoglv64.dll`. The procedure is:<br>1. **Before the fix**, try to reproduce on the 4090: pipeline stats on, S-2 and S-3 at their worst poses, 5 minutes each.<br>2. If it reproduces, the fixed build must survive the identical run, and G-155 is closed as ours.<br>3. If it does not reproduce, the OOB fix stands on its unit red alone. G-155 stays **OPEN** with a note, and closing it moves to a laptop retest. | The OOB fix: red `PipelineStatsSlotsFitStorage` (§3.1). The G-155 link: the pre-fix repro above. A single full-screen quad gives frag_invocations ≈ W×H. |
| **I3** | **Light census** `GET /api/debug/light_stats` | Counts: registered, enabled, **uploaded**, dropped. **Unique positions among uploaded lights** (the duplicate count; positions equal within 1e-3 u). **By source needs new data.** `PointLight`/`SpotLight` carry no source today (`Light.h:14-30`: id, position, color, intensity, radius, enabled). Add a `LightSource` enum field: `EmissiveVoxel, Fixture, ItemEffect, Vfx, Api, Editor`. It is **a required parameter of `LightManager::addPointLight`/`addSpotLight`, with no default**, so an untagged call site does not compile. The compiler is the red test for coverage. Lights are created at **11 call sites along 6 paths** (grep-verified 2026-09-24):<br>• `EmissiveVoxel`: `RenderCoordinator.cpp:1230` (the `updateVfx` reconcile).<br>• `Vfx`: `RenderCoordinator.cpp:283` (the callback handed to `VfxSystem`).<br>• `ItemEffect`: `Application.cpp:1854` (`itemEffectSystem->setLightCallbacks`).<br>• `Fixture`: three injected lambdas, `Application.cpp:13492`, `:13721` (settlement) and `:17313`. `StructureForge.cpp:1297` only calls `ctx.deps.addPointLight`.<br>• `Api`: `Application.cpp:14279` (point), `:14298` (spot).<br>• `Editor`: `ImGuiRenderer.cpp:820`, `:875`.<br>There is **no NPC source**: `NPCEntity` only moves an already-attached light (`updatePointLightPosition`, `NPCEntity.cpp:97-98`). The signature change also means updating 34 test calls (`LightManagerTest.cpp` 29, `LightManagerViewerSpaceTest.cpp` 5; a 35th grep hit was a comment); they pass `LightSource::Api`. The field is CPU-only and never uploaded, so `PointLightGPU` is unchanged. Radius histogram. CPU ms for `getGPUData` sort + `updateVfx` reconcile. | A rig with K lights at known positions returns exactly K. An `oven_bread` returns its known glow-micro count and 1 unique position. |
| **I4** | **GPU light-work counters** (debug toggle, off by default) | `voxel.frag`, `grass.frag` and `foliage.frag` atomically add, per frame, into an SSBO: lights tested, lights passing radius, passing N·L, **marches run, total DDA steps, early-outs by hit**. Read back with the scope ring. This turns mode 18's picture into **marches/frame and steps/march**, which is the actual cost driver. Requirements:<br>• **Enable `fragmentStoresAndAtomics`.** It is not enabled today (`VulkanDevice.cpp:496-528`), and fragment-stage SSBO atomics without it are invalid Vulkan. Enable it after a support check; when unsupported, the toggle is **refused** with `counters_supported:false`.<br>• **Mechanism: a uniform branch** on a UBO flag (one pipeline, no respecialisation). Its off-cost is proven ≈ 0 by the A/A.<br>• **Count-only runs, plain atomics.** At ~59M marches/frame, atomic contention perturbs timing, so counter-on runs never feed timing; timing always comes from counter-off runs. Contention only makes counter runs slower; the counts stay exact. **No subgroup reduction.** `build_shaders.bat` passes no `--target-env`, so glslc targets Vulkan 1.0 / SPIR-V 1.0, and `subgroupAdd` needs SPIR-V 1.3. Raising the target would regenerate every committed `.spv`, which is out of scope for a debug counter. Nothing queries subgroup support either. | Rig: 1 light, a flat floor, a known lit area. Marches ≈ lit pixels. Steps/march matches the analytic distance / (1/9 u). Frame time with the toggle off equals frame time before the change (A/A). |
| **I5** | **Voxel tier census** `GET /api/debug/voxel_tiers` | Per tier (cube/sub/micro): stored objects, faces meshed pre-merge, faces after merge, faces in view (per-view, fixing G-156), shadow faces per cascade, **CPU bytes** (objects + map nodes) and **GPU bytes**. A world total plus an optional per-chunk dump. Faces grouped by whether they are hidden behind a covering fine neighbour (the §1 cube-face waste). | Rig: a known count per tier in one chunk (e.g. 1 cube, 27 subs, 729 micros) returns the analytic face counts before and after merge. |
| **I6** | **Per-tier draw toggles** (attribution by subtraction) | `POST /api/debug/tier_mask {main:[c,s,m], shadow:[c,s,m]}`. **The two passes use different methods, because a degenerate-vertex mask cannot see vertex cost.** The vertex shader and input assembly still run for a degenerate instance. §1b already measured raster + fragment spawn at ~0.03 ms, so a degenerate mask would report "micro costs nothing" even if the vertex cost were real, falsely clearing H-micro.<br>• **Main pass: per-tier draw ranges.** The mesher already counting-sorts faces by direction. Order by (direction, tier) inside that sort and record per-tier sub-ranges, so a masked tier is **not drawn at all** (it removes IA + VS + raster + fragment). This changes cost only: draw order within a direction range is not visible, since depth-tested opaque output is order-independent. It is pinned by a pixel-exact before/after capture with the mask off. It costs up to 3× more `drawIndexed` calls per chunk while a mask is active, and 0 extra when it isn't.<br>• **Shadow pass: post-vertex cost only, stated as such.** The mid cascade is GPU-driven `vkCmdDrawIndexedIndirect` per arena, so per-tier ranges would need per-tier indirect commands. Instead the shadow mask stays a degenerate-vertex mask (it measures raster + depth-write cost). Shadow **vertex** cost per tier comes from I2's `vs_invocations` on the SHADOW slot, times the per-vertex cost measured on R-M1. | Masking everything gives Static Geometry ≈ 0. Mask on/off is visually confirmed on a rig with all three tiers. The main-pass red: masking the micro tier on R-M1 must drop the STATIC-slot `vs_invocations` in proportion to the micro instance count. The proportion is vertices per instance, 4-6 depending on post-transform-cache reuse, measured once on the cube-only control. A degenerate mask would leave `vs_invocations` unchanged, which is exactly the failure this test catches. |
| **I7** | **CPU render + mesh scopes** | `PROFILE_SCOPE` inside `RenderCoordinator::drawFrame`: cull, occlusion BFS, light select, emitter reconcile, uploads, per-pass record. Split `rebuildAllFaces` into cube-greedy / sub-micro occupancy build / sub faces / micro faces / sort, with face counts per call and a **histogram of rebuild ms by micro count**. Add physics step scopes. | Scopes sum to the measured frame within 5%. |
| **I8** | **Memory telemetry** | `VK_EXT_memory_budget` (usage/budget per heap), the process working set, and I5's bytes per tier. Add to `/api/render/stats`. | Allocating a known buffer moves usage by that amount. |
| **I9** | **Debug labels for external profilers** | `vkCmdBeginDebugUtilsLabelEXT` mirrored from every `GPU_PROFILE_SCOPE`, plus object names on the main pipelines and buffers. This unlocks **Nsight Graphics GPU Trace / shader profiler** on the 4090 for instruction-level attribution inside `phxLightVisibility` (memory-latency vs ALU vs divergence). That is the "very high precision" layer the in-engine timers cannot give. | Labels are visible in a RenderDoc capture of the M4 tavern. |
| **I10** | **One harness:** `tools/perf_harness.py` | Replaces the per-run scripts. Inputs: exe (Release), project, scene recipe, pose list, A/B toggle set. Behaviour: launch → wait for **settle**, defined as `GET /api/debug/load_state` reporting `generation_pending == 0`, `remesh_pending == 0` and `remesh_idle_pending == 0`, **and** `visibleInstances` unchanged for 3 s. The harness **refuses to sample** while any of these is unmet, and records the settle wait in the jsonl → set pose and **verify pose** by reading `/api/camera` back → warm up 60 frames → sample I1/I3/I4/I5 over ≥240 frames → **interleaved ABAB** (never AAABBB) → append jsonl with git hash, dirty flag, config, GPU, driver, resolution (swapchain **and** viewport), the **actual present mode** read from the engine, pose, and a `provenance` field (`engine-generated: <route>` or `hand-placed rig: <rig id>`). `--compare` prints median deltas with bootstrap 95% CIs. Runs the **A/A noise floor** first, every session. | A/A on an idle scene reports a delta of ~0 and a CI that sets the noise floor. It refuses to run on a Debug exe unless `--allow-debug` is passed. |

### 3.1 Red tests (each shown failing before its item is built)

| # | Red test | What it reports when it fails (today) |
|---|---|---|
| I1 | `GpuTimingTest.RingReportsMedianOverFrames` (unit, a fake timestamp source) + `GpuTimingTest.TwoScopesInOneBlockCompile` | No ring and no route exist. The second test does not compile today because the `##__LINE__` macro collides. |
| I1 (live) | `perf_harness.py --check gpu_frame`: `gpu_frame_ms` must be ≠ `cpuFrameTime` on a CPU-bound rig (a spin-wait script) | Today `gpuFrameTime == deltaTime*1000` exactly, so it fails with "gpu == cpu at every sample". |
| I2 | `GpuProfilerTest.PipelineStatsSlotsFitStorage`: `std::size(lastPipelineStats) >= NUM_STATS_SLOTS` (static_assert-backed) | `2 < 3`: slot 2 (CHARACTER) is out of bounds. |
| I1 (stale) | `GpuTimingTest.NotReadyDoesNotAddSamples`: a fake query source that returns `VK_NOT_READY` for 10 frames must leave `n` unchanged and raise `stale_skipped` by 10 | Today's readback keeps the previous results on NOT_READY, so a ring built on it would count 10 new samples. |
| I1 (keys) | `GpuTimingTest.SameNameDistinctScopes`: two "Character Shadows" scopes under different parents keep separate histories | A name-keyed ring merges them. |
| I3 | `LightStatsTest.CountsUniquePositions`: an `oven_bread`-style cube with K glow micros gives `registered == K, unique_positions == 1` | The route does not exist. The live red is already in hand: the tavern gives `registered 29, unique 16` (§1b). |
| I3 (coverage) | **Compile-time:** `source` is a required parameter of `addPointLight`/`addSpotLight`, so an untagged call site fails to build | Shown red by making the parameter required *before* tagging: the build lists every untagged site (11 production, 34 test). A runtime test could not do this, because the §1b tavern only exercises 3 of the 6 paths. |
| I3 (live) | `LightStatsTest.EverySourceTagged` (L4): on the §1b tavern, `emissive_voxel + fixture + item_effect == registered` | `PointLight` has no source field today, so the route has nothing to count. |
| I4 (feature) | `perf_harness.py --check counters_supported`: the toggle is refused cleanly when `fragmentStoresAndAtomics` is absent, and accepted on the 4090 | Today the feature isn't enabled, so the shader atomics would be invalid (a validation-layer error with `PHYXEL_VALIDATION=1`). |
| I4 | `perf_harness.py --check marches` on R-L1 with N=1: `marches ≈ lit_pixels ± 5%` | No counter exists. Once built, the first red is the A/A: frame time with counters OFF must equal the pre-change binary. |
| I5 | `VoxelTierCensusTest.AnalyticFaceCounts`: one chunk holding 1 cube, 27 subcubes (one full cube) and 729 micros (one full cube) reports the analytic pre- and post-merge face counts per tier | The route does not exist, and `total_visible_faces` has no tier split. |
| I5 (per-view) | `perf_harness.py --check per_view`: `visible_faces_view` looking at the sky must be 0 | Today `total_visible_faces` is constant at every pose (G-156). |
| I6 | `perf_harness.py --check tier_mask`: with all tiers masked, Static Geometry < 0.05 ms, and masking one tier drops exactly that tier's faces from a pixel diff | The route does not exist. |
| I7 | `FrameProfileTest.RenderScopesSumToFrame`: the render children sum to the parent within 5% | Today `render` has no children. |
| I10 (settle) | `perf_harness.py --check settle`: right after `/api/structure/build` (while `remesh_pending > 0`), the harness must refuse to sample and log the wait | No harness exists. The §1b first look had no settle gate; it relied on a sleep. |
| I8 | `perf_harness.py --check vram`: allocating a known 64 MB debug buffer moves `vram_usage` by 64 MB ± 2 | No fields exist. |

### 3.2 API contract for the new routes

These follow the `/api/debug/*` conventions: **omitted means unchanged**, every response **echoes the
resulting state**, and clamps sit at the route with the failure they prevent written in a comment there.

| Route | Fields and units | Clamps (and what they prevent) | Echo |
|---|---|---|---|
| `GET /api/debug/gpu_timing?frames=N` | Per scope `{name, depth, n, median_ms, p90_ms, p99_ms, mean_ms}`, plus `gpu_frame_ms` with the same statistics. Also `frames_available`. | `N` is clamped to [1, ring size 240]: a larger N would read uninitialised ring slots as 0 ms and drag the median down. | `frames_used`, `timestamp_valid_bits` |
| `GET /api/debug/light_stats` | Counts: `registered, enabled, uploaded_point, uploaded_spot, dropped_point, dropped_spot, unique_positions_uploaded`. `by_source{emissive_voxel, fixture, item_effect, vfx, api, editor}`, read from the new CPU-only `LightSource` field (§3 I3). There is no `unknown` bucket: the source is required at creation. `radius_hist` in world units. `cpu_ms{select_sort, emitter_reconcile}`. | Read-only. | — |
| `POST /api/debug/light_counters {enabled}` + read via `gpu_timing` | Per frame: `lights_tested, pass_radius, pass_nl, marches, march_steps, march_hits`. Counts are per frame, summed over `voxel/grass/foliage.frag`. | Must be bool. Refused with `counters_supported:false` when the device lacks `fragmentStoresAndAtomics`: enabling it anyway would run invalid shader atomics. | `counters_enabled`, `counters_supported`, and `timing_valid:false` while enabled (a reminder that counter runs are count-only) |
| `GET /api/debug/voxel_tiers?per_chunk=1` (default 0) | Per tier `{stored, faces_premerge, faces_merged, faces_view, shadow_faces{near,mid,far}, cpu_bytes, gpu_bytes, covered_cube_faces}` | `per_chunk` output is capped at 512 chunks (`truncated:true`), so it cannot stall the 5 s game-loop budget. | `per_chunk`, `truncated` |
| `POST /api/debug/tier_mask {main:[c,s,m], shadow:[c,s,m]}` | Booleans, true = drawn | Each array must be exactly 3 booleans, and a malformed array is **refused, not partially applied**. A partial apply would leave a mask the caller didn't ask for. Resets on engine restart. | `main`, `shadow` |
| `POST /api/debug/pipeline_stats` | Accepts **both** `enabled` (editor) and `on` (standalone). Today the two hosts disagree. | Bool. Applies at the next frame boundary (already the case). | `enabled`, `applies_at_frame` |

**No existing field changes meaning.** New numbers get new names, and the old fields stay with a doc note:

| Existing field | What it actually holds | New field |
|---|---|---|
| `engine_timing.gpuFrameTime` | Fake: `deltaTime*1000` | `gpu_frame_ms` |
| `total_visible_faces`, `shadow_instances_drawn` | World totals, not per view (G-156) | `visible_faces_view`, `shadow_faces_view` |
| `drawCalls` | Chunks rendered | `chunks_rendered` |

Consumers get updated in the same change: `/engine-perf`, `lod_bench*.py`, and the doc-sync surface
(`ForwardingSurface.md`).

**Defaults:** I4 counters OFF, pipeline stats OFF, tier mask all-on. No shipped default changes in P0.

**Measurement hygiene (standing):** Release only. **Record the present mode; don't assume it.** The device
picks IMMEDIATE, then MAILBOX, then FIFO (`VulkanDevice.cpp:679-684`). IMMEDIATE and MAILBOX render
uncapped. FIFO caps at the refresh rate and makes FPS meaningless, though GPU scope times stay valid.
The harness exposes the chosen mode (a new `present_mode` field on `engine_timing`) and **flags any FIFO run**. Fixed resolution (2560×1440 on the 4090,
native on the laptop). Editor ImGui cost reported separately; the final numbers come from the
standalone `--test` build via `GameApiService`, which must gain I1/I3/I4/I5 parity. GPU clocks are not
locked on consumer cards, so we use n ≥ 240 frames and medians with CIs, never single frames.

---

## 4. P1: the measurement matrix

### 4.1 Rigs and scenes (small first, then real operating points)

Each rig is **one variable, inside one chunk (or one camera view), with a written prediction and a control.**

| Rig | Variable swept | Control | Prediction written before running |
|---|---|---|---|
| **R-L1 Light ladder** | Flat 1-chunk Stone floor plus one 5×5 room. N distinct point lights = 0, 1, 2, 4, 8, 16, 32 (cap), then 64 and 143 to show the cap's behaviour. Same pose. | N = 0 | Static Geometry grows ~linearly in (lights in radius × lit pixels) up to 32, then flat because of the cap. |
| **R-L2 Coincident emitters** | 1 × `oven_bread` vs the same visual with 1 merged light. | 1 merged light | Frame cost ∝ uploaded lights. The image differs by < 1/255 after tonemap once the intensities are summed. |
| **R-L3 March length** | One light, occluder distance 1 → 57 u. | Occluder absent | Cost ∝ steps/march. I4 gives the slope in ns/step. |
| **R-L4 Light radius / gates** | Radius 2 → 15 u, lights behind the camera. | — | Measures how much the radius and N·L gates actually cull, and what a frustum or cluster test would add. |
| **R-M1 Tier density** | A 16×16 slab rendered as cubes vs subcubes vs microcubes at **identical coverage**, **uniform material** (merge-friendly) and **checkerboard tint** (merge-hostile). | Cube slab | Uniform: cost ≈ equal (merge). Checkerboard micro: main-pass vertex cost ↑ ~81×, fragment ≈ flat, shadow ↑ with instances. |
| **R-M2 Micro-heavy chunk rebuild** | A chunk with 0 / 10 / 100 / 1000 / 10000 microcubes of mixed material. | 0 micros | Rebuild ms and RAM grow ~linearly. The string-keyed material lookups dominate the per-face CPU cost. |
| **R-M3 Distance** | R-M1 checkerboard micro slab at 10 / 40 / 80 / 120 / 200 u. **Capped at 200 u**: the residency radius is 256/352 u, and past it far-LOD chunks replace the slab (a second variable). Distances beyond that are a separate LOD study, not this rig. | Same slab as cubes | Past ~X u the micro faces are sub-pixel. Cost stays constant (no LOD) while the visual contribution vanishes. This is the case for micro LOD. |
| **R-M4 Covered cube faces** | A cube wall with a sub/micro skin in front of it. | No skin | I5 counts cube faces hidden behind covering fine neighbours. The cost equals their instance share. |
| **S-1 Tavern** | `M4TavernBench`, 3 fixed poses (interior bar, doorway, exterior). | — | Interior is light-bound. Exterior is light-bound near the windows and geometry-cheap. |
| **S-2 Settlements** | `M4DensityBench` (4 settlements / 25 buildings, seed 7), 3 poses (street, rooftop, high overview). | — | Street: lights > ambient > geometry. Overview: shadow and geometry grow, lights shrink. |
| **S-3 City** | A CityForge `tier:city` build (the densest lights and micro dressing we generate) as a size ladder of ~25 / 50 / 75 / 100 buildings with residents, fixed poses AND a fixed walk route. **Full plan: §16.** | — | The worst case for both hypotheses. It becomes the regression scene. |

Both GPUs: the **RTX 4090** (this machine) and the **RTX 1000 Ada laptop** (min-spec, where G-18 was
measured). A ranking that holds on only one of them is reported as such.

**Rig construction rules:**
- Every R-rig sits inside ONE chunk: the room/slab at local 8..23 of chunk (0,0,0), on the flat Stone
  floor at y=16.
- Place voxels, then **query them back** (`query_voxel` / `voxel_tiers`) before measuring. `fill` is async
  and reports no placed count.
- Lights are counted back via `light_stats`, never from the add responses.
- **Provenance:** every R-rig is **hand-placed** (rooms, slabs and API lights placed by the harness), and
  every number from one is labelled `hand-placed rig: R-xx`. That's legitimate for an isolated rig, but it
  is never presented as generator output (CLAUDE.md provenance rule). The S-scenes are
  **engine-generated** (`/api/structure/build`, `/api/settlement/build`, CityForge), and their raw
  generator responses are saved beside the data.

**Rig vs shipped defaults (state beside every number):**

| Delta | Effect on the numbers |
|---|---|
| Editor host, not the standalone: ImGui on (~0.1 ms), a docked viewport | Small GPU cost. The viewport size, not the window, sets the fragment count: §1b ran at a **1600×900 window** with a smaller docked viewport. P1 finals come from the standalone `--test` build at native resolution. |
| Flat bench worlds carry flora + grass (foliage density 0.25 in §1b) | Foliage and grass costs are pose-sensitive: §1b's 22 ms foliage had the camera inside a canopy. R-F1 sweeps canopy distance; S-scene poses are fixed and recorded. |
| Rigs have no terrain relief, water or NPCs | Isolates the variable. The S-scenes carry those costs instead. |
| R-L1 places lights by API at chosen positions | The shipped lights come from emissive voxels, fixtures and item effects, with duplicates (§1b). R-L2 and the S-scenes cover the shipped mix. |
| A single-GPU run | GPU clocks float on consumer cards, so n ≥ 240 frames, interleaved A/B, and the A/A noise floor is reported with each result. |

### 4.2 Per-scene attribution procedure

At every pose:
1. I1 per-scope medians. This establishes CPU- vs GPU-bound and which pass.
2. The **shader bisect ladder** (modes 11 → 12 → 13 → 14 → 15 → 16 → 17 → 0, via `POST /api/debug/shadow {mode}`)
   on Static Geometry. This apportions raster / texture / shadow / ambient / sun / **lights (gates vs march)**.
3. I4 counters: marches/frame, steps/march, lights passing each gate.
4. I6 tier masks, one tier at a time. In the main pass this gives the **full GPU ms per tier** (the masked tier
   is not drawn). In the shadow pass it gives **post-vertex ms per tier**; shadow vertex cost comes from
   `vs_invocations` × the per-vertex cost measured on R-M1.
5. I3 + I5 census: how many lights are duplicates, and how many faces per tier are in view.
6. I7 CPU scopes plus an R-M2-style rebuild histogram from a live edit (break a wall).
7. One Nsight GPU Trace capture (I9) at the worst pose, to confirm memory-latency vs ALU in the march.

### 4.3 Output

`docs/evidence/perf2026-09/*.jsonl` (raw) plus a results section appended to this doc: a **cost table
per scene × pose × component**, with 95% CIs, on both GPUs. H-light and H-micro are each marked
**CONFIRMED / REJECTED / PARTIAL (which pass, which GPU)**.

---

## 5. P2: optimization candidates (ranked by prior, gated on P1)

The rule is **no unintended visible change**, gated per §6. Two kinds of change are distinguished:
- **Equivalence** (most candidates): identical image for the same inputs.
- **Declared improvement** (L1 and L2): freeing or raising the 32-light upload cap lets lights that the
  cap silently dropped **appear**. That is visible and intended, since today's cap causes popping. It is
  gated as "identical for the same uploaded light set" plus a separate before/after capture that
  shows only the previously-dropped lights changing.

Order is by expected win / risk. P1 will re-rank them.

**Rejected outright:** a "low lights" / "reduced micro detail" quality setting. Detail stays
unconditional (FeatureDesignKeys). Any candidate that removes detail (S5, S6, L5) ships only if it is
pixel-invisible under §6.

### Lights

| # | Candidate | Why it should work | Visual risk |
|---|---|---|---|
| **L1** | **Merge coincident emitters.** **Key = (world cube cell, radius).** Sub- and microcubes already emit at their *parent cube* centre (`ChunkRenderManager.cpp` `emit(mc->getParentCubePosition()…)`, `worldPos = cell + 0.5`), so lights in one key group are already coincident. Merged light: `intensity = Σ tᵢ`, `color = Σ(cᵢ·tᵢ) / Σ tᵢ`, same position and radius. The radius scales (cube 1.0 / sub 0.75 / micro 0.5, burning 15 vs 9) stay **separate lights**. Emit groups in sorted cell order. **No cross-cube "fixture cluster" merge**: a fixture split by a chunk border would merge differently from a whole one. Also fix the nearest-32 **tie-break: world position, not light id**. Ids come from `updateVfx` re-adding lights in chunk-iteration order, so today *which light drops at the cap* depends on chunk order. | Removes duplicates that each cost a full march. `oven_bread` alone is ~65 → 1, and the §1b tavern goes 29 → 16. Frees the 32-slot budget for lights that actually differ. | **Exact for the same uploaded set:** shading is `lightColor * intensity * atten(dist, radius)` (`voxel.frag`, linear), and the grouped lights share position and radius, so their marches are identical. The declared change: lights previously dropped by the cap now appear. Also check the flame/flicker code paths read `intensity` linearly. |
| **L2** | **Clustered (froxel) light culling.** A compute pass bins lights into screen tiles × depth slices. The fragment loops only its cluster's list. Also lets us **raise the 32/16 cap** (an SSBO list, no fixed array). | The G-18 finding is literally "the radius gate culls nothing because all 32 lights are in range of every pixel". Clusters bound the per-pixel list by *local* density. This is the fix direction the ledger already recorded. | For the same light set, none by construction: same radius cut-off, same math per light. Needs a **conservative** sphere–froxel test, pinned by `ClusteredLightTest.MatchesBruteForce`. Raising the cap is the declared change above. **Default change:** `MAX_POINT_LIGHTS`/`MAX_SPOT_LIGHTS` are pinned in `LightManagerTest` and `HearthFuelTest` (F3, "exactly ONE billet is lit… MAX_POINT_LIGHTS = 32"). Update both pins in the same commit, with the reason. |
| **L3** | **Cheaper visibility per light.** (a) Use the two-level `phxSegmentBlocked` (empty-brick skip) instead of the flat micro DDA. (b) Screen-space or temporal reuse is rejected (it smears). (c) **Cache visibility**: lights and geometry are both static between edits, so compute per-light visibility once into a cached structure invalidated by edits (per-light cube shadow maps for the nearest K, or a per-surface-cell light cache in compute). | (a) helps if the steps/march is large (I4 decides). (c) replaces 59M marches per frame with a lookup and is the natural place to **fix G-157** (wall-mounted lights leaking), which the ledger says should be fixed by replacing the approach, not patching it. | (a) none; it must be bit-equal to the micro answer (rule R8). (c) resolution-dependent and is the biggest design. It needs a design-check pass and a `LightWallMatrixTraced` gate. **(c) must first resolve an existing chunk-visibility defect.** `phxOccupancySolid` returns *not solid* when a chunk's directory slot is `PHX_OCC_NO_CHUNK`, so light passes through any chunk outside the camera-centred occupancy window. Today that answer is recomputed per frame. A cache would **bake** it, so the design must say what happens at the window edge (e.g. treat unknown as unknown and re-trace when it becomes resident). |
| **L4** | **Grass and foliage: add the N·L / facing gate, or use the cluster list.** | They currently march every in-radius light. | A small A/B on the blade look. |
| **L5** | **Physically-bounded radius.** Verify the falloff reaches 0 at the radius (window function). If it does not, the cut-off is visible today; if it does, tighten the radius to where the contribution drops below 1/255 after exposure. | A smaller effective radius means fewer lights per cluster. | Must be proven sub-threshold. |
| **L6** | **Light selection CPU:** stop re-sorting all lights every camera move (spatial grid + hysteresis). | Only matters if I7 shows it. | None. |

### Static sub/micro detail

| # | Candidate | Why | Visual risk |
|---|---|---|---|
| **S1** | **Cull cube faces fully covered by sub/micro neighbours** (make culling two-way). This needs the **cross-chunk** fine lookup (`m_fineLookup`). That's justified: face culling is inherently a neighbour question, and the border re-mesh path already exists (R12, `0d99549a`). | Pure waste today. I5/R-M4 sizes it. | A hole if coverage is judged wrong. Coverage must be exact. Pinned by `FineCoverCullTest.ChunkedEqualsWhole` (the visible surface meshed chunk-by-chunk equals one whole-region pass), including a skin that straddles a chunk seam. |
| **S2** | **Cross-cube micro greedy merge** (Increment 4b). | Micro merge stops at the cube boundary, which is where the unmerged instance volume that drives shadow cost lives. | Existing merge-border T-junction crack class. Runs still stop at chunk borders, so **the crack pattern follows the chunk grid**. The crop-verified A/B must include a pose looking across a chunk seam. |
| **S3** | **Compact fine-voxel storage:** material id `uint16` instead of `std::string`, flat per-cube arrays or bitsets instead of heap objects plus 3-level hash maps; O(1) removal; demote the per-call INFO log. | Cuts RAM ~5-10× and speeds meshing, save/load and streaming. Invisible to rendering. | None (data layout only). Big refactor surface: physics, save format, API. |
| **S4** | **Off-thread meshing** (existing `OffThreadMeshingPlan.md`). | Removes the main-thread rebuild hitches that R-M2 quantifies. | None. |
| **S5** | **Sub-pixel micro LOD** (the C0 "9³ appearance brick" design). Beyond the distance where a micro face is below ~1 px, render the cube with a baked appearance instead of micro faces, in both the main and shadow passes. | Only if R-M3 shows real cost at distance. | Needs a dither-free, size-invariant transition (tier-ledger rules). Highest risk in this list. |
| **S6** | **Shadow caster pruning for micro in the mid cascade** (faces smaller than a shadow texel). | Shadow cost tracks instance volume. | A shadow-texel-scale test must prove it invisible. |

The ambient probe (15-23 ms on the laptop, ~3 ms plus ~4 ms probe update on the 4090) and **foliage
overdraw** (22 ms at a close-canopy pose on the 4090, §1b) are outside this program's two hypotheses.
They are **measured anyway** (§4.2 step 1), so P1 can promote them if they outrank micro detail. Add a
rig **R-F1**: camera distance to a canopy (0 / 2 / 8 / 30 u), foliage on/off, with alpha-test vs
opaque-card A/B.

---

## 6. Quality and validity gates (every optimization)

1. **Red-before-green on cost:** the harness A/B shows the win with a CI that excludes zero, on both GPUs,
   at every S-scene pose, not one.
2. **No unintended visible change:** golden captures at the fixed poses with `POST /api/debug/tonemap {"curve":0}`
   **and** with the shipping curve. Per-pixel diff, max and 99.9th percentile ≤ 2/255, and zero pixels over
   8/255 outside a documented mask. The defect-in-frame rule applies: each pose must show the lights and
   micro detail the change touches. For the **declared improvements** (L1/L2 cap), run this gate with the
   uploaded light set held equal. Separately, show that the before/after difference is confined to the
   footprint of the previously-dropped lights.
3. **Correctness gates:** `LightWallMatrixTraced`, the sealed-box light tests, `HearthFuelTest`, and the
   equality pins:
   - `EmitterMergeTest.ChunkSplitInvariant`: a fixture straddling a chunk seam gives the same light set as the whole region.
   - `EmitterMergeTest.ShadingLinear`: merged shading equals the unmerged sum at sampled points.
   - `ClusteredLightTest.MatchesBruteForce`: the clustered loop equals the flat loop, pixel-exact, for the same light set.
   - `FineCoverCullTest.ChunkedEqualsWhole`.
4. **Lighting doc gate:** any change to `occupancy.glsl` / `voxel.frag` / the receivers updates
   `LightingPipeline.md` §0 + §9 and runs `lighting_doc_check.py --update`. `build_shaders.bat` and the
   regenerated `.spv` are committed with the change.
5. **No chunk visibility:** light lists and LOD decisions depend on world position and camera only,
   never on the chunk grid or on chunk processing order (see the L1 tie-break).

---

## 7. Sequencing

| Phase | Contents | Exit criterion |
|---|---|---|
| **P0a** | I1, I2 (the G-155 repro attempt **before** the OOB fix, then the fix), I3, I10 skeleton + A/A noise floor | The noise floor is known. Light census numbers validated on R-L1/R-L2. |
| **P0b** | I4, I5, I6, I7 | Each counter is validated on its rig (§3 table). |
| **P0c** | I8, I9 | An Nsight capture with labels at the S-2 worst pose. |
| **P1** | The §4 matrix on the 4090, then the laptop | The results table in this doc; each hypothesis verdicted. |
| **P2** | The top-ranked candidates, one at a time, each through §6 | Measured wins, identical images. |
| **P1c** | The city benchmark (§16): I11-I13, the C-25…C-100 ladder, fixed poses + walk route | Growth table + hitch report on C-100; P2 re-ranked from it. |

**Likely first fixes, if P1 confirms the prior:** L1 (small, exact for the same light set, removes
duplicates) → L2 (the structural fix) → S1 / S2 (the geometry waste I5 will size).

**Phase status (2026-09-26):** P0a DONE · P0b DONE (I4 deferred) · P0c NOT STARTED · P1 DONE on the 4090
(laptop owed) · P2: L1 SHIPPED, P-DP built (default OFF), GI-1/GI-2 SHIPPED, L3a SHIPPED, structure-proxy
skip SHIPPED · P1c: ladder + fixed-pose growth table DONE, walk routes + hitch report OPEN. **The city data
re-ranked P2: lights (L2/L3c) are no longer first** — see §17.2 for the current order.

## 8. Open decisions (for the user)

1. **Target — DECIDED 2026-09-25:** no fixed budget; as fast as possible without sacrificing visual
   quality, measured at real load (~100-building cities, §16).
2. **L3c changes the light model** (cached visibility, which also fixes G-157). It is the biggest lever
   and the biggest design. Do we open a design for it now, or first take L1 + L2 and re-measure?
   *2026-09-26: deferred by data — in the city, night ≈ noon at every rung, so lights are not where the
   frame goes (§16.11). Revisit for interiors / the laptop.*
3. **Laptop access** for the min-spec half of P1.
4. **City-cap scaling — DECIDED 2026-09-26:** the building cap scales with site area (`scaleForSite`, §16.11).
5. **P-DP default — DECIDED 2026-09-27: ON.** The user watched the C-100 street pose flip off → on live
   (17 → 21 fps, 56.2 → 50.0 ms GPU), saw no difference, and approved (§16.13 has the full evidence).
   Pinned by `RenderDefaultsTest.DepthPrepassDefault`.
6. **OPEN — visible structure proxies (§17.2 step 3) are a LOOK change**, not an equivalence: needs the
   user's before/after sign-off at the city poses.

---

## 9. Design-check record

### 9.1 First pass (`/design-check`, 2026-09-24)

**Verdict: NEEDS WORK → items folded in.** No design key was violated in a way tuning cannot fix.

| # | Unresolved item | Where it now lives |
|---|---|---|
| 1 | The L1 merge key; drop the cross-chunk fixture-cluster merge; tie-break on world position, not id | §5 L1 |
| 2 | "No visible change" was wrong for L1/L2: freeing or raising the cap reveals dropped lights | §5 intro, §6 gate 2 |
| 3 | L3c must resolve the non-resident-chunk light leak (`PHX_OCC_NO_CHUNK` → not solid) before it caches anything | §5 L3 |
| 4 | API contract: units, unchanged, echo, clamps; unify `enabled`/`on`; new field names instead of redefining old ones | §3.2 |
| 5 | A named red test per P0 item, plus the four equality pins | §3.1, §6 gate 3 |
| 6 | R-M3 crossed the residency radius; the rig-vs-shipped deltas were unwritten | §4.1 |
| 7 | Default changes: the cap pins in `LightManagerTest` + `HearthFuelTest` | §5 L2 |

**Existing chunk-coupled behaviours found during the check.** This program did not introduce these,
but it must not entrench them:
- Light passes through chunks outside the occupancy window (`occupancy.glsl`, `PHX_OCC_NO_CHUNK`).
- Which light drops at the 32-cap depends on chunk iteration order (id tie-break).
- Merge-border T-junction cracks align to chunk borders, because runs stop there.

**Answers the check confirmed (no change needed):**
- Nothing here belongs to a generation stage. L1 is correctly placed at mesh time, since emissive voxels
  also come from the API, destruction, furniture baking and flora, not only generators.
- There is no world-recipe state; lights are re-derived from voxels on load.
- P0 changes no shipped default.

### 9.2 Second pass (`/design-check`, 2026-09-24, on the revised plan)

**Verdict: NEEDS WORK → items folded in.** Design keys 1-3 passed. The gaps were instrumentation
correctness in P0, which comes before any measurement can be trusted.

| # | Unresolved item | Evidence | Where it now lives |
|---|---|---|---|
| 1 | The I1 ring was keyed by name, which merges distinct scopes | "Character Shadows" appears twice in one frame's `gpu_scopes` dump (§1b run) | §3 I1, red `SameNameDistinctScopes` |
| 2 | I1 would record stale frames as new samples | Readback is `64_BIT` only; on `NOT_READY` the old results are kept (`GpuProfiler.cpp:75-116`) | §3 I1 (availability bit + frame-index dedup), red `NotReadyDoesNotAddSamples` |
| 3 | `light_stats.by_source` had no data to read | `PointLight`/`SpotLight` have no source field (`Light.h:14-30`) | §3 I3 (`LightSource` enum at six creator sites; **that site list was wrong, corrected in §9.4**), §3.2, red `EverySourceTagged` |
| 4 | I4 fragment atomics without the device feature | `fragmentStoresAndAtomics` is not enabled (`VulkanDevice.cpp:496-528`) | §3 I4 (support check, uniform branch, count-only runs, subgroup reduction), §3.2 |
| 5 | I6 degenerate masking can't see vertex cost, so it risked falsely clearing H-micro | Raster + fragment spawn measured ~0.03 ms (§1b) | §3 I6 (main pass: per-tier draw ranges; shadow pass: post-vertex only + `vs_invocations`), §4.2 step 4 |
| 6 | "Vsync off" was assumed, not checked | Present mode is IMMEDIATE > MAILBOX > FIFO (`VulkanDevice.cpp:679-684`) | §3 hygiene, I10 jsonl `present_mode` |
| 7 | Rig numbers lacked provenance labels | CLAUDE.md provenance rule | §4.1 rig rules, I10 jsonl `provenance` |

### 9.3 Third pass (`/design-check`, 2026-09-24)

**Verdict: NEEDS WORK → items folded in.** Design keys 1-4 passed. Three P0 test-plan gaps remained.

Confirmed sound on this pass: I1's frame bracket. There is one per-frame submit (`VulkanDevice.cpp:1029`,
with the frame fence). The other `vkQueueSubmit` calls are one-shot uploads outside the frame's command
buffer.

| # | Unresolved item | Evidence | Where it now lives |
|---|---|---|---|
| 1 | I4's `subgroupAdd` would not compile | `build_shaders.bat` has no `--target-env`, so glslc targets Vulkan 1.0 / SPIR-V 1.0; subgroup arithmetic needs SPIR-V 1.3; no subgroup support query exists | §3 I4: plain atomics |
| 2 | I2's red-before-green could not be falsified | G-155 was only seen on the laptop; the one dump in `crashes/` here is an unrelated `std::hash<std::string>` fault in `phyxel.exe` | §3 I2 (repro before the fix; otherwise G-155 stays OPEN), §7 P0a |
| 3 | I10's settle named no signal | `/api/debug/load_state` exposes `generation_pending`, `remesh_pending`, `remesh_idle_pending` | §3 I10, red `--check settle` (§3.1) |

### 9.5 Fifth pass: READY

Confirmed the pipeline-statistics slots are well-formed (one gated SHADOW begin/end pair; STATIC wraps
only `3882-3884`), so new slots are valid if **a new slot only wraps a pass that no other slot wraps**.

### 9.4 Fourth pass (`/design-check`, 2026-09-24)

**Verdict: NEEDS WORK (one item) → folded in.** Every design key passed.

Confirmed sound on this pass: I6's per-tier draw ranges. The main pass already draws each chunk as
`drawIndexed` sub-ranges from `getFaceDirRanges()`, with a full-draw fallback when the ranges are stale
mid-remesh (`RenderCoordinator.cpp:~1327-1352`). Splitting each direction range by tier extends that.

| # | Unresolved item | Evidence | Where it now lives |
|---|---|---|---|
| 1 | I3's source tagging named call sites that don't exist | Lights are created at 11 call sites along 6 paths; fixtures come through three lambdas; the editor panel was missing; `NPCEntity` creates no lights (it only moves an attached one). A tavern-only runtime test covers 3 of the 6 paths. | §3 I3 (the real site list; `source` required, so the compiler enforces coverage), §3.1, §3.2 |

---

### 9.6 Sixth pass (`/design-check`, 2026-09-25, on §16 P1c the city benchmark): NEEDS WORK → folded in

No design key violated (measurement only; no look, generation or gameplay change). Seven gaps, each now
answered in §16:
1. The walk route would not exercise streaming: the streaming anchor is the player unless overridden
   (`ChunkManager.h:184`). → I14, `stream_follow` drives the focus override from the path; V4 proves it.
2. `CameraPath` spends 1 s per segment regardless of length (`CameraManager.cpp:129-130`), pinned by
   `CameraPathTest` and used by cinematics. → opt-in constant-speed mode; default and pins unchanged; V1.
3. Scene persistence was assumed; the S-1 tavern did not survive a restart this session. → §16.1 content
   fingerprint, persisted-and-verified or rebuilt-and-matched.
4. API gaps: stop verb, conflict with `POST /api/camera`, a numeric speed max, the `frames` range. → I11/I12
   contracts; `kMaxPathSpeed` derived from measured streaming throughput (§16.6 step 0).
5. Instrumentation validated only in the city. → one-chunk rig R-P1 with V1-V5, numeric tolerances.
6. Time of day and present mode uncontrolled. → noon and night with the clock paused; non-vsync or void.
7. No written prediction for the growth table. → §16.7.

### 9.7 Seventh pass (`/design-check`, 2026-09-25, on §16 after 9.6): NEEDS WORK → folded in

The 9.6 fold-in was checked against the code and introduced/left six instrumentation-correctness gaps
(no design key violated):
1. The streaming focus override already has two owners (WorldForge build job, `/api/worldforge/focus`;
   `Application.cpp:13745-13757`, `:13854`), and a WorldForge release clears any override. → I14 holders:
   refuse-while-held naming the holder, release-only-own; V6 `StreamingFocusOwnerTest`.
2. 240-frame rings cannot hold a ~6,000-frame route, and polling mid-route adds main-thread work to the
   measured frames (API commands drain every frame, `Application.cpp:2912`). → I15 route recorder (sized
   once, stop-don't-wrap, `truncated` flag); I13 sends no request during a route; V3 on recorded poses; V7.
3. I11 would have been a third timing system. → built on the existing `PerformanceProfiler` scopes
   (`Application.cpp:3529-4084`) + `API Drain` + `Streaming Pump` + `drawFrame` + `Other`.
4. V4's rig could not stream (fixed-range Flat: `setMaxChunksPerUpdate(0)`, `ChunkManager.cpp:240`). →
   R-P2, Flat with `world.streaming: true`.
5. The fingerprint counts resident chunks only. → taken at a fixed anchor, load distance, pose, after settle.
6. `kMaxPathSpeed` cannot be both measured and compiled. → engine bounds (64 u/s player-motion clamp; the
   focus moves ≤ `kFocusStep` = 64 u per FRAME, WorldForge's shared no-teleport constant,
   `Application.cpp:13749` — note it is per frame, not per second: WorldForge re-polls every frame,
   `WorldForgeBuildService.cpp:207`) + a per-machine route speed chosen by the harness and recorded.

### 9.8 Eighth pass (`/design-check`, 2026-09-25, on §16 after 9.7): READY

Verified against code: the override signature change touches only the two WorldForge call sites and no
test; GPU samples have one entry point (`history.addFrame`, `GpuProfiler.cpp:151`) for the recorder's
serial matching. Two amendments folded in: the streaming pump's alternate-frame cadence is reported and
judged by parity (not counted as hitches); all new routes go through `PerfApi` in both hosts.

### 9.9 Ninth pass (`/design-check`, 2026-09-26, on the §17 roadmap + the shipped §16.12 skip): NEEDS WORK → folded in

Verified against code. Findings, each folded into §17.2 at the step named:
1. **Shipped defect:** `set_far_terrain` does not echo `structures` / `structures_skip_invisible` /
   `trees` → step 0.
2. **Shipped gap:** the §16.12 skip predicate has no unit test (L4 pixel gate only) → step 0,
   `StructureLodSkipTest` against a CPU transcription of the shader fade.
3. **P-DP default is unpinned** (no test references the prepass) and **the night pixel gate cannot use
   game pause** (it stops the emissive reconcile) → step 2: add the pin; night freeze = clock + grass /
   foliage off + residents despawned, game running.
4. **Visible-proxy LOD had no small rig** → step 3: one building, one chunk, distance as the only
   variable, prediction + L0-vs-L0 control, then user sign-off.
5. **SH1 would drop reflected shadows** (`mirror_voxel.frag` samples the cascades) and needs a
   conservative cull + `ShadowCasterCullTest`; **SH2 risks stepped shadow motion** and touches the
   cascade fit (`LightingPipeline.md` §0 rule) → step 4.
6. **Residents-OFF mechanism was undecided;** built rungs re-derive residents at load, so the generator
   param alone needs a rebuild → step 7: runtime despawn over the API, verified via `/api/npcs`.
7. Tree-regrowth fix gets its pin named (`PlacementChunkEqualsStreamedChunk`) → §17.3.
No design key is violated (no chunk-coupled appearance, no generation-stage change, no detail removed
behind a flag); step 3 remains a declared LOOK change needing user sign-off (§8 #6).

### 9.10 Tenth pass (`/design-check`, 2026-09-26, on §17 after 9.9): NEEDS WORK → folded in

Three mechanisms 9.9 relied on did not hold against the code, plus two undefined metrics:
1. **Step 7:** per-NPC despawn is undone by `ResidentSpawner::update`'s throttled rescan → a route that
   suspends the spawner + `despawnAll()`, residents verified 0 at window start AND end.
2. **Step 2:** fire/VFX flicker and wind are time-driven, so the night freeze could never reach a ~0
   control → an effect-time-hold debug knob (time input only, game still running), with the unfrozen
   timing-matched gate as the stated fallback; the pin is named `RenderDefaultsTest.DepthPrepassDefault`.
3. **Step 3:** there is no chunk-eviction API, and inside 256 u the proxy is already discarded → rig at
   280/320/346/400/500 u (the band where proxies are visible and still L0), no eviction, prediction restated.
4. **Step 4 SH1:** the shadow-map-equality test needed the GPU → headless brute-force ray-vs-AABB test
   (`CullKeepsEveryCasterThatCanShadowAView`), red on a main-frustum-only cull (mirror case).
5. **Step 4 SH2:** "shadow-edge displacement" had no metric → consecutive-frame max change in the shadowed
   region, cached vs uncached, threshold uncached p99 + 2/255; red on a K-frame cache without rebase.
No design key violated.

### 9.11 Eleventh pass (`/design-check`, 2026-09-26, on §17 after 9.10): NEEDS WORK (small) → folded in

1. **Step 3:** `kStructureLevelDist` is `static constexpr`, so the rig could not A/B ladders without a
   rebuild per candidate → runtime `structure_ladder` knob (5 values, strictly ascending, refused
   otherwise, echoed; `lod_report` reads it), residency recorded per capture.
2. **Step 2:** "effect time" named both domains it must hold (GPU `ubo.elapsedTime`, ~20 shaders; CPU
   `FireEmitterManager` / `VfxSystem` light modulation) and the player kept out of frame.
3. **Step 2:** the frozen control is numeric (max ≤ 3/255, 0 px over 8/255): the GI probe rotation
   advances every rendered frame regardless of pause (`GiProbeField.cpp:124`), measured 0–3/255.
No design key violated.

### 9.12 Twelfth pass (`/design-check`, 2026-09-27, on the proxy mesh merge, §17.2 step 3): NEEDS WORK → folded in

Verified that shading is tessellation-independent (world-projected UVs, position/texture/face vertices,
translation-only instances, array-layer textures) and that the merge input is a proxy's own cell set,
never a chunk. Folded into step 3: (1) watertight merge (split edges at neighbouring vertices) +
`NoTJunctions` with a plain-greedy red case; (2) scope = trees and structures, with a forest pose added;
(3) a runtime `proxy_mesh_merge` knob that retires/rebuilds proxies and echoes rebuild progress, so the A/B
stays interleaved in one session; (4) merged/unmerged quad counts in `lod_report`; (5) written predictions
and the default pin. No design key violated.

### 9.13 Thirteenth pass (`/design-check`, 2026-09-27, on the proxy mesh merge after 9.12): NEEDS WORK → folded in

Checked the rebuild knob against the code: tree species have no rebuild path and `cleanup()` frees
immediately, so a naive rebuild would free buffers in-flight frames draw (the device-loss class). Folded
into step 3: (1) `retireAllSpecies()` through a frame-deferred graveyard + a generation stamp that drops
stale builds; (2) the merge flag snapshotted per job (no static read on builder threads); (3) named test
inputs (stepped mixed-material building, a real tree template, every level); (4) edge splitting spatially
indexed, with a predicted and measured city-wide rebuild time. No design key violated.

## 10. P0a results (2026-09-24, RTX 4090, Release)

Evidence: `docs/evidence/perf2026-09/`. Unit suite after the batch: 4025 passed, 20 skipped, 2 failed. Both
failures are documented as pre-existing: `AtlasManagerTest.BuildAtlasFromSourcePNGs` (StructurePipelineGaps
2026-09-24, fails whenever the BC7 cache exists) and `FineFaceMerge.SubcubeMerge_CrossCubeSplitsOnLightBoundaryBetweenCubes`
(UnifiedLightingPlan M0 fallout, G-152).

### I2: G-155 was ours, not the driver's (FIXED, L4)

- **Mechanism.** `lastPipelineStats` was declared `[2]` while `NUM_STATS_SLOTS` is 3. The CHARACTER
  slot's readback wrote 40 bytes past it into `frames` and `lastFrameResults`; the next timestamp readback
  passed the driver a garbage query count, and the driver read off the end of a page. That is why the
  title scene (no character drawn) worked and the town crashed.
- **Repro before the fix (4090):** `M4DensityBench` + `POST /api/settlement/build`
  `{era:medieval, tier:town, seed:7, position:{-40,16,-20}, width:80, depth:40, terrain:true}` (engine-generated,
  raw responses `s2_town_build_*.json`). Enabling stats at the street pose crashed within seconds:
  `0xC0000005 in nvoglv64.dll` (`g155_repro_prefix_crash.txt`).
- **Red:** `static_assert` on the array size, failing with the old size (`i2_red_build.log`).
- **Green, L4:** the identical scene and poses, 300 s each at street and overview: 298/298 samples alive,
  CHARACTER stats non-null in every sample, no repeated serials, no crash (`g155_soak_postfix.jsonl`).
- **Also fixed:** the editor's `set_pipeline_stats` treated an omitted field as ON; both hosts now share
  `PerfApi::setPipelineStats` (accepts `enabled` or `on`, omitted = unchanged).
- Not yet re-run on the RTX 1000 Ada laptop.

### I1: GPU timing history (DONE, L2 + L4)

- `GpuTimingHistory` (pure, unit-tested): 240-frame ring keyed by scope path + occurrence; each frame
  accepted once by the serial of the frame that recorded it. 7 unit tests, shown red first
  (`i1_red_build.log`, `i1_macro_red_build.log`).
- `GpuProfiler`: availability-checked readback (a frame is used only if every query is available),
  `timestampValidBits` masking (64 on the 4090), a whole-command-buffer bracket (`GPU Frame`), new scopes
  `Sky` and `Shadow Mid / Near / Far`, and the fixed `GPU_PROFILE_SCOPE` macro (its `__LINE__` never
  expanded).
- Routes: `GET /api/debug/gpu_timing?frames=N` (both hosts), `engine_timing.gpu_frame_ms` (the last frame,
  read through an atomic because that route runs on the HTTP thread) and `engine_timing.present_mode`,
  `gpu_scopes.serial`.
- **Live (S-2 overview):** GPU frame median 13.3 ms / p99 18.7 ms. The two "Character Shadows" now have
  separate histories (0.055 ms under Mid, 0.027 ms under Near). `Shadow Far` has n=60 of 240 (its cadence).
  `frames=99999` clamps to 240. 0 stale, 0 not-ready (`i1_gpu_timing_live_s2_overview.json`).
- `check gpu_frame`: real GPU frame 4.26 ms vs CPU frame 5.63 ms, while the legacy `gpuFrameTime` equals
  the CPU frame exactly, which confirms it was fake.

### I3: light census (DONE, L2 + L4)

- `LightSource` is a required first parameter of `addPointLight`/`addSpotLight`. The red build listed the
  engine library's untagged sites (`RenderCoordinator.cpp` 283 / 1234, `ImGuiRenderer.cpp` 820 / 875;
  `i3_red_build.log`). The editor and test sites were tagged before their layer of the build ran, so that
  red covers the core library only.
- 11 production sites and 34 test calls tagged. 5 `LightStatsTest` unit tests.
- **Live (S-1 tavern, `i3_light_stats_tavern.json`):** 29 registered / 29 uploaded / **16 unique positions
  / 13 duplicate uploads**. By source: 20 emissive voxel, 7 fixture, 2 item effect (sum = registered, so the
  L4 `EverySourceTagged` check passes).
- **CPU cost is negligible:** selection 0.005 ms, emitter hash < 0.001 ms, last emitter rebuild 0.03 ms.
  **L6 (light-selection CPU) is not a lever** at this scale. Note: selection re-ran on 2169 frames, i.e.
  essentially every frame, so something marks the light set dirty per frame. It is cheap here, but worth
  a look at city scale.
- S-2 town: 119 lights at 36 positions, up to 42 identical lights at one position (70% duplicates).

### I10: harness (skeleton DONE)

- `tools/perf_harness.py`: Release-only (`/api/status` now reports `build_config`), a settle gate on
  `load_state`, pose read-back, history-based windows, provenance on every row.
- **Self-checks, all passing live:** `release` (it failed against the engine built before the field
  existed), `gpu_frame`, and `settle` (refused with `remesh_pending: 3` right after a tavern build, then
  accepted after 3.55 s).
- **The first A/A run caught a method bug.** Plain A,B,A,B ordering put A first in every pair. The
  interior GPU frame drifted 27.8 → 33.8 ms during the run with nothing changed, and the exterior A/A "found"
  +0.80 ms with a CI of [+0.26, +1.57], which excludes zero. **Fixed:** counterbalanced ABBA order, paired
  differences, and no verdict below 8 pairs (`i10_aa_noise_floor_s1.jsonl` is the flawed run, kept).
- **Noise floor (S-1, n=10 pairs, `i10_aa_noise_floor_s1_n10.jsonl`), every A/A CI includes 0:**

| Scope | Interior: median, A/A 95% CI | Exterior: median, A/A 95% CI |
|---|---|---|
| GPU Frame | 34.1 ms, [−0.40, +0.22] | 55.5 ms, [−0.82, +1.18] |
| Static Geometry | 23.4 ms, [−0.31, +0.16] | 8.0 ms, [−0.11, +0.06] |
| Grass | 2.9 ms, [−0.12, +0.23] | 7.9 ms, [−0.09, +0.16] |
| Foliage | 0.23 ms, [−0.002, +0.001] | 31.8 ms, [−0.48, +0.47] |
| Shadow Pass | 0.71 ms, [−0.011, +0.009] | 0.65 ms, [−0.004, +0.011] |

### Open items from P0a

1. **Unexplained drift.** Within one run the interior GPU frame climbed ~20% with nothing changed, and
   absolute levels sit ~20% above the §1b first look at the same pose and light census. Day/night is
   ruled out (disabled, pinned at noon). Candidates: GPU clock / thermal state, the tavern NPC moving, GI
   probe convergence. Counterbalancing protects A/B verdicts from it, but it has to be explained before
   absolute numbers are compared across sessions.
2. **Light re-selection every frame** (2169 selections), cheap now but unexplained.
3. The G-155 fix is not yet confirmed on the laptop GPU.
4. `resolution` (swapchain + viewport) is not yet exposed, so the harness cannot record it.

---

## 11. P0b results (2026-09-24, RTX 4090, Release)

### I5: voxel tier census (DONE, L2 + L4)

- `GET /api/debug/voxel_tiers?per_chunk=0|1&covered=0|1`. Per tier (cube / sub / micro / LOD cell):
  stored objects, instances after merge, unit faces before merge, instances drawn this view (main
  pass + each shadow cascade), GPU bytes, and a CPU-byte floor for sub/micro objects. `covered=1` adds
  the cube faces fully hidden behind opaque sub/micro detail (sizes S1).
- The mesher's direction sort now keys (direction, tier). Faces are built cube → sub → micro and the
  sort is stable, so **this reorders nothing**; it only records each tier's run.
- **L4 rig (hand-placed, one chunk, deltas vs a control census; `i5_rig.py`, `i5_rig_census.json`).**
  All four analytic predictions matched exactly: 1 cube (1, 6, 6), 27 subcubes (27, 6, 54),
  729 microcubes (729, 6, 486), and a covered skin +1.

### I6: per-tier draw ranges (DONE, L4)

- `POST /api/debug/tier_mask {main:[c,s,m], shadow:[c,s,m]}`. A masked tier is **not drawn at all**: in
  the main pass via the tier runs, in the GPU-driven mid cascade via one indirect command per kept
  (direction, tier) run, and in the legacy near-cascade loop via sub-range draws. **No shader change**:
  the plan's degenerate-vertex fallback for shadows was unnecessary, because the indirect commands are
  written on the CPU. The default mask takes the old paths untouched.
- **L4 (`i6_check.py`, `i6_check.json`, pipeline stats on).** Masking each tier zeroed its instances in
  view and cut vertex work in exact proportion: **4.00 VS invocations per instance in the main pass**
  (6-index quad) and **~10.0 per instance in the mid shadow cascade** (36-index cube). Zero skipped
  chunks and zero command overflows. Visual pair: masking micro removes exactly the microcube block
  (`i6_rig_all_tiers.png`, `i6_rig_micro_masked.png`).
- **Side finding:** a shadow-caster instance costs **2.5×** the vertex work of a main-pass instance
  (the M5 36-index requirement).

### I7: CPU render-path scopes + mesh phases (DONE, L4)

- `GET /api/debug/cpu_timing?frames=N`: drawFrame > LOD Update, Light Occupancy, Dirty Chunk Flush,
  Fence Wait, Acquire, Frame Setup (> Light Select+Upload), Record (> Shadow Pass, Scene Pass > Static
  Geometry > Occlusion BFS), Submit, Present. It reuses the I1 history class, so it has the same
  statistics and the same serial rule.
- `GET /api/debug/mesh_timing?reset=0|1`: rebuildAllFaces split into cube greedy / fine occupancy / sub
  faces / micro faces / sort, and whole-rebuild time bucketed by the chunk's microcube count.
- **L4 (`i7_check.py`, `i7_check.json`).**
  - **Sum rule passed:** drawFrame's children account for 99.8% of it (3.351 of 3.356 ms).
  - **Fence Wait is 2.58 of 3.36 ms**, so the CPU is idle waiting on the GPU: the frame is GPU-bound
    (tavern bench, exterior).
  - **The mesh prediction FAILED, from a rig error, not an instrument error.** I predicted a rebuild in
    the 100-999 microcube bucket, assuming the chunk was otherwise empty. It already held ~8,088
    microcubes (flora on the bench world), so the rebuild saw 8,817 and landed correctly in 1000-9999.
    The phase partition held (12.563 vs 12.565 ms). The next mesh rig must control chunk contents
    (census the chunk first).
  - **First CPU number for H-micro:** re-meshing one chunk holding ~8.8k microcubes took **12.6 ms on
    the main thread, half of it (6.3 ms) in microcube face generation.** That is a frame-sized hitch
    per edit. R-M2 will measure the curve.

### I4: GPU light counters (DEFERRED)

- A counter path that is provably free when off needs either a pipeline variant per receiver shader or
  accepting unmeasured overhead in `voxel.frag` (the hottest shader) and `occupancy.glsl` (included
  by every scene shader). The overhead cannot be measured cleanly: the old build would have to run as
  a separate process, and cross-process drift is ~20% (§10).
- Its question ("too many marches or too costly each") was answered on the laptop by G-18 run 6, and
  debug modes 17/18 reproduce it on the 4090 without new code. **Revisit when L3a (cheaper march) is
  weighed**, since that decision needs steps per march.
- **Finding while designing it:** `grass.frag`, `foliage.frag` and `sky.frag` treat
  `debugShadowMode >= 3` as a debug view and paint flat black. So **every bisect mode (11-18) blacks out
  grass, foliage and sky**: the bisect ladder never measured vegetation lighting cost. Grass and foliage
  need their own attribution (the light-trace toggle does it: §1b showed grass paying 4.4 ms of march).

---

## 12. P1 results on the RTX 4090 (2026-09-24, Release)

**Method.** `tools/perf_harness.py`, counterbalanced A/B, 8 pairs of 240-frame GPU windows per cell,
paired median with bootstrap 95% CI (`*` = CI excludes 0). Each component's cost = the frame time saved
by removing it (B − A; negative = saving). Scripts and raw data: `docs/evidence/perf2026-09/p1/`
(`run_scene.sh`, `run_extra.sh`, `ab_*.json`, `summarize.py`, `<scene>_<component>.jsonl`,
`<scene>_summary.md`, `<scene>_census.json`). Same binary for every run (`c63cb75e`).
Editor host, 1600×900 window, present mode IMMEDIATE.

### S-1: engine-generated tavern (M4TavernBench, Flat)

Provenance: `POST /api/structure/build` v2 tavern 14×7, 2 stories (`p1/s1_tavern_build.json`).
Census: 29 lights at 16 positions; 192,334 microcubes and 53,745 subcubes stored (flora + tavern).

| Remove → saves (GPU frame, ms) | Interior (22.7 ms) | Exterior (20.0 ms) |
|---|---|---|
| **Light march (trace off)** | **−13.8 [−13.9, −13.6]*** (61%) | **−10.2 [−10.2, −10.2]*** (51%), of which grass −3.7 |
| Microcubes, main pass | −3.0* (Static −7.4, but grass +2.4, foliage +0.6) | −1.3* (Static −4.4, grass +1.9) |
| Subcubes, main pass | −3.3* | −1.0* |
| Microcubes, shadow | +0.08 (n.s.) | +0.08* (the mask's extra commands cost more than it saves) |
| Subcubes, shadow | −0.01 (n.s.) | +0.20* (same) |
| Foliage | −0.3* | −2.2* |

**What the micro deltas are made of.** Removing a tier does not remove its pixels: whatever was behind
takes them and is shaded instead (hence grass/foliage going UP). A second pass took shading out of the
baseline:

| Remove microcubes, main pass (interior) | Static Geometry saved | GPU frame saved |
|---|---|---|
| Normal (lights on) | 7.4 ms | 3.0 ms |
| Baseline with lights off | 1.9 ms | 0.4 ms |
| Baseline raster-only (debug mode 11: no shading) | **0.01 ms** | (frame +3.6 ms: more grass exposed) |

(Exterior: 4.4 / 1.3 / 0.01 ms. Subcubes: 3.35 / 0.55 / 0.00 ms interior.)

**S-1 verdicts:**
- **H-light: CONFIRMED, dominant.** The per-light occupancy march is 51-61% of the GPU frame, and it is
  also the bulk of what fine detail "costs": of the 7.4 ms that microcube-covered pixels cost indoors,
  ~5.5 ms is light marching on those pixels.
- **H-micro on the GPU: REJECTED as geometry.** Microcube and subcube geometry (vertex + raster + draw)
  costs ~0.01 ms here. What costs is shading pixels, and those pixels are shaded whether micro detail or
  something else covers them. Micro shadow casting is below the noise.
- **H-micro on the CPU/RAM side: PARTLY CONFIRMED** (from §11 + the census). ~136 bytes per stored
  microcube (26 MB floor for 192k), and a 12.6 ms main-thread re-mesh of a chunk holding ~8.8k
  microcubes. These are hitch and memory costs, not steady-state frame costs.
- **S1 (cull covered cube faces) is a small lever here:** 276 of 25,288 cube unit faces (1.1%) are fully
  hidden within chunks (15,751 more at chunk borders were not decided).
- **Foliage is a real third cost outdoors** (2.2 ms at a normal pose, far more at close canopy range, §1b).

### S-2: engine-generated town (M4DensityBench, Perlin seed 7)

Provenance: `POST /api/settlement/build` medieval/town seed 7, 80×40, terrain (`p1/s2_town_build.json`).
**The generator placed 4 buildings** (the response flags `below_tier_min: 15`; this is ONE town, not the
M4 bench's 4 settlements / 25 buildings). Census: 119 lights (32 uploaded, the cap; **12-14 of the 32
slots are duplicates**); **553,149 microcubes stored = 75 MB CPU heap floor**, 183,992 subcubes,
1.32 M cubes; 21,129 micro instances after merging (~17.8k in view on the street).

| Remove → saves (GPU frame, ms) | Street (18.4 ms) | Overview (10.6 ms) |
|---|---|---|
| **Light march** | **−5.3 [−5.4, −5.3]*** (29%) | **−5.2 [−5.3, −5.1]*** (49%) |
| Microcubes, main pass | −2.1* (Static −3.6, grass +0.7) | −1.3* |
| … with lights off | −1.7* | −0.03* |
| … raster-only (geometry alone, Static) | −0.03* | ≈0 |
| Subcubes, main pass | −2.0* | −1.2* |
| … with lights off / raster-only | −0.3* / −0.01 (Static) | −0.03* / ≈0 |
| Microcubes, shadow | −0.13* | −0.23* |
| Subcubes, shadow | −0.13* | −0.10* |
| **Foliage** | −1.4* | **−2.4* (22%)** |

Covered cube faces: 1,992 of 90,269 (2.2%) within chunks.

### P1 verdicts on the 4090 (both scenes)

1. **H-light: CONFIRMED as the #1 GPU cost.** The per-light occupancy march is **29-61% of the GPU frame**
   (S-1 13.8 / 10.2 ms, S-2 5.3 / 5.2 ms). **Duplicate lights are 40-45% of uploaded slots** in both
   scenes (13 of 29; 12-14 of 32).
2. **H-micro as GPU geometry: REJECTED.** Removing the microcube or subcube tier with shading taken out
   saves ≤ 0.03 ms of Static Geometry at every pose, and shadow casting of fine tiers is 0.1-0.25 ms. What
   fine detail "costs" in the normal frame is the **shading of the pixels it covers**, dominated by the
   light march. Those pixels are shaded by something else if the detail is removed, so fewer or
   coarser voxels would not buy the frame back. Fixing lighting cost fixes it.
3. **H-micro as CPU/RAM: CONFIRMED as a hitch/memory problem, not a frame-rate one.** ~136 B per stored
   microcube (**75 MB for one 4-building town + forest**); a 12.6 ms main-thread re-mesh for a chunk
   holding ~8.8k microcubes (half of it in micro face generation). This scales with world detail and
   edits, not with what is on screen.
4. **Unexpected #2: foliage** (1.4-2.4 ms at normal poses, 22% of the overview frame; 22+ ms at close
   canopy range, §1b). It is outside both hypotheses and belongs in P2.
5. **Ambient/probe shading is the residual.** With lights off, pixels covered by fine detail still cost
   ~1.7-1.9 ms at near poses: the ambient-probe lookup + PCSS (§1b put the probe at ~3.2 ms).

### P2 ranking this supports (4090; the laptop run is still owed)

| Rank | Candidate | Why, from the data | Expected effect |
|---|---|---|---|
| 1 | **L2 clustered light culling** | The march is 29-61% of the frame and every pixel runs every in-range light. Clusters bound the per-pixel list by local density | The largest lever. Same image for the same light set |
| 2 | **L1 merge duplicate emitters** | 40-45% of uploaded slots are duplicates. **Under the cap** (S-1: 29 → 16) marches fall ~45%, so ~6 ms of S-1's 13.8. **At the cap** (S-2) the count stays 32, but real lights replace duplicates: the image improves, cost holds | Small change, big correctness and headroom win. Do before L2, since it also shrinks cluster lists |
| 3 | **F1 foliage cost** (new) | 1.4-2.4 ms at normal poses, 20+ ms point-blank | Needs its own attribution: overdraw vs shading |
| 4 | **S3 compact fine-voxel storage + S4 off-thread meshing** | 75 MB / 553k micros; 12.6 ms re-mesh hitch per edit | Memory and hitches, not FPS |
| 5 | L3a cheaper march / L3c cached visibility | Depends on steps per march (I4, deferred) and on what L2 leaves | Re-measure after L1 + L2 |
| — | S1, S2, S5, S6 (geometry-side micro work) | Geometry ≈ free, covered faces 1-2%, fine shadow casting ≤ 0.25 ms | **Deprioritised** by measurement |

### 12b. Follow-up (2026-09-25): overdraw and in-range lights per pixel

Prompted by two videos the user shared: one on Forward+ tiled light culling, one on GTA 6's probe-based
lighting. Script: `p1/m_overdraw_marches.py`; results in `p1/s1_overdraw_marches.json` and
`p1/s2_overdraw_marches.json`.

**Method.**
- **Overdraw** = Static Geometry fragment-shader invocations (pipeline stats, normal shading) ÷ pixels
  covered by static geometry.
- **Covered pixels:** the scene target is window-sized (1600×900). The viewport stretches it, so the
  covered fraction of the panel equals that of the target. Coverage comes from a debug-mode-11 capture.
  HUD overlays hide ~1-2%.
- **Marches per pixel** = debug mode 18 (the R channel holds count/32) over the covered pixels, captured
  with exposure 1 and a linear curve.
- **Control:** mode 11 paints linear 0.5, which must read 188 after sRGB encoding. It read **188 at every
  pose**, so the decode is valid.

| Scene / pose | Covered | Static FS invocations | **Overdraw** | Visible pixels: marching lights | Lights: uploaded / unique |
|---|---|---|---|---|---|
| S-1 interior | 94.8% | 3.45 M | **×2.53** | mean **9.1**, 99% march | 29 / 16 |
| S-1 exterior | 48.7% | 2.08 M | **×2.97** | mean 2.8, 38% march | 29 / 16 |
| S-2 street | 82.1% | 3.19 M | **×2.69** | mean **0.70**, 12% march | 32 / 20 |
| S-2 overview | 63.2% | 1.65 M | **×1.81** | mean **0.15**, 2% march | 32 / 18 |

**What it shows:**
1. **2-3 fragments are shaded per visible pixel**, and each runs the full light loop. This is overdraw
   and/or 2×2 quad overshading of tiny faces. The measurement cannot yet split the two, and it matters:
   a depth prepass removes overdraw but not quad overshading.
2. **Indoors, the lights that march are genuinely in range** (≈9 per pixel, gated per pixel already). So
   per-tile/cluster culling (L2) **cannot** remove them. **L2 is demoted.** Duplicates can be removed:
   13 of 29 uploaded lights are copies.
3. **The town contradicts a visible-pixel model of light cost.** The light march costs 5.2 ms at the
   overview (§12) while only 2% of visible pixels march any light. The S-1 interior calibrates ≈1.1 ns
   per march on the 4090 (13.8 ms / ~12.4 M visible marches), so 5.2 ms ≈ 4.7 M marches, against ≈0.14 M
   on visible pixels. **Leading hypothesis (H-overdraw): most march work outdoors is spent on fragments
   that are later hidden**, chiefly lit building interiors drawn and then covered by roofs and walls:
   ~6 marches per hidden fragment, the interior rate.
   - Checked and ruled out: the "lights off" switch disabling other lighting. `setLightOccupancyBox`
     clears only bit 1, and the shaders read bit 1 only in `phxLightVisibility`.
   - Not yet ruled out: longer marches per light at the overview (steps per march is unmeasured, I4).
4. **Test for H-overdraw:** a depth prepass A/B (depth-only static pass, then shade with an equal depth
   test). If H-overdraw holds, the light cost outdoors collapses and the indoor cost drops by the overdraw
   share.

**Revised P2 order:** L1 (duplicate merge) → **depth prepass A/B (new, P-DP)** → re-measure → then choose
between cached light visibility (L3c: probes or a voxel light cache, the direction the GTA video points to)
and L5 (tighter radii).

### 13. L1 duplicate-emitter merge: SHIPPED (2026-09-25, RTX 4090, Release)

**What changed.**
- `ChunkRenderManager` merges emissive lights per **(cube cell, radius)**: intensity = Σt,
  color = Σ(c·t)/Σt, radii kept separate, output sorted by key (order-independent). The default is ON.
  `POST /api/debug/emitter_merge {enabled}` toggles it and re-meshes, so it can be A/B'd in-process.
- `LightManager` breaks relevance ties at the upload cap by **world position**, then id. It used to be
  id alone, which let chunk iteration order decide which lights were dropped.
- Every light-receiving shader (voxel, grass, foliage, character, transparent) multiplies
  color × intensity × attenuation **linearly**, with no clamp. So a merged light shades exactly like the
  sum of its members, on every surface type (checked in each shader).

**Validation.**
- **Unit tests** (`tests/graphics/EmitterMergeTest.cpp`, 5 tests + `LightManagerTest.L1_CapTieBreakIs…`):
  shown red first with 8 lights where 1 belongs (`l1_red_tests.txt`), then green (`l1_green_tests.txt`).
  The first red run reported 0 lights everywhere: the test process had not loaded `materials.json`, so
  `glow` was not emissive. The fixture now asserts the registry loads, rather than letting that pass
  silently.
- **L4 census.** Tavern **29 → 16** lights (0 duplicates); town **119 → 36** (all unique). The toggle
  round-trips.
- **Pixel gate, tavern interior** (under the cap: same light set; `p1/l1_pixel_gate_s1_interior_v2.json`).
  **PASS** on the linear and shipping curves: merged-vs-unmerged differs less than a same-setting control
  at matched timing.
  - The first run FAILED: its control (A1 vs A2, 1.5 s apart) was not timing-matched to the test
    (a re-mesh + settle apart). Every differing pixel sat in the walking NPC's box, in both control and
    test. The control was fixed (v2: C taken after the same re-mesh and wait as B); the threshold was not
    loosened. Both runs are kept.
- **Town street** (at the cap: a declared change is allowed; `p1/l1_s2_street_median.json` + diff
  images). Per-pixel medians of 7 captures per condition. Differences beyond the control split
  brighter/darker (4,761 / 6,281 px) and sit where the control's do: grass wind, the NPC, canopy. **No
  lighting change detectable above motion noise, and no darkening of static surfaces.**
  - A paused variant is **INVALID** and discarded (`l1_pixel_s2_street_paused.json`). The emissive
    reconcile runs in the simulation update, so while paused the toggle never took effect (it reported
    36/36), and wind keeps animating anyway.

**Measured win** (`tools/perf_harness.py`, 8 counterbalanced pairs, 95% CI; `p1/s1_l1_merge.jsonl`,
`p1/s2_l1_merge.jsonl`):

| Scene / pose | GPU frame, merge off → on | Saving |
|---|---|---|
| S-1 tavern interior | 23.9 → **17.1 ms** | **−6.7 ms [6.5, 7.1] (−28%)** |
| S-1 tavern exterior | 21.1 → **14.5 ms** | **−6.6 ms [6.6, 6.7] (−31%)** (incl. grass −2.5) |
| S-2 town street | 20.8 → **18.1 ms** | **−2.6 ms [2.5, 3.0] (−13%)** |
| S-2 town overview | 12.2 → **9.7 ms** | **−2.5 ms [2.4, 2.8] (−21%)** |

The tavern prediction (~6 ms of 13.8) held. **The town prediction ("cost holds at the cap") was wrong:**
it got faster. A plausible reason: the duplicates clustered where emitters are densest, so up to 42
copies of one spot ranked at the top by distance and marched the same nearby pixels; merged, those slots
go to distinct, on average farther lights with fewer pixels in range.

### 14. P-DP static depth prepass: BUILT, verified, default OFF (2026-09-25, RTX 4090, Release)

**What it is.** With the prepass on, static geometry is drawn depth-only first (`voxel_depth.frag`,
colour writes off), then shaded by `voxel.frag` with **depth writes off and an or-equal test**, so
every pixel is shaded once, by its front-most fragment.

**Why it works here.** `voxel.frag` contains three `discard`s (transparent flag, alpha cutout, mirror
flag). With `discard` present and depth writes on, the GPU cannot reject hidden fragments before
running the shader. With depth writes off in the shading pass, it can.

**The discards are shared, not copied.** Which fragments exist (flag skips, `PHX_CUTOUT_ALPHA`, the
varied-tiling sample coordinates, the albedo sample) now lives in `voxel_surface.glsl`, included by
both shaders.

**The pipelines are built from one definition.** All three static pipelines (normal, prepass,
shade-after-prepass) come from one builder (`RenderPipeline::buildStaticPipeline`), so their state
cannot drift. Both passes use the SAME culled chunk list and instance ranges.

`POST /api/debug/depth_prepass {enabled}` echoes `enabled`, `available` and `ran_last_frame`; it
refuses a non-boolean, and refuses ON when the pipelines are unavailable.

**Red test (overdraw = Static FS invocations ÷ covered pixels; prediction written in the design check:
tavern interior ≤ ×1.3):**

| Scene / pose | Prepass off | **Prepass on** |
|---|---|---|
| Tavern interior | ×2.53 | **×1.05** |
| Tavern exterior | ×2.97 | ×1.71 |
| Town street | ×2.39 | ×1.12 |
| Town overview | ×1.81 | ×1.32 |

What remains outdoors is most likely 2×2 quad overshading of tiny faces, which a prepass cannot remove.
That is a future lead.

**Correctness (pixel gates, `p1/dp_*`).**
- **Lit frames at matched timing were inconclusive** at single-pair noise. They missed by a few pixels,
  with mean differences in the prepass's favour: the walking NPC, grass wind and hearth flicker. Pausing
  did not help, because the flicker is frame-wide.
- **Decisive:** debug mode 12 (the albedo of the surviving front-most fragment, after all three
  discards), game paused. At the tavern poses the control and the test are both ≈0 (p99.9 = 0).
- **R-DP2 discard rig** (hand-placed Stone wall + Glass pane + Mirror in one chunk, 6 orbit angles).
  - First run: 2 of 6 angles failed on ONE curve each. Diff images put every differing pixel, test and
    control alike, in the swaying grass. My first classifier for that was wrong: the blades read ~39/255,
    not < 20.
  - Re-run with grass and foliage off: **control and test are both 0 px over 8/255 at every angle, on
    both curves.** All three discard paths and the winding are exact.

**Measured win** (8 counterbalanced pairs, 95% CI, **on top of L1**; `p1/s1_dp.jsonl`, `p1/s2_dp.jsonl`):

| Scene / pose | GPU frame, prepass off → on | Saving | Prepass cost |
|---|---|---|---|
| Tavern interior | 18.8 → **11.7 ms** | **−7.0 ms [5.1, 8.9] (−37%)** | 0.027 ms |
| Tavern exterior | 14.4 → **10.8 ms** | **−3.5 ms [3.54, 3.56] (−25%)** | 0.030 ms |
| Town street | 17.6 → **12.9 ms** | **−4.6 ms [4.59, 4.74] (−26%)** | 0.042 ms |
| Town overview | 9.3 → **7.5 ms** | **−1.8 ms [1.80, 2.05] (−20%)** | 0.011 ms |

**H-overdraw: CONFIRMED as a large lever, but the specific prediction was too strong.** It predicted
the overview's light cost would collapse. Static Geometry there fell 63% (2.5 → 0.9 ms), but most of the
overview's remaining 7.5 ms is outside the static pass (foliage, grass, shadows, GI probes). The next
attribution should start there.

**L1 + P-DP together** (tavern interior, the scene both were measured in): ≈23.9 ms → ≈11.7 ms GPU
frame. The two A/Bs ran in different processes, so this is an approximate compound, not one A/B.

**Default:** still OFF. Turning it ON is its own commit (the design check's rule). Every gate this
feature was given has passed.

**Moving lights + user visual sign-off (2026-09-25).** The player held a torch (item-effect light) and was
moved to 3 spots in the night tavern under a fixed camera with the prepass ON. `/api/lights` read the
torch light back at each spot: (2.24, 17.92, 3.63) → (6.26, 17.92, 3.65) → (10.26, 17.92, 3.67),
`ran_last_frame` true each time. The middle spot was repeated with the prepass OFF for a side-by-side
(`p1/dp_torch_*.png`, `p1/dp_torch_demo.json`, script `p1/dp_torch_demo.py`). The user looked at the
captures and approved the lighting ("looks good"). Nothing about the prepass persists across frames,
and L1 only merges emissive-voxel lights, so moving lights are unaffected by construction.

**Gap found (not caused by P-DP): NPC-held items do not emit light.** `POST /api/entity/<npc>/equip
{"itemId":"torch"}` succeeds, but no item-effect light is created, because only
`Application::updateHeldItem` registers held-item effects, and only for `held_player`. Candidate fix:
register item effects for any equipped light-emitting item and move the light with that character's
hand bone each frame. Test: an NPC walks a path carrying a torch, and the light-to-hand distance is
checked every frame.

**Thrown torches (2026-09-25).** The light sits on the torch's flame anchor (items.json `effects[].anchor`,
transformed by the item's own transform), so it follows the torch, not the hand, and a thrown torch's
physics body carries it. Two fixes, both red-before-green:
- `ItemEffectSystem`: a new instance's first condition check now runs on its first update. It used to wait
  for the staggered 0.25 s check, so a thrown torch was dark for up to ~0.2 s (measured live: 0.23 s).
  `ItemEffectSystemTest` (red: 1/2/3 lights instead of 2/3/4 in the three later stagger phases).
- `Inventory`: items are finite by default. Creative mode (infinite supply: a throw hands out a copy) is
  now opt-in, per the user's direction that the engine should not default to Minecraft-creative behaviour.
  `InventoryTest.DefaultConstruction` / `DefaultThrowRemovesTheItem` / `JsonWithoutCreativeKeyIsFinite`.
- L4 (`p1/torch_throw_probe2.py` / `.json`, Release, default inventory): `creative` false; after the throw
  slot 0 is empty, the held light is gone, and the thrown light is present in every sample from the first
  (t = 19 ms), moving along the arc (z −6.69 → −5.42) before settling. No sample had zero torch lights.

**Caveats that still stand:** one GPU (the RTX 1000 Ada laptop, where G-18 measured 90%, is owed); editor
host at 1600×900 (the standalone at native resolution is owed); S-2 is 4 buildings, not a city (S-3, a
CityForge city, is owed); NPCs move in both scenes (the paired design protects deltas, not absolute
levels); the §10 drift is still unexplained.

### 15. The GI probe pass: GI-1 (skip buried probes) and GI-2 (the probe trace) (2026-09-25, RTX 4090, Release)

**Where the frame goes after L1 + P-DP** (S-1, prepass on; `p1/s1_breakdown_l1_dp.jsonl`, table by
`p1/breakdown_table.py`):

| Scope (ms) | Interior | Exterior |
|---|---|---|
| **GPU Frame** | 12.29 | 11.56 |
| Shadow Pass | 0.68 | 0.67 |
| **GI Probes** | **3.96** | **3.97** |
| Scene Pass | 7.27 | 6.55 |
|   Static Geometry | 5.31 | 1.34 |
|   Grass | 0.71 | 2.15 |
|   Foliage | 0.11 | 2.06 |
|   Characters | 0.94 | 0.75 |

The probe pass was a third of the frame at both poses, flat regardless of what the camera sees: it is a
fixed per-probe cost (6,912 probes a frame, 18 rays each, 16 u reach).

**GI-1, skip buried probes: prediction FAILED; the saving is noise-level.** `gi_probe.comp` traced all 18
rays and only then asked whether the probe is buried. The lighting doc (§7) had called skipping that
"at most about half" of the pass. Predicted −30..−50%. Measured −0.09 / −0.06 ms (−2%, 8 pairs,
`p1/s1_gi1.jsonl`), and ±0.03 ms in a later one-process check (`p1/gi_combo_check.json`). Buried
probes were already cheap: their rays hit solid on the first cell. Kept because it is exact by
construction (every reader gates on validity, lobe 0 `.a`, before reading colour) and costs nothing.

**GI-2, the probe trace.** The probe's primary ray (`phxDdaTrace`) visited every MICRO cell along 16 u
(up to ~250 steps, each a full occupancy query), while `phxSegmentBlocked` already walked CUBE cells and
dropped to micro only inside mixed cubes. The shipped trace, `phxDdaTraceProbe` (`occupancy.glsl`, CPU
mirror `packedPoolTraceProbe`), runs the micro march for the first unit and a cube walk
(`phxDdaTraceTwoLevelFrom`) beyond it, and reports the hit the micro march reports: the micro cell (the
bounce reads the field at its centre) and the entry axis (the bounce's face normal). How it got there:
1. **Cube walk, red → green on random rays.** The naive port (each mixed-cube slice seeded with axis 1,
   as `phxDdaTrace` is) got the right cells but the WRONG NORMAL on 354 of 20,000 rays. Seeding the
   cube-entry axis fixed that; locating a solid cube's entry cell AT the face crossing instead of 1e-4 u
   past it took cell differences from 35 to 1 of 77,643 hits (a float tie) on 200,000 random rays.
2. **But probe rays are not random rays.** Every probe ray starts ON the 2 u lattice, exactly on a cube
   corner, where the micro march resolves zero-length steps in a fixed tie order and the cube walk's
   1e-4 slice offset resolves them differently. The Lighting Lab's doorway wall (A4) read higher with
   the cube walk in every early pair, which is what sent me looking. A probe-shaped test (lattice starts,
   the shader's rotated 18-direction set) found the cube walk differing on **49 of 41,850** rays.
3. **An exact-by-construction alternative was measured and rejected.** The micro march with the
   per-cell query answered from a per-cube cache (same cells, same order) matched on all 41,850, but
   ran no faster than the plain micro march (3.95 / 3.39 ms vs 3.83 / 3.04, `p1/gi_trace_combo.json`):
   the cost is the STEPS, not the lookups. Removed.
4. **Shipped: micro march for 1 u, then the cube walk**, seeded with the axis that entered the micro
   march's untested end cell. Probe-shaped rays: 1 of 41,850 differs (a tie, hit/miss identical);
   random rays: 1 of 77,643 hits. Tests: `OccupancyTraversalTest.ProbeShapedRaysProbeTraceMatches
   PlainCubeWalkDoesNot` and `...TwoLevelTraceReportsTheMicroMarchHitOnRandomRays`.

Only `gi_probe.comp.spv` changed among the 83 SPIR-V binaries: every other shader that includes
`occupancy.glsl` compiled byte-identical.

**Measured, in-process A/B** of the shipped trace vs the micro march (8 counterbalanced pairs,
`p1/s1_gi2_probe.jsonl`; the plain cube walk measured the same, `p1/s1_gi2.jsonl`):

| S-1 pose | GI Probes | GPU Frame |
|---|---|---|
| Interior | −0.75 ms [−0.77, −0.73] | −0.77 ms [−0.86, −0.52] |
| Exterior | −0.79 ms [−0.81, −0.76] | −0.82 ms [−0.97, −0.74] |

Prediction was GI Probes ≤ 1.5 ms: **missed** (2.1 ms). The march was not most of the pass; what is
left is the bounce (a field read plus a sun segment test per hit) and the lobe bookkeeping.

**Against HEAD** (the unmodified shader), fresh engine processes, each launched, tavern-built and settled
the same way (`p1/gi_process_arm.py`, `p1/gi_process_ab.jsonl`; NEW = the cube-walk build, NEW_FINAL =
the shipped build):

| Build (processes) | GI Probes int / ext | GPU Frame int / ext |
|---|---|---|
| HEAD (3) | 3.96 / 3.91 | 12.07 / 11.06 |
| NEW (2) | 2.18 / 2.22 | 10.21 / 9.33 |
| **NEW_FINAL (2)** | **2.12 / 2.18** | **9.99 / 9.40** |

**Net: probe pass −1.8 ms (−45%), GPU frame −2.1 ms interior (−17%), −1.7 ms exterior (−15%).** Only
~0.77 ms of that is the trace (the in-process A/B). The other ~1 ms comes with the rewritten shader
even with both options OFF (2.84–2.86 ms in-process vs HEAD's 3.9–4.0). **Unexplained, and fragile:** in
the intermediate build with a third trace branch that gain vanished (micro path 3.83 ms), which points
at how the driver compiles the 18-ray loop (register pressure / unrolling). Re-measure on the laptop
before relying on it.

**Correctness.**
- Lit-frame pixel gates, timing-matched control (`p1/gi_pixel_gate.py`), S-1 interior: PASS on both
  tone curves for skip-buried, the cube walk and the shipped trace. Exterior with grass and foliage on is
  inconclusive (the control alone has 47k–84k px over 8/255 of wind). With grass and foliage off: the
  cube walk PASS; skip-buried and the shipped trace each "fail" one curve by ~100 px, and in every such
  run every differing pixel of test AND control lies in the same ~60×100 px box, the moving character
  (`p1/gi_diff_where.py`); zero pixels differ outside it.
- Lighting Lab `ambient_model_check.py`, run in the existing **LightingLab** project (flat, vegetation-free,
  built for lighting gates, `docs/UnifiedLightingPlan.md` D2) since StructGenTest does not exist on this
  machine (an earlier draft of this section wrongly called LightingLab a new scratch project; its
  original `game.json` is intact). `--build` verified the rig from the world: GREEN in every run, options
  on and off. A1-A3 identical to 3 decimals.
- **A4 (the doorway-lit wall) cannot resolve differences of this size.** Order-balanced 8-run series:
  cube walk 2.59 ± 0.19 vs micro 2.20 ± 0.31 (`p1/gi_a4_abba.json`) looked like +18%, but the next series
  of the exact per-cube-cache march vs the micro march (identical by construction) read 2.11 ± 0.25 vs
  2.40 ± 0.58, the micro march alone spanning 1.54–2.81 (`p1/gi_a4_abba_skip_empty.json`). The
  deterministic probe-shaped ray test is the evidence that matters; A4 is a pass/fail floor (≥ 1.5).
  Final shipped trace, fresh LightingLab process, 20 s settle (`docs/evidence/ambient_gi2_final_{on,off}.json`): GREEN both; A1 0.968 / 0.969, A2 0.0719 / 0.0716, A3 0.9975 / 0.9974, A4 1.54 / 1.67. Note A4 sat just above its 1.5 floor in BOTH configurations here, against ~2.2 in a longer-running process: the doorway light arrives by probe-to-probe hops and is still converging shortly after the rig is built. That is a fragility of the check itself (it can go red on timing alone), logged for whoever next touches `ambient_model_check.py`: settle for convergence, or average several captures.

---

## 16. P1c: the city benchmark (S-3) at real load (planned 2026-09-25; ladder + growth table DONE 2026-09-26; routes OPEN — see §17)

**Why.** Every result above was measured in S-1 (one tavern) or S-2 (4 buildings). The user reports that
larger towns "really stress out the fps", and the goal is **cities of ~100 buildings**. The S-3 row in §4.1
has been owed since the plan was written. Until it exists, no percentage in §10-§15 says anything about
the load that matters, and the ranking of the next optimizations is a guess.

**Target (user decision, 2026-09-25):** no fixed frame budget. **As fast as possible without sacrificing
visual quality.** So the benchmark's job is not to pass a number: it is to show *what grows with the city*
and *what breaks first*, so every optimization is ranked on the real operating point. Visual quality
stays governed by §6 (identical image, or a declared and approved change).

### 16.1 The scene: engine-generated, never hand-assembled

- **Generator:** `POST /api/settlement/build` with `tier:"city"` (CityForge), Perlin terrain world with
  `world.streaming: true` (relief, hydrology, flora and fauna present, as in a real game), seed 7.
  **Provenance:** the raw generator request + response is saved beside every number
  (`p1c/city_<N>_build.json`); nothing is hand-placed. If the generator cannot reach a size, that is
  **reported and logged in `docs/StructurePipelineGaps.md`**, never patched around.
- **Size ladder at FIXED density 1.5** (growth by area, the way a city actually grows, not by packing).
  CityForge L4 measured 72 buildings on 160×160 at density 1.5 (`docs/CityForgePlan.md` M4). Predicted
  counts below are extrapolations to be replaced by what the generator reports:

  | Rung | Site | Predicted buildings | Purpose |
  |---|---|---|---|
  | C-25 | ~96×96 | ~25 | Overlaps S-2's scale: ties the new scene to the old numbers |
  | C-50 | ~128×128 | ~45 | Mid point for the growth curve |
  | C-75 | 160×160 | ~72 (measured) | CityForge's verified operating point |
  | **C-100** | ~192×192 | **~100** | **The target** |

  Each rung is its own world DB (same seed, same terrain). Building count, residents, placed props,
  emitters (`light_stats`) and tier census (`voxel_tiers`) are recorded per rung.
- **Scene identity across processes — verified, never assumed** (design check 9.6 #3). This session showed
  that the S-1 tavern did NOT survive a restart of M4TavernBench (it was rebuilt in every process). So each
  rung has a **content fingerprint** = {building count, placed-object count, `voxel_tiers` per-tier
  instance counts, `light_stats` registered/uploaded by source, resident count}, written at build time
  (`p1c/city_<N>_fingerprint.json`). **Taken at a fixed anchor** (design check 9.7 #5): `voxel_tiers` and
  `light_stats` count only RESIDENT chunks, so the fingerprint is taken with the streaming focus held at
  the site centre (I14, holder `camera_path` with a zero-length path, or the WorldForge focus route), load
  distance ≥ the site's half-diagonal + one chunk, after the §10 settle gate, camera at the overview pose.
  Anything else fingerprints the streaming state, not the city. Order of preference:
  1. **Persisted:** `save_world` after the build, relaunch, and the fingerprint read back must be equal.
     Residents are entities and may not persist; if they do not, they are re-spawned by the generator's
     own resident path (never hand-spawned) and their count must match.
  2. **If persistence fails** (logged as a gap in `docs/StructurePipelineGaps.md`): the build is re-run
     per process from the saved request, and the fingerprint must match the reference exactly before a
     single sample is taken. A mismatch voids the run.
- **Residents ON** (the generator populates them; ~1 per household, 69 at C-75). They are real load
  (character pipeline, AI, pathing). A **residents-OFF** arm is measured once per rung to attribute
  their share, not as the operating point.

### 16.2 What is measured, and why static poses are not enough

Static 240-frame medians (everything so far) hide the stress the user is seeing: **hitches while moving**
(chunk streaming, re-meshing, building/prop activation, NPC schedule updates). S-3 therefore has two
kinds of measurement:

1. **Fixed poses** (as §4.2, so the per-pass attribution tools apply unchanged): street level in the core,
   the market square, a rooftop-height view along the main street, an elevated overview of the whole
   city, and the city seen from outside its wall. Poses are defined relative to the generator's reported
   layout (square centre, main-street axis), so they land in the same place on every rung.
2. **A fixed walk route** through the city at walking speed and at a fast-travel speed: in through a gate,
   down the main street, across the square, through a side lane, out the far side. Per frame, recorded:
   wall-clock frame time, GPU frame, per-pass GPU scopes, CPU scopes. Reported as p50 / p95 / p99 / max
   frame time plus **hitch count** (frames > 2× the route median, and > 33 ms) **with what was running in
   the hitch frame** (which CPU scope spiked). A hitch is a finding, never noise to be averaged away.
   **The streaming pump alternates by design** (`updateChunkStreaming()` every 2nd frame,
   `pumpChunkLanding()` on the others, `Application.cpp:4249`), so frame time can alternate too. The
   report shows the even/odd-frame split of `Streaming Pump` separately, and a hitch is judged against the
   median of frames with the SAME parity, so the cadence is never counted as hitches (design check 9.8).

**Controlled conditions (every S-3 run; design check 9.6 #6):**
- **Time of day fixed, clock paused** (`POST /api/daynight/set {timeOfDay, paused:true}`): **noon** and
  **night (22:00)**, measured separately. Night is when windows, lanterns and street lights are lit, so it
  is the light-heavy case and the likely worst one; noon is the shadow/sky-heavy case. (Only the clock
  is paused — never the game: game pause stops the emissive reconcile, §13.)
- **Present mode non-vsync** (MAILBOX or IMMEDIATE, read back from `engine_timing.present_mode` and
  recorded per row). A vsync floor at the refresh rate would hide exactly the headroom we are looking for;
  a run in FIFO is void.
- **Viewport size recorded** per row (editor docked viewport at a 1600×900 window until the standalone
  `--test` follow-up).

Plus, per rung: chunk RAM and meshing totals (I5/I7), draw count, uploaded vs in-range lights, shadow
cascade caster counts. Output: a **growth table** (each pass's cost vs building count) that shows which
costs are flat, which linear and which super-linear. The first super-linear pass is the next target.

### 16.3 New instrumentation this needs (small, measurement-only)

| # | What | Why it is missing today | Contract |
|---|---|---|---|
| **I11** | **Frame-pacing history:** a per-frame ring (same 240-frame shape as I1) of wall-clock frame time (present to present) and the main-loop phases, exposed at `GET /api/debug/frame_pacing?frames=N`. **Built on the EXISTING `PerformanceProfiler` scopes, not a third timing system** (design check 9.7 #3): `Update` and its children `Water`, `Scripting`, `AI`, `NPCs`, `Entities`, `Camera Sync`, `Input Controller`, `Voxel Interaction` (`Application.cpp:3529-4084`), plus two added around existing calls — `API Drain` (`processAPICommands()`, `Application.cpp:2912`) and `Streaming Pump` (`updateChunkStreaming()` on alternate frames, `pumpChunkLanding()` on the others, and `pumpDeferredDbLoads()`, `Application.cpp:4249-4255`) — plus `drawFrame` (the I7 recorder's total) and `Other` = frame − Σ phases, so the phases always sum to the frame. | `cpu_timing` only covers `drawFrame`. A hitch caused by AI, physics or the streaming pump is invisible to every tool we have. | `frames`: integer 1..capacity (240), default = capacity; out of range → clamped and the used value echoed as `frames_used`. Response: the same statistics block as `cpu_timing` (median/p90/p99/max per phase, ms) **plus the raw per-frame series** of exactly `frames_used` entries, each `{serial, frame_ms, phases{...}}`. Read on the HTTP thread from a snapshot only (the `engine_timing` rule). The ring is for STATIC poses; routes use the recorder (I15). |
| **I12** | **Camera path playback over the API**, driving the existing `CameraManager::CameraPath` (Catmull-Rom; in the engine, not exposed). `POST /api/camera/path {waypoints:[{x,y,z,yaw,pitch}] (world units, degrees), speed_u_per_s, loop:false, stream_follow:true}` starts it; `POST /api/camera/path {stop:true}` stops it; `GET /api/camera/path` → `{playing, finished, stopped, progress 0..1, arc_length_u, position, yaw, pitch}`. | Driving the camera from the harness over HTTP gives jerky, latency-bound motion that is not what a player does, and it is not repeatable. | **Constant speed is NEW behaviour, added as a mode.** Today `CameraPath::update` spends a fixed 1 s per segment regardless of its length (`CameraManager.cpp:129-130`), pinned by `CameraPathTest.PlayAndProgress`/`FinishesAtEnd` and relied on by cinematics. So: an opt-in `setConstantSpeed(u_per_s)` (arc-length parametrisation over the Catmull-Rom spline, precomputed lookup); the default stays 1 s/segment and those pins stay green unchanged. **Refusals** (nothing applied, reason echoed): < 2 waypoints; any non-finite value; speed ≤ 0; speed > `kMaxPathSpeed`. **Speed is split in two** (design check 9.7 #6), because streaming throughput is machine-dependent (the laptop streams slower) and a compiled constant cannot be "measured": (a) **two engine-side bounds, each with its reason written at the clamp site:** `kMaxPathSpeed` = 64 u/s, a player-motion bound (~15× walking speed, a fast mount; anything faster is a flythrough, not something a player does, and turns the benchmark into a streaming-failure test); and **the streaming focus never moves more than `kFocusStep` = 64 u in one frame**, the SAME constant and invariant WorldForge uses (`Application.cpp:13749`: "a fast player, not a teleport"; its residency poll re-issues the focus every frame, `WorldForgeBuildService.cpp:207`), shared rather than duplicated, so a long frame cannot teleport the anchor and drop characters onto unstreamed ground; (b) **the route speed actually used is chosen per machine by the harness** from that machine's §16.6 step-0 throughput measurement (the fastest speed at which the chunk under the camera is resident on arrival, with margin) and recorded in every row. **Conflict rule:** `POST /api/camera` while a path is playing **stops the path** and its response says `path_stopped:true` (no silent fight over the pose). **`stream_follow` (default true for this route):** each frame the path's position is written to `ChunkManager::setStreamingFocusOverride` (`ChunkManager.h:181`), and cleared when the path ends or stops — see I14. |
| **I13** | **Route runner** in `tools/perf_harness.py`: `route` subcommand = settle, verify fingerprint (§16.1), `POST` I15 start, start the I12 path, **wait without polling** until the path's known duration has elapsed (+1 s), then one `GET` of `/api/camera/path` (must report `finished`) and one read of the I15 recording; writes one jsonl row per frame plus a summary. A/B arms repeat the route ABBA. | Composes the above. | Refuses Debug builds (as all harness commands do), refuses FIFO present mode, refuses an unpaused day/night clock. **Sends no request while a measured route is running** (the main loop drains API commands every frame, `Application.cpp:2912`, so polling would add main-thread work to the very frames being measured). |
| **I14** | **Streaming follows the route** (design check 9.6 #1). `ChunkManager::streamingAnchor()` is the PLAYER unless an override is set (`ChunkManager.h:184`, comment at `ChunkManager.cpp:264`). A camera flying the route while the player stands at spawn streams nothing new, so the route would measure a static city and miss streaming, one of the prime suspects for the stutter. I12's `stream_follow` drives the focus override from the path's position each frame. | The alternative, walking the actual player character, is more realistic but NPC collisions and physics make it non-deterministic. It is kept as a later confirmation run, not the benchmark. | **The override gets an owner** (design check 9.7 #1). It already has two users: the WorldForge build job (moves it in 64 u steps, widens load/unload distance, and its `releaseFocus` calls `clearStreamingFocusOverride()`, `Application.cpp:13745-13757`) and `/api/worldforge/focus` (`:13854`). So `setStreamingFocusOverride(pos, holder)` records a holder (`worldforge_build`, `worldforge_focus`, `camera_path`); a set by a different holder while one is held is **refused, naming the current holder**; `clearStreamingFocusOverride(holder)` releases only its own hold (a WorldForge release can no longer clear a route's focus mid-run, and vice versa). Existing callers pass their holder name; behaviour for a single user is unchanged. Pinned by `StreamingFocusOwnerTest` (refuse-while-held, release-only-own, clear-on-owner-release). Main-thread only (`ChunkManager.h:115`). The path clears its hold on end, stop and any error. `GET /api/camera/path` echoes `stream_follow` and the current holder. |
| **I15** | **Route recorder** (design check 9.7 #2). The I1 and I11 rings hold 240 frames; a ~60 s route at ~100 fps is ~6,000 frames, so they wrap ~25 times, and reading them mid-route perturbs the frames being measured. `POST /api/debug/record {start:true, max_frames}` begins capture into a buffer sized once at start (no allocation per frame); `{stop:true}` ends it; `GET /api/debug/record` returns it. One row per frame: `{serial, frame_ms, phases{...I11}, gpu_scopes{...I1, matched by serial}, camera{pos, yaw, pitch}, path_progress, streaming{resident_chunks, pending_generation, pending_remesh}}`. | Nothing records a whole route today. | `max_frames` 1..65,536 (clamped, echoed; at ~100 fps that is ~11 min); on overflow recording **stops and reports `truncated:true`** rather than wrapping (a silently wrapped route would drop its first frames). GPU scopes arrive a few frames late: a row's GPU block is attached when its serial resolves, and rows still unresolved at stop are marked `gpu_pending` rather than guessed. Start while already recording is refused. |

These are measurement-only: no gameplay, look or generator behaviour changes. **All new routes go through
`PerfApi`** and are registered by BOTH hosts (the editor and `GameApiService`), like I1-I7, so the
standalone `--test` follow-up has the same tools (design check 9.8).

### 16.4 Validation of the benchmark itself (so its numbers can be trusted)

**Instrumentation is proven on SMALL rigs first, never first in the city** (design check 9.6 #5). Two rigs,
because a fixed-range world does not stream (`configureStreamingGeneration` sets
`setMaxChunksPerUpdate(enabled ? 8 : 0)`, `ChunkManager.cpp:240`; design check 9.7 #4):
- **R-P1 (static):** a fresh Flat world with a fixed `from`/`to` range, one chunk (0,0,0), a 5×5 Stone slab
  at local 8..12 on the floor at y=16 (hand-placed, queried back), camera route = 4 waypoints inside that
  chunk, 1.2 u/s. Used by V1-V3, V5-V7.
- **R-P2 (streaming):** Flat terrain with `world.streaming: true` (nothing placed; flat so terrain relief is
  not a second variable), a straight path along +x across 3 chunks. Used by V4 only.

Each test moves ONE variable, with a written prediction and a control.

| # | Red test (shown failing first) | Level | Prediction / pass criterion | Control |
|---|---|---|---|---|
| V1 | `CameraPathTest.ConstantSpeedCoversEqualArcLengthInEqualTime` (unit): a path with one 1 u and one 10 u segment. Today's timing gives 1 u/s then 10 u/s. | L2 | With `setConstantSpeed(2)`: distance travelled per 0.1 s within ±2% of 0.2 u along the whole path. **Fails today** (the short segment runs at 1/10 the speed). | The existing `CameraPathTest` pins unchanged and green (default timing untouched). |
| V2 | `FramePacingHistoryTest` (unit, `GpuTimingHistoryTest` shape): known per-frame values in → exact percentiles and raw series out; wrap-around; `frames` clamp. | L2 | Exact values. Fails until the ring exists. | — |
| V3 | **Route determinism**, R-P1 (L4): run the I12 path twice under the I15 recorder, **no polling during either run**; compare the RECORDED per-frame `camera` pose against `path_progress`. | L4 | For matched progress, position differs by ≤ **0.05 u** (the harness's pose tolerance) and angles by ≤ 0.1°, over the whole path, in both runs. A path overwritten by `InputManager`, or drifting, fails. | The path's analytic position at that progress (from the waypoints). |
| V4 | **Streaming follows**, R-P2 (L4): the recorder's `streaming.resident_chunks` plus a `load_state` read after each run tell whether the chunk under the camera was resident when the camera entered it. | L4 | `stream_follow:true`: resident at every chunk entry. `stream_follow:false`: the second and third chunks are NOT resident on entry (the player stands at spawn). | `stream_follow:false`. |
| V6 | **`StreamingFocusOwnerTest`** (unit): hold as `worldforge_build`, then set as `camera_path` → refused, naming `worldforge_build`; `clear(camera_path)` → no effect; `clear(worldforge_build)` → released; then `camera_path` can take it. | L2 | As stated. **Fails today** (no holder: the second set overwrites, any clear releases). | A single holder set/clear behaves exactly as today. |
| V7 | **Recorder capacity and ordering** (unit + R-P1): `max_frames` = 100 over a 300-frame run → exactly 100 rows, `truncated:true`, first row = first frame after start; every row's GPU block matches the GPU ring's entry for the same serial, or is `gpu_pending`. | L2 + L4 | As stated. | A run shorter than `max_frames`: `truncated:false`, no gaps in `serial`. |
| V5 | **A hitch we caused is seen, with its cause** (R-P1): mid-route, `POST /api/world/fill` a 16×16 slab (forces a re-mesh). | L4 | I11 shows ≥ 1 frame > 2× the route median within 0.5 s of the fill, whose largest phase is `Dirty Chunk Flush` / meshing. | The same route with no fill: no such frame. |

**Then in the city:**
- **A/A noise on the route:** 8 ABBA repeats of the route with no change, per rung and per time of day.
  The route-level noise floor (p50, p95, p99, hitch count) is published before any A/B on it is read. No
  verdict below 8 pairs (§10 rule).
- **Settle gate before every run:** `load_state` pending counters at 0, `visibleInstances` stable at the
  route's start pose (§10), and the §16.1 fingerprint equal to the rung's reference.

### 16.5 Deviation from the small-rig rule, stated

CLAUDE.md asks for small one-chunk rigs. S-3 is deliberately the opposite: its purpose is the real
operating point. It is **not** used to prove any single change is correct; that stays with the
one-chunk rigs and pixel gates of §6. S-3 is used to (a) find and rank costs at real load and (b) confirm
a change's win survives at that load. Rig-vs-shipped deltas for S-3: editor host at 1600×900 with ImGui
(standalone `--test` at native resolution is the follow-up), RTX 4090 only until the laptop run.

### 16.6 Sequencing and exit criterion

0. **Measure Release streaming throughput** at the C-100 site (chunks made resident per second while the
   focus override moves at increasing speed) to derive `kMaxPathSpeed` (I12). The walking route runs at
   ~4 u/s; the fast route at the highest speed that stays under that bound.
1. I11 + I12 (+ constant-speed mode) + I14 (+ override holders) + I15 + I13, each with its red test (V1-V7,
   §16.4) on R-P1 / R-P2.
2. Generate C-25 … C-100; record counts and fingerprints; verify persistence (§16.1). Stop and report if
   C-100 cannot be generated.
3. Per rung, per time of day: census, fixed-pose attribution (§4.2 steps 1-5), route runs (A/A first).
4. Growth table + hitch report → check against the §16.7 predictions → re-rank §5 (L2, L3, L4, S1-S6,
   foliage, shadows, characters) on C-100.
5. Re-verify L1, P-DP and GI-2 on C-100 (their wins were measured at S-1/S-2 scale only).

**Exit:** the growth table and hitch report exist for all four rungs on the 4090, with provenance, and the
next optimization is chosen from C-100 data. The laptop run of the same ladder remains owed (§8.3).

**Progress (2026-09-26):** step 0 DONE (§16.10: walk 4 u/s, fast 32 u/s) · step 1 DONE (§16.8) · step 2
DONE (§16.11: 25/59/72/104 buildings, reload-verified; needed the site-scaled cap) · step 3 fixed poses
DONE (noon + night, all rungs) — **route runs OPEN**, residents-OFF arm OPEN · step 4 growth table DONE,
hitch report OPEN, P2 re-ranked (§17.2) · step 5 OPEN. Exit criterion NOT yet met (hitch report).

### 16.7 Predictions, written before the ladder runs (design check 9.6 #7)

Street-level core pose, GPU ms vs building count B (25 → 100), prepass + L1 + GI-2 on. Each is a
falsifiable claim; the growth table marks it CONFIRMED / REJECTED.

| Pass / cost | Predicted growth with B | Why |
|---|---|---|
| **GI Probes** | **Flat** (±5%) | Fixed grid around the viewer (§15); the city only changes how many rays hit. |
| **Scene Pass / Static Geometry** | Sub-linear at street level; ~linear in the overview | At street level near buildings occlude far ones (the prepass kills their shading); from above everything is in view. |
| **Point lights (inside Static Geometry)** | Rising toward the 32-light cap, then flat per pixel, **night ≫ noon** | Lights in range per pixel grow with density until the cap binds; L2 is re-evaluated here. |
| **Shadow Pass (mid cascade, 420 u)** | **~Linear** in B, the steepest GPU term at noon | Every building in the cascade casts, whether or not it is visible. |
| **Characters** | ~Linear in residents in view | One rig draw per visible character. |
| **Grass + Foliage** | Flat or falling | Buildings and paving replace grass and trees inside the city. |
| **CPU: NPC/AI + update** (I11) | ~Linear in residents (all residents, not just visible) | Schedules and pathing run for everyone. |
| **Hitches on the walk route** | Grow with B; cause = meshing (`Dirty Chunk Flush`) and streaming, **not** GPU | Denser chunks re-mesh slower (12.6 ms for a micro-heavy chunk, §11). |
| **Worst case overall** | C-100, night, overview pose (GPU); C-100 fast route (hitches) | Lights + shadows + everything in view; streaming at speed. |

### 16.8 RESULTS — instrumentation built and validated (2026-09-25, RTX 4090, Release, editor host)

Built: I11 (frame pacing on the existing `PerformanceProfiler` scopes, via `PerfCapture`), I12 (camera path
API + constant-speed mode), I14 (`StreamingFocus` with holders), I15 (`RouteRecorder`), plus the route
runner helpers (`docs/evidence/perf2026-09/p1c/rig_common.py`, `hitch_analysis.py`). One implementation,
`Core::PerfCapture` on `EngineRuntime`, driven by the editor loop and by `EngineRuntime::endFrame()`;
routes through `PerfApi` in both hosts.

| # | Result | Evidence |
|---|---|---|
| V1 | **Red → green.** Mutant (constant speed ignored) failed; green: equal arc per equal time, no backtracking, pose = `poseAt(progress)` at uneven dt; original `CameraPathTest` pins unchanged. | `CameraPathTest.*` (10) |
| V2 | **Red → green.** `series()` mutant (empty) failed; green: per-frame values in order, window clamp, `max`. | `GpuTimingHistoryTest` |
| V3 | **PASS.** Two recorded runs, ~2,570 on-path frames each: worst camera error vs the analytic polyline **6×10⁻⁵ u / 0.0002°** (tolerance 0.05 u / 0.1°). No InputManager fight, no drift. | `p1c/v3_route_determinism.json` |
| V4 | **Premise falsified → redesigned as V4b.** The control (stream_follow OFF) was resident at every chunk entry, like the follow arm: in the editor `playerPosition` IS the camera (`Application.cpp:4245`), so streaming already follows a camera path. Design check 9.6 #1 took `ChunkManager`'s "player" comment at face value. | `p1c/v4_stream_follow.json` |
| V4b | **PASS, 12/12 live steps:** WorldForge focus refuses a path's stream_follow naming the holder; a path without it is unaffected; each holder releases only its own hold; a path's hold refuses WorldForge; `POST /api/camera` stops a playing path, says `path_stopped`, and releases its hold. | `p1c/v4b_focus_ownership_live.json` |
| V5 | **PASS on the stated criterion; original prediction partly falsified.** The caused hitch was caught and attributed (below). But the control was NOT hitch-free (next row). | `p1c/v5_caused_hitch_rescored.json`, raw `v5_raw_{control,fill}.json` |
| V6 | **Red → green.** Holder-less mutant failed 2 tests; green 5/5. | `StreamingFocusOwnerTest` |
| V7 | **Red → green.** Wrapping-ring mutant lost the route's first frames; green: stop-don't-wrap, late GPU attach by serial (also after stop), clamping, dropped-key counting. | `RouteRecorderTest` (5) |

**Design changes the validation forced:**
- **Constant-speed mode walks the POLYLINE, not the spline.** Uniform Catmull-Rom through unevenly spaced
  waypoints (x = 0, 1, 11) dips to x = −0.125 on its first segment: a route camera that walks backwards
  and swings wide of corners (into buildings). The cinematic default keeps the spline.
- **A hitch is a LOCAL spike**, not "2× the route median": a route passes from cheap views to expensive
  ones, and the global rule flagged 210 frames of a sustained GPU-bound view change (3.7 → 8.5 ms, all
  `drawFrame/Fence Wait`) in the V5 control. Rule now: > 2× the same-parity median within ±0.5 s AND
  ≥ 4 ms above it (`hitch_analysis.py`).
- **The recorder also takes the render path's CPU scopes (I7)**, or a hitch inside `drawFrame` could only
  be attributed to "render"; and a per-frame `camera_chunk_resident` flag.
- **I14 `stream_follow` is redundant in the editor** (the camera already anchors streaming) but kept: the
  holder rules are what protect a WorldForge focus and a route from each other.

**Findings from the rigs (real engine behaviour, not tool artefacts):**
1. **`POST /api/world/fill` of 3,072 cubes froze the main loop for 1.84 s**, all in `Frame/API Drain`
   (1,787 ms), then a 29 ms re-mesh (`drawFrame/Dirty Chunk Flush`) and a 42 ms `update` frame. "Async" API
   commands still run on the main thread (only JobSystem jobs are off-thread). A dev-API cost today; the
   same path would hitch any gameplay edit of that size.
2. **At one R-P1 view the GPU frame alternates ~4.2 / ~9.5 ms every other frame** (`Fence Wait`
   dominant), phase-shifting occasionally; GI probe slices also vary 0.45 → 5.2 ms by which eighth of the
   grid updates. Unexplained; a candidate for the city attribution (frame pacing, not just averages).
3. **Standalone games do not stream terrain at all** (only the editor calls the streaming pump) —
   logged in `docs/StructurePipelineGaps.md` 2026-09-25. The standalone `--test` benchmark follow-up is
   blocked on it for streaming worlds.

Rig note: R-P1 = M4TavernBench; R-P2 = new project `PerfRigStream` (Flat, streaming, loadRadius 2, no
flora). Unit suite after the change: 4,059 passed, 20 skipped, 2 failed -- the two documented as failing
before this work (`AtlasManagerTest.BuildAtlasFromSourcePNGs`, `FineFaceMerge.SubcubeMerge_CrossCube...`).

### 16.9 BLOCKER FOUND AND FIXED: GPU device lost in streaming worlds (2026-09-25)

The S-3 world (project `CityBench`: Perlin seed 7, streaming, default residency) lost the GPU device
(`vkQueueSubmit` → `VK_ERROR_DEVICE_LOST`) ~30 s after EVERY launch (5/5), after a ~5 s silent submit;
the main loop then spun on one core. Pre-existing, found only because this was the first streaming world
with dense sub-voxel flora driven this long. Bisected live, one variable per launch:

| Launch | Device lost? |
|---|---|
| defaults (×2) | yes, ~30 s |
| GI-2 two-level trace off | yes |
| **pre-change `gi_probe.comp.spv`** (HEAD before GI-2) | **yes** → not a regression from §15 |
| probe field off (`/api/debug/gi false`) | no (120 s) |
| grass + foliage off (GI on) | no (120 s) |
| grass off, foliage on | yes |
| GPU scopes polled up to the loss | frames 5-13 ms right up to it → a SUDDEN fault, not load |
| `vkDeviceWaitIdle` before the occupancy copy (diagnostic build) | **no** (150 s, world settled) |

**Cause.** `VoxelLightOccupancyGpu::flushIfDirty()` `memcpy`'d the repacked directory + pool into ONE
shared, persistently mapped buffer, called from `updateLightOccupancy()` BEFORE `drawFrame`'s fence wait,
with 2 frames in flight — so the CPU rewrote occupancy another frame was still reading. A torn read handed
`phxOccupancySolid` a garbage mixed-cube count; its binary search (`mid = (lo + hi) >> 1`) overflowed for
counts near 2³² (a fully solid row packs as `0xFFFFFFFF`) and never converged. Foliage fragments reach
that search constantly through `phxAmbient` → `phxGiIrradiance` → `phxSegmentBlocked` on the leaf
microcubes' mixed cubes — hence "GI on + foliage on".

**Fix** (LightingPipeline.md §0 rule R11 + §9): one directory/pool copy per frame in flight; `flushIfDirty`
repacks on the CPU only; `uploadToSlot(currentFrame)` copies the latest pack into that slot right AFTER its
fence; each frame slot's descriptor set binds its own copy. Plus hardening, exact on valid data, in the
shader and its CPU mirror: count clamped to 32³, overflow-free midpoint, bounds-checked against the pool.
Cost: a second 64 MB host-visible pool.

**Verified:** CityBench at defaults 180 s, no loss, world settled (969 chunks, nothing pending);
`VoxelLightOccupancyTest` pins (corrupt count → terminates, "not solid"; every pack owed to every slot);
unit suite 4,061 passed / same 2 known failures; Lighting Lab ambient check GREEN, A1 0.985, A2 0.0716,
A3 0.9976 / 0.0715, A4 2.90 (`docs/evidence/ambient_occfix.json`) -- sealed rooms read exactly as before.
Evidence: `p1c/device_lost_trend.{py,json}`.

### 16.10 Step 0 + the first finding at real load: streaming stutter is main-thread CPU (2026-09-25)

**Release now emits symbols** (`/Zi` + `/DEBUG /OPT:REF /OPT:ICF` via the CMake cache; optimisation
unchanged): a CPU access violation (read of `0xffffffffffffffff`, a non-main thread) killed the first
step-0 attempt at 22:19 and its dump could not be symbolised. It did not recur on the re-run. Open: if it
recurs, `crashes/*.dmp` now resolves against `build/editor/Release/phyxel.pdb`.

**Step 0 (streaming throughput, CityBench, `p1c/step0_streaming_throughput.json`).** Straight camera
routes into never-loaded terrain: every chunk entry resident at 8, 16, 32 and 48 u/s; at 64 u/s the last
entry arrived unloaded. **Route speeds on this machine: walk 4 u/s, fast 32 u/s** (a margin under 48).

**But the frame time while streaming is bad at every speed:** p50 49–87 ms, p99 up to 344 ms, worst 486 ms.
One recorded 8 u/s route (`p1c/streaming_cost_{raw,summary}.json`, 1,360 frames): frame p50 24.8 ms, p95
67 ms, max 125 ms, attributed per phase:

| Phase | p50 | p95 | max | Cause |
|---|---|---|---|---|
| `drawFrame/Light Occupancy` | 0.2 | **35.5** | 59 | `VoxelLightOccupancyGpu` repacks the WHOLE pool on any chunk change (its header: "REPACK-ON-DIRTY ... optimise after the cost is MEASURED"). Measured. Grows with total resident sub-voxel detail -- i.e. with the city. |
| `Update/Water` | 0.02 | 0.03 | **59** | Rare large spikes (6 of the 10 worst frames): the water CA region re-reading solidity as the camera moves. |
| `drawFrame/Dirty Chunk Flush` | 4.3 | 23 | **72** | Chunk meshing on the main thread (S4, off-thread meshing). |
| `drawFrame/Record` | 0.9 | 1.3 | 10 | Rendering is not the problem here; `Fence Wait` p50 0.01 ms (GPU idle-waiting). |

**Streaming stutter is entirely main-thread CPU work, and none of it is rendering.** New P2 candidates,
to be re-ranked with the city data: **O1 incremental occupancy packing** (patch changed chunks'
blobs; keep the per-slot upload but copy only changed ranges), **W1 water region recentre off the main
thread or amortised**, and S4 (off-thread meshing, already planned).

### 16.11 RESULTS — the ladder exists, and the first growth table (2026-09-26, RTX 4090, Release, editor host)

**Two more device losses and three generator/persistence defects had to be fixed first** (all
committed, each red-before-green): the far-shadow cascade replayed cached far-tile/tree draws whose
buffers the graveyard had freed (`700d2ecd`, 5/5 clean vs control 2/2 lost); building lights did not
survive save/load and outlived a replaced building (`20bdb399`, C-25 67 → 0 after reload; 7 ghost
lights); and **the city tier could not make 100 buildings at all** — `buildings.max` 48 × density was a
fixed ceiling (72 at 1.5, 96 at the 2.0 clamp; a 192×192 site built the same 72 as 160×160). User chose
area scaling: `scaleForSite` grows the cap by site area past the tier's `reference_site` (160×160), scale-up
only (`0eedbcb4`). Logged, not fixed: trees cleared by a build regrow after a reload (a chunk saved
EMPTY loads as "not saved"; +0.2–0.4 % sub/micro) and CityForge plans overlapping lots (C-25: 26 → 25).

**The ladder** (all engine-generated, `POST /api/settlement/build` tier `city`, seed 7, density 1.5;
fresh copies of the CityBench world; every rung saved, relaunched and fingerprint-checked — placed
objects, residents, cubes and building lights identical across reload):

| Rung | Site | Buildings | Residents | Walkability gate | Build time | Project |
|---|---|---|---|---|---|---|
| C-25 | 96×96 | 25 | 29 | — | 285 s | `CityBench_C25M` |
| C-50 | 128×128 | 59 | 62 | — | 841 s | `CityBench_C50` |
| C-75 | 160×160 | 72 | 72 | 170/176 | 1,021 s | `CityBench_C75` |
| **C-100** | 192×192 | **104** | 104 | 241/251 | 1,531 s | `CityBench_C100b` |

**Rig facts** (each learned the hard way this session): settle + fingerprint from an ANCHOR straight above
the site centre (the overview pose left the C-75 far corner 274 u away, unstreamed, and the generator
correctly refused the build as ungrounded); the reference fingerprint is the RELOAD fingerprint (stable
across reloads; build-time state differs by the tree-regrowth drift); fixed poses come from the
generator's layout (`city_poses.py`: main street, site centre, rooftop +14 u, overview, 40 u outside);
defaults as shipped (prepass OFF — §16.7 assumed ON), IMMEDIATE present mode, clock paused.

**Growth table, street pose, noon, GPU ms** (all five poses in `p1c/growth_table.md`; raw rows
`p1c/attrib_*.jsonl`, 2 counterbalanced repeats × 240 GPU frames per cell):

| Pass | C-25 (25) | C-50 (59) | C-75 (72) | C-100 (104) | Growth |
|---|---|---|---|---|---|
| **GPU frame** | 45.1 | 55.0 | 89.8 | **103.0** | ~linear, **~0.75 ms / building** |
| Scene Pass / Far Terrain (tree-LOD pipeline) | 8.6 | 16.1 | 31.9 | **39.7** | **~linear, 0.4 ms / building** |
| Shadow Mid (420 u) | 11.1 | 13.1 | 19.2 | 20.0 | sub-linear |
| Shadow Near | 2.0 | 3.9 | 7.3 | 8.7 | ~linear |
| Static Geometry | 11.1 | 4.3 | 14.3 | 15.6 | pose-noisy, mild |
| GI Probes | 3.5 | 3.6 | 5.8 | 5.3 | flat-ish (+50 % C-25→C-75, then flat) |
| OIT | 1.2 | 1.8 | 2.7 | 3.4 | linear, small |
| night − noon | −0.2 | +0.4 | −0.6 | +1.0 | **none** |

Every pose tells the same story (C-100: 86–109 ms GPU at all five poses, i.e. **9–12 fps on a 4090**).
Against §16.7: **point lights REJECTED as a city cost** (night ≈ noon at every rung — L1 dedup already
took them off the table); **Shadow Mid CONFIRMED rising but it is second, not steepest**; GI probes
roughly flat (CONFIRMED within noise past C-75); characters negligible (0.2–1.4 ms). **The steepest
term, which no prediction named, is the "Far Terrain" scope.**

**What "Far Terrain" is here.** With far trees switched off (`POST /api/debug/far_terrain {"trees":
false}`), C-100 street drops 109 → 69 ms and the scope falls from 41.8 ms to < 1 ms. But that knob turns
off the whole tree-LOD pipeline, which ALSO draws the **structure LOD proxies** (one per building), and
the scope grows with the BUILDING count — distant forest does not. `lod_report` at C-100 shows every one
of the 104 proxies drawn at chain level 0 (the finest) at 150–210 u with `min_fade` 0: the shader's
distance fade (`vFade = max(smoothstep(fadeNear0, fadeNear1, dist), minFade)`, discard below the Bayer
threshold) throws away every fragment of every one of them. Suspect: 104 full-detail meshes drawn every
frame only to be discarded. §16.12 separates the two and tests a pixel-identical skip.

### 16.12 SHIPPED — structure proxies that the shader throws away are no longer drawn (2026-09-26)

**Change.** `tickStructureLod` skips a proxy's main-view draw when every fragment is provably discarded:
`minFade == 0` (its real building is resident) and its instance base is inside `fadeNear0 − 1 u` (the
smoothstep returns 0, so `vFade = 0 <` every Bayer threshold). Far-cascade casters are untouched. Default
ON; `s_structureLodSkipInvisible` + an attribution knob `s_structureLodEnabled` on
`POST /api/debug/far_terrain {"structures_skip_invisible", "structures"}`; `lod_report` reports
`skipped_invisible_last_frame`.

**Attribution + win, C-100, noon, interleaved ABBA** (`p1c/ab_structure_lod_C100.jsonl`, 2 pairs × 240
GPU frames per cell; this session's absolute numbers ran lower than §16.11's run — only pairs are compared):

| Pose | old | **skip (shipped)** | no proxies at all | Far Terrain scope old → skip |
|---|---|---|---|---|
| street | 75.8 ms | **49.8 ms (−26.0, −34 %)** | 49.8 ms | 28.2 → 2.8 ms |
| overview | 59.7 ms | **43.1 ms (−16.6, −28 %)** | 32.9 ms | 27.0 → 10.0 ms |

At street level the skip equals removing the proxies entirely: every proxy was invisible there. From
the overview ~10 ms of proxy cost is REAL (visible, still at chain level 0 — see next).

**Pixel gate** (`p1c/structure_skip_pixel_gate.py --freeze`: grass + foliage off, game paused, clock at
noon; A1 skip-on / B skip-off / C skip-on at matched timing; linear and shipping tone curves): street and
outside **bit-identical** (max delta 0, 65 proxies skipped); overview max 1–3/255, **no larger than its
own control** (103 skipped). Unfrozen, the city's noise floor (104 walking residents, grass) was ~4,100
px over 8/255 — too high to decide anything; freezing is required for gates in the city.

> **CORRECTION 2026-09-27: the gate above was invalid as labelled.** It froze the scene with the GAME
> PAUSE, and pausing (a) draws the pause menu into `/api/screenshot` and (b) makes `POST /api/camera` a
> no-op — each "street / outside / overview" run photographed the PREVIOUS pose with the menu over the
> middle of the frame (a contact sheet of the captures showed it). **Re-run with the no-pause freeze**
> (`city_pixel_gate.py /api/debug/far_terrain structures_skip_invisible --freeze`: residents + wildlife
> suspended, effect time + GI probe rotation held, player parked out of view, convergence wait, camera
> pose read back ≤ 0.05 u before any capture; `structure_skip_gate_C100_{street,outside,overview}_noon.json`):
> **street, outside and overview all PASS** on both tone curves (overview control and test exactly 0;
> street/outside test max 1/255 = control). The skip's pixel-identity claim now stands on valid captures.

**Next levers, re-ranked on C-100 after this fix** (to be re-measured with the fix in the growth table):
1. **Visible structure proxies still draw at L0** (the finest chain level) out to 360 u
   (`kStructureLevelDist[0]`): ~10 ms at the overview. A coarser level nearer, or a screen-space level
   rule, needs a visual check — it is a LOOK change, not free.
2. **Shadow Mid ~20–24 ms** (every building in 420 u casts, visible or not) — S-class shadow work.
3. **Static Geometry 7–16 ms** and the default-OFF depth prepass (−20..−37 % at S-1/S-2) to re-verify here.
4. Far trees proper: ~2.8 ms at street after the fix — no longer a city problem.

### 16.13 P-DP depth prepass re-verified at C-100, noon and night (2026-09-26/27; §17.2 step 2)

**Cost** (`p1c/ab_prepass_C100.jsonl`, noon, interleaved ABBA, 2 pairs × 240 GPU frames per cell):

| Pose | off | on | Δ | Static Geometry off → on | prepass cost |
|---|---|---|---|---|---|
| street | 71.8 | 64.8 | **−7.0 (−10 %)** | 14.9 → 5.6 | 2.6 |
| square | 51.4 | 49.0 | −2.4 | 6.2 → 2.7 | 1.5 |
| rooftop | 69.8 | 61.5 | **−8.3 (−12 %)** | 10.7 → 3.8 | 1.7 |
| overview | 52.4 | 54.2 | **+1.8** | 6.1 → 4.5 | 3.1 |
| outside | 60.7 | 58.8 | −1.9 | 6.3 → 2.5 | 2.6 |

It wins where buildings occlude each other (eye level, the gameplay case) and loses ~2 ms from the
overview, where little is occluded and the prepass costs more than it saves. The overview pair is
also confounded (visible instances 58.3 M vs 62.7 M between its arms — residency moved). Smaller than
§14's −20..−37 % at S-1/S-2 because Static Geometry is a smaller share of the city frame.

**Pixel gates.** The first noon gates here used the game pause and were INVALID (wrong poses, pause menu
in frame; see the §16.12 correction) — their "3–4 pixels at street" were really the overview view. All
gates below use the no-pause freeze with pose read-back (`p1c/prepass_gate_C100_<pose>_{noon,night}.json`):

| Pose | Noon | Night 22:00 |
|---|---|---|
| street | **1 pixel** differs (13/255); control 1/255 | **1 pixel** (12/255); control 1/255 |
| rooftop | not decided: control drifted 6/255 even after the convergence wait; the prepass stayed inside it (max 4) | **identical** (test = control, max 1/255) |
| overview | **3 pixels** (max 14–17/255); control exactly 0 | **3 pixels** (max 14–19/255); control exactly 0 |

Deterministic (same pixels at noon and night) and isolated: 1–3 of 600,695 viewport pixels, no contiguous
region, prepass-on slightly brighter. Inside `render_pixel_diff.py`'s "perceptually lossless" bar, but
NOT bit-identical as §14 claimed. Likely an equal-depth edge tie on merged-face borders (the known crack /
speckle class) — **not proven**.

**Night cost** (`p1c/ab_prepass_night_C100.jsonl`, residents on, ABBA 2 pairs): street 52.1 → 44.2
(**−7.9, −15 %**), square 43.4 → 41.6, rooftop 53.6 → 48.6 (−5.0), overview 44.4 → 44.8 (+0.4, the same
residency confound), outside 52.7 → 50.7. Same shape as noon.

**What the night freeze needed** (each found by a VOID control, then fixed): residents AND wildlife
suspended (`FaunaSpawner` fills in within 15 s of residents going); effect time held INCLUDING the GI
probe ray rotation (time-like; ×8 exposure made its night jitter visible); the PLAYER parked out of view
(idle animation + held torch light); and a convergence wait after each pose change (the camera-following
probe grid keeps blending for 10–30 s).

**For the user's decision (§8 #5):** −7..−8 ms at eye level (street, rooftop), about −2 ms at the square
and outside, 0 to +2 ms from the overview; 0–3 changed pixels per frame at the tested poses. Visual
comparison page: https://claude.ai/artifact/JiK48kRkbVv5RDFnuC4eFL (live captures off/on at five
poses, noon and night, with the enlarged changed pixels). Prepass stays OFF until the user decides.

---

## 17. ROADMAP FROM HERE: the guide (updated 2026-09-26)

This section is the living plan. Update it in the same commit as any result that changes the order: mark
steps DONE with the commit and the measured number, and re-rank from data, never from priors.

### 17.1 Operating point (RTX 4090, Release, editor host 1600×900, defaults, noon)

| Scene | GPU frame | Top passes | Source |
|---|---|---|---|
| C-100 street (104 buildings), before §16.12 | 103 ms | Far Terrain 40 (structure proxies), Shadow Mid 20, Static Geometry 16, Shadow Near 9 | §16.11 |
| C-100 street, after §16.12 (re-baseline) | **63.6 ms** | Shadow Mid 19.0, Static Geometry 13.7, Shadow Near 8.2, Far Terrain 4.9, GI 4.6, Foliage 4.5 | step 1, `p1c/growth_table_rebase.md` |
| C-100 overview (re-baseline) | **56.3 ms** | Shadow Mid 18.8, **Far Terrain 13.5 (visible proxies)**, Static Geometry 6.6, Foliage 5.7, OIT 5.3 | step 1 |
| C-100 outside (re-baseline) | **70.2 ms** | Shadow Mid 22.3, Foliage 13.3, Static Geometry 7.2, Shadow Near 6.6, Far Terrain 6.0 | step 1 |

**Re-baseline growth, street, noon (GPU ms), all rungs with §16.12 in:** C-25 51.5 → C-50 53.5 → C-75 61.5 →
C-100 63.6 (was 45 → 103). The steep building-count term is gone; what still grows with the city is
**Shadow Near** (2.9 → 8.2, ~linear) and **Shadow Mid** (14.4 → 19.0); the overview's Far Terrain (3.4 →
13.5) is the visible-proxy residual (step 3). **Shadow Mid is now the #1 term at EVERY pose** (15–22 ms).
Session drift is real: C-25's street frame rose 45 → 51.5 between sessions with no change affecting it,
and night − noon ran ±10 ms this session (it was ±1 before) — decide nothing from a single session's
absolute or night delta; interleaved pairs only.

Established facts to build on: point lights cost nothing measurable in the city (night ≈ noon, every
rung); GI probes ~5 ms and flat; characters ≤ 1.4 ms; far trees ~2.8 ms at street; streaming stutter is
main-thread CPU (Light Occupancy repack, Water, Dirty Chunk Flush; §16.10), not GPU.

### 17.2 Ordered next steps

Each step: **why** (the data), **do**, **measure**, **done when**. Design check 9.9 (below) is folded in.

0. **Close the design-check debts on what already shipped (§16.12)** before building anything new:
   - **Echo the knobs.** `set_far_terrain` (`Application.cpp` handler) echoes only `enabled` and
     `per_instance_levels`; it must also return `structures`, `structures_skip_invisible` and `trees` (the
     keys' "echo the resulting state"; today only `lod_report` shows them).
   - **Unit-pin the skip predicate.** Extract it to a pure function (`structureProxyFullyDiscarded(base,
     camera, fadeNear0, minFade)`) and pin `StructureLodSkipTest.SkipOnlyWhenShaderDiscardsEverything`
     against a CPU transcription of `far_tree_mesh.vert/.frag` (`max(smoothstep(e0, e1, d), minFade) <
     bayer_min`, bayer_min = 1/32) at the boundaries: d = fadeNear0 − 1 ± ε, d just inside fadeNear0,
     minFade = 0 vs the smallest positive float. **Red (corrected while building it):** on the CPU a
     margin-less predicate and one that skips at tiny minFade are both still SOUND (the fragment keeps
     only if vFade ≥ 1/32, so any minFade < 1/32 is still fully discarded; the 1 u margin guards CPU-vs-GPU
     float disagreement, which a CPU test cannot reproduce). So the tests are three:
     `SkipOnlyWhenShaderDiscardsEverything` (soundness sweep; red on a predicate that skips at
     minFade ≥ 1/32 or where smoothstep ≥ 1/32), `KeepsTheOneUnitMarginAndExactZeroMinFade` (the stricter
     spec, pinned as spec), `SkipsTheResidentCityCase` (red on a never-skip predicate). Today only the L4
     pixel gate covers it.
   *Done when:* both land with the test shown red first.
   **DONE 2026-09-26.** Predicate extracted to `RenderCoordinator::structureProxyFullyDiscarded`;
   `StructureLodSkipTest` (3 tests, `tests/core/LodServiceTest.cpp`) shown RED against a deliberately
   unsound mutation (skip at minFade < 0.5 and up to fadeNear0 + 20 u: soundness caught it at 265.5 u,
   the first distance where smoothstep ≥ 1/32; the margin test failed inside (255, 256]) and GREEN on the
   real predicate. `set_far_terrain` now echoes `trees`, `structures`, `structures_skip_invisible`. Live
   on C-100 street (`p1c/step0_echo_live.json`): echoes follow set/restore; 103 proxies still skipped;
   `solid_proxies_in_band` 0. Unit suite 4,070 pass; the same 2 known failures (AtlasManager cache,
   FineFaceMerge) as before the change.
1. **Re-baseline the growth table with §16.12 in.** *Why:* §16.11's numbers predate the skip; every later
   A/B needs the true starting point. *Do:* `run_rung_attrib.sh` on all four rungs (§17.4), then
   `growth_table.py`. *Done when:* §17.1 row 2 holds measured numbers for every rung and pose.
   **DONE 2026-09-26** (`attrib_*_rebase.jsonl`, `growth_table_rebase.md`, §17.1). Consequence for the
   order: Shadow Mid is the largest term at every pose, so step 4 (shadows) outranks step 3 (visible
   proxies, overview only) on cost; step 3 stays next only because it is smaller and its knob is simple.
2. **Re-verify the shipped wins at C-100 and decide P-DP's default** (§16.6 step 5, §8 #5). *Why:* L1, P-DP
   and GI-2 were measured at S-1/S-2 scale only; P-DP (−20..−37 % there) is still OFF. *Do:* ABBA with
   `perf_harness.py sample --ab` (prepass on/off) at the five C-100 poses, noon + night; frozen pixel gate
   (the prepass is equivalence-class). **Night freeze without game pause:** game pause stops the
   emissive reconcile (§13), so a night gate that pauses the game compares a stale light set. Night
   freeze = clock paused at 22:00 + grass/foliage off + residents despawned (step 7's route) + **effect
   time held** (new debug knob, e.g. `POST /api/debug/effect_time {"frozen": bool}`, echoing its state)
   while the game, and so the emissive reconcile, keeps running. The knob holds BOTH time domains and
   changes nothing but those time inputs (say so at the knob): (1) the GPU time `ubo.elapsedTime`, read
   by ~20 shaders (`static_voxel.vert`, `kinematic_voxel.vert`, `dynamic_voxel.vert`, `character.frag`,
   `sky.frag`, `transparent_voxel.frag`, the far-terrain/far-tree/foliage/grass stages); (2) the CPU
   clocks that modulate light intensity: `FireEmitterManager` pulse/flicker and `VfxSystem`. The player
   character is hidden or kept out of frame (its animation is not on either clock). **Known remaining
   temporal source:** the GI probe ray-set rotation advances every rendered frame, paused or not
   (`GiProbeField.cpp:124`); the frozen noon gates measured it at 0/255 (street, outside) to 1–3/255
   (overview). So the control criterion is numeric: **frozen A1-vs-C control max ≤ 3/255 and 0 pixels
   over 8/255**, or the run is void. **Fallback** if the knob cannot be made clean: the unfrozen timing-matched gate
   (`gi_pixel_gate.py` method) with its noise floor stated in the result. **Default pin:** no test pins
   the prepass default today (nothing in `tests/` references it); flipping it must ADD
   `RenderDefaultsTest.DepthPrepassDefault` in the same commit, with the reason. *Done when:* written up as a
   §16.x, the default decided with the user, and pinned.
   **TESTING DONE 2026-09-27 (§16.13):** cost noon + night at all five poses; pixel gates at street,
   rooftop, overview, noon + night, on the corrected no-pause freeze (1 px street, 3 px overview, rooftop
   identical at night / undecided at noon). Knobs built: `/api/debug/effect_time` (UBO time + VFX dt + GI
   probe rotation), `/api/debug/residents {enabled, fauna}`. Visual comparison page published for the user.
   **DONE 2026-09-27: default ON** — the user flipped it live at the C-100 street pose (17 → 21 fps), saw no
   difference and approved; pinned by `RenderDefaultsTest.DepthPrepassDefault`; `LightingPipeline.md` §0/§9
   updated.
3. **Visible structure proxies still draw at chain level 0 out to 360 u** (~10 ms at the overview). *Why:*
   §16.12's residual (overview: skip 43.1 vs no proxies 32.9 ms). *Options:* a coarser level nearer
   (`kStructureLevelDist`), or a screen-space level rule (the level whose cell projects to ≤ ~1 px, the C1
   metric). **A LOOK change** (§8 #6): before/after captures at the overview and outside poses for user
   sign-off; `LodTierLedger.md` row 7 updated. **Chunk independence:** level choice stays a function of
   the structure's own world-space centre distance (never a chunk's), as today. **Where the cost is:**
   inside `fadeNear0` (256 u) a resident building's proxy is already fully discarded (and now skipped,
   §16.12), so the visible-L0 cost lives in **256–360 u**: the dither fade band (256–346) plus the run
   up to `kStructureLevelDist[0]` = 360 where L1 takes over. **Rig (before the city):** a Flat world, ONE
   engine-built building (`build_structure` schema v2, a tavern — provenance recorded) inside one chunk;
   the only variable is camera distance: **280, 320, 346, 400, 500 u**, same bearing, pose-verified.
   **Prerequisite knob (BUILT 2026-09-27:** `s_structureLevelDist`, `structure_ladder` on `set_far_terrain`,
   validated + echoed, read by all three readers incl. `lod_probe`; default pinned by
   `StructureLodSkipTest.StructureLadderDefaultIsUnchanged`**):** `kStructureLevelDist` was `static constexpr` (`RenderCoordinator.h:1199`), so
   candidates cannot be A/B'd back-to-back without a rebuild per candidate. Make it a runtime static
   with a `structure_ladder` knob on `set_far_terrain`, the same shape as `tree_ladder`: exactly 5 values,
   strictly ascending and > 0, otherwise refused with the reason at the check (a non-ascending ladder
   makes the level search skip levels); the response echoes the ladder in effect; `lod_report`
   (`Application.cpp` structures `level_dist`) reads the runtime ladder, not the constant. All six levels
   are already uploaded per structure, so a switch applies the next frame. Record the residency state
   (`lod_report` `readiness` / `last_min_fade`) with every capture. No
   eviction (there is no API for it, and none is needed): at each distance capture today's level vs the
   candidate level; the real building, where resident, is identical in both captures, so the diff
   isolates the proxy. **Prediction, written first:** at 280–346 u the candidate's delta is damped by the
   dither (the proxy is partly transparent) and stays below L0-vs-L1's delta at today's 360 u switch; at
   400–500 u both captures already use L1+, so the delta is ~0. **Control:** today's level vs itself at
   each distance (~0).
   **Run 1 (2026-09-27), written BEFORE running.** Candidate A = ladder `[256, 500, 700, 900, 1200]` vs today
   `[360, 500, 700, 900, 1200]`: L1 takes over at 256 u, where the proxy starts to fade in, so L0 is never
   visible (below 256 it is fully discarded and skipped). Rig: `CityBench_ProxyRig` (fresh copy of the
   streaming base, seed 7), one engine-built tavern near the site centre; frozen (no-pause freeze);
   camera elevated on one bearing, looking at the building. **Predictions:** (a) 280/320/346 u: the
   A-vs-today difference is confined to the building's screen footprint, small (≤ 15/255 on most
   pixels) because the proxy is still dither-fading and L1 cells (6 micros) are close to L0's (3 micros)
   at that size on screen; (b) 400 and 500 u: exactly 0 (both ladders pick L1); (c) control (today vs
   today) ≤ 3/255 everywhere. A difference outside the building footprint, or any change at 400/500 u,
   falsifies the claim that only the proxy level changed.
   **Run 1 RESULT (`p1c/proxy_ladder_rig_run1.json`): candidate A REJECTED.** (b) 400/500 u: exactly 0 ✓.
   (c) control 0 at 320–500 u; one 4-px spot (13/255) at 280 u. (a) the change IS confined to the building
   (≈30×40–85 px at screen centre) but is NOT small: up to 70/255, 150–263 px over 8/255, the roof turning
   lighter and speckled. Why: in the 256–352 u band the real building is still resident and drawn (the
   rig's edited chunks stayed resident even at 500 u; a saved city would evict them past ~352 u) and the
   proxy fades in on top of it; L0 (⅓-voxel cells) coincides with the real surfaces and vanishes into
   them, while L1 (6-micro cells) pokes through them. **L0 is not waste: it is what makes the handoff
   invisible.** The prediction (≤ 15/255) was wrong.
   **Proxy cost with the prepass ON** (`p1c/ab_proxies_prepass_on_C100.jsonl`, C-100 noon, ABBA): overview
   Far Terrain 10.6 → 1.4 ms with proxies off (GPU frame 45.3 → 35.8); rooftop and outside ~0. So the lever
   is real, ~9 ms, and only in views across the whole city.
   **Next candidate (needs its own design check): merge the proxy MESH, keep the level.**
   `TreeLodMeshRegistry::buildLevelMesh` emits one quad per exposed cell face, with no merging, so an L0
   proxy is thousands of ⅓-voxel quads per building. `far_tree_mesh.frag` derives UVs from world position
   (`worldFaceUV(vWorldPos, face)`) and a vertex carries only position + texture + face, so greedily
   merging coplanar, same-texture, same-face cells gives the same shading at a fraction of the vertex and
   raster cost (equivalence-class, no look change). Known risk: T-junction cracks where merged and
   unmerged edges meet (the chunk greedy-merge crack class) — the frozen pixel gate at the overview must
   show it, and the look must be checked in the crossfade band where L0 overlays the real building.
   **Design (design check 9.12 folded in):**
   - **Why shading cannot tell** (verified in code): `worldFaceUV` is a pure world projection (`wp.xz` /
     `wp.zy` / `wp.xy`, no per-voxel hash rotation); `FarVertex` is position + texture + face only;
     instances are translation-only (`far_tree_mesh.vert`, no scale/yaw jitter); textures are array
     LAYERS, so UVs past 1.0 wrap. The merge input is one proxy's cell set (a structure's soup or a tree
     template), never a chunk; deterministic scan order (x, y, z per face plane).
   - **Watertight, not plain greedy.** After merging, split every quad edge at each neighbouring quad's
     vertex that lies inside it, so no T-junction remains (plain greedy merge leaves one-pixel cracks
     that show the sky or terrain behind — the open chunk-merge crack class).
   - **Scope: trees AND structures** (the builder is shared: `TreeLodMeshRegistry.cpp:157` species,
     `RenderCoordinator.cpp:658` structures). Both are gated; a forest pose joins the gates.
   - **Runtime A/B knob:** `proxy_mesh_merge` on `set_far_terrain` (boolean; omitted = unchanged) sets the
     builder option, retires every structure proxy through the existing graveyard path and rebuilds the
     tree species set; echoes `proxy_mesh_merge` plus rebuild progress (`proxies_built` / `pending` for
     structures and species). The harness waits for pending = 0 before sampling.
   - **Species rebuild must be frame-safe (design check 9.13).** Today a species is built once, on first
     request (`TreeLodMeshRegistry::level` → `m_queued` → builder thread), and the only teardown,
     `cleanup()`, destroys buffers immediately (shutdown only). Reusing it at runtime would free buffers
     that in-flight frames (and the one-frame-stale far-shadow caster cache) still draw — the use-after-free
     class behind this week's two device losses. New path: `retireAllSpecies()` moves every species'
     levels into a frame-deferred graveyard (≥ 4 frames, the far-terrain / structure pattern, ticked from
     `tick()`), clears `m_species` AND `m_queued` so `level()` re-requests on demand (cards cover
     meanwhile), and bumps a **generation stamp**; a build that finishes carrying an older generation is
     discarded, not landed. Structures already have this (`structureLodRetiring` waits for running jobs;
     `structureLodGraveyard` defers frees).
   - **The option travels with the job.** The merge flag is snapshotted into each species request and each
     structure `std::async` job when it is queued; builder threads never read a mutable static (a data
     race otherwise).
   - **Edge splitting is spatially indexed.** Vertices are bucketed per edge line (plane + fixed axis +
     offset), so each quad edge is split against only the vertices on its own line: O(n log n), not the
     naive pairwise O(n²). **Prediction:** a full C-100 rebuild (104 structures + all species) completes in
     a few seconds on the builder threads with no main-thread stall; measured and recorded.
   - **Measurement:** `lod_report` reports quads per structure and per species, merged and unmerged.
   - **Tests (L2, headless, red first):** `ProxyMeshMergeTest.MergedCoversExactlyTheSameFaces` —
     decompose every merged quad into unit cell faces; the (cell, face, texture) set equals the unmerged
     builder's, no overlaps (RED: a merge that ignores texture); `ProxyMeshMergeTest.NoTJunctions` — no
     vertex lies strictly inside another quad's edge in the same plane or at a shared corner edge (RED:
     plain greedy without edge splitting). **Inputs (named):** (i) a synthetic stepped, L-shaped building
     whose wall changes material partway along a face (exercises texture boundaries and the T-junctions at
     every step); (ii) a real tree template loaded from `resources/templates/nature/`; both run through
     `TemplateLodChain` + `buildLevelMesh` headless at EVERY chain level.
   - **L4:** frozen pixel gates (no-pause freeze) at the C-100 overview, rooftop and outside, a forest pose,
     and the proxy rig at 280/320/346/400/500 u (the crossfade band, where L0 overlays the real building).
   - **Predictions, written first:** quads per structure L0 drop 5–20× (flat walls and roofs); C-100
     overview Far Terrain 10.6 → about 3 ms; every gate within its control (0 changed pixels beyond the
     control) because shading is tessellation-independent and the mesh is watertight. A crack pixel in any
     gate falsifies "watertight".
   - **Default:** ships ON once proven, pinned by `RenderDefaultsTest.ProxyMeshMergeDefault`; `LodTierLedger.md`
     rows 5 (tree mesh tier) and 7 (structure proxies) updated in the same commit.
   **BUILD 2026-09-27.** `TreeLodMeshRegistry::MeshOptions{merge, splitTJunctions}`; the merged builder
   greedy-merges per (face, plane) in a fixed scan, indexes every rectangle corner by its axis line and
   splits edges at them, then emits a plain quad (nothing split) or a centre fan. Tests (L2, headless):
   `ProxyMeshMergeTest.MergedCoversExactlyTheSameFaces` RED on a texture-blind mutation (faces covered 0
   times by triangles of their own material, stepped building and every oak level), GREEN on the real
   merge; `ProxyMeshMergeTest.NoTJunctions` GREEN, with its built-in teeth (plain greedy on the stepped
   building has T-junctions > 0). **SCOPE NARROWED BY DATA: trees are not merged.** On `forge_oak_m` the
   merge saves only 1.0–1.2× triangles per level (L0 9,832 → 8,030; irregular leaves leave few flat runs,
   and edge splitting adds some back), not worth the species rebuild machinery 9.13 required. Structures
   only: `s_proxyMeshMerge` snapshotted into each build job (`e.merged`), `rebuildAllStructureLod()` via the
   existing retiring + graveyard path, `proxy_mesh_merge` on `set_far_terrain` (echoed), and `lod_report`
   structures gain `tris_l0` per entry plus `proxy_mesh_merge`, `tris_l0_total`, `pending`.
   **L4 RESULTS (C-100, noon, prepass on).** Triangles (`p1c/proxy_merge_tris_C100.json`): L0 total over 103
   proxies **1,388,232 → 145,442 (9.5×)**, median building 10,668 → 1,382; full rebuild 8.4 s off-thread
   (predicted 5–20× and a few seconds: ✓). Cost (`p1c/ab_proxy_merge_C100.jsonl`, ABBA): overview Far
   Terrain **9.97 → 2.11 ms**, GPU frame **40.6 → 32.1 ms (−8.5, −21 %)**; rooftop/outside unchanged
   (predicted ~3 ms: ✓). **Pixel gates (frozen, no pause; `merge_gate_C100_*.json`): rooftop and outside
   PASS; OVERVIEW FAILS** — 286 px (linear) / 434 px (shipping) over 8/255, max 64/255, control exactly 0,
   all on the farther buildings (the 256–352 u band where the proxy fades in over the still-resident real
   building). Prediction "0 changed pixels" FALSIFIED there. **Cause (code-verified):** the proxy main pass
   uses the scene's STRICT depth test (`TreeLodRenderPipeline.cpp:304`, `sceneDepthCompareOp()`) and draws
   after static geometry. An unmerged L0 face has exactly the real voxel face's corners, so its depth is
   bit-equal and the real surface always wins the tie (that is why L0 "vanished into" the real building in
   run 1). A merged face covers the same area, but its depth is interpolated across a larger triangle and
   comes out a hair nearer at scattered pixels, so the proxy wins there: z-fight speckle, not cracks.
   **Candidate fix (needs its own design check):** push structure proxies a hair BEHIND in depth in the
   main pass (a constant + slope depth bias toward far, applied only to structure-proxy draws via dynamic
   depth bias), so any coincident real surface always wins, merged or not; where no real building is
   resident the proxy only competes with terrain, within the bias distance. Gate: the same overview gate
   must then read within control, and the unmerged-with-bias vs unmerged-without-bias image must also be
   within control (proving the bias changes nothing today's look depends on).
   *Done when:* rig numbers + city captures signed off, and the C-100 overview A/B measured.
4. **Shadow Mid (~20–24 ms) and Shadow Near (~9 ms, linear in buildings).** *Why:* the largest GPU term
   left after step 3; every building within 420 u casts whether or not its shadow can reach the view.
   *First:* attribute shadow cost per caster class (chunks / characters / LOD casters) at C-100.
   *Candidates:* **SH1** cull casters whose sun-extruded volume misses the view frustum (cannot affect a
   pixel: equivalence-class, frozen pixel gate); **SH2** cache static casters in the mid cascade (re-render
   only when casters change or the fit moves past a texel budget, as the far cascade already runs on a
   cadence; the moving-sun case must be gated); **S6** micro faces below a shadow texel (deprioritised at
   S-2 scale; recheck with the `voxel_tiers` shadow counts). *Done when:* the chosen one ships through §6.
   **Design-check requirements (9.9):**
   - **SH1 must cull against EVERY view that samples the cascades**, not just the main camera:
     `mirror_voxel.frag` (the reflection pass) samples the shadow maps too, and so do far terrain,
     far trees, foliage, grass, characters and transparent voxels, all inside the main view. The cull is
     per-chunk AABB, so it must be CONSERVATIVE (the `bladesForDistance` model: chunk quantities bound
     cost only). Pin it **headless**, in the chunked-vs-whole shape:
     `ShadowCasterCullTest.CullKeepsEveryCasterThatCanShadowAView` — sample points across every view
     that reads the cascades (main + reflection frusta, near to far plane), march each point toward the
     sun to the cascade's light-space near plane, and assert every chunk AABB the ray hits is in the KEPT
     set (brute force vs the cull predicate; random + adversarial camera/sun poses). **Red:** a cull
     against the main frustum only must fail it on the mirror case. **Rig:** Flat world, one chunk: a wall just OUTSIDE the view frustum
     whose shadow falls INSIDE it (the adversarial case), plus a mirror pose that sees a caster behind
     the camera. **Prediction:** identical shadow-map texels under the view; **control:** culling off vs
     off. Frozen pixel gate at the C-100 poses.
   - **SH2 must not step.** A cached mid cascade re-rendered on a threshold makes shadows move in steps
     while the sun moves and the camera translates, a class of motion the user has rejected before
     (vegetation wind). **Stepping metric:** capture N consecutive frames (N ≥ 120) twice — (a) sun
     advancing at game speed, camera still; (b) camera walking at 4 u/s, sun still — cached and
     uncached. Per consecutive frame pair, the max pixel change inside the SHADOWED region (masked with
     the sun-visibility debug view). **Pass:** the cached run's max frame-to-frame change never exceeds
     the uncached run's p99 + 2/255, and its count of frames above the uncached p99 is not larger than
     uncached. **Red:** a cache refreshed every K frames with no rebase must fail (b). The cascade fit is a `LightingPipeline.md` §0 item: update §0 and
     §9 and run `lighting_doc_check.py --update` in the same commit.
   - Knobs for either follow the `/api/debug/*` conventions (omitted = unchanged, state echoed).
5. **Static Geometry (7–16 ms).** Third term, pose-dependent, likely overlaps P-DP (step 2): re-rank after
   it. S1 (covered-face cull) was 1–2 % at S-2; recheck with `voxel_tiers` at C-100.
6. **Walk routes + hitch report** (§16.6 steps 3-4; the P1c exit criterion). *Why:* the user's complaint
   is stutter while MOVING through big towns, which static poses cannot see. *Do:* `rig_common.route()` at
   4 and 32 u/s through each rung (A/A first, 8 pairs), `hitch_analysis.py` for per-hitch causes.
   *Expected (§16.10):* Light Occupancy repack, Water, Dirty Chunk Flush → **O1** incremental occupancy
   packing, **W1** water recentre amortised or off-thread, **S4** off-thread meshing. *Done when:* the hitch
   report exists for every rung and the top cause has a planned fix.
7. **Residents-OFF arm** (once per rung, §16.1): sizes the NPC/AI CPU share (predicted linear in
   residents, §16.7). **Mechanism decided (9.9):** the generator already has `{"residents": false}`
   (`SettlementBuildService.cpp:1721`), but a BUILT rung re-derives its residents at every load
   (ResidentSpawner reads the persisted locations), so the param would need a 5-25 min rebuild per rung
   and a second world. A per-NPC despawn (`/api/npc/remove`) does NOT work either: `ResidentSpawner::update`
   rescans on a throttle and re-spawns/adopts every resident whose ground is resident
   (`ResidentSpawner.h:8-11,50-53`). **Mechanism:** a new debug route that SUSPENDS the spawner and calls
   its existing `despawnAll()` (`ResidentSpawner.h:55`), e.g. `POST /api/debug/residents {"enabled":
   false}` → echoes `{enabled, despawned, remaining}`; `{"enabled": true}` resumes (the spawner re-derives
   residents deterministically from the locations). Verified by `/api/npcs` reading 0 residents at the
   START and the END of every sampling window (a respawn mid-window voids the row), recorded in every
   row. The same route is step 2's night freeze. **ROUTE BUILT 2026-09-27** (`/api/debug/residents
   {enabled, fauna}`; live: 104 despawned, 0 NPCs held for 40 s with fauna off, 107 back on resume). The
   residents-OFF measurement itself is still owed.
8. **Foliage at the outside pose (12–22 ms)** — F1 (§12): attribute overdraw vs shading with the R-F1 rig.
9. **Owed platforms:** the laptop run of the ladder (min-spec; lights may matter there, §1) and the
   standalone `--test` at native resolution (blocked for streaming worlds: standalone games never pump
   streaming, `StructurePipelineGaps.md` 2026-09-25).

**Demoted by data** (revisit only if a new scene says otherwise): L2 clustered lights, L3c cached
visibility, L4 grass/foliage light gate (lights ≈ 0 in the city); S2/S5 (geometry ≈ free on the GPU).

### 17.3 Correctness debts the benchmark found (logged in `StructurePipelineGaps.md`, not fixed)

| Debt | Impact | Fix direction |
|---|---|---|
| Trees cleared by a build regrow after a reload (a chunk saved EMPTY loads as "not saved") | +0.2–0.4 % sub/micro per rung; trees return over cleared lots | Fix together with `ChunkManager::ensureChunkAt` creating EMPTY chunks in streaming worlds; both red tests are specified in the gap entry |
| CityForge plans overlapping lots; the later build silently replaces the earlier house | C-25 built 25 of 26 | Reserve realized footprints; a settlement job must not remove a structure it built |
| Scene transitions restore neither structure lights nor item props | Transitioned scenes lose lamps and props | One shared placed-object world-load routine used by every load path |
| (pin for the tree-regrowth fix) | A chunk created on demand must equal one streamed in | `PlacementChunkEqualsStreamedChunk`: `ensureChunkAt` on a generator world yields the same voxels as the streaming worker for that coord (the generator is per-chunk order-independent, `FloraMarginTest`) |
| Standalone games never pump streaming | Blocks the standalone half of the benchmark | Move the pump into `EngineRuntime` |

### 17.4 Runbook: running the city benchmark

All scripts live in `docs/evidence/perf2026-09/p1c/`.
- **Projects** (`Documents/PhyxelProjects`): base `CityBench` (never build in it — copy it); rungs
  `CityBench_C25M` (96², 25 buildings), `CityBench_C50` (128², 59), `CityBench_C75` (160², 72),
  `CityBench_C100b` (192², 104). A new rung = a copy of `CityBench` in a NEW directory; never overwrite
  an existing world.
- **Build a rung:** `city_build.py C-<N> <W> <D> <prefix>` — generator only, explicit save, fingerprint
  at the ANCHOR above the site centre, retries honest generator refusals; `--attach=<job>` adopts a job
  already running. **Verify persistence:** relaunch, `city_build.py --fingerprint-only …`, then
  `fingerprint_diff.py <prefix>` and `chunk_diff.py <prefix>`. The REFERENCE fingerprint is the reload one.
- **Measure a rung:** launch the engine on the project (Release, MCP `launch_engine`), then
  `sh run_rung_attrib.sh <prefix> <W> <D> <N>` (anchor settle → generator-derived poses → noon/night
  ABBA); tables with `attrib_table.py <run.jsonl>` and `growth_table.py`. A/B any knob with
  `tools/perf_harness.py sample --ab <configs.json>` (examples: `ab_structure_lod.json`,
  `ab_far_trees.json`, `tod_noon_night.json`).
- **Pixel gates in the city must freeze:** `structure_skip_pixel_gate.py … --freeze` is the template
  (grass + foliage off, game paused, clock at noon). Unfrozen, the residents + grass noise floor is ~4k px
  over 8/255 and decides nothing. Game pause also stops streaming, so settle before freezing.
- **Footguns met in this program:** the far-trees knob also removes the structure proxies (use the
  `structures` knob to separate them); the overview pose does not stream the C-75+ corners (settle at the
  ANCHOR); absolute GPU numbers drift between sessions (compare interleaved pairs only); one clean run of
  an intermittent fault proves nothing (several runs per arm, with the trigger shown to fire); Bash
  heredocs execute backticks (write docs and scripts with Write/Edit); `phyxel.log` rotates at 64 MB
  (`phyxel.log.1`).
