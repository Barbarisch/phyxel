# World-look backlog — user review 2026-08-01

Raised after walking the ProvingGrounds world. Ordered by **mechanism depth**, not by the order
reported, because several of these share a root cause and fixing that one thing resolves more than
one symptom. Nothing here is speculative — each has a named mechanism or an explicit "not yet
diagnosed".

## A. The far-field representation gap (one root cause, three symptoms)

Beyond `unloadRadius` (320u) the **only** tier is far terrain: a bare generator heightmap. It
structurally cannot show a tree, a building, or an edit. LOD pyramids exist only for **saved**
chunks, and streaming worlds never save generated terrain (regenerable ⇒ never dirty — C3 evidence
FINDING 1). So anything that isn't raw terrain has no far representation at all.

| # | Symptom | Status |
|---|---|---|
| A1 | **Trees vanish at medium distance** — "too important to the look of far away terrain to take away completely" | **RE-OPENED 2026-08-02 — mechanism works, LOOK REJECTED by the user; QUARANTINED default-off** (see below) |
| A2 | **Structures fully disappear** | Structures (saved chunks) still work — the pre-existing storage-driven tier squashes buildings acceptably and stays ON. The eviction-cache feed (which carried trees) is what's quarantined |
| A3 | **Detail chunks sometimes stay low-detail when zooming in** | ⚠️ NOT diagnosed. Candidates: far-LOD entry not evicted when the chunk becomes resident (the eviction rule exists but is only verified all-or-nothing), or the known **mesh backlog** (resident but unmeshed renders nothing — `LargeWorldScalePlan` LOD-seam investigation, 3,650 resident / 1,932 meshed) |

**A1/A2 fix as built (2026-08-01):** `Core::EvictedLodCache` (`engine/{include,src}/core/EvictedLodCache.*`)
— at eviction, while the chunk is still fully alive (`ChunkStreamingManager::setOnChunkEvicted`, invoked
after the dirty-save, before the map erase), the chunk's **level-1 LOD volume** is built and stashed
in memory as a `LodBlobCodec` blob (same format as `chunk_lod_blobs`; higher levels re-squash on
demand, bit-identical to the persisted chain — pinned by `EvictedLodCacheTest`). The far-LOD tier
(`RenderCoordinator::updateFarLodChunks`) now takes candidates from **storage ∪ cache** and serves
memory-first; the candidate rescan also keys on the cache's revision counter, closing the
stationary-camera blind spot. LRU-capped (default 4096 chunks), no DB writes, cleared on world
switch. Pure-cube terrain is cached by neither source (far terrain's job) — the cache carries
exactly the sub/microcube chunks: **trees (flora stamps as subcubes), structures, edits**.
Observability: `set_far_lod` response now includes `evicted_cache_chunks` / `evicted_cache_bytes`.

**Runtime verification (ProvingGrounds, Release, 2026-08-01 —
`docs/evidence/pg_worldlook_20260801_pass{1,2}.jsonl`):**
- Pass 1 (cold boot, first vantage circuit): `village_from_afar` far chunks **8 → 344** /
  far instances **2,382 → 183,296** vs the morning baseline — the village renders as a coarse
  mass at 700 u and the plains carry coarse tree masses. Eviction invariant held: 0 far chunks
  at both resident village vantages, and after a teleport-back the far set dropped 344 → 259
  **incrementally** as chunks became resident.
- Pass 2 (warm cache): every vantage draws 382–534 far chunks — the world accumulates far
  representation wherever the camera has been. Cache: 652 chunks / **1.7 MB** (~2.6 KB/chunk).
- **Direct A/B at the same pose** (`village_from_afar`, camera pinned): tier OFF → village and
  all mid-field tree masses **vanish from the frame** (the original symptom, reproduced on
  demand), FPS 95; tier ON → present, FPS 91. The whole mechanism costs ~4 FPS at its
  worst-case pose.
- **Follow-ups:** (1) far chunks serve LOD levels up to 5 uncapped — isolated thin detail
  (trunks) shows the known fattening defect as dark slabs at range (M2 owns the real fix;
  consider capping at `s_lodMaxLevel` meanwhile); (2) ~500 far chunks = ~500 individual draws
  per frame — same structural pattern that made the shadow pass expensive; batch before raising
  the LRU cap or `chunkInclusionDistance`.

**⚠️ REVERSAL 2026-08-02 — the user rejected the LOOK and the eviction feed is QUARANTINED
default-off** (`EvictedLodCache::s_evictionFeedEnabled = false`; live opt-in:
`set_far_lod {"eviction_cache": true}`). User verdict, exploring the fresh world from an
elevated camera: *"squashing of trees just makes weird floating voxels … very very broken."*
Confirmed and attributed by a same-pose A/B (`screenshot_20260802_094148` tier-on vs `_094253`
tier-off): the entire eviction band reads as **floating confetti** — OR-occupancy squash turns
canopies into full solid cells with an EMPTY support column under them, and isolated trunks
fatten into dark towers. **Why the 2026-08-01 "L4-verified" call was wrong:** every verification
pose was at ground level, where the eviction band compresses into the horizon and reads as
"coarse mass"; a plan view lays the band out and exposes every unsupported cell. The counters
and the A/B measured *presence*, not *quality* — presence was never the hard part.
**Re-enable requires a look fix first**, some combination of: a serve-distance floor (only
beyond ~2× the unload radius, where cells merge into silhouettes), a per-cell coverage floor
(cull sparse canopy cells, keep solid masses), support-aware squash (drop cells with no path to
ground), and the resident-path level cap. Iterate with the live toggle **from an elevated
camera** — that is the pose class that failed.

**✅ THE REPLACEMENT SHIPPED 2026-08-02 — far-tree IMPOSTORS ("a tree can be seen from a
kilometer away").** New subsystem: `FarTerrainMesher::planTrees` asks the **deterministic flora
plan** (`WorldGenerator::planFlora` — a pure function of the seed, no chunk data) which trees
live on each far-terrain tile, on the existing far-terrain worker; each tree renders as an
instanced, cylindrically-billboarded **procedural card** (`FarTreeRenderPipeline`,
`far_tree.vert/.frag`: conifer cone / broadleaf canopy / palm / bare snag, species-tinted,
cutout, depth-writing) anchored to the tile's quantized surface — **structurally incapable of
floating**. Range: fade-in past residency (300–360 u) to ~2 km fade-out; per-ring subsampling
(100/55/30 % at steps 2/4/8) keeps counts bounded. Trees exist on tiles the camera has NEVER
visited — the fresh-boot horizon is forested immediately, unlike the eviction cache.
Pinned by `tests/graphics/FarTreeImpostorTest.cpp` (5 tests, red→green: plan→instances,
tile-local sanity + never-floats, determinism across meshers, undergrowth filtered,
coarse-ring subsample). Verified live at the SAME elevated pose that failed the squash tier
(96 FPS, continuous forest, no artifacts) and at ground level (treeline across the lake).
**Known phase-1 gaps:** impostor tint reads paler than near-field leaf cards (calibrate);
hard-ish handoff band at 300–360 u; the flora plan doesn't know the settlement flattened its
footprint, so a far view of the village sprinkles phantom impostor trees through it;
`lastFrameStats.farTrees` counter not yet exposed on `/api/render/stats`.

**Proposed fix for A1/A2:** build a coarse LOD mesh when a chunk **unloads** and hand it to the
far-LOD tier in memory (LRU-capped, no DB writes). Detail then *degrades* instead of vanishing, and
one mechanism covers trees, structures and terrain detail. Groundwork exists: the cut already
handles sub/microcubes, and the leaf-canopy handoff means a coarsened tree keeps its canopy as
solid mass.

**A3 must be diagnosed before it is "fixed"** — it is the one item here with no confirmed
mechanism, and guessing at LOD residency bugs is how the far-terrain misdiagnosis happened.

**A3 experiment 2026-08-01 — did NOT reproduce.** Controlled fast-travel on ProvingGrounds
(700 u teleport from `village_from_afar` back to `village_street`, 25 s settle): the village came
back at full micro-detail (thatch courses, picket fences, hovered voxel = Microcube), far-drawn
chunks dropped 344 → 259 as residency returned (the eviction rule works *incrementally*, not just
all-or-nothing — that candidate is ruled out), and no unmeshed-hole chunks were visible. Whatever
produced the original sighting needs its own repro conditions (likely sustained flight + mesh
backlog, not teleport); do not fix blind.

## B. Water

| # | Symptom | Likely mechanism |
|---|---|---|
| B1 | **Endless ocean under the world** — the edge of the world should be water *or* land, not an infinite sea plane beneath everything | **Mechanism CONFIRMED 2026-08-01 (not yet fixed).** Two compounding holes: (1) `water.frag`'s copy of `basinLevelAt` **drops the `valid` flag** — a dry-sentinel or out-of-bake column falls back to `seaLevel` instead of discarding; (2) the alpha gate (`water_common.glsl` ~483) requires `hasSeabed` (scene depth > cleared), so wherever **no geometry was drawn behind the sheet** the gate is skipped entirely and the sheet draws at full alpha. The rim-wall band-kill can't fire there either (the fragment's own Y *is* sea level on fallback columns). Fix shape: restore `valid` in the frag copy + discard on the dry sentinel rather than falling back |
| B2 | **Water spills past its boundary in low-detail chunks — floods over coastlines** | **Mechanism CONFIRMED 2026-08-01 (not yet fixed).** The near field's **runtime shoreline snap** (`rebuildOcean`, BFS refining the 128-u bake to the carved waterline) is applied via `fillWaterTable` and cached CPU-side only — `recordHydrologyUpload` sends the **raw** `hydro->levels()`, so the GPU far layer keeps the unsnapped 128-unit wet/dry boundary, guarded only by the B1 depth gate (which degrades exactly where far-chunk depth is coarse). Fix shape: upload the snapped grid where available, and/or the B1 per-pixel dry-column discard |
| B3 | **Creeks have foam and mist but almost no water** | **FIXED 2026-08-01 (render-side), runtime check pending.** Root cause confirmed: the pin is deliberately a sub-hold film (flood-safety invariant, kept), but `rebuildSurface` reported that film as the column depth → water alpha ~0 while flow-keyed foam drew at 0.55 strength. Fix: **kinematic depth** — the surface report floors a baked channel's shading depth at its carve depth (`WaterManager.cpp` rebuildSurface, same philosophy as kinematic flow), and foam now keys on that depth. Sim mass/pins untouched — `CreekPinIsFractionalAndConfined` still green; new pin: `CreekSurfaceReportsCarveDepthNotThePinnedFilm` |

## C. Vegetation density and shape

| # | Symptom | Action |
|---|---|---|
| C1 | **Forests way too sparse** | `biomes.json` Forest was density 0.85 / **spacing 6**. Spacing is the dominant knob (slot grid) — halving it roughly quadruples slots |
| C2 | **Not nearly enough brush** | **DONE 2026-08-01 (data + one bug), runtime check pending.** The original entry was **stale**: `floraLayers` *is* parsed and placement *does* run N bands with per-layer decorrelated seeds (landed `fafbf121`, 2026-07-04) — what was missing was **data** (no biome declared an undergrowth band; EnchantedForest used the one existing band for giants) plus a real bug: `applyRecipe` cleared `extraFloraLayers` **unconditionally**, so any stored recipe predating a band stripped it on load (pinned by `FloraLayersTest.RecipeWithoutLayersKeepsTheBiomesJsonLayers`; guard now mirrors layer 0's). All 8 biomes now carry an undergrowth `floraLayers` band (bush/fern/shrub at spacing 3–8) and the tree pools got their slots back (bushes pulled out of layer 0). Note the placement grid floor: `kFloraGrid = 3` — spacing below 3 cannot subdivide further |
| C3 | **Grass should be taller, a little denser** | `bladeHeight` / `bladesPerVoxel` |
| C4 | **Grass should taper at the edge of a grassy area** | **SHIPPED 2026-08-01.** `ChunkRenderManager` bakes each grass voxel's 8-neighbour grassy count (±1-voxel step tolerance, so terraces don't taper) into `GrassInstanceData::tex` bits 16-19; `grass.vert` tapers blade height toward edges (floor 0.40 — short fringe, never bald). **Cross-chunk neighbours count as GRASSY** — material is unqueryable across the border, and a wrong "edge" would draw a taper seam along every chunk boundary (the density-LOD seam lesson). Red-before-green: `tests/graphics/GrassEdgeTaperTest.cpp` (4 tests: interior 8/8, dirt border, terrace tolerance, border-as-grassy) |

## D. Fauna

| # | Symptom | Notes |
|---|---|---|
| D1 | **Animals not always patrolling** — should walk, graze, etc. by default | **Code-level mechanism identified 2026-08-01 (not yet live-confirmed).** Update LOD is ruled out — the behaviour tick is explicitly not LOD-gated and the LOD banks dt (distance preserved). The real gate: fauna are `PatrolBehavior` in wander mode with **exactly one waypoint**; on `computePath` failure they zero velocity and retry the **same unreachable point** every 2 s forever (the recovery branch needs `waypoints.size() > 1` — never true in wander). Targets go unpathable because (a) `NavGrid::MAX_STEP_UP = 0` — A\* cannot gain a single voxel on sloped terrain, (b) `nearWall` exclusion under dense flora (a stamped tree makes its column's surface the canopy top), (c) the NavGrid **never grows into streamed chunks** while `FaunaSpawner` follows the camera — outside the built region `findPath` bails instantly. Animals that spawned with **no** pathfinder wired take the direct-line fallback and roam fine — matching "not *always*". **CONFIRMED LIVE 2026-08-01**: 40,173 `findPath failed: startCell=NULL goalCell=NULL` lines in ~10 min of ProvingGrounds uptime, every fauna NPC frozen with sub-voxel drift since spawn — both cells NULL means the animals stand entirely outside the built nav region. **FIXED**: wander-mode path failure now walks the leg DIRECT (the update loop's existing direct-line branch; arrival re-rolls a fresh target; stuck-recovery stays as backstop) — exactly how pathfinder-less animals always roamed fine. Patrol routes keep strict pathing. The off-grid A\* failure (both cells NULL) demoted to DEBUG — it was the dominant log line (TerrainIK lesson). Pinned by `WanderOffTheNavGridWalksDirectInsteadOfFreezing`, which asserts **travelled distance** (>10u/20s), not "any nonzero velocity" — the broken loop still emitted one moving frame per 2 s retry cycle, which a weaker check mistakes for wandering. Deeper nav gaps remain open: `MAX_STEP_UP = 0`, NavGrid never grows into streamed chunks. **⚠️ PARTIAL — a SECOND blocker exists below the behavior layer (verified live on the fixed build):** with the fix in, PatrolBehavior demonstrably re-rolls fresh targets and commands direct-line velocity every leg (`Wander path unavailable — walking leg direct` DEBUG lines, distinct targets each time, `STUCK (replan)` firing every ~1.5 s) — **but the character bodies produce ZERO displacement**: positions bit-identical over minutes, `y` frozen at the exact integer spawn height (never settles to a grounded fraction), a fresh spawn moves ~2 u once then wedges. The commanded `setMoveVelocity` is not being executed by the character-motion/physics layer — suspects: capsule embedded at spawn (FaunaSpawner spawn-Y vs collision), or streaming-spawned NPC never properly registered for grounding. Needs its own diagnosis; the behavior fix is necessary but not sufficient. **Bonus look observation:** at the retuned grass height (0.44 × 1.85 lush ≈ 0.8 u) small fauna (fox/deer) are completely invisible inside meadows even from 6 u away |

---

### Discipline note

A/B/D items are **mechanism work** and several are explicitly undiagnosed. The temptation is to
tune a constant until the symptom moves; the far-terrain episode in this same session is what that
costs — a misread far plane produced a confident, wrong "regression" diagnosis. Diagnose first,
then fix, then measure at a pinned ProvingGrounds vantage.
