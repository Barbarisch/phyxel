# Off-Thread Chunk Meshing — Implementation Plan

> **Status: PLANNED.** Written 2026-07-08 on branch `render-offthread-mesh`. Goal: move per-chunk
> `ChunkRenderManager::rebuildAllFaces` off the main/game-loop thread so a chunk re-mesh no longer
> hitches the frame. Prerequisite-de-risker for **Increment 4b** (microcube cross-cube, whose bigger
> mesh cost would otherwise amplify the hitch). Same discipline as `BinaryGreedyMeshingPlan.md`:
> red-before-green, evidence in `docs/evidence/`, solution-auditor on every "works" claim, each
> increment independently buildable / verifiable / revertible behind a toggle.

## 1. The problem (measured)

Chunk meshing is **100% main-thread today** (Explore audit, 2026-07-08). A single chunk's
`rebuildAllFaces` is a monolithic CPU op (~40–50 ms; the greedy mesh + a full sky/block-light BFS
bake) that cannot be time-sliced, so it hitches the frame whenever it runs:

- **Voxel edit** (break/place): `ChunkManager::updateAfterCubeBreak/Place` → `FaceUpdateCoordinator`
  marks the chunk (+ cross-chunk neighbours) dirty; `DirtyChunkTracker::updateDirtyChunks(6ms budget)`
  meshes it next frame — but ONE chunk overshoots the 6 ms budget to ~40–50 ms in a single frame
  (adaptive back-off at `DirtyChunkTracker.cpp:103` because "a single remesh can exceed the whole
  budget"). Felt as an edit hitch.
- **Streaming** (flying into new terrain): the generation worker builds the chunk off-thread but
  **deliberately defers meshing to the main thread** (`ChunkManager.cpp:98-108`, comment records
  synchronous meshing caused "multi-second frame hitches while flying"). Each streamed chunk = one
  ~40–50 ms mesh frame.
- **Whole-world re-mesh** (`rebuildAllChunkLighting`, from `set_fine_merge`/`set_smooth_lighting` +
  world-init): re-meshes EVERY chunk with **unlimited budget, up to 4 passes** → the 230–315 ms stall
  measured in `docs/evidence/inc5_remesh_cost_release.txt`. Debug/load path, not gameplay — but a
  cheap separate win (see Increment T5).

The greedy-mesh feature added ~10 ms/chunk on top of the ~40–50 ms base (measured, same evidence file).

## 2. Ground truth (Explore audit 2026-07-08 — cite before editing; line numbers drift)

- **Per-chunk ownership:** each `Chunk` owns its `ChunkRenderManager renderManager` (`core/Chunk.h:66`).
  The mesher's state — output `faces`, baked light `m_skyLight`/`m_blockR/G/B`, scratch
  `m_solidVis`/`m_cellMat`/`m_cellDamage` — is **private per-chunk**, not shared. `rebuildAllFaces`
  (`ChunkRenderManager.cpp:133`) is pure CPU, **no Vulkan calls**.
- **GPU upload is a lock-free memcpy:** `ChunkRenderBuffer` memory is `HOST_VISIBLE|HOST_COHERENT`,
  **persistently mapped** (`ChunkRenderBuffer.cpp:113`); `updateVulkanBuffer` (`ChunkRenderManager.cpp
  :1402`) is a bare `memcpy` into the mapped pointer — no staging/command-buffer/submit. **BUT**
  `ensureBufferCapacity`→`reallocateBuffer` (`:1409`) does `vkDestroy/Free/Create/Allocate/Map`; freeing
  a GPU-in-flight buffer is a use-after-free (streaming already defers buffer frees a full pump,
  `ChunkStreamingManager.cpp:324-353`). **Realloc/free/create stay main-thread + frame-synced.**
- **Existing async infra:** `JobSystem` (`core/JobSystem.h`) — worker queue with a `backgroundWork(ctx)`
  phase + a main-thread `mainThreadFinalize(result)` phase, drained by `processCompletedJobs()`
  (`Application.cpp:3111`). Already used by `fill_region`/`clear_region`/`generate_world`/`save`;
  `fill_region.backgroundWork` **already mutates voxel data off-thread** under
  `cm->acquireWriteLock()` (`Application.cpp:14927`) — precedent for off-thread voxel work, not meshing.
  `ChunkStreamingManager` also has a generation worker + a disposal worker.
- **Dirty routing:** `DirtyChunkTracker` guards its lists with `m_dirtyMutex`; meshing lands via
  `ChunkManager::updateChunk` (`ChunkManager.cpp:343`) → `rebuildChunkFacesWithCrosschunkCulling`
  (`:389`) → `chunk.rebuildFaces()` → `rebuildAllFaces`, then `chunk.updateVulkanBuffer()`.

### The three hazards for off-thread meshing (ranked)
1. **Cross-chunk neighbour reads.** `rebuildChunkFacesWithCrosschunkCulling` (`ChunkManager.cpp:389-455`)
   passes the mesher two live closures: `getNeighborCube` reads adjacent chunks' `cubes` arrays;
   `getNeighborLight` → `neighbourChunk->bakedLightAt(...)` reads the neighbour's `m_skyLight`/`m_blockR/G/B`
   (`ChunkRenderManager.cpp:124`). Meshing chunk A off-thread while neighbour B is edited/re-meshed is a
   race. Also `getChunkAtCoord` walks the chunk map the main thread mutates on streaming insert/evict.
2. **Own voxel vectors mutated mid-mesh.** `rebuildAllFaces` reads the live `cubes`/`staticSubcubes`/
   `staticMicrocubes` vectors; interactive edits and the JobSystem fill/clear worker mutate them. Today
   the mesh runs on the main thread AFTER the edit → no overlap; off-thread introduces a reader/writer
   race unless it joins the existing `m_chunkAccessMutex` (currently taken only by fill/clear jobs).
3. **GPU buffer alloc/lifetime** — see §2. Realloc/free/create must stay main-thread + frame-synced;
   only the in-capacity memcpy is off-loadable. `needsUpdate` is a plain bool (no atomic).
- Lower risk: `MaterialRegistry::instance()` is read heavily during meshing; `add_material`/
  `remove_material` exist as runtime commands → a shared read that a concurrent add could rehash.
  The mesh toggles are `static bool` (read-only during a rebuild).

## 3. Design — snapshot + JobSystem + version-gate

Sidestep all three hazards by making the worker read ONLY an immutable snapshot, and keeping every
Vulkan/shared-state touch on the main thread:

```
MAIN THREAD (per dirty chunk, in updateDirtyChunks):
  1. bump chunk.meshVersion (atomic) — records "this is the state being meshed"
  2. build MeshInput snapshot (plain data, owned by the job):
       - copy of this chunk's voxel leaves (cubes/subcubes/microcubes) OR a stable ref held stable
         by the access lock for the snapshot's lifetime (see Increment T2 for the copy-vs-lock choice)
       - neighbour BOUNDARY snapshot: the 6 neighbours' boundary cube occupancy + bakedLightAt() for
         the boundary plane only (not whole neighbours) — taken now, on the main thread
       - the mesh toggles (smoothLighting, fineGreedyMerge, mergeTolerance) captured by value
       - worldOrigin, meshVersion
  3. JobSystem.submit(backgroundWork = mesh, mainThreadFinalize = commit)

WORKER THREAD (backgroundWork):
  - run rebuildAllFaces against the SNAPSHOT (no live neighbour closures, no MaterialRegistry writes)
    into a job-owned faces vector. Pure CPU. Never touches Vulkan or another chunk.

MAIN THREAD (mainThreadFinalize):
  - if chunk.meshVersion != snapshot.meshVersion  -> DISCARD (chunk changed since dispatch;
    it is already re-marked dirty, will re-dispatch). This is the version-gate.
  - else: ensureBufferCapacity (realloc if needed, main-thread) + memcpy faces into the mapped
    buffer + set needsUpdate. (Same code as updateVulkanBuffer, just fed the job's faces.)
```

**Why this is safe:** the worker reads only the snapshot (immutable, job-owned) → no race on voxels or
neighbours. Stale neighbour boundary → at worst a one-frame-conservative boundary that self-corrects
on the next rebuild (the code already treats chunk borders conservatively — Phase-1 culling deferral).
All Vulkan (alloc/free/memcpy) stays main-thread. Version-gate discards work made stale by a
mid-flight edit and re-dispatches. `MaterialRegistry` reads: make runtime add/remove take a lock the
mesher read-locks, OR (simpler) snapshot the needed texture indices into MeshInput at dispatch (§T3).

**Toggle:** gate the whole off-thread path behind `s_asyncMeshing` (static bool, default false until
audited), same pattern as `s_fineGreedyMerge`, so OFF = today's synchronous path (byte-identical) and
an instant A/B + revert.

## 4. Increments (each buildable / verifiable / revertible)

### T0 — Baseline + red measurement (no threading change)
Instrument `rebuildAllFaces` wall time (a scoped timer → a `GET`-able stat or log line). Measure, in a
Release build with the shipped merge ON: (a) a single-chunk voxel-edit mesh time, (b) the frame stutter
it causes (the `STUTTER DETECTED` frame). Record to `docs/evidence/offthread_baseline.txt` — the RED
"before" (edit = ~40–50 ms main-thread frame). Add a unit/bench asserting mesh time is measured.

### T1 — Snapshot-decouple the mesher (still MAIN thread; the load-bearing correctness step)
Change `rebuildAllFaces` (or a new `rebuildAllFacesFromInput(const MeshInput&)`) to read a **MeshInput
snapshot** instead of the live `getNeighborCube`/`getNeighborLight` closures + live neighbour state.
Build the snapshot on the main thread, then mesh from it — still synchronously, no worker yet.
**Red-before-green:** assert the snapshot-fed mesh produces a **byte-identical `faces` vector** to the
live-closure mesh on the tavern chunk + a chunk-seam-straddling case (the existing `FineFaceMerge`
geometry oracle style — decode emitted output, compare). This proves the snapshot captures everything
the mesh needs. This increment is where all the "what does the mesher actually read" risk lives; do it
first, on one thread, provably identical.

### T2 — Move the mesh to the JobSystem worker (behind `s_asyncMeshing`)
Dispatch T1's snapshot-mesh via `JobSystem` (`backgroundWork` = mesh into job faces; `mainThreadFinalize`
= version-gate + `updateVulkanBuffer`-equivalent memcpy/realloc). Decide copy-vs-lock for the chunk's
own voxel snapshot: start with **copy** (simplest, race-free); measure the copy cost; if it dominates,
switch to holding `m_chunkAccessMutex` for the snapshot build only. Verify at L4: same render output
(pixel-compare vs OFF at fixed poses), and the edit/stream mesh frame is now cheap (just the finalize
memcpy) — the ~40–50 ms hitch gone from the main thread.

### T3 — Version-gate + stale-neighbour + MaterialRegistry safety
Implement the `meshVersion` discard-and-redispatch; re-dispatch a chunk when a neighbour's boundary
changes (mark neighbours dirty on edit, already done by `FaceUpdateCoordinator`). Handle
`MaterialRegistry` concurrent reads (snapshot texture indices into MeshInput, or a read-lock). Unit
tests: an edit landing mid-mesh → the stale result is discarded, the chunk re-meshes to the correct
final state (no lost/torn update).

### T4 — Stress / thread-safety (MANDATORY, per the stress rule)
Scaling axis = concurrent mesh churn. Rapid edits during streaming; many chunks re-meshing at once;
edit-a-chunk-while-its-neighbour-streams. Run under a data-race check if available (TSan build, or a
targeted stress loop with `assert`s). Assert: no torn buffers, no lost updates (final state always
correct), no use-after-free on realloc (the GPU-in-flight buffer-free deferral honoured off-thread).
Re-measure the edit/stream hitch (Release) → the win. `docs/evidence/`.

### T5 (cheap, optional, parallelisable) — Budget the whole-world re-mesh
Make `rebuildAllChunkLighting` (toggle/world-init path) drain the dirty tracker with a per-frame budget
instead of unlimited/4-pass, so the 230–315 ms stall spreads across frames. Independent of T0–T4;
helps load + the debug toggles. Low risk.

## 5. Verification summary (per CLAUDE.md — none optional)
1. **T1 byte-identical** snapshot-vs-live mesh (the correctness foundation) — shown failing on a
   deliberately-incomplete snapshot first.
2. **Toggle A/B:** `s_asyncMeshing` OFF reproduces today's output byte-for-byte at any point.
3. **Pixel-compare** at fixed poses after T2 (render identical, sync vs async).
4. **Stress** (T4) with the data-race check + the "no lost update / no torn buffer / no UAF" invariants.
5. **Release measurement** of the edit/stream hitch before (T0) and after (T2/T4) — the shipped bar.
6. **Solution-auditor** before any "works"; a fix is not done until the engine runs it.

## 6. Non-goals
- Cross-chunk sub/micro **merging** (still Increment 4-series; boundaries stay conservative).
- A general job graph / multi-worker mesh pool — start with the existing single-worker `JobSystem`;
  widen only if T4 shows the worker is the bottleneck.
- Off-thread Vulkan buffer allocation — stays main-thread by design (§2/§3).
- GPU-driven / compute meshing — separate, later.
