# Ray Tracing Plan — optional RT rendering mode

**Status:** SLATED (2026-07-09), research done, not scheduled ahead of the greedy-meshing campaign
**Supersedes:** `EngineAdvancesResearch.md` §6 "hold" verdict — RT is now a planned option, not just a fallback.
**Target rig:** RTX 4090-class (removes the hardware excuse); engine is C++17 / Vulkan.

## Why

1. **The density wall.** Sub/micro faces aren't greedy-merged; one furnished tavern = ~412k faces
   → 49 FPS (`RenderOptimization.md` #40). Rasterized instancing scales with voxel count³; ray
   tracing scales with *screen resolution*. Even after greedy meshing lands, microcube-fidelity
   assets keep pushing the same wall.
2. **Visual ceiling.** Baked skylight + shadow map + mirror pass is the current lighting cap. RT
   secondary rays (shadows, AO, reflections, eventually GI) are the standard next rung.

## Research base

Deep-research sweep 2026-07-09 (7 sources, 25 extracted claims). **Caveat:** the adversarial
verify phase failed on infrastructure (rate limits), so claims below are *source-extracted, not
independently 3-vote verified* — except the Gruen paper, which was read in full, and the
Teardown/VoxelRT/64-tree sources already vetted in `EngineAdvancesResearch.md` §6.

### A. Compute-shader (software) voxel tracing

| Finding | Source |
|---|---|
| Naive flat-grid DDA ~31 Mray/s vs 148–183 Mray/s hierarchical → **multi-level empty-space skipping is mandatory** | [VoxelRT](https://github.com/dubiousconst282/VoxelRT) |
| **Tree64** (4³-branching sparse tree, 64-bit occupancy masks) ~183 Mray/s primary on an *integrated* GPU — ~2× ESVO octree (~95); ~12 B/node, O(log m) edits; author estimates 5–10× on discrete GPUs (≈1–2 Gray/s on our target class) | [VoxelRT](https://github.com/dubiousconst282/VoxelRT), [64-tree guide](https://dubiousconst282.github.io/2024/10/03/voxel-ray-tracing/) |
| **XBrickMap** (3-level brickmap + 4³ occupancy bitmasks) ~166 Mray/s primary, 125 with a diffuse bounce, **~O(1) edit cost** — best structure for *destructible* worlds in that benchmark set | [VoxelRT](https://github.com/dubiousconst282/VoxelRT) |
| Pointerless multi-level occupancy-bitmask grids ≈ octree traversal speed but no pointer chasing; **edits = one bit per layer, parallel-friendly**; per-voxel SDF traverses fastest but edits force SDF recompute | [fast voxel data structures](https://bink.eu.org/fast-voxel-datastructures/) |
| **Teardown** (shipped, fully destructible): OpenGL 3.3, *fragment-shader* tracing only — world in a 3D texture (2504×256×2504 voxels, 8 voxels/texel, >200 MB), mip levels as a dense octree; primary visibility = raster each object's OBB, then DDA inside the fragment shader | [Teardown breakdown](https://juandiegomontoya.github.io/teardown_breakdown.html) |
| **Aokana**: SVDAG + GPU-driven compute (Hi-Z tile culling, indirect dispatch, ESVO intersection) renders a ~10-billion-voxel scene in ~6 ms on an RTX 3060 Ti with ~5 % of voxel data VRAM-resident — but **no runtime edits** (SVDAG rebuild is an open problem) | [arXiv 2505.02017](https://arxiv.org/abs/2505.02017) |

**Takeaway A:** software tracing of voxels is proven at scale, including in a shipped destructible
game on 2010-era API features. For editable worlds the structure choice is
**brickmap / occupancy-bitmask hierarchy** (O(1)-ish edits), *not* SVDAG/SVO (edit-hostile,
Aokana's own limitation). Phyxel's cube→subcube(3³)→microcube(3³) is already a natural 3-level
hierarchy, and per-chunk occupancy grids already exist in `VoxelDynamicsWorld`.

### B. Hardware RT (VK_KHR_ray_tracing / ray query)

| Finding | Source |
|---|---|
| HW RT is **fastest on triangles**; AABB/procedural primitives are accelerated but slower — favors tracing our existing meshed faces over AABB-per-voxel intersection shaders | [NVIDIA RTX best practices](https://developer.nvidia.com/blog/best-practices-using-nvidia-rtx-ray-tracing/) |
| BLAS refit ≪ rebuild but degrades quality after large changes → periodic rebuild; independently moving objects get own BLASes; skip updates for culled/distant; TLAS: full rebuild each frame, PREFER_FAST_TRACE | same |
| Standard TLAS rebuilds *everything* even if one instance changed; **PTLAS** (`VK_NV_partitioned_acceleration_structure`) rebuilds only touched partitions — maps directly onto chunk regions; partition size = trace-perf vs update-cost knob | [Khronos proposal](https://github.com/KhronosGroup/Vulkan-Docs/blob/main/proposals/VK_NV_partitioned_acceleration_structure.adoc) |
| **CLAS / Mega Geometry** (OptiX 9, VK cluster ext): cluster-level acceleration primitive for dynamic dense geometry; building the top level over CLASes shrinks build input ~100× → 10–100× faster builds | [NVIDIA OptiX 9 blog](https://developer.nvidia.com/blog/fast-ray-tracing-of-dynamic-scenes-using-nvidia-optix-9-and-nvidia-rtx-mega-geometry/) |
| **Gruen et al. 2026** (read in full): tetrahedral-cage ray warping ray-traces 585 M uniquely *deforming* triangles at 60 fps / 770 MB (RX 9070 XT) by animating only the cage + rebuilding only the tet TLAS. Authors' own limits: **not for topology changes (destruction) and not for rigid objects** (plain instancing preferable). Relevant to Phyxel only if wind-deformed vegetation is ever ray-traced | [DOI 10.1145/3820014](https://dl.acm.org/doi/10.1145/3820014) |

**Takeaway B:** the HW path for Phyxel = per-chunk BLAS over the **already-meshed faces**
(triangles), rebuild-on-edit with a per-frame chunk budget (mirrors the existing dirty-queue
discipline from streaming), TLAS rebuilt per frame (cheap at chunk granularity), PTLAS/CLAS as
future upgrades. Rigid dynamic content (furniture, debris groups, character bones) = TLAS
instances, free. Vegetation stays raster (or later: tet cages).

### C. Hybrid raster + RT (the industry default)

Raster G-buffer primary + RT secondary effects, each independently togglable — reference
implementation [diharaw/hybrid-rendering](https://github.com/diharaw/hybrid-rendering) (Vulkan:
RT soft shadows, AO, reflections, DDGI probe GI). Stochastic effects need denoising
(SVGF-family / ReSTIR for many-light; DLSS-RR/FSR as vendor paths). Teardown is itself a hybrid —
raster primaries, traced secondaries.

## Architecture decision

Two viable end-states; they share almost no code, so the choice is deliberately **deferred behind
a cheap experiment** (Phase 1) rather than made on paper:

- **Path S (software voxel tracing):** brickmap/bitmask hierarchy over voxel data, compute-shader
  traversal. Wins if microcube density is the dominant concern (resolution-scaled cost, O(1)
  edits, no meshing for traced content). Cost: a parallel scene representation on GPU + a
  hand-rolled lighting/denoise stack.
- **Path H (hardware RT):** per-chunk triangle BLASes over meshed faces + ray-query effects
  layered on the existing raster pipeline. Wins for incremental visual features (shadows → AO →
  reflections → GI) with vendor denoisers/upscalers. Cost: BLAS rebuild churn under heavy
  destruction; sub/micro face counts inflate BLAS sizes until greedy meshing lands.

They compose: Teardown's model (raster/trace hybrid) and NVIDIA's guidance both permit **S for
the voxel world + H-style effect passes**, or H everywhere. Phase 1 produces the number that
decides.

## Phased plan

**Ordering constraint:** binary greedy meshing (`BinaryGreedyMeshingPlan.md`) stays the next
render campaign — it's 10× cheaper, already planned, and shrinks Path H's BLAS problem too.

### Phase 1 — micro-detail trace prototype (the go/no-go experiment)
Teardown's OBB trick applied to Phyxel's pain point: rasterize each *sub/micro-detailed cube
region* as a proxy box; the fragment (or compute) shader DDAs through that region's 3³/9³
occupancy + material data. Traced content replaces those instances' raster faces.
- Scope: new small pipeline + a per-chunk "detail volume" GPU buffer (occupancy bits + material
  indices for subdivided cubes only); debug toggle `/api/debug/rtdetail`; no lighting change
  (sample the same baked light as `static_voxel.vert`).
- **Gate (deterministic):** the tavern scene (~412k faces today). Measure FPS + face count with
  (a) current raster, (b) greedy meshing when it lands, (c) traced detail. If (c) beats (b) at
  equal visuals, Path S graduates to the strategic direction; if not, RT investment shifts to
  Path H effects only.
- Validation: L2 — CPU reference tracer vs GPU output on synthetic volumes (every subcube
  pattern); golden-image raster-vs-traced diff on a fixed scene; stress = break/rebuild churn on
  traced chunks (edit latency must stay < 1 frame — occupancy bit flips only).

### Phase 2 — hardware RT bring-up + first shippable effect
`VK_KHR_acceleration_structure` + `VK_KHR_ray_query` (query from existing passes; no full RT
pipeline yet). Per-chunk BLAS over meshed faces; dirty-chunk BLAS rebuild budget/frame; TLAS
rebuilt per frame (chunks + kinematic groups + character bones as instances; GPU debris excluded
initially). First effect: **ray-traced sun shadows + AO** (replaces shadow-map artifacts:
peter-panning, cascade seams), behind a settings toggle, SVGF-lite temporal denoise.
- Gate: ≥ 60 FPS on the tavern scene at 1440p with shadows+AO on; destruction stress (clear a
  chunk/frame for 100 frames) without hitching > 2 ms/frame in AS builds.

### Phase 3 — RT reflections replace the mirror pass
The mirror pass is a known perf risk (`project_perf_optimization`: winding-fragile, scan cost).
Ray-query reflections on Mirror/Glass/Metal materials retire an entire raster pass and generalize
it (curved/multi-bounce). Gate: reflection quality parity screenshots + net GPU-time reduction vs
the mirror pass on a mirrored scene.

### Phase 4 — GI / full RT mode (the fork)
Probe-based GI (DDGI) or radiance cascades (`EngineAdvancesResearch.md` #4) fed by whichever
tracer Phase 1–3 proved out; optional path-traced reference mode for beauty shots. Only planned
in detail after Phases 1–3 report numbers.

## Engine-specific constraints (read before implementing)

- Sub/micro = exact 1/3 and 1/9 grids → the world is intrinsically a 3-level occupancy hierarchy;
  reuse this rather than inventing brick sizes.
- Per-chunk occupancy grids already exist (`VoxelDynamicsWorld`, physics) — same data layout can
  seed the traversal structure, but the render copy must live on GPU and update via the existing
  dirty-chunk path (`ChunkRenderManager`).
- Materials: traced hits must sample the same mixed-res BC7 `sampler2DArray` via
  `MaterialRegistry::getTextureIndex(materialID, faceID)` — no separate material path.
- The dual `InstanceData` struct footgun (core/Types.h vs vulkan/VulkanDevice.h) applies to any
  new GPU struct — single source of truth or `static_assert` both.
- Vegetation (grass blades, leaf cards) stays rasterized and *excluded from RT geometry*; traced
  shadows treat it via the existing shadow map composite, or alpha-tested any-hit later. Tet
  cages (Gruen 2026) are the literature if traced deforming vegetation is ever wanted.
- Multiple engines can run at once — VRAM budget for AS/brick buffers must respect that.

## Sources

1. VoxelRT benchmarks + sparse-64-tree guide — dubiousconst282 (<https://github.com/dubiousconst282/VoxelRT>, <https://dubiousconst282.github.io/2024/10/03/voxel-ray-tracing/>)
2. Teardown renderer breakdown — juandiegomontoya (<https://juandiegomontoya.github.io/teardown_breakdown.html>)
3. Aokana GPU-driven voxel framework (<https://arxiv.org/abs/2505.02017>)
4. Fast voxel data structures survey (<https://bink.eu.org/fast-voxel-datastructures/>)
5. NVIDIA RTX ray tracing best practices (<https://developer.nvidia.com/blog/best-practices-using-nvidia-rtx-ray-tracing/>)
6. VK_NV_partitioned_acceleration_structure proposal (<https://github.com/KhronosGroup/Vulkan-Docs/blob/main/proposals/VK_NV_partitioned_acceleration_structure.adoc>)
7. OptiX 9 / RTX Mega Geometry CLAS (<https://developer.nvidia.com/blog/fast-ray-tracing-of-dynamic-scenes-using-nvidia-optix-9-and-nvidia-rtx-mega-geometry/>)
8. Gruen, Benthin, Kern, McAllister — tetrahedral-cage ray warping, PACMCGIT 9(4) 2026 (<https://dl.acm.org/doi/10.1145/3820014>) — read in full 2026-07-08
9. diharaw hybrid-rendering reference (<https://github.com/diharaw/hybrid-rendering>)
