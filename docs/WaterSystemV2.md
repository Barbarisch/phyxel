# Water System v2 — Scaling to Oceans, Rivers & Lakes

> Status: **Phase A IN PROGRESS** (updated 2026-07-10). This is the water-**runtime** plan for
> supporting large procedural bodies of water. It is the runtime counterpart to
> [`docs/TerrainGenerationV2.md`](TerrainGenerationV2.md) §P2 (which decides *what generation
> feeds* the water system) — this doc decides *how the water runtime must change to receive it*.
> The two must stay reconciled. It supersedes the scale/roadmap sections of
> [`docs/WaterSystem.md`](WaterSystem.md) (v1), which remains accurate for **what shipped**.
>
> ### Progress (verify each via `git show <hash>` + `build/tests/Debug/phyxel_tests.exe --gtest_filter='Water*'`)
> **SHIPPED (branch `terrain-gen-v2-p0`, unit-tested + solution-auditor-verified):**
> - **A1 — relocatable region** (6ebeab7): `WaterSimulation::shift(delta)` + `WaterManager::recenter`
>   translate the field so the box can move with content preserved. Tests: `WaterManagerTest`,
>   `WaterSimulation.Shift*`.
> - **A2a — player-following** (499fdde): `WaterManager::followTo(focus, hysteresis)` recenters on the
>   camera (XZ, keeps Y); wired into `Application::update`. L4-confirmed the box follows.
> - **A2b — ocean boundary condition** (4773dc1): `setOceanBoundary(bool)` seeds the ocean from the
>   region edges so the sea survives a recenter. **Mechanism only — NOT wired into the live Application.**
> - **A2c part 1 — settle-skip / sleep** (d6fd987): a settled field's `step()` returns O(1) (measured
>   ~36,000× cheaper on a 64³ box, Debug). Includes a fix for a real GPU-bypass freeze the auditor
>   caught (`stepGpu` must `wake()` the sim). Tests: `WaterSimulation.SettledFieldSkipsWork`,
>   `WakeForcesTheNextStepToRun`.
> - **Z-fighting fix** (3e7bafc): depth-bias the flat sea plane so voxel tops at sea level stop
>   flickering. Rendering change; static waterline confirmed clean, **temporal flicker NOT
>   independently confirmed** (move the camera along a coast to verify).
> - **Phase A wrap-up** (2026-07-10): (1) **sea level UNIFIED** — one shared constant
>   `Core::kSeaLevelY` (`engine/include/core/WorldConstants.h`) now feeds the WorldGenerator bake,
>   the `WaterManager` sim default (was 0) and the `RenderCoordinator` sea-plane default (was a
>   private 16); `game.json` load sets both sides from the same value and `set_sea_level` moves
>   both. (2) **ocean boundary wired LIVE**: `game.json` `water.oceanBoundary: true` (applied +
>   reset on world switch in `autoLoadGameDefinition`), new `water_ocean_boundary` debug command,
>   persisted by `water_save`. (3) **Phase-A STRESS test shipped** (`WaterManagerTest.Stress*`):
>   50 recenters oscillating over a standing walled lake (incl. a NEGATIVE window origin) assert
>   volume/level/world-position at EVERY recenter; a 100-recenter one-way walk with the boundary
>   condition asserts the sea re-establishes at the same volume+level every stride. Red-verified by
>   mutation (seam drift ±1 → caught at recenter #3; boundary seeding disabled → caught at flood).
> - **A2c part 2 — per-COLUMN active set** (2026-07-10): a sweep now visits only columns whose mass
>   changed since the last sweep + their N4 neighbors (P = dirty ∪ N4(dirty)); the double-buffer
>   snapshot/write-back is restricted to W = P ∪ N4(P), eliminating the O(box) `m_next = m_mass`
>   copy. Measured (Debug, 64×16×64): partially-active step ~169µs vs ~790µs full sweep, max 77 of
>   4096 columns swept for a basin-contained drop. Equivalence is the load-bearing test
>   (`ActiveSetMatchesFullSweepExactly`: active vs wake()-forced full sweep, per-cell equality over
>   80 steps of a scenario exercising every flow rule). Also fixed two latent holes: `setEvaporation`
>   didn't wake a settled field (thin films would never dry after the toggle), and
>   `WaterManager::update` paid the O(box) `rebuildSurface()` at 20 Hz even when every step was a
>   settled skip (now gated on sweepsRun advancing; the GPU stepper forces the rebuild explicitly).
>   All four new tests red-verified by mutation. L4 smoke: same 7206.999 flood as pre-change,
>   place_water evolves, GPU on→off switch keeps flowing (no freeze).
>
> - **Phase C1 — baked water table** (2026-07-11): `WaterSimulation::fillWaterTable(levelFn)`
>   (per-column levels, connectivity-gated flood seeded from each wet column's surface cell + the
>   region edges) + `WaterManager::setWaterTable` (supersedes scalar sea level / point seeds /
>   boundary flag while bound; re-derives on every recenter) + Application binds it when the
>   STREAMING generator baked hydrology (`world.streaming: true`, height-based non-Flat; opt-out
>   `water.bakedTable: false`). New `water_table_level {x,z}` debug probe (+ the missing HTTP routes
>   for it and `water_ocean_boundary` — CommandRegistry handlers need explicit `EngineAPIServer`
>   routes). L4: Perlin streamed world, water block = `{enabled}` ONLY — sea flooded purely from the
>   bake (region at open ocean = exactly 64×64×9 layers), re-derived across recenters.
> - **Ocean "slab" fix** (2026-07-11, user-reported): the region's per-cell renderer double-drew the
>   pinned sea as a differently-shaded, hard-skirted 64×64 slab following the camera.
>   `rebuildSurface` now SUPPRESSES source-pinned sea-surface cells at the plane's level (the flat
>   plane draws the ocean, inside and outside the region); suppressed columns still feed corner
>   smoothing so splash/lake skirts close against the sea. Lakes (≠ sea level) + unpinned water
>   render per-cell as before. Verified by mutation red (suppression disabled → test fails) AND live
>   same-vantage before/after + a pixel probe (mean B−R over the foreground seabed crop). NOTE — a
>   depth-bias hypothesis was DISPROVEN in the process: the sea-plane's `depthBiasSlopeFactor=1.5`
>   (3e7bafc) was suspected of hiding the plane behind the seabed at grazing angles; a red/green
>   pixel probe with the OLD bias + suppression showed full water tint at both originally-broken
>   vantages (B−R +13.5 / +58.1), so the bias was NOT the cause and the shipped values were kept
>   unchanged. The REAL far/near LOD handoff remains Phase B.
>
> - **Live frame profiling + pinned-ocean settle fix** (2026-07-11): `PROFILE_SCOPE("Water")` around
>   followTo+update — `/api/debug/frame_profile` now shows the water system's per-frame share. First
>   live measurement immediately found a real defect: a fully-pinned ocean NEVER settled (the
>   compression rule wants deep cells slightly over full; the re-pin clamps to exactly 1.0 → pinned
>   stacks re-donated ~0.01 down every step forever → ~6ms Water on every 20 Hz step frame over open
>   sea, at rest, doing nothing). Fix: pinned→pinned transfers are skipped (both ends get re-clamped
>   anyway); pinned→unpinned still flows (breach flooding/springs — covered by the new
>   `FullyPinnedOceanSettlesAndStopsSweeping` test, true red-before-green, + 3 pre-existing breach/
>   dig tests). **Measured (Debug, table-flooded 64×32×64 open-ocean region):** settled ocean
>   0.002–0.004 ms/frame (was ~6 ms on every step frame); a 5×8-mass splash burst ~3.3–3.8 ms on
>   step frames for ~2–3 s, then back to 0.002; heavy chunk-streaming churn still costs ~6 ms per
>   step frame (each chunk-load batch dirties the ocean → one full O(box) re-flood per update) —
>   REMAINING follow-up, not fixed.
>
> **NOT done (do not assume these exist):**
> - Active-set follow-ups: the three O(columns) mask passes use Debug-checked `vector[]` and cap the
>   win in Debug builds (a dirty-LIST would fix it); no Release-build measurement yet; recenter/shift
>   marks ALL columns (correct but unoptimized).
> - Phase C remainder: RIVERS are still dry (no channel tags / head springs from the FlowField bake);
>   lake fill is unit-proven but not yet observed on a real baked lake in-engine (the L4 world's
>   region only reached ocean); the L3 validation pass (river continuity / lake containment vs the
>   actually-carved terrain) is not built — a bake-vs-terrain mismatch would leak a lake silently.
> - Phases **B, C, D** — none started. In particular **C (generation feeds water) is NOT built**: the
>   procedurally-carved rivers are DRY channels; nothing auto-fills them. Only the flat sea plane shows
>   water, and only where terrain is below sea level.
> - **Two demo-related items I was asked about but did NOT build** (2026-07-10, recorded honestly): a
>   **soft shoreline / beach band** (sand/gravel coastal material + gentler near-shore slope) does not
>   exist — the coast is a hard grass-to-water edge; and the **"floating slab"** at a generated patch's
>   edge is NOT fixed — exposed chunk-boundary faces still render as vertical walls (a demo was framed
>   to avoid them, which is not a fix). Real fixes: a coastal-band material rule in the terrain
>   generator, and skipping/streaming the exposed boundary faces.

---

## 0. The goal

Support **oceans, rivers, lakes, ponds** at world scale, eventually driven by the procedural
terrain system — not hand-placed voxel by voxel. The current system's *physics is correct*; the
**scale architecture around it** is the gap. This plan closes that gap foundation-first, per the
terrain-v2 HYBRID decision: **static baked water far from the player, live CA simulation near it.**

---

## 1. What ships today (v1 ground truth)

Three layers, all on `main`. Verified by source read 2026-07-09.

| Layer | File | What it does | State |
|-------|------|--------------|-------|
| **Sim core** | `engine/src/core/WaterSimulation.cpp` | CPU cellular automaton over a dense `float` mass grid. Compression-aware gravity, horizontal leveling, upward pressure (connected water rises to a common level), evaporation sink (off by default), source pinning, channel (no-evap) tags, `fillOcean()` flood-fill. | Solid + unit-tested |
| **Manager** | `engine/src/core/WaterManager.cpp` | Wraps the sim over the live world: 20 Hz fixed step, syncs solidity from chunks, builds sloped per-corner surface + waterfall lips, ocean seam, springs, channels, optional GPU step. Persists authoring inputs to `game.json`. | Shipped |
| **Render** | `engine/src/graphics/WaterRenderPipeline.cpp` (flat sea plane), `WaterCellRenderPipeline.cpp` (per-cell quads) | Two independent pipelines. | Shipped |

**The physics is right.** `tests/core/WaterSimulationTest.cpp` proves mass conservation, basin
leveling, pressure rise, and **connectivity-gating** (a sealed sub-sea pit stays dry until
breached). Source→channel→sink primitives for authored rivers all exist. **Reuse all of this.**

---

## 2. Limitations that block larger bodies

The blocker is the scale architecture, not the CA rules. Each item below is evidenced in source.

1. **The entire simulated water world is one hardcoded 64×32×64 box at world origin.** It does not
   follow the player, stream, or grow — water cannot exist outside it.
   → `editor/src/Application.cpp:361`: `WaterManager(cm, ivec3(0,8,0), ivec3(64,32,64))`.

2. **Dense storage + full-box sweep every tick, no active-set/sleep.** 5 dense arrays (~14 B/cell);
   `WaterSimulation::step()` iterates *every* cell each tick. Scaling the box to ocean size is
   quadratic-infeasible (a 1024×64×1024 region ≈ 67 M cells ≈ 940 MB, swept 20×/s). The design's
   "sparse active regions + sleep" is **documented but unimplemented**.
   → `WaterSimulation::step()` triple loop.

3. **Zero connection to world generation.** `WorldGenerator` has no water code. Oceans/lakes/rivers
   are 100% hand-authored (seaLevel, ocean seeds, springs, channels). No basin detection, no river
   routing. → grep `Water` in `WorldGenerator*` = no matches.

4. **Two render models that don't reconcile.** The flat plane assumes *one* global sea Y (can't
   draw a mountain lake at a different height); the per-cell renderer only draws inside the fixed
   box. No far/near LOD handoff. → `RenderCoordinator.cpp:1651` (plane) vs `:1666` (cells).
   Sea level is also **duplicated state**: `RenderCoordinator::m_seaLevel` (render plane) and
   `WaterManager::m_seaLevel` (sim) are set independently from the same `game.json` key and can
   drift (`Application.cpp:5404` vs `:5498`).

5. **Persistence is authoring-seeds only, to `game.json`.** The field re-derives from seeds each
   load; there is **no per-voxel water field in `world.db`**, so a generated continental hydrology
   (river graph, per-basin levels) has nowhere to live. → `water_save`, `Application.cpp:10380`.

6. **Full-voxel only.** `setSolid` is per whole voxel; subcubes/microcubes collapse to
   all-solid/all-empty — wrong for the engine's mixed-resolution identity.
   → `WaterSystem.md` "Sub-voxel terrain" note.

7. **GPU path is synchronous and round-trips everything each step — "NOT yet a perf win," off by
   default.** `stepGpu()` uploads the full field + all masks, submits a single-time command buffer
   and **waits** for it, then reads the whole field back — every tick.
   → `WaterManager.cpp:348` (`stepGpu`, per-step submit+wait+readback); quote from
   `docs/AgentContext.md` water entry.

**Net:** *oceans* = cosmetic flat plane + box-bounded flood only; *lakes* = correct but only inside
the box and only if hand-placed; *rivers* = right primitives, no routing, can't cross box or chunks.

---

## 3. Target architecture — static-far / sim-near hybrid

```
                        player active region (streams with camera)
                      ┌───────────────────────────────────┐
   static far water   │   LIVE CA (WaterSimulation)         │   static far water
   (baked levels,     │   • per-cell sloped surface render  │   (baked levels)
    cheap plane/mesh) │   • flow, splashing, flooding       │
  ───────────────────►│   • active-set / sleep (O(active))  │◄──────────────────
                      └───────────────────────────────────┘
        ▲                          ▲                                 ▲
        │ far LOD handoff          │ fed by                          │
   per-region water levels    CoarseWorldModel bake (terrain-v2 P2): sea level,
   (sea + each lake)          per-basin lake levels+rims, river polylines+Strahler
                              order, spring/head points → world.db
```

Far from the player, water is a **static surface at a baked level** (no sim). Near the player, the
existing CA runs in a **region that streams with the camera**. Generation bakes the global
hydrology once into `world.db`; the runtime reads it locally. This is exactly the terrain-v2
Layer-0/Layer-1 split applied to water.

---

## 4. Phase plan

Foundation-first (terrain-v2 decision). Each phase is independently testable (red-before-green),
names its required validation depth (**L1** exists · **L2** structural invariant on real output ·
**L3** functional agent-usability · **L4** live runtime), and flags every number **⚑GROUND** for the
grounding-auditor. Standing discipline applies: grounding-auditor on every dimension, red-before-
green + solution-auditor on every "works" claim, stress-test phase, per-placer validation ledger.

### Phase A — Free the sim from the fixed box  ← START HERE
Foundation, no new visible features; unblocks everything.
- Replace the hardcoded 64×32×64 `WaterManager` box with a **player-following active region** that
  re-centers / streams as the camera moves, carrying mass across recenters and re-syncing solidity
  on the moving frontier. ⚑GROUND active-region radius (vs. view distance + sim cost).
- Implement the **active-set / sleep** the design already specifies: track dirty cells, skip
  settled columns, wake on disturbance (the voxel-edit occupancy callback at
  `Application.cpp:366` already exists → feed it a wake list). Turns the O(all cells) sweep into
  O(active).
- **Re-apply authored features as the region slides.** Today everything is silently box-local:
  ocean seeds convert world→local and flood only inside the box (`rebuildOcean`,
  `WaterManager.cpp:245`), and springs/channels no-op when out of bounds (`applySprings`,
  `setChannelWorld`). Under a moving region: (a) ocean seeding must generalize from *point seeds*
  to a **boundary condition** — any region-frontier cell at/below sea level that is open toward
  baked ocean water acts as a seed — else walking away from the seed point makes the ocean vanish;
  (b) springs and channel tags must (re-)pin whenever the region slides over their world cells.
- **Validation L2 + L4:** mass conserved across a recenter (no gain/loss at the seam); a fully
  settled lake steps ~0 active cells; water visually continuous as the region slides.
- **Stress:** walk the player a long distance so the region recenters many times over a standing
  lake — assert level and volume are invariant at every recenter, no seam artifacts.

### Phase B — Static-far / sim-near hybrid + persisted field
- Unify the flat plane and per-cell renderer into **one LOD model** with **per-region water
  levels** (sea *and* each lake at its own height — the single-global-Y assumption is what breaks
  mountain lakes today). Far = static surface at baked level; near = per-cell sim surface; clean
  handoff at the active-region boundary (no double-draw / z-fight).
- **Persist a sparse water field to `world.db`**: per-chunk mass array only where it deviates from
  the baked level, plus a fully-wet / fully-dry flag (the design's sparse storage, currently
  missing). Authoring seeds stay in `game.json`; the *derived field* lives in the world DB.
- **Validation L2 + L4:** far/near surfaces meet flush at the handoff; a deviating chunk reloads
  identically from `world.db`.

### Phase C — Generation feeds water (the terrain-v2 P2 seam)
This is where the water system joins the **procedural terrain system**.
- Consume the CoarseWorldModel priority-flood bake (terrain-v2 P2): **sea level, per-basin lake
  levels + rims, river polylines + Strahler order, spring/head points**. Emit static far water at
  baked levels; in the active region spawn CA **springs at river heads** + **channel-tag
  riverbeds**; set the **ocean-seam** boundary. Generalize `fillOcean()` from box-local seeds to
  baked basin/level data.
- ⚑GROUND: river width & depth by Strahler order (real-world), lake min-volume threshold
  (discard micro-puddles). Sea-level baseline is already declared: **`kSeaLevelY = 16`**
  (terrain-v2 P0 grounded-values table — an engineering-continuity decision, not a geographic
  figure; water must consume that constant, not re-declare its own).
- **Validation L3** (design-required, silent-failure-prone): every river is **continuously
  downhill to a lake or sea** (walk the graph); every lake surface is **flat and contained**
  (single spill level); **no water on a slope**; **no chunk-border level mismatch**.
- **Stress:** a river crossing many chunks and a lake spanning a region border derive **identical
  levels**.

### Phase D — Fidelity + gameplay
- **Sub-voxel floor height per cell** (one float/cell derived from sub-occupancy — the design's
  cheap option; avoids 27×/729× cell explosion) so water sits correctly on subcube/microcube
  terrain, matching the engine's mixed-resolution identity.
- **Buoyancy + drag** on rigid bodies and GPU debris; **swimming / drowning**.
- **Revisit the GPU CA** now that the active set is sparse: keep the field resident on GPU, upload
  only dirty pages — so it becomes an actual perf win rather than a per-step round-trip.

---

## 5. Reuse vs. build new

**Reuse (physics is already right):** the entire CA rule set (gravity/level/pressure/evaporation),
source/channel/sink primitives, connectivity-gated `fillOcean`, sloped per-corner surface +
waterfall detection, the voxel-edit occupancy callback (free wake signal), both render pipelines
(as the far/near ends of the LOD model), the `water` game.json authoring block.

**Build new:** player-following active region + streaming, active-set/sleep, per-region water
levels, unified far/near LOD render handoff, sparse water-field persistence in `world.db`,
generation→water wiring (consume the coarse hydrology bake), sub-voxel floor height, buoyancy/
swimming, resident-GPU CA.

---

## 6. Risks & open questions

- **Mass conservation at a moving seam** (Phase A) and at the static-far/sim-near boundary
  (Phase B) — the v1 risk register's #1, now at region scale. Prototype/measure first.
- **Active-region radius** — sim cost vs. how far interactive water must reach. Measure in Phase A.
- **Physics lifecycle under streaming** — the standing "every DB-load path must call
  `buildAllChunkPhysics()`" rule; water solidity re-sync must ride the same churn.
- **Per-region levels vs. one plane** — the renderer must handle N distinct water heights visible
  at once (sea + several lakes) without z-fighting.
- **Coarse-grid dependency** — the `CoarseWorldModel` scaffold **already exists** (terrain-v2 P0
  implemented + audited 2026-07-09, `engine/{include,src}/core/CoarseWorldModel.*`), but Phase C
  still waits on the **priority-flood hydrology bake** (terrain-v2 P2) to populate it with
  sea/lake/river data. A and B are independent of both and can proceed now.
- **Render density** — the engine's standing #1 issue; watch per-cell water face counts as bodies
  grow (the cell renderer caps at 100k instances).

---

## References
See `docs/TerrainGenerationV2.md` §2b (Priority-Flood hydrology bake) and its reference list;
`docs/WaterSystem.md` (v1 design rationale — implicit ocean, sources/sinks, channel tags,
CA rules); `docs/MixedResolutionVoxelComposition.md` (sub-voxel terrain interaction).
