# LOD Tier Ledger — every distance-tiered system, in one place

> **Status: LIVING DOCUMENT — created 2026-08-05.** Update this table whenever a tier's levels,
> thresholds, transition, or invalidation path changes. `ContinuousLodPlan.md` §0.3 recorded the
> recurring failure this doc exists to stop: nobody could state how many LOD/distance systems the
> engine has (counts of "four", "nine", "ten" were all undercounts). The answer is maintained
> here, or it is wrong.
>
> Companion docs: [`WorldRenderV2Plan.md`](WorldRenderV2Plan.md) (the active north star),
> [`ContinuousLodPlan.md`](ContinuousLodPlan.md) (C-series architecture),
> [`NearShadowCascade.md`](NearShadowCascade.md) (shadow cascades).
> Observability: `GET /api/debug/lod_report` + `POST /api/debug/lod_probe` +
> `tools/lod_ladder_probe.py` (Phase 0.5 of the 2026-08-05 campaign) measure where each tier is
> actually active at runtime — cite measured switch distances, not header defaults.

## The tiers (code-verified 2026-08-05)

There is no single LOD ladder. There are **8 independent distance-tiered render systems** plus
non-render distance systems (streaming residency, sim LOD) listed below the line.

| # | System | Code | Levels | Distance thresholds (defaults) | Transition | Invalidation / handoff path | Default |
|---|---|---|---|---|---|---|---|
| 1 | Chunk fine mesh (greedy-merged) | `ChunkRenderManager` (`s_fineGreedyMerge`) | 1 (L0) | residency: `ChunkManager::loadDistance` 256 / `unloadDistance` 352 | — | chunk eviction | ON |
| 2 | Distance-driven resident-chunk LOD (C5) | `RenderCoordinator::updateChunkLod`, `LodService::levelForDistance` | 4 (cells 1/2/4/8 cubes; ladder to 5, capped by `s_lodMaxLevel=3` for the fattening defect) | window ~136–352 u (`s_lodTargetPixels=8`) | **POP** (full remesh, 1-level hysteresis) | per-frame re-select, 2 chunks/frame budget | **OFF** (grass conflict + unmeasurable win) |
| 3 | Far-LOD chunk tier (C3.3 — saved chunks/structures past residency) | `RenderCoordinator::updateFarLodChunks/drawFarLodChunks`, `LodPyramidService` | 5 | to `RenderCoordinator::chunkInclusionDistance` 2000 | **POP (still — the last un-dithered transition).** Deferred 2026-08-06: needs push-constant fade plumbing through the main chunk pipeline AND a saved-chunk world to verify on (inactive in the current test worlds) | robust: any entry with a resident chunk dropped per frame. ⚠️ pyramid refreshed only on save; `invalidate()` has no production caller | ON |
| 4 | Far terrain heightmap tiles | `FarTerrainManager/Mesher/RenderPipeline` | 4 rings, steps {2,4,8,16} (tile 128/256/512/1024 u) | bands 0-512-1024-2048-4096 × viewScale | depth-arbitrated underlay (0.5 u below-surface bias); no fog fade | wanted-set refresh after 64 u camera travel, on viewScale change, **and on maxDistance change (fixed 2026-08-05, live-verified 62→39→62 resident with a stationary camera)**; `terrainHidden` also re-checked every 15 frames while stationary (`recheckTerrainHidden`) so meshed chunks stop being papered over | ON |
| 5 | Far-tree instanced LOD meshes | `TreeLodRenderPipeline`, `TreeLodMeshRegistry`, `TemplateLodChain::treeConfig` | 6 (cells {4,6,9,13,18,27} micros) | **PER-INSTANCE** (2026-08-05): `kTreeMeshLevelDist` {360,560,820,1150}; a tile straddling a boundary draws both bracketing levels and each instance dithers to exactly one (`levelBand` complementary partition, ±25 u crossfade) — boundaries cross one TREE at a time, never one tile | 4×4 Bayer dither; fade band `loadDistance→min(load+90, unload−6)` | residency-gated `tileHandoffMinFade` with 3 escapes (distance early-out, `terrainHidden` bypass, out-of-band quadrants don't vote) | ON |
| 6 | Far-tree impostor cards | `FarTreeRenderPipeline` | 1 | 1600 → fade out 1850–2050 | dither | same near-gate; horizon fade distance-driven | ON |
| 7 | Structure LOD proxies | `RenderCoordinator::tickStructureLod`, `TemplateLodChain::structureConfig` | **6** (cells {3,6,9,13,18,27} micros, L0 selected — densified 2026-08-05) | `kStructureLevelDist` {360,500,700,900,1200}, cull 1600 | dither + residency gate (`structureGateProbe`, test-pinned) | **FIXED 2026-08-05**: gate got the 3 tree escapes (`StructureLodGateTest` red→green — out-of-band columns don't vote, votes==0 releases, full-Y-span probe); `setStructureLodTargets` reconciles (removed structures retire via graveyard; live-verified: build→proxy→remove→gone). `solid_proxies_in_band` on `lod_report` is the regression detector | ON |
| 8a | Grass blades | `GrassRenderPipeline` | continuous | radius 224, density `1/(1+140u²)`, floor 1/18 | continuous thin-out + height fade | per-frame | ON |
| 8b | Foliage leaf cards | `FoliageRenderPipeline` | 1 | radius 512 + 27.8 | **Bayer-dither dissolve over the last 10% of radius (2026-08-06** — was a hard whole-chunk pop; evidence: `docs/evidence/foliage_radius_dither_fade.png`) | per-frame | ON |
| 8c | Characters | `LodService::characterLodLevel` | 3 (full / lod1 35 u / lod2 80 u) + cull 400 u | world-unit, viewScale-corrected | pop | per-frame | ON |

**No far tier exists for:** water (other session owns Phase B), kinematic voxels (no distance
bound at all), GPU debris, VFX.

### Non-render distance systems (for completeness — do not confuse with render LOD)

| System | Values | Where |
|---|---|---|
| Chunk streaming residency | load 256 / unload 352 | `ChunkManager.h` |
| Character update LOD | >120 u = 6 Hz, >220 u = 2 Hz | `AnimatedVoxelCharacter` |
| NPC update gate | `fullCharacterTicks` | `NPCManager` |
| Occlusion BFS bound | 512 (cost bound, not a fade) | `RenderCoordinator::applyOcclusionCulling` |
| Shadow reach | **3 cascades (2026-08-06)**: near 40 u (blade-resolving) / mid 420 u (multidraw) / far 1600 u (LOD band casts+receives, cadence 4) — `docs/NearShadowCascade.md` | `RenderCoordinator` |
| Water sim region | player-following CA | `WaterManager` |

### The render-distance config trap (still true)

Only **`Application::maxChunkRenderDistance`** (4096) governs the editor — it feeds the culling
radius (post reverse-Z it is no longer the far plane, but it still bounds what draws).
`EngineConfig` / `WorldInitializer` / `GameSettings` copies are inert. game.json
`world.renderDistance` overrides. See `Application.h` comment block at the declaration.

## Standing rules for touching any tier

1. **Fades are dither discards, never geometry scaling** (user-settled; tiers 5/6/7).
2. **Fade-out must be residency-gated, never distance-only** — streaming is async; distance-only
   fades produce "nothing there" gaps (tier 5's three escapes are the reference implementation).
3. **A proxy's fade gate must be able to reach 0** — every veto condition needs an escape for
   out-of-band/unknowable probes, or the proxy pins solid (tier 7's 2026-08-05 bug).
4. **Correspondence**: a far instance is the same template at the same anchor — no jitter, no
   scale, `stampAnchorFor` anchor math (pinned by `TreeLodMeshTest.AnchorMatchesTheNearStamp`).
5. **Never key deferred per-chunk work by vector index** — coord-keyed only (DirtyChunkTracker
   lesson).
6. **Elevated cameras mandatory** for far-field look verification; ground poses hide the mid band.
7. **Measure switch distances with the harness**, not from header defaults — several thresholds
   are overwritten per frame from live config (e.g. tree fade bands from `loadDistance`).
8. Chunks must not be visible (CLAUDE.md): per-chunk quantities bound cost, never looks.

## Ladder density target (2026-08-05 direction)

User: LOD should be *mostly invisible* and needs **way more than 3 effective levels**. Direction:
densify per-tier ladders (structures {3,9,18,27} → add intermediate steps; trees per-instance
level selection instead of per-tile-centre) until the ladder harness measures no visible pop at
any switch distance under the dither band. Record measured switch distances here as they land.
