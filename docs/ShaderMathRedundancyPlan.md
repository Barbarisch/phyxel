# Shader Math Redundancy — Fix Plan

> **Status: ✅ EXECUTED 2026-07-03** (branch `render-perf-shader-math`). Both increments shipped
> and verified visually identical at the three fixed poses. **Measured FPS outcome: no change**
> — verified-pose A/B (old vs new shaders, same binary/world) showed differences within noise;
> the RTX 4090 driver was already hoisting the uniform mat4×mat4 (the caveat below, confirmed).
> Kept anyway: correct-by-construction, fixes the misdeclared `static_voxel.vert` UBO block
> (declared offsets didn't match the C++ struct — unread fields, latent trap), removes the
> per-fragment matrix product in `character.frag`. Full results + FPS-methodology warnings
> (HTTP camera unreliable, ±20% restart variance): `RenderOptimization.md` "Baseline 2026-07-03".
> Deviation from plan: `character.frag` consumes `ubo.biasedLightSpace` in-place instead of a
> vertex-shader varying — the character vertex stage has no UBO binding, and adding one would
> mean descriptor stage-flag surgery for negligible further gain.
>
> Original plan follows.
>
> Written 2026-07-02 from an audit inspired by the
> ["Redundancy seen in AAA game engines" series](https://zero-irp.github.io/Redundancy-seen-in-AAA-game-engines/)
> (z1rp). Companion research digest: [`EngineAdvancesResearch.md`](EngineAdvancesResearch.md).
>
> **⚠️ Coordination:** engine sources may be under concurrent edit in another session. Before
> starting: `git status` / ask the user which files are in flight. This plan touches
> `engine/include/vulkan/VulkanDevice.h`, `engine/src/vulkan/VulkanDevice.cpp` (updateUniformBuffer),
> `engine/src/graphics/RenderCoordinator.cpp`, and shaders under `shaders/`. If those overlap the
> other session's work, wait or rebase.

## Background — the pattern being fixed

The article series shows AAA engines paying "death by a thousand cuts": full/generic matrix math
where a trivial operation suffices, distributed so evenly no profiler spike appears. The four
patterns: (1) full mat4 multiply for a swizzle/add, (2) recomputing a value already stored,
(3) extracting matrix rows via multiplication by unit vectors, (4) generic `inverse()` on rigid
transforms where `mat3(M)` / transpose is exact.

A codebase audit (2026-07-02) found Phyxel's **CPU hot loops clean** (kinematic `buildFaces`,
chunk meshing, `VoxelRigidBody` inertia via `R·I·Rᵀ`, GPU-physics prep, sequential-impulse solver
— all verified free of these patterns). The waste is concentrated in **GLSL vertex/fragment
shaders**, where GLSL's left-associativity makes `proj * view * vec4(p,1)` execute a full
**mat4×mat4 per vertex** even though `proj*view` is frame-constant.

**Why it matters here:** the #1 open issue is render density (~412k faces → ~49 FPS on the
furnished tavern, `docs/RenderOptimization.md`). ~412k faces ≈ **~1.65M static vertices/frame**,
each currently paying 1–2 redundant mat4×mat4 products. This plan does NOT replace the parked
greedy-mesh work (RenderOptimization.md plan item 1); it's an independent, low-risk cut of
per-vertex ALU on the same hot path.

**Honest expectation:** modern drivers (NVIDIA uniform datapath, AMD scalar unit) can partially
hoist uniform×uniform products, so the FPS gain may be smaller than instruction counts suggest.
Findings #3 and #4 (per-fragment matrix product; per-vertex generic `inverse()`) are unambiguous
wins regardless. **Measure, don't assume** — see Verification.

## Findings (audited 2026-07-02 — re-verify line numbers before editing; sources shift)

| # | Where | Waste | Fix |
|---|-------|-------|-----|
| 1 | `shaders/static_voxel.vert:318`, `dynamic_voxel.vert:263`, `kinematic_voxel.vert:128`, `grass.vert:137`, `foliage.vert:104`, `debug_voxel.vert:155`, `debris.vert:27`, `debug_line.vert:17` | `ubo.proj * ubo.view * vec4(worldPos,1)` → mat4×mat4 **per vertex** | Use precombined `ubo.viewProj` (see Increment 1). Reference for the correct pattern already in-repo: `water.vert:28` uses a precombined `viewProj` push constant. |
| 2 | `static_voxel.vert:203`, `dynamic_voxel.vert:256`, `kinematic_voxel.vert:119` | `const mat4 biasMat` (compile-time constant) `* ubo.lightSpaceMatrix * vec4(worldPos,1)` → mat4×mat4 per vertex | Bake `biasMat * lightSpaceMatrix` on CPU once per frame → upload as `ubo.biasedLightSpace`; shader does one mat4×vec4. |
| 3 | `character.frag:95` | Same biasMat×lightSpaceMatrix product **per rasterized fragment**; shadowCoord belongs in the vertex shader | Compute `shadowCoord` in `character.vert` from the precombined matrix, pass as interpolated varying. |
| 4 | `character.vert:63` | `mat3(transpose(inverse(pushConsts.model)))` per vertex. Model is rigid (built `RenderCoordinator.cpp:1728` as translate×mat4_cast, no non-uniform scale) → normal matrix **is** `mat3(model)` | `fragNormal = mat3(pushConsts.model) * inNormal;` — exactly what sibling `character_instanced.vert:75` already does. |
| 5 | `character.vert:61`, `character_instanced.vert:72` | `pushConsts.viewProj * pushConsts.model * vec4(localPos,1)` — draw-constant mat4×mat4 per vertex; `model * vec4(localPos,1)` also computed twice (clip path + fragWorldPos) | Reassociate: `vec4 wp = pushConsts.model * vec4(localPos,1); gl_Position = pushConsts.viewProj * wp; fragWorldPos = wp.xyz;` (Or precombine MVP on CPU if a slot is free.) |
| 6 | `RenderCoordinator.cpp:1805` | `viewProj = cachedProjectionMatrix * cachedViewMatrix` rebuilt **inside** the per-entity fallback loop | Hoist above the loop. Trivial; do it while in the file. |

## Increments (each independently buildable + verifiable)

### Increment 1 — `viewProj` + `biasedLightSpace` in the UBO (findings 1, 2, 6)

1. **UBO** (`engine/include/vulkan/VulkanDevice.h:104` `struct UniformBufferObject`): append
   `alignas(16) glm::mat4 viewProj;` and `alignas(16) glm::mat4 biasedLightSpace;` **at the tail**
   (the struct's trailing-field comment at `elapsedTime` says appending is safe; a precombined
   `reflectedViewProj` already exists at line 114 as precedent). Mirror the fields in **every**
   GLSL UBO block that declares this struct — grep shaders for `lightSpaceMatrix` to find them
   all; std140 offsets must match the C++ layout.
2. **CPU fill** (`VulkanDevice::updateUniformBuffer`, decl `VulkanDevice.h:163`, + the reflection
   variant at `:168`): `ubo.viewProj = proj * view;` and
   `ubo.biasedLightSpace = kBiasMat * lightSpaceMatrix;` where `kBiasMat` is the same constant
   currently inlined in the shaders (0.5-scale/offset clip→UV). One multiply per frame instead of
   millions.
3. **Shaders** — replace in all 8 files from finding 1:
   `gl_Position = ubo.viewProj * vec4(worldPos, 1.0);`
   and in the 3 files from finding 2:
   `shadowCoord = ubo.biasedLightSpace * vec4(worldPos, 1.0);` (delete the local `biasMat`).
   **Caveat:** `debris.vert` / `debug_line.vert` may use push constants or a different UBO —
   read each shader's actual uniform source first; where the UBO isn't bound, reassociate
   parentheses instead: `ubo.proj * (ubo.view * vec4(...))`.
4. Hoist the `RenderCoordinator.cpp:1805` per-entity `viewProj` (finding 6).
5. Rebuild shaders: `.\build_shaders.bat` (and note `voxel.frag`'s `#include` caveat from
   CLAUDE.md if touched).

### Increment 2 — character shader fixes (findings 3, 4, 5)

1. `character.vert`: rigid normal matrix (`mat3(model)`), single `model*localPos` reused for
   clip + fragWorldPos, compute `shadowCoord` here and emit as a new varying.
2. `character.frag`: consume the varying; delete the per-fragment `biasMat` + matrix product.
3. `character_instanced.vert`: reassociate the `viewProj * model` product (finding 5).
4. **Correctness guard for finding 4:** confirm no caller ever puts non-uniform scale into
   `pushConsts.model` (grep call sites of the push-constant fill near `RenderCoordinator.cpp:1728`).
   If any path scales non-uniformly, keep `inverse-transpose` for that path only — don't ship a
   subtly wrong normal.

## Verification (MANDATORY — per CLAUDE.md; a fix is not done until run live)

These are refactors that must be **visually invisible** and **measurably not slower**:

1. **Baseline first (red side of red-before-green):** before touching anything, launch the engine
   on the furnished-tavern scene (StructGenTest project per memory, or rebuild via
   `/api/structure/build`), capture `get_render_stats` (FPS, frame ms) and reference screenshots
   (`get_visual_diagnostic` + `screenshot` from a fixed `set_camera` pose — record the pose).
   Also screenshot a **shadowed** view and a **character on screen** (increment 2 touches both).
2. Per increment: `stop_engine` → `build_shaders.bat` + `build_project` → `launch_engine` → same
   camera poses → pixel-compare screenshots (identical or within lighting-noise tolerance) and
   re-capture `get_render_stats`.
3. **Claim honestly:** report actual before/after FPS. "No visual change + N FPS delta" is the
   deliverable; if the delta is ~0 (driver already hoisted), say so — the character-shader fixes
   still stand on their own.
4. Stress angle (per the standing stress-test rule): the scaling axis is **vertex count** — verify
   on the densest available scene (furnished tavern ≈ 412k faces), not an empty world.
5. Run the solution-auditor before claiming done.

## Explicit non-goals

- Greedy-meshing sub/micro faces (parked; `docs/RenderOptimization.md` plan item 1) — this plan
  is complementary, not a substitute.
- CPU-side `glm::inverse` calls in `VoxelRaycaster.cpp`, `AnimatedVoxelCharacter.cpp` (foot IK,
  1×/frame), `DynamicFurnitureManager.cpp:677` (per shatter event) — audited, too cold to matter.
  Do not "fix" these; churn without measurable benefit.
- Hand-written SIMD intrinsics on the CPU (the articles' most aggressive prescription) — nothing
  found on the CPU side warrants it.
