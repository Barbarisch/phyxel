# Engine Advances Research Digest

> Web research sweep, 2026-07-02 — modern game-engine techniques evaluated for applicability to
> Phyxel. Companion to [`ShaderMathRedundancyPlan.md`](ShaderMathRedundancyPlan.md) (the immediately
> actionable item). Ordered by (relevance to current pain × maturity). Each entry says what it is,
> why Phyxel specifically, and the adoption cost. These are **candidates**, not commitments — each
> would need its own grounded plan + stress test before any code.
>
> **This doc is the standing home for external-tech evaluations** — items #1-#7 are the original
> 2026-07-02 render-technique sweep; later entries are dated individually and may cover any
> subsystem. Before evaluating a third-party technique/model/library, check here first: it may
> already have a verdict.

## 1. Binary greedy meshing — direct hit on the #1 open issue

> **➡ EXECUTED 2026-07-02:** grounded implementation plan written —
> [`BinaryGreedyMeshingPlan.md`](BinaryGreedyMeshingPlan.md). Key discovery: 32 provably-unused
> bits in the existing lighting words hold merged-quad extents with **zero struct widening**,
> sidestepping the failure surface of the reverted Phase 2 attempt.
>
> **➡ SHIPPED 2026-07-07 (stale by the time of this audit):** sub/microcube greedy merge landed
> and is **ON by default** — `s_fineGreedyMerge` in `engine/include/graphics/ChunkRenderManager.h`
> (used in `ChunkRenderManager.cpp`), confirmed 10-12× face reduction / 5-8× FPS recovery on
> face-bound scenes per `docs/LargeWorldScalePlan.md` §0. The "Verdict"/sequencing text below still
> frames this as a future campaign to schedule — that framing is out of date; treat it as done.

**What:** meshing sub/micro voxel faces with bitwise ops — cull 64 faces at a time with bitmasks,
merge quads via bit manipulation. Reference implementation:
[cgerikj/binary-greedy-meshing](https://github.com/cgerikj/binary-greedy-meshing) (claims ~3.9×
faster than classic greedy meshing at ~20% more triangles); see also
[Vercidium's voxel-mesh-generation](https://github.com/Vercidium/voxel-mesh-generation) and the
[Exile voxel pipeline writeup](https://thenumb.at/Voxel-Meshing-in-Exile/).

**Why Phyxel:** the #1 known issue is exactly un-merged sub/micro faces (412k faces → 49 FPS,
`docs/RenderOptimization.md`). Phase 2 greedy-merge was attempted and **parked** on the
`InstanceData` encoding constraint (bits 20–31 doubly-booked: merge extents for cubes vs grid
position for sub/micro). Binary meshing doesn't remove that encoding problem, but it makes the
*mesher* cheap enough that re-meshing on edit stops being the bottleneck argument, and the
bitmask-column representation is a natural fit for 32³ chunks (a column = one `uint32_t`).
A subcube chunk column at 3× subdivision is 96 bits = fits established 64+32 mask tricks.

**Cost:** medium-high — reopens the parked Phase 2 including the instance-encoding redesign.
**Verdict: strongest candidate; schedule as the next render-perf campaign.**

## 2. GPU-driven rendering (multi-draw indirect + compute culling)

**What:** move per-chunk draw decisions to the GPU: scene data in SSBOs, a compute pass does
frustum + occlusion culling (last frame's depth pyramid) and writes `VkDrawIndexedIndirectCommand`s;
CPU issues one `vkCmdDrawIndexedIndirect`. Canonical guides:
[vkguide GPU-driven overview](https://vkguide.dev/docs/gpudriven/gpu_driven_engines/),
[compute culling](https://vkguide.dev/docs/gpudriven/compute_culling/),
[Vulkan samples: multi-draw indirect](https://docs.vulkan.org/samples/latest/samples/performance/multi_draw_indirect/README.html),
and vkguide's newer [voxel/mesh rendering chapter](https://vkguide.dev/docs/ascendant/ascendant_geometry/).

**Why Phyxel:** chunks are a perfectly uniform unit for indirect draws, and occlusion culling is
where dense settlement scenes (many buildings occluding each other) would pay off — greedy meshing
reduces faces per chunk, GPU culling reduces chunks drawn at all. Complementary to #1.

**Cost:** high — restructures how ChunkManager feeds the renderer (per-chunk instance buffers →
one big buffer + offsets, depth pyramid pass, indirect buffer plumbing). **Partially de-risked
since this was written:** the "per-chunk instance buffers → one big buffer + offsets" half of that
restructuring already shipped as `ChunkArenaAllocator`/`ChunkArenaSystem` (`docs/RegionArenaPlan.md`,
region-keyed arena blocks with span offsets bound at draw sites) — built for the allocation-count
crash ceiling, not for indirect draws, but it leaves this item's remaining cost as depth-pyramid
occlusion + indirect-command generation + `vkCmdDrawIndexedIndirect` plumbing (still absent from the
engine; `docs/RegionArenaPlan.md` §3.3 "4.3b multidraw" is listed as future work). **Verdict: right
long-term direction; adopt after #1, starting with GPU frustum culling only (no occlusion) as a
low-risk first increment.**

## 3. AVBD — SIGGRAPH 2025 paper vs Phyxel's implementation

> **➡ EXECUTED 2026-07-02:** audit complete — [`AvbdSolverAudit.md`](AvbdSolverAudit.md).
> Verdict: Phyxel already implements genuine AVBD (primal-dual, warm-started λ/κ); no formulation
> rewrite needed. Found: iteration headroom (8 vs the paper's 3-4) and **two silent-failure
> defects** in dense piles (bodies with graph color ≥ 12 skip the primal solve; constraints past
> the 60k cap silently dropped). Ranked fix plan R1-R6 in the audit.

**What:** Augmented Vertex Block Descent — VBD + Augmented Lagrangian for hard constraints —
published at SIGGRAPH 2025 (Giles et al., Roblox/Utah):
[project page + paper](https://graphics.cs.utah.edu/research/projects/avbd/),
[Real-Time Live! demo](https://dl.acm.org/doi/10.1145/3721243.3735982), plus a readable
[WebGPU implementation](https://www.webgpu.com/showcase/webphysics-webgpu-avbd-solver/).
Headline claims: unconditional stability, stable large stacks and long articulated chains at
**a few iterations per frame**.

**Why Phyxel:** `GpuParticlePhysics` already runs XPBD/AVBD-style compute. Worth a focused
comparison of Phyxel's solver against the published formulation — especially warm-starting
(the paper's penalty/λ carry-over) and stacking stability, since debris piles and destruction
are the primary use. Potential outcome: fewer iterations for the same stability → raises the
~10k particle cap.

**Cost:** low to investigate (read paper, diff against our compute shaders), medium to adopt
changes. **Verdict: cheap high-value audit; also relevant to the CPU `VoxelDynamicsWorld` if
furniture stacking ever shows jitter.**

## 4. Radiance cascades — GI that fits voxel worlds

**What:** noiseless real-time GI storing a radiance field in hierarchical cascades of probes
(near = dense probes/few directions, far = sparse probes/many directions); cost independent of
light count and geometry complexity. Intro:
[jason.today/rc](https://jason.today/rc); 2025 advance:
[Holographic Radiance Cascades (arXiv 2505.02041)](https://arxiv.org/abs/2505.02041);
overview: [80.lv article](https://80.lv/articles/radiance-cascades-new-approach-to-calculating-global-illumination).

**Why Phyxel:** current lighting is per-corner sky+block light words (Minecraft-style flood).
Taverns/inns are interior scenes lit by point lights — exactly where flood-fill light looks
flattest and GI would visibly raise the "better than Minecraft" bar. A voxel grid gives free
ray-marching structure (DDA through chunk occupancy) for cascade interval tracing.

**Cost:** high (new lighting pipeline + storage), and interacts with the existing emissive/
point-light system. **Verdict: the most promising *visual-quality* leap; park until render-perf
(#1/#2) lands — GI on top of an unmerged 412k-face scene compounds the wrong thing.**

> **⚠️ THE PARKING CONDITION HAS EXPIRED (noted 2026-08-11).** The verdict above rests on the
> unmerged 412k-face scene, and #1 shipped: greedy meshing has been default-ON since 2026-07-07.
> Nobody revised this entry, so it has been reading as "still blocked" for a month while its own
> blocker was gone. Re-read it as: **unblocked, unscheduled.**
>
> Two things have changed the shape of the decision since it was written:
> 1. **The premise "flood-fill light looks flattest in interiors" is now only half true.** The bake
>    was leaking daylight through sub-voxel roofs and inventing daylight at chunk seams; both are
>    fixed (see `LightingPipeline.md` §2). Interiors are genuinely dark now, so the remaining
>    interior flatness is the absence of bounce and of real AO — a narrower target than before.
> 2. **A physical atmosphere now supplies sun, sky, ambient and haze**, and the scene is exposed and
>    tone-mapped. Any GI work inherits a calibrated radiance pipeline instead of having to invent
>    one, which removes a large chunk of what made this "high cost".
>
> The nearest cheaper alternatives to weigh against it first: a **dedicated per-corner AO channel**
> (fits in the 16 spare bits of `light2`/`light3`, applies to every light term) and a
> **multiple-scattering LUT** for the atmosphere, which is separately needed for the blue hour and
> is pinned by `DISABLED_TwilightZenithIsBlue_NeedsMultipleScattering`.

## 5. Render graph / frame graph for the Vulkan backend

**What:** declare render passes + resource usage as a DAG; the graph derives barriers, layout
transitions, pass order, and aliases transient memory. References:
[LegitEngine (rendergraph Vulkan framework)](https://github.com/Raikiri/LegitEngine),
[Vulkan-tutorial engine-architecture chapter](https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/Engine_Architecture/05_rendering_pipeline.html),
[a 2025 build log with VMA-backed frame graph](https://dev.to/p3ngu1nzz/advanced-vulkan-rendering-building-a-modern-frame-graph-and-memory-management-system-15kn).

**Why Phyxel:** RenderCoordinator/RenderPipeline currently sequence passes (shadow, main, mirror,
water, post) with hand-placed barriers — the mirror/reflection descriptor-pool bug and the pass
count growing (GI, occlusion pyramid would add more) are the classic symptoms that precede
adopting a graph. It's insurance against sync bugs, not a speed win.

**Cost:** high, invasive, low user-visible payoff near-term. **Verdict: not now; reconsider when
the pass count next grows (occlusion culling or GI). Keep hand-rolled barriers until then.**

## 6. Voxel ray tracing (brickmaps / Teardown-style) — the alternative future

**What:** skip meshing; trace rays through voxel data directly (DDA + brickmap or sparse-64-tree
acceleration). Teardown rasterizes each object's OBB and traces inside the fragment shader.
References: [Teardown teardown](https://juandiegomontoya.github.io/teardown_breakdown.html),
[sparse 64-tree ray tracing guide](https://dubiousconst282.github.io/2024/10/03/voxel-ray-tracing/),
[VoxelRT experiments](https://github.com/dubiousconst282/VoxelRT),
[Aokana: GPU-driven voxel framework (arXiv 2505.02017)](https://arxiv.org/pdf/2505.02017),
[fast voxel data structures survey](https://bink.eu.org/fast-voxel-datastructures/).

**Why Phyxel:** the microcube-fidelity direction (memory: lean into microcubes for quality) is on
a collision course with rasterized instancing — face count scales with subdivision³. Ray tracing
scales with *screen resolution*, not voxel count; it's how Teardown affords its density, and the
RTX 4090 target rig removes the hardware excuse. This is the "if meshing keeps losing" escape
hatch — a whole-renderer bet, plausibly prototype-able for sub/micro detail only (hybrid: rasterize
cubes, trace fine detail inside them, i.e. exactly Teardown's OBB trick applied to microcube
regions).

**Cost:** very high (parallel renderer). **Verdict (superseded 2026-07-09): promoted from "hold"
to a slated workstream — see [`RayTracingPlan.md`](RayTracingPlan.md) for the researched, phased
plan (micro-detail trace prototype → HW-RT shadows/AO → RT reflections → GI). The hybrid form
(trace only micro-detail volumes) remains the entry point (its Phase 1); greedy meshing was the
prerequisite sequencing note here — it has since shipped (`s_fineGreedyMerge`, see item #1 above),
so this item is now unblocked by that dependency (still not started itself).**

## 7. Small, opportunistic wins

- **Vertex pooling / persistent-mapped chunk buffers** ([Nick's blog](https://nickmcd.me/2021/04/04/high-performance-voxel-engine/)):
  reduce per-chunk (re)allocation churn on edits.
  **➡ SHIPPED 2026-07-18 (stale by the time of this audit):** region-keyed GPU buffer arena
  suballocation — `ChunkArenaAllocator`/`ChunkArenaSystem`
  (`engine/include/graphics/ChunkArenaAllocator.h`, `ChunkArenaSystem.h`), default ON, per
  `docs/RegionArenaPlan.md` (A0-A4 shipped): collapsed 4,693 raw per-chunk `vkAllocateMemory`
  calls down to 38 blocks at ~4k resident chunks (127×), sustaining 10,609 resident chunks / 12,054
  spans in 81 blocks past the old `maxMemoryAllocationCount` crash ceiling. The open question this
  bullet posed ("check whether ChunkManager already pools") is answered: it now does.
- **Fixed-precision instance attributes** (vkguide ascendant chapter): chunk-local coords need
  ≤6 bits/axis (10 for micro) — Phyxel's static `InstanceData` is already tight (24 B, not the 20 B
  once quoted here — a `tint` field was added since; see `engine/include/core/Types.h`), but the
  64 B `DynamicSubcubeInstanceData` likely compresses (quantized rotation, half-float scale) →
  less bandwidth on the debris path at high particle counts.

## 8. MotionBricks — generative character motion (evaluated 2026-08-10) — ❌ DO NOT ADOPT

**What:** NVIDIA GEAR lab, SIGGRAPH 2026. A generative motion model: VQ-VAE motion tokenizer +
pose model + root model, driven by **"smart primitives"** — locomotion takes
`(velocity, heading, style)`, object interaction takes **proxy keyframes**, synthesized zero-shot
with no per-task fine-tuning. Claims 15,000 FPS / 2 ms latency. Trained on ~350k production mocap
clips (BONES-SEED, [bones.studio/datasets](https://bones.studio/datasets)).
Links: [site](https://nvlabs.github.io/motionbricks/) ·
[code](https://github.com/NVlabs/GR00T-WholeBodyControl/tree/main/motionbricks) ·
[paper](https://research.nvidia.com/labs/gear/motionbricks/pdfs/motionbricks_siggraph_2026.pdf).
Code Apache 2.0; **weights under the NVIDIA Open Model License** (commercial use permitted with
attribution, subject to trustworthy-AI requirements). Runtime: Python 3.10+, PyTorch, CUDA GPU,
MuJoCo; checkpoints ~2.2 GB via Git LFS.

> ⚠️ **The marketing and the release do not match — verified, not inferred.** The site advertises
> Unreal Engine 5 and animation production. The preview release ships **only the Unitree G1 robot
> skeleton**: `motionbricks/assets/skeletons/` contains exactly one entry, `g1` (MuJoCo XML + STL),
> confirmed against the GitHub contents API on 2026-08-10. The human mocap was *retargeted onto G1*
> for training; there is **no human/SMPL/character rig in the repo**. There is also **no BVH/FBX
> export, no UE integration, no ONNX/TorchScript, and no C++ runtime** — inference is
> `python scripts/interactive_demo_g1.py` into MuJoCo, and the output is MuJoCo qpos. The README
> defers full robotics integration to a future release.
>
> Research gotchas for whoever revisits: `research.nvidia.com/labs/gear/motionbricks/` returns
> **403** to WebFetch, and the paper PDF **exceeds WebFetch's 10 MB limit**. Fetch
> `raw.githubusercontent.com/NVlabs/GR00T-WholeBodyControl/main/motionbricks/README.md` instead.

**Why Phyxel (and why it fails here):** the appeal was replacing hand-authored `.anim` clips with
generated locomotion/interaction variety for NPCs. Three blockers:

1. **Runtime integration is out.** Phyxel packages self-contained C++ games
   (`tools/package_game.py`); a PyTorch + CUDA + 2.2 GB weight dependency contradicts that model,
   contends with the Vulkan renderer for the GPU, and there is no exported graph to run from C++.
2. **Offline `.anim` authoring is reachable but not worth it yet.** It would require writing both a
   qpos→joint-rotation exporter *and* a G1→Phyxel-voxel-rig retargeter from scratch. The precedent
   for what that costs is the Quaternius fauna import that *did* work — `tools/asset_pipeline/
   extract_animation.py` + `tools/anim_pipeline/finalize_quadruped.py` → `resources/animated_characters/`.
   G1 source motion also carries robot artifacts — permanently
   bent knees, restricted ankles, no spine or finger detail — that read badly on stylized characters.
3. **Design-key tension** ([`FeatureDesignKeys.md`](FeatureDesignKeys.md)): production mocap realism
   fights the voxel aesthetic that the FSM + procedural approach
   ([`CharacterAnimationV2.md`](CharacterAnimationV2.md),
   [`LessonsLearned_ProceduralAnimation.md`](LessonsLearned_ProceduralAnimation.md)) deliberately targets.

**What IS worth taking — free, no dependency:** the **smart-primitive interface contract** as an API
shape for the NPC animation layer. `(velocity, heading, style)` for locomotion is exactly what
NavGrid + A* already produces ([`NavigationArchitecture.md`](NavigationArchitecture.md)) and is a
cleaner seam than poking FSM states directly; **proxy keyframes** for object interaction is a better
framing than the bone-constraint calibration in [`InteractionPipeline.md`](InteractionPipeline.md).

**Cost:** prohibitive as a runtime (parallel ML stack + renderer contention); medium as an offline
authoring pipeline, for output that is currently wrong-skeleton and off-aesthetic.
**Verdict: bookmark, do not build. Re-check trigger — a release shipping a human-skeleton
checkpoint AND a standard export path (BVH/FBX or ONNX).** That is the version that makes offline
clip authoring viable; until then the only actionable item is the interface-shape idea above, which
stands on its own regardless of the model.

## 9. anyCreature (ACS) — parametric creature compiler (evaluated 2026-08-21) — ✅ ADOPT THE SPEC (front half ported)

**What:** anyCreature (github.com/Ariescar/anyCreature, MIT, solo author "Ariescar") is a
zero-dependency Node.js compiler from one parametric JSON spec to a skinned, vertex-colored,
animated GLB creature, wrapped in an AI-session card workflow with two reader-agent quality gates
(context-free silhouette recognition; "punchier than last round"). The spec ("ACS") is a joint tree
resolved from relative offsets, named chains, swept superellipse volumes with uniformly-parameterized
Catmull-Rom profiles, six part types (curve/spike/membrane/fin/eye/paw), and per-joint degree
keyframe tracks with automatic contralateral mirroring (`mirror_phase`). Links: repo only (no site,
no paper). License: MIT with bundled three.js attributions; render harness needs headless Chromium,
the compiler itself needs nothing.

> ⚠️ **Reality check — verified 2026-08-21 by reading the source, not the README.** The engine is
> real and complete (`engine/core/*.js`, ~120 KB total, fetched and dissected file-by-file); the
> honest-limitations section in its README is accurate (their own example wolf ships 2 of the 3
> asked-for animations). Two gotchas for whoever revisits: `calibration/wolf_red.json` is a
> *silhouette*-calibration bad example, NOT geometrically invalid — do not assume it trips geometry
> checks; and the extracted `proportion` gate threshold (adjacent segment ratio > 0.923 blocks)
> would block their own shipped wolf (front-leg segments 0.968 equal), so that one rule cannot be
> ported as a hard gate without re-derivation. Token cost of their full card workflow is honestly
> documented at ~4.4M tokens per boss-tier creature — we did not adopt the workflow.

**Why Phyxel (and why it fits):** Phyxel's 10 fauna rigs all came from imported third-party
glTF/FBX (`tools/asset_pipeline/extract_animation.py`); there was no way to *author* a species.
The ACS front half maps almost 1:1 onto `.anim`: same Y-up/+Z-forward axes, ≤2-joint smoothstep
ring skinning that collapses cleanly to per-voxel max-weight bone assignment, and degree tracks
that sample directly into `PosKey`/`RotKey` channels. The GLB back half (uv/ao/normals/glb) is
useless here and was discarded.

**What was taken — `tools/creature_forge/` (2026-08-21):** a Python port of relative.js /
skeleton.js / geometry.js / section.js / anim.js plus the deterministic subset of checks.js,
re-targeted to a voxelizer (membership-function fill at 0.05, greedy per-(bone,color) box merge)
emitting `.anim` via `tools/anim_pipeline/anim_format.py`, finalized by
`finalize_quadruped.ensure_ground_ref` + `measure_walk_speed`. Reference specs vendored with MIT
attribution (`specs/LICENSE-anyCreature.md`); their wolf compiles as the fidelity test. First
shipped species: `forge_ibex` (31 bones, ~1.4k boxes, quadruped-morphology names, Tundra+Snow
fauna). 32-test suite in `tests/test_creature_forge.py` (red-before-green; the balance gate is
proven on a known-bad cantilever spec). NOT taken: the card workflow, the AI silhouette gates
(deferred as a possible skill on top of `orbit_screenshots`), the `coil` curve op, GLB output.

**Cost:** zero runtime dependency; authoring a species = writing one JSON spec + one
`gen_creature.py` run (seconds). The port itself was one session.

**Verdict: ADOPT-THE-SPEC — shipped.** Re-check trigger: if upstream grows spec features we lack
(new part types, IK-aware animation), diff `cards/SYNTAX.md` against `creature_forge/spec.py`.

## Suggested sequencing

1. **Now (separate session, no source conflict):** `ShaderMathRedundancyPlan.md`.
2. **Next render campaign:** binary greedy meshing for sub/micro (#1) — ✅ plan written
   (`BinaryGreedyMeshingPlan.md`), ready to execute. **Stale note: this has since SHIPPED**
   (`s_fineGreedyMerge`, ON by default 2026-07-07 — see the item #1 update above).
3. **Cheap parallel audit:** AVBD paper vs `GpuParticlePhysics` (#3) — ✅ done
   (`AvbdSolverAudit.md`); its R1 (fix two silent-failure defects) is now an actionable item.
4. **After meshing lands:** GPU frustum culling → occlusion culling (#2).
5. **Visual-quality leap, after perf headroom exists:** radiance cascades (#4).
6. **Hold:** render graph (#5) until pass count forces it.
7. **Slated (2026-07-09):** ray tracing (#6) — plan in `RayTracingPlan.md`; vegetation wind
   realism — plan in `VegetationWindPlan.md`.
8. **Rejected (2026-08-10):** MotionBricks (#8) — no work scheduled. Only the smart-primitive
   *interface shape* survives as an idea for the NPC animation layer; the model itself is
   re-checkable only on a human-skeleton + standard-export release.
9. **Adopted (2026-08-21):** anyCreature ACS spec (#9) — front half ported as
   `tools/creature_forge/` (spec → voxel `.anim` rigs); first species `forge_ibex` live as
   Tundra/Snow fauna. Possible follow-up: a silhouette-recognition gate skill on top of
   `orbit_screenshots`.
