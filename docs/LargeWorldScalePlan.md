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
2. **THEN: palette-compressed static storage (items 2a/2b).** Targets the other ~42% (the `Cube`
   objects) — now the dominant cost at the post-4.1 **10.5 MB/chunk**. Surveyed 2026-07-16:

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
