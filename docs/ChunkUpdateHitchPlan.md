# Chunk-Update Frame Hitch — Diagnosis & Fix Plan

> **Status: PLANNED (supersedes the off-thread-meshing direction).** Written 2026-07-08 on branch
> `render-offthread-mesh`, grounded in the T0 RED measurement
> ([`docs/evidence/offthread_baseline.txt`](evidence/offthread_baseline.txt), commit 828e8c5).
> Same discipline as every plan here: **measure before assuming** (T0's lesson), red-before-green,
> evidence in `docs/evidence/`, solution-auditor on every "works" claim, each increment
> independently buildable / verifiable / revertible behind a toggle where it changes behaviour.

## 0. Why this plan replaces OffThreadMeshingPlan

T0 directly instrumented `ChunkRenderManager::rebuildAllFaces` and measured **2–4 ms typical,
13 ms worst** (densest Mountains chunk, over a 192-chunk / 2022-call dense-gen influx) — never the
~40–50 ms the off-thread plan assumed. The mesh is **not** the ≥30 ms frame hitch. The real hitches
observed were:
- **Intermittent ~40–45 ms edit stutters** (first edit after load, and occasional later edits),
- **30–48 ms frames** during a dense generation tail, and
- **568 ms & 1627 ms** frames at generation *start* (the bulk generate/whole-world job on the main
  thread — the OffThreadMeshingPlan's Increment T5 territory).

The mesh is ≤13 ms of any of those. Something else on the chunk-update path owns the rest. This plan
finds it and fixes it. **The mesh timer (T0, shipped) stays** — it's how we prove the mesh is not the
regression.

## 1. The problem (measured + code-read; NOT yet fully attributed)

The per-frame chunk-update phase — `Application.cpp:3633 chunkManager->updateDirtyChunks(...)` — is
**unprofiled** (its own comment: *"streamed worlds hitch in this block; the profiler doesn't cover
it"*). Inside it, per dirty chunk: `rebuildFaces()` (the 2–13 ms mesh, T0) **+** `updateVulkanBuffer()`
(GPU commit) **+** chunk physics-occupancy rebuild on edits. T0 profile dumps for the ~40 ms stutter
frames show `update`+`render` ≈ 0.5 ms of the 40 ms, so **~39 ms lives in this unprofiled phase, and
it is not the mesh.**

**HONESTY NOTE (correcting a T0 over-reach):** T0's evidence attributes the ~40 ms to GPU buffer
*reallocation* by correlation + code-read, NOT by direct measurement. New ground truth weakens that
guess: `DEFAULT_BUFFER_CAPACITY = 25000` instances (`ChunkRenderBuffer.h:23`), and a furnished-tavern
chunk is only a few thousand faces — **so most chunks never realloc**, and the first-edit stutter may
instead be first-time-use warm-up, the physics rebuild, or a hidden device-wait. **Increment B0
resolves this by direct instrumentation before any fix is designed.** Do not build a fix on the
realloc guess.

## 2. Ground truth (Explore audit + code read 2026-07-08 — cite before editing; lines drift)

- **Per-chunk raw allocation, no pool.** Every `ChunkRenderBuffer` owns its own `VkBuffer` +
  `VkDeviceMemory` via a raw `vkAllocateMemory` (`ChunkRenderBuffer.cpp:106,164`). 1–3 buffers per
  chunk (face always; grass + foliage only with flora — `Chunk.h:301-307`). **No VMA, no arena, no
  suballocator anywhere in engine source** — every allocation is a bare `vkAllocateMemory`.
- **`reallocateBuffer` frees inline, no deferral, no fence** (`ChunkRenderBuffer.cpp:122-172`):
  `vkUnmapMemory` → `vkDestroyBuffer` → `vkFreeMemory` → `vkCreateBuffer` → `vkAllocateMemory` →
  `vkMapMemory`, all synchronous. With `MAX_FRAMES_IN_FLIGHT = 2` (`VulkanDevice.h:451`), the freed
  buffer may still be bound as a vertex buffer in an in-flight frame → **use-after-free window + the
  likely driver stall**. Grows to `requiredInstances * 1.5` (50% headroom).
- **Default capacity 25,000 instances** (`ChunkRenderBuffer.h:23`; chosen at
  `ChunkRenderBuffer.cpp:78-80`). At `sizeof(InstanceData)=20 B` that's ~500 KB/chunk face buffer.
  Consequence: realloc only fires for chunks exceeding 25 k faces (microcube-dense) — realloc is the
  *exception*, not the rule, so it cannot alone explain a stutter seen on ordinary edits.
- **The only existing deferral is whole-`Chunk`, not buffer-granular** (`ChunkStreamingManager.cpp:
  320-353`, `m_pendingDeletion`): an evicted chunk's Vulkan teardown is deferred one streaming pump
  (pump throttled to once / 6 render frames, `Application.cpp:3644`) ≥ frames-in-flight. **The
  *pattern* (defer the handle, drain after ≥2 frames) is the template for B1; the *container* holds a
  whole `Chunk`, so a buffer-granular deferred-free queue is new code.**
- **No Vulkan-layer deferred-destroy / trash queue exists** for `VkBuffer`/`VkDeviceMemory`
  (`VulkanDevice.cpp/.h`). Only `inFlightFences` for frame pacing.
- **Allocation-count ceiling is a real latent scaling bug.** Residency defaults
  `loadDistance=256 (8 chunks)` / `unloadDistance=352 (11 chunks)` (`ChunkManager.h:99-100`), disk ×
  5 vertical bands (`ChunkStreamingManager.cpp:269`) ⇒ **~1,000–1,600 resident chunks**; × up to 3
  buffers with flora ⇒ **~3,000–4,800 allocations**, at/over the common
  `maxMemoryAllocationCount = 4096`. A larger configured `loadRadius` blows past it. Every in-flight
  realloc temporarily doubles that chunk's allocations. This can itself cause driver-side allocation
  slowdowns near the limit, and an eventual hard failure. **A suballocator (B2) fixes both the hitch
  frequency and this ceiling.**

## 2b. B0 RESULT (measured 2026-07-08 — [`docs/evidence/chunkhitch_attribution.txt`](evidence/chunkhitch_attribution.txt))

Direct per-operation timers (Release, dense 377k-face scene) attribute the hitch:
- **mesh ≤12.8 ms, steady per-frame chunk-update ≤11.8 ms** — neither is the hitch (confirms T0).
- **The hitch is a single GPU buffer alloc/realloc stall:** `buffer_create` ≤25.6 ms,
  `buffer_realloc` ≤32.2 ms — a **rare tail** (avg sub-millisecond), concentrated in allocation
  **bursts** (DB world-load; would recur on heavy streaming / buffer growth), partly first-touch.
- **Crucial disentangling:** ~99% of "STUTTER DETECTED" frames were ~32 ms **steady render cost of
  face density** (~30 FPS, the #1 issue — docs/RenderOptimization.md #40), NOT a chunk-update hitch.
  Only 12 of 4540 frames exceeded 45 ms (the alloc stall stacked on a render frame). Two 586/1604 ms
  frames were the bulk-gen job (OffThread T5). **On dense scenes the alloc hitch is largely masked by
  render density; it is most user-visible on light scenes (T0's ~40 ms flat-world edit) and load/
  streaming bursts.**

**Re-prioritisation:** the alloc hitch is real but **modest/rare** — smaller than assumed. **B2's
motive is now the strongest:** ~1–1.6k resident chunks × up to 3 raw `vkAllocateMemory` approaches
the ~4096 `maxMemoryAllocationCount` ceiling (a latent **crash/scaling bug**), and `reallocateBuffer`
frees an in-flight buffer (a real **use-after-free**). A suballocator fixes the hitch tail, the
alloc-count ceiling, and the UAF together. B1 (deferred-free) remains a cheap correctness+tail win.
The dominant dense-scene frame cost (render density) is a **separate track** this plan does not own.

## 2c. B1 RESULT (implemented + measured 2026-07-08)

Deferred-free shipped (`DeferredBufferReclaim.h`; `reallocateBuffer` hands the old buffer to a
per-frame-drained queue, retired after >MAX_FRAMES_IN_FLIGHT; toggle `s_deferBufferFree`, default ON).
Measurement reframed its value:
- **The buffer stall is ONE-TIME COLD FIRST-TOUCH at world-load, not recurring.** After load,
  generating 64 more chunks on the warmed device produced **no new stall** (create max 1.13 ms,
  realloc avg 0.49 ms). The 25–48 ms spikes are the OS/driver committing the first `VkDeviceMemory`
  pages — one-time, load-only. (Reconciles T0's "first edit after load" ~40 ms → cold first-touch.)
- **B1 is therefore a CORRECTNESS fix (closes the in-flight-buffer use-after-free), not a perf fix.**
  A deferred *free* cannot speed up a cold `vkAllocateMemory`. There is essentially no recurring
  buffer stall to remove.

**Track outcome:** T0→B0→B1 established the render-perf problem is NOT meshing, NOT steady
chunk-update, NOT a recurring buffer hitch. The one dominant recurring cost is **render density**
(B0: ~99% of stutters were the steady ~32 ms/frame cost of 377k faces — the #1 issue). **B1 is kept
as a committed correctness fix; the track pivots to render density.** B2 (suballocator) is deferred —
its remaining motive is the `maxMemoryAllocationCount` crash ceiling, not perf.

## 3. Increments (each buildable / verifiable / revertible)

### B0 — Localize the hitch by DIRECT measurement (no behaviour change; the load-bearing step)
Add scoped wall-time timers (the T0 atomic-stat pattern, exposed under `get_render_stats`) around the
distinct costs inside the dirty-chunk update, reported separately so a stutter can be attributed:
1. `ChunkRenderBuffer::createBufferRaw` (first-time chunk buffer alloc — the streaming influx cost),
2. `ChunkRenderBuffer::reallocateBuffer` (growth realloc),
3. `updateVulkanBuffer` memcpy (the in-capacity common path),
4. the per-chunk **physics-occupancy rebuild** on edits (VoxelDynamicsWorld),
5. `updateDirtyChunks` total wall time (the existing `tChunk0/tChunk1` at `Application.cpp:3632-3634`
   — surface it, don't re-derive).
Reproduce the RED cases from T0 (first edit after load; a dense-gen influx; a >25 k-face microcube
chunk forced to realloc) and record **which timer owns the ~40 ms**. Also probe for a hidden
`vkDeviceWaitIdle` / fence stall on the edit path (grep + timer). Output: `docs/evidence/
chunkhitch_attribution.txt` — the falsifiable statement "the hitch is X ms in <named call>", shown
before any fix. **The fix target is chosen by B0's numbers, not §1's guess.** If B0 shows the hitch is
NOT the buffer path (e.g. physics rebuild, or a device-wait), B1 retargets accordingly and this doc +
the T0 evidence are corrected.

### B1 — Fix the dominant cost B0 identifies (behind a toggle for A/B)
Design chosen *after* B0. The pre-loaded candidates and their fixes:
- **If growth realloc:** buffer-granular **deferred-free** — on realloc, push the old
  `{VkBuffer, VkDeviceMemory, mappedPtr}` onto a small queue drained after ≥`MAX_FRAMES_IN_FLIGHT`
  frames (mirror `m_pendingDeletion`'s one-pump discipline), so the free never stalls on in-flight
  work and the UAF window closes. Pair with a larger growth factor / smarter initial capacity to cut
  realloc frequency. (Also fixes a real correctness bug — the current inline free is UB.)
- **If first-time create during streaming:** pre-warm / recycle freed chunk buffers into a free-list
  (a chunk evicted this frame hands its 25 k buffer to the next chunk needing one — no driver alloc),
  or seed initial capacity from the chunk's actual face count to avoid the 500 KB default when small.
- **If physics-occupancy rebuild:** make the edit rebuild incremental (only the touched
  cells/columns) instead of a full-chunk grid rebuild; or budget/defer it like the mesh.
- **If a device-wait / fence stall:** remove or defer it off the edit's critical frame.
Verify: the RED stutter from B0 is gone (re-measure, Release), render output byte-identical (toggle
A/B), no new UAF/validation errors.

### B2 — Suballocator / arena (structural; kills per-chunk vkAllocateMemory + the 4096 ceiling)
Replace per-chunk `vkAllocateMemory` with sub-allocation from a few large device blocks (a minimal
custom suballocator, or vendor VMA). Chunk "buffer" becomes an offset+size into a shared block;
growth = reassign a sub-region (no driver alloc/free, no stall, no UAF); the persistent map is per
block. This removes the realloc stall class entirely AND caps device allocations at O(blocks) instead
of O(chunks×3), fixing the `maxMemoryAllocationCount` scaling bug (§2). Larger effort; gated so B1
ships the fast win first. Scope may narrow to just the chunk face/grass/foliage buffers.

### B_stress — Scale + thread-safety (MANDATORY, per the stress rule)
Scaling axes: (a) **allocation count** — generate a world large enough to push resident chunks ×
buffers toward 4096 and assert no allocation failure / no slowdown cliff; (b) **growth churn** — a
microcube-dense chunk edited repeatedly across the 25 k realloc boundary, ×1000, asserting no leak /
no UAF (validation layers ON) / bounded frame time; (c) **streaming influx** — a world larger than
the residency radius (T0b could not force eviction at 192 chunks; go bigger) flown through, measuring
first-create cost per frame. Re-measure the hitch (Release) → the shipped bar. `docs/evidence/`.

## 4. Verification summary (per CLAUDE.md — none optional)
1. **B0 attribution** — a falsifiable "hitch = X ms in <named call>", measured on the real RED case,
   shown before the fix. This is the foundation; the fix target derives from it.
2. **Toggle A/B** — the B1 fix OFF reproduces today's behaviour; ON removes the measured stutter.
3. **Render identical** — pixel/face-count compare, fix ON vs OFF, at fixed poses.
4. **Correctness** — Vulkan validation layers clean across the growth-churn stress (the current
   inline free is UB; the fix must be provably not).
5. **Release measurement** of the hitch before (B0) and after (B1/B2) — the shipped bar.
6. **Solution-auditor** before any "works"; a fix is not done until the engine runs it.

## 5. Non-goals
- Off-thread meshing (parked — the mesh is not the bottleneck; T0). Revisit only if a future
  density/feature pushes a single `rebuildAllFaces` past frame budget.
- The bulk generate/whole-world job stall (568/1627 ms at gen start) — real but separate; budget it
  per OffThreadMeshingPlan Increment T5. Can be done independently.
- GPU-driven / compute meshing.
