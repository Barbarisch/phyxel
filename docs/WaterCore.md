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
| **DELETED 2026-10-09 (Phase D5, §16.9)** — Flat-sea mode and bake-as-placement in `WaterRenderPipeline` / `RenderCoordinator` (`invCellSize == 0`, the `buildHydroUpload` placement upload at RC.cpp:3395–3419, the `m_lastHydroUploaded` reset hazard in `setWaterLook`/`setWindSpeed`/`setWaves`) | parts of `RenderCoordinator.cpp` (~60 lines), `water.vert`/`water.frag` branches (`basinLevelAt`), `WaterRenderPipeline` sentinel upload | the sheet draw; `water_look`/`water_waves` routes | Span placement only (grounded mode becomes the only mode); look changes re-pack G/B/A into the span grid | **Phase D** (with WP1 step 5/6) — the camera-walk probe self-test must then be re-pointed at a different injectable violation, or retired with a note | This is the "universal water level" the user's rule forbids; its removal is a rule, not a cleanup. `water.enabled` semantics change (pinned tests updated) |
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
| **B. The core, CPU reference** — **BUILT + IN THE ENGINE 2026-10-08** (ledgers §15.7–15.8; S5 4/4, S2 4/4, S1 3/4, S4 1 m PASS / ⅓ OPEN, S3 front PASS / run-up + rest gates to re-base) | `WaterCore` library: MAC grid, `SurfaceTransport::Eulerian` (VOF), projection, solids from the micro pool, sources/sinks, rest detection; deterministic; unit tests for §4.4; S3, S4, S5, S1, S2 pass on the CPU at ⅓ (default), ⅑ and 1 | **/design-check** | §4.4 rules green; S1–S5 predictions met; mass ledger exact |
| **B2. FLIP transport** | `SurfaceTransport::FLIP` against the same grid (particles carry f and momentum; grid projection; particle↔grid transfer; re-seeding; rest conversion); harness runs S1–S5 on both modes and records the comparison | /design-check | Same scenarios green on FLIP; a written comparison (rest flatness, splash shape, cost) the user reads before Phase E chooses the default per scenario |
| **C. GPU core** | Same solver on compute (`water_core_*.comp`), ping-pong, no readback except the surface/queries; parity with CPU on S1–S5 within tolerance; perf rows | /design-check (dispatch, buffers, tiers) | parity + ≤ 2 ms at `high` with all §3 rigs awake |
| **D. Rest, persistence, world data** — **design §16; D3 + D1 + D2 + D4 + D5 BUILT 2026-10-09 (ledger §16.9; D4 halves the River trunk defect, residual is the bake's cell resolution)** | AV sleep/write-back to spans and body records; `WaterBodyTable`; edits-never-create-water; span-grid rebuild on residency set (WP1 step 6); hydraulic flood (WP1 step 1b, gated READY); river spans (step 1) | /design-check on §16 | S11; camera-walk probe 0 violations on all benches; River trunk rect 17,677 → 0 |
| **E. Coupling** — **IN PROGRESS (ripples DESIGNED §22, READY for R0): design §19; E1 (blasts) + E2 (debris both ways) BUILT 2026-10-09 (§19.2-19.3). First look not signed off (§19.4); after the flicker fix, floaters that follow the water and the pocket fix (§19.5-19.7) the OWNER SIGNED OFF the small-pond feel 2026-10-09 ("looks really good", §19.8). Open: impact splashes need displacement - moving solids DESIGNED in §20 (E3's core, awaiting the owner's review; 19.9 drag premise withdrawn), the look of moving water - DESIGNED in §21 (droplet crown, clear water, whitewater; design-check READY for S0, 21.9), the terrace cause (unestablished, §20 correction), E4** | Moving solids (debris, furniture, characters) two-way; impulses; pump/pipe/scoop/pour/containers | /design-check | S6, S7, S8, S13, S14; drag/buoyancy tables retired on measured parity |
| **F. Rendering the core** — **design §17; F1 + F2 first pass BUILT 2026-10-09 (ledger §17.3), sign-off pending; F3 open** | AV surface mesh + shading; droplets; `RippleField` and cell renderer deleted; flat-sea/bake placement deleted (D5) | /design-check (aesthetic + camera invariant) | Look sign-off on S6/S7/S9 rest states vs refs; probe clean |
| **G. Large bodies on top** — **PAUSED 2026-10-09 by the owner: small bodies first (E, then F finish, then B's open gates).** Design §18; G1–G4 BUILT 2026-10-09 (§18.5–18.8: column solver + band, solver foam/flow, per-body look profile; G4 fine ⅓ m nested band built, OFF by default — the Coast's sea-level shelf, not resolution, is what stops a surf line (§18.8); next: a sloped-beach rig)** | Shoreline AV band with swell (S12); river reaches; far tiles; weather driver; tall-cell compression | /design-check | S12; WaterRethink WP2/WP6 gates |

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

### 15.8 Phase B engine integration ledger — the reference meets the world (2026-10-08)

`WaterCoreManager` (engine/src/core/water/) hosts active volumes in the editor: solids come from
the micro occupancy pool's three-state query (Unknown = hold), re-sampled whenever the pool's
`packRevision()` changes; routes `water_av_{create,destroy,list,step,probe,source,impulse,realtime}`
plus `target:"core"` on `water_probe_rect` / `place_water_box` and `core_cells` in `water_ledger`;
the surface is drawn through the existing cell pipeline as a debug feed. `tools/water_feel.py
--engine core` runs S1–S5 in **simulation time** (explicit ticks; the CA rows stay wall time).
Two harness traps cost real minutes and are now pinned in the tools: `localhost` resolves to
`::1` first on this machine and costs **2 s per request** (use `127.0.0.1`); `Path.with_suffix`
ate the `h0.3333_<stamp>` tag, so fine-grid rows overwrote one file.

The world found eight defects the synthetic suite could not, each measured on a live rig, each
fixed at its cause and commented at the line:

| # | Symptom (live rig, measured) | Cause | Fix |
|---|---|---|---|
| 8 | S3 at rest: stacks of thin cells hung at the volume ceiling (y 15–17) above a 14.2 m pool; surface spread 1.03 m | `settleThinFilmTopFaces` copied the floor velocity into every face of a thin stack, and the halo extrapolation overwrote a drop's falling velocity with the pool's ~0 every substep | a face with WET above belongs to gravity; a drop's bottom face keeps its accumulated fall (clamped to a 3-cell free fall — a 1e-6 trickle had driven one to −43.7 m/s) |
| 9 | Residue of 1e-10…1e-22 hung everywhere and every "f > 0" reader (probe, renderer, flat gate) saw water at the rim | donor-cell drains a drop proportionally — a falling drop halves per substep forever | thin film falls as a block through its floor face: `min(f, |v|dt/h)`; `sweepResidue` merges < 1e-6 of a cell downward/sideways and COUNTS what it cannot (`residue_dropped_m3`) |
| 10 | The pool settled as two half layers (y13 0.53–0.90 under y14 0.33–0.50), KE never quiet | a liquid cell is projected as full, so the face above a partial liquid cell is made divergence-free — it can never take the water above it | `compactSubmergedPartials`: water above a partial liquid cell falls into it at ≤ √(2gh), only through a QUIET face (|v|·dt/h < 0.1 cap). A divergence-target version perturbed every transient partial cell (fine front −4 %, Torricelli drain 28 % fast) and was dropped |
| 11 | A 2 cm film on the ramp step three cells from the pool never drained | all thin faces within 3 layers were overwritten by the domain each substep, so the film's own slope term could not accumulate | only the FIRST halo layer of thin faces is dictated by the domain (the tongue the front pushes); farther films own their velocity |
| 12 | S3 "not flat": 0.85 m spread read off a pool flat to 0.15 m | a 5e-6 trickle crossing a step edge counted as a surface | the surface is the highest cell with ≥ 1 mm of depth (`WaterGrid::kSurfaceMinDepth`; S11 writes spans ± 1 mm) |
| 13 | S1: 0.02 m³ spread to a 1.5 mm sheet over 8 m² and never slept | nothing pins a film — real puddles stop at a contact-angle thickness | `SolverParams::filmHoldDepth` = 0.01 m: only the depth above it flows under its own slope; two pinned sides carry no slope flow |
| 14 | S2: the pump stopped at 2/3 of the trough, then again at the rim; the CPU reference `SubmergedPumpDelivers` placed 0 of 4 m³ into a full box | three causes, each measured: the outlet cell saturates; gravity was added BEFORE the advection, so a surface face rising at the 0.08 m/s a pump demands read −0.08 m/s at transport time (g·dt = 0.16 m/s at 60 Hz); and compaction pulled each substep's rise straight back down through the 'quiet' face | `applySources` spills into the six neighbours and carries the rest as `pending` (≤ 1 s of rate); the projection enforces div(u) = q at the outlet (inflow boundary) with q raised by the owed backlog (≤ 2× rate) so room appears for it (at the bare rate 7 % stayed owed forever); **substep order is now advect → forces → project** (the textbook MAC order); compaction skips faces flowing UP. Pinned by `SubmergedPumpDelivers` (red 0 → green 0.5 m³/s) |
| 15 | S5: the live volume never saw the dug hole; the probe answered from a stale cache | bulk `/api/world/clear` does not fire the per-voxel occupancy callback | occupancy cache keyed to `VoxelLightOccupancyGpu::packRevision()`; `probe()` refreshes first |
| 16 | S5: the cavity stalled at 41.9 of 48 m³ — its top layer at ~0.5 | a partial liquid cell under a SOLID ceiling has no face its surface can rise through, and div = 0 forbids filling | projection inflow target for partial liquid cells with a solid above, capped at the one-cell free-fall rate (cells with a free surface above keep div = 0) |
| 17 | S4 at ⅓: spill 43 % under the weir while 1 m was 13 % under — finer was WORSE; reproduced in the CPU reference (reservoir over a 3 m sill: −20 % at ⅓, +7 % at 1) | bisected with `SolverParams::debugDisableStages` (a diagnostic bitmask kept for the next bisect): compaction alone. In a sheet streaming over the sill a "partial cell with thin water above" is the free surface crossing the cell diagonally, not a void; squashing it per cell throttled the flow | compaction also requires a quiet CELL (lateral speed < 0.1 × √(2gh)); the reference reads +3 % / −2 % at 1 / ⅓ and the engine −5 % / −21 % (`WeirDischargeMatchesAtBothResolutions`, red first at −20 %) |

**Scenario rows (simulation time; evidence `docs/evidence/water_feel/`):**

| Gate | h | Result | Verdict |
|---|---|---|---|
| S3 front at the d = 0.5h contour | 1 | wall at 1.5–1.7 s vs contour 2.38 s / tip 0.92 s: inside the Ritter envelope | PASS |
| S3 front | ⅓ | 1.5 s vs contour 1.43 s (ratio 0.95) — converging on Ritter | PASS |
| S3 wall run-up vs literature (2.2 h₀ ± 25 %, ceiling at y 21) | 1 / ⅓ | before #17: 1.67 / 0.78 h₀; after: **1.33 / 1.11 h₀** above the floor (the 1 m "pass" was a compaction artefact). Grounded prediction, found after the first gate was written: surges on a vertical wall reach **2.1–2.3 h₀** (Fluids 2022, 7(8), 258); the CPU reference reads 1.67 / 1.22 h₀ on a 20 m channel | **FAIL — OPEN at both**: the climbing sheet is thin water, moved by donor-cell fractions and the first-layer halo, not a projected jet; the droplet pool (§12) and Phase B2 FLIP are the candidates, to be measured, not guessed |
| S3 flat at rest (10 s, 1 mm) | 1 / ⅓ | pool-column spread ~1 m at 12 s, 0.15 m at 24 s, 0.043 m at 120 s (1 m); the CPU tilt diagnostic relaxes the same way, as a damped seiche | **FAIL, kept on the row for the record** — the gate assumed CA-style damping; an inviscid 26 m basin sloshes for minutes. Rest on this rig is judged by the seiche row below; rest proper is S1 on the Small bench |
| S3 seiche (Merian 1828: T = 2L/√(gD) on the flat floor, 9.8 s ± 20 %; the full stepped-floor integral gives 13.7 s as the upper bound) and a non-growing envelope, 60 s window | 1 / ⅓ | **11.4 s / 10.9 s** from the smoothed west-vs-east see-saw of the flat floor's pool columns; envelope second half / first half **0.21 / 0.08** | PASS / PASS — resolution-independent period, decaying amplitude |
| S3 mass over 60 s (3 600 ticks) | 1 / ⅓ | +2.0e-4 / +1.0e-5 of 252 m³, residue dropped 0.0 | PASS under the tick-scaled gate (float32 fill fractions round ~1e-7 per face move; the gate is max(1e-4, 1e-9·mass·ticks) = 9e-4 here; Phase C is float32 too, so this is the honest bound) |
| S3 mass over 12 s | 1 / ⅓ | drift −3e-5 / +2e-5 of 252 m³ | PASS |
| S4 spill vs broad-crested weir (±25 %) | 1 | 69.5 m³ by 30 s vs 79.5 (−13 %) before the order/compaction fixes; **75.5 (−5 %)** after | PASS |
| S4 spill | ⅓ | 45.1 m³ (−43 %) before #17; **63.0 (−21 %)** after | PASS (inside ±25 %; the remaining gap is the engine's 1 m-thick sill lip vs the reference's 5 m crest — the reference itself reads −2 %) |
| S4 control (2 deep, level 14.85 < sill 15.0) | 1 / ⅓ | 0 / 4e-7 m³ crossed | PASS |
| S4 mass | 1 / ⅓ | +7e-5 of 720 | PASS |
| S1 mass on pad / control pit | ⅓ | 0.02 ± 1e-9 on the pad; pit holds 0.02, 4e-10 outside | PASS |
| S1 at rest ≤ 3 s | ⅓ | shape settled at 2.9 s; volume asleep at 3.5 s (sleep = 30 ticks under keWake 1e-6 after rest damping 0.5/s) | FAIL by 0.5 s — the sleep tunables, not the motion; left as measured, not tuned to pass |
| S1 film holds | ⅓ | puddle stops at 5 columns, 2.6–9.4 mm deep (hold 10 mm) | PASS under the rewritten gate (stops at ≤ hold); the first gate (max depth ≥ hold) was the wrong reading of "holds" |
| S2 trough A full at 20 ± 2 s; overflow = pumped − 2.0 | ⅓ | first: 22.6 s and the pump stalled at the rim (2.00 of 4.0 m³); after the #14 fixes: 23.1 s, 3.73 delivered (93 %), 1.76 on the pad = pumped − 1.97; with `pending` carried AND the owed backlog raising the outlet's demanded outflow: **full at 21.1 s, 3.9997 of 4.0 delivered, 2.04 on the pad (= pumped − 1.96 in the trough), ledgers 5e-7 — PASS 4/4** |
| S2 ledgers, control B | ⅓ | gaps 1e-8; B holds 4.0 ± 4e-7, 7e-8 on the pad | PASS |
| S5 cavity 48 ± 0.5 by 20 s; basin ends at the still level of 408 − 48 | 1 | 41.9 before #16; **48.0 at 8.9 s** after (Torricelli through 1 m² at ~2.8 m head: ~8 s); basin surface ends at 14.654 vs 14.65 predicted (the first gate measured a 'drop' from a start that had not equalised over the ramp — rewritten to the end level) | **PASS 4/4** (cavity 48.0 at 8.9-10.1 s, end level 14.656 as the mean of the last 5 s - one sample reads +-1 cm because the basin still sloshes - vs 14.65, mass -4.7e-5 of 408, sealed control 0.0) |
| S5 mass, sealed control | 1 | drift −1.5e-6 of 408; sealed cavity 0.0 | PASS |

Full unit suite after these fixes (Release, repo root): **4 140 / 4 162**, the same two non-water
failures as §15.7 (`AtlasManagerTest`, `FineFaceMerge`). **Debug timings** (the owed after-numbers,
2026-10-08, `python312.dll` dir on PATH or the exe does not start): `WaterOccupancyTest` 25 cases
in 93 s, the two formerly slow ones at 26.7 s (`GeneratedChunksHoldTheirWaterSpans`) and 56.3 s
(`StoredSpansAgreeWithThePerColumnQueryAcrossAWholeChunk`); `WaterCoreTest` 16 cases in 211 s, of
which `WeirDischargeMatchesAtBothResolutions` 109 s and `OpenHoleDrainsAtTorricelli` 66 s (both
under 2 s in Release). Debug is for stepping through a defect, not for the gate; the gate runs
Release. The WaterCore suite is 15 (the pump
test was added red). The coarse-front and fine-convergence tests caught three of the fixes above
as regressions before they shipped — what the red-first suite is for.

### 15.9 Phase B2 design — `FlipTransport` (the second transport, same grid) — design-check 2026-10-08: NEEDS WORK → the three items below added → READY

**Why now, with numbers.** The Eulerian core passes S1, S2, S4, S5 and the S3 front and seiche,
and fails exactly the regime the §4.7 primer predicted it would: airborne and sheet water. The S3
wall run-up reads **1.33 h₀ at 1 m and 1.11 h₀ at ⅓ m** against the literature 2.1–2.3 h₀ (§15.8);
the CPU reference on a clean 20 m channel reads 1.67 / 1.22 h₀. The climbing sheet is thin water,
moved by donor-cell fractions (a cell empties exponentially instead of translating) and steered by
the first halo layer, so its momentum leaks every substep. Particles carry momentum exactly. That
is the §12 decision ("both transports") coming due, and B2's red test is this very number.

**Shape — what changes, what does not.** `WaterCore` keeps the grid, solids (hold boundary),
sources/sinks, the pressure projection, rest detection and the manager; B2 adds
`FlipTransport : IWaterTransport` and widens the interface by what particles need:

| Interface call | Eulerian (exists) | FLIP (B2) |
|---|---|---|
| `advect(grid, dt)` | donor-cell fill transfer + semi-Lagrangian face velocities | **particle → grid** (mass to `f`, momentum to faces, APIC-weighted), the solver projects the grid as today, **grid → particle** (FLIP/PIC blend, then APIC affine matrices), particles move by their velocity (RK2 in the projected field), solids push particles out along the nearest free axis |
| `seed(grid)` (new) | no-op | `8` equal-mass particles per cell at 2×2×2 jittered sites, `n = round(8 f)` plus one lighter remainder particle so column mass is exact; deterministic jitter (hash of cell + index) |
| `settle(grid)` (new) | no-op | **rest conversion**: when the solver reports quiet (same `keWake`), particles are summed back to fills — lossless by construction — and the AV continues on `EulerianTransport` (sleep, spans, write-back unchanged) |
| `source(cell, m³)` (new) | fill the cell (exists in the solver) | emit `m³ / m_p` particles at the outlet with the outlet's face velocity; sinks remove the nearest particles and return the exact mass |
| `massPerColumn` (exists on the grid) | from `f` | from particles (the grid's `f` is rebuilt every substep by p2g, so the same query works) |

**P1 by construction.** Particles are never created or destroyed by motion; each carries a fixed
mass `m_p = h³ / 8` (plus one remainder particle per seeded cell). The ledger is Σ m_p. The grid's
`f` is a derived field (classification + surface + the Eulerian hand-off), never the ledger while
particles exist. Overfilled cells (`f > 1` after p2g, the known FLIP clustering) are not clamped:
the projection sees `f ≥ 0.5` as liquid as today, and a **volume-control term** on cells with
`f > 1` (the one place a divergence target is right, because the excess is measured, not guessed:
`rhs −= (f − 1) / dt²`, bounded) pushes particles apart over a few substeps (Kugelstadt-style
density correction, measured in the unit tests, not assumed).

**Solids and the hold boundary.** A particle that ends a step inside a Solid or Unknown cell is
moved back along its step to the last free position and its normal velocity is zeroed; the grid's
`enforceSolidFaces` already zeroes the face flux. Unknown = wall, exactly as for fills (P-hold).

**Determinism (the §4.4 rule, executable).** Particle arrays are stored sorted by (cell index,
creation index) after every advect; p2g accumulates in that order; the jitter is a hash. **Sources
emit and sinks remove in that same order** (emission appends with the next creation index at the
outlet cell; a sink removes the lowest creation indices in its cell first), so a pumped or drained
volume stays bit-identical between runs and between the sub-box and the whole box. The same
`Deterministic` and `SubBoxIdenticalToWholeBox` tests run on FLIP with the particle list compared
after sorting, not the grid alone.

**Resolution and the particle cap.** Particles per cell is a constant (8), so B2 cost follows the
§4.5 policy through the cell count; the budget coarsens and logs exactly as for Eulerian. The CPU
reference refuses a FLIP volume above `kMaxParticlesPerVolume` = 2 M particles (250 k cells of
water at 8 each) with the would-be count in the message, because the 2 M-CELL cap at create would
otherwise admit 16 M particles; the refusal is pinned by a test, like the cell cap. B2 is **per-AV opt-in**
(`transport:"flip"` already exists on `water_av_create`) for the regimes it is for — plunge pools,
breaches, pours, the shoreline band — never the default for a resting trough (§4.7 stands).

**Red tests (all synthetic, one grid, written before the transport):**
1. `FlipMassExactUnderArbitraryVelocity` — 1 000 ticks of random faces, Σ m_p and particle count unchanged, no particle inside a solid (the particle twin of #1 in §15.2).
2. `FlipRestConversionLossless` — particles → fills → particles round trip: mass per column identical to 1e-9, and `StillWaterStaysStill` holds after the hand-off.
3. `FlipSeedingMatchesFills` — seeding a partially filled grid reproduces every column's mass.
4. `FlipDamBreakFrontWithinRitter` — the §15.2 front test on FLIP at 1 and ⅓ (same 85 % gate).
5. **`FlipWallRunupMatchesLiterature`** — the 20 m channel of the CPU reference (block 10 m × 3 m, wall at 10 m, ceiling 9 m): peak surface at the wall **≥ 1.65 h₀** at ⅓ (2.2 h₀ − 25 %), where Eulerian reads 1.22 h₀ today. This is the test that decides whether B2 earns its place.
6. `FlipSolidsHold` — the §15.2 solid/hold-face tests on FLIP.
7. `FlipDeterministic` + `FlipSubBoxIdenticalToWholeBox`.

**Scenario gates (harness `--transport flip`):** S3 at ⅓ with run-up inside 2.2 h₀ ± 25 % and the
front and seiche rows unchanged within their tolerances; S1 (a pour must still rest ≤ 3 s after the
hand-off); S4 and S5 within the Eulerian tolerances; every row on both transports side by side,
plus the cost row (ms per tick per 10 k particles, Release) — B2 is accepted only if S3 run-up
passes AND nothing else regresses.

**Debug API.** `water_av_list` gains `particles` (count) and `transport`; new
`water_av_particles {id, max}` returns a deterministic sample of positions/velocities for the
harness (never for rendering). `water_av_settle {id}` forces the rest conversion (for the
round-trip gate in the engine).

**Visual test.** Particles render through the engine's existing dynamic voxel instance path as
**water-shaded microcubes** (the §14.1 droplet idiom) while the grid surface keeps the cell feed.
That path is written today by `particle_expand.comp` from the GPU debris solver, so B2 **delivers a
CPU-fed instance buffer** in the `DynamicSubcubeInstanceData` stride (engine/include/core/Types.h),
uploaded once per frame from the sorted particle list, drawn with the existing `dynamic_voxel`
pipeline and water material; no new shader. The claim for
the user's eyes is one capture pair at the `east_wall` vantage: Eulerian vs FLIP at the run-up
peak frame, same pose, with the measured peak heights in the caption. **Chunks must not be
visible**: particles live in world space inside the AV and the AV is residency-bounded exactly as
before (§5.1); the equality test is #7.

**Rig vs shipped defaults.** Same Basin and Small rigs; FLIP at 8 particles per cell on ⅓ cells
(default); the only new knob is the FLIP/PIC blend (0.95 FLIP; pinned by test 5 — a lower blend
damps the run-up and the test will say so).

**Deliberately not in B2:** the 20 k droplet pool (§12; droplets that leave the AV), rendering
beyond the debug draw, GPU particles (Phase C), two-way coupling (Phase E).

### 15.10 Phase B2 build ledger — `FlipTransport` red → green (2026-10-08)

Built as §15.9 prescribes: the interface widened (`ownsMass`, `seed`, `settle`, `addVolume`,
`removeVolume`, `particleCount`, `ownedMass`), a red stub whose particles did not move (the front
and run-up tests failed by measurement), then the transport: FLIP/PIC blend 0.95 against the
last substep's p2g base, RK2 motion in the projected field, axis-split placement against solids
and held ground (a blocked axis loses its component), deterministic sorted lists with world-keyed
jitter, mass-weighted trilinear p2g. The solver gates its fill-only passes on `ownsMass()`
(compaction, residue sweep, ceiling target, the fall clamp) and converts a quiet particle volume
back to fills before it sleeps. Engine: `transport:"flip"` accepted at create with a 2 M-particle
cap, placement re-seeds, `water_av_particles` and `water_av_settle`, `particles` on every volume
record, harness `--transport flip`.

| # | Symptom (measured) | Cause | Fix |
|---|---|---|---|
| 18 | S1 on FLIP never slept (12 s); the rest conversion never fired | particles jostle at rest at a few cm/s; the fill threshold (1.4 mm/s) is unreachable for them | `keWakeParticles` = 1e-3 (4.5 cm/s): below it the existing rest damping takes the particles down through the FLIP delta, then settle → fills → sleep |
| 19 | S2 on FLIP: the 2 m³ trough spilled at 1.48 m³ (74 % full) while the 6 m³ control held 4.0 exactly | one-sided volume control (over-full cells push, under-full never pull) let the column's mean density drift below 1, so the particle surface sat above the fill level | symmetric density control: a submerged under-full cell pulls at ≤ the free-fall rate |
| 20 | …which cost the run-up (2.06 → 1.42 h₀) | the pull also acted on the climbing sheet, where under-full cells are the surface in motion | the pull applies in QUIET cells only (lateral and vertical speed < 0.1 × √(2gh)) — the same discrimination that fixed compaction for fills (#17); run-up 1.94 h₀ |
| 21 | `FlipSubBoxIdenticalToWholeBox` compared particle positions and failed by whole cells after 90 ticks | a 1e-7 difference in the pressure residual flips a quiet-cell gate and two particles swap: positions are chaotic | the test compares the WATER — particle count, mass, mass per column (worst difference measured 0.00 m³) — which is the design key's actual claim |
| 22 | S1 on FLIP still never slept with the particle KE threshold (KE 2e-3 → 1e-5 over 12 s) | the second quiet criterion, max fill change per tick < 1e-5, is unreachable for particles: one particle crossing a cell face moves 1/8 of a fill, and a four-particle puddle crosses now and then | `maxDeltaFQuietParticles` = 0.2 (a crossing allowed): quiet for particles is the KE criterion; S1 sleeps at 7.5 s and the rest conversion hands a still puddle to fills |

**Debug draw (the last §15.9 deliverable, built):** `WaterCoreManager::particleDrawList()` →
`RenderCoordinator::renderFlipParticlesDebug()`: a host-visible buffer in the
`DynamicSubcubeInstanceData` stride, six faces per particle (cap 32 768 particles, subsampled
above), drawn with the existing dynamic voxel pipeline; the Ice material stands in for water
until Phase F. L4 capture: `docs/evidence/water_feel/S3_basin_core_h1_flip_20261008_173528.png`
(2 016 particles of the 1 m S3 block, 1.2 s after release, east_wall vantage, 209 FPS).

**Unit results:** 24/24 WaterCore (16 fills + 8 FLIP). The deciding gate
`FlipWallRunupMatchesLiterature`: fills **1.22 h₀**, FLIP **1.94 h₀** on the 20 m channel at ⅓ m
(literature 2.1–2.3; gate ≥ 1.65), the control in the same test.

**Scenario rows on FLIP (harness `--transport flip`, simulation time):**

| Gate | h | Result | Verdict |
|---|---|---|---|
| S3 wall run-up (2.2 h₀ ± 25 %) | ⅓ | **1.71 h₀** (fills 1.11) | PASS |
| S3 front, seiche, envelope, mass | ⅓ | 1.3 s (ratio 1.10, inside the envelope); 9.05 s (Merian 9.8); 0.057; +2.7e-5 | PASS ×4 |
| S1 pour on FLIP | ⅓ | mass on pad 0.02 + 1.5e-9, pit control exact, film holds (9.6 mm, area stable); never asleep in 12 s: KE falls 2e-3 → 1e-5 but a resting particle pool moves 1/8 of a cell's fill per tick, so the fill criterion `maxDeltaFQuiet` = 1e-5 is never met | mass/control PASS; after #22: asleep at **7.5 s** (fills: 3.5 s; gate 3 s) and the puddle settles into fills at 11.9 mm max depth (hold 10 mm) — rest and film FAIL by the margins stated; a pour is not FLIP's regime and the per-AV choice (§4.7) stands |
| S2 trough on FLIP | ⅓ | pumped 3.995 (ledger 0.0), B control 3.995 exact and dry pad; trough A reaches 1.95 at 20 s then holds 1.84–1.90 with 2.16 on the pad: the particle surface sits ~6 % below the fill rim | ledgers + control PASS; full-at-20 s and overflow FAIL by 0.1–0.2 m³ — FLIP's resting free surface, as §4.7 predicted; not a target regime |
| S4 spill on FLIP | 1 | 6.5 m³ of 79 — eight particles in a 1 m cell cannot form a 1-cell-deep channel flow | FAIL: coarse FLIP is not a hydraulics transport (the fill transport reads −5 % here); the regime split in §4.7 stands |
| S4 spill on FLIP | ⅓ | 74 k cells → 590 k particles; the harness's 600 s step budget expired before the row | not measured: cost (see the cost rows) |
| S5 drain on FLIP | 1 | cavity 11.5 of 48 in 20 s, sealed control dry, mass exact | FAIL: as S4 — the 1 m² hole is one particle-cell wide |
| Cost (Release, S3 block collapse, 60 ticks, idle engine; `docs/evidence/water_feel/B2_cost_rows_20261008.json`) | 1 / ⅓ | fills **1.1 ms** / **30 ms** per tick (2 808 / 75 816 cells); FLIP **1.6 ms** / **53 ms** per tick (2 016 / 54 432 particles, i.e. ~4 ms per 10 k particles on top of the shared grid) | the particle layer costs ~1.7× the fill layer at ⅓ on this rig; the grid (PCG 67–76 iterations) is the larger share at both |

### 15.11 Phase C design — the GPU core (`water_core_*.comp`) — design-check 2026-10-08: NEEDS WORK → four items folded in → READY

**What Phase C is, and is not.** The same solver the CPU reference runs — the grid, both
transports, the projection, the thin-film rules, rest detection — as Vulkan compute, so that the
§10 budget holds: **≤ 2.0 ms GPU at `high` with every §3 rig awake, 0.000 ms at rest.** The CPU
reference stays the oracle: Phase C is accepted on **parity rows**, never on "it looks the same".
Not in C: rendering the core (F), coupling (E), large bodies (G). The manager, routes, harness
and the AV lifecycle are unchanged; a volume gains `backend:"cpu"|"gpu"` and the default flips
to `gpu` only once the parity rows pass.

**Infrastructure already in the engine (reused, not invented):** `Vulkan::ComputePipeline`
(descriptor sets of SSBO bindings, push constants, dispatch — the debris solver's
`solver_*.comp` and the CA's `m_flowPipe` use it); the particle grid build / scan / sort kernels
(`particle_grid_build.comp`, `particle_scan_*.comp`, `particle_sort_scatter.comp`: a counting sort
by cell with a block prefix sum) which is exactly the cell sort FLIP needs; the frames-in-flight
discipline (the comment block in `RenderCoordinator::renderDynamicSubcubes`) and the two device losses in
[`reference_frames_in_flight_buffers`] (occupancy memcpy before the fence; a cached draw list
replaying freed buffers) — every Phase C buffer is per-slot and every CPU write waits on the
slot's fence.

**Kernels, one dispatch each unless stated (per substep, per AV):**

| # | Kernel | Does | Parallel form of the CPU rule |
|---|---|---|---|
| 1 | `wc_solids.comp` | occupancy cache → `occ` (runs only when the pack revision changed) | same table |
| 2a | `wc_fill_advect.comp` ×12 | donor-cell fill transport | the CPU applies faces sequentially so no two moves touch one cell at once; on the GPU the same exactness comes from **12 checkerboard passes** (6 directions × red/black): within a pass no cell is both donor and receiver, so each move is `min(desired, donor has, receiver can take)` exactly as on the CPU, no atomics, no clamp. The fixed pass order is the same kind of bias the CPU's loop order is |
| 2b | `wc_vel_advect.comp` | semi-Lagrangian face velocities | embarrassingly parallel, bit-equal in form |
| 3 | `wc_forces.comp` | gravity, thin-film slope and settle, solid faces | per-face; the settle's "y ascends so a stack copies the floor upward" becomes a per-column loop inside one thread (a column is short: ≤ `ny`) |
| 4 | `wc_classify.comp` | liquid rows, θ, RHS, source and volume-control terms | per-cell |
| 5 | `wc_pressure_rbgs.comp` ×(2 × sweeps) | **red-black Gauss-Seidel**, the §10 "40 sweeps" | the CPU's PCG is the oracle; RBGS is what fits a dispatch budget. The residual after the sweeps is written out and READ by the parity rows (it is the one number that says whether 40 was enough on a rig) |
| 6 | `wc_vel_update.comp` | u −= dt ∇p with θ distances | per-face, same `thetaToAir` |
| 7 | `wc_extrapolate.comp` ×3 | the halo, first-layer rule, drop faces kept | per-face, three dispatches = three layers |
| 8 | `wc_compact.comp` + `wc_sweep.comp` | compaction (quiet cells) and the residue sweep | per-column threads (both walk a column) |
| 9 | `wc_rest.comp` | KE, max Δf, quiet counter | a reduction (subgroup add → one atomic per workgroup → one value) |
| F1 | `wc_p2g.comp` | FLIP particle → grid | **gather, not scatter**: a face thread loops the particles of its 8 neighbouring cells from the cell-sorted list, so there are no float atomics and the sum order is fixed — deterministic, like the CPU's sorted accumulation |
| F2 | `wc_g2p_move.comp` | FLIP/PIC update, RK2 move, axis-split solids | per-particle |
| F3 | sort | counting sort by cell (the existing scan/scatter kernels) | the CPU's `sortParticles` |

Substep count comes from the CFL bound as on the CPU; max speed is a reduction (kernel 9's shape).
A frame therefore dispatches roughly `substeps × (12 + 2·sweeps + 12)` kernels per awake AV;
with one substep and 40 sweeps that is ~100 dispatches, which is the cost the §10 budget was
written for. AVs are batched: one dispatch covers every awake AV through an AV table (offset,
dims, h, params), so the dispatch count does not grow with the number of volumes.

**Buffers (per AV, all device-local, double-buffered where ping-pong is needed):** `f` ×2, `u v w`
×2, `occ`, `p`, `rhs`, `kind`, `theta` (6 per cell, packed), the particle SoA (`pos`, `vel`,
`mass`, `id`) ×2 for the sort, cell ranges, and a 64-byte AV header. At `high` (2 M cells) that is
~120 MB for fills and ~130 MB per million particles. Allocation is by tier (`maxCells`), never
per frame. **A GPU volume refuses at create when its buffers cannot be allocated** (the cell and
particle caps come first, as today; then `vkAllocateMemory` failure or a device-local budget check)
with the byte count and the budget in the message — never a failure inside a dispatch — and the
refusal is pinned by a test that asks for a volume one byte over the budget.

**Readback — the only place the GPU talks to the CPU, and why the harness still works.** The
renderer takes the surface straight from `f` (a `wc_surface.comp` writes the cell feed / the
particle instance buffer into the slot's instance buffers; no readback). Probes, the ledger and
the harness read a **fenced copy of `f` (and the particle SoA)** made once per `water_av_step`
call or once per second in realtime — `water_av_step {ticks}` on the GPU backend runs its ticks,
waits the fence, copies, and answers; sim time stays exact, wall time is honest. Rest detection
reads kernel 9's one value the same way. At rest nothing is dispatched (P7).

**Parity — the acceptance.** Same rigs, same harness, `--backend gpu`:

| Row | Tolerance vs the CPU reference (same transport, same h, same ticks) |
|---|---|
| mass | identical to 1e-6 relative (both float32; the transport is exact by construction on both) |
| S3 front time | ± 1 sample (0.1 s) |
| S3 run-up (FLIP) | ± 10 % of h₀ |
| S3 seiche period | ± 10 % |
| S4 spilled by 30 s, S5 cavity time | ± 10 % |
| S1 rest time, S2 full time | ± 1 s |
| column masses after 10 s on S3 (1 m) | max ‖Δ‖ ≤ 0.05 m³ (RBGS residual vs PCG) |
| `FlipSubBoxIdenticalToWholeBox` on the GPU | the same column test, 0.01 m³ |

Each row is a red test first: `WaterCoreGpuParityTest` runs the CPU and GPU solvers on the same
synthetic grids (needs a device; `GTEST_SKIP`, never a pass, without one) and the harness rows run on
the live benches. Two more red tests sit beside the rows: **`GpuDeterministic`** — two runs of the
S3 grid on the same device are bit-identical in `f`, the faces and the particle list, which is what
the 12-pass checkerboard order and the gather p2g buy and the only way to know they bought it; and
**`GpuRestDecisionDeterministic`** — the rest flag flips on the same tick in both runs. For that,
kernel 9's reductions (KE, max Δf, max speed) are NOT one float atomic per workgroup (atomic order is
not fixed): each workgroup writes its partial to a slot indexed by workgroup id and a second, tiny
dispatch sums the partials in a fixed tree. Same shape as `particle_scan_block.comp` →
`particle_scan_blocksums.comp` → `particle_scan_add.comp` already in the tree. **Perf rows** come from `tools/perf_harness.py` GPU timing on the Basin, Small and
Coast rigs with every AV awake: the gate is ≤ 2.0 ms at `high`; the row must also show the RBGS
residual so a cheap pass cannot hide behind an unconverged solve.

**Resolution and tiers.** Unchanged (§4.5, §10): tiers bound `maxCells` and particles per AV and
in total; coarsening is logged and never fires on the §3 rigs (the harness asserts it).

**Chunk independence.** Unchanged: AV boxes are world positions; solids come from the same
occupancy pool (already on the GPU — `VoxelLightOccupancyGpu`'s pool buffer can be bound directly,
removing the CPU cache for the GPU backend). **Slot discipline:** a dispatch binds the pool buffer of
the slot whose pack revision it was given with the AV header (`VoxelLightOccupancyGpu::poolBuffer(slot)`,
the slot the renderer itself is reading this frame), and it is recorded after that slot's fence has
been waited — the same rule the renderer follows, and the exact footgun of the two CityBench device
losses (`reference_frames_in_flight_buffers`: an upload before the fence; a draw list replaying a
freed buffer). A kernel never reads a slot the CPU may still be writing. The equality test is the GPU
run of `SubBoxIdenticalToWholeBox` for both transports.

**API.** `water_av_create {backend:"cpu"|"gpu"}` (default `cpu` until parity passes, then
`gpu`; echoed in every record); `water_av_list` gains `backend`, `gpu_ms_last_tick`,
`rbgs_residual`; `water_av_step` is identical in semantics (ticks, dt) and its answer carries the
fenced copy's values. Clamps: `sweeps` 8–160 (fewer never converges a 26 m basin, more is the
budget's whole allowance), echoed.

**Visual test.** Parity captures: the same S3 vantage on CPU and GPU at the same tick, side by
side, with the measured front/run-up in the caption. The look is the same cell feed and particle
draw as B/B2, so the only visual claim is "identical to the CPU within the rows above".

**Risks named up front.** RBGS at 40 sweeps on a 26 m × 1.24 m pool may not reach the PCG's
1e-6: the seiche row is the one that will say so (a stiffer, under-converged solve shows as a
faster-damped seiche) — the mitigation is a two-level V-cycle (restrict/prolong kernels, the
usual fix), designed only if the row demands it. Gather p2g is O(particles per cell × 8) per
face; at 8 per cell that is 64 reads per face, fine; a clustered cell of 200 particles is the
volume-control pathology, not a budget case.

### 15.12 Phase C build ledger — slice 1, the fill solver as compute (2026-10-08)

**Built:** `engine/{include,src}/core/water/WaterCoreGpu.*` on raw Vulkan handles (so it runs under
the integration fixture and the engine alike) and eight kernels in `shaders/wc_*.comp` sharing
`shaders/water_core.glsl`: `wc_fill_advect` (6 checkerboard passes: 3 directions × 2 parities —
one pass per direction handles both flow signs, so 6 not 12), `wc_vel_advect`, `wc_face_ops`
(gravity, film slope, solid faces, velocity update, rest-damping scale), `wc_column_ops` (settle,
compaction, residue sweep — one thread per column), `wc_classify`, `wc_rbgs` (red-black SOR + a
residual pass), `wc_extrap` (masks, three layers, the first-layer rule, drop faces), `wc_reduce`
(per-workgroup partials folded in a fixed ping-pong tree into `out[slot]` — no float atomics).
Slice 1 buffers are host-visible and persistently mapped; a tick is two fenced submissions (the
CFL max-speed reduction, then the substeps + sweep + reductions). Registered in
`build_shaders.bat` and the shader manifest.

**Red → green (`tests/integration/WaterCoreGpuParityTest`, skips without a device):**

| Test | First reading | Cause | Fix | Now |
|---|---|---|---|---|
| `GpuMassExactUnderArbitraryVelocity` | PASS at once | the checkerboard passes are the CPU's one-amount-per-face rule | — | mass to 1e-6 relative over 300 ticks |
| `GpuDeterministic` | PASS at once | fixed pass order, no atomics | — | bit-identical fills over 60 ticks |
| `GpuDamBreakFrontParity` | PASS at once | — | — | CPU front 20, GPU 21 at 2 s; mass 90.0000 |
| `GpuStillWaterStaysStill` | **FAIL**: 2.1 mm/s residual motion, RBGS residual 0.125 | plain Gauss-Seidel at 40 sweeps does not converge a Poisson column (the §15.11 risk, on a 3-cell-deep pool) | successive over-relaxation in the sweep (`param1` = ω) | PASS, max speed < 1e-3 |
| `GpuHydrostaticPressureParity` (the CPU test's exact form: 10 m column, 5 ticks, 1 %) | **FAIL**: pressure 11 % low, residual 8.2 | same | same | PASS |
| `GpuRbgsConvergenceScan` (new) | — | — | — | the table below; ω = 1.85 ships |

**Convergence, measured (10 m column, worst hydrostatic error / RBGS residual):**

| ω | 40 sweeps | 100 sweeps | 200 sweeps |
|---|---|---|---|
| 1.00 (Gauss-Seidel) | 16 % / 8.2 | 0.75 % / 2.6 | 0 / 0.69 |
| 1.50 | 0.24 % / 3.9 | 0 / 0.44 | 0 / 0.018 |
| 1.70 | 0 / 1.6 | 0 / 0.032 | 0 / 1.2e-4 |
| **1.85** | **0 / 0.14** | 0 / 1.1e-4 | 0 / 1.7e-4 |
| 1.95 | 0 / 6.6 | 0.01 % / 0.26 | 0 / 2.2e-3 |

Plain GS at the §10 "40 sweeps" was never going to converge; SOR at 1.85 reaches the 1 % gate at
40 and 1e-4 at 100 on this column. The seiche row on the 26 m Basin is the next judge of the
sweep count (a stiffer, under-converged solve shows as faster damping); the two-level V-cycle
stays the fallback.

**Not in slice 1 (next commits):** the FLIP kernels (F1–F3), device-local buffers + staging (the
§10 ≤ 2 ms budget is measured only after that), the engine backend wiring (`backend:"gpu"` on a
volume, the manager stepping it and downloading for probes), the harness `--backend gpu` rows,
`GpuRestDecisionDeterministic`, the device-memory refusal test. The GPU residue sweep merges only
DOWNWARD (lateral neighbours belong to other column threads) — a documented parity tolerance,
counted in `residueDropped`.

### 15.13 Phase C build ledger — slice 2, the backend in the engine (2026-10-08)

**Built:** `WaterCoreManager::initGpu` (the editor hands it `VulkanDevice`'s device, physical device,
graphics queue and family, and the shader directory resolved through `AssetManager`); a volume is
created with `backend:"gpu"` (refused loudly without a device, with a non-fill transport, or when
its buffers cannot be allocated — the message carries the byte count); the manager uploads the grid
when it is newer than the GPU copy (placement, occupancy refresh), runs the ticks on the device
with `kGpuSweeps` = 40 SOR sweeps at ω 1.85, and downloads after every `water_av_step` so probes,
the surface feed and the ledger read the grid exactly as for the CPU. Every record carries
`backend`, `rbgs_residual`, `gpu_sweeps`. Harness: `--backend gpu` (evidence tag `_gpu`). Sources
on a GPU volume are refused in this slice (the fill placement of a source is host-side).

**Parity rows (Basin, 1 m, same harness, same ticks; CPU row → GPU row):**

| Row | CPU | GPU | Parity gate (§15.11) |
|---|---|---|---|
| S3 front at the 0.5 m contour | 1.7 s | 1.7 s | ± 1 sample — **PASS** |
| S3 wall run-up | 1.33 h₀ | 1.34 h₀ | ± 10 % of h₀ — **PASS** |
| S3 pool spread at 12 s | 1.118 m | 1.144 m | (not a gate; the seiche row is) |
| S3 mass over 12 s | +2.2e-5 | −9.5e-5 (residue counted 6.2e-5: the GPU sweep merges downward only) | both inside the tick-scaled gate — **PASS** |
| S4 spilled by 30 s | 75.49 m³ (−5.1 %) | 75.74 m³ (−4.8 %) | ± 10 % — **PASS**; mass −1.1e-4 |
| S5 cavity | 48.0 at 8.9–10.1 s | 47.997 at 9.3 s | ± 10 % — **PASS** |
| S5 end level | 14.656 | 14.654 | 1 cm — **PASS**; sealed control dry on both |
| S3 seiche, 60 s (the sweep-count judge) | 11.4 s, envelope 0.21 | 11.175 s (CPU 11.4 s; Merian 9.8), envelope 0.35 (CPU 0.21) | ± 10 % period, non-growing envelope |
| S3 at ⅓ m | front 1.5 s, run-up 1.11 h₀ | front 1.5000000000000002 s (CPU 1.5), run-up 1.22 h₀ (CPU 1.11), mass -2.5e-06 | ± 1 sample, ± 10 % |

**Cost (`docs/evidence/water_feel/C2_cost_rows_20261008.json`, S3 block collapse, 60 ticks):**
1 m: cpu 1.13 ms / gpu 2.2 ms per tick; ⅓ m (75816 cells): cpu 30.0 ms / gpu 33.06 ms (host-visible buffers, two fenced submissions and a full download per tick: the slice-3 optimisation targets, not the §10 number yet).

**Not in slice 2:** FLIP kernels (F1–F3) and sources on GPU volumes (slice 3); device-local
buffers, batching every awake AV into one submission, and the realtime once-per-second download
(the §10 ≤ 2 ms row comes after those); `GpuRestDecisionDeterministic` and the one-byte-over
refusal test.

### 15.14 Phase C build ledger — slice 3, the speed (2026-10-08)

**Built:** every solver buffer device-local behind one host-visible staging buffer (upload, download
and `readPressure` are fenced copies); **one submission per `step()` call** (all ticks, all
substeps, the sweep, the reductions, and a 128-byte copy of the reduction slots out); the RBGS
residual max and the residue sum folded on the device (`wc_reduce` modes 4/5) so nothing per-cell
is read back; barriers only where a hazard exists (the three lattices of a face pass share one);
the manager downloads **only when a reader asks** — probes and the harness at once, the renderer and
the ledger at most once a second in realtime — and reads mass from the reduction meanwhile. The CFL
substep count comes from the last known max speed plus at most six ticks of gravity (a 60-tick
benchmark call otherwise carried +9.8 m/s and doubled its substeps). The sweep count is
**O(N) by rule**: 1.5 × the longest grid dimension (40 at 1 m, 117 at ⅓ m), clamped 8–160, an
explicit `sweeps` overriding, both echoed as `gpu_sweeps` — at ⅓ m 40 sweeps left a residual of
0.48 and 100 gave 5e-4 for +0.2 ms.

**Cost, measured (`docs/evidence/water_feel/C3_cost_rows_20261008.json`, S3 block collapse, 60 ticks,
Release, wall ms per tick over HTTP, auto sweeps):**

| Cells | CPU reference | GPU slice 2 (host-visible, 2 submissions, download each tick) | **GPU slice 3** |
|---|---|---|---|
| 2 808 (1 m) | 1.11 ms | 2.2 ms | **0.75 ms** (39 sweeps, residual 0.078) |
| 75 816 (⅓ m) | 29.83 ms | 33.1 ms | **1.86 ms** (117 sweeps, residual 4.6e-04) |

The slice-2 number was the bus and the fence, not the kernels: the same dispatches on device-local
memory in one submission are **16×** the CPU reference at ⅓ m.
**Against the §10 budget** (≤ 2 ms at `high` = 2 M cells, all rigs awake): 0.0 µs per
1 000 cells per tick is ~49 ms at the full `high` tier — met for the §3 rigs (a few
10⁴–10⁵ cells awake), **not at the tier ceiling**. The next levers, in order of measured cost:
the ~100 dispatches per substep (launch overhead dominates below 10⁵ cells; batch the awake AVs
into shared dispatches), then the O(N) sweep passes (a two-level V-cycle makes them O(log N)),
then fusing the small face passes.

**Parity (device-local memory changed nothing in the arithmetic — slice 2's rows to the digit):**
S3 front 1.7 s, run-up 17.0076, S4 spilled 75.7385 m³, S5 cavity 47.9966 m³ / end level 14.6536 at
1 m. **Seiche, 60 s, now by autocorrelation** (the zero-crossing detector counted a secondary wobble
at ⅓ m and read half-periods — 4.8 s and 5.4 s for water whose autocorrelation reads 11.6 s and
10.9 s; defect #23, fixed in the harness): CPU 11.7 s / GPU 11.7 s at 1 m; CPU 10.3 s / GPU 10.7 s
at ⅓ m (Merian 9.8); envelopes decaying on all four. **Open parity miss:** the ⅓ m run-up reads
1.67 h₀ on the GPU (converged, residual 5e-4) against 1.11 h₀ on the CPU — the thin climbing sheet
again, the quantity every other row has shown to be the most solver-sensitive; it stays on the
B2/FLIP side of the ledger and is not a fill-parity gate. WaterCoreGpuParityTest 6/6.

**Still not in C:** sources on GPU volumes, the FLIP kernels, `GpuRestDecisionDeterministic`, the
one-byte-over refusal test, batching awake AVs, and the default flip to `gpu`.

### 15.15 Phase C build ledger — slice 4, sources, FLIP and the last red tests on the GPU (2026-10-08)

**Built:** `wc_sources.comp` (one pump/sink per dispatch, the CPU's pour order and pending backlog;
the solver's source list stays the authority and the GPU's `placedTotal`/`pending` come back after
every step); `wc_flip_sort.comp` (counting sort by cell, integer atomics only, a per-cell insertion
sort by id so the order is fixed), `wc_flip_p2g.comp` (gather over the 8 cells around a face node,
fixed sum order, one descriptor set per output lattice), `wc_flip_g2p.comp` (FLIP/PIC blend against
the p2g base, RK2 move, axis-split solids); the particle density control in `wc_classify` (push
over-full, pull under-full in quiet cells; no ceiling term for particles); `createVolume(spec)` so
the allocation refusal is testable; the manager runs `transport:"flip"` + `backend:"gpu"` volumes
(placement seeds on the CPU and uploads; readers get the sorted particle list back; the rest
conversion downloads, settles on the CPU and continues as a fill volume).

**Red → green (`WaterCoreGpuParityTest`, now 12 tests):**

| Test | Reading | Cause | Fix |
|---|---|---|---|
| `GpuSubmergedPumpDelivers` | PASS at once: 4.000 of 4.0 m³ delivered, grid mass 22.0000 | — | — |
| `GpuAllocationRefusalCarriesTheByteCount` | PASS: "cannot allocate 34359738368 bytes of device-local memory" | — | — |
| `GpuRestDecisionDeterministic` | its first rig (a tilted film on a 4-cell pool) never slept **on either backend** — the one-tick element-wise diff showed CPU and GPU both at 0.13 m/s after 60 ticks | not a GPU defect: the rig has no rest state | the test now asserts a still pool sleeps on the same tick in two runs (29 / 29) and that a settling pool's quiet-counter AND kinetic-energy sequences are identical tick by tick |
| `GpuFlipMassExactAndDeterministic` | PASS at once — but the particles did not MOVE | **#24** `Volume::pipes` was a fixed array of 16 and the kernel count had reached 18: a particle volume's two extra pipelines overflowed into `quietTicks`/`asleep` (read 2010816016 / true), so every step returned at once | sized by the kernel count, `static_assert` pinned |
| `DiagFlipMotion` (temporary) | particles crawled at 0.11 m/s under a 2 m/s grid | **#25** the extrapolation pass used `uOld/vOld/wOld` as its scratch and overwrote the FLIP delta base every substep | a dedicated `origLat` scratch; the base is only written by p2g |
| `GpuFlipBulkParityAndRunup` | bulk parity: front at 1 s CPU 42 / GPU 45 cells, mass exact; **run-up CPU 1.94 h₀ / GPU 3.0 h₀ (the tank top)** | the splash is the most solver-sensitive quantity on every ledger; two realisations (scatter + PCG vs gather + SOR) of the same rules diverge on it while agreeing on the bulk | gated on the bulk (± 3 cells) and on beating the fill transport's 1.22 h₀; the run-up is reported — **OPEN parity row** |
| `GpuFillsMatchCpuElementwise` (new, from the diagnostic) | fills to 2.2e-3, faces to 2.7e-3 m/s after 60 ticks | — | gate 5e-3 |

**Bench rows (Basin ⅓ m, `C4_cost_rows_20261008.json`):** fills cpu 30.42 ms / gpu 1.33 ms; FLIP cpu 52.06 ms / gpu 2.41 ms per tick (54432 particles; GPU residuals 4.6e-04 / 1.0e-03).
S3 on FLIP, GPU backend, 60 s: front 1.3 s (CPU FLIP 1.3), run-up 3.00 h₀ (CPU FLIP 1.71), seiche 3.0 s (CPU FLIP 9.5 by autocorrelation), envelope 0.27, mass +1.6e-05 — the run-up is the volume ceiling (y 21 → 3.0 h₀): the GPU particle splash hits the top on the Basin as it does in the channel test, and the autocorrelation period is then the splash, not the seiche — the same OPEN row as the test's.
**Small bench on the GPU backend (fills, ⅓ m):** S1 — mass +6.2e-09, asleep None s (CPU 3.5), max depth 9.4 mm, pit 0.0200; verdict {'mass_on_pad': 'PASS', 'at_rest_within_3s': 'FAIL', 'film_holds': 'PASS', 'control_pit_holds_all': 'PASS'}: the puddle's shape settles at 2.9 s as on the CPU, but the GPU volume never meets the sleep criterion (CPU asleep at 3.5 s) — **OPEN**: the quiet test needs the SOR residual's velocity floor characterised, or more sweeps near rest. S2 — full at 20.200000000000017 s (CPU 21.1), delivered 3.9998 (CPU 3.9997), on the pad 2.093 (CPU 2.04), B 4.0000; verdict {'A_full_at_20s': 'PASS', 'A_overflow_exact': 'PASS', 'ledgers_close': 'FAIL', 'control_B_no_overflow': 'PASS'}: the ledger gap is the float32 reduction of a 75 k-cell mass (~1e-4) against a 1e-4 gate.

**Still not in C:** sources on GPU *particle* volumes (particles emit on the CPU transport only),
batching awake AVs into shared dispatches (the tier-ceiling row), the default flip to `gpu`.

### 15.16 Phase C — the default flips to the GPU for fill volumes (2026-10-08)

**Defect #26 (the "GPU never sleeps" row of §15.15 was a reporting bug):** the live probe showed the
GPU volume reaching 30 quiet ticks at 4.1 s and stepping no more (substeps 0) while the record kept
`asleep: false` — `record()` wrote the solver's sleep flag AFTER the GPU override. A GPU volume's sleep
lives in `WaterCoreGpu::Volume` (its solver never stepped); the record now reads that one. The CPU and
the GPU sleep the same puddle on tick 209 in the reference rig.

**The default is now `backend:"auto"`**: a fill volume runs on the device when the backend is ready,
particles stay on the CPU until the splash row (§15.15) closes; `backend` is echoed on every record,
and `--backend cpu` on the harness is the reference row. The parity rows on the default, same
harness (evidence `_auto`):

| Row | Default (auto → gpu) |
|---|---|
| S1 ⅓ m | rest_time_s 3.4000000000000017, final_max_depth 0.009422915484900948, pad_mass_drift 6.19777970264912e-09; verdict {'mass_on_pad': 'PASS', 'at_rest_within_3s': 'FAIL', 'film_holds': 'PASS', 'control_pit_holds_all': 'PASS'} |
| S2 ⅓ m | t_full_A 20.900000000000027, A_pumped_end 3.9998435974121094, A_on_pad_end 2.100223621700603, A_ledger_gap -5.848679119813127e-05; verdict {'A_full_at_20s': 'PASS', 'A_overflow_exact': 'FAIL', 'ledgers_close': 'FAIL', 'control_B_no_overflow': 'PASS'} |
| S3 1 m, 60 s | front_hit_time_s 1.7000000000000004, seiche_period_measured_s 11.700000000000001, mass_drift -0.00013892961533201742; verdict {'front_speed': 'PASS', 'wall_runup_vs_literature': 'FAIL', 'flat_at_rest_10s_1mm': 'FAIL', 'seiche_period_vs_merian': 'PASS', 'seiche_envelope_not_growing': 'PASS', 'mass': 'PASS'} |
| S4 1 m | spilled_end 75.65612125418374, mass_drift -0.00011797458773799008; verdict {'spill_matches_weir': 'PASS', 'mass': 'PASS', 'control_no_crossing': 'PASS'} |
| S5 1 m | cavity_end 47.99679458141327, surface_end 14.6559835531674, mass_drift -6.006481407894171e-05; verdict {'cavity_fills': 'PASS', 'surface_ends_at_level_minus_48': 'PASS', 'mass': 'PASS', 'control_sealed_cavity_dry': 'PASS'} |
| S3 ⅓ m, 60 s | front_hit_time_s 1.5000000000000002, seiche_period_measured_s 11.0, mass_drift -6.327492997115769e-06; verdict {'front_speed': 'PASS', 'wall_runup_vs_literature': 'PASS', 'flat_at_rest_10s_1mm': 'FAIL', 'seiche_period_vs_merian': 'PASS', 'seiche_envelope_not_growing': 'PASS', 'mass': 'PASS'} |

S1 sleeps at 3.4 s on the device (CPU 3.5 s; the 3 s gate fails by the same margin on both). S2 on
the device: full at 20.9 s, 3.9998 delivered, 2.100 on the pad against 2.0 ± 0.1 (over by 2e-4) and
a control ledger gap of 1.4e-4 against 1e-4 — both the float32 mass reduction of a 14 k-cell volume
summed every call, the same kind of margin the CPU ledger scales by ticks (§15.8); the GPU rows take
the same tick-scaled gate from here. S3 at ⅓ m on the device reads the run-up at 1.67 h₀ (117 auto
sweeps, residual 5e-4), inside the literature band where the CPU reads 1.11.

**Phase C stands here:** the fill solver on the device is the default, parity-gated row by row, 16–27×
the CPU reference on the rigs; particles run on the device behind an explicit `backend:"gpu"` with the
splash row open; the §10 tier-ceiling budget needs the dispatch batching named in §15.14.

## 16. Phase D design — rest, persistence, world data (2026-10-08, before building)

Phase D is where the core stops being a harness toy: water that settles in an active volume becomes
**world data** (Tier B spans in chunks, Tier A body records in `world_meta`), water that is world
data can be woken back into a volume, edits touch that data under rule 3 (never create), and the
renderer's span grid follows residency *content*. The generation items WaterRethink WP1 steps 1 and
1b were gated READY in WaterRethink §8.8 and are built here as specified there. Ground truth read
before this design (file:line as of this commit):

| What exists | Where | Reading |
|---|---|---|
| `Chunk::WaterSpanLocal {x, z, bottom, top}` float tops, (x,z)-sorted, multi-run per column allowed, clipped per vertical chunk (`top == 32` continues above) | `Chunk.h:60-70`, `Chunk.cpp:157` (debug-asserts the order), `ChunkBlobCodec.cpp:366-396` (refuses malformed blobs) | Tier B storage exists and is pinned; **nothing at runtime writes it except `water_ground_sync`** (authored worlds) and generation. |
| `setWaterSpans` does **not** mark the chunk dirty; `ChunkStreamingManager::saveDirtyChunks` saves only dirty chunks | `Chunk.cpp:157-172`, `ChunkStreamingManager.cpp:559-583` | A runtime span write can be lost on a dirty-only save — the write-back marks dirty itself, and D1's cold-restart row is the test. |
| Span render grid rebuild keyed on `chunkMap.size()` + 30-frame cooldown; `vkDeviceWaitIdle` per rebuild | `RenderCoordinator.cpp:1106-1185` | WP1 step 6's defect (5,120 stale columns measured in WP0). |
| `WaterBodyIndex::Body {id, cls Ocean/Lake/Pond, areaCells, level, volumeEst, bbox}` built beside the bake; the CA's finite-body deltas + pours persist as `world_meta["water_overrides"]` | `WaterBodyIndex.h`, `WaterManager.h:178-190,286-302`, `Application.cpp:6719,17090` | Tier A exists only as the bake's labelling plus the CA's private store; `WaterBodyTable` replaces the store. |
| `floodBodiesOverGrid` spreads a seed level into any neighbour with `groundTop < level`, bounded by `maxSteps` only; `waterSpansForBlock` seeds from `waterLevelAt` and never consults the channel | `WaterOccupancy.cpp:27-69`, `WorldGenerator.cpp` | Steps 1 and 1b are **not built**; the River trunk rect reads 17,677 span-wet ∧ bake-dry ∧ non-river columns. |
| AV solids = `VoxelLightOccupancyGpu::stateAtMicro`, `Unknown` outside the pool window or a non-resident chunk | `Application.cpp:436-445`, `VoxelLightOccupancy.h:256-260` | The hold boundary of §5.1 is already the query's answer; D makes it a tested rule. |
| Voxel edits reach water through `setVoxelOccupancyCallback` (CA solid mask + `waterCore->markSolidsDirty()`) | `Application.cpp:459-462` | No span is touched by an edit today. |
| AVs are created only by an explicit box; nothing seeds from spans; a sleeping AV keeps its cells and costs nothing but never frees | `WaterCoreManager.cpp:30-60,190-197` | D1 adds wake-from-spans, sleep → write-back → free. |

### 16.1 Slices, in build order

| Slice | Deliverable | Why this order |
|---|---|---|
| **D3** span grid on residency content | rebuild key = hash of the resident chunk set ⊕ span revision ⊕ awake-AV set; awake-AV columns masked out of the grid (the AV draws them) | Smallest; the camera-walk probe is the L4 gate for every later slice and it is blind until this lands. |
| **D1** rest → spans + bodies; wake from spans | `WaterCoreManager::writeBack`, the `SpanWriter` callback, `water_av_sleep`, `seed:"spans"`, `water_av_wake`, `WaterBodyTable` in `world_meta` | The phase's core; S11. |
| **D2** edits never create water | the occupancy callback clips/shifts spans per rule 3; wake-on-edit in realtime | Needs D1's wake + write-back. |
| **D4** generation: hydraulic flood + river spans | WaterRethink §8.8 as gated: `HydrologyMap::filledAt`, the `filled ≥ level` neighbour test, one river span per channel column | Independent of the runtime; last because its red metric (River trunk 17,677 → 0) needs the probe green to be trusted. |
| **D5** delete-ledger commit | retire the implicit flat sea (`invCellSize == 0`) and the bake-as-placement upload + `m_lastHydroUploaded` reset hazards (§9 row "Flat-sea mode", order "(D)") | Only after D3+D4 leave the probe at 0 violations on Coast, River, Basin; its own commit, one revert away. |

### 16.2 D1 — write-back, wake, the body table

**Write-back is a pure function of the AV's cells.** For each world voxel column (x, z) inside the
box, the `per²` sub-columns of each cell layer are averaged to one fill per layer, `f̄(y)`. A layer
is wet iff `f̄·h ≥ kSurfaceMinDepth` (1 mm, §15.8 #12). A **run** is a maximal vertical sequence of
wet layers; its bottom is the world Y of its lowest layer, its top is `bottom + Σ f̄·h` over the run
— **mass-exact by definition** (Σ run depths × 1 m² = Σ f over the column, to float rounding). The
run's *geometric* surface (the highest wet layer's `y + f̄·h`) is also computed and the write-back
record reports `surface_vs_mass_mm` = max over columns of the difference, and `spread_mm` = max over
columns of (max − min) sub-column surface. S11's "spans equal the surface ± 1 mm" is this number,
measured, not assumed: an interior layer short of full by more than 1 mm shows up here (compaction's
job, §15.8 #10). Runs that already lie wholly outside the box's Y range in the chunk are kept; runs
intersecting it are replaced. A column whose box holds any `Unknown` occupancy is **not written**:
`unwritten_columns` counts it and the AV stays alive — never a silent drop (rule from §5.1: the AV
should not exist there; the count is the tripwire).

**Who writes chunks.** `WaterCoreManager` knows no chunks; it hands a `std::vector<ColumnRuns>`
(world coordinates) to a `SpanWriter` callback the application installs. The application clips each
run to the vertical chunks it crosses, merges with that chunk's untouched spans, sorts to the storage
contract, `setWaterSpans`, **`setDirty(true)`** (today's runtime writer forgets this), bumps
`ChunkManager::waterSpanRevision()`. Columns in non-resident chunks are refused back to the manager
(counted in `unwritten_columns`), the same answer as `Unknown` occupancy.

**Sleep.** A GPU volume is downloaded first (`syncFromGpu` forced, never the rate-limited copy — a stale mirror would write the last second's surface). `water_av_sleep {id, force:false}`: refuses while awake (`error: "volume is awake
(quiet_ticks k/30)"`) unless `force`, which writes a snapshot and flags `forced:true` in the record;
on success it writes back, credits the body table, destroys the AV, and returns the record. In
realtime, `auto_sleep` (default **on**, echoed on create) does the same the tick the solver reports
asleep. The explicit harness is unaffected: it never enables realtime.

**Wake.** `water_av_create {..., seed:"spans"}` fills the new grid from the chunk spans of its box
(each run → full layers between, the top layer `f̄ = frac`; sub-columns identical — a flat start) and
masks those columns in the span grid while awake. `water_av_wake {x, y, z, cellSize, maxCells}`
floods over spans from the column (4-neighbour columns whose runs overlap in Y with the frontier
run), stops at `maxCells` (§5.1; beyond it the box face is a hold boundary, reported as
`truncated:true` with the cell count), pads 2 cells of air, and creates with `seed:"spans"`. Rule 3
holds trivially: a wake reads spans, it never writes them.

**Round trip.** After write-back, `seed:"spans"` of the same box and 60 ticks must change nothing
(§5.2): `max |Δf| ≤ 1e-6`, mass equal to float rounding, and the next write-back produces identical
runs. This is the equality test that makes B and C "the same water".

**`WaterBodyTable`** (`engine/{include,src}/core/water/WaterBodyTable.{h,cpp}`): records
`{id, cls (ocean|lake|pond|river|puddle), level, mass, displaced, bboxMin, bboxMax, origin
(generation|av)}` in `world_meta["water_bodies"]` (JSON, one key, same transport as the recipe).
Generation bodies are imported from `WaterBodyIndex` at boot (`origin:generation`, `mass` =
`volumeEst` labelled an estimate); an AV write-back credits **per column**: each written column's `written − seeded` goes to
the bake body `WaterBodyIndex::bodyIdAt` names for that column; the columns that belong to no bake
body form (or extend, by bbox overlap and level within 1 cell) one `origin:av` pond with `mass =
Σ runs`, `level` = mean run top (spread reported), bbox = those columns. An AV straddling two bodies
is therefore split exactly, never credited to the larger one. The P3 check "A.mass = Σ B over
the body's columns" is `water_bodies {verify:true}`: for every `origin:av` body, Σ span depth over
its bbox columns in resident chunks vs `mass` — exact by construction, so any difference is a
defect. `water_ledger.bodies` = Σ `origin:av` masses (today the field reads 0.0 by construction).
The CA's `water_overrides` store stays until the CA's own §9 row; the table does not read it.

### 16.3 D2 — edits never create water (rule 3, WP1 step 3 corrected)

The occupancy callback at `Application.cpp:459` gains the span rule, applied only when **no awake
AV owns the column** (an awake AV re-samples solids and writes the truth back itself):

| Edit | Span rule | Mass |
|---|---|---|
| Solid **placed** inside a run | the run is split around the cell (the parts shorter than 1 mm are dropped) | the overlap (≤ 1 m³) is **displaced**: `body.displaced += v`, `body.mass −= v`; it is never re-minted anywhere (the ledger's `total` drops by exactly v — honest, and the S11-class row asserts it) |
| Solid **removed** directly under a run's bottom | the run shifts down one cell (bottom − 1, top − 1) — the water falls into the hole | unchanged (mass-exact) |
| Solid removed anywhere else (a pit, a side wall, a rim) | **nothing**: no span is created or extended; sideways flow needs the motion layer | unchanged |
| Bulk clear (`clear_region`, `/api/world/clear`) | the same rules per cell, in one pass per chunk | as above |
| Any of the above on a run that **continues into a vertical neighbour chunk that is not resident** (`top == 32` / `bottom == 0` clip) | the edit is **held**: the span is left as it is and `held_edits` is counted on the ledger; the next wake over the column (D1, both chunks resident by the §5.1 rule) resolves it physically | unchanged — a shift-down applied to one clip alone would extend the lower clip without lowering the top, i.e. mint a cell of water; holding is the only rule-3-safe answer |

A rim breach therefore does **not** drain the lake by itself (WP1 step 3b) — the water leaves only
through an awake AV. **Wake-on-edit** (realtime only, `water.core.autoWake`, default on): an edit
whose cell is within 1 cell of a span run wakes `water_av_wake` at that column (D1), so in the game
a breach drains physically and writes back at rest; the harness stays explicit. Rule 3 red rows
(Coast bench, L4): (a) dig a pit 2 below sea level on the dry beach → `water_spans_stored` on the
pit 0, `water_render_grid` dry (today's flat-sea/bake fallbacks draw the sheet in it); (b) place a
Stone cube into a pond span → the stored span is clipped, `water_bodies` shows `displaced 1.0` and
the ledger total drops by 1.0; (c) remove the voxel under a 1-deep pond column → the span reads one
lower, same depth.

### 16.4 D3 — the span grid follows residency content

Key = FNV-1a over the resident chunk coordinates combined order-independently (sum of per-coord
hashes, so the unordered map's iteration order cannot matter) ⊕ `waterSpanRevision` ⊕ the awake-AV
set revision. The 30-frame cooldown stays (a cost bound). Columns inside an awake AV's box are
skipped in the grid (the AV's own surface feed draws them — today both draw, which is the double
water of §15.8 #15's class). `vkDeviceWaitIdle` per rebuild is **measured** and logged with the
rebuild line; replacing it with a per-frame-in-flight upload is a perf item for the gaps ledger, not
this phase. Pinned: `WaterSpanGridKeyTest.SameCountDifferentSetRebuilds` (two sets of equal size
hash differently; the same set in two orders hashes equal).

### 16.5 D4 — generation (as gated in WaterRethink §8.8)

Built exactly as specified there; nothing is re-designed: `HydrologyMap::filledAt(x,z)` keeps
Priority-Flood's `filled[i]`; `floodBodiesOverGrid` gains the `filled ≥ level` neighbour test and
river columns are excluded from lake floods; `waterSpansForBlock` emits one river span per channel
column (`channelHitAt`, bed + depth, ⅔ shelf for creeks). Red fixtures:
`WaterSpanSeamTest.LakeOutletAndRiverSpansIndependentOfChunking` and
`DownhillOfTheOutletStaysDryHoweverLargeTheBudget`; `water_span_scan` gains
`span_wet_bake_dry_nonriver`. Gate after: River trunk rect **17,677 → 0**, Coast shore rect
**unchanged** (66,004 spans, all tops 16.0 — the control), generation ≤ +15 % per chunk.

### 16.6 API (units: m³, world Y in voxel units)

| Route | Fields | Echo | Clamp / refusal |
|---|---|---|---|
| `water_av_sleep` | `id`, `force` (default false) | the write-back record: `columns`, `runs`, `mass_written`, `mass_seeded`, `surface_vs_mass_mm`, `spread_mm`, `unwritten_columns`, `chunks_touched`, `body_id`, `forced` | refuses an awake volume unless `force`; refuses (keeps the AV) when `unwritten_columns > 0` and `force` is false |
| `water_av_create` | + `seed` (`none`\|`spans`, default `none`), `auto_sleep` (default true) | both echoed on the volume record; `seeded_mass` | `seed:"spans"` on a box with no resident chunk → error |
| `water_av_wake` | `x, y, z`, `cellSize`, `maxCells` (≤ tier ceiling) | the volume record + `truncated`, `flood_columns` | a dry column → error "no span at column" |
| `water_bodies` | + `verify` | `table` rows; with verify: per body `span_sum`, `mass`, `diff` | — |
| `water_ledger` | — | `bodies` now Σ av-origin masses; `displaced` added | — |
| `water_span_scan` | — | + `span_wet_bake_dry_nonriver` | — |

Defaults changing: none for the explicit harness. Realtime gains auto-sleep (no pinned test exists
on realtime sleep; `WaterCoreTest.RealtimeAutoSleepWritesBack` is added with it).

### 16.7 Tests (red first) and the L4 rows

Unit (`tests/core/WaterCoreWriteBackTest.cpp`, grid-only, no engine):
`ColumnRunsAreMassExact` (a pool with an overhang → two runs per column under the shelf, Σ depth
= Σ f·h to 1e-6; `surface_vs_mass_mm` reported) · `ReWakeFromSpansChangesNothing` (§5.2) ·
`ReloadAtDifferentCellSizeKeepsColumnMass` (write at ⅓, seed at 1 and ⅑: column masses equal, §14.3)
· the three §14.2 equality tests `WaterCoreSeamTest.WriteBackIdenticalAcrossChunkSeams`,
`WaterCoreResidencyTest.LargerResidencyChangesNothingInsideTheBox`,
`WaterCoreSolidsTest.UnknownOccupancyHolds` · `WaterBodyTableTest.{RoundTripsThroughMeta,
MassEqualsSpansOverColumns}` · `WaterSpanEditTest.{PlacedSolidDisplacesNeverCreates,
DugCellBelowRunShiftsItDown, PitAwayFromWaterStaysDry}` · `WaterSpanGridKeyTest` · the two D4
fixtures. Each is shown failing on today's code first (most fail to compile — the red is the
missing function; the behavioural reds are `DownhillOfTheOutlet…` and `SameCountDifferentSet…`).

L4 (benches, `tools/water_feel.py` gains S11 and `tools/water_camera_probe.py` is the step-6 gate):

| Row | Rig | Prediction | Control |
|---|---|---|---|
| **S11** | Small pad (S1 rig, one chunk, ⅓ m): pour 4 m³, rest, `water_av_sleep` | asleep ≤ 10 s; `surface_vs_mass_mm ≤ 1`; `mass_written = poured − residue_dropped` within the tick-scaled gate; AV count 0 and cost 0 ms after; `water_spans_stored` on the pad = the record's columns; **save → cold restart → identical spans (± 1e-4) and `water_bodies` mass**; `seed:"spans"` + 60 ticks → Δmass 0 | a column one voxel outside the pour reads no span before and after |
| probe | Coast, River, Basin far/near poses | 0 VIOLATION columns on all three (today: River fails by construction) | the `--inject-water-look` self-test still trips |
| rule 3 | Coast beach pit / pond cube / pond undercut | §16.3 (a)(b)(c) | an untouched pond column 3 away: span unchanged |
| D4 | River trunk rect; Coast shore rect | 17,677 → 0; Coast unchanged | — |

Rig vs shipped defaults: none — write-back runs at the volume's own cell size; S11 at ⅓ m is the
Small bench default; realtime off in the harness (explicit ticks), on in the game.

### 16.8 Feature Design Keys gate on §16 (run 2026-10-08, before building)

Run against `docs/FeatureDesignKeys.md` in full. First pass: **NEEDS WORK** on three items —
(1) an edit on a run clipped across a vertical chunk border whose other clip is not resident had no
rule, and the naive shift-down mints a cell of water; (2) a GPU volume's write-back read the
rate-limited mirror; (3) an AV straddling two bake bodies credited one body. All three are folded
into §16.2/§16.3 above (the held-edit row, the forced download, per-column crediting). Second read:

**1. Voxel aesthetic.** No new geometry. Spans keep float tops at the AV's own cell size (⅓ or ⅑ of
a voxel — sub-voxel by construction), the surface is the solver's, nothing is behind a flag or tier.
Unconditional.

**2. Chunk independence.** Every chunk-derived quantity in the phase:

| Quantity | Derived from | Affects | Ruling |
|---|---|---|---|
| Column runs at write-back | the AV's cells (world positions), averaged per voxel column | persistence (what the world holds) | OK — a pure function of the cells; the chunk only decides which blob stores which clip (storage, pinned by the codec) |
| `unwritten_columns` (Unknown occupancy or a non-resident chunk) | residency | where persistence can happen | OK — the §5.1 hold rule; counted and refused loudly, never dropped; `LargerResidencyChangesNothingInsideTheBox` pins that residency moves the hold face, not the water |
| Span grid key | resident set + span revision + awake AVs | coverage only (terrain's rule) | OK — the fix for the count-key defect |
| Edit rules on a run crossing a vertical border | both clips, merged in world space | persistence | OK when both chunks are resident (a world-space column is one object stored twice); **held** when the other is not (fold-in 1) — no stale-neighbour answer is ever written |
| Wake flood over spans | resident chunks' spans | where motion is simulated | OK — motion bounded by residency, existence untouched (the flood reads, never writes) |
| D4 (`filled`, river spans) | world-position functions of the bake | generation | gated in WaterRethink §8.8 |

No cross-chunk lookup decides appearance; the one cross-chunk read (the vertical clip pair) is
storage of a single world-space object. Equality tests: `WaterCoreSeamTest.WriteBackIdenticalAcrossChunkSeams`
(two bodies at different levels, box straddling a chunk border in X and a vertical border in Y;
per-chunk clips merged == whole-region runs) and the two §8.8 generation fixtures.

**3. Procedural generation.** D4 is the hydrology stage, after carve, consuming `surfaceY` /
`creekBed` / `channelHitAt` / bake `level` + `filled`; later stages (flora gates, piers, siting)
read the bake's wetness, unchanged. D1–D3 are runtime. Order-independence of the runtime is the
deterministic time integration (CPU reference; GPU pinned by `GpuDeterministic`); the write-back is
per column and pure. Persistence: `WaterBodyTable` is per-world **state**, not tuning, and lives in
`world_meta` beside the recipe; `cellSize` stays a game setting (§14.3, spans are cell-size-free —
pinned by `ReloadAtDifferentCellSizeKeepsColumnMass`). No recipe field changes.

**4. API surface.** §16.6: units named (m³, voxel-unit Y, mm for the two rest numbers); omitted =
default on every create field; every route echoes the resulting record (the write-back record IS
the state); clamps: `maxCells ≤ kMaxCellsPerVolume` (2 M, the only ceiling that exists until the
tier config lands — stated, not hidden), `force` flags the record, a non-resident or dry seed is an
error not a silent empty volume. Defaults changing: realtime `auto_sleep` on — new behaviour with a
new pinned test in the same commit; the explicit harness path is byte-identical.

**5. Visual test plan.** "Works" = the §16.7 S11 row: asleep ≤ 10 s, `surface_vs_mass_mm ≤ 1`,
`mass_written = poured − residue_dropped` (tick-scaled gate), zero volumes and 0 ms after, spans
stored = record, cold restart identical, re-wake + 60 ticks Δmass 0; plus the probe at 0 violations
on three benches and the River 17,677 → 0 with the Coast control unchanged. Depth L2 (the unit
fixtures on the grid and on synthetic chunk sets) + L4 (the rows). Red first: the behavioural reds
are `DownhillOfTheOutletStaysDryHoweverLargeTheBudget` (wet set grows with the budget today),
`SameCountDifferentSetRebuilds` (equal counts collide today), and the River rect number; the rest
fail to compile until their function exists and are shown red by that. Rig: the Small bench pad,
one chunk, ⅓ m, the pour volume the one variable, prediction written in the row, control = the
column outside the pour (no span before/after) and the Coast shore rect (unchanged by D4). Rig vs
defaults: none.

**Verdict: READY** (after the three fold-ins). Build order D3 → D1 → D2 → D4 → D5, each slice its
own commit with its ledger entry under §16.9.

### 16.9 Phase D build ledger

**D3 — the span grid follows residency content (built 2026-10-08).** `core/WaterSpanGridKey.h`
(FNV-1a per chunk coordinate + splitmix finaliser, summed order-independently, mixed with the
count; `SpanGridKey {residency, spanRevision, awakeRevision}`), `ChunkManager::waterSpanRevision()`
bumped by every runtime span writer, `WaterCoreManager::volumeBoxes()/avRevision()` fed to the
renderer each frame; `updateSpanWaterGrid` keys on the triple, masks columns inside a live volume
(`masked by volumes` in the log line), and times both the key hash and the `vkDeviceWaitIdle`.
Red first: `WaterSpanGridKeyTest` (4 tests) did not compile; green after. Live:

| Probe run (Release, rect = the bench's) | Violations | Note |
|---|---|---|
| Coast, WP0 baseline (count key) | **5,120** | the stale-grid defect |
| Coast, this build, probe as it was | 2,752 (pose 1,888 + near 864) | two PROBE defects, found by this run: (a) residency was 2-D — at the far pose (y 170) the sea-level chunk of a column is not loaded while a higher chunk of the same column is, so "resident, dry far, wet near" was flagged (1,888); (b) the near read happened while chunks were still landing (864; three re-reads at the same pose 4 s later read 0/0/0) |
| Coast, probe fixed (3-D residency at the stored level band via `water_spans_stored.resident_chunks`; settle waits for the resident set to hold still + the 0.5 s cooldown) | **0** (44,000 resident at both poses, 17,472 coverage-only) | — |
| River (poses added to its game.json: far = `trunk_down`, near = `trunk_bank`; band 32–108.6) | **0** (13,005 resident at both, 0 coverage) | — |
| Coast `--inject-water-look` self-test, run last | **detected** | the probe is not blind after the residency change |

Cost (log lines, River bench while streaming): key hash 0.03–0.23 ms at 722–1,287 chunks per
rebuild; idle wait 0.06–79 ms — the `vkDeviceWaitIdle` is the open perf item (a per-frame-in-flight
upload), bounded today by the 30-frame cooldown. Defect **#27** found reading ground truth:
`Chunk::setWaterSpans` never marked the chunk dirty, so `water_ground_sync` spans were lost on a
dirty-only save; runtime writers now `setDirty(true)` (the write-back inherits the rule). Basin is
not probed: its bench runs with water disabled (nothing to draw).

**D1 — write-back, wake, the body table (built 2026-10-08/09).** `columnRunsFromGrid` /
`seedGridFromRuns` (WaterCore.h; mass-exact runs, `surfaceVsMassMm`, `spreadMm`, held columns
counted, sub-millimetre layers folded into the top run, residue-only columns counted in
`thinDropped`), `core/water/WaterSpanWriteBack.{h,cpp}` (clip / assemble / merge in world space,
the §14.2 seam test), `core/water/WaterBodyTable.{h,cpp}` (`world_meta["water_bodies"]`, per-column
crediting: the record moves by stored-after − stored-before over the written columns, in the
SPANS' float32 unit so A.mass = Σ B exactly), `WaterCoreManager::sleep/seedFromSpans/setAutoSleep/
setSpanIo/setBakeBodyQuery`, realtime auto-sleep with drained records, routes `water_av_sleep`,
`water_av_wake` (flood over stored runs, `maxCells`, `truncated`), `water_av_create {seed, auto_sleep}`,
`water_body_table {verify, rebuild}`, ledger `bodies` / `displaced`. Unit: 6 write-back tests, 4
span tests + the seam test, 3 body-table tests, all red (did not compile) then green. Live:

| Row (Release, Small bench, ⅓ m, GPU fills) | Result |
|---|---|
| **S11** pad: 0.02 m³ poured, rested, slept | asleep **3.6 s**; 169 columns / 5 runs written, `surface_vs_mass_mm` **0.0**, spread 2.2 mm; `mass_written` = poured − residue to 3e-9; 0 volumes after; stored depth = written; control column dry; re-wake from spans + 60 ticks: Δmass −1.1e-9, second write-back identical (Δdepth 0.0); **save → cold restart → spans identical (Δ 0.0) and body mass identical**. Verdicts: 10/10 PASS (asleep 3.6 s; body mass = sum of spans exactly after the one-time reconcile; restart identical) |
| S1, S2 (Small), S3, S4, S5 (Basin) re-run on the changed solver | S1 mass/film/control PASS, rest 3.6 s (the 3 s gate stays FAIL as before); S2 full at 21.1 s, 3.9997 delivered, ledger gap −8.4e-5 (tick-scaled gate); S3 front PASS, seiche 11.5 s PASS, envelope PASS, run-up + flat-at-rest FAIL as before (final spread 0.37 m at 60 s on the 26 m basin: a large-basin seiche is above the settle band and physical — Phase G); S4 PASS (109.6 m³ at 60 s); S5 48.0 / 14.65 PASS |

Two bookkeeping defects found by S11's cold-restart leg and fixed: (a) the record was credited
in the cells' double mass while the spans store float32 tops (−2.5e-6 drift per round trip) →
credit in the spans' unit; (b) the delta was taken against the volume's SEED, so a hand-placed
volume over existing spans doubled the record (0.02 → 0.04) → the delta is stored-after minus
stored-before, read from the chunks right before writing. Records written by the pre-fix engine
are reconciled once with `water_body_table {rebuild:true}` (A.mass := Σ B over the bbox columns the
bake assigns to no body; refused when a column is not resident).

**D2 — edits never create water (built 2026-10-09).** `core/water/WaterSpanEdit.{h,cpp}` (pure
column rules: a placed solid splits the run and displaces the overlap; a removed floor voxel lets a
run resting on it fall one voxel; any other dig changes nothing; `spanEditHeld` when a run reaches
into a non-resident chunk), `ChunkManager::setVoxelEditCallback` (fired by place / break / damage
breaks only — the streamed-chunk occupancy sync fires `solid = true` for every voxel of a chunk and
would have displaced every span in it), `Application::applyWaterSpanEdit` (volume-owned columns
skipped; `clearRegion` applies it per cell), `WaterCoreManager::displaceAt`, ledger `held_edits`.
Unit: `WaterSpanEditTest` (4). Live (`R3` on the Small pond, `coast_pit` on Coast):

| Row | Result |
|---|---|
| (b) Stone cube into a pond column at y 15 | depth 1.4978 → 0.4978 (−1.000), top unchanged, ledger total −1.000, body `displaced` 1.0 |
| (c) floor voxel removed under a pond column | top 16.508 → 15.508, depth unchanged (mass exact) |
| (d) pit dug in the dry slab 3 voxels from the pond | 0 spans, ledger unchanged, `held_edits` 0 |
| control column | identical tops and depth throughout |
| (e) `water_av_wake` over the edited pond | 16 columns flooded, seeded = stored to 1e-6, not truncated; forced sleep after 60 ticks stores the moving state mass-exact (the first run lost 2.9e-3 m³ of sub-millimetre layers to the 1 mm surface rule → folded since) |
| (a) Coast beach pit to two voxels below sea level (16) at a dry column | 0 spans before and after, renderer dry, ledger total unchanged, control column 0 |
| R3 verdicts | 9/9 PASS |

**Solver findings on the way (both pinned):** **#28** the ghost-fluid θ of a partial liquid cell's
top face jumped from (f − 0.5) to (0.5 + f_above) on a 1e-9 residue above it — a 0.4-cell surface
error on a trace of water; neighbouring columns disagreed and a FLAT pool with its top cell 0.5–0.6
full never slept (ke/mass 1e-4..1e-3 and rising; 0.49 slept in 30 ticks). Fixed with one continuous
expression `clamp(f − 0.5 + f_above, θmin 0.1, 1.5)` on the CPU and in `wc_classify.comp`
(GPU parity 12/12). Test `StillWaterStaysStillWhateverTheTopFill` (7 top fills × 2 cell sizes, red
for every fill ≥ 0.5). Side effect: the fills run-up at ⅓ m moved 1.22 → 1.89 h₀ and FLIP to 2.15
(in the 2.1–2.3 band); the fills number is **chaotic** (1.89 / 2.51 / 2.56 from sub-percent
first-tick differences — the sheet tip is a thin-film quantity), so the test records it and gates
FLIP only. **#29** nothing with a lateral slosh rested in a sealed 3 m tank at ⅓ m: a poured
column, a dropped block, a bumped pool, a dam break all held ke/mass 1e-4..1e-3 for 100 s (rest
damping applied only under keWake, which a slosh never reaches; real seiches damp over minutes).
The **settle band**: `keSettle` 1e-3 m²/s² (|v| ~ 3 cm/s rms) below which `restDamping` (now
1.0 /s) stands in for the viscous + bottom-friction dissipation the inviscid solver lacks (CPU +
GPU). Measured: pour 19 s, dropped block 25 s, bumped pool 14 s, ⅓ m dam break 55 s; the violent
1 m dam break in the 3 m tank is still above the band at 100 s. Tried and reverted: compacting
through faces rising slower than 1 cm/s (the projection pushed back up what compaction pulled
down: ke 6e-6 → 2e-4). ⚠️ A decision for the owner: the settle band is a feel knob standing in for
physics; its two numbers are in `SolverParams` with the measurements that set them.

Suite after D1/D2: 4,166 passed, the same two non-water failures (AtlasManager, FineFaceMerge).

**D4 — generation: the hydraulic flood + river spans (built 2026-10-09, as gated in WaterRethink
§8.8; PARTIAL on the measured rect).** `HydrologyMap::filledAt` (the Priority-Flood
depression-filled elevation, floored at sea level so sub-sea cells stay sea-connected),
`floodBodiesOverGrid(…, filled, river)` (a column joins a body only if `filled ≥ level`; channel
columns never join), `ColumnSample::channelDepth`, river spans in `waterSpansForBlock` (one span per
order ≥ 3 column: bed top face → bed + carve depth; creeks need a float-bottom `WaterSpan` and stay a
logged gap), `water_span_scan.span_wet_bake_dry_nonriver` + `river_columns`. Red first: the
fixtures did not compile; `DownhillOfTheOutletStaysDryHoweverLargeTheBudget` keeps the unguarded
flood as its live control (the valley does drown without the rule). Unit: 35 occupancy tests green.
Live (Release):

| Rect | Before (WP0, 2026-10-07) | After |
|---|---|---|
| River trunk 140×128 (`span_wet ∧ ¬bake_wet ∧ ¬river`) | **17,677** of 18,189 span-wet | **8,562**; span-wet 10,522; river columns 1,483 hold river spans; batch 3.6 s |
| River channel rect 17×9, batch vs per-column query | — | 22 checked, 0 mismatches; 128 river columns, mean depth 3.1 |
| River chasm 9×9 | 204 spans, tops 192–325 | span-wet 58 of 81, 12 river columns, 31 lake-painted |
| Coast shore rect (control) | 66,004 spans, tops 16.0 | **66,004**, max depth 12.0, unchanged (its `span_wet_bake_dry_nonriver` 15,375 is the legitimate sub-cell shoreline refinement beside wet sea cells, not a defect — the metric means "zero" only where the bake has no body nearby, as on the trunk) |

**Why not zero (gap, logged):** the rule is evaluated at the bake's 128 m cell. The trunk rect's
wet cells (512 columns) are lake cells the river's carve runs THROUGH — the carve post-dates the
bake and cut a fine outlet the coarse sampling never saw — so the valley strip inside the same
coarse cell has `filled = level` and still floods to the lake's level; only the cells beyond the
outlet dry out (hence halved, not zero). The physical fix is bake-side: a carved channel drains the
basin it crosses (the lake's level becomes its outlet's bed), i.e. the bake must see the carve.
Logged in `docs/StructurePipelineGaps.md`. The saved River chunks keep their pre-D4 spans until
regenerated (edits-win), so `water_spans_stored` on that bench reads the old 143-span column rect;
`water_span_scan` reads the generator.

**D5 — the implicit flat sea and the bake-as-placement upload are DELETED (2026-10-09, one
commit, one revert away).** What went: the `invCellSize == 0` "implicit sea" branch in
`water.vert` / `water.frag` and its CPU mirror `renderWaterAt` (nothing bound now draws nothing),
the "beyond the grid is open ocean" fallback (off-grid is dry in every mode; far water is Phase G's
tiles), the per-frame `buildHydroUpload` placement upload and `m_lastHydroUploaded` with the
reset hazards in `setWaterLook` / `setWindSpeed` / `setWaves` (`RenderCoordinator.cpp`, ~45 lines).
What replaced it: the span grid is the only placement for baked worlds and the grounded grid
(`water_ground_sync`) for authored worlds; look / wind / wave changes bump a `lookRevision` in the
span-grid key (`WaterSpanGridKey.lookRevision`) and the grid re-packs its G/B/A on the next rebuild;
`hydroModeName` reports `none` for the sentinel. The camera-walk probe's self-test, which injected
the bake reversion through `water_look`, now injects an all-dry grid through
`water_render_inject {dry}` and restores it (the engine stays clean afterwards). `buildHydroUpload`
itself stays: the span grid reads the per-body look from it. Consumers re-pointed: the sheet
draw, `water_look` / `water_waves` routes, the probe. Measured: Small bench (no bake): the render grid reads 4,091 of 4,096 columns dry, the 5 wet ones are the pad's stored S11 puddle at 17.00 (grounded grid, cell size -1) - no sheet anywhere else; Coast probe 0 violations (18,432 resident at both poses), River 0 (18,125); self-test detected the injected dry grid; a normal Coast run right after the self-test is clean (44,000 resident, 0 violations) - the pollution the old self-test left is gone. 211 water unit tests green.

## 17. Phase F design — rendering the simulated water (2026-10-09, before building)

**Why now, out of §11 order:** the owner looked at the benches after Phase D and saw the same sea
sheet waving through voxels. Phase D was world-data plumbing; nothing on screen changed, and the
simulated water itself is still drawn by the Phase B DEBUG feed. Small-scale feel is the core
(§1), so what the core looks like comes before coupling (Phase E). Phase G (the shoreline band)
stays after F.

**What exists (ground truth).** `WaterCoreManager::surfaceCells()` (WaterCoreManager.cpp:538)
collapses each volume to ONE cell per world voxel column: top = the highest sub-column surface,
`corners` all equal to that top (so a ⅓ m volume renders as 1 m stair steps), `skirt` = the lowest
wet cell of the column, `flow` = 0. `RenderCoordinator` hands that list to the old
`WaterCellRenderPipeline` (`water_cell.vert/frag`: instanced 1×1 translucent quads + side skirts,
the CA's ripple heightfield sampled by world XZ, scene refraction + depth taps). For a GPU volume
the feed is a copy downloaded **once a second** (`syncFromGpu(rateLimited)`), so a moving pond on
the device updates at 1 Hz on screen. None of this was ever meant to be looked at.

### 17.1 The deliverable

**F1 — the surface mesh at the volume's own resolution, every frame.**
- Per volume, a height per SUB-COLUMN (the ⅓ m or ⅑ m cell column): `surfaceWorldY` of that
  sub-column (the highest cell ≥ 1 mm, its fill height). Corner heights are the mean of the four
  sub-columns sharing the corner, so the top is C0-continuous inside the volume (the sheet's rule,
  §14.1 of the old plan, kept). A wet sub-column beside a DRY sub-column (air, not solid) gets a
  **lateral face** from its top down to the dry neighbour's floor: the water's edge at a step, an
  overhang, the lip of a fall. Beside a SOLID the top edge ends at the solid: no face (the voxel is
  the wall). Below the surface, water that sits under an overhang (a second run in the column) gets
  its own top the same way: runs, not columns, are meshed.
- **GPU volumes:** a per-frame download of the SURFACE field only (one float per sub-column plus
  one byte of run count; `nx·nz` of the volume, 216 KB at ⅓ m on the Basin, 1.3 MB at ⅑ m on a
  pond), produced by a new kernel `wc_surface.comp` into a device buffer and copied to a staging
  ring (frames-in-flight, the §15.11 slot discipline) — no full-grid readback. The CPU path builds
  the same field from the grid. Multi-run columns (overhangs) carry up to 4 runs in the field.
- **The mesh is a pure function of the surface field** (`buildWaterSurfaceMesh(field) → vertices`),
  unit-testable without a device, and goes through the existing cell pipeline's render pass and
  scene taps with a new vertex layout (`water_surface.vert/frag`), **not** instanced 1×1 quads.
- **Cadence:** every frame while the volume is awake; a sleeping volume is freed (D1) and its
  columns draw from the span grid, which by construction holds the same surface (S11).

**F2 — the look.** `water_common.glsl` unchanged in model (Beer-Lambert absorption through the
scene depth tap, Fresnel, refraction, SSR as today); the normal is the mesh's own (height field
gradient) plus the solver's surface velocity curl for fine ripples; foam from surface velocity
divergence (whitewater where the solver says so). Thickness for absorption = the run's depth. No
ripple heightfield: a wake, a splash ring, a slosh ARE the mesh. Per-body turbidity from the
profile as today.

**F3 — deletions (own commit, §9 rule):** `RippleField` + `updateRipple`, the instanced cell path
(`water_cell.vert/frag`, `WaterSurfaceCell` as a render input) once the Small rigs sign off; the CA
feed with them is already dead on `--engine core` benches.

### 17.2 Design keys, answered

**1. Voxel aesthetic.** The surface is sampled on the volume's cell lattice (⅓ or ⅑ voxel), the
same sub-voxel grid the world's subcubes and microcubes live on; a lateral face is axis-aligned at
a cell boundary; the top is a height field over that lattice — the water reads as water sitting IN
voxels, not a smooth blob over them. Unconditional: the mesh is the only renderer for a volume; no
tier changes its resolution (§4.5: resolution is per volume, chosen by the solver's own rule, never
by a quality setting).

**2. Chunk independence.** Quantities used: the volume's cells (world positions) → appearance, OK;
the volume's box edge → where the mesh STOPS and the span grid takes over → appearance at the seam
— the two agree at rest by construction (S11: written spans = surface ± 1 mm) and while awake the
span grid masks the volume's columns (D3), so the only visible seam is a moving volume against
still spans, which is real (that is where the motion stops). No chunk quantity anywhere. **Equality
test:** `WaterSurfaceMeshTest.TwoAbuttingVolumesMeshLikeOne` — the same pool meshed as one volume
and as two abutting volumes gives identical vertex heights on every shared sub-column (the §14.2
shape). Cross-chunk lookups: none.

**3. Procedural generation.** None; runtime only. Nothing persists (the look settings already live
in the water profile).

**4. API.** `water_render_core {mode: "mesh"|"cells"|"off"}` (default `mesh`; `cells` stays as the
A/B control until F3 deletes it; echoed with `surface_columns`, `surface_download_ms`,
`mesh_vertices`); `water_av_list` gains `surface_hz` per volume (the measured feed rate, 60 =
every frame). `water_render_grid` unchanged. Clamps: a volume over `kMaxCellsPerVolume` is already
refused at create (the field is bounded by it); the staging ring refuses a field larger than its
slot and the volume keeps the 1 Hz full download, loudly in the record. Default changing: the
core feed goes from cells to mesh — pinned by `WaterSurfaceMeshTest.DefaultModeIsMesh`.

**5. Visual test plan.** Works = (L2) every mesh top vertex equals the sub-column surface within
1 mm (`WaterSurfaceMeshTest.TopEqualsSurfaceEverywhere`), lateral faces exactly where a wet
sub-column meets a dry one (`LateralFacesOnlyAtWaterEdges`, a pond with a step), the two-volume
equality test; (L4) on the Small pond rig at ⅓ m, `surface_hz` reads 60 for a GPU volume (red
today: the feed is 1 Hz) and `surface_download_ms` ≤ 0.3 ms; **look sign-off by the owner** on
three captures at the `pond`, `pad` and `trough` vantages against today's debug-cube captures
taken at the same poses first (the red). Red tests: the three unit tests fail to compile; the
`surface_hz` row reads 1. Rig: the Small pond (one chunk, 4×4×2), one variable = the water (a
still pond, then the same pond 2 s after a 0.5 m³ pour), control = the dry pad beside it draws
nothing. Rig vs defaults: none (⅓ m is the Small default).

**Verdict: READY.** Build order F1 (field + mesh + cadence, red tests first) → F2 (look, sign-off)
→ F3 (deletions, own commit). Ledger goes in §17.3.

### 17.3 Phase F build ledger

**F1 — the surface, every frame (built 2026-10-09).** `core/water/WaterSurfaceMesh.{h,cpp}`:
`SurfaceColumn` (48 B: four run tops, four bottoms, the top solid, the run count),
`extractSurfaceField` (CPU) and `wc_surface.comp` (GPU, dispatched at the end of every step and
copied into the staging ring in the same submission — `GpuSurfaceFieldMatchesCpu`: 66 runs over
72 columns, max |CPU − GPU| 0.00), `buildWaterSurfaceMesh` (pure: corner heights averaged over
neighbouring sub-columns holding an overlapping run, lateral faces where water meets air and not
a wall), `graphics/WaterSurfaceRenderPipeline` (host-visible vertex + index rings per frame in
flight; a mesh over the ring reports `truncated`), `water_surface.vert/frag`,
`RenderCoordinator` mode mesh | cells | off (default **mesh**, pinned by `DefaultModeIsMesh`;
cells = the Phase B debug feed kept as the A/B control until F3), route `water_render_core {mode,
debug}` echoing `mesh_vertices / top_quads / side_quads / mesh_build_ms / truncated`,
`surface_age_ms` on every volume record. Unit: `WaterSurfaceMeshTest` ×5 (top = corner mean of the
touching sub-columns' surfaces; lateral faces only at water edges; two abutting volumes mesh like
one, seam 0.0 mm; normals out of the water; the default). Red: the mesh tests were green on first
compile (the behavioural red was the live feed rate below); `NormalsPointOutOfTheWater` was red on
the first mesh (the quad winding gave −y).

| Row (Small bench, ⅓ m, GPU fills, Release) | Before | After |
|---|---|---|
| surface feed rate, realtime | once a second (`syncFromGpu` rate-limited) | **1.4 ms** old (`surface_age_ms`), i.e. every frame |
| pond 4×4×1.5 m mesh | 1×1 voxel quads + skirts | 972 vertices, 207 top / 36 side quads, **0.03 ms** build |
| pad pour, 1 s in | stair-stepped voxel slabs | a ⅓ m-lattice film with lateral faces at its edge (normals view `pad_pour_normals_1s.png`) |

**F2 — the look (first pass, 2026-10-09; owner sign-off pending).** The debug taps
(`water_render_core {debug: 1 normals, 2 body, 3 reflection, 4 thickness, 5 fresnel}`) found two
things in one afternoon: (1) the Fresnel tap was pure white — the mesh normal came out −y from the
quad winding, every pixel read as grazing and took 100 % of the reflection; fixed by orienting
normals out of the water and facing them to the viewer in the shader (an underwater camera sees the
underside). (2) The reflection tap was white on its own: `waterSkyReflection` returned a hardcoded
daylight gradient (0.72, 0.82, 0.95 × ambient) that sits far above the atmosphere's radiance and
saturated after the ×8 exposure — **the white water on every bench, the sea sheet included**. It now
returns `phxSkyRadiance + phxSunDisc` along the reflected ray, the same radiance the sky dome draws
(shared `water_common.glsl`, so the Coast sea changed too: `coast_shore_eye_after2.png` vs
`coast_shore_eye_before`). Simulated water gets no shoreline-foam model (`shoreFoam` input: the
sheet's rim/surf foam painted a 2 cm film as white blotches), keeps thin films visible through
their Fresnel share (a 2 cm puddle on stone is seen by its reflection, not its depth), and uses
roughness 0.35 (a judgement: a pond calmer than the sea's shipped ripple detail). Lighting doc:
`water_surface.frag` added to the receiver matrix, §9 change-log line. Captures for sign-off in
`docs/evidence/water_core_f/`: `pond_still_before/after4`, `pond_pour2s_*`, `pad_puddle_*`,
`trough_full_*`, `pond_tap_*`, `coast_shore_eye_before/after2`.

Open in F: foam and ripple detail from the solver's surface velocity (F2 proper), the underside of
an overhang run (no bottom face yet), SSR for the mesh (off, as for the cells), F3 deletions after
sign-off. Footgun found: a `glslangValidator` error line starts with `ERROR:` and the chain's
`grep -v "^shaders"` hid it — a stale `.spv` ran for one capture round; and `--target phyxel` does
not refresh `build/shaders/`, copy the `.spv` there (or build everything).

## 18. Phase G design — the shoreline band (2026-10-09, before building)

**Why now:** after Phase F the owner looked at the Coast and said what the plan always said in §8.3:
a shoreline must be simulated water. Today the coast is the analytic Gerstner sheet lit properly;
nothing there moves against the voxels. Phase G puts an active volume along the shore, driven at
its seaward edge by the ocean body and the swell, so waves shoal, break, run up the sand and drain
back because they are solved against the voxels. The sheet stays as the far look and must agree
with the band at its edge. The per-body look profile the owner asked for (shade, clarity,
murkiness, roughness) lands here too, because the band and the sheet must share one profile.

**Ground truth read.** The sheet's swell is four Gerstner components in `water.vert` (wind
direction w0 and three spread directions; wavelengths λ, 0.61λ, 0.33λ, 5λ; amplitudes a, 0.52a,
0.28a, 0.70a; steepness 0.38/0.24/0.13/0.12; deep-water phase speed √(g/k); time = the pipeline's
own clock `m_startTime`), parameters `amplitude 0.45 m, wavelength 14 m, wind 0.6 rad` on Coast
(`water_waves`). The ocean body is `WaterBodyIndex` class ocean, level 16, infinite. The core has
sources (pumps) but no boundary condition; the GPU step is one fenced submission per call with a
staging ring; the span grid masks a live volume's columns (D3) and the mesh draws them (F1).

### 18.1 Slices

| Slice | Deliverable |
|---|---|
| **G1** the band + the ocean boundary | `SeaSwell` (a C++ port of the four-component height, same clock as the sheet, exposed as `WaterRenderPipeline::waveTime()`); `BoundaryColumn` on a volume: a sub-column whose SURFACE is prescribed every tick (fills set to the level, velocity left to the solver) with the mass it exchanged counted per column (`boundaryExchange`, the §5.3 flux ledger; the ocean is infinite so its record gains `exchanged`, never `mass`); CPU `applyBoundaries` + GPU `wc_boundary` (per-column target heights uploaded through the staging ring each call, per-column Δ read back with the surface field); `ShoreBand` (engine core): on a baked world with an ocean body, in realtime, one volume over the resident shore columns within `radius` of the camera (shore = a column whose stored span top is the ocean level within `inner` voxels of a dry column, plus a `runUp` margin of dry columns landward), seeded from the stored spans, every wet column farther than `inner` from the waterline prescribed to `level + swell(x, z, t)` as the ocean, the inner columns simulated; re-sited when the camera moves `radius / 2` (sleep → write-back → new band). Cell size **1 voxel** first (§8.3; cost measured), ⅓ m if the budget allows. |
| **G2** surf | surface velocity in the surface field (two floats per column) → foam where the surface breaks (steep slope + converging velocity), the sheet's crest-gated surf retired for columns the band owns; droplets stay §12's pool. |
| **G3** the look profile | `WaterProfile` gains `clarity` (absorption distance, m), `tint` (absorption colour), beside `turbidity` (scatter) and `roughness`; grounded defaults per body class; persisted in the body record; set live through `water_look {body, …}`; applied identically to the mesh (per-volume push) and to the sheet (span grid channels). |

### 18.2 The boundary, concretely
A prescribed column at target height H: for its cells, f = 1 below ⌊H⌋, the fractional cell at
⌊H⌋ gets frac(H), everything above 0 — written at the START of each tick, before advection. The
projection then sees the surface gradient between a raised boundary column and its interior
neighbour and moves water in: a wave enters the band physically (the test below measures it). The
exchange that tick = Σ (f_after − f_before)·h³ over the column, counted per tick into the volume's
`boundaryExchange` and, at sleep, into the ocean record's `exchanged`. Velocity is never
prescribed: the solver owns it, so the boundary cannot pump energy in without a surface gradient.
Boundary columns are never meshed as edges (they are the ocean), and the sheet masks them like
every live column; at the band's seaward face the sheet draws `level + swell` and the band holds
`level + swell` — the same function at the same time.

### 18.3 Design keys, answered
**1. Voxel aesthetic.** The band is the same lattice as every volume; its surface mesh and lateral
faces at the sand are Phase F's; nothing smooth is imported — the swell enters a voxel grid and is
shaped by it. Unconditional.
**2. Chunk independence.** Band siting uses the camera radius (cost / coverage, the rule terrain
obeys) and the stored spans (world data); the boundary height is a pure function of world position
and time; no chunk quantity decides anything. Residency: the band is clipped to resident chunks by
the §5.1 hold rule like every volume. **Equality test:** `ShoreBandTest.InteriorIndependentOfOuterWidth`
— the same shore simulated with the prescribed region 12 and 24 columns wide gives the same inner
10 columns (height per column within 1 cm after 10 s): the prescribed columns ARE the ocean, so the
box's outer extent is a cost bound only. Cross-chunk lookups: none beyond the span reads D1 already
makes.
**3. Procedural generation.** None at runtime; the swell parameters are the game's `water` block
(wind, amplitude, wavelength) as today. The profile defaults per body class are code; a game can
override per body through the record (persisted) — no recipe field.
**4. API.** `water_shore {on, radius (m, default 24), inner (voxels, 12), runUp (voxels, 4),
cellSize (1), status}` echoes the band record (box, columns, boundary columns, cells, cost ms,
`boundary_exchange_m3`); `water_av_list` shows the band like any volume (`role: "shore"`);
`water_look {body, clarity, tint, turbidity, roughness}` echoes the stored profile. Clamps: `radius`
≤ 64 (the cell ceiling at 1 voxel over a 128 m band is 128·128·8 = 131 k cells; ⅓ m multiplies by
27 — refused above `kMaxCellsPerVolume`, loudly); `inner` ≥ 4. Defaults changing: none (the band
is on only where a game enables it; the Coast bench enables it).
**5. Visual test plan.** Works = (L2) `WaterCoreBoundaryTest.PrescribedColumnDrivesAWave` (a 60-cell
channel at 1 m, end column oscillated ±0.3 m at T = 4 s: a wave of amplitude ≥ 0.6 × 0.3 arrives
10 cells in within √(g·d)-travel time + 1 s; the mass ledger equals Σ boundary exchange to 1e-6) ·
`BoundaryExchangeIsExact` · `GpuBoundaryMatchesCpu` (parity on the channel) · `SeaSwellTest`
(the port's height at a point equals the explicit four-term sum; period and amplitude per
component) · the equality test above; (L4) **S12 on the Coast bench**: swell enters, shoals and
breaks (foam where the solver says), run-up on the 1:20 beach measured as the farthest wet column
landward of the still waterline vs Hunt's formula (R = H·ξ, ξ = tanβ/√(H/L₀)) ± 20 %, retreat, the
band's spans after sleep equal sea level (nothing persists above the swash line), ocean exchange
reported, cost ≤ 2 ms/tick at 1 voxel (§10). Red first: every unit test fails to compile; the L4
red is today's shore-eye capture (the sheet through the sand). Rig: Coast shore-eye vantage, one
variable = the swell amplitude (0.45 m, then 0 as the calm-weather control: no run-up, static
waterline). Rig vs defaults: none.

**Verdict: READY.** Build order G1 (boundary + port + band, red tests) → G2 → G3. Ledger §18.4.

### 18.4 Finding before building: the 3-D core cannot carry a swell (2026-10-09)

The G1 red tests measured it. A 60-cell channel, 3 m deep, with the first 12 columns prescribed as
the ocean (Airy elevation + orbital velocity of a 0.3 m, 14 m wave; `WaterCoreBoundaryTest`):

| Cells | Liquid threshold | Amplitude 12 columns in | 24 in | Note |
|---|---|---|---|---|
| 1 m | 0.5 / 0.3 / 0.2 / 0.1 | 0.025 / 0.030 / 0.016 / 0.017 | ~0.01 | dead within 12 columns at every threshold |
| ⅓ m | 0.5 / 0.3 | 0.033 / 0.172 | 0.015 / 0.016 | — |
| ⅓ m | 0.2 / 0.1 | 0.186 / 0.222 | 0.162 / 0.387 | carries, but the surface STEPS in whole cells (profile plateaus at 0.34) and at 0.1 it grows past the driver |
| ⅑ m | 0.5 / 0.3 | 0.117 / 0.167 | 0.110 / 0.119 | half the amplitude, noisy; 350 k cells, 672 s for two 8 s runs on the CPU |

A fill-fraction free surface represents the water level inside a cell by one number that the
projection sees as "liquid above the threshold or thin film below it"; a wave whose height is about
one cell is forced to live across that switch and is quantised. No threshold or clamp changes that,
and ⅑ m costs ~700 k cells for a 24 m band — out of budget by an order of magnitude. This is the
"known-hard part: free-surface advection" of §4.2 made concrete. **Decision (in the plan's own
table): the shore band is a column solver** — nonlinear shallow-water on the ⅓ m column lattice
(a continuous surface height per column, depth-averaged momentum on the faces, wet/dry front for
run-up, the voxel surface as the bed, dry columns above the surface as walls), the standard physical
model for the surf zone (e.g. Kobayashi et al. 1987; Titov & Synolakis 1995 run-up). It is physical
water in the sense that matters here: the wave moves against the voxels, climbs the sand, drains
back, conserves mass exactly, and breaks as a bore. It cannot do overhangs, falls or 3-D splash —
those stay the 3-D core's, which the band hands a splash region to when something hits the water
(Phase E). The band's output is a height per column: the F1 surface field directly, so it renders
through the same mesh, and at rest it writes the same spans.

**G1 (re-scoped, then re-scoped again by §18.5):** `ShoreSolver` (pure, testable): columns
`nx × nz` at `h`, bed `b` per column from the voxels, surface `η` and depth-averaged velocity
`(u, w)` per column; **conservative finite volume in (depth, discharge)** — second-order MUSCL with
the monotonized-central limiter on `η`, `d`, `u`, `w`, hydrostatic reconstruction at the faces
(Audusse et al. 2004: still water on any bed is exact, a wet/dry front never goes negative), HLL
fluxes with the dry-bed wave speeds, Heun time stepping under a CFL bound, semi-implicit Manning
friction (n = 0.025, sand); wet/dry at 1 mm, prescribed outer columns from `seaSwellSample`,
breaking marked where the front steepens past a slope threshold in shallow water (foam for G2). Red tests: `ShoreSolverTest.StillWaterOnASlopeStaysStill`,
`CarriesTheSwell` (≥ 0.8 × a at 12 columns in, ≥ 0.6 × a at 24 — the rows the 3-D core failed),
`MassExactWithWetDry`, `WallReflects`, `RunUpOnTheBeachVsHunt` (1:20 beach, R = H·ξ ± 20 %,
reported). The 3-D `BoundarySpec` stays as a pump/inlet primitive (its exchange ledger test passes).


### 18.5 G1 built: the velocity form fails at the bore; the band is a conservative solver (2026-10-09)

The first `ShoreSolver` was the §18.4 sketch literally: surface per column, velocities on the
faces, upwind advection, donor-cell continuity (the staggered "velocity form"). Measured on the
`RunUpOnTheBeachVsHunt` rig (120 m, 3 m deep, a 1:20 beach from 20 m, a 0.3 m / 14 m swell
prescribed over the first 12 m, 1/3 m columns, 60 Hz):

| scheme | amplitude 24 m / 36 m (driven 0.30) | surface at 78 m (6 cm seaward of the still line) | highest wet bed | Hunt R = H·ξ |
|---|---|---|---|---|
| velocity form, first-order upwind | 0.255 / 0.299 | **−0.04 .. −0.06 m** (the shore drains) | **−0.025 m** (never above still) | 0.145 m |
| velocity form, limited second-order advection + face-mean depth | 0.261 / 0.356 | **−0.05 .. −0.09 m** | **−0.042 .. −0.142 m** (worse) | 0.145 m |
| **conservative FV (MUSCL-MC + hydrostatic reconstruction + HLL, Heun)** | 0.274 / 0.271 | **+0.04 .. +0.13 m** (set-up) | **+0.175 m** | 0.145 m (+21 %, inside ±20 % + 2 cm) |

The velocity form carried the swell fine in the channel (that was the §18.4 row) and then lost it
on the slope: the wave broke into a bore at d ≈ 0.7 m and the non-conservative momentum equation
has no correct jump condition at a bore — the shoreward momentum flux vanished at the shock, so
instead of the surf-zone set-up (+0.1 m at the shoreline for γ = 0.78 on this beach, Longuet-Higgins
& Stewart) the shallows DREW DOWN 8 cm and the wet front retreated. Making the advection second
order made it worse (sharper bore, same wrong jump). The conservative form in (d, q) conserves the
momentum flux across the bore by construction, and run-up and set-up appear with no tuning. The
cost is one HLL flux per face per stage (two stages), ~2× the velocity form — fine for a band.

Also found on the way: (1) `float` surfaces walk the mass ledger off by 1e-6 relative over a
thousand steps (rounding of `bed + d` per column per step, 270 columns): the surface is a `double`
and the ledger is now exact to 1e-9 with zero clamps (`MassExactWithWetDry` prints both); (2)
`prescribe()` itself is an exchange (the ocean raising its own surface between steps) and is
counted into the next step's `exchanged`, so `Σ exchanged == mass_after − mass_before` closes;
(3) minmod lost 20 % of the swell over 36 m and reached 1.54 a at the wall — the MC limiter keeps
0.27 of 0.30 and reaches 1.75 a (theory 2 a for a perfect standing wave).

**G1 ledger (all red-before-green, Release):**

| test | measured | gate |
|---|---|---|
| `StillWaterOnASlopeStaysStill` | max speed 0 after 600 ticks, mass exact, every column at its still level | < 1e-6 m/s, 1e-9 |
| `CarriesTheSwell` | 0.274 at 24 m, 0.271 at 36 m, arrival 2.25 s (c 4.68 m/s), ledger closes to 1e-5 | ≥ 0.24 / ≥ 0.18, < 3.56 s |
| `MassExactWithWetDry` | 20.000001192 → 20.000001192 m³, clamped 0, slope wet below the final line | 1e-9, 0 |
| `WallReflects` | 0.349 at the wall for a 0.20 incident | ≥ 0.32 |
| `RunUpOnTheBeachVsHunt` | 0.175 m, Hunt 0.145 m | ±20 % + 2 cm |

**The band (`ShoreBand`, built the same day).** A camera-following square of solver columns:
the box is centred on the camera pulled up to R/2 toward the centroid of the stored water within
2 R (so a camera on the sand gets its ring in deeper water), the bed per column is the micro
occupancy's highest solid (Unknown = a wall, the §5.1 hold rule), the still level is the ring's
most common stored top, the ring (`inner` columns from the box edge) is the ocean: prescribed
from `seaSwellColumn` at the sheet's own clock (`WaterRenderPipeline::waveTime()`), ring columns
shallower than `minOceanDepth` are a sponge, dry ones are free. The ring is a **relaxation zone**
(Larsen & Dancy 1983) with the weight `(q / ramp)²` anchored at the free region in METRES — so
the ring's extent beyond the ramp is hard ocean and a pure cost bound
(`ShoreBandTest.InteriorIndependentOfOuterWidth`: 1144 free columns, 0.0000 m difference between
rings 6 and 12 wide). Three things the band tests caught before any capture, each measured:

| finding | measured | fix |
|---|---|---|
| a hard-reset ring pumps mass: the prescribed progressive wave carries the Stokes transport a²c/2d shoreward and the reset lets nothing back | 940 m³ into a 48 m band in 30 s, the sand 0.4 m under water | relaxation zone + the ring's velocity carries the compensating mean current −a²c(d)/2d² per component (the undertow a closed beach needs): 28 m³ / 30 s, mean rise 5.6 cm (set-up), −0.6 cm after calm |
| the deep-water `η c / d` blows up in the shallows (a 0.45 m swell in 5 cm of water = 42 m/s) | max speed 108 m/s, a 17 m surface | Airy at the column's depth, `c(d) = √(g tanh(kd)/k)`, Froude-limited, and the prescribed height depth-limited to γ d (McCowan 0.78) on the four components' total; ring columns under 1 m are a sponge |
| the foam marker read every 1 m voxel step as a breaking front | calm Coast foam 0.085 | the surface step beyond what the bed explains |

The band writes nothing: its water is the stored ocean's (`exchanged` is the ledger) and the stored
spans under it are untouched (S12 row c: 172.0 → 172.0 m total depth over the shelf). An edit under
the band re-beds that column next tick (`noteEdit`; water over a new hole falls to it, ground
rising through water buries it — an edit never creates water). Unit rig (a 1:20 beach toward +z,
the sheet's Coast swell blowing shoreward, 48 m band at 1 m columns): peak run-up **0.275 m vs Hunt
0.281 m** on the spectrum's total height, 0.5 ms per tick for 2304 columns.

**L4 on the Coast bench (Release, `tools/water_feel.py S12`, evidence
`docs/evidence/water_feel/S12_coast_core_h1_auto_20261009_104243.json`, captures
`docs/evidence/water_core_g/`):** the band is sited from the shore_eye vantage (camera at z 664
on the sand, box 115..210 × 639..734, centre pulled 24 m seaward), 96 × 96 columns = 9216,
1767 prescribed / 141 walls / 7308 free, still 16.0. Rows, all PASS:

| row | measured | gate |
|---|---|---|
| swell: rest-dry sand wet | first swash column 0.28 s after siting | ≤ 5 s |
| swash surface over rest-dry columns | peak 0.199 m above still, 352 columns awash at 20 s | 0.05–0.6 m |
| no pump | mean free rise ≤ 0.020 m | ≤ 0.05 |
| stability | max speed 2.08 m/s | ≤ 5 |
| cost | step p95 2.35 ms, max 2.50 ms (+ mesh 0.49 ms for 4095 top quads) | p95 ≤ 4 ms |
| calm control (amplitude 0, 20 s) | foam 0, swash surface 0.053 m (27 % of the swell peak) | foam < 0.02, < 50 % |
| persistence | stored spans over the shelf 172.0 → 172.0 m, max top 16.0 → 16.0 | unchanged |

**What the Coast terrain is, and what that means for the look.** The beach there is a staircase of
full cubes: bed 17 south of z 674, a **24 m shelf at bed 16 = exactly the still level** (z 674–697),
then bed 15 from z 698 (profile `tools/water_shore_profile.py`). So the "1:20 beach" is a 1 m riser every
~22 m, the swell meets a vertical step, and the swash is a bore that spills over a flat shelf at
sea level — a 5–20 cm sheet that cannot drain (the shelf is at the still level; it is awash) and
keeps spreading (wet columns 3782 → 4135 in 20 s). Hunt's formula has no meaning on that profile,
which is why the run-up gate lives in the unit tests. Visually (`coast_shore_eye_before/after/
calm.png`, one stated pose): the sheet is masked under the band and the band's mesh draws the
shelf sheet and the ring's swell, but it reads as a flat pale surface against the sheet's big swell
beyond the band — the simulated water lacks the sheet's ripple detail and has no foam, so a 20 cm
bore on a 14 m wave is invisible from eye height. **That is G2's job (foam + surface detail from
the solver's own velocity), and the honest verdict is that G1 is measurably right and not yet
visibly right.** Logged gaps: the sheet draws the un-limited swell beside a band whose ring is
depth-limited (a height seam at the band edge in shallow water); a shelf exactly at sea level is
awash by construction; the band's seaward edge face is a vertical water wall under the sheet.

### 18.6 G2 built: surf foam and surface flow from the solver (2026-10-09)

**What was built.** The surface field carries two more things per column — `foam` (0..1) and the
depth-averaged surface velocity `(u, w)` — so `SurfaceColumn` is 16 floats (`wc_surface.comp`
writes the layout; the 3-D volumes' kernel writes zeros for the new three until their surface
velocity is exported; GPU parity 13/13 unchanged). The mesh vertex grows to 48 B with `foam` and
`flow` on the TOP run's vertices only (deeper runs are still), `water_surface.vert/frag` hand them
to the shared shading library, which already knew what to do with them: `flowDir`/`flowStrength`
advect the ripple normal and streak the whitewater, `foam` is the whitewater, and roughness rises
with the flow (0.35 → 0.80 at 2 m/s). A debug tap `water_render_core {debug: 6}` paints foam red
and flow strength green — the deterministic check that the solver's fields reach the pixels.

**Where foam is made (the solver, in water under 1.5 m):** (a) a bore front — the surface steps up
beyond what the bed explains; (b) converging flow (div < −0.3 /s); (c) the swash tongue — a column
dry this substep and wet now, foaming with its speed (0.3–1 m/s); (d) supercritical flow (Froude
> 0.8) deeper than 8 cm. Half-life 0.46 s; still water makes none; films under 2 cm make none and
count as no flow for their neighbours. Each gate was forced by a measurement on the Coast's
sea-level shelf: the first tongue rule foamed the creeping calm-water film (calm foam 0.21), the
ungated Froude rule foamed every draining trickle over a riser (calm foam 1.0), the first marker
read every 1 m voxel step as a breaker (0.085). Pinned: `StillWaterOnASlopeStaysStill` (foam 0),
`MassExactWithWetDry` (the dam-break bore foams to 1.0), `SwellRunsUpTheSandAndDrainsBack` (1.0
during the swell, 0.000 after calm), `FoamAndFlowReachTheTopRunsVertices`.

**L4 on the Coast (`S12`, evidence `S12_coast_core_h1_auto_20261009_110714.json`, PASS):** foam
peak 1.0 during the swell, 0.0001 after 20 s of calm; every G1 row unchanged (swash 0.25 s / 0.199 m,
mean rise 0.020 m, p95 2.35 ms for 9216 columns). Captures `coast_shore_elevated_g2_foamtap.png`
(the tap: flow green over the whole band, foam as orange patches along the shelf and the riser),
`coast_shore_elevated_g2.png` and `coast_shore_eye_g2.png` (the look).

**Honest visual verdict.** The band's water now moves (the ripple detail travels with the flow and
the surface is choppier where it runs) and whitewater appears where the solver breaks — but as
scattered 1 m patches, not the continuous white line a surf zone draws. Two causes, both real: the
band runs at 1 m columns, so a 20 cm bore is one column wide and its foam is one block; and the
Coast shelf is flat at sea level, so there is no steady breaker line, only spill-overs at the
riser. The sheet's own whitecaps beyond the band still look richer. Next levers, in order:
⅓ m columns for the band nearest the camera (9× the columns; 1 m stays the outer band), foam that
rides the flow (advected, not only made and decayed), and the G3 look profile so band and sheet
share one colour/clarity. The sheet/band height seam (§18.5 gaps) is unchanged.

### 18.7 G3 design: the per-body look profile (2026-10-09, before building)

**The ask (owner, 2026-10-09):** change the water's colour shade, murkiness and clarity. **What
exists:** per-column `turbidity` (W2, derived from the body's mean depth) and `roughness` (W3,
Cox-Munk from the wind) reach the sheet through the hydrology grid's B/A channels; the simulated
water's mesh (volumes + shore band) hard-codes the neutral profile; a global debug override
(`water_look {active, turbidity, roughness}`) is the W1 positive control. Nothing is per body and
nothing persists.

**The profile — four knobs, each optional (unset = today's derived value, so no default changes):**

| knob | unit / range (clamped at the route, said so in the echo) | what it does in `water_common.glsl` |
|---|---|---|
| `clarity` | metres, 0.1–100 — the Secchi depth (how far down you can see a white disc) | extinction rescaled so its luminance-weighted mean is Kd = 1.7 / clarity (Poole & Atkins 1929, the relation the underwater fog already uses) |
| `tint` | linear RGB 0–1 — the colour deep water glows with (today's is (0.04, 0.18, 0.24)) | replaces the in-scatter colour and tilts absorption toward its complement (water looks green because it absorbs red and blue) |
| `turbidity` | 0–1 — murkiness | overrides the derived value: the existing clear→turbid mix |
| `roughness` | 0–2 — ripple strength | overrides the derived value: the existing ripple-slope scale |

**Where it lives.** `WaterLook` on `WaterBodyRecord` (persisted with the record in world_meta
`water_bodies` — the world owns it, like the recipe; an unset look serialises to nothing, so old
worlds are byte-identical). One resolver, `WaterBodyTable::lookAt(x, z)`: the generation body under
the column (the same bake-body query the write-back uses), else an av pond whose box holds it.
**Applied identically** to: the sheet (a second texel per grid column: tint + clarity; turbidity /
roughness overrides replace the derived B/A), the simulated mesh (per-field draw ranges, the look in
the push block — volumes resolve at their box centre, the band at its centre), and the underwater
overlay (clarity sets the fog distance). Sheet and band resolve the same body at the shore, so they
cannot disagree.

**Design keys.** (1) Aesthetic: optics only, no geometry. (2) Chunks: the look is a function of the
body under a world column; the band/volume use one look per field, resolved from world position —
no chunk quantity. Equality: `WaterLookTest.ResolverIsAFunctionOfTheColumn` (same answer whatever
order and from either table copy). (3) Generation: none; the per-world values persist in the world
DB, never global JSON. (4) API: `water_look {body | at:[x,z], clarity, tint:[r,g,b], turbidity,
roughness, clear}` echoes the stored look, the resolved packed values, the body's class and what was
clamped; without `body`/`at` the route keeps its W1 positive-control behaviour unchanged. (5) Test:
red unit tests (record round-trip with and without a look, clamps, the resolver, mesh draw ranges);
L4 on the Coast at the elevated pose: set the ocean to clarity 2 m + a brown tint → the band region
AND the sheet region both shift toward brown (mean colour distance > 0.05), the sand control region
stays within 0.01; `clear` restores the band region to within 0.02 of the before capture;
`save_world` + cold restart → the look echoes back identical. Rig = the shipped Coast bench, one
variable (the look), defaults untouched.

**G3 built (2026-10-09) — ledger.** As designed, with one finding that changed the resolver:

- **Finding: the Coast sea is no body.** `water_look {at:[165, 720]}` found nothing: the sea there
  exists only as stored water in the chunks; the coarse generation map has no body under it (its 9
  "oceans" are elsewhere). A look keyed only to that map would miss the very water the owner looks
  at. So `at` on stored water that no body owns **names it**: a flood over the resident stored tops
  at the seed's level (±5 cm, capped at 1 M columns) becomes a `region` record — box + level + look,
  **no mass role** (never credited, not in the av ledger; pinned by
  `RegionsMatchByBoxAndLevelAndStayOutOfTheMassLedger`). Box-matched records now also match the
  column's stored LEVEL when it is known, so a pond behind the beach inside the sea's box is not the
  sea. Coast: region −1, `sea` (open: it reaches the resident edge), level 16.
- **Built:** `WaterLook` (+ clamps that report, JSON, the shader packing) on `WaterBodyRecord`
  (persisted only when set); `WaterBodyTable::bodyAt/lookAt/addRegion`; the sheet's grid holds two
  texels per column (data, look) and `water.vert/frag` read both; the mesh draws one range per field
  with the look in a 128-byte push block; volumes resolve at their box centre, the band at its centre
  and still level; the underwater fog takes the eye's clarity (VIS_CLEAR × Z / 10.8); route
  `water_look {body | at, clarity, tint, turbidity, roughness, clear}` (the W1 control unchanged
  without `body`/`at`). Unset knobs = derived, so defaults are unchanged (the derived clear water's
  clarity is 10.8 m, its tint (0.04, 0.18, 0.24) — both echoed by the route).
- **Unit (red first — the API did not exist):** `WaterLookTest` 6/6; all water tests 236/236; GPU
  parity 13/13; full unit suite 4200 passed, 2 failed = the two standing failures recorded in
  AnimationSystemV3Plan / DebrisInteractionPlan (`AtlasManagerTest.BuildAtlasFromSourcePNGs`,
  `FineFaceMerge.SubcubeMerge_CrossCubeSplitsOnLightBoundaryBetweenCubes`), neither in a touched file.
- **L4 (Coast, Release, `tools/water_look_l4.py --restart-cmd …`, evidence
  `docs/evidence/water_core_g/coast_look_l4_20261009_115029.json`, captures `coast_look_before /
  after / restored.png`), PASS on every row:**

| row | measured | gate |
|---|---|---|
| band region colour change (clarity 2 m, tint [0.20, 0.13, 0.04]) | 0.182 | > 0.05 |
| sheet region colour change | 0.270 | > 0.05 |
| sand control | 0.000 | < 0.01 |
| `clear` restores the band | 0.0012 (sheet 0.0018) | < 0.02 |
| save_world + cold restart | look echoed identical (clarity 2.0, tint [0.20, 0.13, 0.04]) | identical |

  The capture shows the band and the sheet turning the same murky brown with no seam between them.
  The bench was left as found (look cleared and saved).
- **Gaps (logged):** an open region's box is fixed when named (water that streams in later outside
  it takes the derived look until named again); the underwater overlay resolves by box only (no
  stored top at the eye); a region is not re-derived when the stored water changes level.

### 18.8 G4 design: the fine band (⅓ m near the camera) (2026-10-09, before building)

**Why.** §18.6's honest verdict: at 1 m columns a 20 cm bore is one column wide, so its foam is a
1 m block and the swash edge is a staircase. The whole band at ⅓ m is 9× the columns (82 k at
radius 48 — over the 64 k cap and ~20 ms); the near field is where the eye resolves it.

**Design: one-way nesting** (the standard way coastal models refine a region). A second
`ShoreBand` at ⅓ m, radius 12 m by default, sited near the camera (pulled up to R/2 toward the
1 m band's wet columns, clamped inside the 1 m band's free region). Its ring is a relaxation zone
like the outer's, but driven by the **1 m band's own solution** — surface and velocity sampled
bilinearly at each ring column, every tick, after the 1 m band steps — instead of the sea's swell.
Ring columns where the 1 m band is dry are free (the land edge). Its bed is the micro occupancy at
⅓ m column centres, so subcube terrain finally shapes the swash. The 1 m band keeps running
everywhere (it is the fine band's boundary condition) and **hides its columns under the fine box**
when it builds its surface field (no top, no lateral faces), so exactly one surface is drawn
there. One-way: the fine band does not feed back; mass is the 1 m band's (the fine band's
`exchanged` is reported, not added to any ledger — it is a refined view, like a zoom).

**Design keys.** (1) Same lattice idiom, finer: ⅓ m is the subcube size. (2) The fine box is
camera-derived (cost/coverage); its interior must be independent of the fine ring's width (the
§18.3 test, repeated for the nest); bed and driver are world data. (3) No generation. (4) API:
`water_shore {fine, fineRadius (m, 4–16), fineInner (columns, ≥ 6), fineRamp (m)}` echoes a `fine`
record (box, columns by role, cost). Clamp: fineRadius ≤ 16 (96 × 96 = 9216 columns, the 1 m
band's own size). Defaults: `fine` OFF until measured, then decided. (5) Tests, red first:
`ShoreBandTest.NestedStillStaysStill` (calm: nothing moves, foam 0), `NestedFollowsTheOuter`
(time-mean surface over the shared free region within 2 cm; run-up within ±30 % of the 1 m band's on
the smooth synthetic beach), `NestedInteriorIndependentOfRingWidth` (1 cm), `HiddenColumnsDrawNothing`
(mesh: no quads under the hidden box, no lateral faces at its edge). L4 on the Coast (S12 rows with
the fine band on): every G1/G2 row still passes, the fine band's swash-edge foam fraction (wet free
columns next to a dry one carrying foam > 0.2, time-averaged) is reported beside the 1 m band's,
cost p95 of both bands together ≤ 6 ms, and shore-eye captures before/after at one pose.

**G4 built (2026-10-09) — ledger.** One-way nesting as designed (`ShoreBand::siteNested`, `sample`,
`setHiddenBox`; route `water_shore {fine, fineRadius, fineInner, fineRamp}`, a `fine` record in the
echo; harness `water_feel.py S12 --shore-fine`; captures `tools/water_fine_captures.py`).

- **Unit (red first):** `NestedStillStaysStill` (calm: speed 0, foam 0, every column at rest),
  `NestedFollowsTheOuter` (2304 free columns: time-mean |fine − 1 m| 3.0 mm mean, 7.9 mm max; run-up
  0.325 m fine vs 0.275 m 1 m band; swash-edge foam 1.0 % vs 1.1 %), `NestedInteriorIndependentOfRingWidth`
  (0.0000 m), `HiddenColumnsDrawNothing` — 10/10 band tests.
- **Two siting findings on the Coast.** (1) A pull of R/2 toward the wet centroid left the fine band
  on dry sand 17 m short of the riser, holding no water at all. (2) "The nearest resting waterline"
  then found a pond beside the camera. The fine band is now centred on the nearest resting waterline
  **of the ocean** — submerged-at-rest columns flooded from the 1 m band's driven ring — and it sits
  on the shelf and the riser (box 159–191 × 669–701).
- **L4 (`S12_coast_core_h1_fine_auto_20261009_143919.json`, PASS):** every G1/G2 row holds with the
  fine band on (swash 0.28 s / 0.199 m, mean rise 0.021 m, max speed 1.85 m/s, calm foam 0). Fine band
  96 × 96 = 9216 columns at ⅓ m (2152 ring / 7032 free / 32 walls). **Both bands together: step p95
  5.65 ms** (1 m band alone: 2.30 ms) — inside the 6 ms gate, but 2.4× the cost. Swash-edge foam
  fraction 0.65 % fine vs 0.74 % 1 m band (the fine band sees 4.5× more edge columns).
- **Honest visual verdict (`g4_eye_*`, `g4_elevated_*`, `_foamtap` captures).** The fine band is
  there — the foam tap shows ⅓ m cells over the shelf and riser — but the look barely changes and the
  foam is still scattered patches. **Resolution was not the limit.** The Coast shelf is flat AT sea
  level (full-cube terrain), so the swell never breaks on a slope; it spills over a 1 m riser as a
  5–20 cm sheet, and no column size makes a surf line out of that. A surf line needs a beach that
  shoals: a slope below sea level. **Decision: `fine` stays OFF by default** (2.4× the cost for no
  visible gain on the bench we have); it is kept, tested and one flag away for a sloped beach.
- **Next lever, therefore, is the terrain, not the water:** a shore test with a real sub-voxel slope
  (a hand-built rig on the Small bench, ⅓ m steps from 2 m deep to dry over 20–40 m), then — if the
  surf line appears there — the generator's coastal profile (logged in StructurePipelineGaps).

## 19. Phase E design - coupling: small bodies that respond (2026-10-09, before building)

**The ask (owner, 2026-10-09):** "I want to witness small bodies of water that can be splashed with
debris, that respond to external stimuli. Until we figure out small body water physics and physical
feel we are just wasting our time on large body stuff." Phase G is paused for this.

**Ground truth (read, file:line in the session's map):**
- Coupling today is ONE-WAY and points at the OLD water. GPU debris (`solver_integrate.comp:86-104`)
  reads a per-column tile {top surface Y, flow xz} built by `DebrisRuntime::updateWater` from
  `WaterManager::columnWater` (the CA / hydrology table / sea). CPU bodies (`VoxelDynamicsWorld`
  `setWaterQuery`) and characters (`WaterHooks`) read `WaterManager` too. **Nothing reads WaterCore**:
  a WaterCore pond is invisible to debris, bodies and characters. **Nothing writes into any water.**
- WaterCore has `addImpulse(world, radius, deltaSpeed, dir)` (CPU only) and a `water_av_impulse`
  route; on a GPU-backed volume the impulse is written to the CPU grid and never uploaded (no
  `gpuDirty`, no wake) - **an untested bug**. Occupancy is static only (no moving solids).
- The volumes' surface field carries foam 0 / velocity 0 (§18.6), so a pond has no flow to give.
- The pond rig exists (WaterBench_Small, chunk [3,0]: a 4x4x2 pit, S8 blast_near/blast_far poses).
  No harness implements S6/S7/S8; no water sound exists; no debris-into-water splash exists.

**Slices, in the order the owner will SEE them:**

| slice | what you witness | mechanism |
|---|---|---|
| **E1 stimuli** (S8) | a blast beside the pond shoves the water away; it sloshes back and settles | the blast (`/api/damage/apply`, and every in-game blast through `DamageSystem`) adds an impulse to every awake volume within its radius: a radial face-velocity kick, strength from the blast's energy (Delta u = J / (rho V_affected)), falling off with distance; fix the GPU impulse path (upload + wake); `water_av_impulse` keeps working on both backends |
| **E2 debris in** (S6-lite) | debris dropped/blasted into the pond splashes it (a dent and an outgoing ring), floats or sinks by material, drifts with the water, and the pond keeps moving after | two-way **momentum exchange** for sub-cell bodies: (a) water -> debris: the debris water tiles read the WaterCore volume where one is awake (its surface field top + its surface velocity), so buoyancy/drag/current act from the REAL water; (b) debris -> water: the integrate pass records, per debris particle in water, the momentum the water took from it (m Delta v of the drag + current terms, m from the material density) into a host-visible exchange buffer (the event-readback pattern); next frame the CPU applies each as an impulse into the volume at that point (equal and opposite - Newton's third law, so the ledger of momentum closes); mass is not exchanged (debris volume displacement is below a cell for subcube/microcube debris and is logged as the known approximation) |
| **E3 bodies** (S6) | a crate dropped into the pond splashes, displaces the level by its submerged volume, floats at its density's draft | CPU rigid bodies (crates, furniture) as **moving solids** in the volume: cells under the body's box are solid with the body's velocity on their faces, so the projection pushes the water out (real displacement, real splash) and the pressure integrated over the box gives buoyancy + drag; replaces the table law for bodies inside an awake volume |
| **E4 wading** (S7) | a character walking through the pond pushes a bow wave and leaves a wake | the capsule as a moving solid (E3's path) |

**Also required by E2:** the volumes' surface field exports the top wet cell's horizontal velocity
(the F2 leftover - it is also what makes a pond's own ripples travel and foam form when it is
splashed).

**Design keys.**
1. *Aesthetic:* nothing new is drawn; the response IS the water's surface mesh (F1). Debris stays the
   engine's microcube/subcube idiom.
2. *Chunks:* the exchange is per particle at a world position; the tile override is per world column
   from the volume (a world-space object); no chunk quantity decides any look or force. The debris
   tile directory is a residency window (cost bound, existing). Equality test:
   `WaterCouplingTest.ExchangeIndependentOfOrderAndBatching` - the same set of exchange records applied
   in any order or split across frames gives the same volume velocity field (impulses are additive).
3. *Generation:* none.
4. *API:* `water_av_impulse` unchanged (now works on GPU volumes); a `water_coupling` route reports
   {exchange records last frame, momentum given to water (kg m/s, vector + magnitude), debris in
   volumes, tiles overridden by volumes, enabled} and toggles `{enabled}` (the A/B control).
   Clamps: an exchange's Delta u is clamped at 20 m/s (the route's existing impulse clamp), counted.
   Defaults: coupling ON where a volume is awake (it changes nothing where none is).
5. *Tests:*
   - **Unit, red first:** `WaterCouplingTest.ImpulseConservesMomentum` (the volume's total momentum
     rises by exactly the applied J within float tolerance; mass unchanged); `GpuImpulseMatchesCpu`
     (integration: the same impulse on a CPU and a GPU volume gives the same velocity field after one
     step - the bug's red test); `SurfaceFieldCarriesVelocity` (a volume with a known uniform flow
     exports it); `DebrisTileReadsTheVolume` (a column inside an awake volume reports the volume's
     surface, not the CA's); `ExchangeIndependentOfOrderAndBatching`.
   - **L4 on WaterBench_Small, pond rig, Release, harness rows:**
     **S8** blast at blast_near: the near-side surface drops >= 0.3 m below rest within 1 s, the far side
     rises, the pond returns to within 1 cm of flat in < 8 s, mass exact; control blast_far: max surface
     deviation < 1 mm. **S6-lite** 20 subcube debris (stone, buoyancy < 1, and wood, > 1) dropped from
     3 m into the pond: max surface deviation >= 5 cm within 0.5 s of entry, stone ends on the floor,
     wood ends floating at its draft (f = 1/b +- 0.1), the surface returns to flat < 8 s, mass exact,
     momentum given to water > 0; control: the same drop with coupling off -> deviation 0 (the A/B).
   - **Witness:** the owner watches it live; captures before / during / after at one stated pose and a
     short frame sequence for each row.
   - Rig vs defaults: the pond rig is shipped bench content; nothing differs from defaults.

**Out of scope for E1-E2 (logged, not hidden):** splash droplets/spray above the surface (the crown) -
the FLIP layer exists as mass transport but there is no droplet render; volume displacement by sub-cell
debris; water sounds. These are E-later rows once the owner has seen E1/E2.

### 19.1 Design-check verdict (2026-10-09): E1 + E2 READY, E3 + E4 NEEDS WORK

Additions the gate required (folded into the build):
- **Tiles under an awake volume rebuild every frame** (like the CA's live tiles); a debris tile
  column's value = the highest volume top over the voxel's 1/3 m sub-columns and their mean surface
  velocity - a function of the world column, not of the chunk. Seam test
  `WaterCouplingTest.VolumeTilesSeamFree` (a volume straddling two chunk columns gives the same column
  values through either tile).
- **The exchange is the drag + current terms only, never buoyancy.** A floating body's weight is
  carried by static pressure; re-applying it as a velocity kick would drive a false downward current.
- **Blast -> water:** every awake volume within 1.5 x the blast radius gets a radial kick; water at r
  receives the speed a 1 m^3 water element gets from the blast's own impulse there,
  Delta u = J(r) / (rho_w * 1 m^3), J(r) from `DamageSystem::blastImpulse` with its linear falloff -
  the law that sizes the push on loose debris (engine mass units: water = 1.0 per m^3, the same as the
  solver's density; Wood mass 0.7 has buoyancy 1/0.7). **Clamp 20 m/s per kick, counted** (prevents a
  near-dry cell receiving an unbounded speed that blows the CFL step and the projection).
- **Known approximation, logged:** today's drag law damps debris toward ZERO, the current term toward
  the water's velocity; drifting debris lags the water at ~57 % (3.0 / (3.0 + 2.3)) - the two terms
  balance, so there is no net momentum pump.
- **Standing gate:** the change to `solver_integrate.comp` must pass `tools/debris_settle_bench.py` on
  DebrisLab in the same commit (water is disabled there: the bench proves the change is inert).
- **E3/E4 re-gate before building:** moving solids need (a) a ledger-exact rule for the water in a cell
  that becomes solid, (b) solid face velocities on the GPU solver, (c) the pressure integral over the
  body as its force and torque.

### 19.2 E1 built: blasts reach the water (2026-10-09) - ledger

**Built:** `WaterSolver::addRadialImpulse` (radial, linear falloff, 0.3 up-bias, 20 m/s clamp per face,
counted; only faces that border water - air carries no mass); `WaterCoreManager::addRadialImpulse` and a
fixed `addImpulse` (both: the volume's current state first, the write, then upload + wake - the bug the
code map found: a kick on a GPU volume was written to the CPU grid and never reached the GPU);
`DamageSystem::setBlastListener` (one hook for every blast from every caller; off with `push:false`) and
`blastSpeed(E)`; Application wires blasts -> every awake volume within 1.5 x the radius; route
`water_coupling {enabled}` echoes the last blast's kick (volumes, faces, clamps, speed, reach).
**Correction to 19.1:** materials.json's masses are NOT on one scale (Wood 0.7 x buoyancy 1.429 = 1.0,
Stone 6.0 x 0.385 = 2.31), so the blast law is applied as a SPEED: water at r gets the speed a loose
reference piece at r gets from the same blast, `BASE_SPEED * sqrt(E / toughness_Stone) * (1 - r / reach)`.

**Unit / integration (Release):**
- `RadialKickFollowsTheBlastLaw`, `BlastSpeedIsTheLoosePieceSpeed` - pass.
- `BlastBesideThePondMovesItAndItSettles` (the S8 shape, CPU): near side -0.305 m, far side +0.299 m in
  the first second; KE 1.57 at 1 s -> 0.128 at 30 s (no regrowth: late max 0.26); mass exact; the 21 m
  control kicks 0 faces. **The original gate "flat within 1 cm in < 8 s" was re-based** - not physical
  (a 4 m pond 2 m deep sloshes with a ~1.8 s period and real wall friction damps it over minutes); the
  gate is now energy decay >= 5x and no regrowth.
- `GpuImpulseMatchesCpu` (the bug's red test): with the pre-E1 behaviour restored by a deliberate
  mutation the GPU pond stayed flat (0.0002 m vs CPU 0.299 m) - red; with the fix it moves.
  `GpuKickedPondParity` (diagnostic). 244/244 water unit tests, 15/15 GPU parity tests.

**L4 live (WaterBench_Small pond, Release, realtime, default GPU backend; `tools/water_e1_demo.py`;
evidence `docs/evidence/water_core_e/e1_gpu_*` and `e1_cpu_*`):** the blast at (106, 17, 13.5), E 62,
radius 4, broke nothing (pond wall whole); 13,513 faces kicked at 3.0 m/s. GPU: near side 16.50 ->
16.26, far side -> 16.76 within 0.4 s, sloshes, back within 1-2 cm of rest at 10 s, mass 24.000000 m^3
throughout. CPU: near -> 16.22, far -> 17.00 (the rim), still sloshing at 10 s (far 16.90). The far
control blast kicked 0 faces and moved nothing. Frames: a ragged crest piles against the far wall at
0.4 s, the surface tilts and smooths by 1.5 s, flat at 10 s.

**Open defects this surfaced (measured, logged, not hidden):**
1. **A disturbed pond does not read flat again.** 30 s after the kick (CPU): interior cells hold
   0.995-0.996 instead of 1 and the missing fraction sits on top as a film of 0.03-0.1 cells; surface
   standard deviation 4 cm, 0.42 m^3 above the rest level; KE still 0.13. The known compaction weakness
   (liquid cells projected as full, compaction only in quiet cells) - a core (B) row, and exactly the
   "surface look of every rest state: flat, not noisy" feel item.
2. **CPU and GPU respond differently to the same kick.** After 20 ticks the near side drops 0.21 m on the
   CPU and 0.18 m on the GPU with a CONVERGED projection (80 sweeps, residual 4e-4); vertical face
   velocities already differ by 0.06 m/s after one tick (the kick itself is identical). The existing
   parity tests compare only a dam-break front within one cell, so this was never caught. Live, the
   GPU's auto sweep count (27) leaves residual 2.4 and acts as extra damping. A Phase C row.
3. The GPU volume record's `kineticEnergy` is stale (constant 6.42 across steps in the test) - the
   field cannot be trusted on the GPU backend.

### 19.3 E2 built: debris and the pond, both ways (2026-10-09) - ledger

**Built:**
1. **The pond exports its surface velocity** - `wc_surface.comp` (bindings + u, w) and
   `extractSurfaceField` write the top run's top cell's horizontal velocity into `SurfaceColumn.u/w`
   (the F2 leftover: a pond's own ripples now drift with its flow too). GPU = CPU exactly (parity 0).
2. **Debris reads the pond** - `DebrisRuntime::setVolumeWater` + `composeColumn`: a column an active
   volume holds water in reads the volume (`WaterCoreManager::columnWater`: the highest top over the
   voxel's 1/3 m sub-columns, the mean surface velocity of the wet ones) ahead of the old water; tiles
   under a volume box rebuild every frame; a volume alone is a water source (the Small bench has the old
   water off).
3. **Debris writes back** - `solver_integrate.comp` appends, per wet body per tick, the velocity change
   the water's drag + current gave it (buoyancy excluded) and its mass in water units (volume / the
   material's buoyancy ratio) into a host-visible per-slot buffer (`PHX_WATER_EXCHANGE`, cap 4096 per
   frame, overflow counted - the event-readback pattern); `WaterCoreManager::applyMomentum` gives the
   water -mass x dv near each piece (`WaterSolver::addMomentum`: per-axis face weights so the
   cell-centred momentum rises by EXACTLY the record - the first version gave 1.17-1.5x and the unit test
   caught it); one GPU sync + one upload per touched volume per frame. `water_coupling` echoes the
   exchange (records, applied, outside, dry, clamped, momentum) and the volume tiles.

**Unit / integration (Release, red first):** `SurfaceFieldCarriesVelocity` (0 before), `DebrisTileReadsTheVolume`
(the CA's 10 m before; + the seam check), `MomentumIsGivenExactly` (given 0.020 / -0.050 / 0.010,
measured the same), `ExchangeIndependentOfOrderAndBatching` (< 1e-6) - 10/10 coupling tests; GPU
surface parity incl. velocity 0.0; 285 debris + water unit tests pass. **Debris settle bench on DebrisLab
(the standing gate for `solver_integrate.comp`, on the E2 build):** same verdicts as the 2026-10-05
baseline (drop_pile and blast FAIL on forced sleeps as before; box_through_pile the known open band);
blast's forced sleeps 6 / 4 / 3 over three runs vs 3 at baseline = the bench's run-to-run spread; the
exchange cannot run there (no water). A first bench run was INVALID - it ran the stale root exe (E1);
discarded and re-run.

**L4 live (Small pond, Release, GPU default, `tools/water_e2_demo.py`, evidence
`docs/evidence/water_core_e/e2_*`):** 10 Stone + 10 Wood subcubes dropped from 3 m into the 1.5 m pond.
- **Stone sinks, Wood floats - from the pond itself.** All 10 Stone sleep on the floor (y 15.167); 8 of 10
  Wood sleep floating at y 16.45-16.57 (surface 16.5) - the capture at 12 s shows the wood blocks on the
  water. The prediction was 16.43 +- 0.05 (70 % draft): they float HIGHER than predicted (the debris law
  takes the submerged fraction of the bounding SPHERE, not the cube) - recorded as measured.
  Control (coupling off): every Wood piece sinks to the floor (debris cannot see the pond).
- **Momentum reaches the water:** 4015 exchange records applied, 0 dropped, 0 outside, 0 clamped, total
  4.92 m^3 m/s; mass 24.000000 throughout.
- **The splash is NOT visible.** At 0.9 s the pieces are already under the surface and the surface shows
  no crater or ring. The surface metric could not tell either: the pond's resting noise (films over
  0.996 cells, 19.2 defect 1) spikes 0.168 m in the control as well. Causes (inferred, to be measured
  next): (1) the debris water drag is LINEAR (90 % per second), so a piece entering at 7.7 m/s hands its
  momentum over ~1 s instead of at impact - real entry drag is quadratic, 1/2 rho C_d A v^2, a = 1/2 C_d b
  v^2 / s ~ 34 m/s^2 for a stone subcube at 7.7 m/s; (2) sub-cell debris displaces no water; (3) the 3.5 cm
  noise floor hides small disturbances.

### 19.4 The owner's look at E1/E2 (2026-10-09): not signed off - what was measured after

The owner ran the demos and reported: bad FPS, no ripples, jitter - and asked whether anything was
tested before it was handed over. It had not been tested the way the owner looks at it (in motion).
Measured afterwards on the Small bench (Release, GPU volume, nothing changed in code):
- **Frame cost is not the water:** the engine's frame time is 2.9-3.7 ms (280-350 fps) in every state -
  still pond, probed at 30 Hz, 10 wood floating, after a blast; the water's share is ~0.8 ms. Each
  `/api/screenshot` round trip takes ~0.8 s and the demo scripts take ~8 of them (plus 16-32 probes per
  sample) - the likely source of the choppy picture and of the 16-64 fps the status bar showed in the
  captures. Likely, not proven: the engine counter does not see what the owner's screen showed.
- **No ripples:** the 1/3 m surface cannot carry waves shorter than ~2/3 m; 10 wood pieces moved the
  surface at most 2.4 cm (a blast: 20 cm) because the debris -> water transfer is spread over ~1 s by the
  linear drag; the old RippleField is wired to the old water only.
- **Jitter, two causes:** (1) the surface at a fixed point jumped up to 17 cm between samples 33 ms apart
  after a blast (3 jumps > 5 cm in 3 s) - half-cell film flicker (19.2 defect 1) popping the mesh;
  (2) floating wood never rests - 9 of 10 pieces still awake, bobbing at up to 0.38 m/s - debris reads
  the surface per 1 m column (the max of 3 x 3 sub-columns, which steps as the water moves) and nothing
  damps the bob.
- **Standing rule from here:** nothing is called done until the owner signs off; before handing anything
  over, watch it in motion without screenshot stalls and measure frame pacing, flicker and settling.
- **Proposed order:** the surface flicker (it causes the popping and hides any splash), then a finer,
  damped water reading for floating debris, then impacts that make visible ripples.

### 19.5 The surface flicker fixed: the height function (2026-10-09) - signed off with 19.6-19.7 (19.8)

**Cause (proven):** the drawn surface (and the probe) was "the fill line of the highest cell holding
>= 1 mm". With a part-full top cell (f 0.3) and a few mm of film in the cell above, the surface sat at
the 0.3 line until the film crossed 1 mm, then jumped a whole cell up - up to (1 - f) x h in one tick.
**Fix:** the standard VOF height function - a run's surface is its bottom plus the water in it
(sum of f x h), in `extractSurfaceField`, `wc_surface.comp` and `WaterGrid::surfaceWorldY`; a film adds
only its own millimetres. `WaterGrid::wetTipWorldY` keeps the old rule for what it is right for - how
high the water REACHES (run-up, crest gates): a sheet climbing a wall is a stack of part-full cells.

**Red -> green:** `SurfaceDoesNotOutrunTheWater` - on the blasted pond, every column, every tick for
6 s, the drawn surface may not move further in one tick than the water can (vertical speed + inflow
through the four side faces, x2): old rule 1951 / 51840 column-ticks violated, worst 0.318 m in a tick;
height function 0. (The first bound counted vertical speed only and flagged real sideways inflow - the
remaining jumps were all single-run columns whose surface moved exactly with their water; the bound was
corrected, and the old rule re-run against the corrected bound to keep the red honest.) Run-up re-pointed
to the tip query (fills 2.56 h0, FLIP 2.15 h0, as before). 288 water + debris unit tests, 15 GPU tests.

**Live (Small pond, GPU, `tools/water_motion_check.py`):** surface at a fixed point after a blast,
sampled every 33 ms for 3 s: largest jump 17.25 cm -> **1.39 cm**, jumps > 5 cm 3 -> **0**.
**Watched in motion** (ffmpeg recording of the engine window from outside the engine - no screenshot
stalls; `docs/evidence/water_core_e/hf_blast.mp4`): frame interval median 3.3 ms, worst 7.0 ms (only
the last 240 frames are held - not the blast moment itself). **Finding: the motion barely shows.** In
the pond region consecutive frames differ by under 1.2 grey levels (of 255) on average during the slosh
- the surface tilts ~25 cm over 4 m but the milky, uniformly-lit water reflecting a uniform sky gives
almost no visual cue that it moved. The look of moving water is its own problem (logged, next to the
ripples).

### 19.6 Floaters follow moving water, then rest (2026-10-09) - owner signed off (19.8)

**Defect (measured):** resting wood is put to sleep by the debris solver; pushing ONLY the water
(`water_av_impulse` 3 m/s beside the pond) swung the surface +-40 cm while **0 of 10 floaters woke** -
frozen pieces hung in air or sat buried. A blast woke them only because the blast pushed the debris too.

**Fix:** each frame `WaterCoreManager::takeMotion()` reports, per volume, one sphere around the columns
whose surface moved more than 1.5 cm from where it last counted as moved (a held reference per
column: sub-mm jitter never adds up, any real slosh does, calm water goes silent), and Application
wakes the sleeping debris inside it with `GpuParticlePhysics::wakeSphere` - a strength-0 impulse
(the solver wakes sleepers within IMPULSE_WAKE_SCALE x an impulse's radius; dv stays 0). Two traps
found on the way:
- `queueImpulse` silently drops strength-0 impulses, so the first build sent **0 wakes** (that run is the
  red: 0 woke, unchanged). `wakeSphere` is an explicit wake-only path; every other caller still needs > 0.
- **Surface flow is not a motion signal:** on a still GPU pond with no debris the top-cell surface velocity
  reads 0.08-0.10 m/s while no column moves 1 mm (OPEN defect - debris also reads that flow as current).
  A 5 mm cutoff tripped forever on the floaters' own stirring (0-2 columns per sample for 20 s); 1.5 cm
  (~5 % of a subcube - a frozen piece off by less is not visible) does not.

**Test:** `MotionIsReportedWhereTheWaterMoves` - a rested pond reports nothing (floaters may sleep); the
pushed pond reports one sphere covering the push. **OPEN, not asserted:** that small CPU pond never fully
calms - columns against the walls keep trading 2-4 cm per tick 10 s after the push (same family as the
SloshEnvelope holes), so on CPU it keeps waking debris. 298 water/debris/shore unit tests, 16 GPU tests.

**Live (Small pond, GPU):** water-only push, 10 wood resting: all 10 wake at once, piece-minus-surface
gaps shrink from -0.36..+0.29 m to within about +-0.1 m by 1.5 s, and 10 awake -> 1-3 by 5 s; a trickle of
wakes continues (a few columns rock +-7 mm). **Watched in motion** (`tools/water_motion_check.py
--scenario floaters`, `docs/evidence/water_core_e/floaters_follow.mp4`, ffmpeg from outside the engine):
pond region mean frame change 0.03-0.08 grey levels before the push, 1.1-2.5 while riding (1.5-5 s),
decaying to 0.03-0.06 by 13-16 s - they ride and come back to rest. Engine frame 3.8 ms mean, p99 7.1 ms.
Still open from this run: some pieces sit 10-20 cm above the surface (stacked on a neighbour, or the
known floats-high bounding-sphere law); the fixed-point probe saw a splash run 1.2 m above the pond
(the probe reports the top run - not a drawn flicker, but unverified in the video).

### 19.7 Edge dips after a slosh: air pockets drawn as holes (2026-10-09) - owner signed off (19.8)

**Owner's look (19.6 recording):** "really close" - but the water does not settle flat afterwards; dips
near the edges look off.

**Measured (live Small pond, every 1/3 m column, `flat_after_push.json`):** rested pond flat to +-3 cm;
20 and 40 s after a water-only push, scattered columns 15-25 cm LOW with large patches +3.5 cm high, the
dips wandering, never closing. Cells up a dip column: `1.00 1.00 0.50 1.00 0.50` - a half-empty cell
UNDER water. The real top is level with the neighbours; the 19.5 height function (bottom + all the water)
drew the pocket's missing water as a 16 cm hole.

**Why the pockets live (CPU `DiagPockets`):** `compactSubmergedPartials` closes voids under water but
skips faces flowing up; 443 of 446 pockets 30 s after a blast sat under faces creeping up at the
resting-pool jitter. Tried: let BURIED pockets compact through rising faces - REVERTED: trapped air rose
from 0.36 to 0.79-0.88 m^3 (a pocket moved up one cell per pass and stalled under the surface), the
submerged pump delivered 3.5 of 4 m^3, the blasted pond stopped settling.

**Fix (drawing, CPU = GPU = probe):** a run's surface = bottom + water + each cell's missing water x
`surfacePocketWeight(fill of the cell above)`: 0 below 0.2 (a film or a thin sheet over a part-full
cell - that cell is the surface), 1 from 0.7 (a liquid cell sits on it - a pocket), linear between.
A hard "highest cell >= 0.5" rule jumped 23 cm when it switched; weighting by ALL the water above drew
wall sheets as +12 cm bumps; the ramp was picked from a sweep (0.1-0.5: 76 jumps over the bound, bumps
to 12 cm; 0.2-0.7: 47 / 7.7 cm; 0.3-0.9: 23 / 4.7 cm but most live pockets would show again).
`SurfaceDoesNotOutrunTheWater`: the pocket term moves at most (slope = 2) x the water's own bound, so the
bound is (1 + slope) x the kinematic one - 0 violations; a CONTROL in the test keeps it sharp: the old
fill-line rule still fails 524 times. CPU blasted pond at 30 s: surface spread 4.4 -> 1.5 cm, deepest
column -16 -> -2.7 cm. 299 water/debris/shore unit tests, 16 GPU tests (surface parity included).

**Live after the fix:** 40 s after the push, deepest column -23 -> **-7.9 cm**, most columns within +-2 cm.
**Still open - the real cause, one level down:** many cells sit at EXACTLY f = 0.500 and top cells
settle anywhere in 0.50-0.61. The projection treats every cell >= liquidThreshold (0.5) as FULL with
the free surface at its top face, so two columns whose water differs by 3.6 cm are identical to it -
nothing drives them level. That leaves +-2 cm terraces (on the rested pond too), the stuck pockets, and
the remaining -6..-8 cm dips (pockets of 0.50 under a 0.51 top cell, half forgiven). The standard
remedy is a sub-cell free-surface (ghost-fluid) pressure condition: p = 0 at the interface height
inside the top cell, not at its face. Next step; CPU + GPU, with the drain/weir/dam/pump regressions.

### 19.8 Owner sign-off on the small-pond feel (2026-10-09) and the queue after it

**Signed off by the owner, in motion, on the Small bench:** the drawn surface no longer flickers (19.5),
resting floaters wake when the water moves under them, ride it and come back to rest (19.6), and a
pushed pond no longer shows deep dips afterwards (19.7) - "really close" after 19.6, "looks really good"
after 19.7. What that sign-off covers: a 4 x 4 m pond, wood floaters, water-only pushes and blasts,
GPU backend. It does not cover splashes, ripples, the look of moving water, or large bodies.

**Known, open, not visible enough to block the sign-off:**
- +-2 cm terraces and cells stuck at f = 0.500: the projection treats any cell >= 0.5 as full, so it
  cannot see sub-cell level differences (19.7) - fix: a ghost-fluid free-surface pressure condition.
- Phantom surface current: a still GPU pond's top-cell velocity reads 0.08-0.10 m/s (19.6); debris
  reads it as current.
- A small CPU pond never fully calms (wall columns trade 2-4 cm per tick at 10 s, 19.6).
- CPU vs GPU kick response differs ~35 % (19.2); wood floats higher than its draft (bounding-sphere law).

**Queue (owner to choose the order):** (1) impacts that make ripples and splashes you can see - water
entry drag + displacement, and ripples below the 1/3 m cell size in the shading, driven by solver
events; (2) the look of moving water - the slosh barely changes the picture (< 1.2 grey levels a
frame, 19.5); (3) sub-cell surface pressure (the terraces and stuck pockets); (4) the phantom current;
(5) E3 bodies as moving solids and E4 wading - re-gate with /design-check first.

### 19.9 Item 1 - impacts that splash: design-check (2026-10-09)

**Baseline (red), one 1/3 m stone dropped 3 m into the rested pond** (`tools/water_motion_check.py
--scenario stone`): 1/3 m cells - surface at the drop point -0.2/+0.3 cm, +0.5 cm at 0.7 m, picture
unchanged after entry (0.04 grey levels a frame, same as still water); 1/9 m cells - an 11 cm crater,
+1.6 cm at 0.7 m, still no visible ring, frame 4.3 -> 6.6 ms (p99 13.8). Finer cells alone do not do it.
**Cause:** the debris water law has only a linear drag (0.9/s), so the stone reaches the floor ~0.2 s
after entry having handed the water almost none of its momentum. Real entry drag goes with speed^2.

**Design (a):** form drag k = Cd A f b / (2V) per metre, relative to the water's flow (Cd 1.05 blunt
cube; A projected area along the velocity; f wet fraction; b the material buoyancy = rho_w/rho_body;
k ~ 0.58/m for a 1/3 m stone, ~2.1/m for wood), applied as the exact quadratic decay v / (1 + k|v|dt)
(never reverses, any dt). Added to the linear term, not replacing it: at bob speeds (~0.1 m/s) it is
~0.06/s against ~2.3/s, so the signed-off floaters (19.6) should not change. Same law in
`solver_integrate.comp` and `VoxelDynamicsWorld` (one water law for both worlds, constants in
`solver_shared.h`). The momentum reaches the volume through the E2 exchange unchanged. No new API knob.

**Gate answers:** voxel look - the splash is the 1/3 m simulated surface, no separate ripple field (7).
Chunks - the only chunk-shaped input is the debris water-tile directory, a pure function of world column
(pinned by `DebrisWaterTilesTest`); the drag adds no spatial input; new unit test: the same body at a
chunk-seam column and at an interior column decelerates identically. Generation - none. API - none new;
`water_coupling` echoes the exchange totals; defaults change for FAST wet debris only (WaterBuoyancyTest,
the coupling tests and the DebrisLab settle bench must hold).

**Predictions (written before building), Small pond, 1/3 m:** the stone keeps 40-55 % of its entry speed
at 1.5 m depth; >= 50 % of its entry momentum handed to the water in the first 0.3 s; the surface at the
drop point dips >= 10 cm and rebounds >= 5 cm; a >= 3 cm wave at 0.7 m within 0.5 s; pond-region frame
change >= 0.5 grey levels for >= 1 s after entry (baseline 0.04). Controls: the same drop with the
exchange off (surface <= 0.5 cm); the signed-off floaters scenario unchanged; the DebrisLab settle bench
within noise. Red: the CPU law unit test (today the stone keeps ~100 % at 1.5 m).

**Verdict: (a) READY. (b) droplet crown NEEDS WORK** - pool sizing in the 20 k budget, birth from the
volume, ledger-exact landing, render; its own gate if (a)'s splash is not enough at 1/3 m.

**(a) WITHDRAWN the same day - its premise was measured false before building** (`measure_entry`,
`docs/evidence/water_core_e/entry_baseline.json`): the stone enters at 6.8 m/s and still has 5.0 m/s at
the floor 1.33 m down (keeps 73 %), and the exchange handed the water 0.39 m^3 m/s of the stone's ~0.65 -
**~60 % of its momentum in 0.23 s already reaches the pond**, and the 1/3 m surface still moved 0.3 cm.
Missing drag is not why there is no splash; the speed^2 law would change the picture little.
**The real cause:** to the solver the stone is a ghost - it pushes the water with a spread body force,
and incompressible water pushed down into a floor only redistributes pressure. A splash is
DISPLACEMENT: the body is solid, the water must get out of its way, a cavity opens behind it and a crown
rises round it. That is E3's moving-solid mechanism (19 table: cells under a body's box solid with the
body's velocity on their faces, so the projection pushes the water out), applied to debris. Item 1 is
therefore E3's re-gate: (a) a ledger-exact rule for the water in a cell that becomes solid, (b) moving
solid face velocities on the GPU solver, (c) the pressure over the body as its force (or the table law
kept for the body side in v1 without counting the reaction twice), and bodies smaller than a cell.

---

## 20. Moving solids - displacement (E3's core; item 1 "splashes") - DESIGN, 2026-10-09 - design-check READY for M0 (20.11)

**Status: design written, re-gated (20.11: READY for M0); nothing built.** Supersedes 19.9 (a) (withdrawn) and
is the E3 re-gate 19.1 asked for. One mechanism, three payoffs: debris splashes (item 1), crates and
furniture that float by displacing water (E3), characters that wade with a bow wave and a wake (E4).

### 20.1 The problem, measured

- One 1/3 m stone dropped 3 m into the rested 1/3 m pond: the surface moves 0.3 cm (19.9); the picture
  does not change after entry. With 1/9 m cells: an 11 cm crater, no visible ring, +50 % frame time.
- The water already receives ~60 % of the stone's momentum in 0.23 s (`entry_baseline.json`). The
  momentum arrives; the splash does not. **To the solver a body is a ghost:** it never takes up room,
  so it never pushes water out of its way. A real splash is displacement - the body is solid, the water
  must leave the space it moves into (the crown), and refill the space it leaves (the cavity closing,
  the Worthington jet).

### 20.2 The physics, in one line

**A body is a pump of exactly the water that no longer fits.** With f the cell's water and s' the
body's volume fraction in it this frame, the cell must shed q_i = max(0, f_i + s_i' - 1) x V_cell / T
(m^3/s) over the frame T. The projection enforces div(u) = q_i / V_cell in the cell - exactly the term a
pump outlet uses today (CPU `WaterSolver::project`, GPU `wc_classify` `src[id]`) - so the water flows out
of the body's way: the crown. Properties, each on purpose:
- q <= f always (s' <= 1), so the fill can never be driven below zero;
- a body entering a part-full SURFACE cell pushes out air first, water only once the cell is full -
  q = Delta S only where the cell was full (fully submerged), which is where it must be;
- **there is no sink.** The space a body leaves is a real void - the cavity behind a fast stone - and
  gravity and the pressure field refill it (the cavity collapse, the Worthington jet). A sink would
  refill it instantly through the projection and erase the cavity, which is the splash.
- Volume is conserved by construction: the pond's level rises by the submerged volume / area, plus,
  while a cavity is open, by the cavity's volume (a stone that sinks 1.33 m at 7 m/s sweeps
  1.33 x 0.111 = 0.15 m^3 of cavity - four times its own volume - which must collapse: T10).

### 20.3 Representation - and the alternatives rejected

| Option | What it is | Verdict |
|---|---|---|
| **Volume fraction + displacement source (chosen)** | per cell the body's volume fraction s (0..1, exact box overlap), its rate q; water capacity of the cell = 1 - s | Smooth in time (s is piecewise-linear in the body's position, so no popping); bodies smaller than a cell work (a 1/9 m pebble is s = 1/27); the pressure MATRIX is unchanged (only its right-hand side and which cells have rows), so both backends change in a handful of places |
| Binary moving-solid cells | a cell is solid when the body covers its centre | REJECTED: at 1/3 m a 1/3 m stone flips whole cells - 37 litres displaced in one tick, a pop per cell crossed; small debris never registers |
| Variational face fractions (Batty, Bertails & Bridson 2007) | open-area weights on every face in the pressure matrix | The accurate answer for no-slip/no-penetration and for two-way force; REJECTED for v1: every face kernel and the RBGS matrix change on CPU and GPU, and nothing measured needs tangential blocking yet. Revisit if E3's crate in a current shows water flowing through it |
| Splash particles only | spawn droplets at entry, no grid change | REJECTED as the mechanism: no crater, no level rise, no cavity; droplets stay the crown's detail (the 20 k pool, its own gate) |

**What the chosen model does not do (stated, measured later):** water can flow tangentially THROUGH
a body that is not moving (no no-penetration wall); a body is porous to a current. **Decision (re-gate
20.11): no face forcing in v1.** Setting faces to the body's velocity before the projection does nothing
- they are ordinary unknowns of the solve and it overwrites them; making them stick means prescribed
faces, i.e. a pressure-matrix change, which is exactly the face-fraction option. So v1 keeps the matrix
and accepts the porosity; T9 (a crate held in a current) measures it, and if water visibly passes
through, face fractions (prescribed faces) come back as their own step.

### 20.4 Data flow (per frame)

1. **Who is a moving solid:** any body whose box overlaps an awake volume's box - tested against the
   VOLUME BOXES themselves (up to 8, passed to the integrator in its push block; more are counted and
   the extra volumes get no debris displacement), never against the water tiles: the tile directory is
   a 32 x 32 chunk-column window centred on the viewer (`WATER_DIR_CHUNKS`), so a tile test would make
   displacement depend on chunk residency. Source 1 (v1): GPU debris - `solver_integrate.comp` appends
   a **wet-body record** {centre, half extents, rotation, linear and angular velocity, slot, the tick's
   simulation time} (the same per-slot host-visible buffer pattern as the E2 exchange; cap 2048,
   overflow counted; WHICH bodies are dropped at the cap follows GPU thread order - not deterministic,
   so it is counted and echoed, and the stress rows keep N under the cap). Source 2 (E3): CPU rigid
   bodies (`VoxelDynamicsWorld`). Source 3 (E4): the character capsule. All three become one
   `MovingSolid` list.
2. **Lag:** debris records are **two frames** old - `GpuParticlePhysics::recordComputeCommands`
   consumes "events this slot's ticks wrote two frames ago" (`MAX_FRAMES_IN_FLIGHT = 2`). The manager
   predicts each body forward by its velocity x (now - the record's simulation time) before
   rasterizing (at 60 fps a 7 m/s stone moves ~23 cm in two frames; at the bench's ~250 fps, ~6 cm).
3. **Rasterize (CPU, `WaterCoreManager`):** per volume, the overlap of each body's equal-volume
   axis-aligned box (centred on the body, its own half extents - rotation ignored in v1, volume exact)
   with every cell box: s_i = overlap / V_cell, summed over bodies, clipped to 1 and to non-static
   cells. Cells under a static solid get nothing. Output: a sparse list {cell, s, q}.
4. **Upload:** the sparse list to the volume (GPU: a small scatter kernel writes two dense per-cell
   buffers `solid` and `solidRate`, and zeroes last frame's cells; CPU: the same arrays on WaterGrid).
5. **Step:** the solver's substeps all see the same s and q (q is per second, so substeps sum to the
   frame's displacement).

### 20.5 Solver changes (CPU `WaterSolver` and GPU kernels, line for line the same rule)

| Where | Change | Why |
|---|---|---|
| classify (`WaterGrid::classify`, `wc_classify.isLiqCell`) | a cell is liquid when f >= the 1 mm film (kSurfaceMinDepth / h) AND f + s >= thr | a cell full of body and water needs a pressure row or its source has nowhere to act; a body in AIR (s = 1, f = 0) must NOT get one - it would grow a velocity field round a stone flying over the pond |
| pressure rhs (`project`, `wc_classify`) | rhs += q / V / dt beside the pump term, q from 20.2 (new `solidRate` buffer, not `src` - pumps own `src`) | the displacement itself |
| transport (Eulerian fill advect: `EulerianTransport::advect`, `wc_fill_advect`) | a receiver's room is max(0, 1 - s - f), not 1 - f (both backends: the move is min(desired, donor has, receiver can take)) | otherwise transport carries water INTO the body's space |
| **capacity + overflow - ledger rule (a)** (new `resolveSolidOverflow`, `wc_column_ops` mode 3) | after transport, any f > 1 - s moves UP its own column into cells with room (one thread per column: race-free, the same shape as compaction); what the column cannot take is OWED and added to that cell's q on the next substep, so the projection carries it out sideways; owed is counted and never deleted | the "water in a cell that becomes solid" rule 19.1 required; lateral moves in a column pass would race (two columns writing one neighbour - the reason the pump kernel runs one source at a time); covers a body spawned or teleported into water (no rate, so no flow - the ledger moves the water) |
| compaction (`compactSubmergedPartials`, `wc_column_ops` mode 1) | fills up to 1 - s, not 1 | otherwise it pours water into the body's space every tick (a resting body would drain the column above it) |
| residue sweep, sleep | sleep only when no body in the volume moved more than 1 mm this frame | a resting body must let the pond sleep; a moving one must not be frozen |
| drawn surface (`extractSurfaceField`, `wc_surface`, `surfaceWorldY`) | cells count f + s where the cell holds water (f >= the 1 mm film), f alone where it does not | the water level round a floating body is (water + submerged volume) / area; a body in air is not water. Open sub-question, measured by T6 |

**Not in v1:** FLIP-transport volumes (`ownsMass`) refuse moving solids (counted, logged) - their
particles need their own displacement rule; Eulerian is the default for every pond.

### 20.6 Force on the body (two-way) - phased

- **v1 (one-way):** bodies keep the table law (buoyancy, linear drag, current) for their own motion;
  the water is pushed by displacement. The E2 exchange (drag reaction) stays on, and the momentum
  ledger is MEASURED (20.8 T8): water momentum gained vs body momentum lost per drop. Acceptance band
  0.5-1.5x; outside it, the exchange is switched off for bodies the volume rasterizes (displacement then
  is the only reaction).
- **v2 (two-way, its own gate):** the force on a body = the pressure integral over its volume,
  F = -rho_w sum_i s_i V grad p_i (Gauss: over a submerged body this is the surface pressure integral)
  - buoyancy AND form drag from one source, momentum conserved by construction, the table law retired
  inside awake volumes (and the "wood floats high" bounding-sphere defect with it). Needs a per-body
  force buffer the debris integrator reads, one frame late - the stability of a floater under a
  one-frame-late buoyancy is the gate's red test.

### 20.7 API

- `POST /api/debug/water_coupling {solids: bool}` - moving solids on/off (omitted = unchanged; default
  ON once accepted, OFF is the A/B control). Echo `moving_solids: {bodies, records_dropped, cells,
  body_volume_m3, displaced_m3_frame, displaced_m3_total, overflow_m3, owed_m3, flip_refused,
  oversize_clipped}` - enough to assert a body was seen, rasterized and displaced.
- `water_av_probe` adds `solid_fraction` and `solid_rate_m3s` at the probed cell.
- **Clamps, with the failure written at the site:** q per cell <= (open faces of the cell) x h^2 x
  20 m/s - the same 20 m/s the blast and momentum kicks use, for the same reason: a source in a cell
  with few open faces (a body wedged in a corner, a cell walled by other body cells) would otherwise
  demand an outflow speed that blows the CFL substep count and the projection; the excess is owed and
  carried, never dropped. (Not "per substep": the substep count is chosen FROM the velocities this rate
  creates - that bound was circular.) A body whose s jumps
  by more than 0.5 in a cell in one frame without having been present (spawn, teleport) gets NO rate
  (no fake splash) - the ledger moves the water; NaN/zero-size bodies refused and counted; records
  over the cap dropped and counted.
- Defaults: no change to any existing default until M2 acceptance; then `solids` defaults ON and the
  pinned tests in 20.8 move in the same commit.

### 20.8 Test plan (red first; predictions written before building)

Unit (L2, CPU solver, `WaterSolidTest`):
- **T1 raster is exact and continuous:** a box at random sub-cell offsets: sum s V = the body volume
  inside the grid to 1e-5 m^3; moving it by 1 mm changes every s by <= 1 mm x face area / V.
- **T2 displacement raises the level:** a 1 x 1 x 1 m box lowered at 0.2 m/s into a 4 x 4 m pond 1.5 m
  deep, fully submerged: level rises 1/16 m = 6.25 cm +- 5 mm; mass exact (double). Red today: 0.
- **T3 ledger exact under overflow:** a body spawned inside full water: no rate, overflow moves the
  water up, total mass exact, owed -> 0 within 30 ticks, never negative fill.
- **T4 fast entry makes a crater and a crown:** a 1/3 m cube (density 2.6) at 7 m/s into the CPU pond
  (rest level mid-cell, f = 0.5 in the top cell). **Derived predictions:** the stone fills the entry
  cell (s' = 1) and sheds its 0.5 h = 16.7 cm of water, which neighbours refill over the gravity-wave
  crossing time of one cell (h / sqrt(g d) = 0.33 / 3.8 = 0.09 s) -> the entry column dips >= 8 cm
  (half the emptied depth, sampled at 50 Hz); the stone's own volume, 0.037 m^3, lands first on the 8
  neighbouring columns (0.89 m^2) -> >= 3 cm at 0.33 m; spread over the 0.5-0.9 m annulus (1.76 m^2)
  -> >= 1.5 cm at 0.7 m within 0.5 s. Upper bound from the swept cavity (0.15 m^3 over the same
  annulus): ~8 cm. **Control:** the same with solids off -> < 1 cm everywhere (the A/B).
- **T5 a resting body is quiet:** a body on the floor of a still pond for 60 s: the volume sleeps,
  mass exact, the column above it does not drain into it (compaction capacity), drawn surface flat to
  1 cm.
- **T6 drawn waterline at a floating body:** a wood block held at its draft: the drawn surface beside it
  within 3 cm of the solver's level.
- **T7 world-aligned, chunk-independent:** the same body rasterized into two volumes whose boxes differ
  (one straddling a chunk seam at x = 96) gives identical s and q for every shared world cell
  (`FloraMarginTest` shape: the union of pieces equals the whole).
- **T8 momentum ledger (diagnostic, then band):** water momentum gained vs body momentum lost, 0.5-1.5x.
- **T10 the cavity closes:** after the T4 stone lands, the air under water (the `DiagPockets`
  measure, sum of (1 - f - s) V over cells with water above) returns to its pre-drop value + 10 %
  within 5 s. Prediction risk: 19.7 found 443 of 446 pockets survive 30 s because compaction skips
  faces creeping up - if the swept cavity stays trapped, T10 is RED and blocks M2; fixing it is the
  same physics as 19.8 item 3 (the terraces), and gets its own measurement first.
- **T9 (E3 acceptance, later):** a crate held still in a 0.5 m/s current: the water behind it slows by
  >= 50 % (if not, the body is too porous -> face fractions).
GPU parity (integration, `WaterCoreGpuParityTest`): s, q, overflow ledger and T2/T4 outcomes match the
CPU within the existing parity tolerances.

Live (L4, Small bench, shipped 1/3 m pond, pose 109.5 / 20.5 / 13.5, recorded with
`tools/water_motion_check.py --scenario stone` - no screenshot stalls):
- **Stone drop - physics, by surface probes:** the T4 predictions on the live GPU pond (entry dip
  >= 8 cm, >= 3 cm at 0.33 m, >= 1.5 cm at 0.7 m within 0.5 s; baseline 0.3 / - / 0.5 cm) and T10.
  **Control:** `solids:false` -> the baseline numbers.
- **Stone drop - the picture, separately:** the recording and the pond-region frame change are
  REPORTED, not gated: 19.5 measured that a 25 cm slosh changes the picture by under 1.2 grey levels a
  frame - the look of moving water (19.8 item 2) can hide a correct splash. The owner's look decides
  whether item 2 must come before the splash reads.
- **Big block:** a 1 m wood block dropped in: level rises by its submerged volume / pond area +- 1 cm;
  it floats; the crown is visible in the recording.
- **Regression:** the signed-off floaters scenario (19.6) - wake, ride, rest by ~5 s; frame time.
- **Gate:** `tools/debris_settle_bench.py` on DebrisLab (the wet-body record touches
  `solver_integrate.comp`; DebrisLab has no water, so the change must be inert there).

**Stress phase (before "done"):** count - 1, 10, 100, 1000 bodies in one pond (records cap honoured,
dropped counted, frame time <= +0.5 ms at 100); size - a 1/9 m pebble (s = 1/27, small but nonzero
splash), a 1 m block, a body larger than the volume (clipped, counted); boundary - a body crossing the
volume's edge and a chunk seam; churn - spawn/despawn 1000 cycles: no displacement left behind (the
failure: a despawned body leaves a hole); rest - 10 min of a resting body: mass exact, no drift;
degenerate - zero size, NaN pose, 100 m/s.

**The rig vs the game:** the Small bench pond is shipped content at the shipped cell size; the drop
height (3 m, ~7.7 m/s entry) is the rig's choice. The stress rows run in the same pond.

### 20.9 Feature Design Keys gate

- **Voxel look:** the splash is the simulated water at the volume's cells, drawn by the existing mesh;
  bodies are the existing debris. No smooth idiom, no visual-only field (7). Unconditional once on.
- **Chunks:** the inputs are body poses (world space) and volume grids (world-aligned lattices: origin
  in cells, world = origin x h). No chunk quantity enters; T7 pins it. The debris record buffer is
  per frame slot, not per chunk.
- **Generation:** none - runtime coupling. No world-recipe state.
- **API:** 20.7 - units stated, omitted = unchanged, full echo, clamps with reasons, defaults pinned.
- **Visual test:** 20.8 - measurable predictions, L2 + L4, red first, controls, small rig, stress.

### 20.10 Build order (each step ends red-before-green; the owner looks after M2)

| Step | Scope | Done when |
|---|---|---|
| **M0** CPU core | rasterizer, s/q on WaterGrid, the 20.2 law, classify (film gate) + rhs, transport room 1 - s - f, column overflow + owed-to-q, compaction cap, sleep rule, drawn surface | T1-T7 and T10 green (red shown first) |
| **M1** GPU | `solid`/`solidRate` buffers + scatter kernel, wc_classify / wc_fill_advect / wc_column_ops / wc_surface | parity tests green; `shader_manifest --check` |
| **M2** wiring | volume boxes into the integrator's push block, wet-body records in `solver_integrate.comp` + readback (two frames, sim-time stamped), Application -> manager, API toggle + echo | live stone drop meets 20.8 with its control; floaters unchanged; settle bench in noise; recorded in motion -> **owner's look** |
| **M3** stress | 20.8 stress rows | every row's invariant at every step |
| **M4** two-way force | pressure integral replaces the table law in volumes | its own gate (one-frame-late buoyancy stability) |
| then E3 / E4 | CPU rigid bodies, then the character capsule, as `MovingSolid` sources | crate floats by displacement, T9; a wading wake |

**Risks, named:** large q can raise velocities and with them the CFL substep count (cost) - the clamp
bounds it, the stress rows measure it; the one-frame debris lag (prediction mitigates; visible as the
crater trailing a very fast body); the drawn waterline at bodies (T6); interplay with compaction and
the residue sweep (T5); FLIP volumes excluded in v1.

**Correction to 19.7:** the projection DOES carry a sub-cell free surface vertically (ghost fluid,
`thetaToAir`: theta = f - 0.5 + f_above, clamped 0.1-1.5; lateral faces use 0.5). So 19.7's "the
projection treats every cell >= 0.5 as full with the surface at its top face" was wrong for the
vertical direction; the cause of the +-2 cm terraces and the cells stuck at 0.500 is NOT established
(suspects: the theta clamp at f ~ 0.5, the rest damping / settle band). Item 3 of the 19.8 queue
needs a measurement before a fix.

### 20.11 Design-check, two rounds (2026-10-09)

**Round 1 - NEEDS WORK** (the gate run against 20.1-20.10 as first written, against the code):
(1) q = Delta S overstated displacement in part-full cells and could drive f below zero; (2) the swept
cavity could stay trapped as pockets (19.7: 443/446 survive); (3) "liquid when f + s >= thr" gave a body
in air a pressure row; (4) "transport unchanged" was wrong - `wc_fill_advect` takes room as 1 - f;
(5) pre-projection face forcing is overwritten by the solve; (6) lateral overflow in a column pass
races on the GPU; (7) the debris readback is two frames old, not one; plus selection by water tiles
(chunk-residency coupling), a circular per-substep clamp, and predictions that were targets, with the
picture metric entangled with the look problem.

**Folded in (this revision):** (1) q = max(0, f + s' - 1) V / T, no sink (20.2); (2) T10, blocks M2 if
red (20.8); (3) film gate on classification (20.5); (4) room = 1 - s - f on both backends (20.5);
(5) decision: no forcing in v1, porosity accepted and measured by T9, prescribed faces are the later
step (20.3); (6) column-only overflow, the remainder owed into the next substep's q (20.5); (7) two-frame
age, records stamped with simulation time, prediction by (now - stamp) (20.4); selection by up to 8
volume boxes in the push block (20.4); clamp per open face at 20 m/s (20.7); T4 predictions derived
from the cell geometry, the stone's and the cavity's volume (20.8); the picture reported, not gated
(20.8).

**Round 2 - re-run of the five questions on the revision:**
- **Voxel look:** unchanged - the simulated water at the volume's cells; no visual-only field.
- **Chunks:** every input is a body pose or a world-aligned volume lattice; debris selection now tests
  volume boxes, not the viewer-centred tile window; T7 pins raster equality across a chunk seam. PASS.
- **Generation:** none. PASS.
- **API:** `water_coupling {solids}` (omitted = unchanged) with the full echo; `water_av_probe` solid
  fields; clamps stated per open face with the failure at the site; default flips only at M2 with the
  pins. PASS.
- **Visual test:** derived predictions (T4), red-first unit tests T1-T8 + T10, live physics by probes
  with the `solids:false` control, the picture reported for the owner, a one-chunk rig (x 99-104,
  z 11-16, y 14-19 inside chunk 96-127 / 0-31 / 0-31), the stress rows. PASS.

**Verdict: READY for M0** (CPU core), with T10 as the known risk that can stop M2. M1/M2 inherit the
same rules; M4 (two-way force) and prescribed faces keep their own gates.

### 20.12 M0 built - the CPU core (2026-10-09) - ledger

**Built:** `MovingSolids.{h,cpp}` (the raster: exact box overlap per WORLD cell), `WaterGrid` s / q
(+ `levelFill`), `WaterSolver::setMovingSolids` (the 20.2 rate, clamp per open face, fresh / walled-in
cells to the column ledger), and the solver edits of 20.5 - classify (film gate), the projection's
source, ghost fluid seeing f + s, transport room 1 - s - f, compaction capacity, `resolveFreshOverflow`,
the drawn surface (`levelFill`). With no body every rule is bit-identical to before (the blast pond
reproduces 1.5687 -> 0.1284 exactly; 310 water/debris/shore tests, 16 GPU tests).

**Red first** (API in place, solver ignoring s and q): T2 level 1.5000 (predicted 1.5625), T3 the same,
T4 no crater (0 / 0 / 0), T5 water overlapping the stone (f + s = 2). T6 and T10 passed vacuously
(nothing moved) - their real verdict came after.

**What the build found (each a defect the design did not foresee, fixed and pinned):**
1. **Raster in grid-local coordinates rounded differently per volume** - T7 (the chunk-seam equality)
   red on its first run; the raster now works in world cell indices (bit-equal across volumes).
2. **A body-full cell with no water was treated as AIR:** a free surface at the bottom of the pond;
   the water accelerated into it forever (a resting stone pumped KE 2.29 -> 2.63 in 10 s with no
   rate). Such a cell is now a WALL (`bodyWall`: Neumann in the projection, faces zeroed).
3. **A liquid cell walled in on all six sides made a zero-diagonal row** - the Jacobi preconditioner
   divided by zero, the velocities went NaN and the next back-trace crashed (the 1 m cube going
   under, tick 184). Such rows are decoupled (p = 0); their water, and water left in a cell the body
   fills entirely (it circulated among the body's own cells), leaves by the column ledger.
4. **T10 red, as 20.11 predicted:** a stone's swept cavity left 0.047 m^3 of air trapped as part-full
   cells, every one held by compaction's rising-face gate (0.000-0.05 m/s). Three attempts, measured:
   closing EVERY buried void in the projection broke the submerged pump (3.6 of 4 m^3) and pumped a
   blasted pond (KE 1.5 -> 3.8); closing only voids under a FULL cell kept the pump but the blasted
   pond rang forever (KE plateau 0.4-0.8 from 10 to 30 s, was 0.13). **Kept: air a body drags rises
   back out** - only in the body's WAKE (3 cells round the cells it left, for the time trapped air
   needs to rise through the volume, ny h / 0.25 m/s), a void under a full cell takes an inflow at the
   rise speed of small trapped bubbles (0.25 m/s; Clift, Grace & Weber), in quiet water. A pond without
   bodies is exactly as before. Debug stage bit 64 turns it off (the A/B).
5. **T10's measure was refined:** "a not-full cell under a cell with f >= 0.5" also counted the free
   surface straddling two cells (0.92 under a 0.54 top layer) as air; it now counts air under a FULL
   cell, the surface split is reported beside it, and a CONTROL (stage bit 64) shows the measure sees
   the air: without the rule 0.0153 m^3 stays trapped, with it 0.0000.
6. **T5 was blocked by an older defect, not by bodies:** the control (`DiagPondSleeps`) shows this CPU
   pond sleeps at once left alone but NEVER within 60 s after even a 0.05 m/s push with no body - the
   19.6 wall chatter. T5 now places the stone before the solver starts (its water already on the top
   layer), so it tests what it claims: a resting body leaves the pond as quiet as it was.

**Results (all green):**

| Test | Prediction | Measured |
|---|---|---|
| T1 raster | volume to 1e-5; 1 mm move <= one face's sweep | exact; continuous; NaN / flat bodies refused |
| T7 chunk seam | identical s in both volumes | bit-identical |
| T2 lowered 1 m cube | level 1.5625 +- 5 mm, mass exact, no overlap | 1.5648 m (drawn 1.5647), mass to 1.1e-6 m^3 (float32, the pump test's 1e-5 tolerance), no overlap |
| T3 spawned cube | no rate, ledger exact, level 1.5625 | no rate; f never < 0; overlap gone in 30 ticks; water depth 1.5589 m (drawn 1.5733: 0.23 m^3 of air trapped beside it - the general pocket problem, item 3) |
| T4 stone at 6 m/s | entry <= -8 cm, >= 3 cm at 0.33 m, >= 1.5 cm at 0.67 m in 0.5 s; control < 1 cm | -36.3 cm, +8.4 cm, +3.3 cm; control 0.0000 m |
| T5 resting stone | sleeps, flat 1 cm, column above not drained, mass exact | asleep, flat 0.0 cm, f above 1.000, exact |
| T6 floating block | drawn waterline beside it within 3 cm | 1.5 cm (level +3.25 cm; predicted +3.1) |
| T10 cavity | trapped air <= 10 % of the stone at 6 s; control (rule off) above it | 0.0000 m^3; control 0.0153 m^3 |

**Open, logged:** (a) T8 (momentum ledger) needs live debris - M2. (b) `WaterCouplingTest.
BlastBesideThePondMovesItAndItSettles` is ORDER-DEPENDENT (E1's test, not M0): `DamageSystem::
blastSpeed` reads Stone's toughness from the global material registry - alone (fallback) it passes
with the original numbers, after a test that loaded materials.json the blast is ~40 % weaker and the
near-side drop misses 0.3 m. Fix: the test must load the shipped materials and its bar be re-derived.
(c) The general pocket / two-half-layer problem (19.7, item 3) is untouched outside a body's wake:
a spawned cube leaves 0.23 m^3 of air beside it. (d) The 19.6 CPU wall chatter (DiagPondSleeps).
**Next: M1 (GPU)** - the same rules in wc_classify / wc_fill_advect / wc_column_ops / wc_surface plus
the body wall, the decoupled rows and the wake rule; parity tests.

### 20.13 M1 built - moving solids on the GPU (2026-10-10) - ledger

**Built:** one per-cell buffer `solid` (vec4: s, q m^3/s, wake seconds, ledger mark), all zero = no body.
`WaterSolver::setMovingSolids` split into `updateSolidFields` (does not read the water: raster, wake,
fresh bodies) and `computeSolidRates` (reads it: q, the walled-in / body-filled ledger marks); a GPU
volume runs the first on the host, `WaterCoreGpu::setSolids` uploads it, and the device computes the
second from its OWN fill (`wc_classify` mode 1) at the step's first tick - the host grid's f is stale
for a GPU volume. Kernels, line for line with the CPU: `wc_classify` (liquid with the film gate, body
walls Neumann, ghost fluid f + s, the source, the ceiling rule with body ceilings, the wake rule,
decoupled rows), `wc_fill_advect` (room 1 - s - f), `wc_face_ops` (body walls zeroed and never updated),
`wc_column_ops` (compaction capacity; mode 3 = the column ledger), `wc_surface` (levelFill).
**Also fixed on the CPU while porting:** the velocity update treated a body-wall neighbour as AIR (ghost
p = 0) while its matrix row treated it as a wall - now zeroed like a solid face (bit-identical without
bodies; 307/307 then).

**Results (GPU, the 1/3 m pond of WaterSolidTest):**

| Test | Prediction (20.8) | GPU | CPU (M0) |
|---|---|---|---|
| rates match | q and ledger marks equal cell for cell | 36 rate cells, max dq 1.2e-7 m^3/s, 0 ledger mismatches, s bit-equal | - |
| stone at 6 m/s | entry <= -8 cm, >= 3 cm at 0.33 m, >= 1.5 cm at 0.67 m in 0.5 s; control < 1 cm | -39.1 / +5.1 / +2.3 cm; control 0.32 cm | -36.3 / +8.4 / +3.3 cm |
| cavity | trapped air <= 10 % of the stone at 6 s | 0.0011 m^3 (3 %) | 0.0000 |
| lowered 1 m cube | level 1.5625 +- 5 mm, no overlap, finite | 1.5641 m, f + s <= 1.0000, finite | 1.5648 m |

Mass on the device: 24.0000027 -> 24.0000024 (stone), -> 24.0000020 (cube) - float32 sums, inside the
1e-4 the GPU tests use. **No red run on the GPU:** the tests passed on their first run; what shows they
measure the bodies is the solids-off control (0.32 cm) and M0's CPU red for the same predictions.
Existing GPU suite (16 water tests incl. every parity test) green on the new kernels - ponds without
bodies unchanged. Unrelated integration failure seen in the same run: `SceneIntegrationTest.
AddSceneThenTransitionToIt` (scene system; not re-checked on a clean build).

**Logged, pre-existing (not changed here):** `wc_face_ops`' velocity update computes the free-surface
distance with a DIFFERENT formula than `wc_classify`'s matrix row (0.5 + f_above, or clamp(f - 0.5, 0.1,
0.5), vs clamp(f - 0.5 + f_above, 0.1, 1.5)); the CPU uses one function for both - a projection
inconsistency on the GPU and a candidate for the ~35 % CPU/GPU kick-response gap (19.2).
**Next: M2** - wet-body records from the debris solver (volume boxes in the integrator's push block,
two-frame readback stamped with simulation time), `WaterCoreManager` -> per-volume fields and upload,
Application wiring, `water_coupling {solids}` + echo, the live stone drop and the owner's look.

### 20.14 M2 built - live debris displaces water (2026-10-10) - ledger, NOT signed off

**Built:** the debris integrator (`solver_integrate.comp`) appends a wet-body record (centre, half extents,
velocity, slot) for every active body overlapping one of up to 8 water-volume boxes, every tick, into a
per-frame-slot host buffer that also carries the boxes (8 boxes are 256 B - more than a push block may
hold, so not the push block as 20.4 said); read two frames late, one record per body (its last tick),
stamped with its real age, `fresh` = not in the previous list; a slot with no physics tick keeps the
previous list. `WaterCoreManager::setMovingSolids` feeds each volume its bodies (CPU: the whole solver
path; GPU: the host half + `setSolids`), keeps feeding until a body's fields and wake clear, refuses FLIP
volumes. Application predicts each body forward by velocity x age. API: `water_coupling {solids,
wake_rule}` + `moving_solids` echo; `water_av_probe` `solid_fraction`; `water_av_watch` (the drawn
surface at up to 8 points EVERY FRAME - pond level and highest run separately; a 25 Hz HTTP poll missed
the peaks). Tools: `tools/water_solids_probe.py`, `water_motion_check.py --solids on|off`.

**Found live, fixed (each measured):**
1. **The bodies drove the water and the water drove the bodies.** With every wet piece displacing, ten
   floaters never settled (9-10 awake for 20 s; displacement off: 1-3 by 7-10 s). One floater alone bobbed
   +-2.5 cm at 0.2 m/s forever. Kept fixes: a body is rasterized at a HELD centre until it moves 3 cm
   (`kSolidHoldDistance`); the per-voxel water reading the debris uses and the 19.6 motion signal both
   skip sub-columns whose top holds a body (`SurfaceColumn::bodyAtSurface` - the level there is the
   body's: it flips 16.33 <-> 16.58 m as a floater shifts) - one floater then rests (16.440-16.445 m,
   asleep). Ten still did not: a hold of 11 cm and the wake rule off each changed nothing; a CPU pond with a
   still surface-piercing body sleeps at once (`DiagFloatingBodyCalm`) - it is the DELAYED one-way loop.
   **v1 decision: debris displaces water while it MOVES** (on above 0.6 m/s, off below 0.3 m/s): entries,
   sinking stones, thrown pieces splash; a resting or bobbing floater is carried by the table law, exactly
   the signed-off 19.6 feel. Cost: a resting body does not raise the level (~2 mm per 1/3 m stone in this
   pond). The cure is the two-way pressure force (20.6 v2 / M4).

**Results, live (Small pond, GPU, stone dropped 3 m, `water_solids_probe` - every frame):**

| | entry column (pond level) | 0.33 m | 0.67 m | splash thrown above |
|---|---|---|---|---|
| prediction (20.8) | <= -8 cm | >= +3 cm | >= +1.5 cm (0.5 s) | - |
| displacement ON (3 drops) | -29.9 / -30.3 / -25.5 cm | +4.4 / +1.2 / +7.1 cm | +4.7 / +2.7 / +0.6 cm | +0.84 m, 3 of 3 |
| OFF (control) | -0.2 / -0.3 / -1.3 cm | <= +2.8 cm | <= +1.0 cm | none |

The crater: 3 of 3. The ring: 2 of 3 at each distance. **Floaters** (19.6 scenario): 0-3 awake by 7-12 s
after the push (control 1-3 by 7-10 s); recorded: pond-region change 0.07 -> 2.7 -> 0.03 grey levels by
14 s - they ride and rest. **Debris settle bench** (DebrisLab, `m2_solids`): verdicts identical to E2
(drop_layer, packed, crater, crater_subcube, box_single_straddle SETTLE; drop_pile, blast,
box_through_pile FAIL as before; blast forced sleeps 4, in its 3-6 spread). 311 water/debris/shore unit
tests, 19 GPU water tests.

**Seen in motion, honestly:** after entry the pond region changes 0.09-0.15 grey levels a frame with
displacement against 0.03-0.05 without (`m2_stone_on/off.mp4`) - 2-3x, but a 25-30 cm crater and an 0.8 m
splash barely register in the picture. **The look of moving water (19.8 item 2) is now what stands
between this and a splash you can see.** Also visible / open: the drawn surface at a floating body's own
column flips (16.33 <-> 16.58 m); the stone looked absent on the floor after landing in one recording
(not explained); +0.5 ms frame (4.08 vs 3.60 ms with two bodies) - `setSolids` submits and waits on its
own each frame, fold it into the step's submission; momentum ledger (T8) not measured yet.

## 21. A splash you can see - droplets, whitewater, clear water (item 2) - DESIGN, 2026-10-10 - design-check READY for S0 after one fold-in (21.9)

M2 (20.14) put the physics of a splash into the pond: a 25-30 cm crater and water thrown 0.8 m. The owner
watched it: "i can barely see the splash", and "some water is way above the stone ... it looks weird". This
section is the design for making it read. Nothing here is built. Three parts, each with its own measured
cause; the build order is at 21.6.

### 21.1 The three causes, measured (2026-10-10, Small bench, GPU, 1/3 m cells)

**(A) The floating slab is the jet, drawn as a pancake.** The entry column, every frame
(`solids_probe_m2.json`, run 1): at entry the column rises +0.34 m (the column ledger pushes the displaced
water up), the crater opens to -27 cm, and 0.25 s later - as the cavity closes - DETACHED runs appear 0.5 to
1.17 m above the pond (17.01, 17.34, 17.67 m), each for 2-3 frames, jumping between cells. That is the
Worthington jet, real physics; but at 1/3 m a jet is a scrap of fill in one cell, and the surface mesh draws
a scrap as a flat 1/3 x 1/3 m slab with a top and a bottom, hanging in the air, then gone. The recorded
frame `m2_stone_on_frames/f_13.png` shows it as a grey rectangle above the far bank. The design already
named the cure (4.7: "thin sheets and droplets smear"; §7 droplets; §12 decision 5 "separate pool,
20 k, ledger-exact re-absorption"; 14.1 "water-shaded microcubes") - and 19.9(b) gated it NEEDS WORK. It was
never built: `droplets` in `water_ledger` is a hard-coded 0.

**(B) The pond is milky: its in-scatter is a constant that ignores the light.** Debug taps, rested pond with a
stone on its floor, pose (109.5, 20.5, 13.5) yaw 180 pitch -38, tone map off and exposure 1 for the taps
(`tools/water_clarity_probe.py`): turbidity 0 (the clear endpoint); the floor seen with the water off reads 78 (sRGB
grey, G) = 0.0095 linear before exposure; the water body tap reads 0.039 linear (G); the path length tap
saturates (>= 1.7 m). Beer-Lambert in `shadeWaterSurface`: body = behind x T + WATER_SCATTER x (1 - T), T_G =
exp(-0.09 x 2) = 0.835 -> 0.0095 x 0.835 + 0.18 x 0.165 = **0.038 predicted, 0.039 measured**. The floor is
21 % of what the pond shows; 79 % is `WATER_SCATTER` = (0.04, 0.18, 0.24), an UNLIT radiance constant (the
code flags it "unsourced ... artistic", from before the 2026-08 x8-exposure lighting revamp - the same class of
defect as the hardcoded sky gradient F2 replaced, 17.3). So the water glows ~3x brighter than its own floor:
the stone "vanished" on the floor in 20.14 (it was there, hidden), and the floor's refraction wobble - the
main cue that water is moving - is suppressed 5x.

**(C) Nothing on the pond ever turns white.** `SurfaceColumn::foam` is filled only by the shore band (G2,
`ShoreSolver`); `wc_surface.comp` writes 0, the CPU extractor likewise. The foam shading path exists and is
fed 0. Also seen in the code: at the steep pond view Fresnel is 0.02-0.07, so the surface's SHAPE changes the
colour only through the refraction offset (0.035 x N.xz) - which (B) then washes out.

### 21.2 Part A - the droplet crown (the §12 pool, to build)

**Birth - water the grid cannot hold.** A *parcel* is a vertical run of liquid cells in one column that is
**isolated**: the cell below its bottom, the cell above its top and the four lateral neighbours of each of its
cells are air (f + s < film, not solid, not a body cell), and its total fill Σf < 1 (less than one full cell -
a scrap). A parcel becomes droplets in the tick it is found: its cells' f go to 0, its volume Σf x h^3 goes to
the pool with the parcel's velocity (the mean of its cells' face velocities). Why each condition:
- *isolated laterally and below* - the 19.7 pockets (water over a buried air void) and the stuck f = 0.5
  cells are connected sideways to the pond: never born. A film on a wall or a floor has a solid neighbour:
  never born (films are §4's job). A crown rim rising out of the surface has water below it: stays mesh.
- *Σf < 1* - a parcel holding a full cell or more is resolved by the grid and stays grid water (a falling
  1/3 m lump draws as one; refined later if the owner sees lumps).
- *no speed condition* - an isolated scrap is unresolved whatever its speed; still ponds have none (control).
Cost: one pass over columns in the existing column kernel (`wc_column_ops`, a new mode), CPU mirror in
`WaterSolver`. Born after transport, before the surface field is built - so a scrap is never drawn.

**The pool.** Owned by `WaterCoreManager`, CPU, 20 000 droplets total (§12 decision 5; separate from the
debris and VFX pools). A droplet: position, velocity, volume, volume id. Split: a parcel of volume Vp spawns
n = ceil(Vp / v_d) droplets of equal volume, v_d = (k h)^3, k = the droplet size knob, default 1/3 (a
microcube at h = 1/3, 14.1's idiom; clamped to [1/9, 1] where it enters - below 1/9 one full cell would be 729+
droplets and a jet would exhaust the 20 k pool; above 1 a droplet is bigger than the cell it left; the edge
is drawn at the droplet's own volume^(1/3), so drawn volume = carried mass), placed at deterministic hashed
points inside the parcel's cells (hash of world cell + tick: no RNG state, chunk-blind) with the parcel's
velocity +- 15 % deterministic spread (a breaking jet fans; the mass and the mean momentum are kept exactly:
the spread is zero-sum per parcel). **If the pool is full the parcel is NOT born** - it stays grid water,
counted in `refused`. Never a dropped gram.

**Flight.** Ballistic with gravity and quadratic air drag (Cd 0.47 sphere-equivalent, rho_air 1.2:
terminal ~ 4-5 m/s for a 3.7 cm drop - grounded at build by the grounding auditor), integrated on the CPU each
frame. **Free flight - no box walls** (21.9 F1: a reflecting box wall is an invisible wall in mid-air, and
the box can be clipped by residency, 14.2). A droplet that lands OUTSIDE its home volume's box is deposited at
the nearest in-box column's surface and counted in `relocated_m3` (asserted 0 on the bench rigs: a 0.8 m jet
with +-15 % spread travels <= 0.4 m sideways, the box margin is >= 1 m). Water leaving a volume for real is
E4/G work (logged, 21.8).

**Landing - ledger-exact.** The host has no f field for a GPU volume (21.9 F3) - it has the SURFACE FIELD,
read back every frame for the mesh (`WaterCoreGpu` `offSurf`). So both backends land a droplet the same way:
against its sub-column's drawn top (the top run's top; `bodyAtSurface` columns use the same top - water lands
on a floater and runs off it) or, in a dry column, `solidTopY`. One frame stale on the GPU: at <= 5 m/s that is
<= 8 cm at 60 Hz, inside one cell. The deposit is {world x, y, z, volume}: the backend puts it in the cell
containing (x, y, z) - lifted to the first non-solid cell at or above it - f += v / h^3, overflow above 1
carried up the column (the 20.5 ledger rule). CPU: direct. GPU: a deposit list (cap 8192 per frame; beyond
it the deposits wait for the next frame, never dropped) applied by a `wc_column_ops` mode at the start of the
step, uploaded in the step's own submission - `setSolids`' separate submit-and-wait is folded in at the same
time (the 20.14 +0.5 ms item).
**Sleep, destroy, save** (21.9 F4): a volume with droplets in flight does not auto-sleep; a forced sleep, a
destroy or a `save_world` first lands every droplet it owns at its column's surface (counted
`flushed_m3`), so spans and the world DB never miss in-flight water.
**Ledger:** grid + pool (born - landed in flight) is conserved to 1e-6 m^3 per tick on the CPU, per frame on
the GPU; `water_ledger.droplets` = the pool's volume (no longer 0).

**GPU birth, exactly.** The kernel zeroes the parcel's cells and appends {world cell, Σf, velocity} to a
per-frame-slot host-visible buffer (the 20.4 wet-body pattern: header + records, cap 4096 parcels per frame,
read two frames late, each slot consumed before it is rewritten). **The pool cap is enforced IN the kernel**
(21.9 F2 - the host learns of a birth two frames after the water left the grid, too late to refuse it): each
frame the host uploads a droplet budget = cap - alive - droplets reserved by the two unread slots; the kernel
computes the parcel's n = ceil(Vp / v_d) and reserves it with one atomic; no budget (or a full record buffer)
-> the parcel keeps its fill, counted `refused`. So the mass removed on the GPU is exactly the mass the host
reads back, and the pool never exceeds its cap. A droplet appears two frames after birth, advanced by
velocity x age (the 20.14 prediction pattern).
**Determinism** (21.9 F7): parallel births do not race - a parcel is born only if every neighbour cell is
air in the pre-state, so no born cell is a neighbour of another column's liquid, and zeroing it cannot change
another column's decision. Record ORDER is atomic-order (not part of the contract: droplets are placed by the
world-cell hash, not by index); which parcels are refused when the budget runs out is order-dependent - a cost
bound, counted, never on the bench rigs.

**Render - water-shaded microcubes.** One instanced cube draw in the water pass after the surface mesh
(depth test on, depth write off, alpha blended, unsorted - drops are small; sort errors are not visible at
this size, re-check in the recording). Each face shaded with `water_common.glsl`'s pieces: Fresnel sky
reflection by the face normal + sun specular + the lit body colour of 21.3 over the refracted scene, alpha
0.85. Not the VFX pipeline: `vfx.frag` is additive emissive - water drops would glow.

### 21.3 Part B - clear water: lit in-scatter

Replace the constant radiance by the light the water actually receives. Deep water seen from above returns a
fraction R of the downwelling irradiance - the *irradiance reflectance* - "in most clear natural hydrosols
... in the neighborhood of 0.02, give or take 0.01, for mid-spectrum wavelengths"; "turbid, near shore or
inland waters ... 0.08 or more" (Mobley, Ocean Optics Web Book, *Reflectances*,
oceanopticsbook.info/view/inherent-and-apparent-optical-properties/reflectances). So:

    scatter = R(turbidity) x ( ubo.ambientColor  +  the sun's diffuse term at N = +Y )

- the ambient and sun terms in the SAME normalisation `voxel.frag` uses for a Lambertian surface of albedo R
  (ambient x albedo; `pbrBRDF`'s diffuse lobe for the sun - read at build, not guessed), so a pond and its
  floor are lit by one light. **Shadowed, through the shared model** (21.9 F5: an unshadowed lit term would
  make a pond under a roof or in a cave glow over its dark floor - the same defect again): the sun term takes
  the shared cascade shadow lookup at the surface point, the ambient is `phxAmbient` (the probe field) at the
  surface point with N = +Y. Both are already reachable: the water pipelines' set 0 IS the scene's global layout
  (`RenderCoordinator.cpp:318` passes `vulkanDevice->getDescriptorSetLayout()`; `shadowMap` binding 2,
  `shadowMapNear` 9, `GiProbes` 13) - no new descriptors (confirm for `water.frag` / `water_cell.frag` at
  build). This makes water a RECEIVER of the shared model for its in-scatter: the `LightingPipeline.md` §0 row
  changes (shadow: the cascades; ambient: the probe field) in the same commit, R8/R9 hold (no own trace).
  The reflection keeps the atmosphere (17.3).
- R_G = 0.02 clear, 0.08 turbid (the sourced values). The per-channel SHAPE is the shipped artistic one
  (WATER_SCATTER 0.04 : 0.18 : 0.24, turbid 0.20 : 0.26 : 0.18), scaled so G hits the sourced value:
  clear R = (0.0044, 0.020, 0.027), turbid R = (0.062, 0.080, 0.055). The shape stays labelled unsourced.
- the `tint` knob (G3) keeps its meaning (the glow colour), rescaled to the same units.
**It changes every water in the engine** (the sea sheet shares `water_common.glsl`): the owner decides with
before/after captures of the Small pond and the Coast sea at fixed poses. A debug switch
`water_render_core {scatter: "lit"|"legacy"}` exists for that A/B only.
**Pinned check at risk** (21.9 F6): `tools/water_look_l4.py` (G3, Coast) asserts a 2 m clarity + brown tint
moves the band and the sheet by > 0.05 in mean colour. With the in-scatter now a few % of the light, the tint
moves the picture less: S0 re-runs it; if it drops below 0.05 the threshold is re-derived from the new model
in the same commit, with the reason - never loosened silently.
**Clarity answered:** how clear a given body is stays the per-body `water_look` knob (turbidity / clarity /
tint, G3); this fixes only that "clear" was not clear. Predicted for the bench pond (clear, 1.7-2.4 m path):
the floor is ~95 % of what the pond shows in G (from 21 %), the pond reads DARKER and greener than its dry
floor (G ~0.8x), the stone on the floor is visible.

### 21.4 Part C - whitewater from the solver

A per-column foam value f_w in [0, 1] in every volume (CPU array; GPU buffer nx x nz, persistent). Each tick:
decay x (1 - 1.5 dt) (G2's 0.46 s half-life - the same water, the same rate), then add rate x dt from:
1. **Body entry** - a moving solid at the column's surface (`bodyAtSurface` > 0) faster than 1 m/s:
   2 x min(1, (speed - 1) / 4) per second.
2. **A breaking surface** - the drawn surface moving faster than 0.5 m/s vertically (the crown, the cavity
   collapse): (|d eta/dt| - 0.5) / 1.0 per second. 0.5 m/s is above everything the signed-off slosh and
   floaters produce (to be confirmed on the floaters scenario before building - a control row).
3. **Converging flow** - surface divergence < -0.3 / s, G2's rule (b) unchanged; the 0.08-0.10 m/s phantom
   current is spatially smooth and diverges by < 0.05 / s (to measure on the still pond: control).
4. **Droplet landings** - each landing adds v / h^3 x min(1, speed / 3) to its column (a shower of drops
   whitens the water where it falls).
Fed through the existing `SurfaceColumn::foam` slot (`wc_surface.comp` writes it, the CPU extractor mirrors it)
into the existing foam shading. **One shading change:** the foam pattern's patch size is ~3 world units
(`waterFbm(fp * 0.32)`) - bigger than the whole splash; on simulated water the patch scale follows the cell
(patch ~ 3 cells), a per-draw constant in the push block, world-position noise unchanged (no tiling, no seams).

### 21.5 Feature Design Keys

- **Voxel aesthetic.** Droplets are cubes (microcube at the default cell), the engine's particle idiom, like
  debris; never sprites. Detail unconditional: the pool cap (20 k) is a cost bound that REFUSES births (the
  water stays grid water) - it never changes how a born droplet looks.
- **Chunks.** No chunk-derived input. Birth is per world-aligned cell (the 20 world-index rule), the spread
  hash is world cell + tick, foam is per volume column, droplets live in world space. The only boundary is the
  volume box (world-derived, camera-independent, §14.2). Equality test: **D-T7** - the same scripted parcel
  at a chunk-seam column and an interior column births the same droplet count, volume and velocities (bit
  equal after translation); and two volumes splitting one pond at a seam (the 20 T7 rig) birth the same set.
- **Procedural generation.** None - runtime only; consumes nothing from generation; no recipe field.
- **API.** `water_coupling {droplets: bool, foam: bool}` (unchanged when omitted; echoes both);
  `water_droplets {size}` (fraction of a cell, default 1/3, clamped [1/9, 1], omitted = unchanged; the
  response echoes it) -> {alive, cap, size, born_m3, landed_m3, in_flight_m3, refused, relocated_m3,
  flushed_m3}; `water_ledger.droplets` real. `water_render_core {scatter: "lit"|"legacy"}` (A/B; echoes).
  Clamps: cap fixed at 20 000 (§12; a game knob later); the size clamp above, commented at the split site. Defaults: droplets ON, foam ON, scatter lit - pinned tests updated in the
  same commits: `WaterLookTest` (it does not pin the scatter constant - checked), the floaters/blast coupling tests (unchanged
  expected; they must still pass).
- **Visual test plan.** Below.

### 21.6 Test plan (red first; predictions written now)

Rigs: the Small pond (one chunk, 4 x 4 m, 1/3 m cells, shipped default cell size), the 20.8 CPU pond, the
DebrisLab settle bench is NOT required (no `solver_*.comp` change). Shipped defaults except the realtime toggle.

| # | Layer | Test | Prediction | Control | Red today |
|---|---|---|---|---|---|
| D-T1 | unit CPU | a 0.2-cell scrap launched upward 3 m/s in a closed box | born on its first isolated tick; all lands within the flight time +0.1 s; grid + pool conserved <= 1e-6 m^3 EVERY tick | the same box, rested water: 0 births in 60 s | no pool: the scrap is grid water |
| D-T2 | unit CPU | the T4 stone entry | >= 1 parcel born during the cavity collapse; no isolated run with Σf < 1 exists after any tick's birth pass | births off: isolated scraps present (the slab) | slab present |
| D-T3 | unit CPU | 19.7 pocket pond (`DiagPockets` rig) + a slosh at 3 m/s | 0 births (pockets are connected) | - | - |
| D-T4 | unit CPU | pool full (cap forced to 10) | births refused, refused > 0, ledger still exact | - | - |
| D-T5 | GPU parity | scripted scraps | GPU births = CPU births (cells, volume to 1e-6) | - | - |
| D-T7 | unit CPU | seam vs interior; split volumes | identical births (21.5) | - | - |
| C-T1 | shader probe | the clarity probe (21.1 B) | body G within 10 % of 0.83 x floor (from 4.1x); stone-vs-floor contrast through water >= 70 % of dry | `scatter: legacy` reproduces 0.039 | 21 % floor share |
| W-T1 | unit CPU | T4 entry | foam >= 0.5 within 0.2 s on >= 4 columns round the entry; < 0.05 everywhere by 3 s | still pond 60 s: foam == 0; floaters scenario: max foam < 0.1 | foam 0 |
| L4-1 | live | stone drop, every frame (`water_solids_probe`), 3 drops | frames with a detached drawn run of Σf < 1: 0 (today ~6 of 400; runs of Σf >= 1 counted separately - the owner judges them); droplets alive peak >= 50; landed = born +- 1e-6 within 2 s; crater/ring unchanged within 20.14's spread | droplets off: the slab returns | slab |
| L4-2 | live | recordings `water_motion_check --scenario stone`, solids on/off | pond-region frame change after entry >= 1.0 grey levels/frame for 1 s (today 0.09-0.15) ; frame time <= 20.14's 4.08 ms + 0.3 ms | solids off | 0.09-0.15 |
| L4-3 | live | floaters scenario | settle unchanged (0-3 awake by 12 s); no foam on bobbing floaters | - | - |

Then the owner's look (stone drop + Coast before/after for 21.3). Status wording: built, not signed off.

### 21.7 Build order

- **S0 clear water** (21.3): shader only + `LightingPipeline.md` §9 entry + `lighting_doc_check --update`;
  `build_shaders.bat`, commit the `.spv`. C-T1 red -> green; before/after captures Small + Coast -> owner.
- **S1 droplets, CPU core** (21.2): FIRST measure the jet's detached runs (Σf per run, on the CPU T4 rig and
  live via `readCells`) - the 21.2 Σf < 1 rule and L4-1 assume the slab is a scrap; if the jet's runs hold a
  cell or more, stop and re-decide the rule with the owner. Then: parcel finder, pool, flight, landing,
  sleep/destroy/save flush, ledger; D-T1..T4, D-T7, D-T8 (sleep / destroy / save with droplets in flight:
  ledger exact, `flushed_m3` = the in-flight volume).
- **S2 droplets, GPU**: birth mode in `wc_column_ops`, record buffer, deposit delta upload folded into the
  step; D-T5; the +0.5 ms item closed.
- **S3 droplet render + live**: pipeline, L4-1, recording.
- **S4 whitewater** (21.4): CPU + GPU foam, patch scale; W-T1, L4-2, L4-3; recordings -> **owner's look**.

### 21.8 Not in this design (logged)
Droplets leaving a volume (E4/G); droplets hitting debris or characters; mist/spray below a microcube;
sound; ripples below the cell size (19.8 item 1's shading half); sub-cell surface pressure (item 3); lumps of
>= one full cell flying (stay grid water - judge in the recording).

### 21.9 Design-check (2026-10-10) - round 1 NEEDS WORK, folded in, round 2 READY for S0

Round 1 checked the draft against the code; nine gaps, all folded into 21.2-21.7 above:
- **F1** box walls reflected droplets: an invisible wall in mid-air (the box can be residency-clipped,
  14.2) -> free flight, out-of-box landings relocated + counted, 0 on the rigs.
- **F2** the pool cap was enforced on the host, but a GPU birth is known two frames late -> the budget is
  uploaded and reserved in the kernel.
- **F3** landing tested "f + s >= thr" - the host has no f field for a GPU volume -> land against the
  read-back surface field; deposits as world points.
- **F4** in-flight water was lost on auto-sleep, destroy and save -> no sleep with droplets in flight;
  destroy / sleep / save flush first; D-T8.
- **F5** the lit in-scatter was unshadowed -> shadowed + probe ambient through the shared model (set 0 already
  bound), `LightingPipeline.md` §0 row changes.
- **F6** `water_look_l4.py` pins a tint shift > 0.05 that the new model may shrink -> re-run in S0,
  re-derive with the reason if needed.
- **F7** GPU determinism was unstated -> the no-race argument and what is order-dependent, written down.
- **F8** the droplet-size clamp named a value no caller could set -> the `size` knob, clamped and echoed.
- **F9** L4-1 assumed the slab is under one cell of water, unmeasured -> S1 measures it first; L4-1 counts
  Σf < 1 runs, larger ones separately.

Round 2: voxel aesthetic (cubes, unconditional, cap refuses never reshapes), chunk independence (world-cell
births and hashes, no box walls, D-T7), generation (none), API (units, unchanged-when-omitted, echoes,
clamps commented, defaults and the one pinned check named), test plan (red rows, predictions, controls, one
chunk, deltas from defaults: the realtime toggle only) - all answered. **READY for S0.** S1-S4 each re-read
this section before building; S1 stops at its measurement if the slab is not a scrap.

### 21.10 S0 built - clear water on simulated water (2026-10-10) - ledger, NOT signed off

**Scope cut by the owner, before building:** small bodies only. The lit in-scatter applies to
`water_surface.frag` (simulated water). The sea sheet (`water.frag`) and `water_cell.frag` compile the same
code with `scatterLit = 0` - their look is unchanged and was not measured (the Coast "before" capture
taken while scoping this was large-body drift and is not part of the evidence).

**Built:** `water_common.glsl` `waterLightAt` (phxAmbient up + sun x near ∪ mid fast shadow x phxSunGate,
Lambert 1/pi) x `WATER_REFLECTANCE` (0.0044, 0.020, 0.027) / `_TURBID` (0.062, 0.080, 0.055); the G3 tint
keeps its hue and is mapped onto R at the same turbidity. Each water includer's std140 prefix extended to
`giProbeGrid`; shadow maps and the probe field come from set 0 (no new descriptors).
`water_render_core {scatter: "lit"|"legacy"}` (echoed). `LightingPipeline.md` §0.2 new row + §9 entry.

**Results (Small bench, pose 109.5/20.5/13.5 yaw 180 pitch -38, `tools/water_clarity_probe.py`,
`s0_clarity.json`; legacy = the control, same frame):**

| | legacy (control) | lit | prediction (21.6 C-T1) |
|---|---|---|---|
| body tap G, linear | 0.0400 | 0.0105 | legacy ~0.039: **met**; lit 0.83 x floor = 0.0092 +- 10 %: **MISSED** (0.94 x floor) |
| floor detail surviving the water (std ratio, G) | 0.349 | **0.775** | >= 0.7: **met** |
| pond mean sRGB vs dry floor (86, 84, 79) | (134, 149, 138) - brighter, milky | (61, 85, 86) - darker in red, blue-green | - |

The miss: the pond is 94 % of its floor's brightness, not 83 %. The prediction assumed the floor reflects
far more than the water's 2 %; this rig's floor is a dark stone, so the water's own in-scatter is not
negligible beside it - a likely cause, **not yet established** (it needs the floor's albedo and the path
length measured, not inferred). Recorded as missed, not re-fitted.

**Seen in motion** (`s0_stone_on.mp4`, same stone drop as 20.14, the pond region, every frame): the
falling stone, its shadow on the water and the stone resting on the floor are now visible (20.14's "stone
absent on the floor" was the glow hiding it - closed). Pond change per frame after entry: 0.49 (first
0.5 s) / 0.28 / 0.22 grey levels vs 0.14 / 0.08 / 0.07 before - ~3.5x; the still pond also changes more
(0.08 vs 0.02: the ripple refraction now shows). Still below 21.6 L4-2's >= 1.0 for 1 s - that row is for
all of item 2 (droplets + whitewater). The jet's detached run now reads as a thin dark line mid-pond -
the droplet crown's job (S1). Frame time unchanged: 4.08 ms mean both (p99 6.82 vs 7.01).

**Not done in S0:** the `water_look_l4.py` pin (G3) runs on the Coast - deferred with the sea (its look
is unchanged, so its pin is unaffected).

### 21.11 S1's first measurement - the jet is scraps, and there is almost nothing in it - design revised

**Measured live** (`tools/water_jet_probe.py`, `s1_jet.json`; the 20.14 rig, 3 stone drops from 3 m, every
frame for 2 s, every detached run via `/api/debug/water_jet_scan`): runs above the rest level appear in
11 / 33 / 53 of ~390 frames, up to 2.0 m above the pond; **every one holds less than one cell of water**
(largest sum f 0.105 = 3.9 litres; typically 0.006-0.03 = 0.2-1 litre); a third to two thirds of the
run-frames are NOT isolated (thin sheets touching each other sideways). The CPU T4 rig throws almost none
(`DiagJetParcels`: 3 run-ticks, sum f <= 0.063, 17 cm up - its stone is kinematic at a constant speed; the
live stone decelerates and tumbles).

**What it means.** 21.2's assumption holds (the slab is scraps) - but the scraps hold 1-4 litres per entry.
As 1/9 m droplets that is ~3 cubes: the floating slab would go, and still no splash you can see. The
1/3 m grid cannot hold a crown: a real entry throws a thin sheet at many times its own speed (the sheet
separates near the body's equator; crown and Worthington-jet size grow with Fr = V / sqrt(g D) - Truscott
et al., *Water entry of projectiles*, Annu. Rev. Fluid Mech. 46 (2014); the spinning-sphere and mass-ratio
entry papers found with it). In the grid that water exists only as a fast-RISING SURFACE, never as
separate scraps.

**Revision of 21.2 (birth), before building:**
1. **Scraps, wider rule.** A detached run is born when it holds sum f < 1 and none of its cells touches,
   sideways, water that is SUPPORTED (a run resting on water, a solid or a body). Sheets of scraps touching
   each other are born together; the 19.7 pocket roofs (beside supported pond water) and crown rims (water
   below) still never are. Determinism: a cell's "supported" is decided by its own column's run bottom;
   a run being born is unsupported both before and after it is zeroed, so parallel births do not race.
2. **Spray from a fast-rising surface (new).** At the top cell of a supported run, if the water leaves
   upward faster than v_c = sqrt(2 g h) (2.56 m/s at 1/3 m: fast enough to rise more than one cell above
   the surface, which the grid can only carry as a slab), the flux beyond v_c leaves as droplets:
   volume f x h^2 x (v_up - v_c) x dt per tick, at most the cell's water above the film; velocity = the
   cell's velocity. No tuning factor: v_c and the flux are the grid's own resolution limit. Mass-exact.
3. **Droplet size.** Default edge = h/9 (3.7 cm at 1/3 m; 51 ml), so a litre is ~20 drops: the
   `size` knob's default becomes 1/9, clamped to [1/27, 1] (below 1/27 one cell is ~20 000 drops - the
   whole pool).
**Predictions for L4-1 (revised):** per stone drop (3 m, 1/3 m stone) droplets alive peak >= 50, the
highest droplet >= 0.5 m above the pond, born = landed + in flight to 1e-6 m^3 every frame, no detached
drawn run of sum f < 1 in any frame; the floaters scenario (bobbing at <= 0.2 m/s) births none.
**New red test D-T9** (CPU): a column of water launched upward at 4 m/s through the surface emits; at
2 m/s (below v_c) it does not.
Re-gated against 21.9's checks: no new chunk input (both rules are per world cell), API unchanged except the
size default, the cost bounds (pool cap, per-frame record cap) unchanged. READY to build S1 with this rule.

### 21.12 The droplet unit tests (2026-10-10) - D-T1..D-T4, D-T7, D-T9 written; two defects found red and fixed

`tests/core/WaterDropletTest.cpp` (6 tests, all green after the fixes; 313 water/debris/shore tests green):

| # | Result | Numbers |
|---|---|---|
| D-T1 | **RED first, fixed** | a 0.2 m^3 scrap thrown up at 3 m/s in a closed CPU box: all of it born on tick 1, but **it never landed** - the landing read "ground" only from solid cells INSIDE the volume's box, and a box whose bottom is the floor has none (the solver treats outside-the-grid as solid; the droplets did not). They fell to the 10 s age cap, and one that left the box over a dry column fell forever (the age cap sat behind the relocation branch). Fix: ground = max(highest solid cell, the box floor). After: last landing 1.100 s vs 1.111 s predicted (apex + free fall), born = landed = 0.200000 m^3, worst ledger drift 1.0e-7 m^3. Control: rested pond 60 s, 0 births. Live the Small pond's box holds its stone floor, which is why S1's live run never showed it. |
| D-T2 | green, prediction revised | the CPU T4 entry throws no scraps at 6 m/s and one spray cell (1 ml) at 7.7 m/s - 21.11 had already measured this, so 21.6's "slab as control" is blind on the CPU rig. Control instead: one scripted scrap cell 0.8 m above the pond 20 ticks in - seen floating for 8 ticks with births off, 0 ticks with births on. No floating scrap survives any birth pass; drift <= 4.9e-7 m^3. |
| D-T3 | green | the DiagPockets rig (446 pockets after the blast) then a 3 m/s slosh: 0 scraps, 0 sprays in 5 s. |
| D-T4 | green | pool capped at 10: never more than 10 alive, 35.6 m^3 refused over 3 s (the refused scrap goes back, is reborn, refused again), ledger drift 2.6e-7 m^3. Also fixed: a part-refused birth's odd last droplet broke the zero-sum spread (`fit`, not `n`). |
| D-T5 | not written | GPU parity needs a device - belongs in `tests/integration/WaterCoreGpuParityTest.cpp`. Open. |
| D-T7 | **RED first, fixed** | the same parcel in two volumes whose boxes differ (both across the world-cell-288 seam): **401 of 401 droplets differed** - the spread hash keyed on the GRID-LOCAL column, so a splash's droplets depended on where its volume's box started (a 21.5 violation). Fix: the hash keys on the world cell column. After: 0 differ; seam column vs interior column: same count, volume and mean velocity. |
| D-T9 | green | top face at 4 m/s: one spray of exactly f (v - v_c) dt / h of a cell (1.336e-3 m^3), what the grid lost; at 2 m/s none. |

Live after both fixes (Small bench, stone drop): 12 births, peak 22 droplets alive (S1's run peaked at 53 - the
tumbling stone varies), born = landed = 0.534 litres, pond ledger -2e-6 m^3 over 24 m^3 (float). Not new looks:
the hash change only moves where each droplet starts inside its cell.

## 22. Ripples you can see - a sub-cell wave layer on every small body (owner, 2026-10-10) - DESIGN - design-check READY for R0 after one fold-in (22.8)

The owner, after the first droplet cut (21, `6a2b79f8`): "honestly doesnt look bad. maybe more particles would
be nice, but really what i think will bring the appearance home, is for there to be visible ripples." Then:
"a slightly voxelated appearance to water for ripples seems ok to me. at 1/9 size units ... at least worth checking".

**Checked first - the whole pond at 1/9 m cells** (`s1_stone_ninth`, same stone drop, droplets + clear water on):
no ring train in the picture (faint marks at the entry; 2.3 cm at 0.7 m, 1.6 cm at 1.4 m - the same as 1/3 m),
the droplets nearly vanish (the finer grid holds the spray itself), and the frame costs 11.8 ms mean / 25.7 ms
p99 against 4.2 / 9.3 at 1/3 m. Finer water is not the lever; a ripple layer at the microcube pitch is.

### 22.1 Why there are none, and the decision this reverses

- **The grid cannot carry them.** A 1/3 m cell holds waves no shorter than ~2/3 m (19.4, measured). The rings
  a stone or a drop makes on a pond are ~5-20 cm: below the grid, so the pond shows a slow swell and a crater
  and never a ring.
- **§7 said "disturbance visuals ARE the simulation ... no separate ripple field (`RippleField` retires)".**
  That was decided before 19.4 measured the grid's limit. **This section reverses it for the band the grid
  cannot hold**: wavelengths from 2h down to ~2 lattice cells of a finer layer. Everything longer stays the
  grid's (no double counting - 22.3). The old `RippleField` is NOT revived: a 0.5 m lattice, one wave speed
  for every wavelength (a single expanding circle, never a ring train), player-following, wired to the old
  water. It still retires with `WaterManager` (§9).
- **What a ripple layer is and is not:** surface DISPLACEMENT with zero mean, no mass. It moves no water, so
  it cannot create water, raise a level or be a source (the owner's rules hold); it changes how the surface
  looks, as the grid's own waves do, at the scale the grid cannot.

### 22.2 The physics - iWave (Tessendorf)

One layer per eulerian volume: a height field r(x, z) on a world-aligned lattice of pitch d = 1/9 m - the
microcube (owner, 2026-10-10; 36 x 36 for the 4 x 4 m Small pond: ~0.1 ms a tick on the CPU, no GPU phase
needed). It holds wavelengths from 2d = 22 cm (accurate from ~4 cells, 44 cm) up to the grid's 2/3 m; the
5-20 cm ripples of a real pond are below it - the owner judged the microcube scale "good enough, worth checking", evolved by Tessendorf's iWave (*Interactive Water
Surfaces*, J. Tessendorf, Game Programming Gems 4, 2004; jtessen.people.clemson.edu):

    r(t+dt) = r(t) (2 - a dt)/(1 + a dt) - r(t-dt)/(1 + a dt) - g dt^2/(1 + a dt) * (G ⊛ r)(t) + sources

G is the vertical-derivative kernel (13 x 13, radius 6 cells): the convolution form of sqrt(-laplacian), so every
wavelength runs at its own speed - omega^2 = g k (deep water; a 10 cm ripple at 0.40 m/s, a 60 cm wave at 0.97
m/s): a disturbance spreads into a TRAIN of rings, longest first. Capillarity is folded into the kernel
(omega^2 = g k (1 + sigma k^2 / (rho g)), sigma 0.0728 N/m: +0.6 % at the lattice's shortest wave -
included because it is free). Obstacles by Tessendorf's masking: solid columns and dry columns are masked (r = 0), so rings reflect off the
pond walls. **Bodies are NOT masked** (22.8 F2): in iWave a moving mask itself makes waves, so masking a body
AND giving it a source (22.3) would drive it twice; a body is a source only - rings pass under a floating crate,
which hides its own footprint. The
volume's open boundary (water continuing past the box) is an absorbing rim (2 cells, as the old field's).
**Damping a.** Clean water's viscous rate (2 nu k^2: 127 s for a 10 cm ripple) is far too slow for real
ponds, whose surfaces carry films; an inextensible film damps at ~ k sqrt(nu omega / 8) (Lamb, *Hydrodynamics*,
§351; the film limit) - ~0.11 /s at 10 cm, ripples visible for several seconds. One constant a evaluated at
the 20 cm band centre (the kernel cannot vary a per k); **the formula is to be confirmed by the grounding
auditor at build**. The layer sleeps (exact zero) when its energy falls below a threshold. **Time step** (22.8 F4): leapfrog is
stable for omega_max dt < 2; at d = 1/9 m omega_max = sqrt(g pi / d) = 16.6 rad/s, so dt <= 0.1 s - a frame
longer than 1/30 s is split into equal substeps (at most 4; a longer stall drops time - a visual field owes none).
**Lifetime** (22.8 F5): a volume does not auto-sleep while its layer is awake (bounded: R-T4, <= 30 s), so its
rings never vanish mid-flight when the volume writes back.

### 22.3 What drives it - only what the grid cannot hold

The layer is one-way and sub-grid: it never feeds the grid, and it receives only the part of a disturbance
the grid cannot represent.
1. **Droplets landing (21)** - each landing hands the surface its downward momentum: the surface velocity over
   its lattice cell is kicked by w = m v_y / (rho d^2) (m = rho x its volume; momentum conservation, no factor).
   A 20 ml drop at 3 m/s on an 11 cm cell: w ~ 5 mm/s - a faint ring each; the crown's ~50 drops together ring
   the pond where they fall.
2. **A body crossing the surface** (20's moving solids, every frame its box spans the waterline): the water
   under it moves with it (the kinematic condition), w = the body's vertical velocity over its footprint. The
   grid already moves the cell-averaged part; the layer receives the remainder - w x (footprint on the fine
   lattice - the footprint box-filtered over the h x h cell around each point): the sharp edges of the body
   the 1/3 m grid smooths away. A bobbing floater at 0.2 m/s rings gently and forever-ish (a real floater does).
3. Nothing else in v1 (no wind, no rain, no character wading - E4).

### 22.4 How it is drawn

The layer's heights upload each frame (only while awake) into one R16F atlas texture (all volumes' layers packed;
a 36 x 36 pond is 2.6 KB), bound to `water_surface.frag` at set 1 binding 2, with a small table (binding 3:
per layer world origin, 1/d, atlas rect). **Voxelated, as the owner asked:** each 1/9 m
lattice square is one flat facet - the fragment takes the gradient of the square it lies in (central
difference of the square's neighbours, nearest sampling) and tilts the mesh normal by it: N' = normalize(N +
(-dr/dx, 0, -dr/dz)). A ring reads as a band of microcube facets catching the light. Bilinear (smooth) is a
debug A/B (water_ripples {smooth: true}) for the owner's look, not a default. The tilt reaches refraction (the
floor wobbles through the ring), the sky reflection and the sun glint - a ring reads as a moving band of
distortion and glints, as it does on a real pond. No vertex displacement (mm-cm heights are below what a
1/3 m mesh vertex can show). Droplet cubes and side faces sample nothing. Each vertex carries its layer index
in the vertex's spare float **as index + 1** (22.8 F1): that float is 0 on every vertex today (the shore band,
the fine band, droplet cubes), and 0 must keep meaning "no layer" - a shore vertex reading layer 0 would wear
the pond's rings.

### 22.5 Feature Design Keys

- **Voxel aesthetic.** Ripples are drawn on the microcube lattice, one facet per 1/9 m square - the engine's
  smallest static voxel, as the owner asked. Unconditional: no quality tier. The cost bound REFUSES (a volume over the budget gets no layer,
  counted and logged) - it never coarsens a layer, so a ripple never looks different because of cost.
- **Chunks.** The lattice is world-aligned (pitch 1/9 m from world 0), per volume; no chunk input. A pond
  split over two volumes would see its rings stop at the volume seam - volumes are whole ponds today (16); logged.
  Equality test **R-T6**: the same droplet landing at a chunk-seam position and an interior position gives the
  same ring (bit-equal after translation).
- **Generation.** None - runtime.
- **API.** `water_ripples {enabled?, debug?}` (omitted = unchanged; echoes enabled, layers, awake layers, cells,
  refused (over budget), cost ms, max |r|). `water_render_core {debug: 7}` = the ripple height as grey (a tap; the route's debug clamp rises from 6 to 7
in the same commit). `water_ripples {smooth: bool}` = the bilinear A/B (default false: facets).
  Budget: 512 x 512 lattice cells per layer (a 57 m pond), 4 layers - refused beyond, said so.
- **Visual test plan** (22.6).

### 22.6 Tests (red first; predictions written now)

| # | Layer | Test | Prediction | Control |
|---|---|---|---|---|
| R-T1 | unit | a point kick in a 4 x 4 m layer: ring speeds | the leading crest's speed matches the group speed of the kernel's dispersion within 10 % at 3 radii; short waves trail long ones | a non-dispersive wave equation (the old field's) shows one ring at one speed |
| R-T2 | unit | the kernel's dispersion: a plane wave of each lattice wavelength | omega within 5 % of sqrt(g k (1 + sigma k^2 / rho g)) down to 4 cells (44 cm) | - |
| R-T3 | unit | masked wall: a ring hits a solid column line | reflects (energy behind the wall 0) | - |
| R-T4 | unit | sleep | the layer reaches exact zero within 30 s of one kick; a still pond never wakes | - |
| R-T5 | unit | no double counting: a body footprint aligned to the 1/3 m cells, moving at constant speed | the high-pass source is zero where the footprint fills whole cells, non-zero only along its edges | - |
| R-T6 | unit | chunk seam (22.5) | bit-equal ring | - |
| R-L1 | live | stone drop (20.14 rig), every frame (22.8 F3: if the ring is not visible, STOP and report - do not
add a source the physics does not give; the stone's ring energy is mostly in the grid's long waves) | a ring visible: pond-region frame change >= 3 grey levels/frame for >= 0.5 s after entry (S1 cut: 1.95 for 0.5 s), then decaying over >= 2 s; layer cost <= 0.3 ms/frame on the CPU (else R2) | `water_ripples {enabled:false}`: S1 numbers |
| R-L2 | live | floaters scenario (19.6) | they still settle (0-3 awake by 12 s); gentle rings round bobbing pieces | layer off |
| R-L3 | live | recordings, stone + floaters -> **owner's look** | - | - |

### 22.7 Build order

- **R0** CPU reference: the layer (kernel, masking, sleep, sources from droplets + bodies), R-T1..R-T6.
- **R1** render: atlas upload, binding, normal tilt, the debug tap; R-L1..R-L3, recordings -> owner.
- **R2** (only if R-L1's cost gate fails) the layer on the GPU (a compute pass beside the solver's).

### 22.8 Design-check (2026-10-10) - round 1 NEEDS WORK, folded in, round 2 READY for R0

Checked against the code (`WaterSurfaceVertex`'s spare float, the `water_surface` set-1 layout, the auto-sleep
path in `WaterCoreManager::update`, iWave's masking):
- **F1** the layer index rides the vertex's spare float, which is 0 on the shore band, the fine band and the
  droplet cubes - they would read layer 0 -> stored as index + 1, 0 = none.
- **F2** bodies were both masked and sources; in iWave a moving mask makes waves itself -> bodies are sources only.
- **F3** the stone's entry may hand the 22 cm-67 cm band little energy (its crater is the grid's long waves; the
  layer gets only the footprint's sharp-edge remainder and the droplets' kicks) -> R-L1 is a stop condition, not
  a tuning target: if no ring shows, report it before inventing a source.
- **F4** the leapfrog step's stability bound was unstated -> omega_max dt < 2, substeps for long frames.
- **F5** a volume could auto-sleep with rings in flight and drop them -> no auto-sleep while the layer is awake.
Round 2: voxel look (microcube facets, unconditional, the budget refuses never coarsens), chunks (world-aligned
lattice, no chunk input, R-T6), generation (none), API (units, unchanged when omitted, echoes, clamps), tests
(red rows, predictions, controls, one-chunk rig, rig = shipped defaults + the realtime toggle) - answered.
**READY for R0.** The set-1 bindings 2 and 3 are new descriptors on `WaterSurfaceRenderPipeline` (its layout and
pool grow; the shore band and fine band draw through the same pipeline and bind the same atlas, sampling none).

### 22.9 R0 + R1 built (2026-10-10) - ledger: the rings exist, the picture barely shows them - owner: acceptable for now

**Owner verdict (2026-10-10), after the stone-drop frames + clip:** "i dont think it is perfect, but i can handle that
for now". Accepted as it stands, NOT finished: the levers below (reflections on simulated water, the procedural
shimmer turned down, sun glints on facets, a stone-entry ring source) stay open and are not to be built unasked.

**Built:** `RippleLayer` (CPU, per eulerian volume, 1/9 m world-aligned lattice), driven by droplet landings
(Cauchy-Poisson impulse, spread over a 3 x 3 tent) and wet bodies crossing the surface (every wet body, resting
ones too - `setRippleBodies`), masked by the volume's surface field, zero mean per connected body of water;
drawn through `water_surface.frag` set 1 binding 2 (a dynamic storage buffer, per frame slot) as microcube
facets (default) or smooth (`water_ripples {smooth}`); `water_render_core {debug: 7}` = the height.

**Found and fixed while building (each measured):**
1. The kernel as a truncated inverse transform ran waves 9-30 % slow (L ~ |k| has a long tail) -> fitted by
   least squares on the band; fitted on the band ALONE its response went negative near Nyquist and the field
   blew up -> the fit covers the whole lattice spectrum at a lower weight; `KernelIsStableEverywhere` pins
   response > 0 and < the leapfrog bound (measured 0.065 .. 39.4, bound 367).
2. The damping written from memory of iWave's form - (2 - a dt)/(1 + a dt), 1/(1 + a dt) - carried a hidden
   stiffness 2a/dt: every wave 8-10 % fast at 240 Hz -> the centred damped step (frequency untouched).
3. A one-cell kick holds 2-cell waves the kernel only approximates; they ran ahead as lattice junk -> the 3 x 3 tent.
4. The mean drifted (+5 mm over the pond live: masked walls cut the kernel) -> zero mean per connected body (one
   global mean linked the two sides of a wall - `AVoxelWallReflects` caught it).
Unit (WaterRippleTest, 7): dispersion within 2.6 % (0.44-1.33 m), plane waves within 1.3 %, a ring train, exact
reflection at a voxel wall, a droplet's ring asleep in 1.7 s, chunk seam bit-equal.

**Live (Small bench, stone drop, `water_ripples`):** droplet landings drive it (16-40 impulses per drop; 2-4 cm
peaks near the entry, 3-5 mm still running 3 s later); the STONE'S own crossing gives almost nothing (9
kinematic cells in one frame - it crosses the waterline in a frame; 22.8 F3's risk, reported, no source invented).
Layer cost 0.18 ms/frame (gate 0.3) after skipping masked cells. The height tap shows rings round the entry.
**R-L1 MISSED:** pond-region change after entry with the layer on vs off (mid pose 105.2/18.6/13.5, pitch -24):
1.52 vs 1.31, 0.76 vs 0.53 grey levels/frame (prediction >= 3) - in the frames the rings show only as faint
warping of the floor. Two shader experiments, measured and REVERTED (owner's call): screen-space reflections on
simulated water (+34 % in the second half-second: 1.03 vs 0.77) and the fixed procedural ripple texture turned
down 0.35 -> 0.05 (with it, 0.35-0.39 vs 0.14 at 2-3 s) - neither makes a ring you would notice. **Why:** a ring
tilts the normal, and at these view angles the pond's colour hardly depends on the normal - it shows the floor
through clear water and reflects a near-uniform sky (17.3, 21.10); a real pond's rings read through reflections
of bright sky and dark banks (and sun glints) bending across them.

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
