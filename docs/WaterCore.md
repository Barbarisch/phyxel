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
| **S3 Dam break** | Basin bench (exists): 3-deep block x 12–18 at the WEST end of the flat floor, released EAST toward the vertical wall (x 29), L = 10 u | Basin | Front reaches the wall at the Ritter speed 2√(g·h₀) ≈ 10.8 m/s → ≈ 0.92 s (frictionless bound; measured must be ≤ 15 % slower); wall crest ≥ still level + 0.8 × incident amplitude; flat within 1 mm after 10 s; mass ± 1e-4. **CA red (2026-10-08): front 6.26 s (ratio 0.15), not flat at 12 s (spread 0.45), mass exact — `docs/evidence/water_feel/S3_basin_ca_*.json`** | Same block, sim disabled: nothing moves | L3 |
| **S4 Breach** | Basin full to 3; dig a 1-wide notch in the east wall 1 above the floor | Basin | Outflow rate through the notch ≈ Torricelli A·√(2g·h) (h = head above notch sill; within 20 % while h > 0.5); level falls accordingly; outside puddle grows by exactly what left | Notch above the surface: no flow | L3 + L4 |
| **S5 Drain hole** | Basin full; dig a 1×1 hole in the floor into a 4×4×3 sealed cavity below | Basin | Water falls into the cavity (3-D path), cavity fills to 48 m³ then the hole surface equalises; basin level drops by 48/area; air trapped? — no (open hole), so it fills completely | Cavity with no hole: stays dry | L3 |
| **S6 Crate splash** | Drop a 1 m³ crate (density 0.6) from 5 m into the full basin | Basin | Splash crown and ring; crate settles floating at 60 % draft (0.6 m submerged) ± 5 %; surface flat again < 8 s; mass unchanged; displaced volume = 0.6 m³ reflected in level rise = 0.6/area | Crate of density 1.6: sinks, rests on the floor, level rise 1.0/area | L3 + L4 |
| **S7 Wading** | Character walks 10 m through 0.8 m water | channel rig (2 wide × 1 deep × 12 long) | Speed drops to the shipped wading factor; bow wave ahead, wake behind (surface deviation ≥ 2 cm measured at 1 m behind); stride splashes; water pushed sideways returns; mass unchanged | Same walk on dry channel: no surface events | L4 (look) + L3 (mass) |
| **S8 Push** | Blast impulse 2 m from a 4×4 pond | pond rig | Water displaced away ≥ 0.3 m on the near side, returns and settles; mass unchanged; droplets (if any) counted in the ledger and re-absorbed | Blast 20 m away: no response | L3 + L4 |
| **S9 Waterfall** | Pump 0.3 m³/s over a 6-high ledge into an empty pit | ledge rig | Falls as a stream (droplet layer), forms a pool that rises until the pit overflows; landing splash/mist; ledger exact including airborne droplets | Pump off: pool stays at its level, no droplets | L4 (look) + L3 |
| **S10 Stream over rocks** | 1-wide channel, 1:20 slope, 3 subcube rocks, inflow 0.05 m³/s at top, outflow sink at bottom (**v1 rig, Phase A: flat channel with full-cube rocks** — the API cannot carve sub-voxel slopes or place subcube rocks yet; the slope and subcube rocks arrive with the Phase E sub-voxel edit path) | channel rig | Steady state within 10 s: inflow = outflow ± 2 %; standing waves at rocks; no accumulation; depth ~ Manning's for the slope (stated per rig) | Remove rocks: flat steady flow, same discharge | L3 + L4 |
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
**Decision (user, 2026-10-08, final form): resolution is a developer-tunable parameter, not a
constant.** The core supports every power-of-three fraction of a voxel — **1, ⅓, ⅑, 1/27, 1/81**
(the fine-item grid, 1.23 cm) — chosen per game in `game.json` (`water.core.cellSize`, **default
⅓**), overridable per active volume (a container may ask for ⅑ or finer; a shore band for 1).
Powers of three only, because the solid mask comes from the micro occupancy pool at ⅑ and the
engine's static tiers are 1/⅓/⅑: a cell size that does not divide those evenly would put a water
cell half inside a microcube. Below ⅑ the solids are still ⅑-resolved (a water cell of 1/27 sees
microcube-aligned walls; fine items at 1/81 are kinematic bodies, not static occupancy — stated so
nobody expects a 1/81 sim to resolve the fluting on a goblet). The solver code is written once,
parameterised by cell size; the scenario harness runs every §3 scenario at ⅓ (default), ⅑ and 1 so
the resolution dependence of each prediction is measured, not assumed, and a game that tunes finer
inherits the same tests.

**Cost is per cell and memory-bound, so the budget — not the knob — decides what fits.** An AV
whose extent at the configured resolution would exceed the tier's `maxCells` is coarsened one
power of three at a time (never finer than the game's floor, never coarser than 1) and the
coarsening is logged, so a developer who sets 1/27 for a 40 m moat learns the cost rather than
getting silent slowness. Mass and existence are identical across resolutions (a re-sampled AV holds
the same mass per column — a pinned test); only motion detail differs — the same contract the
engine's own voxel tiers make. Reference points, pre-run estimates until `perf_harness` rows exist:

| AV extent (longest axis) | Cell | Cells (cube of that extent) | Pressure traffic / tick (40 sweeps × ~16 B) | At 60 Hz |
|---|---|---|---|---|
| ≤ 8 m | **⅑** | 373 k | ~0.24 GB | ~0.25 ms on a 1 TB/s GPU |
| 8–12 m | ⅑ (cap) | 1.26 M | ~0.8 GB | ~0.8 ms — the ⅑ ceiling |
| 12–40 m | ⅓ | ≤ 1.7 M (40 m) | ~1.1 GB | ~1.1 ms |
| > 40 m (shore bands, reaches) | 1 | bounded by `maxCells` | — | — |

| any (1/27, default-⅓ game asking finer per AV) | 1/27 | 27× the ⅑ count | — | only small containers; the budget coarsens anything larger and says so |

At the ⅓ default a 40 m pond is ~1.7 M cells (≈ 1 ms at 60 Hz); at ⅑ the same budget reaches a
12 m pond; at 1, shore bands and reaches. **Multigrid** for the pressure solve (≈ 5× fewer
sweep-equivalents) is the lever that widens every tier; it is planned as the first optimisation
after parity (Phase C), not assumed. Every number here is a pre-run estimate to be replaced by
`perf_harness` rows on the rigs, at each of the three harness resolutions.

### 4.6a Containers are simulated (user decision 3)
A bucket must hold a meaningful amount of water, so **containers are real water**: a container is
an AV at ⅑ bound to the object's frame (a 0.3 m bucket = 3×3×3 cells, 27 cells; a trough 2×1×1 m =
18×9×9 = 1 458 cells). Placed or stationary, it simulates like any AV (fills from a pump, overflows
at the rim, pours when tipped — S1, S2). **Carried**, v1 holds the mass and fill level (no slosh)
and pours as a source when tipped; **carried slosh** (the AV in the item's moving frame with the
inertial force −a_frame added in tick step 3) is a listed feel feature for Phase E once the static
case is right. Capacity is the geometry: a bigger bucket holds more. Test cases: **C1** bucket under
a pump fills to the rim then overflows (ledger: pumped = bucket + overflow); **C2** tipped bucket =
S1; **C3** a carried bucket keeps its mass across a 50 m walk and a save/load; **C4** trough = S2.
**Containers can hold more than their geometry (user, 2026-10-08 — a future gameplay feature).** A
container is therefore two things with one ledger entry: a **physical volume** (the ⅑ AV, what you
see sloshing and what overflows at the rim) and a **reserve** (a logical mass with a capacity knob,
default = 0 extra). Filling: water enters the physical volume; when it reaches the rim, further
inflow goes to the reserve instead of overflowing, until the reserve's capacity is reached, then it
overflows. Pouring: the physical volume empties as a source; the reserve refills it as it drains, so
a "bottomless" bucket pours for as long as its reserve lasts. Mass in the reserve is real mass in
the ledger (P1) — it just has no position until it leaves. Test case **C5**: a bucket with reserve
capacity 1.0 m³ under a 0.1 m³/s pump: rim reached at ~0.2 s, overflow begins only after 10.2 s;
tipped, it pours for ~10 s and the ledger stays exact throughout. The default-zero reserve keeps
ordinary buckets physical; the knob is the gameplay hook (magic vessels, bags, tanks).
Gameplay (what a character can carry) is a later layer on the same mass.

### 4.7 Method primer — what the two candidates are, in plain terms (for decision 1)
Both candidates share **the grid**: the active volume is cut into cells; each cell face carries a
velocity; every tick the solver asks "where does the water want to go under gravity and its own
momentum?", then **fixes the result so water does not compress** (the pressure solve: the water
in a sealed bucket cannot shrink, so pushing on it pushes the whole column, which is what makes a
breach jet and a slosh behave). Solids are cells water cannot enter. That grid is 80 % of the work
and it is identical in both candidates. They differ only in **how the water's position is tracked**:

- **Eulerian (fill fractions, VOF).** Each cell stores how full it is, 0..1. Water moves by
  transferring fractions between neighbours along the velocities. Strengths: exact accounting (a
  number per cell, trivially summed), a surface that rests perfectly flat and still, cheap, no
  particle budget. Weakness: thin sheets and droplets smear (a splash becomes a lump unless the
  grid is fine, which at ⅑ it is), and the surface position inside a cell has to be reconstructed.
- **FLIP (particles carry the water).** The water is tens of thousands of particles that carry
  mass and momentum; the grid is used once per tick to enforce incompressibility, then velocities
  go back to the particles. Strengths: splashes, crowns, sheets and droplets come for free, nothing
  is smeared, feel is excellent while water moves. Weaknesses: resting water is noisy (particles
  jostle; the surface never goes perfectly flat without extra machinery), cost scales with the
  number of particles (~8 per cell: a ⅑-resolution pond is millions), and persistence means
  converting particles back to spans anyway.

**Decision (user, 2026-10-08): both.** The core is built so the water's transport is a
**selectable mode behind one interface**: `WaterCore` owns the grid, the solids, the sources/sinks,
the pressure solve, rest detection and write-back (the shared 80 %); a `SurfaceTransport` strategy
owns how water position moves — `Eulerian` (fill fractions) is the default and is built first
(Phase B); `FLIP` (particle transport) is built second against the same grid (Phase B2, after S1–S5
are green on Eulerian) and must pass the same scenarios. Per-AV selection is allowed (a trough on
Eulerian, a waterfall plunge pool on FLIP) because mass accounting is identical through the
interface (P1). The harness runs every scenario on both so the comparison is evidence, not taste.

**Why Eulerian is the default and goes first:** at ⅑ resolution the Eulerian
surface is fine enough that smearing is below what the eye sees in a bucket or a trough, it rests
exactly flat (P7), and the accounting is the ledger by construction (P1); the one thing it does
badly — airborne water — is exactly what a bounded particle layer does well. The reverse (FLIP as
the primary) spends its budget on the resting state we do not want to simulate. **The decision is
reversible at low cost:** both run on the same grid and pressure solve, so if the Eulerian free
surface disappoints on S6/S9 after Phase B, FLIP's particle transport is an addition, not a
rewrite. That is also the proposed way to decide: Phase B builds the grid and the Eulerian
surface; S3–S5 are the judge; the user watches S6 and S9 captures before Phase E.

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
- **An AV lives inside chunk residency and the occupancy window** (gate §14 finding). Solids come
  from `PackedOccupancyPool`, a 32×32 chunk-column directory recentred on the viewer
  (`solver_shared.h:79`), and spans can only be written to resident chunks. So an AV box is clipped
  to the intersection of the resident chunk set and the occupancy window; a face that would cross
  that boundary is a **hold boundary** (no flux, like the debris solver's `SS_FROZEN_UNKNOWN`:
  bodies are HELD when a contact sample needs occupancy the pool lacks, `solver_shared.h:155`).
  Water never flows into ground it cannot see. This bounds where MOTION happens by residency —
  the rule terrain already obeys — and never where water EXISTS (spans/bodies). When residency
  grows, the hold boundary moves and the water resumes; a pinned test asserts the result is
  identical to having had the larger residency from the start (§14 test 2).

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
reservoir flag (mass unbounded, level fixed). **Lakes are conserved bodies with a water balance**
(user decision 6): mass changes by inflow (the FlowField's accumulated discharge into the lake's
cells × a rainfall rate), minus outflow at the outlet while above the spill, minus evaporation
(area × a climate rate from the biome moisture field). So a lake in a dry climate can **dry up**,
and a wet one overflows — an emergent world-generation feature rather than a knob. Rates need
grounding before Phase G (a simple P − E balance; Thornthwaite-class evaporation from temperature;
rainfall from the climate field) — listed as Phase G grounding work. Ponds are finite. **Swell** (S12) is a prescribed surface motion on an ocean face: the Gerstner field's height
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

## 9. What exists: keep, re-point, replace, delete — THE DELETE LEDGER

User decision 7: delete rather than keep dead code, and be very careful. Rules: (1) nothing is
deleted until its replacement has passed the scenarios named in its row; (2) every deletion is one
commit, listing the files and the consumers it re-pointed, so it is one revert away; (3) the
reference tests that pin a behaviour we still want are MOVED to the new core before the old file
goes; (4) everything stays in git history (`git show <hash>:<path>`). Sizes are today's line counts.

| Component | Files (lines) | Consumers today | Replacement | Delete when | Risk / care |
|---|---|---|---|---|---|
| **`WaterSimulation`** — the mass CA (gravity split, 0.25 horizontal rate, compression, MIN_HOLD, evaporation, sources/pins, channel mask, flow proxy) + **`water_flow.comp`** (the opt-in GPU port) | `engine/{include,src}/core/WaterSimulation.{h,cpp}` (290 + 588), `shaders/water_flow.comp` (111, + .spv), `tests/core/WaterSimulationTest.cpp` (986, 38 tests) | `WaterManager` only; tests | `WaterCore` solver (Phase B/C) | **After Phase B** passes S1–S5 on the CPU reference. The 38 tests are triaged first: conservation / no-leak / basin-leveling / sealed-pit tests are re-expressed against `WaterCore` (they pin behaviour we keep); the CA-specific ones (MIN_HOLD donor gate, momentum bias, evaporation thresholds, GPU parity of the CA) go with the file | The `enableGpu` resources are allocated at boot unconditionally (WaterManager.cpp:1248) — remove with the manager, not before |
| **`WaterManager`** — the 64×32×64 camera-following window, recentre/shift, `rebuildOcean` + shoreline snap + `fillWaterTable`, river pins (`applyRiverInflows`), `kinematicRiverFlow`, overrides store (`water_overrides`), outflow bank, finite-body deltas, `rebuildSurface` (cell surface build), waterfall lips/mist, `sampleWater`/`columnWater`/`flowAtWorld`/`submergedFraction` | `engine/{include,src}/core/WaterManager.{h,cpp}` (465 + 1 328), `tests/core/WaterManagerTest.cpp` (1 461, 55 tests), `tests/core/WaterBuoyancyTest.cpp` (147) | `Application` (construction, `followTo`/`update`, 30 debug commands, game.json wiring 6417–6687, save/load of `water_overrides` at 6682/16785), `RenderCoordinator` (cell surface, submergence, hydro upload), `DebrisRuntime` (`columnWater` tiles), `VoxelDynamicsWorld` (query callbacks), `AnimatedVoxelCharacter` + `NPCManager` (`WaterHooks`), `EngineAPIServer` (routes) | `WaterCore` AV manager (lifecycle §5) exposing the SAME query names (`sampleWater`, `columnWater`, `flowAtWorld`, `submergedFraction`) so consumers re-point by one include; Tier A/B replace the three stores; the debug commands are rewritten over the AV manager one by one | **After Phase D** (rest/persistence/world data) — the last consumer to move is `DebrisRuntime`'s tile build (Phase E). The `water_overrides` world_meta key is read once by a migration (writes body records) and then ignored | Largest blast radius in the engine. Consumers are re-pointed in Phase D/E commits BEFORE the file is removed; `WaterManagerTest`'s 55 tests are triaged like the CA's (query semantics kept: "+1 cell over naive seaLevel−y" fill semantic, Water.md §… memory, is a candidate to KEEP and re-pin) |
| **`RippleField`** — 128² half-voxel damped wave field, `addRipple`, player-following | `engine/{include,src}/core/RippleField.{h,cpp}` (91 + 161), `tests/core/RippleFieldTest.cpp` (117) | `WaterManager` (tick, follow), `WaterCellRenderPipeline` (R32F texture, set-1 b2), `AnimatedVoxelCharacter` via `WaterHooks.addRipple`, `water_ripple` debug route + MCP tool | The AV surface itself (a wake, a splash ring and a footfall ripple are height in the simulated surface) | **After Phase F** (the AV surface mesh renders and S7's wake is judged) | The `water_ripple` MCP tool and route are deleted in the same commit (an orphan tool that 404s is worse than none). `WaterHooks.addRipple` becomes "add impulse" on the AV |
| **`WaterCellRenderPipeline`** — instanced 30-vertex cell quads + skirts, per-draw host-coherent instance buffer, ripple sampling | `engine/{include,src}/graphics/WaterCellRenderPipeline.{h,cpp}` (100 + 515), `shaders/water_cell.{vert,frag}` (106 + 138, + .spv) | `RenderCoordinator` (creation, `WaterCells` scope, `rebuildSurface` feed), `water_cell_render` A/B route | AV surface mesh pipeline (Phase F) | **After Phase F** sign-off on the rest-state captures | Delete the two `.spv` and update `tools/shader_manifest.py`'s list in the same commit (the manifest check fails otherwise) |
| **Flat-sea mode and bake-as-placement** in `WaterRenderPipeline` / `RenderCoordinator` (`invCellSize == 0`, the `buildHydroUpload` placement upload at RC.cpp:3395–3419, the `m_lastHydroUploaded` reset hazard in `setWaterLook`/`setWindSpeed`/`setWaves`) | parts of `RenderCoordinator.cpp` (~60 lines), `water.vert`/`water.frag` branches (`basinLevelAt`), `WaterRenderPipeline` sentinel upload | the sheet draw; `water_look`/`water_waves` routes | Span placement only (grounded mode becomes the only mode); look changes re-pack G/B/A into the span grid | **Phase D** (with WP1 step 5/6) — the camera-walk probe self-test must then be re-pointed at a different injectable violation, or retired with a note | This is the "universal water level" the user's rule forbids; its removal is a rule, not a cleanup. `water.enabled` semantics change (pinned tests updated) |
| **`kinematicRiverFlow`**, river pins, `setRiverQuery`/`setRiverOrderQuery`/`setRiverFlowQuery` bindings | inside `WaterManager` + `Application.cpp:6615–6656` | `Application`, `WaterManagerTest` | River reaches as bodies with discharge; real transport inside AVs (Phase G) | with `WaterManager` | None beyond the manager's |
| **Debris water tiles' source** (`DebrisRuntime::updateWater` reading `WaterManager::columnWater`) | `engine/src/core/DebrisRuntime.cpp:157–232` (kept), `shaders/solver_shared.h` tile format (kept) | GPU debris solver (`solver_integrate.comp` buoyancy/drag/current — kept) | Same tiles fed from the AV surface + velocity (Phase E) | not deleted — re-pointed | The Phase 6c bench (`tools/debris_settle_bench.py`) is the regression gate for the re-point |
| **Keep unchanged:** `Chunk::WaterSpanLocal` + codec v2 + `waterSpansForBlock`; `HydrologyMap`/`FlowField`/`PriorityFlood`/`WaterBodyIndex` (gain `filledAt`, outlets); `WaterOccupancy` (+1b); `WaterProfile`; `WaterRenderPipeline` sheet + `water_common.glsl` + `water_underwater.frag`; `SeaMesh`; buoyancy/drag/wading **interfaces** (`setWaterQuery`, `WaterHooks`); the 30 routes + 13 MCP tools + `water_render_grid` + probes (extended with `water_mass_ledger`, `water_av_list`, `water_av_probe`); benches + references | — | — | — | — | Drag/buoyancy *tables* (VoxelDynamicsWorld.cpp:381–412 constants, `buoyancy` 1.6 default) retire only when S6 passes with integrated forces |

**Order of removal, for the record:** CA + `water_flow.comp` (after B) → flat-sea/bake placement
(D) → `WaterManager` (after D/E) → `RippleField` and the cell renderer (after F). Each is its own
commit, each names its consumers, each leaves the benches and the camera probe green.

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
| **A. Spec & rigs** — **DONE 2026-10-08** | §12 settled; `WaterBench_Small` (port 8111) with 7 one-chunk rigs (pad S1 · trough S2/C4 · channel S7+S10-v1 · pond S8/S13 · ledge S9 · pipes S14 · bucket C1–C5) authored from a generic rigs spec, verified by layer scans (7/7, dry), reference captures in `docs/evidence/water_v4_refs/small_*.png`; routes `water_ledger`, `water_probe_rect`, `place_water_box`; harness `tools/water_feel.py` (S3 implemented; the others are added as the core makes them runnable) | — | **RED recorded:** `docs/evidence/water_feel/S3_basin_ca_20261008_121725.json` — the CA's front reaches the wall in 6.26 s vs Ritter 0.92 s (ratio 0.147), wall crest 13.91 vs still level 14.24 (no reflection at all), surface spread 0.42 after 15 s (not flat), mass drift −6e-5 (conserved). The first run's crest metric read the draining block as a crest (false pass) and was fixed before the row was kept. |
| **B. The core, CPU reference** | `WaterCore` library: MAC grid, `SurfaceTransport::Eulerian` (VOF), projection, solids from the micro pool, sources/sinks, rest detection; deterministic; unit tests for §4.4; S3, S4, S5, S1, S2 pass on the CPU at ⅓ (default), ⅑ and 1 | **/design-check** | §4.4 rules green; S1–S5 predictions met; mass ledger exact |
| **B2. FLIP transport** | `SurfaceTransport::FLIP` against the same grid (particles carry f and momentum; grid projection; particle↔grid transfer; re-seeding; rest conversion); harness runs S1–S5 on both modes and records the comparison | /design-check | Same scenarios green on FLIP; a written comparison (rest flatness, splash shape, cost) the user reads before Phase E chooses the default per scenario |
| **C. GPU core** | Same solver on compute (`water_core_*.comp`), ping-pong, no readback except the surface/queries; parity with CPU on S1–S5 within tolerance; perf rows | /design-check (dispatch, buffers, tiers) | parity + ≤ 2 ms at `high` with all §3 rigs awake |
| **D. Rest, persistence, world data** | AV sleep/write-back to spans and body records; `WaterBodyTable`; edits-never-create-water; span-grid rebuild on residency set (WP1 step 6); hydraulic flood (WP1 step 1b, gated READY); river spans (step 1) | — | S11; camera-walk probe 0 violations on all benches; River trunk rect 17,677 → 0 |
| **E. Coupling** | Moving solids (debris, furniture, characters) two-way; impulses; pump/pipe/scoop/pour/containers | /design-check | S6, S7, S8, S13, S14; drag/buoyancy tables retired on measured parity |
| **F. Rendering the core** | AV surface mesh + shading; droplets; `RippleField` and cell renderer deleted; flat-sea/bake placement deleted | /design-check (aesthetic + camera invariant) | Look sign-off on S6/S7/S9 rest states vs refs; probe clean |
| **G. Large bodies on top** | Shoreline AV band with swell (S12); river reaches; far tiles; weather driver; tall-cell compression | /design-check | S12; WaterRethink WP2/WP6 gates |

Rule for every phase: the previous phase's scenarios stay green (the harness runs them all);
nothing is "done" without its evidence row and a same-vantage capture where look is claimed.

---

## 12. Decisions — status after the 2026-10-08 round

| # | Decision | Status |
|---|---|---|
| 1 | **The method** | **SETTLED: both, Eulerian first.** One core, a selectable transport mode (`Eulerian` default, built in Phase B; `FLIP` built in Phase B2 against the same grid), same scenarios run on both, per-AV selection allowed (§4.7). |
| 2 | **Resolution** | **SETTLED: a developer-tunable parameter.** Any power-of-three fraction of a voxel from 1 to 1/81, `game.json` `water.core.cellSize` default **⅓**, per-AV override; the cell budget (not the knob) coarsens oversize volumes and logs it; the harness runs every scenario at ⅓, ⅑ and 1 (§4.5). |
| 3 | **Containers** | **SETTLED: simulated, with a reserve** (§4.6a). Static/placed containers are ⅑ AVs; a container may hold more than its geometry through a logical reserve with a capacity knob (default 0) — the future gameplay hook; carried v1 holds mass and pours; carried slosh is a Phase E feel item. Test cases C1–C5. |
| 4 | **Shorelines** | **SETTLED: small-scale bands on the ocean boundary** (§5.3, §8.3). |
| 5 | **Droplets** | **SETTLED: separate pool**, 20 k, ledger-exact re-absorption. |
| 6 | **Lakes** | **SETTLED: conserved bodies with a water balance** — inflow from the river network, outflow at the spill, evaporation from climate; lakes can dry up or overflow as an emergent world feature (§5.3). Grounding of the rates is Phase G work. |
| 7 | **Delete list** | **SETTLED: delete, carefully.** §9 is now a ledger with files, consumers, replacement, the phase after which each goes, and the risk — one commit per removal, nothing removed before its replacement passes its scenarios. |
| 8 | **Rigs** | **SETTLED:** `WaterBench_Small`, one chunk per rig; the §3 rates stand until a rig says otherwise. |

**All eight decisions are settled (2026-10-08).** Phase A may start on the user's go; Phase B
opens with its design-check.

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

---

## 15. Phase B design — the `WaterCore` CPU reference (gated 2026-10-08, READY)

### 15.1 Shape
- **Library:** `engine/{include,src}/core/water/` — `WaterGrid` (one active volume's arrays),
  `WaterSolver` (the tick), `IWaterTransport` + `EulerianTransport` (Phase B) / `FlipTransport`
  (B2), `WaterSources` (sources/sinks/impulses), `WaterRest` (sleep detection). No engine
  dependencies beyond glm: solids arrive through a `SolidQuery` callback
  (`std::function<OccupancyState(const glm::ivec3& cellWorld)>` returning Solid / Air / Unknown),
  so unit tests use synthetic lambdas and the engine binds `packedPoolSolidAt` + the pool's
  `OccupancyState` (VoxelLightOccupancy.h:253-258) through a new `occupancyPool()` accessor.
- **Grid (SoA, one AV):** cell size `h = 1/3^k` m (k from `water.core.cellSize`, 1 ≤ 3^k ≤ 81);
  box origin in world cells (`ivec3`, cells of size h, world = origin·h); dims `nx,ny,nz`; index
  `i = x + nx·(y + ny·z)`. Arrays: `f` (fill 0..1, float), `u,v,w` MAC face velocities (float,
  sized `(nx+1)·ny·nz` etc.), `solid` (uint8: Air/Solid/Hold), `p` (double for the solve),
  `div`, `fNext` (ping-pong), `kind` (Liquid f ≥ 0.999 / Surface 0 < f < 0.999 / Empty).
  Memory: 1.7 M cells ≈ 7 floats × 4 B + 8 B ≈ 61 MB at the ⅓ 40 m ceiling — fine.
- **Tick (fixed Δt = 1/60 s, substeps so max|v|·Δt_sub ≤ 0.5 h — the count is computed and
  reported, never discovered):** (1) solids refresh (every tick; Unknown → Hold, which is a wall
  for flux) · (2) sources/sinks (`f += rate·Δt/h³`, clamped to free capacity; the unplaced
  remainder is kept in the source's own buffer and reported) · (3) gravity `v -= g Δt`, impulses ·
  (4) **transport**: `EulerianTransport` = conservative donor-cell VOF: per face, flux
  `F = u_face · h² · Δt · f_donor`; outflows of a cell are scaled so Σoutflow ≤ f_cell·h³ (the CA's
  clamp rule, which is what makes Σf exact), inflows take what was actually sent; velocity by
  semi-Lagrangian back-trace with clamping · (5) **projection**: solve ∇·(1/ρ ∇p) = ∇·u/Δt on
  Liquid cells; Surface cells p = 0 (ghost-fluid weighted by f so the free surface sits inside the
  cell); Solid/Hold faces: Neumann (zero normal velocity). CPU: PCG with incomplete-Cholesky(0),
  deterministic ordering, tolerance 1e-6 relative, max 400 iterations (reported) · (6) velocity
  extrapolation into Empty cells within 2 cells of Surface · (7) rest damping (`v *= 1 − c Δt`
  only when KE < ε_wake, so moving water is never damped) · (8) rest detection (§5.2) · (9) surface
  extraction: per column, the top Surface cell's y + f, plus lateral faces where a Surface cell
  has an Empty neighbour sideways.
- **Engine integration (Phase B minimum):** `WaterCoreManager` owned by `Application`, stepped in
  the existing "Water" profile scope (Application.cpp:3550) beside the CA (both exist until the CA's
  §9 row); one or more AVs created by **debug routes** (§15.4) over a rig box; solids from the pool
  via `occupancyPool()`; **eyes-on without new rendering**: the AV's surface is aggregated to
  1-voxel columns into `WaterSurfaceCell`s (WaterManager.h:23-31) and appended to the cell renderer's
  list on authored worlds (Basin/Small have no table, so cells draw) — a debug feed, replaced in
  Phase F.
- **Time base:** the CPU reference is **stepped explicitly** by the harness (`water_av_step
  {ticks}`) and every Phase B measurement is in **simulation time** (ticks × Δt), never wall time —
  at ⅓ on the Basin (≈ 104 k cells) PCG costs tens of ms per tick on one core, so real-time is not
  promised for the reference and is not what it is for. (The CA red used wall time because the CA
  runs real-time; the rows say which.)

### 15.2 Unit tests (red first, all synthetic, inside one grid — the §4.4 rules made executable)
`WaterCoreTest.ConservationUnderArbitraryVelocity` (random divergence-free u, 1000 ticks, Σf
drift ≤ 1e-4) · `StillWaterStaysStill` (hydrostatic column, 100 ticks, max|v| < 1e-6, surface
unchanged) · `HydrostaticPressure` (p at depth d = ρ g d ± 1 %) · `SealedCavityGainsNothing` ·
`OpenHoleDrainsAtTorricelli` (tank 10×10 h, hole 1 cell: h(t) = (√h₀ − (A_h/A_t)√(g/2)·t)² ±
20 %) · `SolidFacesCarryNoFlux` (every face of a Solid cell, every resolution 1/⅓/⅑) ·
`HoldFacesCarryNoFluxAndRelease` (§14 test 3) · `SubstepCountMatchesCFL` · `Deterministic` (two
runs bit-identical) · `MassPerColumnInvariantAcrossResolution` (⅓ grid re-sampled to 1: same mass
per voxel column) · `SubBoxIdenticalToWholeBox` (the §14 residency test's solver form: a 2-cell
margin vs a 10-cell margin, identical interior) · `DamBreakFrontWithinRitter` (the solver-only S3:
1-D-like channel 60×6×3 cells at h = 1, h₀ = 3, front ≤ 15 % slower than 2√(g h₀)) ·
`WallCrestReflects` (after the front hits the wall, the wall column peaks ≥ still + 0.8·incident).

### 15.3 Scenario gates (engine, via the harness, sim time)
S3, S4, S5 on the Basin at h = 1 and ⅓ (⅑ would be 2.8 M cells on the CPU reference — the
budget coarsens it and the row must say so); S1, S2 on the Small bench at ⅓ and ⅑. Controls as in
§3. Predictions as in §3; the CA red row is the comparison.

### 15.4 Debug API (Phase B; the `/api/water/*` namespace arrives with Phase D)
| Route | Fields | Unchanged | Echo | Clamp |
|---|---|---|---|---|
| `water_av_create` | `x1,y1,z1,x2,y2,z2` (world voxels), `cellSize` (1, 1/3, 1/9), `transport` | — | the AV record (id, box, cells, cellSize) | cells ≤ 2 M (CPU reference memory/time; refused with the count); `cellSize` ∈ the power-of-three set (refused otherwise) |
| `water_av_destroy` | `id` | — | remaining list | — |
| `water_av_step` | `id`, `ticks` (int), `dt` (default 1/60) | — | ticks run, substeps used, PCG iterations (max/mean), KE, Σf | `ticks` ≤ 6000 per call (100 s of sim; the game loop blocks for the call — stated) |
| `water_av_list` / `water_av_probe` | as §14.4 | — | — | — |
| `water_ledger` | gains `core_cells` | — | — | — |
| `place_water_box` / `water_probe_rect` | gain `target: "ca"\|"core"` (default `ca` until the CA's §9 row) | omitted = ca | echoes target | — |
No shipped default changes in Phase B.

### 15.5 Chunk independence, restated for the solver
The solver reads nothing chunk-shaped: a box in world cells, a solid query by world position, and
sources by world position. Hold faces (Unknown occupancy) are the only residency effect and are a
flux wall, not a shape (§14 tests 2–3). The debug feed to the cell renderer aggregates by world
column. Equality test: `SubBoxIdenticalToWholeBox` (§15.2) plus the §14 three.

### 15.6 Rig vs shipped defaults
CPU, explicit stepping, tier `high`, no coarsening on the Basin at 1 and ⅓; the Basin is a Flat
world with no bake and water disabled, so no table, no sea, no spans interact. Numbers from this
phase are correctness numbers; none is a performance claim.

**Verdict: READY** — the first commit of Phase B is the failing `WaterCoreTest` suite.

### 15.7 Phase B ledger — the CPU reference, red → green (2026-10-08)

Red commit `af1ac49d`: 11 of 13 tests failed against the strawman (gravity + naive flux). Green:
**15 of 15** after seven iterations, every one driven by a measured failure, not a guess. Each
defect below is now a comment at the line that fixes it, because each is the kind of thing a
later "simplification" would silently reintroduce:

| # | Symptom (measured) | Cause | Fix |
|---|---|---|---|
| 1 | Still water kept g·Δt of velocity; hydrostatic pressure carried a +4.5 Pa offset; nothing flowed | The pressure→velocity update at free-surface faces used a different distance than the matrix row (×2) — an inconsistent projection | One `thetaToAir()` for the row and the update |
| 2 | 832 → 825.5 m³ in 1 000 ticks; cavity lost 52 m³; a drain "finished" in 11 s | A two-pass scaled limiter let a receiver assume its own outflow, then a final clamp destroyed the overshoot | Sequential face application: one amount per face = min(desired, donor has, receiver can take), no clamp anywhere |
| 3 | Dam-break front at a fifth of Ritter | Only f ≥ 0.999 cells in the pressure domain; the collapsing front felt no horizontal gradient | Domain = cell centre submerged (f ≥ 0.5); filling still works because donor-cell flux carries the donor's fill |
| 4 | 80–130 Pa pressure spikes each time a cell joined the domain; the front stalled per cell | Gravity accumulated on faces of thin (non-domain) cells that nothing projected or reset | `settleThinFilmTopFaces()`: a thin cell's upper face takes its lower face's velocity (rest on floor = 0, falling drop keeps falling) |
| 5 | Fine-grid tongue stopped dead at the 0.5 m contour; tip faces at 0.35 m/s beside a 5 m/s front | "Known" faces for extrapolation included thin-film faces, which were never projected — orphans with stale values | The DOMAIN dictates its 3-cell halo (air and thin faces alike); unreached thin water keeps its own motion |
| 6 | Tongue cells all reported the same surface height | Ghost-fluid distance 0.5 for every partially filled cell with air above | Surface inside the cell: θ = clamp(f − 0.5, 0.1, 0.5); full cells unchanged |
| 7 | Test predictions themselves | Ritter's dry tip vs the resolvable contour; thin-plate orifice vs a 1-cell short tube; a convergence test at 1/3 | Front measured at the d = 0.5 h contour (10.85 − 3√(g·d)); c_d 0.8; `DamBreakFrontConvergesWithResolution` |

What the reference solver now does, measured: conservation to 1e-4 over 1 000 ticks of random
flow; still water < 1e-6 m/s; hydrostatic pressure within 1 %; sealed cavity exactly dry;
Torricelli drain (c_d 0.8) within 20 %; zero flux through solid and hold faces at 1, ⅓, ⅑;
hold releases with mass intact; CFL substeps as stated; bit-deterministic; mass per column
invariant across resolution; 2-cell vs 10-cell margin identical; dam-break front ≥ 85 % of
Ritter at the resolvable contour at both h = 1 and h = ⅓ (the fine run accelerates to 9.3 m/s
toward the 10.85 tip); wall crest above still + 0.8 × incident. Scenario gates S1–S5 on the
benches are the next step (Phase B engine integration, §15.1).

Full unit suite from the repo root after the green commit (Release): **4 138 / 4 160 pass**; the
two failures are `FineFaceMerge` and `AtlasManagerTest` cases that touch no file this work changed
(last modified 2026-09) and are not water; a run of the same binary from a scratch directory
reports hundreds of failures because resource-loading tests resolve paths from the working
directory — run the suite from the repo root, as `build_and_test.ps1` does.

## 14. Feature Design Keys gate on this design (run 2026-10-08, before Phase A)

**Verdict: NEEDS WORK → fixed in this revision → READY for Phase A.** Phase B, C, E, F and G keep
their own gates (§11). Four gaps were real and are closed below; the rest holds.

### 14.1 Voxel aesthetic
Water is the engine's standing exception to the cubic look: the AV surface is a smooth sub-cell
height field, as the sea sheet and cell tops already are. **Gap found: droplets.** The droplet
layer had no stated idiom. Decision: droplets render as **water-shaded microcubes** (the engine's
particle idiom, the size of one AV cell at ⅑ or the configured cell), not smooth sprites — in a
voxel world a splash is a burst of tiny cubes, and that matches debris. Detail is unconditional:
resolution is an authoring parameter of the game, not a runtime quality tier; the budget
coarsening (§4.5) is a cost gate that changes motion detail only, is logged, and must never fire
on the shipped-default §3 rigs (asserted by the harness: `coarsened == 0` on every scenario row).

### 14.2 Chunk independence — every chunk-derived quantity
| Quantity | Derived from | Affects | Ruling |
|---|---|---|---|
| AV box | connected water around the trigger (world positions) | where motion is simulated | OK — world-derived, camera-independent |
| **Solid mask** | `PackedOccupancyPool`, 32×32 chunk columns recentred on the VIEWER | behaviour | **Was a hidden camera dependence** (water would flow into unknown ground differently depending on where the viewer stood). Fixed (§5.1): AVs are clipped to the resident ∩ occupancy window, unknown ground is a hold boundary. Motion is bounded by residency (like terrain); existence is not. |
| Span write-back target | chunk residency | persistence | OK by the same rule: an AV never extends over non-resident chunks, so every column it writes is resident |
| Span storage per chunk; vertical chunk clipping of tall columns | chunk identity | storage | OK (Tier B, exists, pinned) |
| Span render grid bounds + rebuild trigger | resident set | coverage | the count-vs-set defect (WaterRethink WP1 step 6) is fixed in Phase D |
| Debris water tiles directory | same 32×32 window | debris coupling | OK — already residency-bounded by the debris solver's own design |
| Cell budget per tier | tier | cost only | OK; coarsening logged, never on default rigs |

No cross-chunk lookup is introduced: an AV reads world positions and the pool. **Equality tests
(must exist before each phase ships):** (1) `WaterCoreSeamTest.WriteBackIdenticalAcrossChunkSeams`
— an AV straddling a chunk boundary writes column tops that are identical on both sides and equal
to a whole-region reference (two bodies at different levels in the fixture); (2)
`WaterCoreResidencyTest.LargerResidencyChangesNothingInsideTheBox` — the same scenario with the
resident set grown by one chunk ring produces bit-identical cells inside the original box (the
hold boundary is a cost bound, not a shape); (3) `WaterCoreSolidsTest.UnknownOccupancyHolds` — a
column whose pool entry is absent is a wall for flux this tick and releases exactly when the entry
appears, mass unchanged.

### 14.3 Procedural generation
The core is runtime; it touches generation only through Tier B/A (spans and bodies, hydrology
stage, already gated in WaterRethink §8.8). `water.core.cellSize` is a **game** setting (game.json),
not world recipe, because spans store float tops independent of cell size, so a world generated
under one resolution reloads correctly under another (pinned: write-back → reload at a different
cell size → same mass per column). Lake water-balance rates (Phase G) are recipe fields. Order-
independence of the runtime does not arise (it is a time integration, deterministic per the CPU
reference); of generation it is inherited.

### 14.4 API surface — the Phase A/B routes, specified now
| Route | Fields (units) | Unchanged | Echo | Clamp (and why) |
|---|---|---|---|---|
| `GET /api/water/ledger` | — | — | `cells` (Σ awake AV mass, m³), `spans` (Σ Tier B), `bodies` (Σ Tier A), `reserves` (containers), `droplets`, `total`, `sources_since_boot`, `sinks_since_boot`, `drift` (= total − initial − sources + sinks) | — (a read; `drift` is the invariant, expected 0 ± 1e-4) |
| `POST /api/water/core` | `cellSize` (fraction of a voxel: 1, 1/3, 1/9, 1/27, 1/81), `transport` (`eulerian`\|`flip`), `hz` (ticks/s), `maxCellsPerAv`, `maxCellsTotal`, `dropletCap` | omitted | full config in effect | `cellSize` ∈ the power-of-three set (a non-divisor puts a cell half inside a microcube — refused, not rounded); `hz` ∈ [20, 120] (CFL substeps derive from it); `maxCells*` ≤ tier ceiling (GPU buffers are sized at tier allocation); `dropletCap` ≤ pool size |
| `GET /api/water/av` | — | — | list: id, box (world voxels), cellSize, transport, cells, awake/asleep, KE, mass, resolution-coarsened flag, hold faces | — |
| `POST /api/water/av/probe` | `x,y,z` (world) or `x,z` (surface) | — | `f`, velocity (m/s), surface Y at the column, body id, pressure (Pa) | — |
| `POST /api/water/source` | `x,y,z`, `rate` (m³/s, negative = sink), `id` | omitted | the source record | `|rate|` ≤ 10 m³/s per source (above it a single cell cannot accept the inflow per tick at ⅑ and the solver would mint pressure — the limit is derived from cell volume × hz and stated in the response) |
| `POST /api/water/impulse` | `x,y,z`, `radius` (m), `strength` (m/s added) | — | affected cells | `strength` ≤ 20 m/s (CFL at the finest cell and `hz`) |
| `POST /api/water/container` | `id`, `reserveCapacity` (m³) | omitted | the container record (physical mass, reserve mass, capacity) | `reserveCapacity` ≥ 0 |
Defaults: none of today's defaults change in Phase A/B (the CA keeps running until its §9 row);
`water.enabled` semantics change in Phase D with the flat-sea deletion (WaterRethink §8.4).

### 14.5 Visual test plan — measurement primitives (gap found: §3 stated predictions but not
how each number is read)
- **Surface height at a column:** `water/av/probe {x,z}` → surface Y (sub-cell, from f).
- **Front position (S3):** per tick, the first column along +x with f > 0.5 at the floor layer;
  speed = Δx/Δt over the 10 u run; compared to Ritter as a ratio.
- **Flow rate (S4, S10, S14):** `ledger` deltas per second on the two sides of the notch/pipe,
  plus the sink's own counter; compared to Torricelli/inflow as a ratio.
- **Draft (S6):** the floating body's y from `get_entity` minus the surface Y at its column.
- **Rest (S11):** max |surface Y − written span top| over the AV's columns after sleep; AV cost
  from `gpu_timing` scope `WaterCore` = 0 when asleep.
- **Look (S6, S7, S9, S12):** `water_bench.py refshots` at the rig's pinned vantage vs the
  reference; user sign-off recorded in the evidence row.
- **Controls** are in every §3 row; the harness refuses to record a row without its control.
Rigs run at the shipped default (⅓) plus ⅑ and 1; the rig deltas from shipped defaults are: no
hydrology bake (Small bench is Flat with no world block), fixed 60 Hz, tier `high` — all stated in
each evidence row.

**Red test for Phase A (the first thing built):** the harness runs S3 against today's CA on the
Basin bench and records: no reflected crest (the CA has no momentum), front speed far below Ritter,
and the sloshing-vs-settling profile — the baseline every later row is compared to.
