# Water Core — the foundation for all future water work

> **Status: DESIGN OF RECORD, not yet started (2026-10-08).** This document replaces the ordering
> and the architecture sections of [`WaterRethink.md`](WaterRethink.md) (§3–§6 there are now
> history; its §1–§2 stock-take, §8 gates and WP0 ledger remain the evidence base). `Water.md`
> stays the current-state reference for the code as it exists today. Nothing in this document is
> built; **nothing starts until the decisions in §12 are settled.** Every previous attempt at water
> in this engine was judged unsatisfying by the user. This one is designed slower, from the small
> scale up, and it is written so that each claim can be falsified before the next one is built on it.

---

## 0. The brief, restated (user, 2026-10-07/08)

1. **Small-scale function and feel are paramount.** Water flows, volume is respected, water shows
   visual signs of being disturbed by debris and characters, and it can be pushed, pumped, poured
   and removed.
2. **Small bodies are the core; large bodies sit on top of it.** Oceans, lakes and rivers are built
   from the same water, optimised where nothing moves, never the other way round.
3. **Water is a physical entity, never a universal level, never rendered for looks.** Digging down
   must never reveal a wavy sheet. Edits never create water. (Standing rule, repeated.)
4. **Shorelines of large bodies are probably small-scale.** (User's instinct; §8.3 agrees and says
   why.)
5. **Performance matters, but at small scale it should not be the constraint.** Measure, don't guess.
6. **What exists is reference, not a constraint.** Blow it away where something better is possible.
7. **Be slower, more thorough, far more detailed.** This document is the foundation for all future
   water work.

---

## 1. Principles (the invariants every later decision must satisfy)

| # | Principle | What it forbids | How it is checked |
|---|---|---|---|
| P1 | **Mass is the truth.** Every unit of water is a quantity of mass somewhere: in a simulated cell, a column span, or a body record. The world's total water is a number that only sources and sinks may change. | Levels without mass; water that appears because a renderer decided to draw it; "pinned full" reservoirs that mint mass. | `water_mass_ledger` route: Σ cells + Σ spans + Σ body masses, before/after any operation, ± sources − sinks = 0 to 1e-4. |
| P2 | **Motion is real.** Where water moves it has velocity and inertia; it sloshes, pours in arcs, carries things, piles up against walls, drains at physical rates. | Diffusion-only spreading; kinematic "flow maps" standing in for transport; waves that are only shading. | The feel scenarios (§3) with predicted rates (Ritter, Torricelli, Hunt) and the camera-walk probe. |
| P3 | **One water, three resolutions.** Simulated cells (fine), column spans (medium), body records (coarse) describe the same water; at rest they agree exactly; resolution follows where motion is, existence never follows the camera. | Separate "sim water" and "render water"; a sheet at a level; far planes. | Settle→write-back equality test; the camera-walk probe (WP0 tool) on every change. |
| P4 | **Sub-voxel by default.** Small water lives at the engine's sub-voxel tiers (⅓, ⅑), like every other detail object in this engine. A trough, a bucket's splash, a doorway flood are sub-voxel events. | Simulating a bucket at 1 m³ cells. | Resolution policy §4.5; the trough and pour scenarios. |
| P5 | **Water obeys the voxels.** Solids are the engine's real occupancy at micro resolution (`PackedOccupancyPool`, 9³ per cube — the same pool GPU debris and lighting read). Water finds every hole a debris cube would find. | A separate "water solid mask" that drifts from the world. | Drain-hole and breach scenarios; `occupancy_diff`-style parity check between the pool and the sim's solid view. |
| P6 | **Two-way coupling.** Bodies entering water displace it and are pushed by it; characters leave wakes; blasts shove it; a pump moves it; a bucket takes it. | One-way "sample depth, apply drag" only. | Splash, wading, push, pump, scoop scenarios with mass and motion assertions. |
| P7 | **Rest is free and exact.** Settled water costs nothing, sits perfectly flat, survives save/load bit-exactly, and wakes only when disturbed. | Simulating oceans at rest; jitter; refill-on-load. | Sleep latency and reload tests. |
| P8 | **Large bodies are the same water, compressed.** An ocean is a body record + spans + a boundary condition; its interior is never simulated because nothing moves there, which is a cost decision, not a different kind of water. | A second water system for big bodies. | Shoreline and outlet scenarios run the SAME core against a body boundary. |
| P9 | **Grounded, red-before-green, measured.** Every behaviour has a prediction from a cited formula or a stated spec before the run; every rig is small, one-variable, with a control. | "Looks right". | The Stop-hook discipline. |

---

## 2. The water model

### 2.1 Units
- World unit = 1 voxel = **1 m** (fine items: 1/81 u ≈ 1.23 cm, `docs/FineVoxelItems.md`).
- Mass unit = **m³ of water** (density 1 000 kg/m³ when a force is needed). A full voxel cell = 1.0;
  a full ⅓-cell = 1/27 ≈ 0.037; a bucket ≈ 0.01–0.02 (10–20 L).
- Time: the core steps at a fixed **Δt = 1/60 s** (substeps when CFL requires), rendered with
  interpolation. Gravity 9.81 m/s².

### 2.2 Three representations, one truth (P3)

| Tier | Name | Holds | Resolution | Lives in | Written by |
|---|---|---|---|---|---|
| **C** | **Active Volume (AV) cells** | fill fraction f ∈ [0,1], face velocities, (temperature/dye later) | 1, ⅓ or ⅑ voxel (per AV, §4.5) | GPU + CPU mirror, transient | the core solver |
| **B** | **Column spans** | per column: [bottom, top], float top (fractional fill of the top cell); multi-run for overhangs/caves | 1 column × float Y | chunks (`Chunk::WaterSpanLocal`, ChunkBlobCodec v2 — exists) | generation; AV write-back at rest |
| **A** | **Body records** | id, class (puddle/pond/lake/river/ocean), level, total mass, bbox, outlet columns, discharge, recharge | world | `world_meta` `WaterBodyTable` (new) | generation; AV write-back; sources/sinks |

Rules: an AV overrides B and A inside its box while awake; at sleep it writes its surface to B (top
per column, multi-run where the AV found overhangs) and its mass delta to A, then frees itself.
B and A agree by construction (A.mass = Σ B over the body's columns, checked). The renderer draws C
where awake, B elsewhere near/mid, and A-derived tiles far (§8.5).

### 2.3 What a "body" is at small scale
A **puddle** (≤ 0.05 m deep film, evaporates), a **pond** (finite, conserved, any size the AV can
hold), a **container** (a gameplay volume with capacity: bucket, trough, cup — mass held, not
simulated, poured out as a source), a **stream reach** (steady discharge between an inflow and an
outflow boundary). Each gets a body record the moment it rests; records are what persistence,
scooping and pumps account against.

---

## 3. The feel specification (the heart of this document)

Each scenario is a bench rig **inside one chunk**, one variable, a written prediction, a control,
and a validation depth. They are listed in the order they gate the build (§11). "Feel" items that
cannot be measured are judged by same-vantage captures against the WP0 references and the user's
sign-off, and that is stated where it applies.

| # | Scenario | Rig (WaterBench_Small unless noted) | Prediction (before the run) | Control | Depth |
|---|---|---|---|---|---|
| **S1 Pour** | Tip a 0.02 m³ bucket onto flat stone from 1 m | 8×8 flat pad | Spreads to a puddle; film ≥ 0.01 m holds (P1: 0.02 ± 1e-4 m³ on the pad); at rest in < 3 s; no mass anywhere else | Same pour onto a 1-deep pit: all 0.02 m³ in the pit | L3 + L4 |
| **S2 Trough fill** | Pump 0.001 m³/s into a 2×1×1 trough (⅓-res cells) | trough rig | Surface rises linearly, full at 2 000 s… ⇒ use 0.1 m³/s: full in 20 s; then overflows at the rim onto the pad as S1-type film; mass ledger = pumped − overflow | Pump into a sealed trough: level stops at the rim, no overflow, ledger exact | L3 + L4 |
| **S3 Dam break** | Basin bench (exists): 3-deep block over 10 u of floor | Basin | Front reaches the far wall at the Ritter speed 2√(g·h₀) ≈ 10.8 m/s → ≈ 0.93 s (frictionless bound; measured must be ≤ 15 % slower); reflected crest ≥ 0.8× incident; flat within 1 mm after 10 s; mass ± 1e-4 | Same block, sim disabled: nothing moves | L3 |
| **S4 Breach** | Basin full to 3; dig a 1-wide notch in the east wall 1 above the floor | Basin | Outflow rate through the notch ≈ Torricelli A·√(2g·h) (h = head above notch sill; within 20 % while h > 0.5); level falls accordingly; outside puddle grows by exactly what left | Notch above the surface: no flow | L3 + L4 |
| **S5 Drain hole** | Basin full; dig a 1×1 hole in the floor into a 4×4×3 sealed cavity below | Basin | Water falls into the cavity (3-D path), cavity fills to 48 m³ then the hole surface equalises; basin level drops by 48/area; air trapped? — no (open hole), so it fills completely | Cavity with no hole: stays dry | L3 |
| **S6 Crate splash** | Drop a 1 m³ crate (density 0.6) from 5 m into the full basin | Basin | Splash crown and ring; crate settles floating at 60 % draft (0.6 m submerged) ± 5 %; surface flat again < 8 s; mass unchanged; displaced volume = 0.6 m³ reflected in level rise = 0.6/area | Crate of density 1.6: sinks, rests on the floor, level rise 1.0/area | L3 + L4 |
| **S7 Wading** | Character walks 10 m through 0.8 m water | channel rig (2 wide × 1 deep × 12 long) | Speed drops to the shipped wading factor; bow wave ahead, wake behind (surface deviation ≥ 2 cm measured at 1 m behind); stride splashes; water pushed sideways returns; mass unchanged | Same walk on dry channel: no surface events | L4 (look) + L3 (mass) |
| **S8 Push** | Blast impulse 2 m from a 4×4 pond | pond rig | Water displaced away ≥ 0.3 m on the near side, returns and settles; mass unchanged; droplets (if any) counted in the ledger and re-absorbed | Blast 20 m away: no response | L3 + L4 |
| **S9 Waterfall** | Pump 0.3 m³/s over a 6-high ledge into an empty pit | ledge rig | Falls as a stream (droplet layer), forms a pool that rises until the pit overflows; landing splash/mist; ledger exact including airborne droplets | Pump off: pool stays at its level, no droplets | L4 (look) + L3 |
| **S10 Stream over rocks** | 1-wide channel, 1:20 slope, 3 subcube rocks, inflow 0.05 m³/s at top, outflow sink at bottom | channel rig | Steady state within 10 s: inflow = outflow ± 2 %; standing waves at rocks; no accumulation; depth ~ Manning's for the slope (stated per rig) | Remove rocks: flat steady flow, same discharge | L3 + L4 |
| **S11 Rest, sleep, reload** | After each of S1–S10 | all | AV sleeps within 10 s of the last motion; written spans equal the surface ± 1 mm; total cost at rest = 0; save → cold restart → identical spans and masses | — | L2 + L4 |
| **S12 Shoreline (large-body boundary)** | Coast bench shore band as an AV driven by the ocean body + swell | Coast | Swell enters, shoals, breaks (McCowan 0.78·d), runs up the sand (Hunt's formula ± 20 % on the 1:20 beach), retreats; water on the sand edge moves; nothing persists above the swash line; ocean mass accounting flat | Calm weather: no run-up, waterline static | L3 + L4 (look) |
| **S13 Scoop/pour cycle** | Scoop 0.02 from the pond, pour it 3 m away | pond rig | Pond mass −0.02, new puddle +0.02, ledger exact; repeat ×100: no drift | Scoop from dry ground: 0 | L3 |
| **S14 Pipe/pump transfer** | Pump between two troughs through a 1-wide "pipe" of air cells with a 1-high rise | two-trough rig | Mass moves at the pump rate until the source trough is empty or the sink is full; pipe cells hold only transit water; no leaks into solid | Pipe blocked by one voxel: no transfer, pump stalls (reports it) | L3 |

**Feel items judged by eye (user sign-off, same-vantage A/B against `docs/evidence/water_v4_refs`):**
S6 splash shape, S7 wake, S9 fall and mist, S12 breaking waves, and the surface look of every
rest state (flat, not stair-stepped, not noisy).

---

## 4. The simulation method

### 4.1 Requirements (from §3)
R1 inertia (S3, S4, S6, S7, S8, S12) · R2 strict conservation (every row) · R3 full 3-D topology
(S5 drain, S9 fall, S14 pipe) · R4 two-way bodies (S6, S7, S8) · R5 exact rest and sleep (S11) ·
R6 bounded cost at small scale, GPU-friendly · R7 sub-voxel resolution (S1, S2, S14).

### 4.2 Candidates, honestly

| Method | R1 | R2 | R3 | R4 | R5 | R6 | R7 | Verdict |
|---|---|---|---|---|---|---|---|---|
| Mass CA (today's `WaterSimulation`) | ✗ diffusion, no momentum | ✓ | ✓ | half | ✓ | ✓ | ✓ (cells are cells) | **The reason past attempts felt wrong.** Keep as the mass-accounting reference only. |
| 2.5-D shallow water (pipe model; the WaterRethink WP3 pick) | ✓ surface only | ✓ | **✗** no overhangs, pipes, falls, drains | ✓ surface | ✓ | ✓ | ✓ | Right for open surfaces, wrong as THE core: S5, S9, S14 are impossible. Reappears as the deep-column compression (§4.6). |
| 3-D Eulerian voxel liquid (MAC grid, VOF fill fractions, pressure projection) | ✓ | ✓ with flux limiting | ✓ | ✓ (solid faces; immersed bodies) | ✓ with rest damping | ✓ GPU | ✓ | **The core.** Known-hard parts: free-surface advection without volume drift; surface noise at rest. |
| FLIP/PIC particles + the same MAC grid | ✓✓ | ✓ (particles are mass) | ✓ | ✓✓ splash | ✗ noisy at rest | moderate | ✓ | **The splash/droplet layer** on top of the grid, never the resting water. |
| Tall-cell hybrid (Chentanez & Müller 2011) | ✓ | ✓ | ✓ near surface | ✓ | ✓ | ✓✓ | ✓ | **The compression** of the Eulerian core for deep columns = our spans. Phase G. |

**Decision proposed: a 3-D Eulerian voxel liquid with VOF fill fractions and pressure projection,
running inside bounded active volumes at sub-voxel resolution, with a bounded droplet layer for
spray, and column-span compression for deep still water.** This is one solver; the "shallow-water"
behaviour emerges where the water is shallow instead of being assumed. References: Harlow & Welch
1965 (MAC grid), Hirt & Nichols 1981 (VOF), Stam 1999 (stable advection), Bridson *Fluid Simulation
for Computer Graphics* 2015 (pressure projection, solids, free surface), Zhu & Bridson 2005 (FLIP),
Chentanez & Müller 2011 (tall cells).

### 4.3 One tick, in order (per active volume)
1. **Solids**: refresh the AV's solid mask from the micro occupancy pool at the AV's resolution
   (P5), plus kinematic bodies (characters, furniture, debris) as moving solids with their velocity.
2. **Sources/sinks**: pumps, pours, springs, drains, the body boundary (§5.3), evaporation of films.
3. **Forces**: gravity; external impulses (push, blast); drag from kinematic bodies (two-way, §6).
4. **Advect** velocity and fill fraction (semi-Lagrangian for velocity; a conservative, flux-limited
   VOF transfer for f so Σf is exact to float rounding — the CA's clamp-to-available rule reused).
5. **Project**: solve pressure so velocity is divergence-free in full cells; free-surface cells get
   atmospheric pressure (ghost-fluid); solid faces get zero normal velocity. Jacobi/red-black on
   GPU, PCG on the CPU reference.
6. **Extrapolate** velocity into air cells near the surface (so the surface keeps moving coherently).
7. **Rest detection**: kinetic energy < ε and max |Δf| < ε for N ticks → sleep (§5.2).
8. **Surface**: build the sub-cell surface height per column from f (and lateral surfaces where the
   water column has air below it — overhang cases) for the renderer and the queries.

### 4.4 Correctness rules (each is a unit test before any GPU work)
- Σ f over the AV changes only by sources − sinks − boundary flux (P1).
- Still water stays still: a flat f-field with zero velocity produces zero velocity after a tick
  (no spurious currents from the projection or from solids).
- Hydrostatics: pressure at depth d = ρ g d ± 1 % in a resting column.
- A sealed cavity cannot gain water; an open hole drains at Torricelli ± 20 %.
- Solid faces: no flux through a face whose cell is solid, at every resolution.
- CFL: substep so |v|·Δt ≤ 0.5·cell; falling water from 6 m (11 m/s) at ⅓ cells and 1/60 s needs
  2 substeps — stated, not discovered.
- Determinism: the CPU reference is bit-reproducible; the GPU path matches it within a stated
  tolerance on every §3 scenario (the AVBD solver's `debris_settle_bench` pattern).

### 4.5 Resolution policy (P4, and the "chunks must not be visible" rule)
AV cell size is chosen by the AV's extent, never by the camera: ≤ 8 m span → **⅑ voxel**
(containers, fountain bowls — if §12 decides containers are simulated at all), ≤ 32 m span → **⅓
voxel** (ponds, troughs, moats, doorways, streams), larger → **1 voxel** (shoreline bands, river
reaches). Mass and existence are identical across resolutions (an AV re-sampled at a coarser
resolution holds the same total mass per column); only motion detail differs — the same contract
the engine's own voxel tiers make. Cost: a 32×16×32 m AV at ⅓ = 442 k cells; the pressure solve
(40 red-black sweeps) ≈ 18 M cell-updates per tick ≈ 1 G/s at 60 Hz — well inside a 4090's
budget and the §10 envelope, and the Basin floor (1.0 ms) is the measured baseline to add to.

### 4.6 Deep still water (compression, later)
Below the top K cells of a column with f = 1 and no motion, cells are not stored: the column is a
tall cell (= a span). Pressure in a tall cell is hydrostatic. This is Chentanez & Müller's
restriction applied to our spans and is what makes a 50 m-deep lake edge affordable. **Phase G,
not the core**; the core is small enough to be fully 3-D.

---

## 5. Active volumes

### 5.1 Lifecycle
- **Wake** on: a voxel edit touching water or within 1 cell of it; a kinematic body entering water;
  a source/sink activating; a pour; an impulse; a body boundary being crossed by something. The AV
  box = the connected water around the trigger (flood over spans, bounded by `maxCells`) + a 2-cell
  margin of air + the solids around it.
- **Grow** when water reaches a box face that borders more water or open air; cap at the tier's
  `maxCells`; beyond the cap the face becomes a **body boundary** (§5.3) — water that leaves is
  accounted to the body, not lost.
- **Merge** two AVs whose boxes touch and share water.
- **Sleep** (§5.2): write back to spans and body records, free.
- **Multiple AVs** are the norm (a pond here, a trough there); total cell budget per tier (§10).

### 5.2 Rest and write-back (P7)
Sleep when kinetic energy < ε_K and max |Δf| < ε_f for N = 30 ticks. Write-back: per column, the
surface height from f (and multi-run spans where air lies under water in the column); body record
mass = Σ f × cell volume over the body's columns. **Test:** after write-back, re-waking the same
water from spans and stepping 60 ticks changes nothing (flat stays flat; no mass change). Save →
cold load → identical.

### 5.3 The body boundary (P8; WaterRethink §8.7 item 2 made concrete)
An AV face that borders a large body is a **Dirichlet level + flux ledger**: cells on the face are
held at the body's level (their f set from the level each tick, velocity free); the net mass that
crosses the face is debited/credited to the body record each tick. The body's level is
`f(mass, bathymetry)` — flat-body invariant — and the face level follows it. Oceans have an infinite
reservoir flag (mass unbounded, level fixed); lakes recharge at their rate (decision §12); ponds are
finite. **Swell** (S12) is a prescribed surface motion on an ocean face: the Gerstner field's height
and horizontal velocity at the face drive the AV, so waves enter the simulated band physically.

---

## 6. Coupling (P6)

| Interaction | Mechanism | Scenario |
|---|---|---|
| Rigid body / debris / furniture in water | Body volume marked as a moving solid in the AV (its velocity on the faces); pressure on the body's faces integrated → buoyancy and drag **emerge** from the solver; displaced water is real | S6 |
| Character | Capsule as a moving solid; wading slowdown from the integrated drag, not a table; stride splashes from the surface response; current push from the velocity field | S7 |
| Blast / push tool | Impulse field added in step 3 of the tick, radius and strength grounded on the blast's energy | S8 |
| Pump / pipe | A source cell and a sink cell with a rate; the "pipe" is just air cells water flows through; a blocked pipe stalls the pump and reports | S14 |
| Scoop / bucket | Sink at the scoop column: removes ≤ rate × Δt up to what is there; the container's mass increases; pour = source from the container | S13 |
| Voxel edit | Solid mask refresh; edits never create water (rule 3): a dug cell starts with f = 0 and fills only by flow | S4, S5 |
| GPU debris (existing tiles) | The tile upload reads the AV surface + velocity where awake, spans elsewhere — the existing 6c coupling keeps working, now fed by real motion | S6 |
| Sound | Surface-event hooks (splash energy, flow speed) for `SoundSystemV2` beds and cues | S6, S9 |

Today's one-way coupling code (`setWaterQuery`, `WaterHooks`, `columnWater` tiles) is kept as the
**interface shape** and re-pointed at the AV; the drag/buoyancy *tables* retire once the integrated
forces match them on S6 (red test: the table says 0.625 draft for buoyancy 1.6; the solver must put
a density-0.6 crate at 0.6 ± 0.03).

---

## 7. Rendering the core

- **Surface mesh from the AV** at the AV's resolution: per column the sub-cell height from f, with
  lateral faces where water meets air sideways (overhangs, falls); smooth-shaded, normal from the
  height field plus the solver's own surface velocity for ripples. **Disturbance visuals are the
  simulation**: a wake, a splash ring, a slosh are height in the mesh, not a separate ripple field
  (`RippleField` retires).
- **Shading**: `water_common.glsl` unchanged in model (Beer-Lambert, Fresnel, SSR, foam); foam from
  surface curvature/velocity divergence (whitewater where the solver says so) instead of a crest
  phase guess; per-body turbidity still from the profile.
- **Droplets** (S8, S9, S6 spray): bounded particles rendered as the engine's existing GPU particles
  (cap 10 000 shared with debris — a budget decision in §12); each droplet carries mass and is
  re-absorbed into the AV on landing (ledger-exact).
- **Spans elsewhere** draw via the existing sheet (it becomes a span renderer only; flat-sea and
  bake fallbacks deleted). Far: A-derived tiles (WaterRethink WP2, now Phase G).
- Underwater camera: submergence from the AV f-field where awake, spans elsewhere — one query.

---

## 8. Large bodies on top (Phase G, after the core is proven)

8.1 **Body table** (`WaterBodyTable`): generated from the bake (ocean, lakes, river reaches) with
level, mass (Σ spans), outlets (from the FlowField), discharge for reaches. Persisted.
8.2 **Spans everywhere** (WaterRethink WP1 steps 1, 1b, 5, 6 — gated READY in its §8.8): river
spans; the hydraulic flood that stops at the outlet; retire flat-sea and bake placement; rebuild on
residency set not count. **1b and 6 are small and move early** because the camera probe and the
River bench need them (Phase D).
8.3 **Shorelines are small-scale** (user's instinct, confirmed): the shore band is an AV at 1-voxel
resolution driven by the ocean body boundary + swell (§5.3); waves collide with the voxels because
they are simulated against them. The analytic Gerstner sheet remains the look of open water beyond
the band, and must agree with the band at rest (probe).
8.4 **Rivers**: reaches are bodies with discharge; an AV at any reach where something happens
(crossing, dam, crate); between AVs the surface is spans + the flow-map look; transport inside an
AV is real (S10).
8.5 **Far tiles and weather**: WaterRethink WP2 and WP6, unchanged in content, last in order.

---

## 9. What exists: keep, re-point, replace, delete

| Component | Fate | Why |
|---|---|---|
| `Chunk::WaterSpanLocal` + ChunkBlobCodec v2 + `waterSpansForBlock` | **Keep** (Tier B) | Correct data model; hydraulic-flood fix is additive |
| `HydrologyMap`, `FlowField`, `PriorityFlood`, `WaterBodyIndex` | **Keep** (feeds Tier A) | Correct coarse model; gains `filledAt` and outlets |
| `WaterOccupancy` (`buildOpenWaterSpan`, `floodBodiesOverGrid`) | **Keep + fix 1b** | Tested, window-independent |
| `WaterSimulation` (mass CA, momentum bias, MIN_HOLD, evaporation, pins) + `water_flow.comp` | **Replace** by the core; keep as the conservation reference in tests until the core passes S1–S5, then delete | No inertia — the root of "feels wrong" |
| `WaterManager` window (64×32×64 camera-following), overrides/bank/deltas stores, `kinematicRiverFlow`, pinned rivers | **Delete** | Camera-following existence; fake transport; three persistence hacks replaced by Tier A/B |
| `RippleField` | **Delete** | The AV surface is the ripple |
| `WaterCellRenderPipeline` (per-cell quads) | **Replace** by the AV surface mesh | Hard-coded 1×1 cells, instance buffer rewritten under the GPU |
| `WaterRenderPipeline` sheet + `water_common.glsl` | **Keep**, flat-sea and bake placement deleted | Span renderer + the one shading model |
| Buoyancy/drag/wading tables, `WaterHooks`, debris water tiles | **Re-point** at the AV; tables retire when S6/S7 pass with integrated forces | Interface shape is right |
| Debug routes (30) + 13 MCP tools + `water_render_grid` + probes | **Keep**, extend with `water_mass_ledger`, `water_av_list`, `water_av_probe` | Verification surface |
| Benches: Basin, Coast, River + reference captures | **Keep**; add `WaterBench_Small` (one chunk per rig along +X, DebrisLab pattern) | §3 rigs |

---

## 10. Performance envelope (measured, not assumed)

Baselines (WP0): water today 0.2 ms GPU; Basin floor 1.0 ms; Coast/River frames 16–57 ms dominated
by foliage/shadows. **Core budget:** total awake AV cost ≤ **2.0 ms GPU** at the `high` tier on the
4090 with the §3 rigs awake (pressure 40 sweeps, ⅓-res ponds), ≤ 0.3 ms CPU for lifecycle; at rest
**0.000 ms** (P7, asserted). Tiers bound `maxCells` per AV and total awake cells (`low` 128 k,
`medium` 512 k, `high` 2 M) and droplet caps — never resolution policy, never existence. Every phase
ends with `tools/perf_harness.py` rows on the rigs; the CPU reference is for correctness, not speed.

---

## 11. Build order (slower, gated, each phase a few sessions)

| Phase | Deliverable | Gate before | Gate after (red first) |
|---|---|---|---|
| **A. Spec & rigs** | This document settled (§12); `WaterBench_Small` with rigs for S1, S2, S5, S7/S10 channel, S8 pond, S9 ledge, S14 troughs; `water_mass_ledger` route; scenario harness `tools/water_feel.py` that runs a scenario, records predictions vs measurements, and captures | — | Rigs verified like the Basin (layer scans, cold restart); harness runs S3 against today's CA and records its failure (no reflected crest, front speed wrong) as the RED |
| **B. The core, CPU reference** | `WaterCore` library: MAC grid, VOF, projection, solids from the micro pool, sources/sinks, rest detection; deterministic; unit tests for §4.4; S3, S4, S5, S1, S2 pass on the CPU at ⅓ res | **/design-check** | §4.4 rules green; S1–S5 predictions met; mass ledger exact |
| **C. GPU core** | Same solver on compute (`water_core_*.comp`), ping-pong, no readback except the surface/queries; parity with CPU on S1–S5 within tolerance; perf rows | /design-check (dispatch, buffers, tiers) | parity + ≤ 2 ms at `high` with all §3 rigs awake |
| **D. Rest, persistence, world data** | AV sleep/write-back to spans and body records; `WaterBodyTable`; edits-never-create-water; span-grid rebuild on residency set (WP1 step 6); hydraulic flood (WP1 step 1b, gated READY); river spans (step 1) | — | S11; camera-walk probe 0 violations on all benches; River trunk rect 17,677 → 0 |
| **E. Coupling** | Moving solids (debris, furniture, characters) two-way; impulses; pump/pipe/scoop/pour/containers | /design-check | S6, S7, S8, S13, S14; drag/buoyancy tables retired on measured parity |
| **F. Rendering the core** | AV surface mesh + shading; droplets; `RippleField` and cell renderer deleted; flat-sea/bake placement deleted | /design-check (aesthetic + camera invariant) | Look sign-off on S6/S7/S9 rest states vs refs; probe clean |
| **G. Large bodies on top** | Shoreline AV band with swell (S12); river reaches; far tiles; weather driver; tall-cell compression | /design-check | S12; WaterRethink WP2/WP6 gates |

Rule for every phase: the previous phase's scenarios stay green (the harness runs them all);
nothing is "done" without its evidence row and a same-vantage capture where look is claimed.

---

## 12. Decisions needed before anything starts

1. **The method** (§4.2): 3-D Eulerian voxel liquid with VOF + pressure projection, droplets as a
   layer, spans as compression. Alternative on the table: FLIP as the primary (better splashes,
   worse rest). Recommendation: Eulerian core, FLIP-style particles only for spray.
2. **Resolution policy** (§4.5): ⅓ voxel for small AVs, 1 voxel for bands, ⅑ reserved. Alternative:
   ⅓ everywhere (simpler, 27× cells on shore bands). Recommendation: as written.
3. **Containers** (§2.3): gameplay volumes with capacity (not simulated inside) that pour as sources
   — or simulate inside buckets at ⅑. Recommendation: gameplay volumes; simulate only on pour.
4. **Shorelines are small-scale** (§8.3): confirm the band-AV model, with the analytic sheet beyond.
5. **Droplet budget**: share the 10 000 GPU particle cap with debris, or a separate pool.
   Recommendation: separate 20 k droplet pool (they are not rigid bodies).
6. **Big lakes recharge** (WaterRethink decision 2, kept): slowly recharging conserved. Confirm the
   rate is a per-world recipe knob.
7. **Delete list** (§9): the CA, the camera window, override stores, `RippleField`, the cell
   renderer. Confirm we blow these away rather than keep them "just in case" (they stay in git).
8. **Phase A scope**: is `WaterBench_Small` (one chunk per rig) the right bench shape, and are the
   §3 rates (pump 0.1 m³/s, bucket 0.02 m³, blast 2 m) the ones you want to see?

---

## 13. Risks and unknowns, stated

- **Free-surface advection without volume drift** is the hard numerical problem; VOF with flux
  limiting is the plan, PLIC reconstruction the fallback. Red test: S3 mass ± 1e-4 over 600 ticks.
- **Surface noise at rest** on an Eulerian grid: rest damping + the sleep criterion; judged by eye on
  the rest-state captures. If it cannot be made clean, the fallback is to snap resting columns to
  the written-back spans (exact flat by construction) and only draw the AV while awake.
- **Pressure solve cost** at ⅓ resolution on large AVs: bounded by `maxCells`; multigrid is the
  upgrade path if Jacobi/red-black misses the 2 ms budget.
- **Two-way coupling stability** with light debris (density 0.1 leaves): clamp the pressure impulse
  per body per tick; the AVBD solver already warm-starts contacts — reuse its bench discipline.
- **Merging and splitting AVs** correctly (mass never lost in a merge): ledger tests on a two-pond
  merge rig before Phase E.
- **Three-way agreement** (C/B/A) under save/load during motion: an awake AV must flush to spans
  before save (or save its cells); decided in Phase D's design-check.
- **Determinism across CPU/GPU**: tolerance-based parity like the debris bench; exact equality is
  not promised.
