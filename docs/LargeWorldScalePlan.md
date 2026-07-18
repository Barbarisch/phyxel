# Large-World Scale Plan — load times, save files, render distance ×100

> Goal (user, 2026-07-11): make load times, rendering performance, and large save files/worlds
> actually scale. Headline target: **increase render distance ~100× (192u → ~20km)** with playable
> frame rates, fast world loads, and save files that don't explode. Culling should be far more
> aggressive, especially at edges.
>
> Discipline: every phase has a **measured red baseline → measured green gate** (no vibes), a
> stress axis, and a validation depth per CLAUDE.md. Related docs:
> [`RenderOptimization.md`](RenderOptimization.md) (greedy-mesh campaign, shipped),
> [`ChunkUpdateHitchPlan.md`](ChunkUpdateHitchPlan.md), `AgentContext.md` (far-terrain LOD +
> async streaming arc, occlusion BFS, Phase-C downsample post-mortem).

## 0. Ground truth (surveyed 2026-07-11, working tree)

### What already works (don't redo)
- **Async streaming** (gen worker + disposal worker, two-tier remesh queue, dirty-gated evict
  saves) — zero stutter warnings in Release. `ChunkStreamingManager`.
- **Greedy meshing** — cube faces (commit `e916d1e`) AND sub/microcube faces
  (`s_fineGreedyMerge`, ON by default since 2026-07-07; 10–12× face reduction, 5–8× FPS recovery
  on face-bound scenes).
- **Chunk-level frustum + distance culling** (`RenderCoordinator::renderStaticGeometry:329`) —
  verified working.
- **Far-terrain LOD** (`FarTerrain*`, committed, **OFF by default**): heightmap tiles from
  `WorldGenerator::sampleSurface` on a worker, 3 rings (steps 2/4/8, tiles 128/256/512u) to
  2048u, LRU 512 tiles, deterministic, watertight-tested. Phase 4 (config/fog/far-plane
  auto-extension) and Phase 5 (chunk-downsample LOD) never built.
- **Occlusion BFS** (`applyOcclusionCulling:898`) — chunk visibility graph, conservative, works,
  but env-var-gated OFF and only helps enclosed spaces (caves/interiors).

### The scaling blockers (measured/verified)

**A. Save format — row-per-voxel SQLite, no compression** (`WorldStorage.cpp`)
- One row **per non-air cube** (32,768/solid chunk) with 6 INTEGER coords + flags + **material as
  repeated TEXT** (`"Grass"` copied per row, `SQLITE_TRANSIENT`). Sub/microcubes: more rows.
- No PRAGMAs anywhere: default rollback journal + `synchronous=FULL` → **fsync per chunk save**.
- `saveChunk` = DELETE-all + re-INSERT-all per chunk; delete stmts re-prepared every call.
- Redundant secondary indexes duplicate the PKs (write cost + file size for nothing).
- Consequence: an edited solid chunk ≈ **~2 MB of rows**; DB loads at **150–400 ms/chunk (Debug)**.

**B. Boot path — synchronous load-everything** (`Application.cpp:2488`, `ChunkStreamingManager::loadAllChunksFromDatabase:505`)
- Project open loads **every chunk in the DB one at a time, synchronously**, then
  `rebuildAllChunkFaces` + `initializeAllChunkVoxelMaps` + `buildAllChunkPhysics` over **all**
  chunks before first frame. A 1,000-chunk DB = minutes in Debug. No cap, no async, no
  prioritization by camera distance.

**C. In-RAM chunk = dense fat objects** (`Chunk.h:60`, `Cube.h:63-184`)
- 32,768 `unique_ptr<Cube>` slots; each `Cube` ≈ 150–170 B: `std::string materialName` (heap),
  `VoxelRigidBody*`, physics pos/rot/scale, damage, **6×12 B bonds** — per voxel, even static
  terrain. Plus up to four `unordered_map`s duplicating nodes (`ChunkVoxelManager.h:174`).
- A solid chunk ≈ 32k heap allocations (~5–6 MB+); destruction frees ~64k heap blocks
  (hundreds of ms on the Debug CRT heap — why the disposal worker exists).
- ~1,000–1,600 resident chunks at default radii → **multi-GB RAM for terrain alone**.

**D. GPU memory — 586 KB floor per chunk, raw allocations** (`ChunkRenderBuffer.h:23`)
- `DEFAULT_BUFFER_CAPACITY = 25000` × 24 B `InstanceData` = ~586 KB host-visible **mapped**
  memory per chunk even if nearly empty; bare `vkAllocateMemory` per buffer (no suballocator)
  → at 1,600 chunks × up to 3 buffers we approach the common **`maxMemoryAllocationCount=4096`
  driver ceiling** (latent crash, flagged in `ChunkUpdateHitchPlan.md`).

**E. Per-frame O(all chunks) CPU scans**
- `renderStaticGeometry` linearly scans every resident chunk every frame (fine at 1–2k, not 10k+).
- `getPerformanceStats()` iterates all chunks **every frame** (`drawFrame:1285`).
- The **shadow pass re-culls all chunks itself** (sphere test, no frustum, no reuse of
  `visibleChunkIndices`) and draws with **36 indices** per face vs the main pass's 6 —
  "138 draws vs 20 visible" per its own comment; ~20 ms floor noted.

**F. Culling gaps (the "more aggressive, especially edges" ask)**
1. **Chunk-boundary faces are always emitted** when the neighbor chunk isn't loaded
   (`neighborSolid:521` returns *exposed* on missing neighbor). At the streaming edge this
   creates a full 32×32 wall of faces per boundary chunk face — invisible (below terrain or
   facing away) but meshed, uploaded, and drawn every frame. With bigger radii the edge shell
   grows as O(radius²).
2. **Cross-chunk sub/microcube occlusion not implemented** (`ChunkRenderManager.cpp:841` "later
   phase") — sub/micro faces at chunk borders are always exposed even against a solid neighbor.
3. **No face-direction culling**: a chunk's instance buffer mixes all 6 face directions; the
   ~50% of faces pointing away from the camera are still submitted (GPU backface-culls them, but
   vertex shading + instance fetch is paid).
4. **Occlusion BFS off by default** and chunk-granularity only; no HZB/GPU-driven culling
   (`lastCulledInstances` path is dead — never written).
5. Grass radius-culls (48u) but **foliage has NO distance cull** (radius=512 "no fade",
   participates in shadow pass at any distance).

**G. Render-distance plumbing is scattered and clamped**
- Effective runtime: `maxChunkRenderDistance=192`, `chunkInclusionDistance=288`
  (`Application.h:453`), camera far plane == render distance, defaults disagree across
  `WorldInitializer` (96), `EngineConfig` (256), `RenderCoordinator` (1000), scaffolds (96).
- Shadow = **one 4096² map** clamped to `min(rd,160)` — correct for today, a wall for 20km.
- Standard (non-reversed) depth with near=0.1: far=19,200 → far-field z-fighting guaranteed.
- World-space float precision wobbles >100 km from origin (known; camera-relative rendering is
  the eventual fix).

### Benchmark world: Middle-earth 1:1 (added 2026-07-15)

A **real continental-scale world to measure these phases against**, instead of synthetic radii.
The generator drives Layer-0 from an imported heightmap (`core/MapCoarseSource` +
`WorldGenerator::setHeightmapSource`, wired via game.json `world.heightmap.dir`) — see
`tools/middle_earth/INTEGRATION.md`. Not a pre-baked DB: chunks stream and generate from the map.

- **Extent**: 24000² map px × 4 blocks/px = **96,000 × 96,000 blocks (96 km)**, surface Y 0–388.
  ~9M chunk-columns — the world is effectively infinite for streaming purposes.
- **Setup**: project `MiddleEarth1to1` (terrain dir holds `me_height_24000.u16` + meta; regenerate
  with `tools/middle_earth/import_terrain.py --downsample 1`). Spawn world (60400, 50800) =
  plains at Y24 under an eastern range whose peak (Y254) is ~1957 blocks NW at (59000, 49432).
- **Verified working**: player grounds at the map's true height, flora decorates, ~52-60 FPS at
  the default radii, 84 chunks resident. Unit-locked by `tests/core/MapCoarseSourceTest.cpp`.

#### MEASURED on this benchmark (2026-07-16, Debug, RTX-class NVIDIA, 63.9 GB RAM)

Instrumented via `graphics/GpuAllocStats.h` (live chunk-allocation counter; both free paths report)
plus an external RSS trace, ramping `world.loadRadius` on the 1:1 world.

**Blocker C — CONFIRMED and quantified. This is the real ceiling.**
| resident chunks | 512 | 1024 | 1536 | 2048 |
|---|---|---|---|---|
| process RSS | 14.2 GB | 22.9 GB | 32.7 GB | **41.3 GB** |

- Linear at **~18.1 MB per resident chunk** (Debug), on a ~5.2 GB base.
  That is **~3× worse than this plan's own "≈5–6 MB+" estimate** — re-baseline Phase 4 against it.
- **Extrapolated OOM ceiling: ~3,325 resident chunks** on a 63.9 GB machine. A 16-chunk `loadRadius`
  over tall terrain already reaches 2,048 and is still climbing; the target of "10k+ resident
  chunks" is **~180 GB** at today's per-chunk cost — i.e. unreachable without the Cube rework.

**Blocker D — DISPROVEN on this hardware (but still a real portability risk).**
- This GPU reports `maxMemoryAllocationCount=4294967295` (effectively unbounded), not the assumed
  4096. Ran to **5,888 live allocations with zero failures** — D cannot fire here.
- It remains live on AMD/Intel, where 4096 is common: at **3 bare allocations per chunk**
  (faces/grass/foliage) that is a hard **~1,365-chunk** ceiling — *below* the C ceiling above, so on
  that hardware D bites FIRST. Keep the suballocator work; just don't expect it to explain crashes here.

**`render_distance` does NOT drive residency** — `loadDistance`/`unloadDistance` (game.json
`loadRadius`/`unloadRadius`) do. Ramping render distance 192→1600 left residency pinned at ~170
chunks / 6.0 GB. Use `loadRadius` for any residency stress test.

**The two original crashes are UNREPRODUCED — cause unknown.** Earlier runs died with no exception
and no log line at `loadRadius: 4` (~170 chunks, ~6 GB). At that residency **neither C nor D can
fire**, so the "streaming volume / OOM" attribution first recorded here was WRONG and is retracted.
Re-running the same steps (render distance 1600; render distance 384 + camera teleport to the tall
forested region near 59300,49820,Y300) now survives indefinitely. Treat it as an open, likely
transient/race bug — possibly around camera teleport during streaming, or the water region recenter
(~5 ms/frame over fresh terrain) — not a volume limit. `ChunkRenderBuffer` throwing on a failed
allocation is *uncaught on the main thread* (only the gen worker has handlers), so any future
allocation failure will still `std::terminate` silently; that path now logs first.

**Why it matters**: Phase 4 is not optional for Phase 5's horizon — mid-field LOD (5.4) still needs
many chunks resident, and 18.1 MB/chunk makes "many" impossible.

> **Reference point (Minecraft):** palettized bit-packed subchunks cost **~0.5–1 B per block**
> (16³ section = palette + min-bits index array) vs our **~150–170 B per `Cube`** — a 150–300×
> gap, and the direct reason vanilla holds thousands of chunks resident at 32-chunk view distance.
> Vanilla never solved *distance* (it caps at 32 chunks + fog); the LOD-quadtree answer is the
> Distant Horizons mod — i.e. our Phase 5.4. Phase 4's material interning is a step; the endgame
> is a palettized section array with no per-voxel object.

## 1. Targets (the definition of done)

| Metric | Today | Target |
|---|---|---|
| Render distance (visible horizon) | 192u (2048u w/ manual far-terrain) | **~20,000u default-capable** (config), fog-faded |
| FPS at target distance (Release, 4090) | 146–239 @ 2048u | **≥120 @ 20km** on procedural terrain; ≥60 with a settlement in view |
| Project open (1k-chunk DB, Release) | minutes (all-chunks sync) | **< 5 s to first frame**, world streams in behind |
| Save file size (edited solid chunk) | ~2 MB rows | **≤ 20 KB** (palette+RLE blob, compressed) |
| RAM per resident solid chunk | ~5–6 MB+ | **≤ 300 KB** steady-state (palette storage), fat path only for active-physics chunks |
| Resident-chunk scalability | ~1.6k chunks, alloc-ceiling risk | 10k+ chunks without driver-limit risk |
| Boundary/edge waste | full walls at stream edge | boundary faces culled vs neighbor data or heightfield |

## 2. Phases (ordered by leverage; each independently shippable)

### Phase 1 — Storage format v2: palette blobs + SQLite tuning (load time + file size)
*The single biggest load-time and file-size lever. Pure data-layer, no render risk.*

> **Red baseline (measured 2026-07-11, `WorldStorageTest` on v1):** one fully solid
> 32³ chunk = **1,798,144-byte** DB (row-per-voxel); subcube/microcube **tint and
> state silently dropped** on save/load (no schema columns); journal mode `delete`
> (fsync per commit); no legacy migration. Red tests:
> `SubcubeTintAndStateRoundTrip`, `MicrocubeTintAndStateRoundTrip`,
> `SolidChunkDatabaseSizeUnder256KB`, `LegacyRowsMigrateToBlobsOnReopen`,
> `DatabaseUsesWALJournalMode` — all shown failing on v1 before implementation.

> **✅ SHIPPED 2026-07-11 (uncommitted).** `ChunkBlobCodec` (`engine/{include,src}/core/`)
> + `WorldStorage` rewired: blob-first load w/ legacy-row fallback, blob save
> (delete-32k-rows-and-reinsert is gone), WAL + `synchronous=NORMAL` + page/cache/
> mmap/`journal_size_limit` PRAGMAs, one-time v1→v2 migration on open (`.v1.bak`
> backup, post-VACUUM `wal_checkpoint(TRUNCATE)` so the footprint shrinks even if
> the process is killed), redundant PK-duplicate indexes dropped, blob-aware
> `getTotalCubeCount`, `deleteChunk`/`createNewWorld` clear blobs (+ latent bug
> fixes: `saveChunks` nested-BEGIN always failed; `createNewWorld` never cleared
> microcubes). **All 5 red tests green; 37/37 storage+codec tests; full suite
> 2678/2681 — the 3 failures are pre-existing/unrelated (2× AIEndToEndTest
> network-dependent, 1× ChunkStreamingManagerTest.DoubleInitReplacesPreviousStorage
> documented allocator-reuse flake, passes in isolation).**
>
> **Runtime gate (L4, live engine, Debug, SettlementTest world copy):** migration
> ran on real data — 33 chunks, ~52 s one-time (dominated by reading the old v1
> rows), backup written, then **99,483,648 → 5,214,208 bytes (19.1× smaller)**,
> WAL 0 B after checkpoint. World verified intact after migration AND after a
> restart loading purely from blobs: 33/33 chunks, 94 placed objects, 12
> structures, screenshot-identical scene. Second-boot storage init is 9 ms
> (migration no-ops); the remaining ~42 s Debug world-load is per-chunk engine
> work (32k `addCube` map inserts, Vulkan buffer creation, physics) + the
> load-ALL-chunks design — that is Phase 2/4 territory, no longer storage-bound.
>
> **Follow-ups (deliberate):** blob compression flag reserved (LZ4/zstd if
> sub/micro-heavy chunks warrant it — settlement world averaged ~158 KB/chunk
> uncompressed which is fine); mined-to-empty chunks still report "not loaded"
> (v1-parity semantics, would regenerate — pre-existing edge, fix with Phase 2);
> `deleteCube()` legacy API untouched (zero callers).

1. **Blob-per-chunk format**: serialize a chunk to one BLOB row —
   `header | palette[] (material ids) | dense 32³ palette indices (8/16-bit) | sub/micro sparse section`,
   RLE + LZ4 (or zstd) compressed. One row per chunk in a new `chunk_blobs` table.
   Expected: 32 KB raw → **2–10 KB** typical; loads become one query + one decompress
   (micro/subcube-heavy chunks stay proportional, not row-exploded).
2. **Material interning**: global `materials(id INTEGER, name TEXT)` table; ids in blobs.
   (Also step 1 of Phase 3's RAM work — intern the in-RAM `Cube::materialName` to a `uint16_t`.)
3. **PRAGMA tuning on open**: `journal_mode=WAL`, `synchronous=NORMAL`, `page_size=8192`,
   `cache_size`, `mmap_size`. WAL alone removes the fsync-per-commit stall class.
4. **Write path**: replace DELETE+reINSERT with single-row blob UPSERT; keep `saveDirtyChunks`
   batch transaction; cache the delete/upsert statements.
5. **Migration**: version tag in `world_meta`; on open of a v1 DB, migrate chunk-by-chunk to v2
   (one-time, logged, with progress), keep `.bak`. Drop the redundant secondary indexes.
6. **Red tests**: round-trip determinism (v1 world → v2 → identical voxel-by-voxel scan);
   size assertion (solid chunk blob ≤ 20 KB); timed load benchmark red-first
   (`tests/benchmark/`).

**Gate:** 1k-chunk LodTest DB: total load time and file size measured before/after; ≥10×
smaller file, ≥10× faster per-chunk load. Stress: a 10k-chunk DB (scripted flight) opens and
streams without corruption; kill-mid-save (WAL) recovers.

### Phase 2 — Boot & load path: stream-in instead of load-all (load time)

> **✅ Increment 1 SHIPPED 2026-07-11 (uncommitted).** `loadChunksNearAndDeferRest(anchor)`
> replaces `loadAllChunksFromDatabase()` at both boot sites (`WorldInitializer.cpp` +
> `Application::applyProjectSelection`): only DB chunks within `loadDistance` of the
> boot anchor load synchronously (they get the bulk face/physics passes); the rest —
> **DB-only worlds**: distance-sorted backlog drained ≤4-in-flight through the async
> worker in a new **pure DB-load mode** (no generator; a miss is dropped, never
> generated) via per-frame `pumpDeferredDbLoads()`, drain bypasses the unload-radius
> drop for backlog chunks (full residency preserved, no eviction interplay);
> **streaming worlds**: far DB chunks aren't queued at all — the pump loads them on
> approach (boot cost stops scaling with DB size). The async snapshot+finalize wiring
> moved from `configureStreamingGeneration` to constructor wiring so DB-only worlds
> get the drain finalize; the fallback flora decorator is now gated to *generated*
> (still-dirty) chunks — fixing a latent double-flora bug for DB-loaded chunks on the
> async path. `ChunkRenderManager::createVulkanBuffer` no-ops headless (unit tests
> now drive the real load paths).
>
> **Measured (Debug, MigrateTest settlement, 48 chunk records / 33 with data):**
> boot world-block went **~54 s (42 s sync loads + 12 s all-chunk face rebuild) →
> ~1 s (6 near chunks + 0.9 s face rebuild)**; editor interactive immediately;
> background fill completed in ~53 s ("Stream-in boot complete"), scene verified
> identical (33 chunks, 94 placed objects, 12 structures, screenshot match). Unit:
> `StreamInBootDefersFarChunksAndEventuallyLoadsAll` (5-chunk DB → 1 near, 4
> deferred, all eventually resident) + streaming-world variant. Red baseline = the
> measured load-all boot above.
>
> **Known gaps (follow-ups):** background fill in Debug shows 80–240 ms frames
> (finalize + remesh; was a frozen boot before) — validate Release at the phase
> gate and consider in-flight cap 4→2; boot anchor is the initial camera, not the
> game.json player spawn — a spawn far from the anchor could briefly lack collision
> (add a sync ensure-loaded when the player position is applied); `SceneManager`
> transitions (scene switches) still use the old load-all path; the WorldInitializer
> anchor is the hardcoded initial camera (50,50,50).
1. **Kill `loadAllChunksFromDatabase` on project open.** Boot loads only chunks within
   `loadDistance` of the camera/player (nearest-first, through the existing async gen/load
   worker — it already handles DB-load-else-generate). Everything else streams on demand.
   `rebuildAllChunkFaces`/`buildAllChunkPhysics` then run only on that initial set (they're
   already per-chunk; just drive them from the streamed set).
2. **Prioritized loading**: nearest + in-frustum chunks first (sort key exists at
   `loadChunksAroundPosition:289`; add a frustum bonus), so the visible bowl fills before
   behind-camera terrain.
3. **Raise worker parallelism for the boot burst**: `kMaxPendingAsync` 8 → scale with cores for
   the initial fill (SQLite reads under WAL can go on a read connection; measure first —
   Phase 1 may make single-worker loads cheap enough).
4. **Loading UX**: reuse the existing progress-overlay work (`no-frozen-engine` commit) to show
   stream-in progress instead of blocking.
5. Keep `save_world`/editor full-load available behind an explicit "load entire world" action
   (some editor workflows want everything resident).

**Gate:** LodTest + a 1k-chunk edited DB: time-to-first-frame < 5 s Release, camera can move
immediately, no missing-chunk holes in the first visible ring. Stress: teleport ±12 km
repeatedly during initial stream-in.

### Phase 3 — Culling aggression pass (render perf now, at any distance)
*All CPU-side, no format changes, each item independently A/B-able via face counts.*

> **✅ Batch A SHIPPED 2026-07-11 (uncommitted):** (a) **Occlusion BFS air-gap bug
> FIXED** — the traversal pruned at frustum-*set* membership, and air chunks are
> never in that set, so a camera ≥2 chunks above ground would have culled the whole
> world; absent/air coords are now pass-through, bounded by the view frustum +
> inclusion distance. (b) **Occlusion culling ON by default** (`PHYXEL_OCCLUSION=0`
> or `POST /api/debug/occlusion {"enabled":false}` disables); verified live at
> multiple poses on the settlement world — zero false holes, ON/OFF screenshots
> identical on open terrain (expected: chunk-granularity occlusion only wins in
> enclosed spaces, per the 2026-06-15 wall test). (c) The pre-existing
> `set_occlusion_culling` handler treated an EMPTY body as "enabled=false" — now an
> empty body is a state query (this footgun silently disabled occlusion during
> verification). (d) **Foliage radius (512u) enforced** in view + shadow passes — it
> was declared but never applied, so leaf-card cost scaled unboundedly with render
> distance; no visual change at today's 288u inclusion. (e) Item 5's
> `getPerformanceStats` cache SKIPPED deliberately: after the earlier O(1)-per-chunk
> fix it is adds-only (µs at current scale) — revisit at 10k+ resident chunks.
> Note: the 36→6-index shadow idea in item 1 is INVALID (documented in-code: the
> shadow pipeline front-culls, a one-winding quad casts nothing) — the shadow win
> comes from item 3's direction bucketing instead.

> **✅ Batch B SHIPPED 2026-07-11 (uncommitted) — face-direction bucketing, MAIN
> PASS ONLY.** `ChunkRenderManager::reorderFacesByDirection()` (counting sort at the
> end of every rebuildAllFaces) makes the instance buffer direction-major with 7
> prefix offsets (`getFaceDirRanges()`); `renderStaticGeometry` computes a 6-bit
> camera-side mask per chunk and submits only the ranges the rasterizer wouldn't
> cull (merged into ≤3 contiguous sub-draws via `drawIndexed(..., firstInstance)`),
> with a stale-range fallback to a full draw. Toggle `POST /api/debug/face_dir_cull`
> (default ON). ~45-50% fewer instances submitted in the main pass.
> **Verified pixel-exact:** clean A/B (vegetation off, day paused, held pose) =
> **0.0000% pixel diff, max 0** — after establishing the same-state temporal
> baseline is also 0.0000%. Unit: `FaceDirBucketingTest` (partition property,
> per-direction count preservation, empty chunk) + all 18 FineFaceMerge tests
> unaffected. 74/74.
>
> **Learned the hard way (adversarial diff caught it):** shadow-pass bucketing is
> STRUCTURALLY INVALID — the 36-index draw makes every instance rasterize both
> windings, so even toward-light faces write shadow depth (terrain tops are the
> main occluders at high sun). A first implementation split the shadow pass by
> light direction and produced a real 0.33%>8/255 shadow shift; reverted, with the
> reason documented at the top of `renderShadowPass`. The OIT pass is CULL_NONE
> (glass shows both sides) and the reflection pass uses a reflected eye — both
> deliberately excluded from bucketing.

1. **Shadow pass reuses the main visible set + draws 6-index quads.** Today it re-culls all
   chunks against a sphere and draws 36-index. Fix: cull shadow casters = (chunks in the
   light-extruded main frustum ∪ near set), and use the quad path (front-face cull works with
   correct winding — verify visually per the winding footgun). Expected: the "138 draws vs 20
   visible" collapse; shadow is the top GPU cost after SSAO removal.
2. **Boundary-face culling at the streaming edge ("the edges").**
   - When a neighbor chunk is *loaded*: already handled for cubes; **implement cross-chunk
     sub/micro occlusion** (extend `m_subOcc`/`m_microOcc` probes through `NeighborLookupFunc`
     at borders — the "later phase" comment at `ChunkRenderManager.cpp:841`).
   - When a neighbor chunk is *not loaded*: consult the **WorldGenerator surface heightfield**
     (cheap, deterministic, already thread-safe via the worker's snapshot): a boundary face at
     y < surfaceHeight(neighbor column) − 1 is buried → cull it. Falls back to exposed only
     where the generator can't answer (edited regions). This deletes the O(radius²) face shell
     at the stream edge.
   - On neighbor arrival the existing idle remesh tier already re-culls borders — verify it
     picks these up (it does today for cubes).
3. **Face-direction bucketing (backface elimination at draw level).** Partition each chunk's
   instance buffer into 6 contiguous ranges by faceID at mesh time (stable, free — the mesher
   already iterates per direction). At draw, issue up to 3 sub-draws for the face directions
   that can point toward the camera (dot(faceNormal, chunkToCamera) test per chunk, exact for
   axis-aligned faces). ~45–50% fewer instances submitted in every pass, including shadow
   (which uses the light direction instead). No shader change; draw-call count ×≤3 (still 1
   buffer bind) — measure, and fold into item 5's indirect batching if call count matters.
4. **Occlusion BFS on by default** (`m_occlusionCullingEnabled=true`) + wire
   `POST /api/debug/occlusion`. It's conservative (no false holes, verified) and free on open
   terrain; big underground/interior win. Add the anti-wraparound pruning experiment later.
5. **Kill the per-frame O(all-chunks) scans**: cache `getPerformanceStats()` (recompute on
   dirty/1 Hz); maintain an incremental spatial index (chunk coord grid) so
   `renderStaticGeometry` iterates only chunks within inclusion distance (query the streaming
   manager's coord map instead of the flat vector).
6. **Foliage distance cull + impostor fade**: give foliage cards the grass-style radius cull
   (config, default ~256u) — beyond it the far-terrain/LOD representation owns trees. Shadow
   pass foliage gets the same clamp.

**Gate:** face-count + draw-call + `gpu_scopes` A/B per item on: (a) LodTest flight at 2048u,
(b) 16-tavern settlement, (c) underground cave. Red-first unit tests: boundary-face cull
(buried boundary emits 0 faces; edited/unknown emits exposed), direction bucketing (Σ ranges ==
total instances; range purity), cross-chunk sub/micro (solid neighbor kills border faces).
Winding: multi-angle screenshots after the shadow quad-path change.

### Phase 4 — Chunk RAM + GPU memory scalability (large worlds resident)
1. **Intern materials** (`uint16_t` id in `Cube`, registry lookup for names) — small, safe,
   removes 32k strings/chunk. (Shared with Phase 1.)
2. **Palette-compressed static storage.** The real fix for C: static terrain chunks store
   `palette + 32³ index array` (SoA), not 32k heap `Cube`s. `Cube` objects become the
   *exception* — materialized only for voxels with active physics/damage/bonds (the current
   fields that justify fatness are physics-only). This is the largest refactor in the plan
   (touches `ChunkVoxelManager`, mesher reads, physics occupancy build) — stage it:
   (a) read-only palette mirror built at gen/load, mesher+occupancy read from it (Cube vector
   still authoritative); (b) flip authority, materialize Cubes on demand; (c) delete the four
   redundant hash maps (`cubeMap`/`voxelTypeMap` become O(1) array reads).
   Expected: solid chunk ~5–6 MB → **~70–300 KB**; chunk create/destroy cost collapses
   (the disposal worker becomes nearly idle); 10k+ resident chunks feasible.
3. **GPU suballocation**: adopt VMA (or a simple slab allocator) for chunk instance buffers;
   right-size initial capacity from the actual meshed face count (the 25k floor predates greedy
   meshing — merged chunks now need a few hundred to a few thousand instances) with geometric
   growth. Fixes both the 586 KB/chunk waste and the 4096-allocation ceiling. Also close the
   documented realloc use-after-free window (free via the existing frame-deferred graveyard).

**Gate:** RSS + `vkAllocateMemory` count at 1.6k resident chunks before/after (≥10× RAM cut);
allocation count O(1)-ish; stress = 10k resident chunks (large loadDistance, Release) with no
driver errors; full test suite green (physics/edit paths exercise Cube materialization).

#### Measured re-scope (2026-07-16 — see §0 benchmark)

**MEASURED** on the Middle-earth 1:1 world (Debug): **18.1 MB per resident chunk**, linear —
**~3× this section's own "≈5–6 MB" premise**. So "10k+ resident" is ~180 GB today, and the real
OOM ceiling is ~3,325 chunks. Re-baseline the gate against 18.1 MB, not 5–6.

**ESTIMATED composition** (from `sizeof` + Debug heap overhead; reconciles to ~17–18 MB — the
first refactor's before/after is what confirms it):
| component | est. per solid chunk | share |
|---|---|---|
| 32,768 heap `Cube`s (~176 B each + ~48 B Debug heap header) | ~7.6 MB | ~42% |
| `cubeMap` + `voxelTypeMap` nodes + buckets (~32,768 nodes each) | ~7.5 MB | ~41% |
| 3 × 586 KB host-visible **mapped** instance buffers | ~1.8 MB | ~10% |
| `unique_ptr` slot array, face/subcube vectors | ~1.2 MB | ~7% |

**Re-order by measured leverage.** The listed order buries the best item and leads with the worst:

1. ~~**FIRST: delete the redundant hash maps (was item 2c).**~~ ✅ **SHIPPED 2026-07-16.** Measured
   **18.0 → 10.5 MB/chunk (−42%)**, matching the ~41% prediction; **14.6 GB saved at 2048 resident
   chunks** (41.34 → 26.72 GB); OOM ceiling **~3,325 → ~5,670 chunks**. Full suite green (2,817
   pass / 0 fail). `cubeMap` + `voxelTypeMap` are gone; `cubeAt()` reads the dense array and
   `getVoxelType()` derives the type (the same decision `updateVoxelMaps` used to cache).
   `subcubeMap`/`microcubeMap` stay — genuinely sparse. Net: less RAM *and* less code.
   Original rationale: ~41% of per-chunk RAM — as big as the
   `Cube`s themselves — and it does **not** depend on the palette refactor. `cubes` is *already* a
   dense positional 32³ array (`cubes.resize(32*32*32)`; `index = z + y*32 + x*1024`), so
   `cubeMap` is a 32k-node hash duplicate of an O(1) array index; `voxelTypeMap` likewise derives
   from it. `ChunkStorage` carries **another** `cubeMap` copy. Blast radius is far smaller than
   "the largest refactor in the plan" implies: `getCubeMap()`/`getVoxelTypeMap()` have **zero
   external consumers** (their only reference is their own declaration), and the internal uses sit
   almost entirely in `ChunkVoxelManager.cpp` (95) + `ChunkStorage.cpp` (20), with 4 stragglers in
   `ChunkVoxelBreaker`/`VoxelManipulationSystem`. Independently shippable; red-before-green with a
   per-chunk RSS gate.
2. ~~**THEN: palette-compressed static storage (items 2a/2b).**~~ ✅ **SHIPPED 2026-07-17**
   (4.2a mirror f6234bb; 4.2b authority flip this commit). **MEASURED on the 1:1 benchmark
   (Debug, loadRadius 16, same method as the baselines): 10.5 → 5.88 MB/chunk (−44%)**, linear
   687→2,053 resident chunks; **at ~2,048 chunks RSS 16.5 GB** (was 26.7 post-4.1, 41.3
   pre-4.1); base ~4.4 GB. **OOM ceiling ~5,670 → ~10,100 chunks — the 10k+ target is reached
   on this machine.** The flip: `addCube` writes the palette store only (zero Cube allocations
   for static terrain — gen/decode included); `getCubeAt` materializes on demand (158 callers
   untouched); presence queries answer from the store; every scan site (mesher, occupancy,
   visibility mask, render flags, blob encode, roof probe, GPU-occupancy sync) does a
   drift-proof hybrid read (materialized Cube wins); `NeighborLookupFunc` + physics probes
   became store-backed bool predicates (no border-shell/probe materialization); legacy DB load
   hides voxels via a store write; Chunk moves now carry the store. Red-before-green:
   `ChunkVoxelAuthorityTest` ×10 (8 red first + 2 guards; one pinned a real 4.2a bug —
   `subdivideAt` left a stale solid store entry). Suite 2,836/2,839 (2 pre-existing network AI,
   1 skip); L4 live gate on the 1:1 world: terrain/flora correct, player grounds on store-built
   collision, hover query + place/remove round-trip, evict-save exercised, no new errors.
   **Why 5.9 and not the estimated ~2-3:** that estimate modeled a bare solid chunk. The
   remaining mass is (a) the 3 host-visible mapped GPU instance buffers (~1.8 MB floor — item 3
   below, unchanged), (b) flora **sub/microcube heap objects** (Subcube/Microcube are still fat
   per-object allocations with std::string materials — the benchmark is forested), (c) CPU
   face/instance vectors + direction-sort scratch. Next RAM levers, in measured-leverage order:
   **4.3 GPU suballocation/right-sizing** (item 3) and a possible **4.2c sub/micro
   palettization** (same recipe, sparse sections — worth a survey before Phase 5.4).
   Original scoping survey (2026-07-16) kept below for the record:

   **What static terrain actually needs from `Cube`** (usage counts outside `Cube.h`):
   | field | bytes | callers | verdict |
   |---|---|---|---|
   | `materialName` | 32 | 71 | **the payload** → palette index (u8/u16) |
   | `visible` | 1 | 46 | keep → a state bit |
   | `position` | 12 | 195 | **derivable from the array index** — never store it |
   | `bonds[6]` | 72 | ForceSystem (`breakBond` ×10, `addForceToDirection` ×3) | **physics-only** |
   | `voxelBody`/`physicsPos`/`physicsRot`/`dynamicScale`/`lifetime` | 52 | 1–13 each | physics-only |
   | `accumulatedDamage` | 4 | 2 | physics-only |
   | `broken` | 1 | read 21× — but **`setBroken(true)` has ZERO call sites**, so it is always false | dead; fold away |

   So a static voxel needs **~1–2 bytes** (palette idx + state bit), not 176 + ~48 B of Debug heap
   header. `bonds` alone is 72 B (~41% of `sizeof(Cube)`, ~2.4 MB/chunk) and is *pure physics* —
   exactly the "fields that justify fatness are physics-only" premise, now confirmed rather than
   assumed. (Careful: only the bulk `getBonds()` accessor is unused; the per-direction ones are
   live. Bonds are NOT deletable — they are materialize-on-demand.)

   **The read paths are narrow — this is what makes 2a cheap:**
   - Mesher (`ChunkRenderManager.cpp:280-295`) reads exactly `isVisible()` + `getMaterialName()`.
   - Occupancy build (`ChunkPhysicsManager.cpp:178`) reads presence.
   - `ForceSystem` is **targeted, not a scan** (`getCubeAtWorldPosition` → `chunk->getCubeAt`), so
     materialize-on-demand drops in naturally at that call.

   **Reuse the format we already ship:** `ChunkBlobCodec` (storage v2) *already* palettizes chunks
   on disk — `u16 paletteCount`, `u8/u16` idx, RLE in canonical z-minor order, `stateFlags` bits
   0-6 state / bit 7 tint — and is unit-tested (`ChunkBlobCodecTest`). Phase 4.2 is largely
   **bringing the proven disk shape into RAM**; `decode()` then builds the palette array *instead
   of* 32k `Cube`s, which also removes 32k heap allocations per chunk load (and the disposal
   worker's reason to exist).

   **Staging note:** during (a) the mirror and the `Cube` vector coexist, so RSS goes UP — do not
   gate on (a). The win lands at (b) (flip authority, materialize on demand). Gate at (b) with the
   1:1 benchmark before/after: expect **10.5 → ~2–3 MB/chunk**, taking the OOM ceiling from
   ~5,670 toward the 10k+ target.
3. **GPU suballocation (item 3).** Only ~10% of RSS *here*, but it is the hard **portability**
   ceiling: blocker D is disproven on NVIDIA (limit 4.29e9) yet remains 4096 on AMD/Intel, where
   3 allocations/chunk caps you at **~1,365 chunks — below the C ceiling**, i.e. D bites first on
   that hardware. Keep it; it is a correctness/portability item, not a RAM item.
4. **LAST: intern materials (was item 1).** Measured value is ~1 MB/chunk (~5%): `materialName`
   fits MSVC's SSO, so it costs 32 B inline per `Cube` and allocates **no** heap string. Worth
   doing *inside* the palette work, not ahead of it — "small and safe" is true, "high leverage"
   is not.

#### Phase 4.4 — Sealed/uniform chunks (stages 1–4 ✅ SHIPPED 2026-07-17; stage 5 unbuilt)

> **✅ SHIPPED + GATED (stages 1–4, same benchmark/method as 4.1/4.2):**
> **5.88 → 1.00 MB/chunk (−83%)**; at ~2,050 resident chunks **RSS 6.5 GB** (was 16.5 post-4.2b,
> 26.7 post-4.1, 41.3 pre-4.1); base ~4.45 GB → extrapolated ceiling **~59,000 resident chunks**
> on this 63.9 GB machine. **GPU allocations 3.0 → 1.03 per chunk (−66%)** — and since sealed/air
> chunks now allocate ZERO buffers, the AMD/Intel 4096 ceiling scales with *surface* chunks
> (~3,970 resident at this mix, ~3× the old ~1,365 — blocker D effectively retired for buried
> volume). Observed FPS at ~2,250 resident: 39 vs 24 on the 4.2b run (not a controlled perf gate).
> What landed: (1) uniform `ChunkVoxelStore` representation (split-on-first-non-conforming-write;
> ~64 B for uniform chunks; blob decoder one-run fast path) — `ChunkVoxelStoreUniform` ×11;
> (2) sealed classification in the managed rebuild (`ChunkManager::isChunkCapped`, O(1) against
> uniform neighbours) + mesh/bake skip + scratch release + GPU buffer EMPTY-GUARD with
> create-on-demand in updateVulkanBuffer + physics grid unregister; `faces.reserve` dropped —
> `ChunkSealedTest` ×7; (3) generator uniform deep fill (`fillAllCubes`, required or per-voxel
> addCube would split stores dense and nothing would ever seal) — `WorldGeneratorTest` ×4;
> (4) unseal lifecycle: edit-site unseal on every Chunk mutation wrapper + SYNCHRONOUS
> neighbour-unseal on boundary removals (`unsealExposedNeighbors` — the L3 shaft test caught the
> fall-through window before it shipped) — `DigShaftThroughThreeSealedBands` asserts the ground
> query at every one of 88 dig steps across two chunk seams. L4: live shaft via `clear_region`
> from surface into the sealed band below y=0 — clean state, place-back works, zero new errors.
>
> **⚠️ CORRECTION to the scoping analysis below (recorded honestly):** the "sealed+empty ≈ 1 MB
> each ⇒ surface chunks ≈ 30 MB (flora whale)" inference was WRONG. The missing mass was the
> 4.5 MB `faces.reserve` — I assumed reserve-without-touch stays out of RSS, but the **Debug CRT
> fill pattern touches every page**, so ALL chunks paid it in the Debug measurements (which is
> also why every RSS slope was so linear). Post-4.4 arithmetic: surface chunks ≈ **5.9 MB** each
> (Debug), sealed/air ≈ ~0.05 MB. Consequence: **4.2c sub/micro flora palettization is a smaller
> lever than the scoping claimed** — re-measure before prioritizing it; Release-build slopes will
> also differ (the reserve never inflated Release RSS). Stage 5 (terrain-aware vertical banding)
> remains the unlock for scaling residency with surface area instead of terrain volume.

*Original scope (2026-07-17) kept below for the record:*

*User insight driving this: "most of a chunk is completely hidden — prioritize what's actually
seen across loading, rendering, streaming." Face/chunk-level culling already exists; this is the
data-shape version of the idea: chunks that are all one thing (buried solid / pure sky) should
cost ~nothing to hold, generate, or remesh.*

**Opportunity (measured — heightmap analysis, `scratchpad/sealed_analysis.py` method, streamer
policy modeled from `ChunkStreamingManager::loadChunksAroundPosition` — radius-16 sphere,
vRadius = min(r, 2)):** of ~4,000 candidate resident chunks,

| region | sealed (buried solid) | empty (pure sky) | surface |
|---|---|---|---|
| spawn plains (60400, 50800) | **60.1%** | 23.0% | 16.9% |
| tall terrain (59300, 49820, surf Y149) | **54.3%** | 25.2% | 20.4% |

i.e. **~4 of 5 resident chunks are uniform** (all-solid or all-air). Both degenerate cases share
one representation.

**What a sealed chunk costs TODAY (surveyed post-4.2b, file:line in survey):**
- **586 KB GPU face buffer, eagerly, even for 0 faces** — `createVulkanBuffer` runs
  unconditionally per streamed chunk (`ChunkStreamingManager.cpp:192`) and
  `ChunkRenderBuffer::createBuffer` floors capacity at 25000 with no empty-guard
  (`ChunkRenderBuffer.cpp:135-137`). Pure-AIR chunks pay this too (the air-check in finalize
  only skips physics/maps, not the buffers — `ChunkManager.cpp:77`). 3 `vkAllocateMemory` per
  chunk regardless (grass/foliage floor at 1 instance = 8 B each).
- **~364 KB mesh/light scratch** (`m_skyLight`/`m_blockR/G/B`/`m_solidVis`/`m_cellMat`/
  `m_cellDamage`/border snapshot), allocated on first rebuild, never shrinks.
- **96 KB dense `ChunkVoxelStore`** (`m_idx`+`m_state` always `assign(kVoxels)`) even for one
  palette entry.
- **~8 KB physics bitsets** + a slot in `VoxelDynamicsWorld::m_grids`, which every contact/
  ground/overlap query iterates linearly (per-grid 6-compare AABB early-out, but O(all grids)).
- **Full 32k-cell mesh + bake scans on EVERY neighbour-triggered remesh** (~5 passes + flood;
  the light BFS itself does zero work but the scans run; no sealed short-circuit anywhere).
- **Generation: 32,768 `materialForColumn`+`addCube` calls per buried chunk** — no uniform fast
  path (this cost is *why* the vRadius=2 clamp exists, per the comment at
  `ChunkStreamingManager.cpp:287-293`).
- (Also found: `faces.reserve(32³·6)` = 4.5 MB commit-charge per chunk at construction
  (`ChunkRenderManager.cpp:72`) — mostly untouched pages so it hides from RSS, but drop it;
  geometric growth is fine.)

**Honest accounting:** sealed+empty ≈ 83% of chunk COUNT but only ~0.6–1.0 MB each of the
measured 5.88 MB/chunk average — which means forested SURFACE chunks carry ~30 MB each
(sub/microcube flora heap objects + faces + grown buffers). **Sealed chunks are NOT the RAM
whale — 4.2c sub/micro palettization is.** The value here is: **GPU allocation count −~80%
(retires blocker D's AMD/Intel 4096 ceiling outright)**, ~2–3 GB RAM at radius 16, worker/gen
throughput (32k-fill → O(1) for the majority class), remesh CPU (sealed chunks drop out of
every neighbour ripple), physics query list −55–60%, and the **policy unlock** below.

**Design (staged, each independently shippable, red-before-green):**
1. **Uniform store representation.** `ChunkVoxelStore` gains a uniform state
   (`{material, visible}` or air); dense `m_idx`/`m_state` allocate only on the first
   non-conforming write (the "split"). API unchanged — `solid/visible/material/solidCount`
   answer from the uniform fast path. Covers air (uniform-empty) and buried (uniform-solid).
   96 KB → ~64 B for ~83% of chunks. Red: uniform chunk approxBytes < 1 KB; split-on-write
   round-trips voxel-exactly; blob encode/decode of uniform chunks unchanged byte-for-byte.
2. **Sealed classification + short-circuits.** Sealed = uniform-solid AND every face capped
   (neighbour boundary layer solid, or generator heightfield says buried — same probe Phase 3
   item 2 needs; build once, share). For sealed/empty chunks: skip `rebuildAllFaces` entirely
   (no scratch allocation; set `m_faceConnect` = no-connections directly), **defer the face
   buffer until instances > 0** (the empty-guard also stops pure-air waste today; full
   right-sizing stays Phase 4.3), skip physics-grid registration (interior unreachable).
3. **Generator fast path.** Chunk fully below the column-min surface → one
   `store.fillUniform(deepMaterial)`; fully above column-max → skip. Removes the 32k-call fill
   that motivated the vertical clamp.
4. **Unseal lifecycle.** Triggers: an edit inside the chunk (store split handles state; the
   edit path already remeshes + rebuilds physics), or a neighbour boundary edit exposing a face
   (the existing neighbour-remesh ripple reaches the chunk; rebuild path detects "no longer
   sealed" → allocate buffers, mesh, register grid). Red/L3: dig down through a sealed chunk —
   face appears, collision materializes, character stands in the hole (no fall-through); break
   a voxel at a sealed-sealed boundary; place a structure spanning sealed chunks.
5. **Follow-up unlocked — terrain-aware vertical banding** (separate increment): replace
   `vRadius = min(r, 2)` with per-column `[minSurfaceChunk−1, maxSurfaceChunk+1] ∪ player±2`.
   Fixes the real hole where peaks >2 bands above the player inside load radius never stream,
   and is only affordable because deep stacks become sealed. This is also what Phase 5's bigger
   radii need so residency scales with *surface area*, not terrain volume.

**Gate:** re-run the 1:1 benchmark at both analysis spots: GPU allocation count (GpuAllocStats)
−≥70%; RSS slope re-measured (expect ~5.9 → ~4.5–5 MB/chunk — modest, per the honest accounting);
gen worker throughput on buried bands ≥10×; sealed chunks absent from remesh ripples (counter);
L3 unseal tests green; L4 dig-into-sealed live with evidence. Stress axes: dig a 1-voxel shaft
straight down through 3 sealed bands (unseal chain, collision at every step); teleport to the
mountain spot at radius 16 (the old crash-repro path) with terrain-aware banding on.

### Phase 5 — Render distance ×100 (the horizon)
*Builds on Phases 3–4; near field stays real chunks (~192–288u), mid field becomes downsampled
voxel LOD, far field is the heightmap far-terrain out to ~20 km.*

> **✅ Far-terrain revival + shadow-quality arc SHIPPED 2026-07-11/12 (uncommitted).** The user's
> "far terrain didn't work right" was root-caused and fixed to a live 2048u state on LodTest.
> Architecture written up in [`FarRepresentationProviders.md`](FarRepresentationProviders.md)
> (compositing layer vs provider layer; data-source axis; world-type taxonomy). What landed:
> - **Occlusion BFS was O(360ms/frame) at 2048u** (air-flood over the huge frustum → 3 FPS).
>   Fixed: near-field bound (`kOcclusionMaxDist=512`), a `kMaxOcclusionNodes` budget bail-out,
>   and an early-exit once every within-bound visible chunk is reached. 2048u steady state went
>   3 FPS → **183–225 FPS**, cmdRecord 360ms → 1.4ms.
> - **Far-vs-real-chunk z-fight (the "medium-distance shadow jitter" + "top-down seam lines" +
>   "tan bleed-through" — one root cause, three masks).** Far tiles are drawn from distance 0,
>   overlapping real chunks by design; `quantizeTop` landed them coplanar and the near ring had
>   zero bias → depth test flickered per-pixel (visible as shadow flicker because tile=flat-lit,
>   real=shadowed). Fix = **compositing depth arbiter**: far-terrain pipeline depth bias
>   (`constant 64`, `slope 2.0`) + a **geometric 0.5-voxel push-down on ALL rings**
>   (`FarTerrainMesher::kBelowSurfaceBias`; ring 1 used to get zero). The geometric gap resolves
>   at all view angles/distances where the pipeline slope-bias alone failed (flat top-down). Real
>   chunks now win every overlap; coverage-skip demoted to a pure optimization. Verified by
>   same-pose far-ON/OFF A/B (tan bleed gone) + motion. `FarTerrainMesherTest` updated to the
>   new below-surface contract (6/6 green).
> - **Far-field wall color band** (grey deep-material on every quantization step) fixed: subsurface
>   tone up to height 12, deep only for true cliffs.
> - **Near-field shadow quality** (not far-terrain — surfaced during this arc, long-standing):
>   (a) **comb/striped shadows** = the shadow pass front-culled assuming "voxel geometry is
>   closed," but face-culled meshes have no buried faces, so terrain TOPS (the real occluders)
>   never entered the map → only sparse edge-on wall quads wrote depth → combs. Fixed: **CULL_NONE
>   casters** (this was also the old "no crisp shadows on streaming worlds" mystery). (b)
>   **shadow crawl while moving** = the fitted map followed the camera continuously → sub-texel
>   shimmer. Fixed: **world-anchored texel snapping** (a first snap in a center-relative frame was
>   a proven no-op; corrected to a world-anchored light-rotation frame). (c) **crest-band acne** =
>   contested-texel self-shadow at drop edges. Fixed: **normal-offset shadow sampling** (~2 texels
>   along the surface normal, `static_voxel.vert`). All verified at the user's exact poses.
> - **Confirmed NOT bugs** (deterministic sun-swing / noon tests): short hard-edged shadows from a
>   high sun (real, moves with sun); cube-aligned darkening near walls = **baked skylight/AO**
>   (per-voxel, static — persists at noon when cast shadows vanish), not a shadow.
>
> **Still OFF by default** — far terrain + 2048u are debug-route only; the items below (game.json
> config, far-plane auto-extend, fog, reversed-Z) are what flips it default-on. **Logged fidelity
> gaps (optional, not regressions):** grass blades don't receive shadows / match ground color;
> baked AO steps hard at cube edges; the 0.5-voxel push-down adds a tiny lip at the exact LOD
> boundary (fog will hide it).

1. **Far-terrain ring extension**: add coarser rings (steps 16/32/64 → tiles 1024/2048/4096u)
   with the same doubling band scheme out to 20 km. Tile math: annulus to 19.2 km at 512u tiles
   would need ~4.4k tiles (blows the 512 LRU); with 2048–4096u outer tiles it's **a few hundred
   total** — keep `maxResidentTiles=512` and assert the wanted-set fits per ring config.
   Quantization coarsens with step (already the design); horizon reads as distant hills.
2. **Phase-4-of-far-terrain items, now required**: `game.json` `world.farTerrain` block +
   editor default-on for streaming worlds; **automatic far-plane extension** (camera far =
   max(renderDistance, farTerrain.maxDistance)); **distance fog** fading the last ring into the
   sky color (also hides tile pop-in and LOD seams).
3. **Reversed-Z depth** (D32F is already the depth format): near=0.1/far=20,000 needs it;
   standard depth will z-fight the mid/far field. Touches projection matrix, depth compare ops,
   clear values — isolated, well-trodden change; verify with the far/near overlap scenes
   (far tiles under-lap real chunks by design and rely on depth winning correctly).
4. **Chunk-downsample voxel LOD for the mid field (revive Phase 5/Phase C, fixed).** Real/edited
   chunks and *structures* beyond ~288u down to 1/2–1/8 resolution using the variable-size-face
   format (sound foundation per the post-mortem; the old failure was the non-watertight coarse
   mesher — the new far-terrain mesher's watertight-wall discipline + occupancy-OR downsample is
   the retry recipe, validated up close + wireframe + proper render distance per the recorded
   lesson). This is what makes *settlements* visible past the real-chunk bowl — far-terrain only
   knows generator terrain, not edits/buildings.
   Order note: ship far-terrain extension (items 1–3) first; mid-field LOD is the long pole and
   can land after.
5. **Cascaded shadow maps** (2–3 cascades): keep today's quality inside 160u (cascade 0 ≈
   current fitted map), add 1–2 coarse cascades to a few km, far terrain caster-only in the last
   cascade or skipped + fog. The `s_shadowFrustumCull` hook was explicitly kept for this.
6. **Defer (documented, not in scope):** camera-relative rendering (only matters >100 km from
   origin), far-field water/flora representation, generator-driven far *structures* (settlement
   silhouettes at 10 km).

**Gate:** scripted 20 km-horizon flight (Release, LodTest recipe): ≥120 FPS steady, zero
stutter warnings, no z-fighting at mid/far overlap (screenshot sweep), shadow quality unchanged
inside 160u, horizon fog-faded. Stress axes: teleport ±50 km; spin-in-place at 20 km distance
(tile churn); a settlement at 500u visible via mid-field LOD.

### Phase 6 — Standing perf gates (keep it fixed)
- Add a `tests/benchmark/` scene matrix (empty / LodTest flight / 16-tavern / cave) asserting
  face counts + load times against recorded budgets, run via `build_and_test.ps1 -BenchmarkOnly`.
- `/engine-perf` skill runs per phase close; numbers appended to this doc.
- Wire `get_render_stats` gaps: real shadow draw/instance counts in the JSON, remove the dead
  `lastCulledInstances` fields or make them truthful.

## 3. Sequencing & effort

| Phase | Effort | Risk | Unlocks |
|---|---|---|---|
| 1 Storage v2 | M | Low (data layer, red-tested round-trip) | 10×+ smaller saves, 10×+ faster loads |
| 2 Stream-in boot | S–M | Low (reuses async worker) | <5 s project open |
| 3 Culling pass | M | Med (shadow winding; else low) | Big frame wins now; edge waste gone |
| 4 RAM/GPU memory | L (staged) | Med–High (core data structure) | 10k+ resident chunks |
| 5 Distance ×100 | L | Med (reversed-Z isolated; mid-LOD is the long pole) | The 20 km horizon |
| 6 Perf gates | S | Low | Regression-proofing |

Recommended order: **1 → 2 → 3 → 5(items 1–3) → 4 → 5(item 4–5) → 6 rolling**. Phases 1–3 are
independent and could be parallel workstreams; Phase 5 items 1–3 (far-terrain to 20 km + fog +
reversed-Z) deliver the visible "100× horizon" early, with mid-field LOD (5.4) and the RAM
rework (4) making it *dense* and *big-world-resident* afterwards.

## 4. Open questions (decide before Phase 4/5)
- Palette storage: keep `Cube` API surface (materialize-on-write) vs break the API — audit how
  many systems mutate `Cube` directly.
- zstd vs LZ4 for blobs (zstd smaller, LZ4 simpler/faster; either vendored under `external/`).
- Mid-field LOD ownership: extend `FarTerrainManager` rings inward with voxel-aware tiles vs a
  separate `ChunkLODController` (the approved-but-unbuilt Phase 5 plan used the latter).
- Whether foliage impostors are needed at mid distance or fog + far-terrain coloring suffices.

## 5. Addendum — external survey: how the field solves this (2026-07-17)

*Provenance: web/GitHub research (three parallel research agents) over open-source voxel
engines, published papers, and first-party devlogs; all claims cite a repo/doc/paper URL.
Written against the post-4.4 state (1.00 MB/chunk, ~59k-chunk ceiling). Purpose: validate or
correct this plan against what shipping engines actually do, and record the reference
implementations to copy from.*

### 5.1 The convergent findings

**A. Nobody achieves huge render distance by pushing full-res chunks farther.** Every project
that got "see forever" uses a second, coarser representation. Three proven patterns:
1. **Octree-native world** — the storage hierarchy IS the LOD; distant regions render from
   coarser tree levels directly. Bonsai (billion-block worlds, "view distance = the entire
   world" at "stable, linear cost", GPU-side generation so coarse levels are produced natively:
   <https://github.com/scallyw4g/bonsai>), Cubiquity (SVDAG whose coarser levels are the
   distant instanced cubes: <https://github.com/DavidWilliams81/cubiquity>), VoxelPlugin.
2. **Persisted column LOD in a quadtree** — Distant Horizons (the best-known Minecraft
   answer): per-XZ columns with a few vertical samples, quadtree of detail levels, stored as a
   *first-class artifact* in its own SQLite DB with its own compression policy (LZMA — 3×
   smaller than LZ4; optional lossy mode — LODs are write-rarely/read-often), rendered as a
   **separate pass behind the near field with separate depth handling** (their wiki explicitly:
   the separation exists to avoid z-fighting). 256–1024+ chunk distances in 2–4 GB.
   <https://gitlab.com/distant-horizons-team/distant-horizons>
3. **Generator-coarse layer** — Veloren: far terrain is a heightmap from the world
   *generator's* coarse model (never from downsampling real chunks) + horizon maps for distant
   shadows + server-sent *positions/kinds* of distant trees rendered as cheap instanced
   markers. <https://veloren.net/blog/devblog-171/>

Phase 5's near/mid/far split is exactly the Veloren+DH hybrid — **the plan's shape is
independently validated**. One field-wide correction: **the generator must emit coarse data
directly; downsampling fully-generated fine chunks for far LOD is the trap** (Bonsai moved gen
to GPU partly for this; DH's hardest perf work was avoiding double-generation; Veloren never
touches real chunks for LOD). Our far terrain already samples the generator — correct. Reserve
chunk-downsampling (5.4) for the only content that needs it: *edited* chunks and structures.

**B. The luanti cautionary tale confirms LOD is the only bounded answer.** Luanti/Minetest has
good storage (per-block name→id palettes), mesh chunking (up to 64³ merged meshes), BFS +
raytraced occlusion — and **no shipped LOD tier ever** (celeron55's 2016 "far map" branch died:
<https://github.com/minetest/minetest/pull/3502>). Its draw range hard-caps regardless.
Everything except LOD is a constant factor.

**C. Our ternary sub-voxel scheme (1/3, 1/9) has no precedent — and its face-meshed rendering
is the documented failure mode elsewhere.** No shipping engine uses 3ⁿ nesting (power-of-two
everywhere: shifts not divides, 64-bit-friendly child masks, dedup literature). The closest
analogs are Minecraft's sub-block mods, and their history is the 412k-face tavern in
miniature:
- **Chisels & Bits** (16³ bits/block, meshed to faces like us): issue tracker documents
  detail-heavy chisel work dropping to ~0 FPS, meshing cost scaling with bit count, per-block
  meshing never scaling to whole detailed buildings
  (<https://github.com/ChiselsAndBits/Chisels-and-Bits/issues/543>, /issues/886).
- **LittleTiles** survives by storing **boxes, not voxels** — implicit greedy-merge at
  authoring time; cost ∝ shapes, not cells. (Storage twin of our fine-face greedy merge.)
- Every engine with truly tiny voxels — Teardown, John Lin, Ethan Gore, Avoyd, DouglasDwyer's
  Octo — **abandoned face geometry for volume storage + per-pixel ray traversal**. Octo's
  changelog literally documents outgrowing greedy-meshing-with-LODs and switching to compute
  ray marching (<https://github.com/DouglasDwyer/octo-release>).

The strongest pattern-match for our microcube density problem is **Teardown's hybrid**:
rasterize only each detail region's *bounding box*, then DDA-march the region's palette volume
in the fragment shader (volume texture at 1 B/voxel + 256-entry palette; its own mip chain =
free octree for empty-space skipping; ships on OpenGL 3.3, no RT hardware, no compute).
A furnished tavern becomes a handful of box draws; "face count" stops being a cost axis for
detail. Breakdowns: <https://juandiegomontoya.github.io/teardown_breakdown.html>,
<https://acko.net/blog/teardown-frame-teardown/>. Note the convergence from the other
direction too: Ethan Gore (renders the full 4B³ range) found it *faster to rasterize primary
visibility and ray-trace only shadows/GI* (<https://news.ycombinator.com/item?id=46286930>) —
hybrid is the consensus from both camps. This directly endorses `RayTracingPlan.md`'s
"micro-detail trace prototype first" ordering.

**D. Memory reference points (context for Phase 4's numbers).** Minecraft palettized sections
≈ **0.5–1 B/voxel** (<https://minecraft.wiki/w/Chunk_format>); Teardown **1 B/voxel at 10 cm
resolution**; Veloren "a few hundred KB per chunk" via implicit defaults + subchunk dedup;
SVO-DAGs **0.05–0.6 B/voxel** (Kämpe 2013, EpicCitadel 128K³ in 945 MB:
<https://dl.acm.org/doi/10.1145/2461912.2462024>). The proven compression ladder beyond our
current palette store, with published multipliers:
1. **Implicit most-common-default** per chunk/subchunk — Veloren measured **~7× RAM** from
   storing the dominant block implicitly (<https://veloren.net/blog/devblog-117/>). Phase 4.4's
   uniform store is the degenerate (whole-chunk) case of this; the per-subchunk/default-block
   generalization is the remaining headroom for *surface* chunks.
2. **RLE/interval ordering** for homogeneous runs — ≈SVO compression ratios, far simpler
   (survey: <https://eisenwave.github.io/voxel-compression-docs/>). Already in the blob codec;
   candidate for in-RAM sparse sections.
3. **Copy-on-write dedup of repeated subtrees** — Avoyd's ref-counted copy-on-modify DAG,
   ~4× on their data, the proven *editable* dedup
   (<https://www.enkisoftware.com/devlogpost-20230823-1-Implementing-a-GPU-Voxel-Octree-Path-Tracer>).
   High-value here specifically: settlements place the same furniture template hundreds of
   times. Editable-DAG literature: HashDAG <https://github.com/Phyronnaz/HashDAG>.

**E. The rasterizer scaling recipe is settled** (Sodium, Vercidium, vkguide's Ascendant, and
the Aokana paper — SIGGRAPH 2025, the best end-to-end voxel reference:
<https://arxiv.org/abs/2505.02017>):
- **Region-grouped buffers**: Sodium groups 8×4×8 sections into one `RenderRegion` sharing a
  buffer allocation, one multidraw per region
  (<https://deepwiki.com/CaffeineMC/sodium/3.1-chunk-rendering-pipeline>). This is the field's
  answer to both our per-chunk-buffer model (blocker D remainder + Phase 4.3) and draw-count
  scaling — one arena per chunk-region, suballocated.
- **Shared 6-index buffer + vertex pulling**: packed quads at **8 bytes** (u32 packed
  pos/size + u32 type — the cgerikj/Ethan Gore format) vs our 20 B `InstanceData`; corners
  reconstructed from `gl_VertexIndex` (<https://voxel.wiki/wiki/vertex-pulling/>). Our open
  "6-index draw" perf item is exactly this.
- **Binary greedy meshing is now ~free**: 64-bit column masks, cull 64 faces per bitwise op,
  **50–200 µs per 62³ chunk** (avg 74 µs) — cheap enough to run at streaming time, and our
  occupancy grids already exist as input (<https://github.com/cgerikj/binary-greedy-meshing>).
  Counterpoint for destruction-heavy scenes: Vercidium ships a *run-merge* instead — "~20%
  more triangles than greedy, ~390% faster" — because constant destruction forces near-per-
  frame remesh; also 4 B/vertex packing and neighbor-pointer caching as the top boundary-
  lookup win (<https://vercidium.com/blog/voxel-world-optimisations/>).
- **GPU-driven submission for 10k+ chunks**: compute frustum + **two-phase Hi-Z occlusion**
  (draw last frame's visible set, build Hi-Z, re-test the culled remainder — no readback, no
  popping) writing `vkCmdDrawIndexedIndirectCount`. Portable Vulkan 1.2, no mesh shaders.
  This is what retires the per-frame O(all-chunks) CPU scans (blocker E) at scale. Voxel-
  specific reference: Aokana (above); Vulkan walkthrough:
  <https://vkguide.dev/docs/ascendant/ascendant_geometry/>; explainer:
  <https://medium.com/@mil_kru/two-pass-occlusion-culling-4100edcad501>. Nvidium's
  mesh-shader variant gets ~10× in dense Minecraft scenes but is NVIDIA-only
  (<https://github.com/MCRcortex/nvidium>) — optional later backend, not the path.
- Our occlusion BFS is the same family as Sodium's/Luanti's camera-BFS graph cull — the
  field's verdict is it's the right cheap CPU-side complement. Keep it.

**F. Smaller validations.**
- **SQLite is a validated backend** — Luanti's default (blob per 16³ block, zstd:
  <https://docs.luanti.org/for-server-hosts/database-backends/>) and DH's LOD store. No
  LMDB/LevelDB move is justified; Bedrock's LevelDB precedent argues only for *smaller write
  granularity* (per-subchunk delta keys), adaptable inside SQLite if save churn ever bites.
- **Seams**: cubic-voxel projects don't stitch LOD boundaries — they render the far
  representation as a separate shell behind near geometry and let depth win (DH, Veloren).
  Our compositing depth-arbiter (bias + 0.5-voxel push-down) is the same philosophy. The
  crack-free-by-construction alternative, if per-chunk mid-LOD ever needs it, is Lysenko's
  POP-buffer/geomorph method for blocky voxels
  (<https://0fps.net/2018/03/03/a-level-of-detail-method-for-blocky-voxels/>).
- **LOD as a storage dimension**: godot_voxel keys every saved block by (x, y, z, **lod**) in
  its SQLite stream and treats (position, lod) as the streaming unit
  (<https://voxel-tools.readthedocs.io/en/latest/streams/>) — the cleanest open reference for
  persisting 5.4's mid-field LOD of edited regions. Its clipbox (nested-box, multi-viewer)
  streaming strategy is also the modern replacement for octree-split streaming.
- **John Lin's architecture note** ("The Perfect Voxel Engine",
  <https://voxely.net/blog/the-perfect-voxel-engine/>): don't force render, physics, and
  persistence through one voxel format — multiple specialized formats with explicit
  converters is the *correct end state*, not a smell. We already half-do this (occupancy
  grids / render instances / palette store / blob codec).

### 5.2 Plan adjustments adopted from the survey

1. **Phase 4 follow-on — implicit-default generalization (new 4.2c-adjacent item):** extend
   the 4.4 uniform representation to per-subchunk / dominant-material defaults à la Veloren
   for *surface* chunks (the remaining RAM mass post-4.4 correction). Re-measure first per the
   4.4 correction note; the field multiplier (~7×) was measured on whole-chunk data shapes.
2. **Sub/micro detail endgame = Teardown-style raymarched palette bricks** (box-raster +
   fragment DDA over a per-cube 9³ brick with a mip/occupancy pyramid), merged with
   `RayTracingPlan.md`'s micro-detail prototype — promoted from "slated idea" to the
   field-endorsed answer for the fine-face explosion. Fine greedy merge
   (`RenderOptimization.md` #40) remains the shipping stopgap; the survey's verdict is that
   it's a constant factor, not the fix.
3. **Phase 5.4 architecture = copy Distant Horizons**: mid-field LOD columns persisted in
   their own tables (own compression policy, LZMA-class), quadtree keyed by (pos, lod),
   rendered as a separate pass; terrain LOD generated from `CoarseWorldModel` directly,
   downsampling only edited chunks/structures. godot_voxel's (x,y,z,lod) keying is the
   schema reference.
4. **Phase 4.3 scope upgrade**: implement GPU suballocation as **region arenas**
   (Sodium-style N-chunk regions sharing one allocation + one multidraw) rather than
   per-chunk right-sizing alone — solves allocation count and draw scaling in one motion, and
   is the prerequisite shape for indirect-count GPU culling.
5. **New Phase 5/6-era item — GPU-driven culling**: when resident counts approach 10k, move
   frustum+occlusion to compute (two-phase Hi-Z + `vkCmdDrawIndexedIndirectCount`) per
   Aokana/Ascendant; retires blocker E's O(all-chunks) scans structurally. Mesh shaders stay
   an optional NVIDIA backend, not a dependency.
6. **Compression ladder bookmark (post-4.x, pre-RT):** CoW/dedup of repeated structure
   subtrees (Avoyd pattern) — measure on a settlement world; expected high leverage because
   generated content repeats templates.
7. **Meshing note for the 6-index/vertex-pulling item**: adopt the 8-byte packed-quad format
   (binary-greedy-meshing repo) as the target encoding; keep Vercidium's run-merge in mind if
   destruction remesh latency ever regresses under full greedy.
