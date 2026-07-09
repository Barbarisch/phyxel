# Water System v2 — Scaling to Oceans, Rivers & Lakes

> Status: **DESIGN / not yet started** (2026-07-09). This is the water-**runtime** plan for
> supporting large procedural bodies of water. It is the runtime counterpart to
> [`docs/TerrainGenerationV2.md`](TerrainGenerationV2.md) §P2 (which decides *what generation
> feeds* the water system) — this doc decides *how the water runtime must change to receive it*.
> The two must stay reconciled. It supersedes the scale/roadmap sections of
> [`docs/WaterSystem.md`](WaterSystem.md) (v1), which remains accurate for **what shipped**.

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

5. **Persistence is authoring-seeds only, to `game.json`.** The field re-derives from seeds each
   load; there is **no per-voxel water field in `world.db`**, so a generated continental hydrology
   (river graph, per-basin levels) has nowhere to live. → `water_save`, `Application.cpp:10380`.

6. **Full-voxel only.** `setSolid` is per whole voxel; subcubes/microcubes collapse to
   all-solid/all-empty — wrong for the engine's mixed-resolution identity.
   → `WaterSystem.md` "Sub-voxel terrain" note.

7. **GPU path round-trips masks+field every step → "NOT yet a perf win," off by default.**
   → `WaterManager.h` GPU-backend comments.

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
- Replace the hardcoded 64³ `WaterManager` with a **player-following active region** that
  re-centers / streams as the camera moves, carrying mass across recenters and re-syncing solidity
  on the moving frontier. ⚑GROUND active-region radius (vs. view distance + sim cost).
- Implement the **active-set / sleep** the design already specifies: track dirty cells, skip
  settled columns, wake on disturbance (the voxel-edit occupancy callback at
  `Application.cpp:366` already exists → feed it a wake list). Turns the O(all cells) sweep into
  O(active).
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
  (discard micro-puddles), sea-level baseline.
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
- **Coarse-grid dependency** — Phase C can't land before the terrain-v2 CoarseWorldModel +
  priority-flood exist; A and B are independent of it and can proceed now.
- **Render density** — the engine's standing #1 issue; watch per-cell water face counts as bodies
  grow (the cell renderer caps at 100k instances).

---

## References
See `docs/TerrainGenerationV2.md` §2b (Priority-Flood hydrology bake) and its reference list;
`docs/WaterSystem.md` (v1 design rationale — implicit ocean, sources/sinks, channel tags,
CA rules); `docs/MixedResolutionVoxelComposition.md` (sub-voxel terrain interaction).
