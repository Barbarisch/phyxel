# Region Arena Plan — Phase 4.3: shared GPU buffer blocks for chunk rendering

> **Status: PLANNED** (2026-07-18). Scoped against a full code survey (below). This is
> `LargeWorldScalePlan.md` §5.2 adjustment #4 (Sodium-style region arenas) and the direct
> successor of `ChunkUpdateHitchPlan.md` **B2** (deferred there; its motive — the
> `maxMemoryAllocationCount` ~4096 crash ceiling — is unchanged and now measured at
> **4,693 live allocations at ~4,000 resident chunks** on the 1:1 world, i.e. we sail past
> the ceiling the moment residency grows past today's typical flight). Field reference:
> Sodium's RenderRegion (8×4×8 sections, one buffer arena, one multidraw per region).

## 1. Why (measured)

- **Allocation ceiling:** every chunk takes up to 3 bare `vkAllocateMemory` calls (faces /
  grass / foliage — `ChunkRenderBuffer.cpp:164,237`). Release session 2026-07-18: 4,693 live
  at 4,017 resident. `maxMemoryAllocationCount` is commonly 4,096; the 10k+-chunk target is
  unreachable without pooling. (`GpuAllocStats.h` exists because this already crashed once.)
- **Realloc hitches:** B0 measured rare-tail `buffer_create` ≤25.6 ms / `buffer_realloc`
  ≤32.2 ms stalls (cold first-touch). Arenas replace per-chunk create/realloc with span
  assignment inside an already-touched block — the stall class disappears structurally.
- **Draw scaling (4.3b):** 5 per-chunk bind sites today; one-multidraw-per-region is the
  field's answer and the prerequisite shape for GPU-driven culling (survey §E).
- **RAM:** per-chunk `DEFAULT_BUFFER_CAPACITY = 25000` × 24 B ≈ 586 KB floor per non-empty
  chunk regardless of face count; span sizing kills the floor.

## 2. Ground truth (survey 2026-07-18 — cite before editing; lines drift)

- `ChunkRenderBuffer` = one VkBuffer + one VkDeviceMemory + persistent whole-buffer map,
  HOST_VISIBLE|COHERENT, vertex-buffer usage (`ChunkRenderBuffer.cpp:143-179`). Grow =
  `reallocateBuffer(n*1.5)` with B1 deferred-free (`DeferredBufferReclaim.h`, retire margin
  3 ticks, ticked once per frame at `Application.cpp:3006`).
- `ChunkRenderManager` owns THREE of them (faces/grass/foliage, `ChunkRenderManager.h:329-331`).
  4.4 empty-guard: buffers created only when non-empty; create-on-demand on unseal
  (`updateVulkanBuffer` `.cpp:1530-1593`).
- Draw sites (all bind binding 1 at offset 0 against the per-chunk buffer):
  main `RenderCoordinator.cpp:393` (face-dir bucketed sub-draws slice `[first, first+count)`
  as **firstInstance**), OIT `:667`, reflection `:812`, mirror `:871`, shadow `:1128`
  (36-index both-windings, never bucketed).
- Binding model: binding 0 = shared unit cube (per-vertex), binding 1 = instance buffer
  (per-instance, stride `sizeof(InstanceData)` = **24 B** — the "20 B" in older docs is
  stale; there is a DUAL struct in `VulkanDevice.h:44` that must stay byte-identical).
- `VulkanDevice::bindInstanceBufferWithOffset` (`VulkanDevice.cpp:1830`) already binds
  binding 1 at an arbitrary byte offset — dead legacy path, natural arena hook.
- Eviction contract: whole-chunk deferred teardown via `m_pendingDeletion` (one pump ≥
  frames-in-flight); buffer-level deferral via `DeferredBufferReclaim`. Both must be
  honored by span reuse.
- No VMA, no pool, no free-list anywhere in engine code today.

## 3. Design

### 3.1 ChunkArenaAllocator (new, `engine/{include,src}/graphics/`)

- Owns **blocks**: large VkBuffer+VkDeviceMemory (default **64 MB**, HOST_VISIBLE|COHERENT,
  persistently mapped once). One `vkAllocateMemory` per block → allocation count collapses
  from O(chunks×3) to O(total-bytes / 64 MB) ≈ tens.
- Hands out **spans**: `{blockId, byteOffset, byteCapacity}`; alignment 256 B (covers all
  three strides; friendly to any future device-local path). Free-list per block
  (first-fit + coalescing on free; fragmentation is bounded by span-count churn and gated
  in stress tests).
- **Span lifecycle mirrors today's contracts:** freeing a span does NOT touch Vulkan; the
  span's bytes go into a retire queue and become reusable only after the same 3-tick margin
  as `DeferredBufferReclaim` (generalized: `retireSpan(span)` + per-frame tick). Freeing a
  whole block (empty after coalesce) goes through the existing `deferBufferFree`.
- **Growth** = allocate new span, memcpy from the CPU-side vectors (they are always the
  source of truth on remesh — no GPU-GPU copy), retire old span. No realloc stall: blocks
  are pre-touched at creation (optional warm memset at block alloc, measured).
- **Region keying (for 4.3b):** spans are allocated from the block belonging to the chunk's
  spatial region `key = chunkCoord >> (3,2,3)` (8×4×8 chunks/region, Sodium's shape). A
  region chains extra blocks if 64 MB overflows (dense builds). 4.3a correctness does not
  depend on spatial grouping — it just makes 4.3b's per-region multidraw possible without
  moving spans later.

### 3.2 Wiring (4.3a — behind `s_regionArenas`, default OFF until gated)

- `ChunkRenderBuffer` grows an ARENA mode: instead of own buffer/memory, it holds a span +
  the arena's mapped base; `createBuffer/ensureBufferCapacity/updateVulkanBuffer` become
  span alloc/grow/memcpy. (Alternative — replace ChunkRenderBuffer at call sites — touches
  more code for no gain; the wrapper keeps the 4.4 empty-guard/create-on-demand semantics.)
- Draw sites: chunks draw with **zero per-chunk binds** where possible — bind the region
  block once (sorted visible list), then `firstInstance = span.byteOffset/24 + first` for
  every sub-draw (instanced attributes honor firstInstance; bucketing composes unchanged).
  Shadow/OIT/reflection/mirror take the same treatment (they already draw per chunk with
  firstInstance 0 — becomes spanBase).
- Grass/foliage buffers: same allocator, own strides (8 B / their stride), same spans —
  their pipelines bind their own binding-1 equivalents at span offsets.
- `gpualloc` stats: `live()` counts BLOCKS (the thing the ceiling cares about); new span
  stats (spans live, bytes used/capacity, fragmentation %) in get_render_stats.

### 3.3 4.3b — one multidraw per region (after 4.3a gates)

- Per-region: one `vkCmdDrawIndexedIndirect` (or emulated multidraw loop if
  `multiDrawIndirect` absent) over the region's visible chunks; per-draw chunk origin moves
  from push constants to an SSBO indexed by `gl_DrawID` (needs `shaderDrawParameters`) —
  static_voxel.vert reads origins[drawID] instead of pc.chunkBaseOffset/Abs.
- Shadow pass stays per-chunk 36-index initially (both-windings constraint); OIT/mirror
  keep 36-index too. Main pass first.
- This is also the on-ramp for GPU-driven culling (survey §E: two-phase Hi-Z writing
  drawIndexedIndirectCount) — not in 4.3 scope.

## 4. Increments (each buildable / verifiable / revertible)

- **A0 — allocator core + unit tests (red first):** span alloc/free/coalesce/alignment,
  retire-margin reuse (a span freed at tick T is not re-handed before T+3), growth,
  block chaining, fragmentation bound under churn. Pure CPU tests (headless, no Vulkan:
  block backend mocked behind a tiny interface).
- **A1 — arena mode in ChunkRenderBuffer + create/update paths** (toggle OFF): both modes
  compile; unit tests for the mode switch; headless create paths covered by existing
  chunk suite (118+).
- **A2 — draw-site conversion** (firstInstance = spanBase composition): toggle ON in a
  near-origin world (CharacterTestbed/LodTest) → **pixel-identical A/B** vs OFF at fixed
  poses (clean-diff protocol, 0.0000% target like face-dir bucketing used); then the 1:1
  world repro poses.
- **A3 — streaming lifetime:** fly the 1:1 world (Release), evict/land churn: assert no
  validation errors (PHYXEL_VALIDATION=1 run), no span reuse inside the retire margin
  (debug assert), alloc-count gate: **4,693 → ≤ 32 blocks** at 4k chunks; RAM delta
  (586 KB floors gone) measured.
- **A4 — stress (mandatory):** 10k+ chunk residency (ceiling formerly fatal), rapid
  edit-churn realloc storm, dense-build region overflow (chained blocks), teardown/world
  switch leak check (gpualloc returns to 0).
- **B (4.3b) — multidraw:** origin SSBO + gl_DrawID vert path, per-region indirect draws,
  A/B pixel-identical + draw-call count gate; separate arc, after A gates.

## 5. Non-goals

- Device-local memory / staging uploads (stay HOST_VISIBLE|COHERENT — proven fine).
- GPU-driven culling (later; 4.3b leaves the door open).
- Kinematic/character/water/VFX buffers (own pipelines, not chunk-scaled).
- Replacing the dual InstanceData struct footgun (separate cleanup; do not fork the layout).
