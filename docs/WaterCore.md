# Water Core — the foundation for all future water work

> **Status (2026-10-19): DESIGN OF RECORD and BUILD LEDGER, branch `feat/water-rethink`.** **Read §0a first** - one page:
> the goal, the scale model, what is signed off, the path to large water, the open items. **§27 is the plan for water at
> every scale** (simulated where it counts, cheap appearance elsewhere - the owner, 2026-10-19). §26 is the one water model
> and its build ledger; §1-§25 are the earlier design and ledgers; where a later section changed an earlier one, the later
> wins. Appendix A holds §0a's running log as it stood before 2026-10-19. `Water.md` is the reference for the OLD CA water
> that still runs in normal sessions (retired by §27 W4); `WaterRethink.md` is the 2026-10-07 stock-take. Standing rules:
> §0a.

---

## 0. The brief, restated (user, 2026-10-07/08)

1. **Small-scale function and feel are paramount.** Water flows, volume is respected, water shows
   visual signs of being disturbed by debris and characters, and it can be pushed, pumped, poured
   and removed.
2. **Small bodies are the core; large bodies sit on top of it.** Oceans, lakes and rivers are built
   from the same water, optimised where nothing moves, never the other way round.
3. **Water is a physical entity, never a universal level.** Digging down must never reveal a wavy
   sheet. Edits never create water. (Standing rule, repeated.) *Refined by item 8:* far and resting water may be DRAWN
   with cheap appearance motion, but it always EXISTS as recorded water - an amount, a level per column, real edges -
   so the rule still holds: no water where none was recorded, no level shared by bodies that are not connected.
4. **Shorelines of large bodies are probably small-scale.** (User's instinct; §8.3 agrees and says
   why.)
5. **Performance matters, but at small scale it should not be the constraint.** Measure, don't guess.
6. **What exists is reference, not a constraint.** Blow it away where something better is possible.
7. **Be slower, more thorough, far more detailed.** This document is the foundation for all future
   water work.
8. **Simulation where it counts, appearance elsewhere** (owner, 2026-10-19): *"we can never reasonably expect to have
   real simulated water at large lake or ocean level scales. the plan has to include methods that include simulation
   where it counts but cheap 'appearance' looking water."* The plan for it is §27.

---

## 0a. Where things stand, and the plan (consolidated 2026-10-19)

**The goal.** Water in the world that behaves physically where the player can touch it and looks right everywhere, from a
puddle to an ocean, inside **2 ms of GPU for all water**. Small bodies are the foundation; large bodies are built on the same
water, never the other way round.

**The scale model (owner, 2026-10-19; the plan is §27).** We will never simulate a whole lake or ocean as cells - a 1 km^2
lake 3 m deep is ~2 billion 1/9 m cells. So every body of water **exists** as a record (how much, where, its level per
column, its edges), always, and is **simulated** (the pond model, 1/9 m cells) only **where it counts**: near the player, and
where something disturbs it or it flows nearby. Everywhere else it is **drawn** from the record with cheap appearance motion
(waves, scrolling flow, foam) that moves nothing. The two meet where no seam can be seen. The physical rules still hold: the
record is real water, a dry pit stays dry, edits never create water, each body has its own level.

**Signed off by the owner, in motion:**
- the small pond: blasts, debris both ways, floaters, the drawn surface (§19.8); the ripples (§22.12.2);
- the one water model (§26): the creek as pond water (P1 26.5, 26.14), flat bricks (26.17), a dig without a freeze (26.19),
  the air fix (26.21), the dam & pond on pond water (26.22), floats in the current (26.25-26.26), the surface fixes (26.9),
  the still-water guess (26.10);
- the cost work of 2026-10-18/19: the partial wake's look (`pw_look4`), its freeze test x3 (26.34), 20 pressure sweeps (26.35),
  the partial wake ON by default (26.35).

**Built, not signed off:** moving solids M0-M2 (§20); clear water S0 (§21.10); the droplet pool (§21.12); the §24 wave items;
ground bricks (26.18); the column water removed (26.27); the surface tiles, view culling and CPU cuts (26.30 steps 1-3); the
partial surface (26.33).

**What water costs today (the limits pass, §27.20, 2026-10-23):** one awake pond fits 2 ms to ~25 x 25 m at 1 m deep
(~20 x 20 m at 2 m; to the volume cap at 0.3 m); a sleeping pond ~0.37 us per m^2 drawn; each awake body ~0.4-0.5 ms whatever
its size (~4 small ponds awake at once); 64 sleeping ponds cut the frame rate to 131 fps (the debris water map - fixed 27.21: 587 fps); a 2 m creek
1.5-2.0 ms up to 100 m long and 2.7 m/s, a 6 m creek 2.7-3.5 ms (over); one volume holds 2 M cells (a ~1,300 m^2 pond at 1 m,
a ~100 m creek) and 64 sources. **Tall cells ON (27.22f):** past ~1 m deep, depth no longer costs - a 3 m pond 3.15 -> 1.87 ms.

**The path to large water (§27), in order - each designed and design-checked before it is built, the owner looks in motion:**
1. **W1 - the record is the water** (design-check 2026-10-19: NEEDS WORK, five items in §27.7; step 0 measured §27.8; item 2 - the wake respects the ground - BUILT §27.9; detailed design §27.10 - 8 m tiles awake / frozen / recorded, wake ahead, no CPU box; W1a BUILT and SIGNED OFF 2026-10-20 §27.11 - recorded water drawn by the pond surface, the owner's choice A; W1b first slice BUILT §27.12 - frozen tiles sleep into the record and wake from it, the books exact live, recording OFF by default; W1c §27.13 - the post-push agitation was the life cycle's ORDER (it collected the call just submitted), fixed: a pushed pond settles and goes back into the record, the books exact; OPEN: a collect between two calls breaks the freeze test, woken tiles cycle in some runs; then the per-tile kernels and W1d-W1e). The GPU volumes sleep into the world's water record (chunk spans + body records) and wake a
   PATCH from it, not a box. Phase D built sleep / wake for the old dense volumes (§16); not yet for today's sparse ones. Ends
   the 2 M-cell box (the CPU keeps only the woken patch).
2. **W2 - appearance water.** Resting and far water drawn from the record with cosmetic motion and detail falling with
   distance; at rest it matches the simulated surface where they meet.
3. **W3 - where it counts.** The rule that picks what is simulated (distance, disturbance, nearby flow), held to the budget,
   and the hand-off both ways with no visible seam and the books exact.
4. **W4 - the world on the new water.** Generated lakes, rivers and sea placed as records; the old CA water (`WaterManager`)
   retired from normal sessions.
5. **W5 - rivers at scale.** Far reaches as records with a discharge and appearance flow; near reaches simulated, fed and drained
   at their ends from the record.
6. **W6 - shores and open sea.** A simulated band at the shore near the player; the swell offshore as appearance (§18.4: the 3-D
   core cannot carry a swell).

**Open small-body items** (polish and features; none blocks the path above):
- *Feel:* spiky water on a dropped stone - cause proved, waits for a repeatable rig (26.32) - and a 0.84 m late jet; wood
  floats high and a floating block's pond never sleeps (M4, §20.6); short waves on a half-full cell keep 0.53-0.82 per swing
  (§24.11); added mass (§24.10); the still-pond phantom current not re-measured (§19.6); water under a solid lid does not
  converge (26.23).
- *Look:* F2 sign-off and F3 deletions (§17.3); pond whitewater not wired (§21.4); 1.5-2.3 % black facets at steep tilts
  (§22.12.2); cosmetic splashes that take no water (owner preference 2026-10-11, not designed).
- *Parity:* the CPU-GPU kick gap (§19.2); droplet parity D-T5 (§21.12); the FLIP run-up (§15.15 - the one failing water
  integration test).
- *Cost:* the partial wake's +0.036 ms where nothing freezes; the reductions ~0.17-0.19 ms a call.
- *Not started:* wading (E4), containers, pipes, scoop, volume merge / split, the §23 liquids. Scenarios never run: S7 wading,
  S9 waterfall, S10 stream over rocks, S13 scoop, S14 pipe, C1-C5.

**Standing rules:** small bodies first; one water model - the pond's - for everything simulated (a second kind of water needs
the owner's explicit, plainly-explained approval); appearance water draws recorded water and never moves or makes it; no
universal level; edits and digging never create water; ripples and splashes carry forward wherever water is simulated; always
tools to measure and tune current speed; measure, don't argue; nothing is done until the owner sees it in motion; look
decisions (thresholds, defaults) are the owner's; any `solver_*.comp` change passes `tools/debris_settle_bench.py`; GPU water
tests are in `phyxel_integration_tests`, run from the repo root with the bench engine stopped, on Release; separate runs
drift ~0.2 ms - compare costs only interleaved on the same water.

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

> **Superseded 2026-10-19 by §27** (simulation where it counts, appearance elsewhere); the items below are inside §27 W1-W6.

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
| **B. The core, CPU reference** — **BUILT 2026-10-08** (§15.7–15.8; S2 4/4, S5 4/4, S4 PASS at 1 and ⅓, S3 front + seiche PASS; open: S1 rest 3.4–3.6 s vs 3 s, S3 run-up at 1 m — §0a)** | §4.4 rules green; S1–S5 predictions met; mass ledger exact |
| **B2. FLIP transport** — **BUILT 2026-10-08, opt-in** (§15.10: 24/24 tests, S3 run-up PASS; fails S1/S2/S4/S5, so fills stay the default; the comparison for the owner not yet read)** | `SurfaceTransport::FLIP` against the same grid (particles carry f and momentum; grid projection; particle↔grid transfer; re-seeding; rest conversion); harness runs S1–S5 on both modes and records the comparison | /design-check | Same scenarios green on FLIP; a written comparison (rest flatness, splash shape, cost) the user reads before Phase E chooses the default per scenario |
| **C. GPU core** — **BUILT 2026-10-08/09, the default for fills** (§15.12–15.16; `backend:"auto"`; open: batching of awake volumes, the 2 M-cell tier ceiling, FLIP on the GPU)** | Same solver on compute (`water_core_*.comp`), ping-pong, no readback except the surface/queries; parity with CPU on S1–S5 within tolerance; perf rows | /design-check (dispatch, buffers, tiers) | parity + ≤ 2 ms at `high` with all §3 rigs awake |
| **D. Rest, persistence, world data** — **design §16; D3 + D1 + D2 + D4 + D5 BUILT 2026-10-09 (ledger §16.9; D4 halves the River trunk defect, residual is the bake's cell resolution)** | AV sleep/write-back to spans and body records; `WaterBodyTable`; edits-never-create-water; span-grid rebuild on residency set (WP1 step 6); hydraulic flood (WP1 step 1b, gated READY); river spans (step 1) | /design-check on §16 | S11; camera-walk probe 0 violations on all benches; River trunk rect 17,677 → 0 |
| **E. Coupling** — **MOSTLY BUILT; small-pond feel SIGNED OFF 2026-10-09 (§19.8).** E1 blasts + E2 debris both ways (§19.2–19.3); moving solids M0–M2 (§20.12–20.14, not signed off); clear water S0 (§21.10) and the droplet pool (§21.12); ripples R0+R1 (§22.9, accepted) + the black/white fix B1/B2 (§22.12, signed off 2026-10-11); waves keep their energy, pond friction, stone drag, the level-jerk fix (§24, not signed off). Open: M3/M4, E4 wading, pond whitewater, containers/pipes/scoop (§0a)** | Moving solids (debris, furniture, characters) two-way; impulses; pump/pipe/scoop/pour/containers | /design-check | S6, S7, S8, S13, S14; drag/buoyancy tables retired on measured parity |
| **F. Rendering the core** — **design §17; F1 + F2 first pass BUILT 2026-10-09 (§17.3), sign-off pending; F3 deletions open** (droplets were built under E, §21; the old RippleField's replacement is `RippleLayer`, §22)** | AV surface mesh + shading; droplets; `RippleField` and cell renderer deleted; flat-sea/bake placement deleted (D5) | /design-check (aesthetic + camera invariant) | Look sign-off on S6/S7/S9 rest states vs refs; probe clean |
| **G. Large bodies on top** — **PAUSED 2026-10-09 by the owner: small bodies first (E, then F finish, then B's open gates).** Design §18; G1–G4 BUILT 2026-10-09 (§18.5–18.8: column solver + band, solver foam/flow, per-body look profile; G4 fine ⅓ m nested band built, OFF by default — the Coast's sea-level shelf, not resolution, is what stops a surf line (§18.8); next: a sloped-beach rig)** | Shoreline AV band with swell (S12); river reaches; far tiles; weather driver; tall-cell compression | /design-check | S12; WaterRethink WP2/WP6 gates |
| **H. The world water system** — **DESIGN §25 (2026-10-11), H0 MEASURED (§25.16-25.17); H1 DESIGNED + checked, READY for (a)/(b) (§25.18); creek beds in 1/3 m subcubes (§25.19); bed option (a) CHOSEN, others recorded (§25.21); H1c designed + checked (§25.22)** | Sources and sinks with a ledger; three tiers (3-D windows, columns, far network) of one water; paths and currents simulated from the ground as it is; settling by Priority-Flood + Fill-Spill-Merge; underground drainage limit and water table; floating items in every tier | /design-check per step (H0 measurement first) | WS-1..WS-11 (§25.12); owner's look in motion |

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

### 22.10 The black checkerboard (owner, 2026-10-10) - the layer was unstable under real frame times; fixed. Two defects open

**Seen:** the owner's screenshot (`Downloads/blackspots.png`) - the whole Small pond a checkerboard of black and white
1/9 m facets; five 1/3 m stones (`five_stones.mp4`) - black/white facet rings from every entry; ripples off
(`five_stones_noripple.mp4`) - none. Live, the layer read **0.8-1.4 m** with no body, droplet or impulse feeding it.

**Cause 1 (fixed): instability under uneven frames.** The centred leapfrog stores the previous height; `step(dt)` used
the frame's dt as its step, so every change of frame time rescaled the implied velocity. `UnevenFramesStayBounded`
(R-T7, red first): even 1/60 s steps decay (0.9 mm 60 s after five kicks, walls or not); jittered 4-25 ms frames grew
without bound (1.7e6 m open water, 2.9e6 m walled). Every earlier test stepped evenly. **Fix:** the layer steps a
FIXED kStep = 1/120 s, accumulating frame time (a stall beyond 4 steps is dropped). After: jittered = even (0.9 /
1.1 mm). Live five stones: 0.62 m at entry, then down to 0.06-0.08 m by 28 s (it grew to 1.4 m before).

**Still open (not fixed - next):**
2. **The body forcing has no bound.** Five stones crossing the surface put 0.57-0.62 m into the layer in a frame (the
   high-pass of a 7 m/s entry, 22.3.2); on a 1/9 m facet that is a slope of several to one - black where the facet
   faces away (the reflected ray points below the horizon), white where it catches the sun.
3. **No breaking limit.** Real water holds no wave steeper than ~1/7 (height / length; Stokes) - it breaks. The linear
   layer has no such limit, and its film damping (0.047 /s) keeps an oversized field for tens of seconds.
4. **The shader has no guard for a facet tilted away from the eye** - whatever the layer holds, water should never
   show the ground below the horizon.

### 22.11 When a facet renders black or white - measured (owner, 2026-10-11)

The owner asked for the exact trigger of the black / white squares. Tool: `tools/water_facet_extremes.py` (Small
bench). Two measurement hooks were added, both off unless asked for: `water_ripples {pattern: {slope, azimuth_deg}}`
pins every ripple layer to ONE uniform facet tilt (no stepping), and `water_render_core {debug: 8}` draws, per pixel,
the shader's reflected ray (R = angle to the sun / 180, G = elevation; read with tonemap curve 0, exposure 1, bloom
off; the frame is sRGB - the tap's 0.5 reads 188). `water_ripples` also reports `max_slope` and `slope_hist`: water
cells by facet slope, the shader's own central difference. Black = every channel below 30 / 255 in the owner's
frame (exposure x8 + AgX); white = every channel above 225.

**What (per pixel, 4 camera pitches -12 / -24 / -38 / -62 deg x 8 tilt directions x 11 slopes 0.1-2,
`facet_extremes_pixels.json`):**
- **White = the sun glint.** A pixel is white when its reflected ray points within ~11 deg of the sun: 100 % at
  <= 8 deg, 83 % at 10, 53 % at 11, 13 % at 12, none past 14. That is the `pow(N.H, 220)` glint (x 0.8 sun, x 8
  exposure) saturating; a FLAT 1/9 m facet carries it whole, so the glint is a square, not a sparkle.
- **Black = the reflection below the horizon.** Above the horizon black is <= 0.2 % of pixels at any tilt; below it
  black grows with depth: 6 % at -2 deg, 13 % at -10, 23 % at -20, 49 % at -30, 68 % at -40. The sky model has
  nothing below the horizon but the short near-ground scatter, and at grazing view the Fresnel term sends nearly
  all of the pixel to that reflection.
- In facet terms (the reflected elevation is the view elevation minus twice the tilt away from the eye): the
  demo camera sees the pond at ~30 deg, so a facet tilted ~15 deg away (slope 0.27) already reflects below the
  horizon; seen from the low pose (pond at ~10 deg), 5 deg (slope 0.1) does. A sideways tilt needs 45 deg or more.

**When (five 1/3 m stones, demo camera, droplets off, `facet_extremes.json` B):** black peaks at 6-8 % of pond
pixels 0.5-1.5 s after entry and is still 3 % at 11 s; white peaks at 0.5 % (this sun is behind-left of the camera).
Control (ripples off, same drop): 0.4 % black, 0 % white throughout - the squares are all the ripple layer. The
cause is the field's roughness, not just the entry: 10 s after the drop **66 % of water cells are steeper than 0.3
and 8 % steeper than 0.5** (max |r| 0.11-0.14 m). Five pebbles on a real pond leave slopes of a few hundredths.

**So the levers, in order of effect:** (1) the field is far too rough for far too long - the body forcing puts
0.5-0.6 m in (22.10 item 2), nothing limits steepness (item 3), and 0.047 /s film damping keeps it; (2) the shader
returns near-black for a reflection below the horizon, where real water shows the far bank or the next wave
(item 4); (3) the glint is a whole flat facet. Status: measured; no fix built.

### 22.12 Fewer black and white squares - DESIGN (owner, 2026-10-11: "lets try it out")

Two of 22.11's levers, both physical. The glint (22.11 lever 3) is NOT in this round: white is <= 0.5 % live.

**B1 - the breaking limit (22.10 items 2 + 3).** Water holds no wave steeper than the Stokes limit, H/L = 0.142
(Michell 1893); steeper, it breaks and the excess leaves the wave (as whitewater and turbulence). As a slope that
is s_b = pi x 0.142 = **0.44** (the sinusoid's steepest slope at that height; the limiting crest itself is 30 deg,
0.58, over a span far below the lattice). After every layer step, both leapfrog levels (r and the previous r - the
breaking crest loses its height AND the motion that carries it) are limited so that no lattice edge is steeper than
s_b: axis neighbours |r_i - r_j| <= s_b d, diagonal neighbours <= s_b d sqrt 2 - with the diagonals the bound on the
facet gradient holds in every direction within 8 % (sec 22.5 deg), so a diagonal ring breaks like an axis one.
- **How:** Jacobi passes over a snapshot: each over-steep edge moves both cells toward each other by its excess / 2,
  weighted 1/8 (a cell has 8 edges, so no pass overshoots), repeated until no edge exceeds s_b by more than 1e-4 m
  or 16 passes. Pairwise and equal-and-opposite: the sum of r over each body of water is EXACT (the zero-mean rule
  is untouched); edges across a mask (wall, dry) or between different bodies are skipped. Order-independent (a
  snapshot per pass). A field already gentler than s_b is untouched bit for bit - one scan, no pass.
- **What it replaces:** nothing. The forcing keeps its physics (22.3); the limit bounds what any source leaves -
  a 7 m/s stone entry DOES break the surface. The film damping (0.047 /s) is unchanged.
- **Ledger:** the height removed per step (sum |moved| x d^2, m^3) is reported as `broken_m3` - the hook for the
  whitewater pass (S4), not wired to it now.
- **What it cannot do:** from the demo camera a facet goes black at slope ~0.27 and from the low pose at ~0.1
  (22.11) - below s_b. B1 alone shrinks the black (less of the pond that steep, for less time); B2 removes it.

**B2 - below the horizon, the ray meets the water again.** A ray reflected downward from a facet tilted away from
the eye does not reach the ground: it hits the next stretch of water. Model that stretch as the mean surface (flat,
+Y): the ray reflects once more, R2 = (R.x, -R.y, R.z), with Schlick reflectance F2 = 0.02 + 0.98 (1 - |R.y|)^5 at
that incidence, and the rest (1 - F2) enters the water and returns its body colour:
    refl = F2 x sky(R2) + (1 - F2) x body        (R.y < 0)
Continuous at the horizon (R.y -> 0: F2 -> 1 and sky(R2) -> the sky at the horizon - today it JUMPS there to the
near-ground scatter); never brighter than the sky above it. Only WaterCore's surface (`water_surface.frag`) sets
the new `horizonGuard` input; the old water, the cell renderer and the sea keep their look bit for bit (small
bodies first - no sea work). SSR, off on this path, is unchanged.

**API:** `water_ripples {break_slope}` - 0 = off (the A/B control), else clamped 0.05..5 (below 0.05 would flatten
every ring the droplets make; above 5 is no limit) - echoed with `broken_m3`. `water_render_core {horizon_guard:
bool}` - default on; off = the A/B control; echoed.

**Tests (red first; predictions written now):**

| # | Layer | Test | Prediction | Control |
|---|---|---|---|---|
| B-T1 | unit | a field with one 0.6 m spike and a 0.3 m step | after one step no edge exceeds s_b (+1e-4 m); the sum over the body unchanged to 1e-6 relative | `break_slope 0`: the edges stay steep |
| B-T2 | unit | the existing ripple tests (dispersion, ring train, reflection, sleep, seam, uneven frames) | all pass unchanged: their fields are gentler than s_b, untouched bit for bit | - |
| B-T3 | unit | a wall between two bodies, one steep | the other body is not touched (no edge across the mask) | - |
| B-L1 | live | `water_facet_extremes.py --part P` (tilt sweep, 4 poses) | black where the reflection is below the horizon: from 6-68 % to <= 0.5 % (the above-horizon level); the guarded pixels no brighter than the sky band above them | `horizon_guard false`: 22.11's numbers |
| B-L2 | live | `--part B` (five stones, demo camera) | black <= 1 % at every sample (from 6-8 %); `max_slope` <= 0.48 (s_b x 1.08); water cells steeper than 0.3 at 10 s below 10 % (from 66 %) | both off: 22.11's numbers |
| B-L3 | live | clip, five stones, demo + low pose -> **owner's look** | rings still visible (pond change after entry >= 22.9's 1.52 grey levels/frame) | - |

The rig is the shipped defaults (Small bench, 22.11's poses); B-L1 pins the layer (`pattern`), so B1 does not act
there - B-L1 measures B2 alone, B-L2 both. Cost: one scan per step while awake (54 x 54 cells); passes only where
something breaks. Status: signed off (22.12.2).

**22.12.1 Design-check (2026-10-11) - round 1 NEEDS WORK, folded in, round 2 READY for B1 + B2.** Checked against
`WaterRippleTest`, the `water_ripples` / `water_render_core` routes and `water_surface.frag`'s push constants:
- **F1** B-T2 claimed the existing tests are "gentler than s_b, untouched" - not measured: `AKickSpreadsIntoARingTrain`
  kicks 50 and `UnevenFramesStayBounded` 200 on one cell. Worse, the second exists to catch the stepping instability,
  and a slope limit would hide that instability (a growing checkerboard breaks forever, bounded). -> The tests that
  measure LINEAR propagation or the stepping's stability (dispersion, plane wave, ring train, reflection,
  uneven frames) run with the limit OFF, said in each; the sleep and seam tests run it on. Each test prints its peak
  facet slope so the split is measured, not assumed.
- **F2** the guard's switch needs a push-constant slot: `look1.z` is unused (0 in the neutral `WaterLookPacked`) ->
  `look1.z = 1` means guard OFF, so 0 - every existing pack - is ON with no other change. `water.frag` and
  `water_cell.frag` set `inp.horizonGuard = 0` (a struct member left unset is undefined in GLSL).
- **F3** the new code acts across lattice neighbours, so R-T6's seam test (a gentle ring) does not cover it -> **B-T4**:
  a steep spike (the limit acting) at a chunk-seam and an interior position, bit-equal after translation.
- **F4** where in the step: after the zero-mean pass, on the new r and on the level becoming the previous one,
  before the swap - so the next step starts from a limited field at both levels.
Round 2: voxel look (the 1/9 m facets unchanged, the limit is unconditional with a default, the A/B switch is a
measurement control); chunks (world-aligned lattice, neighbours only, no chunk input; R-T6 + B-T4); generation (none
- runtime); API (units: slope = height / run, dimensionless; m^3; omitted = unchanged; echoed; clamps written at
the route with the failure they prevent; defaults: limit on at 0.44 and guard on - new, no earlier pin); tests (red
rows with predictions and controls on the Small bench, one pond inside one chunk, rig = shipped defaults; B-L1 pins
the layer so measures B2 alone). **READY for B1 + B2.**

**22.12.2 B1 + B2 built (2026-10-11) - ledger. Status: SIGNED OFF by the owner 2026-10-11 ("new ripples look really great").** `RippleLayer::limitSlope` (B1, default
0.44, `water_ripples {break_slope}`), the `horizonGuard` branch in `shadeWaterSurface` (B2, `water_surface.frag` only,
`water_render_core {horizon_guard}`). `tools/water_facet_extremes.py --fix on|off` runs the A/B on one binary.

Unit (WaterRippleTest, 11, all pass; the 62 water-core / droplet / coupling / ripple tests pass): B-T1 - every edge
within the limit after 0.1 s (worst 6e-4 m UNDER), the water's sum kept (-4.3e-6 -> 2.0e-6 m over 2916 cells);
**its control missed my guess**: without the limit the worst edge is 0.061 m past it (slope ~1.0), not > 0.1 m -
still 2x the limit, the threshold is now > 0.01 m. B-T3 the other body exactly 0; B-T4 bit-equal across the seam.
F1 measured: the existing tests peak at slope 0.066 (ring train), 0.008 (plane wave), 0.006 (droplet) - the limit
would not have touched them; the linear and stability tests keep it off anyway.

**B-L1 (tilt sweep, B2 alone, 4 poses x 8 directions x 11 slopes; `facet_extremes_pixels_fix_{on,off}.json`):**

| reflected elevation | black, off | black, on | luma, off | luma, on |
|---|---|---|---|---|
| -60 .. -36 deg | 76.5 % | 2.3 % | 17.6 | 74.3 |
| -35 .. -26 | 49.2 % | 2.3 % | 35.3 | 70.1 |
| -25 .. -16 | 25.6 % | 1.5 % | 52.5 | 74.4 |
| -15 .. -6 | 12.5 % | 0.4 % | 63.1 | 80.9 |
| -5 .. -1 | 7.8 % | 0.2 % | 68.3 | 88.6 |
| 0 .. 4 (above, unchanged) | 0.2 % | 0.1 % | 88.6 | 89.0 |

The jump at the horizon is gone (88.6 -> 89.0) and no guarded band is brighter than the sky above it. **Prediction
<= 0.5 % MISSED below -16 deg** (1.5-2.3 %). The same sweep holds 1.5 % black at reflected elevations 41-90 deg with
the guard on or off, so a second, smaller cause sits outside B2 - not investigated (22.11's "<= 0.2 % above the
horizon" held only near the horizon). Glint unchanged (white at <= 8 deg from the sun 99.5-99.9 %).

**B-L2 (five 1/3 m stones, demo camera, droplets off; `facet_extremes_fix_{on,off}.json`):** black at most 0.91 %
(ripples-off control on the same binary 1.08 %; fix off 8.0 %, as 22.11) - the ripples add no black now; white 0 %
(fix off 0.56 %). Water cells steeper than 0.3 at 10 s: **0 %** (fix off 45 %). **max_slope 0.523 MISSED** the
predicted 0.48: the edge bound holds per edge, but the shader's central difference across a curved field can reach
s sqrt 2 = 0.62 - the 8 % figure is true for plane waves only.

**B-L3 (rings still there):** pond change per frame, ripples on vs off, fix on: 3.4 vs 1.7 (1-2 s), 2.7 vs 0.3
(2-3 s), 2.0 vs 0.1 (5-10 s) grey levels/frame - above 22.9's 1.52 bar; fix off 8-10 (the flicker of the squares).
Clip for the owner: `facet_fix_before_after.mp4` (left before, right after, same drop, same pose).

## 23. Other liquids - a slime experiment (owner, 2026-10-11) - PLAN, not designed in detail, not built

The owner: "obviously water has its defaults to get that specific look and feel, but we can presumably create
slimes of various viscosity and thickness". An EXPERIMENT, after 22.12 is signed off: can the same core carry a
liquid whose look and feel come from a few physical numbers? Water stays the default and does not change.

**What makes a slime a slime** (each a property, each with the water value it replaces):
- **Viscosity nu** (kinematic, m^2/s): water 1.0e-6; olive oil ~1e-4; honey ~1e-2 - 1e-1 (temperature-dependent);
  a slime ~1e-2 - 1. The solver is inviscid today (4.2: water's viscosity is negligible at 1/3 m) - this is the new
  physics: a viscous diffusion of velocity each substep.
- **Yield stress tau_y** (Pa): water 0. Slimes, mud and toothpaste are Bingham-like - below tau_y they do not flow,
  which is why a slime heap STANDS and a slime film stops on a slope: it stops at depth h = tau_y / (rho g sin theta).
- **Density rho**: water 1000 kg/m^3; slimes ~1000-1300 (what a stone floats on, 19/20's coupling).
- **Surface tension sigma**: water 0.0728 N/m; matters for droplets and the ripple layer's capillary term only.
- **Look**: colour, clarity (Secchi depth) and gloss - the per-body profile already carries tint, clarity and
  turbidity (G3); slime adds nothing new to the shader in v1.

**What each property already reaches** (why this is an experiment, not a rewrite):
- the ripple layer: its damping becomes the liquid's - 2 nu k^2 at nu = 1e-2 is ~16 /s at 22 cm: a viscous pond
  shows rings for a fraction of a second, which is the right look and needs no new code beyond reading nu;
- the droplets (21): a viscous liquid barely splashes - the Ohnesorge number Oh = mu / sqrt(rho sigma L) decides
  whether a crown breaks into drops; high Oh -> no droplets (a gate, not a new model);
- the coupling (19, 20): a stone sinks slowly through honey - the drag the grid already applies grows with the
  viscous stress the new term carries.

**Phases (each red-before-green, each its own design-check before building):**
- **L0 - a liquid table.** `LiquidProperties {rho, nu, tau_y, sigma, look}` per active volume; water is the row
  holding today's numbers, and with it every solver output is bit-identical (the gate: the existing water tests and
  benches unchanged). API: `water_liquid {volume, preset | properties}` (echoed; presets `water`, `oil`, `honey`,
  `slime`). One liquid per volume in v1 - no mixing of two liquids.
- **L1 - viscosity.** Explicit velocity diffusion with stability substeps (dt <= h^2 / (6 nu): 0.19 s at nu = 0.1,
  h = 1/3 m; 2 ms at nu = 1, h = 1/9 m) and a stress-free free surface; refused (said so) above the cost bound;
  the implicit solve (Batty & Bridson 2008, *Accurate viscous free surfaces*) only if the explicit cost fails.
  Tests: a shear wave decays at nu k^2 within 5 %; a viscous slump spreads like Huppert's gravity current (1982:
  radius ~ t^(1/8)) where a water column dam-breaks.
- **L2 - yield stress.** Bingham (regularised): a slime heap stands at h = tau_y / (rho g sin theta) on a slope
  within 10 %; a water control flows flat.
- **L3 - the look and the feel, live.** A small slime pool beside the Small pond on one bench: a stone dropped in
  each, a pour onto a slope, a heap pushed; recordings -> **owner's look**. The ripple and droplet gates read nu.

**Owner, 2026-10-11:** "by thickness i mean slow flowing and holding shape" - BOTH: viscosity (L1) and yield
stress (L2) are the first experiment, not a later one. Order: 24 first, then L0.

**The far end of the scale - a gelatinous cube (owner, 2026-10-11: "in a perfect world our system would scale all
the way into being to make a gelatinous cube (or at least get the look right)").** Honest placement: past the
yield stress, a gel is an ELASTIC solid - it springs back and wobbles, it does not just stop. That is a third
property, a shear modulus G (gelatin ~1-10 kPa), which a liquid solver does not carry. Two routes, neither chosen:
- **L4a, the look first:** a dynamic voxel body (it already moves, collides and floats) drawn with this liquid's
  body optics - translucent, tinted, things suspended inside it seen through refraction - and a jiggle from shape
  matching (Mueller et al. 2005, *Meshless deformations based on shape matching*) on its voxels. Cheap, fits the
  dynamic-voxel key, gets the look; the cube does not flow or split.
- **L4b, the physics:** an elastic-plastic material in the grid (stress carried per cell, as MPM does - Stomakhin
  et al. 2013's snow is the reference shape). It would flow, split, heal and wobble, but it is a new solver stage,
  costly per cell, and not to be started before L1-L3 have shown what the liquid path can carry.
L1-L3 come first either way; they decide whether L4 is a shader-and-body job or a solver one.

**Open before L0's design:** CPU first, the GPU backend's parity after (D-T5 pattern); whether the yield surface
needs the implicit solve.

## 24. Water that is not thick, stones that slow down (owner, 2026-10-10) - DESIGN, 2026-10-11

The owner (2026-10-10): "1. stone seems to sink too fast and not slowed by the water. 2. water surface deforms a
little slow making it look a thicker than water". Measured then (`c776889e`, `tools/water_entry_probe.py`,
`WaterWaveTest`): the 1/3 m stone's descent is within ~10 % of a quadratic-drag reference, but debris water drag
is one linear rate (0.9 /s) for every size, so a 4 cm pebble would fall at ~2.7 m/s where it should settle at
~1 m/s; and the pond's waves lose most of their energy per swing, worst with the rest level half-way up a cell.
The owner chose (2026-10-11) this first, then slime (23).

### 24.1 Where a wave's energy goes - measured (2026-10-11)

`WaterWaveTest.DiagWaveLossBisect`: the 20.8 pond (4 x 4 m, 1.5 m deep, 1/3 m cells, CPU), a 4 m standing wave of
5 cm; the whole surface projected onto cos(k x) (the wave's own amplitude - ripples do not count), envelope per
theoretical period, amplitude kept per period over 4 periods. Real water keeps ~100 % (viscous decay ~1e-5 /s).

| | rest level 0.25 of the top cell | 0.5 | 0.75 |
|---|---|---|---|
| shipped | 0.90 | 0.28 | 0.74 |
| liquidThreshold 0.3 / 0.7 | 0.34 / 0.93 | 0.85 / 0.89 | 0.77 / 0.58 |
| thetaMin 0.1 -> 0.01 | 0.92 | 0.61 | 0.76 |
| + compaction off | 0.92 | 0.91 | 0.84 |
| + rest damping off | **0.99** | **0.95** | **0.96** |
| all optional stages off, thetaMin 0.1 | 0.98 | 0.61 | 0.96 |

Three drains, each named:
1. **The ghost-fluid clamp.** The surface sits theta = f - 0.5 (+ the fill above) of a cell over a liquid cell's
   centre, clamped to >= `thetaMin` = 0.1. As a cell crosses the 0.5 cutoff, the true theta passes through 0, but
   the solver sees >= 0.1: the surface it projects against jumps by a tenth of a cell at every crossing - the
   worst case is the rest level sitting on the cutoff (the table: the bad level follows the threshold). Unclamped,
   the two sides of the cutoff agree (theta -> 0 above it, 1 - epsilon from the cell below).
2. **The transport smears water upward; compaction drags it back.** `DiagFilmAboveTheWave`: with the top cell
   0.54-0.64 full and a 5 cm wave (it never reaches the cell above), the cell above holds up to 0.14. The donor-
   cell flux moves `sweep x f_donor` up through a cell's ceiling whatever its fill - water leaves the top of a
   half-full cell whose surface is flat in its middle. `compactSubmergedPartials` (closing real voids, 15.x S3)
   then pulls that film back down at every turning point of the wave, where the face is quiet. The round trip
   lifts water and drops it: energy lost per swing.
3. **Rest damping acts on visible waves.** `restDamping` (1 /s) applies whenever the volume's specific kinetic
   energy is under `keSettle` = 1e-3 m^2/s^2 (~3 cm/s rms) - a 5 cm wave in this pond sits in that band, so it is
   braked as if it were settling.

### 24.2 The fix (W1-W3), CPU and GPU together

- **W1 - donor-acceptor upward flux** (Hirt & Nichols 1981, *Volume of fluid (VOF) method for the dynamics of free
  boundaries*, J. Comp. Phys. 39: the donor-acceptor rule for an interface normal to the flux). Upward through a
  cell's ceiling, a PARTIAL donor (0 < f < 1) whose ceiling neighbour is emptier first fills itself:
  moved = clamp(sweep - (1 - f_donor), 0, f_donor) - the surface rises inside the cell and crosses only once the
  cell is full. Lateral and downward fluxes unchanged (they carry the dam-break, weir and drain fronts the S-rows
  pin). CPU `EulerianTransport::advect` `moveFace`, GPU `wc_fill_advect.comp`.
- **W2 - the clamp to 0.01** (`thetaMin` 0.1 -> 0.01; the GPU's two hardcoded 0.1s in `wc_classify.comp` and
  `wc_face_ops.comp` read the parameter). 1/theta <= 100 on a row's diagonal; PCG iterations reported before and
  after (StepReport), the GPU's RBGS residual too. **Grounding auditor at build:** the 0.01 floor is the value the
  ghost-fluid literature clamps to (Gibou et al. 2002; Bridson 2015 s5) - to be confirmed, not assumed.
- **W3 - rest damping only below what the eye sees.** `keSettle` drops from 1e-3 to the specific kinetic energy of
  a 2 mm wave in the test pond (measured in the test, then written as the constant with its derivation) - a 2 mm
  wave on 1/3 m cells is below a grey level in the frame (22.9). Above it, the water keeps its energy; below it,
  the settle damping still brings the volume to sleep (S11).
- **W4 (only if 24.4 shows it is still needed)** - compaction gated to real voids: a partial cell under a LIQUID
  cell (f_above >= the threshold), not under a thin film. Built only if W1 leaves compaction costing > 5 % per
  period; the S3 two-half-layers case (15.x) is its red test.

**Not changed:** the 0.5 cutoff itself (the ghost-fluid choice, 15.x: a 0.999 cutoff stalled the dam-break front)
- with W1 and W2 the crossing is continuous, so it no longer needs to be gradual, which was 22.10's first guess.

**What sleep then costs.** With the numerical losses gone, a pond loses energy only in the settle band. Real
small ponds still slosh for tens of seconds (laminar wall/floor layers damp a metre wave at ~1e-2 /s); the live
measurement (24.4 W-L2) reports the time from a stone's entry to sleep. If it is far longer than the owner wants,
the next lever is a stated physical wall-friction term - not a return of the blanket damping. Logged, not built.

### 24.3 Pebbles - drag that depends on size (P1)

19.9's design (a), measured unnecessary for the 1/3 m stone and never built, is right for small ones: form drag
k = Cd A f b / (2 V) per metre (Cd 1.05 blunt cube, A the projected area, f the wet fraction, b = rho_w / rho_body),
applied as the exact quadratic decay v / (1 + k |v| dt) (never reverses, any dt), ADDED to the linear 0.9 /s (at
bob speeds ~0.1 m/s it is ~0.06 /s against 2.3 /s: the signed-off floaters (19.6) should not move). A / V = 1 / side
for a cube: a 1/27 m pebble gets 9x the 1/3 m stone's k - small stones settle slowly, big ones barely change. Same
law in `solver_integrate.comp` (GPU debris) and `VoxelDynamicsWorld` (one water law, constants in
`solver_shared.h`); the momentum reaches the water through the E2 exchange unchanged. No new API knob.

### 24.4 Tests (red first; predictions written now)

| # | Layer | Test | Prediction | Control |
|---|---|---|---|---|
| W-T1 | unit | `WaterWaveTest` standing waves, modes 1-4, rest levels 0.1 / 0.25 / 0.5 / 0.75 / 0.9 | amplitude kept per period >= 0.90 at every level and mode (shipped: 0.28-0.90) | shipped params: the 24.1 numbers |
| W-T2 | unit | the period, same runs | within 10 % of omega^2 = g k tanh(k d) | - |
| W-T3 | unit | every existing water-core test (S1-S5 rows, dam break, weir, drain, pump, rest / sleep, solids, coupling, droplets, ripples) | all pass unchanged; any that moves is reported with its numbers before anything is re-based | - |
| W-T4 | unit | the film smear: `DiagFilmAboveTheWave` | the cell above holds < 0.01 (from 0.14) | W1 off |
| W-T5 | unit | PCG iterations, the 20.8 pond and the S3 dam break | reported; <= 2x shipped | - |
| P-T1 | unit | terminal velocity in still water of a stone cube of side 1/3, 1/9, 1/27 m | within 15 % of the quadratic reference (Cd 1.05, buoyancy, linear 0.9 /s) at every size | shipped: one linear law, the small one ~2-3x fast |
| W-L1 | live | GPU, Small bench: a sloshed pond (the 20.8 shape) - the surface tap over time | kept per period >= 0.85 (GPU) | `thetaMin 0.1` + W1 off |
| W-L2 | live | five 1/3 m stones, the demo camera | time from entry to sleep reported; ripples (22) unchanged | shipped |
| W-L3 | live | clips: stones, a 1/9 m pebble beside a 1/3 m stone, the pond sloshing -> **owner's look** | - | - |

The rig is the shipped Small bench (one pond, one chunk); the unit rig is the 20.8 pond at 1/3 m cells. No new API:
`thetaMin` and `keSettle` are solver params (the bisection sets them directly); W1 is unconditional (a debug stage
bit, 128, turns it off for the A/B). Status: designed, not built.

### 24.5 Design-check (2026-10-11) - round 1 NEEDS WORK, folded in, round 2 READY

Checked against `EulerianTransport::advect`, `WaterSolver::thetaToAir` / `project`, `wc_fill_advect.comp`,
`wc_classify.comp`, `wc_face_ops.comp` and `WaterCoreGpu.cpp`'s settle band:
- **F1** W1 as written would also stop a THIN sheet climbing a wall or a jet's partial cells from rising - their
  water is not a flat layer on the cell's floor (S3 run-up, the splash crown on the grid). -> W1 applies only where
  the donor sits on a FULL cell (f + s >= 0.999) or on solid: there the interface is the column's top surface.
  Everywhere else the donor-cell flux stands.
- **F2 (a GPU defect, found reading the kernels)** the GPU's velocity update and its pressure matrix use DIFFERENT
  surface distances: `wc_classify.comp` (matrix) has the CPU's continuous f + s - 0.5 + f_above in [0.1, 1.5];
  `wc_face_ops.comp` (face update) still has the pre-#28 form - 0.5 + f_above whenever there is water above, else
  f + s - 0.5 in [0.1, 0.5]. The CPU's own comment records what that mismatch does ("still water kept g dt of
  velocity, hydrostatic pressure carried a +4.5 Pa offset"). The live pond runs on the GPU, so it carries a drain
  the CPU tests cannot see. -> W2 puts ONE `wcThetaToAir` in a shared include used by both kernels, the CPU's
  expression with `thetaMin` from the push block; W-L1 measures the GPU with the shipped kernels as its control.
- **F3** W3's threshold is a volume-wide specific kinetic energy, so the same surface wave reads smaller in a deeper
  pond. -> stated: derived for the Small bench's depth (1.5 m); a deeper pond enters the settle band at a smaller
  wave (it keeps its energy longer - the safe direction), a shallower one at a larger. Per-surface settling is a
  later refinement, logged.
- **F4** W-T3 runs on the CPU only (D-T5, GPU parity, is still unwritten). -> W-L1 / W-L2 are the GPU's evidence;
  the GPU parity test stays the logged open item, not silently assumed.
Round 2: voxel look (no render change - the water's own surface moves more); chunks (per active volume, no chunk
input, unchanged by this); generation (none); API (no new knobs: `thetaMin`, `keSettle` are solver params, W1 has
the stage bit 128 for the A/B; defaults change - `thetaMin` 0.1 -> 0.01, `keSettle` 1e-3 -> the derived value -
with the tests that pin them updated in the same commit, named); tests (red rows with predictions and controls,
the 20.8 pond for units, the Small bench live, rig = shipped). **READY for W1-W3 and P1.**

### 24.6 Build round 1 (2026-10-11) - W1 REJECTED by measurement, W4 amended, W2 + W3 built; open: how long a pond moves

**W1 (donor-acceptor upward flux) slowed every wave** - measured, then removed: with it a 8 m wave swung at 1.38x its
period (3.48 s vs 2.52) where the clamp change alone gave 1.12x; the fill no longer followed the velocity the
projection gave the ceiling face. The film it targeted is harmless once compaction stops dragging it back (below).
**W4 amended:** compaction runs only in the SETTLE band (`settleBefore` on the CPU, `quietBefore` on the GPU), like
the rest damping - it exists for a settling pool's voids (the frozen half layers), and in moving water it pulled the
film down at every turning point. **W2 built:** `thetaMin` 0.1 -> 0.01; the GPU's matrix and velocity update now
share ONE `wcThetaToAir` (`shaders/wc_theta.glsl`, the CPU's expression; `WcPush.thetaMin`, 52 bytes) - F2's
mismatch is gone. **W3 built:** `keSettle` 1e-3 -> 2e-4 (`WaterWaveTest.WavesKeepTheirEnergy` prints the floor: a
still pond 1e-16; 2-5 mm waves ~1e-4, 6-10x their linear energy - logged, not chased).

**W-T1/W-T2 (CPU, the 20.8 pond, 5 cm waves, modes 1-4 x rest levels 0.1-0.9):** kept per period 0.90-1.11 for modes
1-3 everywhere except mode 3 at 0.9 full (0.897); periods within 2-10 % of theory for modes 1-3. **Missed for mode 4**
(2 m = 6 cells, the shortest wave the grid carries; the ripple layer holds what is shorter): 0.83-0.97 kept, 10-14 %
slow. Before: 0.28-0.90 kept. The test's bounds for modes 3-4 are the measurements, said so in the test.

**W-T3 - what else moved (CPU, 282 water tests):** passed 277; changed, attributed by A/B builds:
- `WaterCouplingTest.BlastBesideThePondMovesItAndItSettles` (S8): energy after 30 s is 0.42 of the 1 s value (pin:
  < 0.2) and the late max 0.52 (pin < 0.33). Only with BOTH changes - the water keeps its energy. The pin encoded the
  old drains; the test's own comment says real wall friction damps such a slosh "over minutes". **Open - owner.**
- `WaterSolidTest.SpawnInsideWaterIsLedgerExact` / `DrawnWaterlineBesideAFloatingBody`: read the level 5 s after a
  1 m cube enters, when the old pond had stopped. Now they wait for sleep (<= 60 s) - and **neither pond sleeps
  within 60 s**. The spawned cube's level is then right (1.5615 vs 1.5625, PASS); beside the floating block the
  surface is still 5.8 cm off the level away (pin 3 cm) - still sloshing, or a waterline defect the old damping hid:
  not yet told apart. **Open.**
- `WaterSolidTest.TheCavityCloses`: its CONTROL (escape rule off) now holds 0.0018 m^3 of trapped air (was > 10 % of the
  stone); the rule's run holds 0.0000. Control re-based to "clearly more air without the rule", reason in the test.

**W-L2 (live, GPU, Small bench, five 1/3 m stones, demo pond resting half-way up a cell):** kinetic energy 0.39 at
1 s -> 0.042 at 10 s -> 0.016 at 28 s; frame change on the pond 15-25 % above the pre-24 clip at the same times
(2.0 vs 1.7 grey levels/frame at 2-4 s, 1.57 vs 1.27 at 8-10 s), still 0.79 at 20-28 s. GPU solver 0.6-2.3 ms/call.
`tools/water_slosh_live.py`; `slosh_live_after24.{json,mp4}`, `slosh_before_after.mp4` (12 s, same drop and pose).

**The question for the owner:** with the solver's own losses gone, nothing in the water dissipates a slosh except the
settle band at the very end, and a smooth-walled box pond is a laboratory tank: laminar wall and floor friction damp
a 4 m slosh by ~0.1-1 % of its amplitude per second, so by physics it moves for minutes. Real garden ponds settle
sooner (irregular banks, plants, films). Either keep that (S8 / S11 re-based: a disturbed pond stays awake - and
costs GPU - for minutes), or add a stated pond-friction rate (a slosh halves its amplitude every N seconds) as the
stand-in for what a voxel box lacks, with N the owner's call. P1 (pebble drag) not built yet.

### 24.7 Pond friction (owner, 2026-10-11: "option 2 for sure") - DESIGN + design-check

**What:** one stated stand-in for the dissipation a smooth voxel box lacks (irregular banks, plants, surface films,
rough beds): every substep, all of a volume's face velocities are scaled by (1 - a ds), a = ln 2 / N, so an undriven
slosh halves its height every N seconds. **N = 5 s** (my recommendation, the owner's option; to be tuned with the
owner on the clip). It acts on all motion, unlike the settle band - and it does not touch mass (velocities only).

**Probed before designing (CPU, a = ln 2 / 5 = 0.139 /s, all 282 water tests):** every flow row still passes - dam
break fronts, weir discharge, Torricelli drain, pumps, run-up - a 0.14 /s drag is small against flows that cross a
cell in a fraction of a second. The blast slosh (S8) is flat in 26 s and its energy is 1e-4 of the first second's
by 30 s; the spawned cube's pond sleeps in 34 s at the exact level (1.5625); beside the floating block the surface is
0.0 cm off the level away - 24.6's 5.8 cm was slosh, not a waterline defect.

**Its costs, stated:** S8's near-side drop is 0.293 m against the spec's >= 0.3 (it was 0.300 before: friction takes
2 % of the first second) - the S8 test's bound becomes the measurement, said in the test; a run 7x longer (until sleep)
accumulates 1.6e-6 m^3 of float rounding on 24 m^3 - the mass tolerance becomes relative (1e-7), said in the test.

**API:** `water_pond_friction {half_life_s}` - every volume, now and created later; 0 = off (the A/B control), else
clamped 0.5-120 s (below 0.5 s it reads as syrup - that is slime's job, 23 - above 120 s it is the lab tank of 24.6);
echoes `half_life_s` and the rate `a`. Default 5 s. GPU: the existing velocity-scale mode (face ops 9) once per
substep - no new kernel.

**Tests:** W-T1/W-T2 measure the solver's own numerics with friction OFF (as 24.4 meant them); **W-T6** - with friction
on, a 5 cm mode-2 slosh's amplitude halves in 5 s +- 15 % (from the envelope); W-L2 re-run live with the clip; the S8,
spawn and waterline tests run the default.

**Design-check:** voxel look (no render change); chunks (per volume, no chunk input); generation (none; a world recipe
does not carry it in v1 - the API sets it per session, logged: persist it with the liquid table, 23 L0); API (units
s, 0 = off, clamp with reason, echoed, default pinned by W-T6); tests (red first: W-T6 fails with friction off; S8's
energy gates are the control). **READY.**

### 24.8 Pond friction built (2026-10-11) - ledger. Status: built, not signed off

**Two corrections while building, each measured:**
1. **The rate.** 24.7 wrote a = ln 2 / N; a velocity drag damps an oscillator's AMPLITUDE at a / 2, so a = 2 ln 2 / N
   (0.277 /s for 5 s). At 0.139 the slosh measured a 20-40 s half-life.
2. **Which tests run it.** At 0.277 /s five tests moved: weir discharge 17 % low, the dam-break front 2 % short, the wall
   crest 11 % low (frictionless references), and two detector rigs that need a slosh or trapped air to survive
   (droplet pockets at 30 s, the cavity control). A vertical-only drag (probed at 0.5 / 1 / 2 /s) was WORSE - it broke
   the dam break and Torricelli drain too, and its half-life swung 2.9-15 s with the rest level - so it was dropped.
   Rule adopted: tests of the solver's NUMERICS against frictionless references, and detector rigs, run with
   `pondFriction = 0` (as W-T1 does); feel and settling tests run the default. Written in each test.

**Built:** `SolverParams::pondFriction` (default 2 ln 2 / 5 s), `WaterSolver::applyPondFriction` every substep (all
velocity components; mass untouched), GPU: the velocity-scale mode (face ops 9) once per substep;
`water_pond_friction {half_life_s}` (route registered in `EngineAPIServer`; 0 = off, clamped 0.5-120 s, echoes rate,
nominal half-life and a note), applied to every volume now and to volumes created later.

**Tests (283 water tests pass):** W-T6 - the nominal 5 s **measures 6.6 / 6.7 / 9.5 s** at rest levels 0.25 / 0.5 / 0.75
(prediction 5 +- 15 % MISSED; the knob is called nominal in the API; the test pins 5-10 s and is red with friction
off). S8 blast: energy at 30 s 1e-5 of the first second's, flat (1 cm) at 16.9 s; near-side drop **0.286 m against the
spec's 0.3** (was 0.300) - bound set to the measurement, a 1.4 cm miss reported. Spawned 1 m cube: the pond sleeps in
27 s at the exact level (1.5625). Floating block: the surface beside it 0.05 cm off the level away (was 5.8 cm - 24.6's
open item was slosh, not a waterline defect); that pond does not sleep within 60 s (a floating body keeps bobbing).

**W-L2 live (GPU, Small bench, five stones, `slosh_live_friction5.{json,mp4}`):** kinetic energy 0.006 at 10 s (without
friction 0.042) and 1e-5 at 28 s; frame change on the pond nearly unchanged from no-friction (2.58 vs 2.58 at 1-2 s,
1.49 vs 1.57 at 8-10 s) - the visible motion is the stones and the ripples; friction removes the long background
slosh. GPU solver 0.6-0.8 ms/call (1.7 once). Clip for the owner: `slosh_before_after.mp4` (left before 24, right
after: waves keep energy + 5 s friction; same drop, same pose). P1 (pebble drag) not built yet.

### 24.9 P1 amended - pebbles: form drag below the surface, the linear term only at the waterline - DESIGN + check

**Why 24.3 changes.** The one water law (`solver_shared.h`; GPU `solver_integrate.comp`, CPU `VoxelDynamicsWorld`) has a
linear drag (1 - 0.9)^(f dt) - a rate of 2.3 /s - for every body. Granite (b = 0.385) then sinks at g(1 - b) / 2.3 =
2.6 m/s whatever its size. Real terminal speeds (form drag only, v^2 = 2 g (1 - b) s / (Cd b), Cd 1.05 face-on cube):
1/3 m **3.15 m/s**, 1/9 m **1.82**, 1/27 m (4 cm) **1.05**. 24.3 ADDED the quadratic term to the linear one: the 1/3 m
stone would then settle at 1.8 m/s - 43 % slow. The linear term is there to kill a floater's bob (19.6, signed off),
and physically a bob is damped by the WAVES the body radiates at the waterline - a term that exists only while the
body crosses the surface.

**The law:** with f the wet fraction, b the buoyancy ratio, A / V from the body's box along its velocity
(A = 4 (hy hz |vx| + hx hz |vy| + hx hy |vz|) / |v|, V = 8 hx hy hz):
- **form drag, whenever wet:** v <- v / (1 + k |v| dt), k = Cd (A / V) b f / 2, Cd = 1.05 (the exact quadratic decay:
  never reverses, any dt);
- **the linear (radiation) term only at the waterline:** applied as today but with weight f only while 0 < f < 1 -
  fully under (f = 1) it is off;
- angular drag and the current coupling unchanged. Added mass (Ca ~0.5) is NOT modelled - it shapes the entry
  transient, not the terminal speed; logged.

**Tests:** **P-T1** (CPU `WaterBuoyancyTest`, flat water): a stone cube (b 0.385) of side 1/3, 1/9, 1/27 m released
deep: speed after 4 s within 15 % of 3.15 / 1.82 / 1.05 m/s - RED today (2.6 for all three); the existing floater
tests (rise, settle at the line, drift with the current, bit-identical dry) unchanged. **P-L1** live: the entry probe
(`tools/water_entry_probe.py`, its quadratic + added-mass reference) for the 1/3 m stone - within 15 % at 1 m depth;
a 1/9 m and a 1/3 m stone dropped side by side -> **owner's look**.

**Design-check:** voxel look (none); chunks (the water tile lookup is world-column keyed, unchanged); generation
(none); API (none - constants in `solver_shared.h`, one law for both worlds); defaults change - the sinking speed of
every submerged body; the floater tests and P-T1 pin it; tests above (red first, predictions written, control = the
floaters). **READY.**

### 24.10 P1 built (2026-10-11) - ledger. Status: built, not signed off

**Built:** `solver_shared.h` `WATER_FORM_CD` 1.05; the GPU integrator (`solver_integrate.comp`) and `VoxelDynamicsWorld`
apply form drag v_rel / (1 + k |v_rel| dt), k = Cd (A / V) b f / 2, to the velocity RELATIVE TO THE WATER (the first cut
used the ground velocity and braked a floater riding a current to 0.78 of 1.6 m/s - `FloaterDrifts...` caught it), and
the linear term only while 0 < f < 1. A / V from the GPU body's local box / the CPU body's world AABB (rotation ignored).

**P-T1 (CPU, flat water, granite b 0.385, after 4 s):** 1/3 m **3.06 m/s** (terminal 3.15), 1/9 m **1.76** (1.82), 1/27 m
**1.00** (1.05) - within 3-5 %; red before (2.51 for all three). The six floater tests unchanged; 321 water /
buoyancy / debris unit tests pass. **Debris settle bench** (DebrisLab, run on port 8112 - 8090 is held by another
program): same verdicts as the 2026-10-09 and 2026-10-10 baselines (drop_pile, blast, box_through_pile FAIL there too;
the water path is off in DebrisLab) - `docs/evidence/debris_settle/water24_p1`.

**P-L1 (live entry probe, 1/3 m stone at 7.2 m/s; `entry_probe_p1.json`):** at 0.5 / 1.0 / 1.25 m depth the engine reads
5.96 / 4.65 / 4.05 m/s against the quadratic + added-mass reference's 6.41 / 5.41 / 5.02 - **14 % slow at 1 m (prediction
<= 15 % - met), 19 % at 1.25 m (missed)**. Before P1 the engine was 9 % FAST. The gap is the added mass (Ca 0.5) the
law leaves out: done properly it divides gravity and buoyancy too and would slow every floater's (signed-off) bob;
dividing the drag alone would raise the terminal speed 9 % - so it stays out, logged. It errs the way the owner
asked ("not slowed by the water").

**P-L3 (live pair, `tools/water_pebble_pair.py`, `pebble_pair_p1.{json,mp4}`, low pose):** the 1/3 m stone 4.7 m/s at
0.2 m -> 3.3 m/s at 1.4 m; the 1/9 m stone 3.5 m/s at 0.3 m -> 1.7-1.8 m/s from 1 m down - the small one visibly lags.

### 24.11 The level dropped in one jerk (owner, 2026-10-11) - two causes, both fixed. Status: built, not signed off

**Seen:** the owner, on `pebble_pair_p1.mp4`: "does it look like the water level drops in a single jerk". Measured: the
visible water shrank by a third in one frame (9,212 -> 6,208 pond pixels). A no-stone control showed the same: the
drawn surface fell 16.62 -> 16.50 m within ~0.5 s, exactly when the volume's specific KE dropped into the settle band.

**Cause 1 - the demo pond was built with a void.** `WaterCoreManager::placeBox` wrote `fill` into EVERY sub-cell of a
voxel, so "half a voxel" (the demo's y = 16 layer) became three stacked half-full 1/3 m cells. Compaction used to
squash that in the first second; 24.6 confined compaction to the settle band, so the pond carried the void (the cell
under the surface 0.62-0.70 full, water held ~11 cm high) and sloshed from its own construction for ~12 s, then
collapsed it in one go. **Fix:** a voxel's water lies on its floor - sub-layer j from the bottom gets fill x per - j,
clamped (same total; the reply says `layering: bottom_up`). Live, no stones: the surface reads 16.500 m from the first
sample, the cell under it full throughout, the largest sample-to-sample move 0.1 mm. (The pre-fix demo clips started
from a pond that was still sloshing; their "before" motion is partly that.)

**Cause 2 - voids made by moving water waited for the settle band.** `TheLevelNeverJerks` (W-T7, CPU, the 20.8 pond built
right, a 5 cm slosh, 30 s): with 24.6's gate the pond-average surface floats 21 mm high and moves 10.5 mm in one tick
(RED); **fix:** a void under a LIQUID cell closes always - at `SolverParams::compactionSpeed` = 0.01 m/s in moving water,
at free fall in the settle band; a void under a thin film still only in the settle band (the wave drain of 24.6).
Measured alternatives: closing at free fall always - waves on a half-full cell kept 0.69 (mode 2) / 0.42 (mode 4) per
swing and a droplet test broke; slow everywhere - voids at rest stayed (pond 13.5 mm high; spawn, cavity, scrap rows
red). W-T7 with the fix: 6.8 mm worst, 2.1 mm per tick. GPU: the same rule in `wc_column_ops.comp` (param0 settling,
param1 the speed).

**Costs, stated:** on a half-full top cell the short waves give some energy back - mode 3 0.82, mode 4 0.53 per swing
(24.6: 0.94 / 0.97); the 5 s friction slosh at that level measures a 1.3 s half-life (5.2 / 4.4 s at 0.25 / 0.75). The
wave tests' bounds for those are the measurements. Long waves (modes 1-2) keep >= 0.91 everywhere. A drift bound in
`AScrapFliesAndLandsMassExact` became relative (the bottom-up scrap rounds differently: 6e-6 relative, born = landed
exactly). The proper cure for voids made by motion - a volume correction in the pressure solve (the deficit of a
liquid cell pulled back over ~1 s) - is the next lever, not built.

**Live (`pebble_pair_p1fix.mp4`):** largest frame-to-frame change of visible water 404 px (the old clip: 3,004 in one
frame); the stones sink as before (1/9 m 1.8-1.9 m/s, 1/3 m ~3.5 m/s). 283 water / buoyancy tests pass.

## 25. The world water system - sources, paths, settling, underground (DESIGN, 2026-10-11; replaces the creek-only plan)

### 25.1 The brief (owner, 2026-10-11, in order)

- Cost: "i am worried that 'moving' water will have a serious engine cost... i dont want to create a situation where a
  single creak tanks performance". Budget: **2 ms** of frame time, worst case ("2ms seems fine me").
- Physical: "the important thing is a physically realized water system. I want to be able to redirect creaks, i want to
  be able to damn flowing water and watch levels change. there has to be some respect to volume... water has to come
  from somewhere and end up somewhere". "current has to be a object of the simulation not set."
- Far away: "drop a floating item into a moving body of water and have it float down the body of water for a long time
  even when it is out of site of player or outside the window".
- Terrain: "the entire foundation of the engine is a destructable and changeable terrain... potentially massive
  construction efforts to shape land and creat water features... there needs to be 'water sources', and then the engine
  needs to be abel to handle the paths that the water will take".
- Players: "the engine should be able to handle the idea of players adding water sources". Evaporation: "just be
  background balance for now".
- Settling: "it cant be a situation where water sources are defined then the world has to 'fill up'... we need a way of
  either speeding it up or figuring out where things settle quickly."
- Underground: "i dont want a water source on teh service from a creek resulting in the underground world slowing
  filling up... a large mineshaft should not be floodable by a small creek... i expect a small underground cave to be
  floodable, but i want that limit to be for very small caves... i can picture a situation were it becomes impossible
  to remove the water of flooded underground openenings, and i want to reduce that risk from a game play perspective."

### 25.2 The principles (each a rule the build is checked against)

- **P-W1 One water.** Every litre is in exactly one place: a column, a 3-D window, a far body, a droplet, a container.
  Moving between them is a transfer counted in the ledger, never a copy.
- **P-W2 Sources in, sinks out, ledger always.** Water enters only from a SOURCE and leaves only into a SINK (25.4);
  edits never create or destroy water (rule 3 stands). The ledger answers "where did this water come from" at any time.
- **P-W3 Paths are computed, never stored as truth.** Where water goes is the result of the ground as it is now plus
  the sources; the generator's hydrology bake is an INITIAL CONDITION and a source placer, no longer an authority.
- **P-W4 The current is simulated.** Velocity is a state of the simulation at every tier; it changes when the ground or
  the sources change.
- **P-W5 Cost follows activity, never extent.** Still water sleeps (free); flowing water costs per wet moving column;
  3-D detail only in capped windows. No quantity in the cost grows with the length of a creek or the size of a lake.
- **P-W6 Drawing is separate from simulating.** What is in view is drawn; what is simulated runs whether seen or not.
- **P-W7 Chunks are invisible.** No result depends on chunk identity or load order (Feature Design Keys); the
  non-local computations (settling, the far tier) run on the hydrology region grid.

### 25.3 What the world stores (the water record)

| Stored | Where | Content | Authority |
|---|---|---|---|
| Sources | world DB, new `water_sources` table | id, world position, rate (m^3/s), kind (spring / player / tributary-in / rain later), enabled, created-by | **truth** |
| Water columns | per chunk, alongside today's `WaterSpanLocal` runs (D1) | per wet column: its BED (so T2 runs without voxel chunks, 25.15 F2), its runs (bottom, top - several per column: a canal over a creek, a pool under an overhang; `kSurfaceMaxRuns` = 4), and for flowing runs a depth-averaged velocity (2 floats) | **truth** (the water itself) |
| Far bodies | per hydrology region, a cache table | basins (spill level, volume curve, current level, outlet), channel reaches (discharge, mean speed, length, slope, endpoints), their links | **cache** - rebuilt from terrain + sources + columns whenever a region's terrain changes; a stale cache is detected by a terrain hash |
| Water table | per hydrology region, a cache grid | groundwater elevation (25.8) | cache |
| Ledger | world DB | cumulative in (per source) / out (per sink kind), and the current total per tier | **truth** (audit) |
| Carried items | world DB (the existing entity/prop records) | an item's tier and, in the far tier, its reach + distance along it | truth |
| 3-D windows | never | written back to columns before any save (as D1 does for ponds today) | - |

Recipe (`world_meta`, so editing global JSON never changes an existing world): the underground drainage rate (25.8),
the evaporation scale, the column tier's radius and rate, the window budget.

### 25.4 Sources and sinks

**Sources.** A spring is a world object with a rate; its water enters the column under it each tick (an existing pump
primitive, §15.15, promoted to a world object). Generation places springs where the terrain gathers water: at creek
heads and lake feeds, with the rate the hydrology bake's flow accumulation gives (catchment area x runoff - the bake
already computes discharge per channel, `docs/TerrainGenerationV2.md` P2). Tributaries are just more springs upstream; there is
no separate "river" object. **Players add sources** through the same object (API 25.11): the engine treats them exactly
like springs; what a GAME allows (a spring block, a pump from a lake, an aqueduct) is a game-design layer on top.

**Sinks.** (1) The sea - a prescribed boundary (Phase G, §18.2); (2) the edge of the world / a region with no further
simulation (a counted outflow); (3) evaporation from open surfaces - background, rate x surface area, the rate from the
world's climate (Penman-class values, a few mm/day; grounding at build); (4) seepage into the ground (25.8). A pump
that removes water into a container is a transfer, not a sink.

**The ledger.** Per tick: sum(sources) - sum(sinks) = change in total water, to float rounding, at every tier and across
every hand-off. A creek in steady flow shows in = out; a dam shows in - out accumulating as a rising level.

### 25.5 Three tiers of one water

| Tier | Where | Model | Cost driver |
|---|---|---|---|
| **T1 live 3-D windows** | around disturbances near the player (a stone, a wader, a blast, a breach, a waterfall) | today's WaterCore (§15-§24): 3-D, splashes, droplets, ripples, bodies | cells in awake windows - capped by the budget |
| **T2 columns** | every wet column inside a simulation radius (recipe; first target ~500 m), whether rendered or not | the §18 conservative shallow-water solver (`ShoreSolver`: hydrostatic reconstruction, HLL, Manning bed friction, exact ledger), extended to MULTI-RUN columns and to a sparse wet set | wet AND moving columns (still columns sleep) |
| **T3 far network** | beyond the radius, and unloaded regions | basins + reaches (25.3) stepped as a graph: reaches carry discharge at their Manning speed, basins fill and spill (25.7) | number of reaches/basins, per region, at a low rate |

**Hand-offs.** T1<->T2: a window is seeded from the columns it covers (depth -> fills, depth-averaged velocity -> face
velocities with a log profile), takes T2's water in at its upstream faces and hands it back downstream, and on sleep
writes its fills back as column runs + velocity (mass-exact, D1's rule). The window's edge must not be seen: the same
surface height and velocity on both sides of the seam (red test 25.12). T2<->T3: when a region leaves the radius its
columns are summarised into basins (volume, level) and reaches (discharge) - exact volume; when it re-enters, the
columns are re-seeded from the basins (level) and reaches (normal depth from Manning at their discharge), then T2 runs.
**Current** exists at every tier: 3-D velocity in T1, depth-averaged velocity in T2, reach speed in T3.

**What replaces pond friction.** T2's bed friction is Manning's law on the wetted bed - physical, and it acts on a
flow exactly as a stream needs. Inside T1 windows the 3-D core gets the same law at its wet solid faces, replacing the
uniform pond friction of §24.7 (measured: it brakes flows, §24.8) - with the 24.8 slosh numbers as the regression.

### 25.6 Terrain edits

An edit changes the bed (or opens/closes a path) of the columns it touches: those columns' beds update, they wake, and
T2 moves the water - a dam is just bed raised across a channel, and the level rises behind it at the rate the inflow
fills it; a dug channel is bed lowered, and water takes it. Near the edit a T1 window wakes (water pouring into a hole,
a breach). Edits only happen near the player, so T3 never sees an edit live: a region's T3 cache is rebuilt from its
final terrain when it leaves the radius (and on load if its terrain hash differs). Massive earthworks are just many
edits; a large reservoir at rest sleeps like any pond.

### 25.7 Settling fast: generation, catch-up, player sources far away

Never simulate filling to reach a resting state. Compute it:
1. **Basins.** Priority-Flood (Barnes et al. 2014; already in the engine, `PriorityFlood.h`) over the region's bed
   gives every depression, its spill level and outlet, nested into a depression hierarchy.
2. **Water through the hierarchy.** Fill-Spill-Merge (Barnes, Callaghan & Wickert 2021, *Earth Surface Dynamics* - "Computing
   water flow through complex landscapes, Part 3: Fill-Spill-Merge"; grounding auditor to confirm the citation) routes
   the sources' discharge through it: a basin whose inflow exceeds its evaporation + seepage at spill fills to its spill
   and passes the remainder on; one that cannot settles where evaporation + seepage over its surface equal its inflow
   (a terminal lake).
3. **Reaches.** Discharge along each channel = what its sources and upstream basins pass, minus losses; depth and
   speed from Manning's normal depth on the channel's actual cross-section and slope.
4. **Polish.** T2 runs a few seconds of simulated time from that state (close to the answer, so it converges fast) -
   only inside the radius.

Uses: **generation** (a new world starts with lakes at level and creeks running); **catch-up** in T3 (a basin fed by a
source fills in TIME at its net inflow, so a reservoir filling far away for an hour is exactly an hour fuller - an
event step on the graph, not a simulation); **player sources far away** (the same); **a region re-entering the
radius** (T2 seeded from it). Near the player nothing is computed this way: a new spring there fills its hollow live.

### 25.8 Underground water

**Openings only.** Water reaches a cave only through real openings in the voxels; T2's multi-run columns and the 3-D
basin analysis (below) see a cave as its own basin, linked to the surface only where an opening exists.

**The drainage limit (owner's rule: small caves flood, large mines do not, nothing becomes undrainable).** Above the
water table, every wetted underground floor and wall drains at a rate k per square metre (m^3/s per m^2 = m/s). A source
pouring into a space spreads until its wetted area drains what it brings: **steady wetted area A = Q / k**. With the
default k = 1e-4 m/s, a 20 L/s creek can keep ~200 m^2 wet: a cave smaller than that fills completely; a large mine
gets a pool of ~200 m^2 in its lowest part and no more, however long the creek runs; a bigger river floods
proportionally more. **k is a gameplay value, not real rock** (real conductivities: gravel ~1e-3-1, sand ~1e-5-1e-2,
clay ~1e-11-1e-8, unfractured granite ~1e-14-1e-10 m/s - Freeze & Cherry, *Groundwater*, 1979; to be confirmed at
build): it is a world-recipe setting, and the ledger books it as water returning to the ground (a sink). Per-material
multipliers keep the physical ORDER (gravel drains fastest, clay slowest) without letting any common rock seal a mine.

**Always drainable.** (1) Cut the inflow and the space drains by itself at k x wetted area (a 1,000 m^3 flooded cave at
200 m^2: ~14 game-hours; small caves minutes); (2) pumps and buckets (the §4.6a containers) remove water faster than
any source refills a cave of the limit's size - a design inequality checked in the tests; (3) a drain tunnel to lower
ground empties it physically; (4) gravel or sand placed as a floor drains at its higher multiplier.

**The water table.** Each hydrology region carries a groundwater surface: equal to the surface-water levels (rivers,
lakes, the sea) where they exist, and between them a subdued copy of the topography (the standard approximation:
Haitjema & Mitchell-Bruker 2005, "Are water tables a subdued replica of the topography?", *Ground Water* 43). **At
generation**, a cave below the table is filled up to the table: underground lakes appear with no filling time. **At
runtime**, a dug space below the table takes groundwater seepage only through permeable layers (sand, gravel), never
through stone, and always slower than a pump - mining below the table is a manageable leak, never an unwinnable
flood. Springs are, physically, where the table meets the surface - the generator places them there.

**Generation keeps the natural world consistent.** A generated creek crossing a cave above the table either gets a
sealed bed or becomes a sinking stream (its water a counted seepage sink) - no accidental floods at world creation.

**3-D basins.** Steps 25.7 1-2 work on a height field; caves need them on the voxel volume (stacked basins linked by
openings). This is the hardest new piece: a design and a red test of its own before it is built (H5).

### 25.9 Floating items in every tier

T1: a full physics body (today). T2: a lightweight floater moved by the column velocity at its position each tick -
snagging on banks (dry neighbour), wedging on rocks (bed above the surface), stopping in still pools; a few lookups per
item, thousands cost nothing. T3: an item becomes (reach, distance along it) moving at the reach's mean speed, through
basins (a residence time from basin volume / outflow) and over spills; a dammed reach (its basin not spilling) holds it.
An item crossing into a nearer tier is placed at its position and handed the local velocity. T3 is coarse by design: a
far item follows a channel's main line at its average speed, not its eddies.

### 25.10 The budget (2 ms worst case)

| Part | Allotment | Enforcement |
|---|---|---|
| T1 windows | <= 1.2 ms | a cap on awake window cells and count; beyond it a new disturbance gets a smaller window or waits - refused or shrunk, counted, logged (the §22 rule: never silently slow) |
| T2 columns | <= 0.6 ms | runs only wet, moving columns; still columns sleep; the rate drops where nobody looks (a slower step for columns outside the view, never below the stability bound) |
| T3 + items | <= 0.2 ms | graph steps at a low rate, amortised across frames |
| Settling (25.7) | off-frame | a worker job; generation and region entry wait on it, play does not |

The allotments are a plan; H0 measures the real numbers before they are fixed.

### 25.11 API (debug routes first, as everywhere in WaterCore)

`water_sources {add | remove | list}` (position, rate, kind; echoes the ledger effect); `water_network` (per tier:
wet / moving columns, windows, items, ms, budget refusals); `water_ledger` (extended: per source and per sink kind);
`water_settle {region}` (run 25.7 now, report the time and the result); `water_ground {drainage_k}` (the recipe value,
clamped and echoed; 0 = no underground drainage, the A/B control). Units in m, m^3/s, m/s; omitted = unchanged.

### 25.12 Tests (each phase red first; the rigs are WaterBench projects, one chunk where the scenario allows)

| # | Test | Prediction | Control |
|---|---|---|---|
| WS-1 | T2 on a sloped test creek: steady flow | inflow = outflow within 2 % after settling; depth within 15 % of Manning normal depth | the same rig with no slope: no flow |
| WS-2 | a dam built across it | the level behind rises at (inflow - spill) / pool area within 10 %; downstream discharge drops, then recovers when it spills | no dam |
| WS-3 | a channel dug to redirect it | the water takes the new channel; the old one drains | - |
| WS-4 | a T1 window inside a flowing creek | no visible seam: surface height and velocity continuous across the window edge within 1 cm and 5 % | window off |
| WS-5 | the ledger | over every test, sources - sinks = change in total, to float rounding | - |
| WS-6 | settling (25.7) on a generated region | lakes at spill or terminal level; creeks at Manning depth; a short T2 run moves the levels < 2 cm | simulated filling from dry: the same end state, hours of sim time |
| WS-7 | chunk independence | the settled state and the T3 cache are identical whatever the chunk load order (FloraMarginTest shape) | - |
| WS-8 | drainage limit | a 20 L/s source into a sealed 100 m^2 cave fills it; into a 2,000 m^2 mine wets 200 m^2 +- 15 % and stops; cut the source and both drain | k = 0: both fill |
| WS-9 | a pump beats refilling | a pump empties a limit-size flooded cave while its source runs | - |
| WS-10 | a floating item | travels 500 m down a T2 creek and on into T3 and back; arrives where and when the water speed says (+- 15 %) | still water: it stays |
| WS-11 | cost | the 2 ms budget held with N disturbances at once; a 1 km creek costs the same as a 100 m one when nothing is disturbed (P-W5) | - |
| WS-12 | the T2/T3 boundary is not chunk-shaped (25.15 F2) | an item and the water crossing the boundary at a chunk seam and mid-chunk follow the same trajectory within 15 % | - |

### 25.13 Build order (each step designed in detail and design-checked before building; owner looks in motion)

- **H0 - measure first.** `ShoreSolver` as a creek solver on a sloped bench channel (WS-1..3 on today's code: does it
  flow, dam and redirect correctly, and what does it cost); the 3-D core on a 30 m creek for comparison. Decides
  whether T2 is the shore solver extended or something new.
- **H1 - T2 as a world service.** Multi-run columns over the wet set, beds from voxels, edits waking columns, sources and
  sinks as objects, the ledger, Manning bed friction (and §24.7's replacement in T1). WS-1..3, 5, 11.
- **H2 - T1<->T2 hand-off.** Windows seeded from and written back to columns, no seam. WS-4.
- **H3 - settling and the record.** Priority-Flood + Fill-Spill-Merge inside the hydrology bake (generation) and on
  edited regions (runtime), the settled levels as the ONE authority flora / WorldForge / render read (25.15 F3, F4), the
  save format of 25.3, generation starting full. WS-6, 7.
- **H4 - T3 and carried items.** Basins + reaches stepped as a graph, item hand-off across tiers. WS-10.
- **H5 - underground.** 3-D basins, the drainage limit, the water table, generated underground lakes, sinking streams.
  WS-8, 9.
- **H6 - the look of moving water** (whitewater at rocks from the solver, ripples carried by the flow) - alongside, as
  the owner's look demands.

### 25.14 Risks and open questions, stated

- **Multi-run columns** (water above water) are new for the shallow-water scheme; H0/H1 decide whether stacked runs
  are separate solves linked at openings or one scheme.
- **3-D basins** (25.8) have no ready algorithm in the engine; their red test comes first in H5.
- **Big events** (a reservoir released): many moving columns at once - T2's cost under a flood wave is WS-11's stress row.
- **The seam** between T1 and T2 (WS-4) is where past attempts failed visibly (G's sheet/band seam, §18.5).
- **Accuracy of T3:** coarse by design (main line, average speed); the owner accepted the trade for far items.
- **The old CA and `WaterManager`** keep running until the delete ledger (§9) retires them against these tests; nothing
  is removed before its replacement passes.

### 25.15 Design-check (2026-10-11) - round 1 NEEDS WORK (seven findings), folded in below; round 2 READY for H0 only

Checked against `docs/FeatureDesignKeys.md`, `ShoreSolver.h`, `WaterSurfaceMesh.h` (`WaterSurfaceField` /
`SurfaceColumn`, up to `kSurfaceMaxRuns` = 4 runs per column), `Chunk::WaterSpanLocal`, `HydrologyMap` / `FlowField` /
`WaterBodyIndex` and their consumers (`WorldGenerator.cpp` flora gating at :1296 and :1402, `WorldForgePlan.cpp`
settlement siting, `WaterProfile.cpp` render upload).

**1. Voxel aesthetic.** No assets. The risk is how T2 water LOOKS: §25 said drawing is separate but never how T2 is
drawn. **F1 (fold-in):** T2 publishes the same `WaterSurfaceField` the 3-D core publishes today (columns of runs with
bottom/top; `kSurfaceMaxRuns` = 4 already allows stacked water), so ONE surface mesher and ONE shader draw T1 and T2 -
the seam (WS-4) is then a data question, not two renderers meeting. T3 is never drawn up close (it exists only where
nothing is rendered at water detail); at distance, the existing far water draw stays until its own design. Detail is
unconditional: T2 is not a quality tier, it is where water is simulated at column resolution.

**2. Chunk independence.**
- *Storage per chunk* (column runs + velocity next to `WaterSpanLocal`): cost/persistence only - fine.
- **F2 - the T2/T3 boundary.** T2 needs beds; if "inside the radius" meant "chunk resident", the boundary would be
  chunk-shaped and an item's behaviour (column-carried vs main-line) would change at a chunk edge - a defect.
  **Fold-in:** the wet-column record stores each column's BED (25.3), so T2 runs without voxel chunks; the T2 set is
  chosen on the WATER GRAPH - whole reaches and basins whose nearest point is within the radius, with hysteresis - so
  the boundary falls inside a reach or at a basin outlet, never at a chunk edge. Red test **WS-12**: an item and the
  water state crossing the T2/T3 boundary at a chunk seam and mid-chunk give the same trajectory within 15 %.
- **F3 - resolution of settling.** Fill-Spill-Merge over a whole region at 1/3 m columns is not affordable, and unloaded
  regions have no fine beds. **Fold-in:** two resolutions. (a) GENERATION: the settle runs INSIDE the hydrology bake, at
  the bake's resolution, as a pure function of the bake's inputs (the bake is already "a PURE function of (generationType,
  seed, climateFrequency...)", `WorldGenerator.cpp:251`) - order-independent by construction, since it reads no chunk;
  fine water is then placed per chunk from the settled levels, as D4 places river spans today. (b) RUNTIME: the fine
  settle (Priority-Flood + FSM on the 1/3 m beds) runs only on the T2 set and on regions whose terrain was EDITED, at
  unload, from their own saved beds. WS-7 is the chunked-vs-whole test for (a): the settled levels per column are
  identical whether the region is evaluated whole or chunk by chunk in any order.
- No new cross-chunk lookups: T2 reads its own column record, not neighbour chunks.

**3. Procedural generation.** Stage: **hydrology** (consumes terrain heights, climate; produces sources, settled water
levels, the water table). **F4 - the later stages read the bake's lake levels**: flora is gated "under lake/sea"
(`WorldGenerator.cpp:1296`, :1402), WorldForge sites settlements from `WaterBodyIndex`, the render upload reads the same
levels. If the settle produced different levels (a terminal lake below its spill), trees would stand in water and
towns would sit on the wrong shore. **Fold-in:** the settled levels REPLACE the bake's lake levels as the one
authority every later stage reads (one field, `HydrologyMap` levels after settling) - no second water-level source. The
water table (25.8) is computed in the same stage. Per-world tuning in `world_meta` (25.3): drainage k, evaporation
scale, T2 radius, budget - and the hydrology version, so an existing world never changes when the code's defaults do.

**4. API.** **F5 - clamps and defaults were unstated.** Fold-in: `water_sources add` rate clamped to (0, 10] m^3/s
(the existing `water_av_source` bound - above it one cell cannot accept the inflow per tick at 1/9), position must be
in a loaded column or it is refused with the reason; `water_ground {drainage_k}` clamped [0, 1e-2] m/s (0 = off, the
control; above 1e-2 a creek could not wet a 1 m^2 floor - water would vanish at the cave mouth), default 1e-4; T2
radius [64, 2048] m, default 512; budget [0.5, 8] ms, default 2. Every route echoes the resulting state; omitted =
unchanged; each clamp states its failure at the site. Defaults are new (no earlier pin); WS-8 pins k, WS-11 the budget.
§24.7's pond friction default changes when H1 replaces it - `PondFrictionHalvesASloshEveryFiveSeconds` and the S8 test
are updated in that commit.

**5. Visual test plan.** "Works" is WS-1..WS-12, each a number with a control (25.12). Depth: L2 for the ledger and
settling (WS-5, 6, 7), L3 for flow, dams, redirects, drainage and items (WS-1..3, 8..10, 12), L4 + the owner's look in
motion for every phase. **F6 - rigs.** WS-1..4, 8, 9 fit one chunk (a 30 m sloped channel and a cave on WaterBench_Small,
both authored like the §11 A rigs); **WS-10 (500 m travel), WS-11 (a 1 km creek) and WS-12 cannot** - they need a long
creek: a new bench, `WaterBench_Creek` (a generated or authored 1 km sloped channel with a pond, a cave and a dam site),
stated as a multi-chunk rig by necessity. **F7 - two waters in one place.** The old CA (`WaterManager`) still pins lakes
and rivers from the bake (Water.md); on a bench where both run, the measurements mix. Fold-in: §25 benches run with the
CA disabled (`water.enabled` false, the core only), and the delete ledger (§9) retires the CA per region only after the
WS tests pass there. Rig vs shipped: benches use shipped §25 defaults; H0 additionally runs friction-off and k = 0
controls.

**Round 2:** voxel look (one surface pipeline for T1 + T2, unconditional); chunks (bed in the record, T2 set on the water
graph, settle inside the pure bake, WS-7 + WS-12); generation (hydrology stage, settled levels the single authority for
flora / WorldForge / render, recipe persistence); API (units, clamps with reasons, echoes, defaults and the pins they
move); tests (WS-1..12 with predictions and controls, one-chunk rigs where possible, `WaterBench_Creek` where not, CA
off). **READY for H0** (a measurement: `ShoreSolver` on a sloped channel - flow, dam, redirect, cost - and the 3-D core
on 30 m). H1-H6 each get their own detailed design and check before building, as 25.13 says.

### 25.16 H0 measured (2026-10-11) - the column solver carries a creek; the voxel staircase bed is the open question

`tests/core/WaterCreekH0Test.cpp` (CPU, printed; the harness supplies a spring and an outfall and keeps its own ledger -
`ShoreSolver` has neither yet). Rig: a straight channel 30 m x 2 m, 1/3 m columns, slope 1:50, Manning n 0.035 (a clean
stony stream, Chow 1959). Ledger closes to ~1e-6 m^3 in every run.

**WS-1 steady flow - smooth bed: exact.** 20 L/s: out/in 1.000, mid depth 2.73 cm vs Manning 2.73 cm, speed 0.363 vs
0.366 m/s. 100 L/s: 1.000, 7.17 vs 7.17 cm, 0.694 vs 0.697 m/s.
**Stepped beds (what voxel terrain gives): not a creek, a staircase of pools.** 1/9 m steps: depth x2.06 / x1.58 of
Manning, speed 0.22 / 0.50 m/s; at 20 L/s only 65 % had reached the end after 180 s. 1/3 m steps: at 20 L/s NOTHING
reached the end in 180 s (each 1/3 m step on a 1:50 slope is a 16.7 m pool that must fill before it spills); at
100 L/s, out/in 1.01 and depth x2.25. The physics is right for a staircase - **the bed is wrong for a creek.** This is
the first design question of H1 (25.17).

**WS-2 a dam (0.6 m across the channel, Q 0.1):** storage behind it = Q t exactly (2.000 m^3 at 20 s, 12.000 at 120 s);
outflow falls 0.1 -> 0.0154 (20 s) -> 0.0001 m^3/s; the pool rises to the crest and spills from ~130 s; outflow back to
0.013 m^3/s at 240 s and rising. **WS-3 a redirect** (a wall opened into a parallel channel, the old one blocked below
it): within 60 s the new channel carries 0.0986 of 0.1 m^3/s and the old one 0.005, then 0.0003. Both as predicted.

**Front speed on a dry bed:** 30 m in 78.9 s (20 L/s) and 40.8 s (100 L/s) against 49.1 / 25.8 s for the kinematic
estimate 5/3 u_n - 1.6x slower, independent of `dryDepth` (1 mm or 0.1 mm). Plausibly physical (the thin leading edge
feels Manning friction as d^(-4/3)), not yet checked against a dam-break-on-a-dry-slope reference - an H1 test.

**The 3-D core on the same creek** (1/3 m cells; its bed is necessarily the 1/3 m staircase): 1.75-1.85 ms per tick
(4,320 cells) - **11x the column solver** - and no better: 20 L/s delivers 0.86 of the inflow with friction off (depth
4.4 cm vs 2.7) and 0.035 with the 5 s pond friction on; 100 L/s 0.99 / 0.79, depth 10.1 / 19.1 cm vs 7.2. A 3-7 cm deep
creek is a fraction of one 1/3 m cell - the 3-D core cannot resolve it. **Confirmed: creeks are columns (T2); the 3-D
core only in windows (T1).**

**Cost, CPU, one thread, 30 Hz, smooth bed:** 30 m (540 columns) 0.154 ms per tick, 285 ns per column-tick; 100 m (1,800)
0.383 ms, 213 ns; 300 m (5,400) 0.826 ms, 153 ns. One CPU thread fits ~200 m of moving 2 m creek inside T2's 0.6 ms;
the 512 m radius needs the GPU (or threads, or a slower step away from the view) - an H1 decision with its own measure.

### 25.17 What H0 decides for H1 (to design and check next)

1. **T2 is `ShoreSolver`, extended** - Manning-exact on a smooth bed, dams and redirects right, ledger exact, 11x cheaper
   than the 3-D core. Extensions: sources and sinks (springs, outfalls) inside the solver with the ledger; multi-run
   columns; a sparse wet set instead of a dense box; beds from the world.
2. **The bed a creek flows over** (the open question). Voxel tops make a staircase of pools; real creek beds slope.
   Options to weigh in H1's design: (a) for UNEDITED ground, the generator's continuous height (the terrain is a pure
   function of world position - the bed a creek "really" has), voxel tops only where the player edited; (b) a hydraulic
   bed: within a run of equal voxel tops, the bed ramps toward the next step, so a staircase reads as the slope it
   approximates (the water's surface then sits within a step's height of the drawn voxels - a look check); (c) the
   microcube-resolution bed (1/9 m steps) - measured here: still 1.6-2x too deep. A staircase built ON PURPOSE (a
   player's terraces) must still pool - the design has to tell intent from quantisation without guessing.
3. **The dry-bed front speed** against a reference (a dam break on a dry slope).
4. **Cost** - GPU port vs threads vs a slower step away from the view, measured against the 0.6 ms allotment.

### 25.18 H1 design - the column tier on real terrain, and the bed comparison (2026-10-11)
> **Removed in 26.27 (P3d-2):** `ColumnWater`, the column regions and the `water_columns` route are gone; pond water (§26) is the only water. `ShoreSolver` stays (the sea's surf band).

**The owner (2026-10-11), on the bed question:** "i think so [(a)]. would be nice to compare both to see what is better
though". **A fact that sharpens it:** natural terrain is FULL 1 m cubes - `WorldGenerator::ColumnSample::surfaceY` is an
integer (the top solid voxel); the generator's height itself is continuous (`HydrologyMap::HeightFunc`, a float of world
position). A natural creek bed is therefore a staircase of 1 m steps - coarser than H0's 1/3 m case, where a 20 L/s creek
did not reach the end of 30 m in three minutes.

**The candidates, compared live (not chosen on paper):**
- **(a) generator bed** - unedited columns use the continuous generator height; a column is "unedited" when its top solid
  voxel equals the generator's quantised surface there (a pure function, nothing stored); edited columns use their
  voxels. Flows like H0's smooth bed. **Risk:** the water sits on a surface up to 1 m BELOW the drawn cube tops - a 3 cm
  creek hidden inside the ground the player sees.
- **(b) hydraulic bed** - along the flow, each flat run of equal tops ramps from the top of the step above it down to its
  own top, so the bed is never below the drawn ground and never above the step it comes from. Flows like a slope; the
  water is always on or above the drawn voxels, and on a long flat run it reads as a pool behind the step. **Risk:** a
  terrace built on purpose (a player's paddies) would not pool - it must be told from quantisation (the rule:
  only runs the generator made, i.e. unedited columns, are ramped).
- **(c) shaped bed** - the generator carves creek beds in SUBCUBES / MICROCUBES (the engine's detail sizes) along the
  channel, so the drawn bed is a 1/3 m or 1/9 m staircase approximating the slope and the water follows real geometry
  (H0: 1/9 m steps ran 1.6-2x Manning's depth - measured again on the real channel). Voxel-honest and in the design keys
  ("detail in subcubes/microcubes"); costs generator work (a hydrology-stage carve) and more static faces near water.

**Measured for each, on the same generated creek and the same authored channel:** steady out/in, depth and speed vs
Manning (25.16's method), **the hidden fraction** (wet columns whose water surface is below the drawn top of the column's
solid voxels - the look-breaker for (a)), the largest gap between the water surface and the drawn ground, and a clip
from the same camera for each -> **the owner chooses**. The losing candidates stay behind the switch only until then.

**H1 builds (CPU first; the GPU port is H1b, 25.17 item 4):**
1. **`ColumnWater`** - a T2 region on the world: a `ShoreSolver` lattice (1/3 m columns) placed over a creek, beds from the
   selected rule, walls from solid voxels above the water range, sources (springs) and sinks (outfalls) inside the solver
   with its ledger (`exchanged` extended to in/out per object), Manning n per bed material (stony 0.035, sand 0.025,
   grass 0.035, wood 0.012 - Chow 1959; confirmed at build).
2. **Drawn through the existing surface pipeline**: T2 publishes a `WaterSurfaceField` (25.15 F1) - one mesher, one shader.
3. **API** `water_columns {create {box, h}, destroy, list, bed: generator|hydraulic|voxel, spring {x,z,rate}, outfall {x,z}}`
   - units m and m^3/s, omitted = unchanged, echoes the region, its ledger (in / out / stored) and the bed rule; rate clamped
   (0, 10] m^3/s (as `water_av_source`, the failure written at the site); `bed` default `voxel` until the owner chooses.
4. **Rigs:** (i) the authored 30 m channel of H0 rebuilt in voxels on `WaterBench_Small` (1 m, 1/3 m and 1/9 m stepped
   beds - one chunk); (ii) a generated creek: `WaterBench_River` (Mountains, streaming, seed 20260729) - a stretch of
   an order-1/2 channel picked once and written into the rig spec; (c) needs the generator change, so its row is built
   after (a)/(b) and runs on a regenerated copy of the River bench (the original stays untouched).

**Tests (red first; predictions now):** C-T1 the column region's ledger exact over 10 min with springs and outfalls;
C-T2 on the authored 1/9 m channel, voxel bed: depth within the H0 numbers (x1.6-2.1) - the harness and the world agree;
C-T3 (b) on the 1 m staircase: out/in = 1 within 2 % at steady state, depth within 25 % of Manning; C-T4 (a) on the
generated creek: depth within 15 % of Manning AND the hidden fraction reported (prediction: > 50 % of wet columns hidden -
the reason to compare); C-T5 the dry-bed front against a dam-break-on-a-slope reference (25.17 item 3); C-T6 cost per
column on the world lattice vs H0's 150-285 ns. Controls: bed `voxel` (the staircase) for C-T3/C-T4; Q = 0 (still water
stays still) for all.

**Not in H1:** multi-run columns, the sparse wet set, edits waking columns, sources as saved world objects, the GPU, the
T1 hand-off (H1b-H2); generation of springs (H3).

**Design-check (2026-10-11):** voxel look - the comparison exists BECAUSE of it: (a) risks water hidden in drawn ground
(measured, C-T4), (b) keeps water on the voxels, (c) is the design keys' own answer (detail in subcubes/microcubes); detail
unconditional - the bed rule is a single world-wide choice, not a quality tier. Chunks - the bed rules are pure functions
of world position ((a) generator height; (b) a run of tops along the flow, which may cross a chunk seam: the run is read
from the column record's beds, not from neighbour chunks; red test C-T7: the hydraulic bed of a run straddling a chunk seam
equals the same run mid-chunk); (c) is a generator carve keyed on world position and the channel, chunk-independent by the
hydrology stage's existing contract. Generation - only (c) touches it (hydrology stage, after the bake, before flora - flora
reads the carved surface as it does today). API - units, clamps with reasons, echoes, the default stated; no existing
default moves. Tests - C-T1..C-T7 with predictions and controls, one-chunk authored rig + the River bench, rig = shipped
defaults except the bed switch. **READY for H1 (a) and (b); (c) gets its own check when its generator carve is designed.**

### 25.19 Owner decision (2026-10-11): creek beds are generated in 1/3 m subcubes

**The owner:** "terrain can be better than 1m voxels, it just standardizes to that. we can add to the plan that the
terrain generation for creak beds has to be 1/3". **Rule:** the generator carves every creek bed (and its banks to the
water line) in SUBCUBES - 1/3 m - not full cubes. This is candidate (c) of 25.18 at 1/3 m, now a requirement, not an
option. It belongs to the hydrology stage (after the bake, before flora; flora reads the carved surface as today) and
gets its own design and check before it is built (H1c).

**What it does and does not settle.** It shrinks the gap between the drawn bed and the slope it approximates from 1 m to
1/3 m - water can hide at most 1/3 m below the drawn ground instead of 1 m. It does not by itself make water flow like a
creek on a gentle slope: at 1:50 a 1/3 m bed still steps only every 16.7 m, and H0 measured exactly that bed pooling (a
20 L/s creek did not reach the end of 30 m in 180 s, 25.16). **So the comparison stands, on 1/3 m beds:** (a) the
generator's continuous height (hidden fraction now bounded by 1/3 m - still enough to hide a 3 cm creek) against (b) the
hydraulic bed ramped between 1/3 m steps (never below the drawn subcubes), with the raw voxel staircase as the control.
On steep reaches (a 1/3 m step every few metres) the three converge; the gentle reaches decide it.

**H1 order, amended:** (i) the authored 30 m channel rebuilt with a 1/3 m stepped bed on `WaterBench_Small` - (a), (b)
and voxel compared there first (one chunk); (ii) H1c, the generator's 1/3 m creek-bed carve, designed and checked; (iii)
the comparison repeated on a generated creek of a regenerated copy of the River bench; (iv) clips -> the owner chooses
the bed rule.

### 25.20 H1 step 1 built and measured (2026-10-11) - the bed comparison on a 1/3 m staircase. Status: built, not signed off

**Built:** `ColumnWater` (`engine/{include,src}/core/water/ColumnWater.*`) - a `ShoreSolver` region over a box of the world
with springs, outfalls and a ledger; beds and walls read from the micro occupancy (the top of the highest solid micro
cell under each 1/3 m column's centre; a column solid to the box top is a wall); bed rules `voxel` / `hydraulic` / `plane`
(a rule switch keeps every column's depth); drawn by publishing a `WaterSurfaceField` to the existing mesher and shader
(the run spans the DRAWN ground to the surface). `WaterCoreManager` steps the regions in real time and appends their
fields after the volumes'; the render feed's gate (`Application.cpp`, "totalCells() ?") also opens for column regions -
without it nothing drew. Route `water_columns` (create / destroy / bed / plane / spring / outfall / clear_flows / refresh;
reply: ledger, `stats` = mid depth and speed, wet and hidden columns). Fix found by the unit test: the hydraulic ramp
counts an OUTFALL as a lower exit - without it the last run of a channel (its next step outside the region) never ramped.

**Unit (`WaterColumnTest`, 3 tests):** C-T1 ledger -2.2e-7 m^3 after 600 s with springs, outfalls and a rule switch (the
switch keeps the stored volume to 1e-9); C-T7 the hydraulic bed of a staircase at two placements identical (0.0 m); the
three rules on a 30 m 1:50 channel floored to 1/3 m steps (100 L/s: plane 7.17 cm = Manning, hydraulic 9.59, voxel 16.1;
20 L/s: plane 2.73 = Manning, hydraulic 4.13 and 98 % through, voxel nothing through yet).

**Live (`tools/water_creek_rig.py`, WaterBench_Small chunk 7: a 30 m x 2 m stone channel, walls to y 21, its bed a 1:50 line
from 19.0 floored to 1/3 m subcube steps - 846 subcubes; 20 L/s, 180 s per rule; `docs/evidence/water_core_h1/`):**

| bed rule | water reaches the end | out/in, last 20 s | mid depth (Manning 2.73 cm) | mid speed (Manning 0.366 m/s) | hidden columns |
|---|---|---|---|---|---|
| voxel (the staircase) | not in 180 s | 0 | 7.3 cm (pooled) | 0.03 m/s | 0 |
| hydraulic | 118 s | 0.91 | 4.2 cm | 0.26 m/s | 0 |
| plane (the smooth line) | 80 s | 1.000 | 2.73 cm | 0.366 m/s | 0 |

Ledger -5e-8 m^3 every run; region step 0.3-0.74 ms (CPU, 1,080 columns - the whole box, walls included). Clip:
`creek_beds_compare_x3speed.mp4` (the three runs side by side at 3x).

**What it shows.** (1) The plane rule - the stand-in for the generator's continuous height, candidate (a) - is Manning-exact
here AND hides nothing: the drawn bed was made by FLOORING the line to 1/3 m, so the line is always on or above the drawn
subcubes (up to 1/3 m above - the water fills each step's corner). **Design consequence for H1c:** carve creek beds by
flooring the continuous height to the 1/3 m grid; then (a) can never hide water. (2) Hydraulic flows but is 1.5x deep and
slow on a gentle slope: with a step only every 16.7 m it ramps over long runs and keeps the head run flat. (3) The raw
staircase pools - as H0 measured. **First impression: (a), with the floor rule.** Not decided - the owner looks at the
clip; the generated-creek comparison (H1c then step iii) follows.

**A process failure, recorded:** the first three live runs measured an EMPTY chunk - the rig had been built in memory, the
engine was restarted, and nothing checked the world. `water_creek_rig.py` now verifies the walls and bed through
`/api/world/terrain_height` and refuses to run without them (shown refusing, then passing after `--build`).

### 25.21 Owner decision (2026-10-11): option (a) - the water flows on the generator's continuous height. The other options, recorded for a future switch

**The owner:** "lets get it done. go with option a, but make sure to record details of other options so we can maybe
switch to it in the future". **Decided:** creek water (T2) flows on the generator's CONTINUOUS ground height where the
ground is as generated; where the player edited, on the voxels. The drawn creek bed is carved to the 1/3 m grid by
ROUNDING DOWN (25.22), so the continuous height is always on or above the drawn bed - water can never hide in it (25.20).

**The options not chosen - each kept as a working bed rule in `ColumnWater` (`water_columns {bed}`), so a switch is a
setting, not a rebuild:**

| Rule | How the bed is made | Measured (25.20: 30 m, 1:50, 1/3 m steps, 20 L/s) | When it would be the better choice |
|---|---|---|---|
| **(a) generator** - CHOSEN | the generator's continuous height (`ColumnSample::bedHeight`, 25.22) on unedited columns; voxel tops on edited ones | plane stand-in: Manning-exact (2.73 cm, 0.366 m/s), through in 80 s, nothing hidden | whenever the drawn bed is carved by rounding down - the generated world |
| **(b) hydraulic** | `hydraulicBed()` - per run of equal voxel tops, a ramp from the step above to the run's own top; outfalls count as a lower exit; walls are banks, never steps; a run with no lower exit stays flat (a pool) | 4.2 cm (1.5x), 0.26 m/s, 91 % through, first water at 118 s; nothing hidden | ground with NO generator height behind it (fully player-built terrain, imported maps) where a staircase should still read as a slope; known limits: few steps on gentle slopes -> long flat ramps; ramps toward non-wall higher banks too (a cross-channel ramp) - needs a flow-direction rule before it is used on natural ground |
| **(c) voxel** | the top of the highest solid micro cell under the column | pools: 7.3 cm, 0.03 m/s, nothing through in 180 s | player TERRACES that must pool (paddies, weirs) - it is what (a) already uses on edited columns |
| **plane** (test only) | `y0 - slope x distance` along a direction | as (a) | authored rigs with no generator surface - the stand-in that measured (a) |

**What a future switch would need:** (b) - its two known limits fixed (a flow-direction rule for which higher neighbours
count as "the step above"; a slope estimate for the head run), then the 25.20 rig and a generated creek re-run; (c) - nothing
(it exists); changing the default is one line in `WaterCoreManager::createColumns` plus the pinned test (25.22 C-T9).
### 25.22 H1c design - creek beds carved to the 1/3 m grid, and the water's generator bed (2026-10-11)
> **Removed in 26.27:** the regions' generator bed (`setGeneratorBedQuery`); the 1/3 m carve stays in the generator.

**What the generator does today** (`WorldGenerator::sampleColumn` / `generateChunk`): a column's continuous height H
(`coarse.baseHeight + relief * blendScale + blendOffset - creekSwale`) is floored to a VOXEL INDEX, `surfaceY = floor(H)`,
and that voxel is solid - so the drawn top, surfaceY + 1, is ABOVE H by up to 1 m (a full-cube rounding UP of the
surface). Rivers of order >= 3 lower surfaceY by `lround(channel depth)` whole voxels. Creeks (order 1-2) are not carved;
the inner band (`creekBed`: channel depth >= 0.15) gets a fixed 2-layer subcube shelf in its surface voxel - a 1/3 m
recess below the banks, the same everywhere, not following the slope.

**The change (creeks, orders 1-2, the `creekBed` band - the owner's rule 25.19 says creek beds):**
1. `ColumnSample` gains `bedHeight` - the continuous H of the column after every terrain term (swale; the river carve as a
   continuous depth, not `lround`) - a pure function of world position, like `surfaceY`.
2. In a `creekBed` column the surface is carved by ROUNDING DOWN to the 1/3 m grid: L = floor(3 (H - floor(H))) subcube
   layers (0, 1 or 2) in voxel floor(H); L = 0 makes that voxel air and the surface voxel the one below (full, top =
   floor(H)). So the drawn bed top = floor(H) + L/3, always <= H and within 1/3 m of it. This REPLACES the fixed 2/3 shelf.
   Banks (non-creek columns) keep today's rounding - the creek therefore sits 0-1 m below its banks, following its own
   slope (today: a constant 1/3 m).
3. Rivers (order >= 3) unchanged in this step - the same rule applies to them later (logged), with their own check.

**The water side (T2):** `ColumnWater` gets the bed rule `generator` - for each column, a `bedHeight(x, z)` query (the
editor wires the streaming generator's sampler); a column is UNEDITED when its drawn top (from the micro occupancy) equals
the top the generator's carve would draw there (step 2's formula; equality within 1e-3 m); unedited -> bed = bedHeight,
edited -> bed = the voxel top. Default bed rule: `generator` where a height query exists, `voxel` otherwise (the authored
bench has none - its rigs keep `plane` / `voxel`).

**Design keys.** *Voxel look:* the carve is in SUBCUBES - the engine's detail size - and unconditional in creek columns;
more static faces near creeks (each carved column emits up to 18 subcubes instead of 1 cube): measured on the River bench
(render face count before/after in a fixed view). *Chunks:* `bedHeight` and the carve are pure functions of world
position (as `surfaceY` and `creekBed` are today - streamed chunks and a whole-region pass agree by construction); red test
**C-T8**: for a stretch of creek straddling a chunk seam, the per-column drawn top and `bedHeight` from a chunk-by-chunk
pass equal a whole-region pass. *Generation:* hydrology stage output consumed by the terrain stage, as today; later
stages - flora is gated on `riverOrder` (unchanged), WorldForge bridges set piers "from the carved bed" (reads the new,
lower bed: a pier is 0-1 m taller - correct), the old CA's creek ribbon reads `Chunk::subVoxelFloor` (it was 2/3; now 0,
1/3 or 2/3 - the OLD water's ribbon follows the new bed; it is retired by §9 anyway, its creek rows to be re-measured
on the River bench before/after). World recipe (`WorldRecipe`, `world_meta`): the carve is a code rule - existing worlds keep their saved chunks, but
their UNSAVED chunks would regenerate with the new carve -> a recipe flag `creekCarve` (`fromJson` default false, so an
old world's recipe without the key stays off; `WorldGenerator::makeRecipe()` sets it true for new worlds; written only
when true, so legacy recipe JSON stays byte-identical, the WorldForge-block pattern) so editing code never re-shapes a saved world's unsaved chunks differently from its
saved ones. *API:* `water_columns {bed: "generator"}` (echoed); the generator has no new route. *Tests:* **C-T8** (seam);
**C-T9** - per creek column: drawn top <= bedHeight < drawn top + 1/3 m (every column of a generated stretch); **C-T10** - the
River bench, a generated creek stretch: (a) on the new carve - out/in, depth vs Manning on the stretch's own slope, hidden
columns (prediction 0), against `voxel` as the control; the owner's look in motion.

**Design-check (2026-10-11): READY**, with the recipe flag (`creekCarve`) and C-T8 as the two conditions the build must
meet; rivers deferred with their own check.

### 25.23 H1c built and measured on a generated creek (2026-10-11). Status: built, not signed off

**Built (commits `9b92ea61` + this one):** `WorldRecipe::creekCarve` (written only when true; new worlds on via
`makeRecipe()`, old worlds off); `ColumnSample::bedHeight` (the continuous height, the river carve as a continuous depth);
`WorldGenerator::creekLayers` / `drawnTop` / `inCreekBand`; in a creek-bed column the surface voxel holds
floor(3 (bedHeight - surfaceY)) subcube layers (0-2); `ColumnWater` bed rule `generator` fed by
`WaterCoreManager::setGeneratorBedQuery` (the editor wires the streaming generator) - per 1/3 m column, bilinear between
voxel centres and never below the drawn top, on unedited columns; edited columns keep their voxels; new regions default to
`generator` where the query exists. Debug route `terrain_column` (one generated column: bed height, drawn top, creek flag,
order). Bench: `WaterBench_RiverCarve` (port 8113) - a copy of the River bench with a fresh world (carve on) and the old
CA water off; the original River bench is untouched.

**Two findings on the way, both fixed and pinned:**
1. **The creek band stepped DIAGONALLY.** The live creek's water pooled 40 cm deep beside the spring and never reached the
   outfall: the band (columns at least 0.15 deep - the old CA ribbon's threshold) is ~0.74 voxel either side of a warped
   centreline, a one-voxel line that joins at CORNERS, and water crosses faces. Under the carve flag the band is the whole
   channel width AND a corner fill: a column is bed when it is the face shared by two diagonal band columns that share no
   other face (`inCreekBand` of four neighbouring POSITIONS - pure, no neighbour's sample). Measured in 8 complete 24 x 24
   windows: corner-only joins 57 with the old band (the control), 40 with the full width alone, **0 with the corner fill**.
2. **The first live map was misleading** (a camera over the hillside, grass over the slot): the tool now takes `--cam`.

**C-T9 / C-T8 (`TerrainRiverTest.CreekBedCarvedToAThirdByRoundingDown`):** 300 creek-bed columns (162 on chunk seams): drawn
top <= bedHeight < top + 1/3 m and = `drawnTop()` in every one; layers 0/1/2: 98 / 94 / 108. 386 water / terrain / recipe /
flora / WorldForge / fauna tests pass.

**C-T10 live (`tools/water_creek_live.py`, a generated order-1 creek near (-2140, -1740): 19 m, slope 0.047, 20 L/s, 150 s):**

| bed | first water at the outfall | out/in, last 20 s | mid depth | mid speed | hidden |
|---|---|---|---|---|---|
| **generator (option a)** | 49 s | **0.999** | 5.9 cm | 0.38 m/s | 0 |
| voxel (control) | 143 s | 0.09 | 8.3 cm (spread over 321 columns) | 0.17 m/s | 0 |

The depth is above the wide-channel Manning estimate (1.4 cm for the 4.2 m band) because the water runs in a narrower,
meandering line with pools; the solver itself was Manning-exact on the straight channel (25.16, 25.20). Ledger 3e-8 m^3.
Clip for the owner: `gencreek_generator_look.mp4` (90 s, a low view downstream along the creek's slot).

**Open, logged:** a corner-fill column takes its OWN continuous height (between the two band columns' - it can sit a few cm
above both; not measured as a barrier yet); rivers (order >= 3) keep their whole-voxel carve until the same rule is designed
for them; the region step reaches 3.4 ms on this 1,400-column box on the CPU (the GPU port, H1b).

### 25.24 The simple creek bench (owner, 2026-10-12) - water first, generator integration later

**The owner, on the generated creek:** "the meandering shape of the creak is pretty good... but yeah we are adding depth on
top of the generator depth which is make it worse... i feel like we should start much much much more simple. a mostly flat
world without flora or fauna. you just carve out a simple mostly straight creak so we have a much more simple testing
point. we can work on integration with the engines world forge after. we need to focus on water for now".

**Decision:** water development runs on `WaterBench_Creek` (port 8114, `Documents/PhyxelProjects/WaterBench_Creek`): an
empty world, a flat 128 x 64 m slab (dirt, a grass top layer, the ground's top at y 16), no flora, no fauna, no generator
terrain, the old CA water off, and ONE straight creek carved by `tools/water_creek_bench.py --build` - x 10-109 (100 m), z
31-32 (2 m wide), its bed a 1:200 line from 15.67 m rounded DOWN to the 1/3 m grid in gravel subcubes (1,224), so it
starts 1/3 m below the grass and ends 1 m below. Saved, and verified after a restart. The creek's water is a column-water
region on the carve's line (`plane`, now with the option-(a) rule on authored ground: the line where the column IS the
carve - drawn top within one 1/3 m step below it - and the voxel top elsewhere, so a bank's water never sits under its own
ground; the 25.20 channel's numbers are unchanged by it), a spring at the head, an outfall at the foot.
`--run [--q]` starts it and leaves it running; `--record S` adds a clip and the numbers.

**First run (100 L/s, 90 s, `creekbench_q0.1.{json,mp4}`):** the water is visible - clear over the gravel between the grass
banks; mid speed 0.46 m/s (Manning 0.46), mid depth 7 cm and rising (Manning normal depth 10.9 cm) - not yet at steady
state: the front had not reached the foot of the 100 m creek in 90 s; ledger 1.3e-7 m^3; region step <= 1.2 ms (CPU).

**Logged for the generator integration (later, not now):** the generator's creek SWALE (up to 1.6 m at the centreline,
`kCreekSwaleDepth`) plus the 1/3 m carve plus 1 m banks rounding up read as a trench beside 1 m terrain - the owner: "it looks
a little deep at many parts"; the creek shape (meander) is good. To revisit when water goes back into the generated world
(`WaterBench_RiverCarve`, 25.23).

### 25.25 What the creek has of the pond work, and the current in action (owner, 2026-10-12)

**The owner:** "first, i want to know that the water rippling and physics are staying true to the pond work. next i want to
'see' current in action". **Measured on the creek bench** (the creek at steady flow, 429 m^3 in / 408 out;
`tools/water_creek_current.py`, `creek_current.{json,mp4}`, `creek_current_frames.png`):

- **The pond's small-scale physics is NOT on the creek yet.** A 1/3 m stone dropped mid-reach: ripple layers 0, droplet births
  0, moving-solid bodies 0, before and after. The ripple layer (22), the droplet crown (21) and displacement (20) are built
  for 3-D volumes only; the creek is a column region (T2). They reach a creek through the T1<->T2 hand-off - a 3-D window
  waking where something disturbs the creek (H2, 25.13) - which is not built.
- **The current works through the shared surface.** Floating debris reads `WaterCoreManager::columnWater` - the same field
  list the renderer draws - so a column region's depth-averaged velocity is the debris current with no new code. Four 1/3 m
  wood blocks dropped at the head drift steadily downstream (frames at 5 / 30 / 58 s). Two caveats: they ride high on the
  water (the known bounding-sphere buoyancy law, 19.3 / cure M4), and `wet_body_list` lists only bodies inside 3-D volumes,
  so their speed was not measured numerically (a debris position route is the missing measurement).

### 25.26 The pond's features on the creek, current you can measure and tune, slightly murky water - DESIGN (owner, 2026-10-12)

**The owner:** "the directive is to focus on small bodies first and to use what we are building as foundation for all the
work following... if we have features that we have worked so hard to get right on ponds, then that work should be reflected
in the creeks. ripples and splashing are foundational work... drift speed will be important. we need to have solid methods
for tuning and measuring current speed. it will play a big role in appearance and function. I also think for testing
purposes water is too transparent... by default water should have a slight murkiness to it, not much though."

**Order of build:** M (murk) -> K (current: measure + tune) -> RC (ripples on column water) -> SC (splashes + displacement on
column water). Each red-before-green, each shown to the owner in motion.

**M - a slight murkiness by default.** Simulated water (`water_surface.frag` - ponds and creeks; the sea and the old CA keep
their look) with no per-body clarity gets a default Secchi depth of **3 m** instead of the derived ~10.8 m (a clear lake
reads 5-10 m, a typical creek or pond 1-4 m - Secchi depths, Carlson 1977 trophic index ranges; grounding at build). The
existing `water_look {clarity, turbidity, tint}` route stays the per-body tuning knob. Test M-T1: captures of the pond and
the creek at the default, 1.5 m and the old derived clarity, same poses -> the owner picks the default (3 m is the proposal,
not the decision).

**K - measuring and tuning the current.**
- *Measure:* `water_columns {probe: {x, z}}` -> depth, depth-averaged velocity (vector, speed), surface and bed at a point;
  `{profile: {x0, z0, x1, z1, n}}` -> n stations along a line: depth, speed, Froude number. Floats: a debug route
  `debris_position_log {path | stop}` drives the GPU solver's existing position readback (CSV); `tools/water_creek_current.py`
  turns it into each float's drift speed over fixed stretches and compares it with the column velocity along its path.
- *Tune:* the inputs Manning's law gives - flow Q (`spring` rate), roughness n (`water_columns {id, manning_n}`, live,
  clamped 0.01-0.15 as at create), slope and width (the bed). The probe echoes the predicted normal depth/speed beside the
  measured, so a tuning change is read as a number, not a guess.
- *Tests:* K-T1 on the creek bench at steady flow: the profile's mid-reach speed within 10 % of Manning (the solver was
  exact on the straight channel, 25.16/25.20); K-T2 floats' drift speed within 15 % of the depth-averaged velocity along
  their path (the debris law couples to that velocity); K-T3 changing n from 0.035 to 0.025 raises the speed by the
  Manning ratio within 10 % (the tuning knob does what the formula says).

**RC - ripples on column water.** Each column region carries a `RippleLayer` (the same class, the same 1/9 m world-aligned
lattice, film damping and breaking limit - 22, 22.12): mask = the region's wet columns; sources as on the pond - droplet
landings (the droplet pool's landing routes into a column region's layer when it lands there) and bodies crossing the
surface (22.3.2's high-pass, the column pitch 1/3 m as the grid's). **New, because the water moves:** before each layer
step the ripple field (both leapfrog levels) is ADVECTED by the column velocity under it (semi-Lagrangian, bilinear) -
waves ride the current, a ring drifts downstream at the water's speed. Drawn through the same atlas: `rippleLayers()`
returns the volumes' layers then the regions', parallel to `surfaceFields()`. Tests: RC-T1 on still column water a kick's
ring equals the pond layer's (the same class, bit-equal for the same lattice and kick); RC-T2 on a uniform current u the
ring's centre moves at u within 10 % and its shape is kept (the ring of RC-T1, translated); RC-T3 live on the creek bench: a
stone's entry rings visible (frame change >= the pond's 22.9 numbers) and drifting downstream; cost <= 0.3 ms per region.

**SC - splashes and displacement on column water.** A body crossing a region's surface: (1) DISPLACEMENT - the change of
its submerged volume each step becomes a column sink under its footprint and an equal source in the footprint's ring (water
pushed aside, mass-exact - the column form of 20's moving solids); (2) SPLASH - droplets born at the waterline from 21's
crown rule (speed from the entry velocity, count and size from the displaced volume, 21.2 / 21.11) into the SAME droplet
pool and renderer; per the owner's preference (0a: "splash particles would not 'take' from the volume of the pond. it should
jsut be a few splash particles for affect") creek droplets are COSMETIC - not taken from the column water, landing adds
nothing back (their landings still drive the ripple layer). The pond's droplets stay ledger-exact until the owner decides to
make them cosmetic too (logged). Tests: SC-T1 unit: a body pressed into column water raises the surrounding level by its
submerged volume / free area within 5 %, ledger exact; SC-T2 live: a stone into the creek - droplets born (> 0, in flight
<= 1.5 s), a crater and rings visible, the column ledger unchanged by the droplets.

**Design-check (2026-10-12).** *Voxel look:* M changes only a default of simulated water, captured A/B; RC draws 1/9 m
facets as on the pond (the owner signed those off, 22.12.2); SC uses the pond's droplets. *Chunks:* the ripple lattice is
world-aligned per region (as per volume, 22.5 - R-T6 / B-T4 cover it); advection reads the region's own column velocity,
no chunk input; probes and profiles read the region, not chunks. *Generation:* none (runtime). *API:* `probe` / `profile`
clamp n stations to [2, 512] and refuse points outside the region (said); `manning_n` live clamp [0.01, 0.15] with its
reason (as at create); `debris_position_log` writes only under the scratch / evidence directory it is given, refuses
otherwise; every route echoes its result. Defaults changing: the simulated water's default clarity (M) - no pinned test
reads it (the W1 no-regression capture pins the sea's `water.frag`, not `water_surface.frag`); verified by M-T1's captures.
*Tests:* as listed, on the creek bench (flat, empty, one creek - the owner's test world) and unit rigs; the rig is shipped
defaults except the creek's spring and outfall. **READY for M, K and RC; SC READY with the cosmetic-droplet rule stated.**

### 25.27 M built; RC amended - flowing water drives its own ripples (owner, 2026-10-12)

**M built (default murkiness).** `water_surface.frag`: a body's own clarity wins, else the simulated water's default Secchi
depth from the push block (`look1.w`), set by `water_render_core {default_clarity}` (0 = the old derived ~10.8 m; clamped
100 m; echoed). A/B sheets at the same poses, the creek running and the pond: `murk_creek.png`, `murk_pond.png` (old / 5 m /
3 m / 1.5 m). 3 m (the design's proposal) read clearly blue-green on the 7-11 cm creek at a low view - more than "a slight
murkiness... not much"; **5 m is the built default** (the gravel stays plain, the water tinted). The owner picks from the
sheets; the knob makes any value a one-line change.

**RC amended - the owner (2026-10-12):** "i would think that ripples should be default on for 'flowing' water. that would help
with appearance and function in one go right? like flow speed/rate is just a function that feeds into the already established
ripple feature?" **Yes.** Moving water roughens its own surface (bed-generated turbulence; standing waves where it speeds up
over steps and stones), so on column water the ripple layer is ON by default and gets a third source beside droplet landings
and bodies:
- **Flow forcing:** each step, a small impulse field over the wet columns - a deterministic hash of the world lattice cell and
  time (no chunk input, no randomness that differs between runs) - with amplitude set by the local turbulence: the bed shear
  velocity u* = u n sqrt(g) / d^(1/6) (Manning's friction, which the column solver already applies), stronger where the flow
  accelerates (the velocity's gradient along the stream - steps, stones). Still water gets none; a pond is unchanged.
- **Advection** (as designed): the field rides the current, so the texture's drift IS the surface speed - a visual current
  gauge, and the float tool (K) can read it.
- **Calibration, measured not guessed:** the forcing's constant is set so the facet slopes on the creek at its design flow
  stay inside the pond's signed-off range (22.11 / 22.12: slopes below the breaking limit, no black/white squares) and the
  texture is visible (frame change above the still creek's); recorded with the owner's look.
- Test RC-T4: still column water + forcing -> exactly zero (no flow, no ripples); RC-T5: at the creek's flow the slope
  histogram's 95th percentile <= 0.3 (inside 22.11's black onset at the demo view) and the frame change > the still case.

### 25.28 K built - the current measured and tuned on the creek bench (2026-10-12). Status: built, not signed off

**Built:** `water_columns {probe: {x, z}}` and `{profile: {x0, z0, x1, z1, n}}` (depth, velocity, speed, surface, bed, bed slope
along the flow, Froude, and each point's Manning prediction for its own unit discharge); live tuning `{id, manning_n}`
(0.01-0.15) and `{id, spring_rate}` (the springs' total, 0-10 m^3/s, proportions kept); the reply carries each region's
`manning_n` and `spring_total_m3s`. `debris_position_log {path | stop}` (paths under docs/evidence or the temp directory
only) drives the GPU debris position log; `tools/water_creek_current.py` (profile / floats / tune).

**Results (WaterBench_Creek, 100 L/s, steady flow):**
- **K-T1** mid-reach (x 25-85): speed 0.452 m/s against Manning's 0.456, depth 10.86 cm against 10.73, Froude 0.44. Unit
  (`WaterColumnTest.ProbeMatchesManningAndRoughnessTunes`, the straight 1:50 channel): 0.694 vs 0.696 m/s, 7.17 vs 7.15 cm.
- **K-T3** roughness 0.035 -> 0.050 live: speed ratio 0.821 against Manning's 0.807 (unit: 0.806 vs 0.807).
- **K-T2** four 1/9 m wood floats: 0.396 / 0.398 / 0.401 / 0.403 m/s over x 25-65 m, against the water's 0.452 - **88 %**
  (prediction within 15 %); steady, no float stopped. The gap is plausibly the float reading its voxel's 3 x 3 sub-columns,
  the slower edge water included.

**Three things found on the way, each fixed:**
1. **A 1/3 m float grounds in an 11 cm creek** (wood draws ~0.6 of its size: 20 cm) - it was shoved along the gravel, not
   carried; the tool's default float is 1/9 m (7 cm draft). Physics, not a defect.
2. **The position log's time base:** it writes a frame only when its GPU readback is ready (about one in three), so its
   frames' dt sum to a third of the run; the tool maps logged frames evenly onto the run's wall time.
3. **A defect in the debris water law: floats drifted at ~0.57 of the current.** The waterline (radiation) drag acted on the
   GROUND velocity, so a float riding a current was braked toward rest while the current coupling pulled it toward the
   flow: steady drift 3 / (3 + 2.3) = 0.57 of the current (live: 0.24 of 0.50 m/s). Now relative to the water, GPU
   (`solver_integrate.comp`) and CPU (`VoxelDynamicsWorld`) - the same correction 24.10 made for form drag. Pinned by
   `WaterBuoyancy.FloatDriftsAtTheCurrentsSpeed` (0.490 in a 0.500 m/s current; the old law's 0.57 is the computed red,
   not run). 333 water / buoyancy / debris / dynamics tests pass; the debris settle bench: the same verdicts as its baselines.


### 25.29 RC built - ripples on the creek, carried by the current and driven by the flow (2026-10-12). Status: built, not signed off

**Built:** every column region carries a `RippleLayer` window (384 x 384 lattice cells max = 42.7 m, clipped to the region,
following the camera in 4 m steps - `WaterCoreManager::setViewPoint`; `RippleLayer::shiftOrigin` keeps the field at its
world position across a move). Each frame: mask = the region's wet columns, velocity = the column velocity under each cell,
flow forcing (25.27) into the layer, then `RippleLayer::advect` and `step`. Drawn through the same atlas as the pond's
(`rippleLayers()` appends the regions' layers, `surfaceFields()` points each region field at its layer). Knob:
`water_ripples {flow_forcing}` (C_f, 0 = off, default **200**); the reply echoes it.

**Advection - two defects found by the red test (RC-T2, `WaterRippleTest.ARingRidesTheCurrent`), each fixed:**
1. Bilinear semi-Lagrangian sampling every frame (as designed) smeared the short ripples away: a ring on 0.5 m/s kept
   **0.17** of its energy over 2 s. Catmull-Rom sampling: 0.68.
2. Still a fractional resample every frame (0.075 cell at 0.5 m/s, 60 Hz). Now dt ACCUMULATES until the fastest wet cell
   has moved a whole cell (or 0.5 s) and the field moves once by the total - a uniform current is an exact cell shift.
   Result: moved **0.915 m** (u t = 1.000, inside 10 %; up to one cell of lag is the pending time), energy **0.979**
   (prediction 1 +- 0.15). A sheared current (0 at the banks, 0.45 m/s centre) costs ~20 % of the forced field's RMS
   against still water (scratch run).

**Calibration (WaterBench_Creek, 100 L/s, steady; `tools/water_creek_ripples.py`, `water_creek_bench.py --run`):**

| flow_forcing | peak height | facet slope p50 / p95 (bin upper edge) | max slope | ripple ms / frame |
|---|---|---|---|---|
| 0 (from rest) | 0 - layer asleep | - | 0 | 0.24 |
| 120 | 24 mm | < 0.05 / < 0.10 | 0.20 | 0.91 |
| 200 | 42-50 mm | < 0.10 / < 0.20 | 0.31 | 0.87-0.94 |

- **RC-T4 (still water -> zero):** forcing off, the layer sleeps to exact zero; forcing is u*^2-weighted, so a still region
  gets none (by construction, not yet a unit test of its own).
- **RC-T5 (p95 <= 0.3):** PASS at 200 (p95 bin < 0.20). Clips for the owner's look: `docs/evidence/water_core_h1/
  ripples_ff0.mp4` / `ripples_ff200.mp4` (bench camera; the ff0 clip starts 40 s after the 200 run, so it shows the field
  still decaying - 5 mm - not flat water). Frame-change metric not run; the visual verdict is the owner's.
- **Found:** the API clamped `flow_forcing` to 0-20 - every value above 20 in the first sweep ran at 20 and read as
  "saturation". Clamp now 0-2000 (a typo guard).
- **The heights are tall for the depth** (42-50 mm peaks on 11 cm water) because on the 1/9 m lattice the visible slope and
  the height are tied (rms slope / rms height ~ 9 /m at every forcing - measured; high-pass forcing and an extra long-wave
  damping were both tried offline and changed it by < 25 %, so neither shipped). Whether that reads as heave is for the
  owner's look.

**Cost - over the 0.3 ms target (logged, not hidden).** Offline (no engine running), per 60 Hz frame on the bench's window
(384 x 54 cells, 6912 wet): 3.2 ms as first built. Three exact rewrites of `RippleLayer::step` (same numbers - all 44
ripple / wave / coupling / droplet / buoyancy tests pass unchanged): row-wise tap loops the compiler vectorises; ONE
convolution per substep for the field and the impulses (K is linear - `next = c0 r - c1 prev - K * (c2 r + c1 ds / rho I)`;
kinematic cells keep their exact form via `convolveAt`); and the kernel's dx / dz symmetry folded (7 row passes of 7 taps
instead of 13 x 13). Now: convolution 0.39 ms, breaking limiter 0.21, zero-mean 0.12, the rest ~0.25 -> **~0.9 ms per frame
live**. The plan's fix is H1b (the column tier on the GPU): the ripple step goes with it. No SIMD flags in the build (SSE2
only); /arch:AVX2 is a project-wide decision, not taken here.

**Not built yet (RC remainder, moves to SC):** RC-T3 - a stone's entry rings on the creek (body-crossing and droplet-landing
sources into region layers), and the forcing's "stronger where the flow accelerates" term. RC-T1 holds by construction
(the same class and lattice), no separate test.

**Open, not from this work:** `WaterCouplingTest.BlastBesideThePondMovesItAndItSettles` is ORDER-DEPENDENT - alone it
reads a near-side drop of 0.287 m (passes >= 0.28); after `DamageSystemBreakProfileTest` loads `resources/materials.json`
(Stone's real toughness 110) it reads 0.222 m and fails. Its threshold was set on the empty material registry; the test
should load the registry itself and re-derive its prediction.

### 25.30 SC built - stones in the creek: displacement, rings, a cosmetic splash (2026-10-12). Status: built, not signed off
> **Removed in 26.27:** the region load and the cosmetic crown (`creek_splash`, the `cosmetic_*` stats); pond water's displacement and real droplets replace them.

**Displacement - realised as a LOAD, not the design's sink-and-ring move.** The design (25.26) moved each step's change of
submerged volume from the footprint to its ring. That shoves water once and lets it flow straight back; a body that stays
would hold no level. In the shallow-water momentum balance a load s pressing on the surface enters exactly as a bed bump
does (-g d d(b + s)/dx), so `ShoreSolver::setSurfaceLoad` hands the fluxes bed + s and eta + s while the mass stays the
water's own depth: hydrostatic reconstruction keeps it exactly balanced at rest, the water under a body stands s lower and
the free surface eta + s is level - held while the body stays, the ledger never sees it. Same contract (SC-T1), mass exact.
- `WaterCoreManager::setMovingSolids` rasterizes each DISPLACING body (the pond's M2 gate: moving > 0.6 m/s, hysteresis
  0.3) onto the region's columns: load = overlap fraction x the body's span between the bed and the free surface of the
  RING of wet columns around its footprint. Entering applies at once (the crater); leaving or coming to rest releases over
  `kLoadReleaseSeconds` 0.3 s (a stone settling on the bed does not throw back the water it held as a wave).
- `ColumnWater::publish` draws the free surface eta + s and flags the column `bodyAtSurface` (buoyancy reads the water
  around a body, as on the pond). The creek regions' boxes join the pond boxes for the debris integrator's wet-body report.

**Rings:** a body crossing a region's surface drives its ripple window with the pond's sharp-edge rule (22.3.2), 3
lattice cells per 1/3 m column. **Splash (cosmetic, the owner's 0a preference):** a body crossing downward faster than
0.5 m/s throws `creek_splash` (default 0.05) x its displaced volume rate as droplets from its waterline - up at 0.8 x its
speed, out at 0.4, plus the current (look parameters, not measured); the count follows the volume (one droplet per
droplet volume, the remainder by a hash). They fly in the shared pool with a negative volume id, land on the region's free
surface or ground, put their momentum into the creek's ripple window and carry NO water: `water_droplets` reports them as
`cosmetic_*`, and its `in_flight_m3` (the ponds' ledger) excludes them. Knob: `water_droplets {creek_splash}` (0 = off).

**Tests and results:**
- **SC-T1** `WaterColumnTest.APressedBodyRaisesTheLevelByItsVolume` - a 1 x 1 m load 10 cm thick pressed over 10 s into a
  closed 10 x 2 m channel of 20 cm water: rise **5.01 mm** against V / A_total = 5.00 (the body does not reach the bed, so
  the whole area counts - the first prediction, V / A_free = 5.26, was mine and wrong); the free surface over the body
  level with its surroundings to 0.1 mm; water volume change 2e-13 m^3; control (no load) 0. Settles 600 s: Manning
  friction is weak at mm/s, the slosh shrinks from 2.4 to 0.2 mm over ten minutes (measured).
- **SC-T2** live, WaterBench_Creek at 100 L/s, a 1/3 m Stone dropped 1.5 m (`tools/water_creek_splash.py`; flow forcing 0 so
  the rings are the stone's, after 40 s of quiet): **31** droplets (prediction 5-30 - the crossing lasts longer than one or
  two frames), all landed within **0.91 s** (<= 1.5); kinematic ripple cells 36 during the crossing only; crater - the
  water depth at the stone's column 9.7 -> 7.8 cm, refilled by ~1.9 s; load peak 7 cm, gone by 2.7 s; ledger error
  unchanged (6.0e-8 -> 6.7e-8 m^3, the run's own float drift). Clips: `splash_stone_ff0.mp4`, `splash_stone_ff200.mp4`
  (the shipped default forcing).
- **Stress** - 20 stones (1/3 and 1/9 m, 1-3 m drops, along 57 m of creek) at once: load cleared everywhere (0 at the end),
  worst load 8-15 cm (no runaway), ledger error drift 2e-8 m^3, 317 droplets born = 317 landed, ponds' water in flight 0.

**Four defects found on the way, each fixed:**
1. **The load ratcheted up to the stone's top.** Measured against its own column's free surface (water + load), each
   frame's load lifted the surface it was read against: water drawn 23 cm up a stone in 11 cm of water (flat white sheets
   around it). Now read against the footprint's ring.
2. **Foam squares.** The solver's surf foam (built for the shore, G - not signed off) read the water's own surface, so a
   loaded column was a 10 cm bore step - and, once fixed to the free surface, the crater's refill still tripped the
   converging / fast-flow rules: a flat white 1/3 m square beside the stone for a second. No foam is now MADE within one
   column of where a body pressed in the last 3 s - the pond makes no whitewater on an entry either (its S4 whitewater is
   not wired, 21.4); splash whitewater, when wanted, gets designed for both.
3. **Droplet count blind to volume** - 8 directions every frame: ~60 per stone, as many for a 1/9 m pebble as for a 1/3 m
   stone. Now one per droplet volume.
4. **Two droplets per birth** - the pool splits ceil(V / v_d) and v_d's own float rounding gave 2 (706 landed for 353
   born). A birth now carries 0.999 v_d; births are counted as the droplets the pool actually spawned.

**Cost (logged, over budget like RC):** the ripple step peaked at **4.2 ms** during the 20-stone impact (kinematic cells and
the breaking limiter on many steep cells at once); ~1 ms otherwise. The fix is H1b's GPU step.

**Found, not this work:** a standing foam spot on the bench creek at x 76.5 (foam 1.0 in steady flow, before any body) -
the shore foam rules firing on the creek; whether the creek should foam at all is for the owner (not changed).
**Not built:** a body that enters FRESH inside a creek (spawned there) presses its load at once - the pond's no-rate rule
for fresh bodies (20.7) is not mirrored on columns. Resting bodies do not obstruct the flow (they stop displacing - M2's
gate - and the beds are static occupancy only).

### 25.31 H1b-M0 measured - where the creek's time goes (2026-10-12)

Offline, Release, engine stopped; diagnostics `WaterColumnTest.DISABLED_CostColumnTier` and
`WaterRippleTest.DISABLED_CostRippleWindow` (run with `--gtest_also_run_disabled_tests`).

**The column tier costs the BOX, not the water** - ~180-200 ns per column per step, wet or dry (a 1:200 creek 2 m wide,
100 L/s, banks dry, 120 s of run-up - the flow had reached ~66 m, 1200 wet columns, in every case):

| Region box | Columns | Wet | ms / step | ns / column | ns / wet column |
|---|---|---|---|---|---|
| bench, 100 x 6 m | 5,400 | 22 % | 1.10 | 203 | 913 |
| wide, 100 x 24 m | 21,600 | 5.6 % | 3.91 | 181 | 3,257 |
| long, 200 x 6 m | 10,800 | 11 % | 2.07 | 192 | 1,725 |
| long + wide, 200 x 24 m | 43,200 | 2.8 % | 7.66 | 177 | 6,379 |

A meandering creek needs a wide box, so the dense box alone overruns the tier's 0.6 ms (25.10). Stepping only the wet
columns and their neighbours (H0's planned "sparse wet set", 25.17 item 1) projects to ~1200 x ~200 ns = **~0.24 ms** for
the bench creek - a CPU change, before any other optimisation (the solver also allocates its per-line work arrays on every
line of every direction of every stage).

**The ripple window costs ~160-210 ns per WATER cell per 60 Hz frame, and its dry cells are not free either:**

| Window | Cells | Water | ms / 60 Hz frame | ns / water cell |
|---|---|---|---|---|
| bench creek 384 x 54 | 20,736 | 33 % | 1.47 | 213 |
| creek meandering across 384 x 384 | 147,456 | 4.7 % | 7.71 | 1,115 |
| full 384 x 384 (pond-sized) | 147,456 | 100 % | 24.1 | 163 |

Even with dry cells skipped perfectly, one creek's 6,912 water cells cost ~1.1 ms on the CPU - over its 0.3 ms target -
and the work is a 13 x 13 convolution, the GPU's kind of work (the pond's ripple layers would gain the same).

**What this says for H1b (to be designed and checked before building):** the column tier's fix is SPARSITY on the CPU
first (cost follows the water); the ripple step's fix is the GPU. The original plan ported both to the GPU.

### 25.32 H1b part 1 design - the column tier costs its water, not its box (owner, 2026-10-12: "ok" to the split of 25.31)

**The change.** `ShoreSolver::rates` - the reconstruction, the HLL faces and the bed-slope source, nearly all of a step's
work - is computed only where it can be non-zero. Per line (x lines, then z lines) a cheap scan flags the cells that HOLD
water (depth > 0 - not the 1 mm dry threshold: a film below it still has mass and its face fluxes are not zero). A face is
computed when either side holds water; a cell is reconstructed when it or a line neighbour holds water. Everything skipped
would have added exactly zero: a face between two empty cells has both hydrostatic depths 0 (HLL returns 0 and the
pressure terms are 0), and an empty cell's bed-slope source is multiplied by its depth 0. The flags are taken per CALL of
`rates` (each Heun stage): a dry cell beside water can wet in stage 1 and must have its faces in stage 2. The stage
updates, friction, prescriptions and foam stay dense (a few ns per column - measured after, not assumed). The per-line work
arrays (`plus`, `minus`, `wetc`), allocated today on every line of every direction of every stage, become member scratch.

**Why not a persistent active list:** the per-call scan costs ~1 ns per column, is exact by construction and has no state to
go stale when beds change (`setSolids`, edits) or water is placed directly (tests, springs); a list would add all three
failure modes for a saving the measurement will show is not needed (if it is, it is logged with the number).

**Contract and tests.**
- **H1b-T1 (red-before-green: equality).** `ShoreSolverTest.SparseRatesEqualDense`: the bench creek's geometry (25.31's
  case, a 4x wide box) run 60 s with the sparse rates and with a forced-dense reference (`setDenseRatesForTest(true)`):
  every column's eta, u, w, foam BIT-identical (the skipped terms are exact zeros; IEEE signed zeros cannot change a sum
  that is then added to a depth). Also a wet/dry swash case on the beach run-up geometry of
  `ShoreSolverTest.RunUpOnTheBeachVsHunt` - the flags' worst case, water advancing onto dry ground every substep, and the
  shore tier's (`ShoreBand`) solver. **The red:** the equality passes trivially before a sparse path exists, so the red is
  a known-wrong flag rule - "holds water" = depth > the 1 mm dry threshold - which MUST fail the swash equality (a film
  below 1 mm still sends flux), shown failing before the correct rule (depth > 0) is shown passing.
- **H1b-T2 (cost).** The 25.31 diagnostic rerun: prediction - the bench creek <= 0.35 ms per step, and the wide and long
  boxes within 1.5x of the bench (cost follows the water); the dense reference reproduces 25.31's numbers (the control).
- All existing ShoreSolver / ShoreBand / WaterColumn / creek tests unchanged; live: the creek bench's `step_ms` falls and
  its K-T1 profile (speed and depth vs Manning) is unchanged to the printed digits.

**Design keys.** *Voxel look:* no change (bit-identical). *Chunks:* none - a region is world-aligned and its lines are its
own; no chunk input. *Generation:* none. *API:* no new route; `setDenseRatesForTest` is a test hook, not exposed. *Defaults:*
none change. *Test world:* the unit rigs above (one variable: sparse vs dense; control: dense reproduces 25.31), then the
creek bench (shipped defaults except the creek's spring and outfall). **Design-check 2026-10-12: NEEDS WORK (the red was
not red; the shore tier's solver was not in the equality) -> both folded in above -> READY.**

### 25.33 H1b part 1 built - the column tier works where the water is (2026-10-12). Status: built, not signed off

**Built, in two steps (both measured, the second because the first missed its prediction):**
1. **Sparse rates** (25.32 as designed): per line, a cell holding water (depth > 0) or beside it is reconstructed and a
   face with water on a side is computed; per-line work arrays kept as members. Cost 1.10 -> 0.41 ms on the bench creek -
   but the wide box still 3.91 -> 1.16 ms: ~50 ns per box column remained in the DENSE loops (stage updates, CFL,
   prescriptions, foam, the flag scans).
2. **An active set of tiles** (`ShoreSolver::buildActive`): 4 x 4-column tiles holding water, a body load or a
   prescription, dilated by ceil((2 n + 1) / 4) tiles for n substeps (water moves at most two columns per substep: stage 1
   wets a dry neighbour, stage 2 that one's) - no water can reach an inactive column within a step, so every term there is
   an exact zero. Every loop of the step runs over the active columns in ascending index (the sums stay bit-identical);
   the per-line work over the active tile runs. The foam quiet zone (25.30) is now stamped (`m_lastLoadT` vs a clock) so
   idle columns need no update. Tile size MEASURED: 16-column tiles plus their margin woke 60 % of a wide box around a 2 m
   creek (1.12 ms); 8 -> 0.65; **4 -> 0.55 ms** (kept).

**Results.**
- **H1b-T1** `ShoreSolverTest.SparseRatesEqualDense`: swash 0 of 360 columns differ, creek (100 x 24 m box) 0 of 21,600 -
  bit-identical; **the red**: the 1 mm flag rule changes 10 swash columns (caught). All 301 Shore* / Water* tests pass.
- **H1b-T2** (`WaterColumnTest.DISABLED_CostColumnTier`, the same 1,200 wet columns in every box):

| Region box | before (25.31) | after | active columns |
|---|---|---|---|
| bench 100 x 6 m | 1.10 ms | **0.40** | 3,328 |
| wide 100 x 24 m | 3.91 | **0.55-0.65** | 3,328 |
| long 200 x 6 m | 2.07 | **0.52-0.60** | 3,328 |
| long + wide 200 x 24 m | 7.66 | **0.65-0.70** | 3,328 |

  Against the prediction (bench <= 0.35 ms; every box within 1.5x of the bench): **missed** - bench 0.40, the big boxes
  1.3-1.75x. Profiled: the active-set scan is the box-proportional part as designed (0.02 ms bench -> 0.12 ms on 43,200
  columns); the rest is MEMORY - the z lines stride a whole box row per cell, so the same 3,328 active columns cost 0.37
  ms of substeps in the bench box and 0.51-0.55 ms in the wide ones. Packing the active columns into a compact local
  grid would remove it (a larger change - logged, not done).
- **Live** (WaterBench_Creek, 100 L/s, 150 s after the start - still filling): `step_ms` **0.52-0.61** (was 0.88-1.1 on this
  creek), ledger exact; the profile against Manning on the same run: speed 0.436 vs 0.432 m/s, depth 10.10 vs 9.96 cm.
  (K's 0.452 m/s / 10.86 cm were at steady flow; this run had not reached it - the unit equality is the proof the physics
  did not change.)

**Where the tier stands against 25.10's 0.6 ms:** one bench creek fits (0.4-0.6 ms); several creeks do not yet - the
compact layout, or the GPU after the ripple port, is the next lever, decided on measurements then.

### 25.34 H1b part 2 design - the ripple step on the GPU (owner, 2026-10-12: "get it done")

**What moves.** `RippleLayer`'s per-substep math - advection (Catmull-Rom gather), the combined field c2 r + c1 ds / rho I, the
13 x 13 convolution, the leapfrog update (rim absorption; kinematic cells with their own K * r), the zero mean per connected
body, and the breaking limiter on both levels - runs as one compute shader with modes (`shaders/wc_ripple.comp`), every
layer of a frame in ONE submission (`RippleGpu`, owned by `WaterCoreManager`, created by `initGpu`). **What stays on the
CPU:** the bookkeeping - the fixed-step accumulator, the advection accumulator, the mask and component labels (recomputed
only when the mask changes), sleep, impulses and kinematic cells as the sources write them, every stat. `RippleLayer` is
split into `planStep` / `planAdvect` / `finishStep` so the CPU step and the GPU step share that bookkeeping word for word.

**Asynchronous, one frame late.** The water core's queue is the GRAPHICS queue (`Application.cpp` `initGpu`): a fenced wait
there would block the CPU behind the previous frame's rendering every frame. So frame N records and submits without waiting;
frame N+1 first collects N (its fence has had a whole frame), copies each layer's new heights into the layer, then runs
its sources and submits again. Staging is double-buffered (inputs and the readback per slot). The drawn heights are one
frame (16 ms at 60 Hz) behind the sources - below what the eye resolves on a ripple. Only r is read back each frame;
the previous level is downloaded on demand (blocking, rare) before a CPU-side mutation that needs it (`shiftOrigin` of a
creek window, `pin`); after any CPU mutation the layer is marked dirty and its levels re-uploaded before the next step.

**Exactness of each rule on the device.**
- State: four level slots in one buffer, rotated by push-constant offsets (no copies).
- Zero mean per body: cells sorted by label on the CPU when the labels change; one workgroup per 256-cell chunk sums both
  levels (shared-memory tree), one thread per body sums its chunks in order, then every cell subtracts its body's mean -
  deterministic (no float atomics), summed from the same pre-state as the CPU.
- The limiter's gate is exact: a pass runs only if some edge exceeds the limit + 0.1 mm (the CPU's `any`), then moves every
  edge past the limit; delta is GATHERED per cell from its 8 edges (Jacobi on a snapshot, as on the CPU). 16 passes per
  level are recorded as INDIRECT dispatches: each pass writes the next pass's group count (0 once a pass finds nothing), so
  a gentle field costs one detection pass. The broken height is summed per workgroup and folded on the CPU.
- Parity, not bit-equality: the device sums in another order (float). Tolerance below.

**Tests.**
- **H1b-T3 parity** (`tests/integration/WaterRippleGpuParityTest.cpp`, Vulkan fixture, SKIP without a device - never a
  pass): the same layer stepped on the CPU and the GPU through the shared bookkeeping; max |r_gpu - r_cpu| <= 1 % of max
  |r_cpu| after: (1) a kick's ring in a still 6 x 6 m pond, 2 s; (2) a wall between two bodies (labels + zero mean); (3)
  the breaking limiter's spike-and-step rig (B-T1's) - the gate in action; (4) a ring on a 0.5 m/s current (advection);
  (5) a body crossing the surface (kinematic cells); plus the broken height within 1 %. **Red:** the parity run with the
  limiter's gate removed (all 16 passes applied) must FAIL case (3) - the test can see a wrong limiter.
- **H1b-T4 cost** (diagnostic in the parity file): the three 25.31 windows (bench creek 384 x 54, meander 384 x 384, full
  384 x 384) - CPU wall per frame (record + upload + collect) and GPU time (timestamps). Prediction: CPU <= 0.3 ms for the
  bench creek window and <= 1 ms for the full one; GPU time <= 0.3 ms for the full window.
- **Live (L4):** the creek bench and the pond bench - `water_ripples` `ms` before/after, the RC / SC clips re-shot at the same
  pose; `water_ripples {gpu: bool}` is the A/B switch (reply echoes `backend`).

**Design keys.** *Voxel look:* unchanged (parity); 1/9 m facets as signed off. *Chunks:* none - layers are world-aligned
lattices (22.5); the GPU sees the same lattice. *Generation:* none. *API:* `water_ripples {gpu}` (bool; omitted =
unchanged; the reply echoes `backend: "gpu" | "cpu"` and why the GPU is off when it is - no device, init failed); default
GPU when the device initialised, CPU otherwise - no pinned test reads the backend (the unit tests construct layers
directly: CPU). *Test world:* unit-scale layers in the parity test (one variable: the backend; control: CPU vs CPU is
exact), then the existing benches at shipped defaults. **Design-check 2026-10-12: READY** (the gate questions answered
above; the queue stall was the one design risk and is the reason the step is asynchronous).

### 25.35 H1b part 2 built - the ripple step on the GPU (2026-10-12). Status: built, not signed off

**Built.** `RippleGpu` (engine/{include,src}/core/water/RippleGpu.*) + `shaders/wc_ripple.comp` (advect, combine, the 13 x 13
convolution, update with kinematic cells, the zero mean per body) + `shaders/wc_ripple_limit.comp` (the breaking limit).
`RippleLayer::step` / `advect` are split into `planStep` / `planAdvect` / `finishStep` (+ `sleepRule`), so both backends run
the same bookkeeping. `WaterCoreManager::stepLayer` picks the backend per layer; `water_ripples {gpu}` is the A/B switch
(the reply echoes `backend`, `gpu_layers`, `gpu_min_cells`, `gpu_ms`, `gpu_wait_ms`, `gpu_record_ms`, `gpu_error`).

**Three things the measurements changed against the design (25.34):**
1. **The limiter as 16 indirect passes cost the GPU 0.69 ms** on the bench window (128 mostly-empty dispatches with a barrier
   each). Now ONE dispatch per level: 16 x 16 core tiles with a 16-cell halo in shared memory run all 16 Jacobi passes - an
   edge's correction moves one cell per pass, so the core sees exactly the global passes; the pass gate is the tile's
   (measured: the gate spares only edges within 0.1 mm of the limit, a 1e-6 m difference).
2. **One frame of slack was not enough.** The queue is the graphics queue: collecting last frame's submission waited 0.8-1.7
   ms live behind the rendering. The device state chains on the device, so up to THREE submissions are now in flight (a
   ring with per-slot staging and readback); collect consumes whatever has signalled and waits only when the ring is full
   (measured wait 0.000 ms live). Results of a layer the CPU changed meanwhile (pin, window shift, sleep) are dropped.
3. **Small layers are cheaper on the CPU** - the pond bench's layer: 0.137 ms CPU vs 0.238 ms GPU (the GPU's fixed per-frame
   cost); the creek's window: 0.57 vs 0.35. A layer steps on the GPU from 8,192 cells (`kRippleGpuMinCells`); the pond's
   signed-off layers stay on the CPU path unchanged.
Plus two CPU-side cuts the live profile found once the step itself was on the GPU (mask + forcing 0.49 ms, slope stats
0.15 ms per frame): the creek's mask, velocity and forcing amplitude are computed per 1/3 m COLUMN and spread to the lattice
by integer index, the impulses go in as one field (`RippleLayer::addImpulseField`, two separable 1 2 1 / 4 passes = the
per-cell tent away from the edges - `WaterRippleTest.ImpulseFieldEqualsPerCellImpulses`, 1.8e-11 m), and the shape stats
(max |r|, slope histogram) are computed when the route asks (`refreshRippleShapeStats`), not every frame.

**H1b-T3 parity** (`WaterRippleGpuParityTest`, Vulkan; max |r_gpu - r_cpu| against max |r_cpu|): ring in a still pond
0.002 %; a wall between two bodies 0.001 %; the breaking limit 0.000 % with the broken height 0.90613 vs 0.90613 m^3; a
ring on a 0.5 m/s current 0.002 %; a body crossing the surface 0.001 %. **Red:** the limiter's gather dropping the
backward edges leaves parity by 37.9 % (broken 1.450 m^3). (The first red tried - the gate removed - moved the field 1e-6 m:
below the test's 1 %, so not a red; recorded so it is not tried again.)

**H1b-T4 cost** (`DISABLED_CostRippleWindowGpu`, offline, per 60 Hz frame; CPU wall = record + collect, the wait apart):

| Window | CPU step | GPU path: CPU wall | device |
|---|---|---|---|
| bench creek 384 x 54 (6,912 water) | 0.87-0.90 ms | **0.12** | 0.15-0.21 |
| meander 384 x 384 (6,912 water) | 5.5-7.0 | **0.56-0.64** | 0.38-0.50 |
| full 384 x 384 (147,456 water) | 21.4-22.2 | **0.71** | 0.94-1.78 |

Against the predictions (CPU <= 0.3 ms bench, <= 1 ms full; device <= 0.3 ms full): CPU met both; device met on the bench and
MISSED on the full window (0.9-1.8 ms - the timestamps also count queue time ahead of the submission; a 147k-cell window is
a pond far larger than any built). **Live** (WaterBench_Creek, 100 L/s, forcing 200): ripple step **0.35-0.37 ms** per frame on
the GPU (device 0.10-0.15 ms, wait 0) against 0.57 ms on the CPU after the same CPU-side cuts (1.0-1.2 ms before them); the
same field statistics (peak 47-48 mm, max slope 0.33-0.36, the same histogram within noise). The SC stone on the GPU: 43
droplets, kinematic cells 36, ripple peak 74 mm (CPU runs 72-77), ledger unchanged; clip `splash_stone_gpu_ff0.mp4` clean.
The pond bench's pebble pair (now CPU by the size rule) - GPU forced: 0.24 ms, peak 18.7 mm; CPU: 0.14 ms, 18.9 mm.

**Against the 0.3 ms per region target (25.26):** 0.35-0.37 ms live - close, not met; the remainder is the creek's per-cell
mask / forcing loop (moving the forcing's hash and spread onto the GPU would take it, logged).

**Found, not this work:** `WaterCoreGpuParityTest.GpuFlipBulkParityAndRunup` - the GPU FLIP run-up reads 1.18 h0 against
its 1.22 gate (the open parity row of 15.15; the FLIP path is untouched here).

### 25.36 The dam & pond bench - DESIGN (owner, 2026-10-12: chosen over H2 for now)

**Why before H2.** Measured against H2 (25.13): the bench creek is ~10 cm deep - at the 3-D core's 1/3 or 1/9 m cells less
than one cell of water (1/27 m: 3 cells, ~5-10 ms per window on the CPU); the creek tier flows on the generator's bed line
(25.21) while a window sees the 1/3 m voxel staircase (it pools: the window's level would not match the creek's at its
edge - the WS-4 seam - unless the window's floor is built from the bed line); the GPU 3-D core has no open boundary to feed
a window (the CPU core has prescribed columns, 18.2). And what the owner's brief asks first - a creek filling a pond,
damming, levels rising and spilling - is the column tier alone when the basin is inside the region. H2 stays for local
3-D detail (waterfalls, wading, a dam bursting), with these three findings as its starting point.

**The bench (WaterBench_Creek, extended - `tools/water_dam_pond.py`).** The creek (25.24) carved on through x 110-111 into a
6 x 6 m pond (x 112-117, z 29-34) with its floor at y 14.0; a spill channel on the far side (x 118-127, z 31-32, floor y
15.0 - the pond holds 1 m before it spills) to an outfall at the slab's edge (x 127). One column region over x 10-127, z
27-36, y 13-17 (the sparse tier, 25.33, pays for the dry ground). Spring 0.2 m^3/s at the head (normal depth ~16 cm: inside
the banks). The DAM is a world edit through the world API (subcubes, Stone) across the creek at x 60, its crest 1/3 m above
the bed and below the banks - the column region re-reads its beds when the solids change (`occRevision`), and the bed rule
puts the bed on the dam's top there (25.22: the line is the bed only where the column is the carve).

**Contract and tests (predictions written before the first run).**
- **DP-T1 fill:** once the creek delivers (out of the creek = in), the pond level rises at Q / A = 0.2 / 36 = 5.6 mm/s
  within 10 % (from 0.2 to 0.8 m of depth); it reaches the spill (15.0) in ~3 min; then the outfall carries the inflow -
  out/in 1.00 +- 0.02 over the last 30 s.
- **DP-T2 dam:** placed at steady state: the inflow to the pond falls below 10 % of Q within 20 s; the pond drains to its
  spill level and the outfall falls toward 0; the pool behind the dam rises to the crest and overtops; at no time does the
  column ledger move (|error| < 1e-5 m^3).
- **DP-T3 break:** the dam removed: the pond's inflow rises ABOVE Q for a while (the pool's water released - a surge); the
  ledger unchanged.
- **Edits never create water:** the stored volume immediately before and after each edit equal within 1e-6 m^3 (water on a
  dam's columns is lifted onto its crest, not deleted; on removal it drops with the bed).
- **Visual (L4):** a clip from a camera over the pond and the dam: the pond filling, the dam pool rising and spilling, the
  break's surge. The owner's look.
**Design keys.** Look: unchanged (the creek's water as signed off so far, built not signed off). Chunks: the region is
world-aligned; edits reach it by the solids revision, not by chunk. Generation: none (a bench). API: existing routes only
(`/api/world/*` for the dam, `water_columns`). Test world: the existing flat creek bench, one region; the variable per test
is the one edit; controls - the ledger and the stored volume across each edit. **Design-check: READY** (no engine change
is planned; any change the runs force is designed and checked on its own).

### 25.37 Found by the dam & pond bench: a pond overtopped its crest by 0.41 m before it spilled - fixed (2026-10-12)
> **Removed in 26.27:** its regression test (`APondSpillsWhenItTopsItsCrest`, a column-region rig); the reconstruction fix stays in `ShoreSolver`.

**What the first run showed.** The pond rose to 15.5 - 0.4 m over its spill crest (15.13) - before any water left; the
spill channel stayed dry for ~60 s with a 0.37 m head behind it. Reproduced offline on the bench's geometry
(`ShoreSolverTest.APondSpillsWhenItTopsItsCrest`), sparse and dense identical (so not 25.33): the channel's FIRST column
filled to 0.38 m and stood still (u 0.01 m/s) beside a dry column on the same bed for 90 s.
**Cause.** `ShoreSolver::rates` extrapolates depth and surface to a face with SEPARATE limited slopes and takes the face's
bed as their difference. Beside a bed step (the pond's 1.1 m drop) the depth slope is mostly the step: at the column's
outflow face it gave a bed of 15.49 - 0.36 m above both cells' beds (15.13) - and the face passed a 5 mm film.
**Fix.** A face's reconstructed bed is clamped between the two cells' own beds (the depth then follows from the surface,
never negative). On a smooth bed the reconstruction is already inside those bounds, so nothing else moves: all 303 Shore* /
Water* tests pass unchanged (Manning 0.0717 m / 0.694 m/s as before; sparse = dense bit for bit).
**Red -> green** (water past the crest column, 1 cm deep): without the clamp the pond was **0.382 m** over the crest; with it
**0.015 m** (prediction <= 0.05). (The test's first metric - water at the outfall 10 m on - also counted the front's trip
over the dry channel while the pond kept rising: 0.12 m; changed before it was used as a gate.)
**Where else it mattered:** every place the column tier meets a bed step - a pond's rim, the creek's 1/3 m risers off the
carve, the voxel staircase (25.16's pools), shoreline steps. The H0 staircase numbers predate this fix.

### 25.38 The dam & pond bench built and run (2026-10-12). Status: built, not signed off

`tools/water_dam_pond.py --build` / `--run --record` on WaterBench_Creek (25.36's world and region, 0.2 m^3/s, the shipped
defaults otherwise). Two runs after 25.37's fix, the same numbers to the third digit; `dam_pond.json`, clip `dam_pond.mp4`
(the pond filling and spilling, the dam's pool, the break - the owner's look).

| | prediction (25.36) | measured |
|---|---|---|
| DP-T1 pond rise (0.2 - 0.8 m of depth) | Q / A = 5.56 mm/s +- 10 % | **5.43 mm/s**; the flow arriving at the pond 0.193 m^3/s (its own Q/A 5.36 - mass consistent) |
| spill | at the crest | water past the crest column with the pond **1.8 cm** over the crest (15.128) |
| DP-T1 spilling | out/in 1.00 +- 0.02 | **0.980** over the last 30 s (still settling) |
| DP-T2 dam | the pond's inflow < 10 % of Q within 20 s | **missed - the prediction was wrong**: 100 % at 20 s, a dip to **46 %** at 78 s, back to 82-94 % as the pool overtops |
| the dam's pool | to the crest and over | 15.83 = the crest (15.67) + 0.16 m - the weir head of 0.2 m^3/s over 2 m (~0.15 m) |
| DP-T3 break | the inflow above Q | **0.34-0.35 m^3/s** (1.7-1.75 Q), decaying over ~2 min |
| ledger | never moves (< 1e-5 m^3) | max **1.6e-6** m^3; unchanged across both edits |

**Why the dam prediction was wrong (the physics, not the solver):** the 50 m of creek below the dam keeps draining, and
the dam's effect travels down it as a kinematic wave at ~5/3 of the flow speed (~0.8 m/s): the pond feels it after ~60 s
(the minimum came at 78 s); the dam is LOW (1/3 m), so its pool (~20 m^3 along a ~60 m backwater) fills in about two
minutes and overtops. A 20 s cut-off needs a short reach below the dam or a dam that is not overtopped - the bench shows a
creek's real response instead. **The first run's 7.9 mm/s rise** came with the 25.37 defect (the pond overshot its crest,
so the creek's own storage drained into it on the way); after the fix the rise matches Q/A within 2.3 %.
**The tool's own defect:** the first recording was killed rather than closed - an mp4 without its index (64 MB unreadable);
ffmpeg is now stopped with 'q'.

### 25.39 H2 design - a 3-D window inside a creek (owner, 2026-10-12: "lets get to h2")
> **Removed in 26.27:** the windows (`openWindow` / `closeWindow`, `Av::window`, `Av::floorY`) - the creek is one pond-water volume (26.14).

**Scope - H2a, the hand-off done right on the CPU.** A T1 window (a CPU 3-D volume, 1/9 m cells) placed over a box of a
creek region's columns: seeded from them, exchanging water with the creek through its edge MASS-EXACTLY, no visible seam,
written back when it ends. **H2b, later and separately designed:** the window on the GPU (its hand-off ported to the
kernels - the GPU core has no open boundary today), finer cells for shallow water, the log velocity profile, windows opened
automatically around disturbances (a stone, a wader, a breach, a fall). The 25.36 findings decide the shape below.

**1. Who owns the water (exact by construction).** The region marks the window's columns HELD. Outside them T2 is the truth;
inside, T1. Each frame, in order:
1. *Ghosts:* each held column's state is set from T1 - depth = the window's water over that column (its 3 x 3 sub-columns'
   fills x cell volume / column area), velocity = the fill-weighted mean of the cells' horizontal velocity; surface =
   the column's bed + depth.
2. *T2 steps* with the held columns frozen INSIDE: a face between two held columns carries nothing (`ShoreSolver` skips
   it); a face between a held and a free column is computed as any other. So a held column's change of mass over the step
   is EXACTLY what crossed the window's edge there - `transfer_c` (+ into the window).
3. *T1 receives it:* `transfer_c` > 0 is added to the window at that column (into its topmost water, upward, at the column's
   cells); < 0 is taken from that column's water, topmost first. The ghost's depth is the window's own water at the same
   instant and T2's fluxes are positive-preserving, so a column can never be asked for more than it holds (asserted;
   any shortfall is counted, never silent). Then the held columns' T2 state is discarded (step 1 resets it).
4. *T1 steps.* The window's box faces stay walls: water crosses the edge only as these transfers.
The world's water = T2's free columns + T1: the region's ledger counts held columns out (`stored()` excludes them) and the
window in, so in - out = stored + window, exactly, every frame.

**2. The window's floor is the creek's bed, not the voxels.** The 3-D core sees the 1/3 m voxel staircase, which pools; the
creek flows on the generator's line (25.21). A window's cell is SOLID when its centre is below the T2 bed of its column (the
bed the region uses - the line on the carve, the voxel top elsewhere), or when the real occupancy is solid above it (a dam,
a stone, a bank). So T1 and T2 stand on the same bed to within one cell (1/9 m), and a real obstacle is still an obstacle.
**3. Bed friction in the window.** The 3-D core's uniform pond friction (24.7) brakes flowing water (24.8); a window in a
creek with it would slow the flow and DIP the surface at the seam. Inside a window, pond friction is replaced by Manning's
law at the bottom: each wet cell resting on a solid cell loses momentum at g n^2 |u| / d^(4/3) (d = the water's depth over
that sub-column, n = the region's roughness) - the same law, same n, as T2. (The pond's own friction is unchanged elsewhere;
replacing it in every volume is 25.5's later step, with 24.8's slosh numbers as its regression.)
**4. Lifecycle.** `water_columns {id, window: {x1, z1, x2, z2}}` opens one (columns of the region, voxel box, its y range
from the lowest bed to the region's top): the window is created, seeded (fills from each held column's depth above the
window's floor, mass-exact - the surface may differ from T2's by the floor's rounding, < 1/9 m, and settles; velocities
uniform with depth = the column's), its columns held. `{window_close}` writes it back: each held column gets the window's
water over it and its velocity, unheld - exact. One window per region in H2a.

**Contract and tests (predictions before the first run).**
- **H2-T1 unit, mass:** a window opened on a flowing channel (the 25.33 creek rig, CPU), run 60 s, closed: the region's
  ledger + window over the whole run closes to 1e-6 m^3 of the inflow; the transfer shortfall 0.
- **H2-T2 unit, the hold is exact:** with a window over still water in a closed tank, T2 + T1 total constant to 1e-9 m^3 per
  frame; a held column's T2 depth after a step differs from its ghost by exactly its transfer.
- **H2-T3 unit, Manning in the window:** on the straight 1:50 rig at steady flow, a window's mid-reach depth and speed within
  10 % of the column tier's (and of Manning); the control - the same window with pond friction instead - visibly off (the
  first-cut prediction: deeper and slower by > 20 %).
- **WS-4 (live, the seam):** the creek bench at 0.5 m^3/s (normal depth ~0.3 m: three cells of the window at 1/9 m) with a
  window 3 m long across the creek at x 55-57: the surface and the depth-averaged speed just inside and just outside the
  window's edge within 1 cm and 5 % at steady flow; control - no window: the same probe points; the window's cost (ms)
  reported against 25.10's 1.2 ms T1 allotment (prediction: ~1-2 ms on the CPU for ~4,000 cells - the GPU is H2b).
- **Visual:** a clip over the window, the seam in frame; the owner's look.

**Design keys.** *Look:* the window draws through the same surface path as every 3-D volume; the seam is the test. *Chunks:*
the window is a world-aligned box; its floor is a pure function of the T2 bed (world position) and the occupancy; no chunk
input. *Generation:* none. *API:* `water_columns {id, window: {x1, z1, x2, z2}}` - voxel box, clipped to the region,
refused (said) if it covers a spring or an outfall or a second window; `{id, window_close: true}`; the reply echoes the
window (box, cells, held columns, transfer in/out m^3, shortfall m^3, ms). *Defaults:* none change (no window = today).
*Test world:* the creek bench (flat, one creek), one window; the variable is the window, the control is the same probes
without it. **Design-check 2026-10-12: READY for H2a** - the one open risk stated: a 0.3 m deep flow is only three cells of
the 3-D core at 1/9 m; if WS-4 fails on resolution rather than on the hand-off, the next step is H2b's finer cells on the
GPU, not tuning the seam.

### 25.40 H2a built - the hand-off is exact; the window's own physics is the seam (2026-10-12). Status: built, not signed off

**Built (25.39 as designed).** `ShoreSolver::setHeld` (faces between held columns carry nothing); `ColumnWater::setHeld /
setGhost / transfers()` with the ledger moving with the mask (`stored()` leaves held water out, `toWindow` counted);
`WaterSolver::setBedManning` (Manning at every horizontal face, replacing pond friction in a window - CPU); the manager's
`openWindow / closeWindow / windowInfo` - a CPU 1/9 m volume whose floor is the region's bed (`Av::floorY`), seeded
mass-exactly, and the frame order ghosts -> T2 -> transfers in `stepColumns`; the window's mass counts its droplets in flight.

**What holds (unit tests, in the suite):**
- `WaterColumnTest.AWindowHoldsItsWaterExactly` - still water, a 4 x 6 m window: region + window constant to **1.6e-8 m^3**
  of 12.96 m^3 over 4 s, no shortfall. (Its first run read 1.5e-3: the window's droplets in flight were not counted as its
  water - they are now.)
- `WaterColumnTest.AWindowInAFlowingCreekKeepsTheLedger` - 0.5 m^3/s, a window opened at 20 s and closed at 50 s: 30 m^3 in,
  12.0 m^3 into the window and 10.7 out of it, region + window = start + in - out to **3.7e-7 m^3** every frame; after the
  write-back the region's ledger **4.5e-7**; shortfall 0. **The hand-off is mass-exact.**

**What does not - the seam (WS-4's unit form), parked as `DISABLED_AWindowInAFlowingCreekShowsNoSeam`:** at steady flow the
window's mid-reach is **177 %** of the column tier's depth (0.41 vs 0.23 m) and slower (0.64 vs 1.08 m/s); across its
upstream edge the surface is 4 cm lower inside and the speed 20 % lower. Control (pond friction instead of Manning): 192 %.
**Why - measured, not guessed** (`DISABLED_DiagWindowCarriesTheCreekAlone`): with the window over the whole channel (the
column tier only feeding and draining it), the 3-D core ALONE carries this creek 0.335 m deep at 0.78 m/s after 110 s and
still rising - 145 % of Manning's depth; with pond friction 0.43 m. So the seam is the 3-D core's own shallow-flow physics
at 1/9 m (two to three cells of water at Froude ~0.7), not the hand-off - the risk 25.39 named. H0 saw the same at 1/3 m.
**Cost:** a 4 x 6 m window (the region's full width, banks included) is ~70,000 cells - ~40 ms per frame on the CPU.

**Decision for the owner (open):** the hand-off works; a window in a CREEK shows a seam because the 3-D core does not carry
shallow fast water as the column tier does. Ways forward, each its own design:
(a) **Windows where the column tier's assumptions break** - falls, plunge pools, breaches, deep water - and not in uniform
    creek flow; the seam there is where the physics genuinely changes. Smallest step; WS-4 restated for those places.
(b) **Make the 3-D core carry shallow flow** - the dissipation in its velocity advection and fill transport at a few cells
    of depth; the largest, riskiest and most valuable (it is also why 3-D ponds' currents read slow).
(c) **The window as a detail layer** - the column tier stays the truth everywhere; the window's column-integrated state is
    relaxed toward it, the 3-D detail rides on top. A seam by construction-free, but the window's water is no longer its own.
(d) **H2b first** (the window on the GPU, 1/27 m cells) - the cost problem; resolution alone is unlikely to fix the depth
    (1/3 m and 1/9 m both read 140-180 %).

## 26. One water model - the pond's, everywhere (owner, 2026-10-13) - DESIGN; supersedes §25.5's tiers

### 26.1 The decision, and why the plan changes

The owner, 2026-10-13: *"i recall saying that ponds need to be the foundation. i have no idea why a 'window' needs to be
used. either there is water there or not, and since the engine works on voxels, it should be: is there water at that
voxel? ... why would the engine ever use an area without water in it to track water."*

**What went wrong.** §25.5 split water into three models: 3-D "windows" (the pond's simulation), "columns" (the shoreline
solver - one depth and one velocity per 1/3 m patch), and a far network. H0 (25.16) measured the pond's simulation as 11x
costlier and too deep on a creek, and the creek was then built on the COLUMN model - a second kind of water - with the
pond's features ported onto it (ripples, splashes, displacement) and a "window" to stitch the two back together (H2,
25.39-25.40). That contradicted the owner's standing rule that small bodies are the foundation and later work builds on
them; the plan said "tiers" without making clear it meant separate kinds of water. The owner approved a plan whose meaning
was not stated plainly. Recorded so it is not repeated.

**The decision.** ONE water model: the pond's - water as cell fills on the world's 1/9 m lattice (the microcube), with the
pond's velocities, pressure, splashes, droplets, ripples, displacement and bodies. A creek, a pond, a dam, a puddle are all
the same water. Nothing is tracked where there is no water: the stored set is every cell that holds water and its
immediate neighbours (where water can move next) - never a box of dry banks and air.

**What the measurements say this needs (not guessed):**
1. **The pond simulation carries shallow, fast water too deep and too slow** - the one real blocker. Alone on the bench
   channel at 1/9 m it ran 145 % of Manning's depth and still rising (`WaterColumnTest.DISABLED_DiagWindowCarriesTheCreek-
   Alone`, 25.40); H0 saw 140-265 % at 1/3 m. This is a defect in the foundation itself - it is also why currents in 3-D
   ponds read slow (24.8's weir at 17 % low) - so it is fixed FIRST, before anything is built on it.
2. **Storage that follows the water.** Today a volume is a dense box (25.40's window: ~70,000 cells for a 4 x 6 m box over a
   2 m creek). A 100 m x 2 m creek 15 cm deep is ~30 m^3 = ~22,000 water cells at 1/9 m - small on the GPU, if only the
   water and its neighbours are stored and stepped.
3. **The GPU.** The CPU pond core costs ~400 ns per cell-tick (H0); the 2 ms budget (25.10) needs the GPU core, sparse.

### 26.2 What stays, what goes

| Built | Fate | Why |
|---|---|---|
| The pond core (§15-§24): fills, MAC velocities, projection, splashes, droplets (§21), ripples (§22, the GPU step 25.34), displacement (§20), bodies, buoyancy, Tier B rest + write-back (§16) | **THE model** | the foundation, signed off on ponds |
| `WaterSolver::setBedManning` (25.39) - Manning at the bed | **kept, becomes the bed law** in the pond core (25.5's "what replaces pond friction"); ported to the GPU in P1 | physical, measured on the columns |
| A volume's floor from the generator's bed line (`Av::floorY`, 25.39) and 25.21's option (a) | **kept**: water stands on the generator's continuous height on the carve, the voxels elsewhere | the owner's decision (25.21) |
| Creek beds carved to 1/3 m (25.19/25.22), the benches (WaterBench_Creek, the dam & pond, WaterBench_Small), the measuring tools (probe/profile, floats, ripples, splash, dam_pond) | **kept** - retargeted at the one model | the rigs and their numbers are the gates |
| The column solver (`ShoreSolver`, `ColumnWater`, regions, sparse tiles 25.32-25.33, the load 25.30, the step clamp 25.37) | **reference only** until P3 passes: the creek benches' numbers it produced are the targets the pond model must meet; then removed from near water (P5 decides if a far stand-in remains) | a second kind of water |
| H2 windows (`openWindow`, held columns, ghosts, transfers - 25.39/25.40) | **removed** in P3 with the column regions | they only stitched the two models |
| Creek-only features on the columns (RC flow forcing, SC load and cosmetic crown) | **re-derived on the pond model** in P3 (the pond already has displacement and a splash crown; flow-driven ripples read the 3-D surface velocity) | features carry forward on ONE model |

### 26.3 Phases (each designed in detail and design-checked before it is built; the owner looks in motion)

**Status (2026-10-14):** P1 **DONE** (signed off 2026-10-13, 26.5). P2a **DONE** - storage in bricks, bit-identical (26.8).
**26.9 surface fixes DONE** (signed off 2026-10-14). **26.10 still-water guess ON by default, DONE** (signed off 2026-10-15, "they all look really good"; the
resting-body loss found and fixed - a per-cell float remainder; GPU still water settles and sleeps). **P2 cost gate MET at the
median** (26.11: the creek 1.02 ms a call at 27 sweeps, GPU 0.79 ms; p90 2.06 ms is host jitter around the fence - the GPU sum
does not vary). **P2b BUILT** (26.12: the engine's volumes sparse, the per-frame paths bounded by the water and the bodies, not
the box). **P3a BUILT; P3b's look signed off 2026-10-15** (the creek bench as pond water, fill and steady clips; its numbers
are open - the cost 3x, the bed comparison; 26.14). P4, P5 **not started**.
**Status (2026-10-17):** P3's cost work since: 26.15 the bench re-carved at 1/9 m, both models on the same bed (built); 26.16 the
water's call not waited on, frame 12.9 -> 7.4 ms (built); 26.17 flat 8 x 2 x 8 bricks, GPU 1.83 -> 1.57 ms (**signed off**
2026-10-16); 26.18 ground bricks, GPU 1.56 -> 1.35 ms (built); 26.19 a dig without a freeze, 38 freezes -> 0 (**signed off**
2026-10-17). **P3 open, in order:** (1) ~~the pond model 6 % / 10 % deeper on the stepped bed~~ CLOSED by 26.20 - its water is at the
reference depth; the drawn surface stands 5-7 % high on air inside moving water (to design, owner's call); (2) P3c, the dam & pond bench as pond water against 25.38's numbers; (3) P3d, the column regions and windows removed from
near water.

- **P1 - the pond model carries a creek.** [DONE, signed off 2026-10-13 - 26.5] Find, by measurement, why the pond core runs shallow fast water deep and slow,
  and fix it in the core (CPU reference first, then the GPU kernels - one law, both backends). Rig: the straight 1:100
  channel of 25.39 (`WindowRig`'s world) run by the pond core ALONE - a dense box is acceptable here, it is a unit rig - at
  1/9 m, 0.2 and 0.5 m^3/s (normal depth ~0.12 and ~0.23 m: one and two to three cells). Candidates, each measured by
  switching it alone (one variable, a control each): the bed law (pond friction vs Manning - done: both deep), velocity
  advection's numerical dissipation, the fill transport (compaction, the film threshold, thetaMin), the pressure
  projection's convergence, the bed as cells (the 1/9 m staircase vs the line), sources and sinks at the ends. **Gate:**
  mid-reach depth and speed within 5 % of Manning at two to three cells deep (and the one-cell case reported); the
  column solver's numbers on the same rig as the cross-check; the still-pond suite (§19-§24: slosh, waves, pebble pair,
  W-T tests) unchanged - the fix must not break the signed-off pond.
- **P2 - storage that follows the water.** [P2a storage DONE - 26.8; cost gate MET at the median - 26.11; P2b BUILT - 26.12] The pond core on the GPU stores and steps BRICKS of 8 x 8 x 8 cells (8/9 m) that
  hold water or touch a brick that does; empty bricks are freed. Bricks are world-aligned (36 per 32 m chunk side), so a
  body of water is one set of bricks wherever it lies. **Gates:** a sparse run equals the dense run of the same water
  (the 25.32 equality shape: bit-identical on the CPU reference, parity on the GPU); cost per water cell measured; the
  100 m bench creek (~22,000 cells) inside 25.10's budget.
- **P3 - creeks and ponds on one model.** [designed - 26.13; P3a BUILT; P3b first run - 26.14: cost and the bed open] The creek bench, the dam & pond bench and the pond benches all run as pond water;
  springs and outfalls are the pond core's sources and sinks; the bed is the generator line (option a). **Gates:** the
  numbers the column solver met (25.28 K: speed vs Manning, roughness tuning; 25.38 DP: fill rate Q/A, spill at the crest,
  the dam's dip and the break's surge, the ledger exact across edits) met by the pond model; ripples and splashes as on
  the pond (no ported copies); then the column regions and windows removed from near water.
- **P4 - rest and the world record.** [superseded 2026-10-19 by §27 W1 + W4] Still water sleeps into spans and wakes where disturbed or flowing (§16, as today);
  flowing water never sleeps; 25.3's world record (saved water, springs as world objects) on the one model.
- **P5 - far away.** [superseded 2026-10-19 by §27 W2, W3, W5, W6] Measured against the budget: either the pond model within the whole simulation radius, or a cheap
  summary far from the player (25.5's T3, basins and reaches) with its hand-off placed where no seam can be seen. Decided
  by numbers from P2/P3, not now.
H3-H5 of 25.13 (settling, the far network, underground) are re-designed on the one model when they are reached; their
§25 requirements (sources and sinks, the drainage limit, never undrainable, evaporation as background) stand.

### 26.4 Design-check (Feature Design Keys, 2026-10-13)

**Voxel look.** The one model is the pond's - its surface at 1/9 m, droplets as microcubes, ripples on the 1/9 m lattice -
already signed off on ponds. No smooth idiom imported. Detail is unconditional: 1/9 m everywhere water is; cost is bounded
by the budget (bricks resident near the player), never by a coarser look.
**Chunks.** Every quantity is world-derived: cell fills on the world lattice, bricks world-aligned (8/9 m, 36 per chunk),
the bed from the generator line or the occupancy. The brick set is a COST structure - a water cell's evolution never
depends on which brick holds it. **Equality test (P2):** the same water simulated as one dense box and as bricks - straddling
a chunk seam and a brick seam on purpose - bit-identical on the CPU reference (`WaterBrickTest.SparseEqualsDenseAcross-
Seams`, the 25.32 shape), parity on the GPU. No cross-chunk lookups: the occupancy pool is world-addressed already.
**Generation.** None in P1-P3 (runtime). The creek bed rule (25.22, generator line on the carve) is consumed, not changed.
P4 touches the world record (save format) - designed then; per-world tuning (roughness, the radius) belongs in the world
recipe when it exists.
**API.** P1 adds no route (unit rigs). P2/P3: the existing `water_core_*` routes (create, place, sources, stats) keep their
units; bricks are reported (`bricks`, `cells`, `ms`); `water_columns` is retired in P3 with the column regions (said in its
reply until then). Defaults: P1 changes the pond core's friction law (pond friction -> Manning at the bed) - the pinned
pond tests that read pond friction (24.7's half-life, the 24.8 slosh numbers, W-T) are re-run and updated in the same
commit with the reason, or the change is gated to flowing water if a pond test shows the still pond's feel changed.
**Visual test plan.** P1: unit rigs only (depth and speed against Manning, one variable per run, the column solver as the
cross-check, the pond suite as the regression). P2: the equality test + cost rows. P3: the creek bench, the dam & pond bench
and the pebble pair re-shot at the same poses as their column-era clips (`creekbench_*`, `dam_pond.mp4`, `pebble_pair_*`)
for the owner's look; the test worlds are the existing small benches (flat, one creek / one pond, shipped defaults except
the springs and outfalls).
**Risks, stated.** (1) P1 may find the dissipation is the core's advection scheme - a deep change touching every pond test;
the gate keeps the pond suite green. (2) A creek one cell deep (a trickle) is at the lattice's limit at 1/9 m - reported,
not hidden; 1/27 m is the answer only if the owner wants trickles resolved, at 27x the cells. (3) The budget at world scale
is unknown until P2 measures cells/ms; P5 exists for that.

**Checked against the code (2026-10-13) - three gaps found, folded in:**
1. *Ripples and the drawn surface for sparse water.* A volume's ripple layer is capped at 512 cells per axis (57 m,
   `kRippleMaxCells`) and four layers (`kRippleMaxLayers`): a 100 m creek as one body would get NO ripples; the surface
   mesh is extracted from a dense box per volume (`WaterSurfaceMesh`). **P2 states both:** the ripple layer becomes a
   camera-following window over the water (RC's design, 25.26/25.29, already built for the columns) and the surface is
   extracted per brick column.
2. *Unloaded ground.* Volumes today treat non-resident ground as a HOLD boundary (`Occ::Unknown`, §14.2/§5.1). **Bricks keep
   the rule:** a brick never extends over a non-resident chunk; unknown ground holds water, never drains it.
3. *The pinned pond tests P1 may move.* Five files read pond friction - `WaterCoreTest`, `WaterCreekH0Test`,
   `WaterDropletTest`, `WaterSolidTest`, `WaterWaveTest` (24.7's half-life, 24.8's slosh) - and the GPU applies it
   (`WaterCoreGpu` mode 9). **P1's regression set is these five plus `WaterCoreGpuParityTest`;** any of them that changes
   is updated in the same commit with its reason, or the new law is limited to flowing water.

**Verdict: NEEDS WORK -> the three gaps folded in above -> READY for P1** (every key answered; its rigs, gate, controls and
regression named). P2-P5 are READY in outline and each gets its own detailed design and check before it is built.

### 26.5 P1 measured - why the pond model ran a creek deep and slow, and the fix (2026-10-13). Status: DONE, signed off by the owner 2026-10-13

**The rig** (`tests/core/WaterShallowFlowTest.cpp`): the pond solver ALONE (`WaterSolver`, CPU reference) in a 48 m x 1 m
channel at 1/9 m cells; the slope as a TILTED FRAME (`SolverParams::gravityX` = g S - no 1/9 m staircase); pumps across the
inlet; the outlet a FREE OVERFALL (the floor ends 2 m before the box's end, the water drops into a pit and is drained);
Manning at the bed (`setBedManning`, n 0.035, S 0.01); measured over the middle third at 60 s. The rig needed two fixes of
its own before its numbers meant anything: a 16 m channel held at normal depth against the box's END WALL backed water up
over the whole measured reach (the surface rose toward the outlet), which was most of the "145 %" of 25.40; the free overfall
removed it (surface slope over the third 0.0000 in every run below).

**Bisected, one variable at a time** (`DiagWhatSlowsAShallowCreek`, `DiagMomentumBudget`, `DiagMassByStage`): compaction, the
thin-film stages, rest damping, the residue sweep and a halved substep change nothing (<= 4 %); flowing the other way changes
nothing (`DiagDirection`). What did: the FLUX the velocity field implies (velocity x the donor's fill) exceeded what the pumps
put in - 0.188 against 0.150 m^2/s at steady state: **the water lagged its own velocity field.**

**Cause - the fill transport.** `EulerianTransport::advect` moved water face by face in a fixed order and capped each move at
the room the RECEIVING cell had at that moment. A full cell receives only what it has already passed on, so in full water
the faces visited before their receiver's own outflow moved little or nothing; only the partly filled surface row carried
the flow, and the creek deepened until it could. The GPU kernel (`wc_fill_advect.comp`) has the same rule (checkerboard
passes, running limits).

**Fix - all faces at once** (`EulerianTransport::s_allFacesAtOnce`): every face's flux from the fills at the start of the
advect; a donor's total outflow capped at its fill (scaled, never clamped after); all moves applied together, summed in
DOUBLE and rounded to float once per cell (six float adds per cell drifted 2.7e-6 m^3 in a 10 s slosh, the double sum
+1e-7, the old rule 2e-8 - `DiagMassByStage`); a cell left over full carries the excess UP its column (21.2's deposit
rule), under a ceiling it stays in the last open cell for a substep (counted). Exact by construction.

| 48 m rig, middle third | depth vs Manning | speed vs Manning | flux the field implies / pumped |
|---|---|---|---|
| q 0.15 m^2/s (1.5 cells), face by face | 104 % | 102 % | 0.158 / 0.150 |
| q 0.15, **all faces at once** | **100.0 %** | **100.0 %** | 0.150 / 0.150 |
| q 0.40 (2.8 cells), face by face | 113 % | 108 % | 0.490 / 0.375 (the pumps could not place 0.400) |
| q 0.40, **all faces at once** | **100.0 %** | **100.0 %** | 0.400 / 0.400 |

Gate (`WaterShallowFlowTest.APondModelCarriesACreek`, in the suite): q 0.40 within 5 % - **100.0 / 100.0 %, flat**.

**Why it is OFF by default - the signed-off pond changes (owner's call).** With the new transport on, five pond tests fail
against gates tuned to the old rule (run with it on, 2026-10-13):
- *Waves* (`WavesKeepTheirEnergy`): mixed, not worse overall - per swing, half-full tank: mode 1 0.98 -> 0.86, mode 2
  0.91 -> 0.87 (worse), mode 3 0.82 -> 0.92, mode 4 0.53 -> 0.65 (better); the old rule also GREW some modes (1.03-1.07 per
  swing - energy from nowhere), the new one grows two (1.05-1.11).
- *The stone-entry splash* (`TheStoneEntryLeavesNoFloatingScraps`): at 7.7 m/s the old rule threw 8 spray cells (0.29 L) of
  droplets, the new none - plausibly the old rule's own blockage squeezed water UP into the spray (the defect made part of
  the splash). Its injected-scrap control also changed.
- *Smaller:* the blast's near-side drop 0.275 m vs its 0.28 floor; the air-pocket test's control comparison; one mass row
  1.5e-6 vs 1e-6.
**GPU port (2026-10-13).** `wc_fill_advect.comp` modes 3-5 run the same rule on the GPU: mode 3 each cell's total
outflow (into the projection's residual buffer as scratch - the projection rewrites it later in the substep), mode 4 each
cell GATHERED (its fill at the start, minus its outflows x its scale, plus its neighbours' flows into it x theirs - no two
threads write one cell), mode 5 the excess carried up per column. Switch: `POST /api/debug/water_transport {"all_faces":
true|false}` (echoes `all_faces`; CPU and GPU, from the next step). Parity (`GpuAllFacesTransportMatchesCpu`): dam-break
front CPU 19 / GPU 19, mass 89.99999 of 90; small tank max |df| 7.1e-4.

**A GPU-only bug the A/B caught, fixed red-before-green.** The first live recording of the new rule GAINED water (slosh:
24.00 -> 24.30 m^3 in 24 s, the resting pond waking up). Cause: a cell left a hair NEGATIVE by rounding (-5e-23) sends
nothing, so its outflow sum 0 is "more than its fill" and the scale divided by zero (-inf; 0 x inf = NaN); compaction's
`min(NaN, cap)` then moved exactly one free-fall cap (0.0738 cells) down from nowhere and the residue sweep zeroed the NaN.
The CPU never divides there (it scales only faces whose donor sends). Fix: scale only when the outflow is > 0. Gate
`GpuAllFacesRestingPondKeepsItsMass` (resting pond, 30 s, one tick per call): red +0.73 cells, green 3.9e-4, no non-finite
fill. Live after the fix: 24.0000 flat over 14 s, stones and droplets on/off.

**The owner's A/B (both rules, same bench, same camera, fixed kernel):** `pebble_pair_p1_ab.mp4`, `slosh_live_p1_ab.mp4`
(left old, right new; the single clips and their JSON beside them). Measured: both hold their mass (slosh 23.99989 ->
23.99988 old, 23.99964 -> 23.99963 new); the slosh calms below KE 1e-3 at 18.6 s old, 19.7 s new; the 0.333 m stone
reaches the floor (1.50 m) either way, the new rule decelerating it earlier (0.57 m/s at 1.44 m vs 1.25 m/s at 1.50 m);
peak GPU time per call 4.5 ms old, 3.6 ms new. Stills are near-identical - the differences are in motion.

**SIGNED OFF by the owner (2026-10-13): all faces at once is THE DEFAULT** (`EulerianTransport::s_allFacesAtOnce = true`;
`water_transport {"all_faces": false}` stays as the A/B switch and the tests' control). Status of P1: **done**.

**Gates re-based with it (each re-derived from what it measures, not loosened to fit):**

| test | was | now | why |
|---|---|---|---|
| `WavesKeepTheirEnergy` | kept >= 0.90 / 0.80 / 0.50 (modes 1-2 / 3 / 4) | >= 0.85 / 0.80 / **0.60**, and **<= 1.15** | long waves lose more on a nearly full top cell (0.86 / 0.87 at top 0.9), short ones less (mode 4 0.65 at top 0.5, was 0.53); some cases still GROW per period (1.05-1.11, the old rule 1.03-1.07) - energy from nowhere, now capped so it cannot worsen |
| `BlastBesideThePond...` | near-side drop >= 0.28 m | >= 0.27 m | measured 0.275 (the spec row's 0.3 missed by 2.5 cm) |
| `TheStoneEntryLeavesNoFloatingScraps` | the 7.7 m/s entry must birth spray; scripted scrap at tick 20 beside the entry | no births asserted; the scrap placed as the stone enters, in a corner | the CPU stone throws NO spray at 6-20 m/s now (the old rule: 8 cells at 7.7, 6 at 10, none at 15-20 - its blockage squeezed water up); the old control was seen floating 1 tick, the new placement 3 ticks under either rule (`DiagScriptedScrap`); the spray rule stays pinned on the grid rigs |
| `TheCavityCloses` | control: >= 2 % of the stone more air without the wake rule | the rule never ADDS air; the measure's sight proven by a scripted pocket (half a cell under the water reads 0.01852 of 0.01852 m^3) | the cavity now closes even without the rule (0.00043 m^3, 1.1 % of the stone) - the transport no longer blocks water from flowing in |
| `PocketsAndASloshAreNotScraps` | grid + born drift <= 1e-6 m^3 | <= 2e-6 | 1.5e-6 measured - float rounding on ~32 m^3 (5e-8 relative) |
| `GpuFillsMatchCpuElementwise` | every face within 5e-3 m/s | every PROJECTION face (f >= 0.5 on a side): 4.3e-4 | film-top faces follow `settleThinFilmTopFaces`' "any water above" switch, which float dust flips (0.16 m/s apart there while the fills agree to 3e-4); printed, not gated |

**A GPU defect the default exposed - the warm start below was REVERTED the same day (26.8: it made a surface at the liquid threshold flicker and lose water live); the creep is an OPEN row.** ~~Fixed: the projection never warm-started.~~ `wc_classify.comp` zeroed the pressure
every substep, so 40 RBGS sweeps stopped 0.02-0.07 % over hydrostatic on a still pool (4.9086 vs 4.9050 at the top
cell): still water crept up into the air (6-15e-6 of a cell per tick per column) under BOTH rules; the old rule leaked
7.8e-6/tick - just under the quiet threshold 1e-5 - and slept; the new one 1.5e-5 and never slept
(`GpuRestDecisionDeterministic` red). 200 sweeps: exact. Fix: liquid cells keep the last pressure as the first guess
(zero outside the water; zeroed on create and upload). Now: the still pool sleeps at tick 32 / 32 (deterministic), its top
cell's pressure 4.9050, the resting-pond mass gate 1.6e-4 cells over 30 s. Diagnostic: `DiagStillPool` (env `DIAG_SWEEPS`). (All as measured WITH the warm start; after its revert `GpuRestDecisionDeterministic` (a) is red again - the open row.)

### 26.6 P2 detailed design - water stored in bricks that follow it (2026-10-13). Status: designed, checked (26.7)

**In plain words.** Today a body of water is a BOX: every cell inside it is stored and stepped, wet or dry - a 100 m creek
running diagonally would need a 100 x 100 m box. P2 stores the world as small cubes of cells ("bricks", 8 x 8 x 8 cells =
8/9 m at 1/9 m) and keeps only the bricks that hold water or lie close enough to it to receive some this tick. A brick is
a STORAGE unit only: the water in a cell behaves exactly as it would in a box (the equality test proves it bit for bit).

**The cost model, measured first** (`WaterCoreGpuParityTest.DISABLED_DiagCostPerCell`, the GPU core as it is, dense boxes
at 1/9 m, one tick per call, Release, this machine):

| cells stepped | ms per call (median / p90) |
|---|---|
| 4,096 | 0.73 / 0.84 |
| 65,536 | 0.82 / 1.27 |
| 262,144 | 1.32 / 1.94 |
| 1,048,576 | 3.36 / 3.74 |

~0.75 ms per call fixed (the dispatch chain and its barriers, the fenced submission) + ~2.6 ns per cell-substep; resting and
sloshing water cost the same. **What it means:** the budget (25.10, 2 ms) holds ~400,000 stepped cells at one substep.
The 100 m bench creek (2 m wide, 0.15 m deep: ~22,000 water cells) stored in 8^3 bricks with the halo below is estimated
at ~250,000 stepped cells (~1.4 ms) - within budget, with the brick indirection's own cost still to be measured (P2a's
cost rows). Smaller bricks would waste less on a shallow sheet (4^3: ~3x the water cells instead of ~10x) but cannot
hold the halo inside one ring of neighbours (below); 8^3 is the design, 4^3 is the fallback if the measured rows miss.

**P2 is built in two halves.** **P2a - the GPU core on bricks** (this design, unit rigs): the solver's storage becomes
bricks; a "dense" volume is simply one whose bricks are all kept. **P2b - the engine on bricks** (designed after P2a
measures): the manager's CPU copy of a volume (`Av::grid`, ~80 uses: bodies, buoyancy, droplets, write-back), the drawn
surface (per brick column) and the ripple layer (a camera-following window - 26.4 gap 1), the routes, the live bench.

**P2a design.**
1. *Bricks.* B = 8 cells per edge. World-aligned: a cell's brick is floor(world cell / 8) - 36 bricks per 32 m chunk side
   at 1/9 m, so every chunk seam is a brick seam and a body of water is the same set of bricks wherever its volume box lies.
   A volume keeps its box (the API's domain and walls: out-of-box cells are blocked exactly as today); its BRICK BOX is the
   box rounded out to whole bricks; cells inside the brick box but outside the box are Solid (as today's out-of-bounds).
2. *Storage.* A pool of brick SLOTS on the device, capacity C fixed at creation (refused with the byte count when the device
   cannot hold it, as today). Every per-cell buffer (f, fOrig, fPrev, occ, liq, diag, rhs, p, res, src, solid, deposit,
   extrapolation scratch) becomes C x 512 entries; every face lattice (u, v, w and their copies) stores each cell's LOWER face
   (512 per slot) - a cell's upper face is its neighbour's lower face, in the neighbour brick. A BRICK MAP (one int per brick
   of the brick box: slot or -1) on the device and mirrored on the host; capped at 4 M entries (16 MB, e.g. a 256 x 256 x 16 m
   box at 1/9 m) - larger boxes are refused with the reason (world scale is P5).
3. *Addressing - one place.* The kernels address cells only through `water_core.glsl` (cellIdx, uIdx/vIdx/wIdx, inBounds,
   decode). Those helpers become brick lookups: (x, y, z) -> map[brick] -> slot x 512 + local. An ABSENT brick reads as dry
   air: f = 0, velocity 0, p = 0, not liquid, occupancy Air. A write to an absent cell is dropped - and a FILL moving into an
   absent cell is never made: the transport treats that face as closed and counts it (`fluxIntoAbsent`, an atomic count).
4. *Threads.* Cell and face kernels dispatch over C x 512 (slot, local) instead of nx x ny x nz; idle slots return at once.
   Column kernels (settle, compaction, carry-up, the residue sweep, droplet births, the surface field) dispatch over the
   resident BRICK COLUMNS (a host-built list: (bx, bz) and its brick-row range), 64 threads each walking y through the map
   (an absent brick in the stack is dry air). Reductions run over slots.
5. *Residency - a pure function of the water.* After each call the device writes, per slot, whether it holds any water
   (f > 0) and the distance of its water to each of its 6 faces (cells); the host reads it with the stats (8 bytes a slot).
   Before the next call, a brick is RESIDENT when it holds water, or when water in a neighbour (all 26) lies within R cells
   of it, with R = 3 (the extrapolation's three layers read that far from liquid) + the farthest the water can move this
   call (ceil(vmax x dt x ticks / h) + 1, vmax from the max-speed reduction the substep count already uses). R <= 8 keeps
   the halo to one ring of neighbours; when the measured speed makes R > 8 the halo takes a second ring (fast water, a
   blast) - stated, counted (`haloRings`). New bricks are zeroed and their occupancy filled from the volume's SolidQuery on
   the host (512 queries a brick; Unknown stays a hold, 26.4 gap 2), uploaded in the call's prologue; a brick no longer
   resident is freed (it holds no water by the rule, so nothing is lost). Slots are assigned lowest-free-first in brick
   order: deterministic.
6. *Capacity.* When residency needs more than C slots, the missing bricks stay absent and the flow into them is held (a
   closed face, counted `heldByCapacity`) - water is never dropped; the stats say so.
7. *Not on bricks:* the particle transport (FLIP, not the shipped transport) stays dense - a sparse FLIP volume is refused.
8. *Stats.* `GpuStepStats` gains bricks, capacity, allocated, freed, haloRings, fluxIntoAbsent, heldByCapacity.

**Tests (P2a gates, red first).**
- `WaterBrickTest.SparseEqualsDenseAcrossSeams` (integration, GPU): the same water run with every brick kept (dense) and
  with residency following the water (sparse); the box placed so it straddles a chunk seam and brick seams on purpose.
  Scenes: a dam break whose front crosses seams; a slosh; a stone entering (moving solid); a pump pouring into dry bricks
  (allocation as water arrives); a sink draining bricks dry (freeing). Every tick: every cell's f, u, v, w, p
  BIT-IDENTICAL between the two (an absent cell compares as 0 against the dense run's value, which must then be exactly 0);
  `fluxIntoAbsent` = 0; mass equal to the reductions' rounding (their summation order differs - stated). RED before the
  residency rule: the sparse run with only its starting bricks kept - the front is held at the first seam,
  `fluxIntoAbsent` > 0, the fields differ (the control that the test can see a wrong halo).
- `WaterBrickTest.CapacityHoldsWater`: C forced small - `heldByCapacity` > 0, mass exact, nothing dropped.
- The GPU parity suite (24 rows) green on the brick storage, tolerances unchanged - the link to the CPU reference
  (the CPU WaterSolver stays dense: it is the oracle, not a storage scheme).
- Cost rows: (1) the synthetic 100 m creek (the bench's 2 m x 0.15 m section at 1:100, run DIAGONALLY so a box would be
  ~100 x 100 m): median ms per tick incl. residency, gate <= 2 ms; resident bricks and stepped cells per water cell
  reported. (2) The WaterBench_Small pond box, dense vs bricks: the indirection's overhead, gate <= +25 %. If (1) misses,
  4^3 bricks are measured before anything else is changed.

**Risks.** (1) The indirection costs more than +25 % - then the map lookup is cached per workgroup (a brick's 26 neighbour
slots in shared memory) before the design changes. (2) A fast front allocates every tick (churn) - counted per tick in the
dam break. (3) Equality fails on air faces: the extrapolation zeroes dry faces beyond its three layers every substep, so
dense air far from water already holds 0 (read in `wc_extrap.comp` mode 6-8) - if the test still finds a difference, it is
named before anything is changed.

### 26.7 Design-check of P2a (Feature Design Keys, 2026-10-13)

**Voxel look.** Unchanged by construction: storage only, 1/9 m everywhere water is; the equality test is the proof.
**Chunks.** Bricks are world-aligned and every chunk seam is a brick seam; residency is a function of the water alone
(never of chunk residency, distance or LOD); Unknown ground stays a per-cell hold. Equality test straddles a chunk seam.
No cross-chunk lookup: the SolidQuery is world-addressed.
**Generation.** None (runtime storage).
**API.** No route in P2a (P2b adds `storage` on create and the brick stats to the replies, echoed). No default changes:
engine volumes stay all-bricks-kept (dense) until P2b; the GPU parity suite pins that nothing moved.
**Visual test plan.** P2a is invisible by design: bit-identical fields, so no look can change - the equality test and
the parity suite are the gate (L2 structural invariant on real output). P2b brings the live look (the pebble pair at its
pose, a long creek) for the owner.
**Small rigs.** Unit boxes of a few metres at 1/9 m, one variable per test (the residency), a dense control each.
**Stress dimension.** Count (bricks: 1 -> thousands, the diagonal creek), boundary crossing (chunk and brick seams), churn
(the pour and the drain allocate and free every tick), degenerate (capacity forced to a handful of slots).

**Verdict: READY for P2a.** P2b gets its own design and check once P2a's numbers are in.

### 26.8 P2a built - the GPU core on bricks (2026-10-13). Status: storage DONE (bit-identical); cost gate OPEN

**What was built.** Every fill volume on the GPU is now stored in world-aligned 8^3 bricks held in slots (slot 0 = the
absent brick, all zero, never written); the particle (FLIP) transport keeps the dense layout. All addressing lives in
`water_core.glsl` (`cellIdx`/`uIdx`/`vIdx`/`wIdx` brick lookups, `threadSite` / `threadColumn` thread mapping); a thread's
id IS its storage index, so every kernel's `x[id]` stayed as it was. `BrickOptions{sparse, capacity}` on
`WaterCoreGpu::createVolume`; dense (every brick kept) stays the engine default until P2b. A new kernel
(`wc_bricks.comp`) writes per brick, after each call, where its water and its liquid lie (two records: any water f != 0,
and liquid f >= thr - distances to the six faces) and checks the absent brick is still zero. The host decides residency
from those records before the next call: reach 4 + travel for liquid (the extrapolation's three layers + a face), 2 +
travel for any other water (films: the rules that read round them reach one cell), travel = the farthest this call can move
water. Bricks holding water are stored first, then the halo; a capacity refusal never strands water (a capacity below
the water itself is refused at create). Slots are compacted when the addressed range outgrows the stored count by a
quarter. Reductions fold per-brick partials in MAP order, so a sparse volume's sums - and so its rest and settle decisions
- equal the dense run's bit for bit. The upload/download API keeps the dense layout (scatter/gather), so nothing outside
moved.

**Gates (red first, all on the GPU; `WaterBrickTest`):**

| test | result |
|---|---|
| `SparseEqualsDenseAcrossSeams` - dam break, slosh, stone entry, pump into dry bricks, sink draining; a box over a chunk seam (world cell x 288) and negative z | **0 differing ticks** in all five (every f, u, v, w, p compared with ==, every tick); halo short 0; absent brick clean; sparse stored 22-60 of 72 bricks |
| `AFrozenResidencyIsCaught` (the control: residency frozen) | differs in 98 of 120 ticks; halo short 907 - the check sees a wrong halo |
| `CapacityHoldsWater` | capacity 10 (water needs 16) refused at create ("0.527 m^3 would not be stored"); capacity 24: 1,981 bricks refused, water held, mass within 2e-8 m^3 |
| GPU parity suite (24 rows) on brick storage | unchanged - same numbers as before bricks (dam-break front 19/19, fills 3.3e-4, still pool asleep at 32/32); FLIP run-up the known open row |
| the 306 CPU water tests | unchanged |

**Found and fixed on the way (each by measurement):** (1) the transport's count of "flux into an absent brick" cannot see
a thin halo - a face between a stored cell and an absent brick belongs to the absent brick and never carries velocity
(the frozen control differed in 98 ticks with the count at 0); the halo check (`haloShort`: water within 4 cells of an
absent brick after the call) is what sees it. (2) A small capacity lost 0.26 m^3 at upload: halo bricks took slots before
water bricks - water now goes first, and a capacity below the water is refused. (3) One reach for all water stored a
whole brick layer above a resting creek for 2.7e-6 of film and freed/re-stored it every tick (9 ms spikes) - films now
get the short reach. (4) Freed bricks left high slots addressed (1,184 walked for 592 stored) - compaction. (5) Reading the
records one value at a time from uncached staging memory cost ~0.3 ms a call - one bulk copy. (6) A box's upper wall faces
had been given their own layer of bricks (a 16^3 box stepped 27 bricks for 8) - they read 0 from the absent brick.
(7) A pump's residency scanned the whole dense grid every call (5 M cells on the creek box) - now marked from its cell.

**Cost (this machine, Release, one tick per call):**

| case | stepped cells | ms per call (median) |
|---|---|---|
| dense boxes, brick addressing vs before (4 k / 65 k / 262 k / 1 M cells) | same | +22 % / +15 % / +9 % / +3 % (gate <= +25 %: met) |
| diagonal creek, ~85 m, 2 m x 0.15 m at 1/9 m - DENSE box | 7.1 M | 16.9 |
| the same creek - SPARSE | 303,104 (592 bricks, 11.4 per water cell) | **1.94-2.02** (host residency 0.08, recording 0.29, GPU 1.57) |

**The cost gate is OPEN.** The creek sits at the 2 ms line (1.94 in one run, 2.02 in the next), and 25.10's 2 ms is the
budget for ALL water, so one creek must cost well under it. `DISABLED_ACreekStoredSparseFitsTheBudget` keeps the
measurement. **The levers, from the split above:** (a) FLAT bricks for shallow water (8 x 4 x 8): the creek's 1.35-cell
sheet fills 17 % of an 8-cell brick column - half the stepped cells, storage only (bit-identical by the same test); (b) the
fixed cost - ~0.9 ms of the 1.57 is per-call (80 RBGS dispatches per substep with a barrier each, 0.29 ms recording):
fewer, fused dispatches; (c) the sweep count - with the warm start (26.5) the solve converges from last substep's pressure,
so fewer than 40 sweeps may hold the parity gates - a solver-accuracy change, measured against the pond suite before
it is considered. P2b (the engine on bricks) does not depend on these.

**The warm start (26.5) REVERTED - found by the live check of P2a.** The live bench pond (WaterBench_Small, GPU) LOST water
while it settled - 2.2e-5 m^3/s, ~3e-4 m^3 in its first 15-25 s, counted nowhere - where the CPU held exactly. Bisected
live: warm start off -> no loss (bricks innocent: every brick test bit-identical either way, every origin bit-identical,
the download/upload round trip exact). Reproduced headless only at the ENGINE's step (the frame's dt, 1/165 s; the tests had
used 1/60): `WaterBrickTest.DISABLED_DiagSettlingLoss` - 2.8e-4 m^3 in 20 s and never asleep; per tick
(`DISABLED_DiagSettlingLossPerTick`) a steady trickle, ~1.5e-7 m^3 every tick, with the top layer - at f = 0.5, ON the
liquid threshold - flickering in a CHECKERBOARD (0.49997 / 0.50003, KE 3e-3). The cold start (what the owner signed off)
holds: 3e-6 m^3 in the same rig. Reverted (`wc_classify.comp` zeroes p every substep again).

**Open rows (honest state after P2a):** (1) [**CLOSED by 26.10** - the still-water guess is ON; its resting-body loss was
the float store, fixed; still GPU water settles and sleeps, `GpuRestDecisionDeterministic` green] the GPU's cold-start projection (40 sweeps) leaves still water creeping
(~1e-5 of a cell per tick per column at dt 1/60) - a still GPU pool never sleeps under the all-faces transport
(`GpuRestDecisionDeterministic` (a) red) and the live pond drifts ~1e-6 m^3/s (24 s: -2.7e-5 m^3; this predates P2a). The
fix is a CONVERGED solve that does not destabilise a threshold surface - the warm start alone did; it belongs with the
cost levers (b)/(c) above (a converged, cheaper solve is both). (2) The creek's cost (above). (3) `GpuFlipBulkParityAndRunup`
(the known FLIP row).

### 26.9 The surface at the liquid threshold, and a still-water first guess (2026-10-14). Status: surface fixes DONE (owner signed off 2026-10-14, "videos look good"); the guess ON since 26.10

**The ask (owner, 2026-10-14: "get it done"):** 26.8's next step - a converged, cheaper GPU solve, for open row (1) (still
GPU water creeps and never sleeps; the live pond drifts) and the cost. What happened, in order, each step measured:

**1. A still-water first guess (built).** The projection starts from the pressure of the water AS IT IS NOW - down each
column, g x the distance to the free surface (the matrix's own theta) - not 0 (`wc_classify.comp` mode 2;
`WaterCoreGpu::s_stillWaterGuess`). A pure function of the water, never last substep's pressure (the warm start's stale
state). For a level pool it IS the solution: `DiagStillPool` p 4.90500 against 4.90500 (cold 4.90862), asleep at tick 30,
nothing creeping above the pool (cold: 5.6e-3 cells in 54 ticks). But the bench pond with its top layer AT the threshold
(f = 0.5) flickered with it at dt 1/60 (KE 2e-2, ~1.5e-7 m^3 lost per tick) - the warm start's symptom. So the warm start
was never the cause; a converged solve exposes something the cold start's error hid.

**2. The CPU fails the same way** (`WaterBrickTest.DISABLED_DiagSettlingCpu`): exact on an exact f = 0.5 layer, but +-1e-5
of noise on it (what float rounding gives the GPU) and the converged CPU never sleeps and loses 2e-6 m^3 in 10 s. A
defect of the ONE model at the threshold - 19.8's "cells stuck at f = 0.5, cause unestablished".

**3. Its causes, measured stage by stage, and the fixes (CPU and GPU, one law each; all ON by default):**
- **(a) The surface jumped at the threshold** (`DiagThresholdColumn`, `DiagSettlingCpu`): a top cell just over the
  threshold has its surface (f - 0.5 + f_above) closer than thetaMin (0.01); the matrix used 0.01 with p_air = 0, so the
  cell's pressure was g x 0.01 h instead of ~0, and its neighbour just under the threshold (air) took a false sideways push
  of 2 g thetaMin. **Fix:** where theta is clamped, the ghost (air) cell holds the still-water pressure at the clamped
  distance, -g (thetaMin - raw) h (`WaterSolver::surfaceGhost`, `wcSurfaceGhost`, `WaterSolver::s_surfaceGhost`) - the
  cell's pressure is then g raw h, continuous across the threshold. Matrix unchanged (the clamp keeps 1/theta finite).
- **(b) A slow surface threw films over itself** (the KE split: 99.7 % of a "settled" pond's KE sat on the free-falling
  top faces of cells just under the threshold, under a film; the transport lifted 0.37-0.53 of a cell over the surface in
  10 s). Two rules: a thin layer RESTING ON WATER rises by filling, not through its ceiling - its top face no longer copies
  a slow upward floor velocity (`settleThinFilmTopFaces`, `wc_column_ops` mode 0); and a QUIET partial cell under an emptier
  one fills before water crosses its ceiling - 24.2's donor-acceptor rule W1, only below `quietRise` = 0.002 x sqrt(2 g h)
  (~5 mm/s at 1/3 m, ~3 at 1/9) (`EulerianTransport::s_quietRiseFraction`, `wc_fill_advect` faceDesired). W1 everywhere
  slowed every wave (24.6); gated, every `WaterWaveTest` row keeps its number (mode 4 at top 0.5: 0.66, was 0.65; 0.005
  cost it 0.59).
- **(c) Water lost to float rounding** where a tiny amount enters a large cell: compaction moved it and the receiver could
  not hold it (`fb + moved` == fb) - now the exact difference leaves the donor; the transport's carry up a column the same
  (the GPU's resting-body loss 2.7e-4 -> 1.8e-4 m^3 with it); the residue sweep counts what the receiver cannot hold as
  DROPPED instead of losing it silently.

**4. Results with (a)-(c) on (guess OFF - the default):**

| test | before 26.9 | now |
|---|---|---|
| `DiagSettlingCpu` - the bench pond, +-1e-5 / +-1e-3 noise on the threshold layer, CPU, dt 1/60 | never asleep, -2.0e-6 / -1.8e-6 m^3 | **asleep by 2 s / by 6 s**, mass +1.5e-8 / +2.5e-7, levels within 4e-5 cells |
| `DiagSettlingLoss` - the same pond on the GPU (cold start), 4 settings | asleep in 1 of 4 | asleep at dt 1/165 (-2.4e-6 m^3); at 1/60 still not |
| the CPU water suite | 318 green | **318 green** (`SpawnInsideWaterIsLedgerExact` now checks water + counted drops - it had passed by CANCELLATION: the transport's rounding gained +2.4e-5 while the sweep dropped 3.0e-5; uncounted now +6.8e-6, four times smaller) |
| the GPU water suite (30) | 2 red (rest, FLIP) | the same 2 red (rest = the open row, FLIP) |
| live, WaterBench_Small, the sloshed pond (stone drop, 25 s) | -9.0e-6 m^3; KE at 12 s 2.7e-2 | **-4.5e-6 m^3; KE at 12 s 4.1e-3** (calms sooner; 1.3e-4 vs 7.4e-5 at the end) |
| live, the still bench pond, 24 s | CPU exact; GPU drifts | CPU exact; GPU +7e-6 m^3, never still (open row) |

The owner's A/B (left the signed-off rules, right 26.9): `docs/evidence/water_core_e/pebble_pair_p269_ab.mp4`,
`slosh_live_p269_ab.mp4` (and the separate `_old` / `_new` clips + JSON).

**5. The guess - built, OFF by default, and why.** With (a)-(c) the guess fixes still GPU water (`GpuRestDecisionDeterministic`
green with it; the threshold pond asleep at every setting, mass exact at 1/165). But with a BODY resting in the pond, at the
engine's sweep count, the GPU loses water: live slosh -1.5..-2.2e-4 m^3 with it, -4.5e-6 without (droplets off: the same);
headless `WaterCoreGpuParityTest.DISABLED_DiagRestingBodyKeepsTheWater`: -1.8e-4 m^3 in 27 s. What is known:
- it is a PARTLY CONVERGED solve with a body, not the guess as such: 27 sweeps cold +5.8e-6, guess -1.8e-4; 40 sweeps cold
  -1.3e-4, guess -9.7e-5; 80 cold -5.6e-5, guess +1.6e-5; 160 both +1.6e-5 (the cold start at 27 sweeps is so far from
  converged - residual 2.3 - that it never reaches the losing band);
- skipping compaction and the carry stops most of it (and the motion: KE 1e-4 -> 5e-11), each alone halves it - they keep
  the water near the body cycling; where the water then leaves is NOT found: no negative or over-full cells, nothing counted
  as residue or droplet births, and a compensated (Kahan) fill update changed nothing (tried, reverted); the GPU's own
  stage-mass sums are float (resolution 6e-5 of a cell) and cannot see it.
- next: exact stage sums on the GPU (integer or two-float), or the CPU on the same body rig stage by stage; then the
  guess can be switched on. A/B: `/api/debug/water_transport {still_water_guess}`.

**6. The cost - "cheaper" is NOT met.** The creek (`DISABLED_ACreekStoredSparseFitsTheBudget`, now tilted so it keeps moving):
40 sweeps 1.80 ms (residual 0.115), 20 sweeps 1.49 ms (3.3), 10 sweeps 1.37 ms (16.7). About 1.2 ms of a call is NOT the
pressure solve, so sweeps can save at most ~0.4 ms, and fewer sweeps cost accuracy in moving water (the dam break's worst
residual at 40 sweeps: 12 with the guess, 5.7 cold - `DiagSweepsAfterTheGuess`). The levers left: (a) flat bricks for
shallow water, (b) fewer, fused dispatches; a solver that converges moving water (multigrid-preconditioned CG, or
brick-local sweeps in shared memory) if the parity or the feel asks for it.

**API.** `/api/debug/water_transport` echoes and sets `still_water_guess` (GPU, default true since 26.10), `surface_ghost` (CPU and
GPU, default true), `quiet_rise` (fraction, default 0.002; 0 = off) beside `all_faces`. **Design keys.** Pure functions
of the water now; no chunk or brick dependence (the brick equality tests pass unchanged); no new world-recipe parameters.

### 26.10 Where the resting body's water went: the float store (2026-10-14). Status: guess ON, DONE (owner signed off 2026-10-15)

**The ask (owner, 2026-10-14: "get it done"):** 26.9's open item - find the still-water guess's resting-body loss, then
switch the guess on (it is what lets still GPU water settle; 26.8 open row 1).

**1. An exact meter per stage.** The device's own mass sums are float (6e-5 of a cell at ~650 cells) and could not see it.
Test hook `WaterCoreGpu::s_stageCapture`: on single-tick, single-substep calls the step copies f after every stage into a
host-visible buffer and the host sums each copy in double - `s_stageDm[k]` per stage (between calls, sources, gather,
carry, ledger, compaction, rest of the substep, residue sweep, droplet births). Used by `DiagRestingBodyKeepsTheWater`.
Result with the guess on (27.5 s, 1/3 m stone resting, 27 sweeps, dt 1/165): mass -1.82e-4 m^3, of which the **gather
(the all-faces fill update) -1.79e-4**, compaction -1.2e-6, residue sweep -2.1e-6, every other stage 0.

**2. The flows balance; the store does not.** A probe writing each cell's `ins - outs x scale` summed it to -1.9e-10 m^3
over the run: the transport's flows are conservative. So the loss is the float store of `fo + change`. Why one-sided: the
pond's top layer sits at f = 0.5 and the float step just below 0.5 is half the step just above, so the tiny back-and-forth
flows of a partly converged solve near a body round down more than up, tick after tick. (Why the guess and not the cold
start: the cold start at 27 sweeps is so far from converged that its flows are not tiny; at 160 sweeps both were fine.)

**3. Why 26.9's compensated sum "did nothing", and why the residue sweep counted 0.** The GPU compiler folds `(a + b) - a`
into `b` unless the arithmetic is `precise` (GLSL NoContraction). Every "exact difference" in the kernels was the rounded
one: compaction's `fa - (nb - fb)`, the sweep's count of the unheld part (`fc - (sum - fn)`, always 0 - 2.4e-6 m^3 went
uncounted in 27 s), and 26.9's Kahan remainder.

**4. The fix (GPU, `precise` two-sum).** `twoSumErr(a, b, s)` in `water_core.glsl` (Knuth: a + b == s + err exactly). A new
per-cell buffer `fLo` holds what the float store could not hold:
- the gather (`wc_fill_advect` mode 4) adds the cell's remainder to its change and keeps the new rounding error in `fLo`; a
  dry cell nothing reaches keeps its remainder (no film from nothing), and a remainder that would take a cell below zero
  waits;
- the column carry (mode 5), compaction, the ledger, deposits and droplet sprays move water with `addExact` (rounding error
  into the cell's `fLo`); the residue sweep merges exactly and the emptied cell's remainder follows its water down (a
  dropped residue counts its remainder too);
- `fLo` is zeroed on create and upload (it belonged to the water that was there), zeroed for a fresh brick, and moves with
  a brick when slots are compacted. It is never drawn or reported: it is below one float step of its cell.
The CPU is unchanged (it accumulates the transport in double; its still pond was already exact).

**5. Measured.** `DiagRestingBodyKeepsTheWater` (27.5 s): guess on **-1.5e-7 m^3** (was -1.82e-4; ~1200x less), cold
+3e-8; the stage table shows compaction and the sweep now handing their rounding to `fLo` and the gather paying
it back. All GPU water tests pass except the known FLIP row - **`GpuRestDecisionDeterministic` is green** (still GPU water
settles and sleeps); CPU water tests 302/302. Live (WaterBench_Small, the 4 x 4 pond at the threshold): the still pond on the
GPU is quiet within 2 s and its mass flat for 30 s (was never still, +7e-6 m^3 in 24 s); with auto-sleep it sleeps into
spans. The slosh (25 s): guess off +-0, on -4.5e-7 m^3; the solve's worst residual in the moving slosh 0.06-0.2 with the
guess vs 2-3 without; the waves calm on the same schedule (KE 3e-4 vs 3e-5 at 25 s, one noisy sample each). Pebble pair:
both stones settle at 1.50 m down either way.

**6. On by default.** `WaterCoreGpu::s_stillWaterGuess = true`. A/B: `/api/debug/water_transport {still_water_guess}`.
**For the owner:** `docs/evidence/water_core_e/slosh_live_p2610_ab.mp4` and `pebble_pair_p2610_ab.mp4` (left = guess off,
as signed off; right = guess on), plus the `_off` / `_on` clips and JSON.

**Still open:** the cost (26.9 point 6 - about 1.2 ms of a creek call is not the solve; flat bricks and fewer dispatches);
the FLIP row. **Design keys:** a per-cell storage detail, a pure function of the water; no chunk or brick dependence (the
remainder moves with its brick); no new parameters.

### 26.11 The cost, measured per stage (2026-10-14). Status: BUILT (no change to how water moves; the surface the renderer gets is the dense box's)

**The ask:** 26.9 point 6 - the creek cost "about 1.2 ms is not the solve". Measured, not argued.

**1. A GPU clock per stage.** Test hook `WaterCoreGpu::s_kernelTiming`: on single-tick calls the step writes a timestamp
after each stage (one extra barrier each, so totals read a little high) and accumulates `s_tsMs[label]`;
`DISABLED_ACreekStoredSparseFitsTheBudget` prints the table. The creek (592 bricks, 303,104 stepped cells, 40 sweeps), GPU
1.63 ms a call: the sweeps 0.45, **the surface copy 0.37 and the births copy 0.20** (both to host memory, every stored
column, every call), the extrapolation 0.20 (33 buffer copies a substep), the reductions 0.15, everything else < 0.06 each.

**2. Only the water's columns cross to the host** (`wc_surface.comp`). A dry column's record is "no water, ground at its solid
top" - the host rebuilds it from the upload's occupancy (`Volume::hostSolidTop`, the whole column: a bricked volume now reads
the dense box's ground where a brick is absent, where it read 0 before). Wet columns append to packed lists by atomics with
their dense column index: one-run columns 32 B (top, bottom, u, w, body), several-run columns the full 64 B record; columns
with droplet births their 8 floats (`birthsPacked`). The host scatters by index, so the field is the same whatever the order.
The copy takes the last call's length + 25 % + 64 (every column on a first call); a list that grew past that is completed
with one more fenced copy. Gate: `SparseEqualsDenseAcrossSeams` now also compares the whole surface field the renderer gets,
byte for byte, every tick (dam break, slosh, stone entry, pump, drain): equal. `GpuSurfaceFieldMatchesCpu` (one- and
multi-run columns) passes.

**3. The extrapolation without copies.** Four descriptor sets per lattice: the masks read the lattice, the three layers
ping-pong A/B by set, the finish writes the lattice in place (`origLat` scratch deleted). Bit-identical (every GPU test's
numbers unchanged, the FLIP row's to the last digit).

**4. Measured (Release, the creek, median of 30 calls).** At the engine's 27 sweeps: **1.02 ms a call** (was ~1.6), GPU 0.79
ms: sweeps 0.32, surface copy 0.09, extrapolation 0.085, transport 0.056, reductions 0.054, the rest < 0.035 each. At 40
sweeps 1.32 ms. p90 2.06 ms at 27 sweeps: host jitter at the fence (the GPU sum does not vary). Live (WaterBench_Small slosh,
25 s): the volume's step 1.03 -> 0.83 ms median; the pond drawn as before (`slosh_live_p2611.mp4`, `pebble_pair_p2611.mp4`).

**Left on the table, by size:** the sweeps (0.32 ms: brick-local sweeps in shared memory would cut the dispatch count -
changes convergence, needs its own gate); flat bricks for shallow water (the creek steps 11.4 cells per water cell); the
reductions fused into fewer passes (~0.02 ms); one fewer fence per call by reading last call's results (a frame of latency -
a design change for the coupling). **Design keys:** cost only; the drawn surface is a pure function of the water and the
grid's occupancy (now also for absent bricks); no new parameters.

### 26.12 P2b - the engine on bricks: design, check, build (2026-10-14). Status: BUILT, not signed off

**Measured first (the design is the measurement's).** `WaterCoreGpuParityTest.DISABLED_DiagEngineCreekBox`: the ENGINE's
path (`WaterCoreManager` + GPU) on the creek bench's box - 100 x 4 x 2 m at 1/9 m (900 x 18 x 36 = 583,200 cells: inside
the manager's 2 M-cell cap), a 2 m channel 1 m deep, water flowing, one 1/3 m body drifting along the surface. Before P2b,
per frame (median of 120): **feeding the one body 20.9 ms**, `update` 7.1 ms (GPU step 3.3; the ripple layer's share ~5 ms
was really the surface read below), the surface read 0.34 ms; a forced read of the CPU copy (`probe`, `sleep`) **257 ms**.
Causes, each found by switching it alone:
1. the body feed ran over EVERY cell of the box three times on the CPU (raster zeroing, wake ageing, the copy) and uploaded
   the whole solid field (9.3 MB) every frame;
2. the GPU volume kept every brick (the engine never asked for sparse);
3. the staging memory the CPU reads back from was uncached - a 512 KB surface list read in ~5 ms, a full read-back 257 ms;
4. the editor asked for `surfaceCells()` every frame - a per-voxel list only the Cells debug draw uses - which reads the whole
   volume back once a second.

**Design (P2b scope: what P3's benches need).** The CPU copy of a volume (`Av::grid`, the 2 M-cell cap) STAYS dense: the
creek bench's box fits, and its readers (probes, write-back, the CPU reference) are not per-frame. What must not scale with
the box is every per-frame path:
- *Bodies:* `WaterSolver::updateSolidFields` works only in the box round where a body is, was, or left its wake (grown by
  the wake's 3 cells): every cell with s, wake or fresh non-zero lies in `m_solidBox`, so outside the box all three are 0
  before and after - bit-identical to the whole-grid pass, sums in memory order either way (`rasterizeSolidsIn`).
  `WaterCoreGpu::setSolidsBox` updates the host copy there and uploads only the stored bricks the box touches; an absent
  brick takes its bodies from the host copy when it is stored (as before).
- *Storage:* engine fill volumes are created SPARSE (`BrickOptions{sparse}`; capacity = the whole box, so never full);
  particle (FLIP) volumes stay dense (26.6 point 7). The record echoes `gpu_bricks` / `gpu_brick_capacity`.
- *Read-back:* staging is allocated in HOST_CACHED memory when the device offers it.
- *Debug cells:* built only when the water render mode is Cells.
- *Not in P2b (logged):* a sparse CPU copy (needed only beyond 2 M cells - P5's question); the ripple layer's
  camera-following window (26.4 gap 1) - **CORRECTION (same day): the creek bench's volume gets NO ripple layer** - its
  lattice would be 900 x 36 and the cap is 512 per axis (`kRippleMaxCells`), so it is refused; the window is REQUIRED before
  the creek runs as pond water (the owner's standing rule: ripples carry forward) and moves to P3's first step; the
  `m_fields` copy per `surfaceFields()` call (~0.3 ms on this box).

**Design-check (Feature Design Keys).** *Look:* nothing drawn changes - the fields are bit-identical and the surface
equality (26.11) holds. *Chunks:* bricks are world-aligned (26.7); the body box is in grid cells of world-aligned bodies; no
chunk or camera input. *Generation:* none. *API:* `water_av_list` echoes `gpu_bricks`, `gpu_brick_capacity`; no new
parameter; defaults change only in storage (results equal). *Visual test:* the bench pond and pebble pair live (unchanged),
the creek box headless (the numbers). *Small rigs + controls:* the equality tests below, each with the whole-grid path as its
control. **Verdict: READY** (built the same day).

**Gates (red-before-green where the rule is new).**
- `WaterSolidTest.ABoxedFeedEqualsTheWholeGrid` (CPU, 400 frames: a fresh body, a second crossing it, both leaving through a
  side, then the wake running out; a negative-origin grid): every cell of s, the wake and fresh, and every report sum,
  identical to the whole-grid pass (the test hook `WaterSolver::s_solidFieldsWhole`); the box smaller than the grid in
  400 / 400 frames.
- `WaterCoreGpuParityTest.GpuBoxedSolidsEqualTheWhole` (GPU, 150 ticks, sparse bricks, a stone dropped in and dragged):
  the device's solid buffer (s, q, wake, ledger) and every fill identical between `setSolids` and `setSolidsBox`.
- All GPU water tests pass but the known FLIP row; CPU water tests 303 / 303.

**Measured after (the same rig).** Body feed **0.36 ms** (was 20.9), `update` **2.24 ms** (was 7.1; GPU step 1.5, ripples
~0.5), surface read 0.31 ms, a forced read-back **14 ms** (was 257), create 76 ms (was 92). Live (WaterBench_Small): the
slosh's step 0.83 -> 0.78 ms median, the pond 27 / 27 bricks (the small box is all water and halo), pebble pair and slosh as
before (`slosh_live_p2612.mp4`, `pebble_pair_p2612.mp4`).

**What it opens:** P3 - the creek bench run as pond water in one sparse volume (its box fits; its frame on this rig is ~2.5
ms with one body, of which the GPU step is 1.5) - once its ripple layer is a camera-following window (above: refused today).

### 26.13 P3 - creeks and ponds on one model: detailed design and design-check (2026-10-14). Status: DESIGN, READY for P3a

**What P3 delivers (26.3):** the creek bench, the dam & pond bench and the pond benches all run as POND water (one sparse
volume each, 26.12), meeting the numbers the column solver met; ripples and splashes as on the pond; then the column regions
removed from near water. **Checked against the code before writing this - four things the pond core cannot do yet:**
1. *Ripples on a long volume.* A volume's ripple layer covers its whole box and is REFUSED above 512 lattice cells per axis
   (57 m) - the creek bench's 100 m volume would have none. The column regions already have the answer (25.26 RC): a
   384 x 384 window that follows the camera in 4 m steps, an absorbing (open) border, carried by the current, forced by the
   flow's bed shear (u* from Manning).
2. *An outfall.* A volume has pumps and fixed-rate sinks (one cell); nothing takes "whatever water arrives" the way the
   column outfall does (it removes all depth above the bed at its columns, counted). A fixed-rate sink either starves or
   floods the foot.
3. *Manning at the bed on the GPU.* P1's bed law (`WaterSolver::applyBedManning`, u /= 1 + dt g n^2 |u| / d^(4/3) on every
   horizontal face, d the water over the face) is CPU only; the GPU core has pond friction alone.
4. *The sweep count.* The automatic count is 1.5 x the longest dimension, clamped at 160 - a 900-cell creek box gets 160
   sweeps (~2 ms). With the still-water first guess (26.10) the solve starts near the answer; the count is measured, not
   derived from the box (P3b sets it from the creek's measured residual).

**P3a - the pond core gets what a creek needs (one law each, CPU and GPU; small rigs):**
- *Ripple window for every volume* - ONE code path for volumes and regions (no ported copy): `stepRipples` builds a
  volume's layer as a window of min(384, box) lattice cells per axis placed and moved exactly as the regions' (centre on the
  camera, clamped to the box, 4 m steps, `syncPrev`/`shiftOrigin`/`markDirty` on the GPU ripple step); the border is open
  only where the window is smaller than the box (a pond that fits keeps its reflecting walls - the signed-off pond look is
  unchanged); the mask from the volume's surface field (as today); the window is CARRIED by the field's surface velocity
  (u, w per sub-column - SurfaceColumn already carries them) and FORCED by u* where the volume has a bed roughness (n > 0);
  a pond (n = 0) is neither forced nor carried differently from today (its surface velocity is ~0 at rest). The refusal at
  512 goes; the four-layer cap stays (the renderer's atlas).
- *Outfall* - a source KIND: an outfall at a cell takes every drop in that cell and the cells above it up to the first
  solid, every substep, after the transport (the column outfall's rule at the pond's resolution), counted in the source's
  `placedTotal` (negative) and the step's `sinkRemoved` - the ledger stays exact. GPU: `GpuSource.pad0` = kind (1 =
  outfall) in `wc_sources.comp`; CPU: `SourceSpec::outfall` in `applySources`. No projection inflow term (it removes what
  arrived, as the column outfall does after its solve).
- *Manning at the bed on the GPU* - `wc_face_ops` mode 10 on the u and w lattices: each face sums its two columns' water
  (the CPU's order) and divides as the CPU does; replaces pond friction when n > 0 (as on the CPU).
- *API* - `water_av_create` takes `manning_n` (default 0 = pond friction, unchanged); `water_av_source` takes
  `outfall: true` (rate ignored); both echoed in `water_av_list` (`manning_n`, per volume `outfalls`).

**P3a gates (red first where new):** `WaterCoreGpuParityTest.GpuBedManningMatchesCpu` - a flat 1/9 m channel started at
0.5 m/s, 3 s under n = 0.035: GPU face velocities within 1e-4 of the CPU's (red before: the GPU ignores n, ~10 % apart);
`WaterCoreTest.AnOutfallTakesWhatArrives` (CPU) and `GpuOutfallTakesWhatArrives` - a pump at one end of a closed channel,
an outfall at the other: at steady state outflow = inflow within 2 %, the volume's water constant, the ledger
(stored + out - in) within 1e-6 m^3 (red before: no outfall kind - the water piles up); a ripple-window rig - a 100 m volume
gets a layer (red: refused today) that moves with the view point, and the bench pond's layer is the same size and border as
before (the pond unchanged). The pond suite and the GPU parity suite unchanged.

**P3b - the creek bench as pond water (L4, the owner looks):** `tools/water_creek_bench.py --run --pond` creates one sparse
volume over the creek (x 10-109, z 29-34, y 14-17 at 1/9 m - inside the 2 M cap; the bed is the carved gravel voxels as
occupancy - the 1/3 m staircase of a 1:200 line, the same ground the column regions ran over), springs as pumps across the
head, outfalls across the foot, `manning_n` 0.035, the camera on the creek; `--record` writes the same JSON fields as the
column run (mid depth and speed from `water_av_probe`, in/out/stored/ledger from the sources' totals). **Gates:** mid-reach
depth and speed within 10 % of Manning at q 0.2 and 0.5 m^3/s (q 0.1's 0.11 m is one cell - reported, the trickle limit
26.4 risk 2), out/in -> 1 at steady state, ledger <= 1e-4 m^3, frame cost reported against 25.10 (the creek's share);
ripples visible and carried (the RC clip re-shot at its pose). Old/new A/B clips for the owner: `creekbench_q*` (column)
vs `creekbench_pond_q*`.

**P3c - the dam & pond bench as pond water** (25.38's FILL / DAM / BREAK numbers: fill rate Q/A, spill at the crest, the
dam's dip, the break's surge, the ledger exact across edits) - designed in detail when P3b's numbers are in (its box is
118 x 5 x 10 m (x, y, z) - 4.3 M cells at 1/9 m: OVER the 2 M cap - it needs the sparse CPU copy P2b deferred, or a box per reach).
**P3d - the column regions removed from near water** once P3b and P3c pass and the owner signs off.

**Design-check (Feature Design Keys).** *Voxel look:* the pond's surface, ripples and droplets, already signed off; the
window's open border fades the forcing over its outer 8 m so its edge is never seen (25.26). *Chunks:* the window follows
the CAMERA - a cost bound like the column regions' (its content is a function of world position: lattice cells are world-
aligned and the forcing hash is per world cell and frame) - the same ruling 25.26 made; the outfall and Manning are per
cell / per face, world-addressed. *Generation:* none (runtime; the bench's carve is authored). *API:* two new params, both
echoed, defaults unchanged (`manning_n` 0, no outfall). *Visual test:* the creek bench at its RC pose, old (column) vs new
(pond) clips, plus the numbers. *Small rigs and controls:* each P3a gate is a unit rig with the CPU (or the old path) as
its control. **Verdict: READY for P3a.**

### 26.14 P3a built; P3b - the creek bench as pond water, first run (2026-10-14). Status: P3a BUILT; P3b's LOOK signed off by the owner 2026-10-15 ("they all look really good"); its numbers not passed

**P3a (26.13's list), each red-before-green:**
- *The ripple window for every volume* - `WaterCoreManager::rippleWindow`, one rule for volumes and column regions
  (`WaterColumnTest.EveryVolumeGetsTheRippleWindow`: a 100 m volume gets a 384-cell window that follows the view, clamped to
  its box - refused before; a 4 x 4 m pond's layer is its whole box at its corner, unmoved - as before). A volume with
  `manning_n` > 0 is carried by its surface velocity and forced by u* (the regions' 25.26 rule); a pond steps as before.
- *Outfalls* - `SourceSpec::outfall` / `GpuSource.kind = 1`: the cell and the cells above it emptied every substep, counted
  (`WaterCoreTest.AnOutfallTakesWhatArrives`, `GpuOutfallTakesWhatArrives`: a pump and an outfall on a 4 m channel - last 30 s
  out 0.06048 vs in 0.06000 m^3, ledger 2e-7 (CPU) / 3e-6 (GPU) m^3; CPU and GPU agree to the printed digit).
- *Manning at the bed on the GPU* - `wc_face_ops` mode 10 (`GpuBedManningMatchesCpu`: 0.36 % from the CPU; the law takes 10 %
  off the rig's frictionless slosh at n 0.1).
- *Found live, fixed:* a pump added to a SLEEPING GPU volume never ran - `setSources` did not wake it (the creek's empty
  volume slept in half a second and its springs placed nothing). `GpuAPumpWakesASleepingVolume` (red: 0 m^3 -> 0.001 m^3).
- *API:* `water_av_create {manning_n}`, `water_av_source {outfall: true}`, `water_av_column {points}` (the drawn surface at
  voxel columns, no read-back; registered in EngineAPIServer), the record's `source_in_m3`, `outfall_out_m3`, `outfalls`,
  `manning_n`. Tool: `tools/water_creek_bench.py --run --pond [--q] [--sweeps] [--record]`.
- All GPU water tests pass but the known FLIP row (33); CPU water tests 305 / 305.

**P3b first run (WaterBench_Creek, q 0.2 m^3/s, one sparse GPU volume of 1,749,600 cells over the creek, 40 sweeps,
n 0.035; `creekbench_pond_q0.2.{json,mp4}` = the fill, `creekbench_pond_q0.2_steady.mp4` = steady flow):**
- *The fill:* the front runs the dry gravel at ~0.6 m/s and reaches the foot at ~170 s; the ledger holds through it (in
  32.207, stored 32.207 m^3 at 160 s).
- *Steady (after ~7 min):* out/in 0.991 over 20 s, stored flat (44.30 -> 44.34 m^3), ledger 7e-5 m^3 (gate 1e-4: met).
- *Ripples:* one window (384 x 54), awake, flow-forced; facet slopes 0.05-0.2 (the band 25.29 tuned the column creek to).
- *The depth - NOT comparable to Manning on this bed.* The pond model flows over the carve's VOXELS: the bed is FLAT from
  x 11 to 77 (15.33 m) and from 78 to 109 (15.0 m) with a 1/3 m drop between (the 1:200 line floored to the 1/3 m grid).
  Steady profile: 0.31 m at the head of each flat reach falling to 0.16 m at the drop's edge (the drawdown to critical depth
  over a step - yc = 0.10 m for 0.1 m^2/s). The column run flowed over the LINE (`bed: plane`, option (a)), so its Manning
  comparison (0.165 m at q 0.2) is a different creek. On the flat reach the surface's friction slope reads ~1.3-1.6x
  Manning's for the measured depth and speed (the 2 m channel's walls account for ~1.3x by hydraulic radius; the rest is
  not explained, and the solve was not converged - residual 0.6 at 40 sweeps).
- *The cost - 3x over.* ~6 ms a call (the record's wall time, live, with the renderer sharing the GPU) at 1,216 bricks:
  the creek's water (2 m x 0.2-0.3 m) straddles brick layers, so ~4 brick layers of 8 are stored per brick column.

**Open (P3b is not passed):**
1. *The same bed for both models* - run the column solver over the VOXEL bed (its default, not `plane`) beside the pond run
   for the depth/speed comparison, or re-carve the bench at the 1/9 m grid (drops of 1/9 m every 22 m - closer to the line)
   and run both. The owner's choice of bench decides which (the column creek's look was signed off on the line).
2. *The cost of a creek-length volume:* flat bricks (8 x 2 x 8 - the creek's water is 2-3 cells deep), the sweep count with
   the guess (measure the residual at 20-40), and the live figure without the renderer's share (GPU timestamps).
3. *The drag excess* on the flat reach, once the solve is converged and the walls accounted for.

### 26.15 The bench re-carved at 1/9 m; both models on the same bed; the books; where the call's time goes (2026-10-15). Status: BUILT, not signed off

**The ask (owner, 2026-10-15: "go with B"):** 26.14 open item 1, option (b) - re-carve the creek bench at the 1/9 m grid and
run both models on it.

**The carve.** `tools/water_creek_bench.py --build` now floors the 1:200 line to the 1/9 m grid (`--grid 9`, the default; `--grid
3` is the 25.22 carve): gravel subcubes for whole thirds, gravel microcubes for the rest - 1,224 subcubes + 19,764 microcubes.
The bed is flat reaches 22 m long with 1/9 m drops between (15.556 m at x 11-32, 15.444 at 33-54, 15.333 at 55-76, 15.222 at
77-99, 15.111 at 100-109). Checked in the WORLD, not the API reply: a pond volume over the creek reads Solid one half-cell below
the carve's top and Air one half-cell above it at all 300 sampled columns (x 10-109, three z each). The column regions'
`voxel` bed reads the same micro cells (`refreshColumnSolids`), so both models now stand on identical ground (`--bed voxel` on
the column run; `plane` remains its 25.22 option (a)).

**The comparison** (`tools/water_creek_profile.py --model pond|column`: the drawn surface over every voxel column minus the
carve, the mean of 10 reads 1 s apart, each reach's mean, and the mean over the three whole reaches - x 33-99; the head and
foot reaches are cut by the springs and the outfalls). Manning for a 2 m channel on 1:200 at n 0.035, wide (q/b) and with the
walls (hydraulic radius):

| q (m^3/s) | Manning wide | Manning, walls | column, voxel bed | pond (1/9 m core) | pond / column |
|---|---|---|---|---|---|
| 0.2 | 0.165 m | 0.176 m | 0.189 m | 0.199 m | 1.06 |
| 0.5 | 0.285 m | 0.319 m | 0.305 m | 0.336 m (80 sweeps: 0.337) | 1.10 |

- Both models run DEEPER than Manning on this bed: the 1/9 m drops cost head (the flow draws down to ~0.11-0.14 m at each
  edge (q 0.2) and expands below it) - the column model, which has no wall friction, is 1.14x / 1.07x Manning-wide.
- **[Corrected by 26.20: the gauge below read the highest drawn sub-column; the WATER is at the column model's depth - 0.187 /
  0.306 m - and the drawn surface stands high on air inside the moving water.]** The pond model is 6 % / 10 % deeper than the column model on the same ground, and its reaches deepen slightly downstream at
  q 0.5 (0.324 -> 0.340 m; the column's stay at 0.306). The pressure solve is NOT the cause: 80 sweeps gives the same depth as
  40 (0.337 vs 0.336 m). Nor is the bed law: `applyBedManning` (and GPU mode 10) divides every layer's face velocity by the
  same depth-averaged factor, which is the column law exactly. Open: where the extra drag comes from (the 3-D core resolves
  the flow over each drop - the column model cannot - and that is a candidate, not a finding). 26.13's gate (10 % of Manning) assumed a smooth bed; on a stepped one the fair
  reference is the column model on the same steps - within 10 % at q 0.2, on the edge at q 0.5.
- Steady: out/in 0.998-1.001, stored flat (q 0.5: 60.2 m^3 from 200 s on).

**The books - a counting fault, found and fixed.** The q 0.2 run's ledger drifted to -7.7e-4 m^3 after 118 m^3 had passed
through, growing with the throughput, not the water. The GPU kept each source's running total as a float: a substep's 1e-5 m^3
added to 100 m^3 rounds by up to 4e-6. Fix: `GpuSource.placedLo` (was a pad) holds the total's rounding remainder (two-sum, as
26.10's `fLo`), the host reads `total()` = hi + lo and writes `setTotal()`. Red-before-green:
`WaterCoreGpuParityTest.GpuSourceTotalsKeepTheBooksAfterALongRun` (the outfall rig with 99 m^3 already counted through): ledger
+1.47e-2 -> +8.3e-7 m^3; the plain outfall test 2.7e-6 -> 4e-8. Live after: -3.4e-6 m^3 after 84 m^3 through (gate 1e-4).

**Where the call's time goes - measured live.** The record now echoes `gpu_device_ms` (two GPU timestamps round the call's
main submission), `gpu_host_ms` (residency), `gpu_wait_ms` (submit + fence) and `gpu_ticks`. The creek at q 0.2 (1,351 bricks,
40 sweeps, one tick a call): **device 1.85 ms, host 0.06 ms, wait 5.78 ms, wall 6.12 ms**. The GPU's own work is 1.85 ms; the
other ~4 ms is the main thread blocked at the fence while the queue finishes the RENDERER's work submitted ahead of it. The frame
shows it: 5.6 ms (178 fps) with the water paused (`water_av_realtime off`), 12.9 ms (77 fps) with it stepping. **The cost to
fix first is the wait, not the kernels** - 26.16.

Evidence: `docs/evidence/water_core_h1/creekbench_pond_g9_q{0.2,0.5}.json` + `_steady.mp4` (the last 60 s; q 0.2: the 7-min run after the fix, with
the timing split; q 0.5: the 80-sweep run), `creekbench_g9_voxbed_q{0.2,0.5}.json` + `_steady.mp4` (the column model on the voxel bed),
`creekbench_{pond,column}_g9_q{0.2,0.5}_profile.json`. Tests: GPU water 34 pass (the known FLIP row fails), CPU water 330 / 330.

### 26.16 The water's call not waited on - design, check, build (2026-10-15). Status: BUILT, not signed off

**The problem (26.15, measured live):** each frame the engine records the water's call, submits it to the graphics queue and
WAITS for its fence. The queue still holds the renderer's frame, so the main thread sits ~4 ms behind the renderer's work plus
1.85 ms of the water's own: 12.9 ms frames stepping vs 5.6 ms paused. The ripple step met the same thing in 25.34 and does not
wait (`RippleGpu`: submit now, collect next frame). The water core gets the same protocol.

**The protocol - one call in flight per volume:**
- `step(vol)` in ASYNC mode: (1) finish the call in flight, if any (`finishCall`: wait on the volume's own fence - normally
  already signalled, a frame has passed - then everything `step` does today after its fence: stats, sleep rule, the brick
  record, the halo check, the list-growth copy); (2) record the next call exactly as today (residency from the record just
  collected, CFL from its max speed); (3) submit with the volume's fence, NO wait; return the stats of the call collected in (1).
- What the host reads of a call is copied out of staging in `finishCall` into host memory (the surface lists, the birth list,
  the sources - their copy rides the call itself, so the per-step `readSources` submission goes too). `readSurface`,
  `readBirths`, `readSources` read those copies and never wait. The staging buffer is touched by the host only between a
  finish and the next submit - never while a call that writes it is in flight.
- Every other entry that touches staging or the device state from the host (`upload`, `setSources`, `setSolids`,
  `setSolidsBox`, `download`, `readCells`, `setParticles`, `readParticles`, `destroyVolume`, switching async off) first
  finishes the call in flight (`settle`). The body feed runs once a frame before the step: the call it settles was submitted a
  frame earlier.
- SYNC mode (the default - every test, every non-realtime call) is the same code with the finish right after the submit:
  unchanged behaviour. The manager turns ASYNC on for a GPU volume while it steps in realtime.

**What the water looks like - one frame late, nothing else.** The GPU runs the same commands in the same order in both modes:
call k's residency reads call k-1's record and call k's substep count reads call k-1's max speed in both. So the water's
STATE is bit-identical; only what the host sees lags one call: the drawn surface is one frame (~16 ms) older (the ripple
heights already are), droplets are born one frame later, the record's numbers are the previous call's, and a volume falls
asleep one call later.

**Design keys.** *Look:* unchanged (a 16 ms lag in what is drawn; the renderer already draws the ripple layer up to kRing frames
behind). *Chunks:* none involved. *Generation:* none. *API:* the record's `gpu_*` timings describe the call collected; a new
`gpu_async` field echoes the mode; nothing else changes. *Visual test:* the creek bench at q 0.2 - frame time with the water
stepping vs paused, before / after, and the clip (the water must look the same). *Small rig and control:*
`WaterCoreGpuParityTest.GpuAsyncEqualsSync` - the 26.13 outfall rig and a sloshing tank, 60 calls each in sync and in async
mode: every cell's f, every face's velocity and the source totals bit-identical after the last call is settled, and the
stats returned by async call k equal sync call k-1's (red before: there is no async mode). **Verdict: READY.**

**Built (2026-10-15).** `WaterCoreGpu::setAsync` / `settle` / `finishCall`; the step submits on the volume's own fence; the
manager turns ASYNC on for GPU volumes while realtime (`water_av_list` echoes `gpu_async`). The surface lists, the birth list
and the sources' totals are copied to host memory when a call is collected (`hostSurf`, `hostBirths`, `hostSources`); the
sources ride the call (the per-step `readSources` submission is gone in both modes).
- `GpuAsyncEqualsSync` (red with a do-nothing `setAsync`: 0 of 149 stats lagged): a sparse dam-break tank with a pump and 6
  outfalls, 150 calls each way - f, u, v, w and the source totals bit-identical, 149 of 149 stats exactly one call behind.
- **Found building it, fixed:** the creek bench's first async run read a ledger of 7.6e-3 m^3, constant from the start. Adding a
  source re-sends the whole list from the solver's copy, whose totals are one call old in async mode - the call in flight had
  placed water the re-sent totals no longer counted (18 springs added one by one). `pushSourcesToGpu` now finishes the call in
  flight and takes the GPU's totals first (`pullSourceTotals`, also after a full download, so the record's mass and totals are
  always the same call's). `GpuAsyncEngineKeepsTheBooksAcrossSourceEdits` (the engine path, realtime, 6 springs added mid-run,
  10 s): worst |mass - (in - out)| 1.5e-3 -> 4.3e-8 m^3.
- GPU water tests: 37 pass (the known FLIP row fails); CPU water 330 / 330.

**Live, the creek bench at q 0.2 (1,352 bricks, 40 sweeps):** host time per call 6.1 -> 0.40 ms (the fence wait 5.8 -> 0.003
ms); device 1.70 ms; **frame 12.9 -> 7.4 ms (77 -> 135 fps)** against 5.5 ms (181 fps) with the water paused - the water's
share of the frame is now ~1.9 ms, about its GPU time. Steady out/in 1.003, ledger -2.1e-6 m^3. The look: unchanged (clips
`creekbench_pond_g9_q0.2_async_{fill,steady}.mp4`; `creekbench_pond_g9_q0.2.json` is now this run's record).

**What is left of the cost:** the 2 ms budget (25.10) covers all water; the creek alone takes ~1.7-1.9 ms of GPU. The next lever
is the cells stepped - the creek's water is 2-3 cells deep but stored in 8-cell-tall bricks (~4 brick layers per column with
the halo): flat bricks (8 x 2 x 8 or 8 x 4 x 8), measured on this bench.

### 26.17 Flat bricks - design, check, build (2026-10-15). Status: DONE, default 2-tall; signed off by the owner 2026-10-16 ("signed off. looks good")

**The ask (owner, 2026-10-15: "get it done"):** cut the creek's GPU time (26.16: ~1.7-1.9 ms of a 2 ms budget that must cover
ALL water) by storing shallow water in flat bricks.

**What the cost scales with - measured first** (`DISABLED_ACreekStoredSparseFitsTheBudget`, 40 sweeps, Release, timestamps per
stage): the same creek stored sparse (303,104 cells) costs 0.86 ms of GPU a call, stored dense (7,102,464 cells) 10.65 ms. So a
call is about **0.42 ms fixed + 1.44 ms per million stored cells** (the sweeps alone: 0.20 ms fixed for 80 dispatches + 0.56 ms per
million). Flat bricks cut only the second term. That test's creek rests on the box floor (the water in brick layer 0 only):
flat bricks would store exactly what 8^3 bricks store there - the saving is not in the water's own layers.

**Where the bench's cells go - predicted, then checked against the live count.** A predictor
(`tools/water_brick_predict.py`) applies the residency rule (liquid reach 4 + travel; per-face distances; Chebyshev rings) to
the bench's carve and its measured q 0.2 depth profile. It predicts 1,316 bricks of 8^3 at travel 2; the live record holds 1,351
(within 3 %). Stored cells for the same water (34,344 water cells):

| brick (x y z) | stored cells | per water cell | + bricks that are all bed not stored (26.18) |
|---|---|---|---|
| 8 x 8 x 8 (today) | 673,792 | 19.6 | 442,368 |
| 8 x 4 x 8 | 492,544 | 14.3 | 310,272 |
| **8 x 2 x 8** | **436,736 (-35 %)** | 12.7 | **262,144 (-61 %)** |
| 4 x 4 x 4 | 487,424 | 14.2 | 256,896 |

The halo is what costs: reach 6 cells round the water, which is 6 cells up into air and 6 cells DOWN into the bed. An 8-tall
brick rounds both up to whole layers (3 layers = 24 cells for a 15-cell span); a 2-tall one follows them. The bed bricks under
the water (all Solid, no water) are a second lever of the same size (26.18, next). Predicted GPU on the bench: -0.35 ms for flat
bricks, -0.6 ms with both.

**The design - storage only, nothing about the water changes.**
1. **A brick is 8 x BY x 8, BY in {2, 4, 8}, per volume** (`BrickOptions::height`; 0 = the engine default
   `WaterCoreGpu::s_brickHeight`). x and z stay 8, so a brick COLUMN is still 8 x 8 columns: the column table, the column
   kernels (64 threads per brick column), the surface and birth lists are untouched. The shader reads `pc.bly` = log2 BY (a new
   push field; the block grows 108 -> 112 bytes, under the 128-byte floor).
2. **A brick's cells are laid y-slowest:** local = lx + 8 lz + 64 ly (was lx + 8 (ly + 8 lz)). A brick of height BY is BY/2
   consecutive 128-cell UNITS (8 x 2 x 8 slabs), each in the same order whatever BY is.
3. **Reductions per unit, folded in a fixed world order.** The sums the rest and settle decisions read (kinetic energy, total
   fill; mode 0) are taken per 128-cell unit (each 256-thread group folds its two halves separately) and gathered into
   the volume's UNIT map (x 8 cells, y 2 cells, z 8 cells, world-aligned; an absent unit as zero) before the fixed tree.
   The tree is then the same for every brick height and every residency, so 8 x 2 x 8 equals 8^3 bit for bit - the same
   argument 26.8 used for sparse vs dense. (The maxima are order-free. The dropped-residue sum - a ledger figure, read by
   no decision - folds in column-table order, which can differ by an empty column: equal to 1e-12, not bit for bit.)
4. **The halo per axis.** `markNeed` walks rings per axis until the reach is covered (ring k reaches dist + 1 + BY (k - 1) cells),
   instead of one ring count for all axes: a 2-tall brick needs 3 rings up and down for reach 6.
5. **Host addressing follows** (`storageIndex`, `scatterSlots`, `recordSlotUploads`, `compactSlots`, `setSolidsBox`,
   `download`, `needFromDenseFill`, `cellToRecord`, `liveCells`, the allocation): the brick size is the volume's, never a
   constant. `wc_bricks.comp`: 64 threads x BY cells, distances 0..BY-1 (3 bits still hold them).
6. **API:** `water_av_create` takes `brick_height` (2, 4 or 8; other values refused with the reason); the record echoes
   `gpu_brick_height`. The default stays 8 until the bench measures the change; then the measured best becomes the default
   in the same commit as its numbers.

**Design check (Feature Design Keys).** *Voxel look:* unchanged - storage only; the water is the same 1/9 m cells (one water
model). *Chunks:* brick shape and residency are world-aligned and decide COST only; what a cell holds never depends on them -
pinned by the equality gate below (bricks of every height straddle the chunk seam at world cell x 288 and negative z, as in
26.8). *Generation pipeline:* none. *API:* `brick_height` in, echoed out; an unknown height is refused, never rounded. *Default
change:* only after measurement, with the pinned tests run on it. *Test world:* the 26.8 scenes (40 x 16 x 24 cells, one
seam, odd y origin so 2- and 4-tall bricks are offset in their brick), plus one new scene - a creek over a stepped bed (water
over solid ground, the case flat bricks are for).

**Gates (red first):**
- `FlatBricksEqualCubic` - the six scenes, 8^3 with every brick kept vs 8 x 2 x 8 and 8 x 4 x 8 sparse: every f, u, v, w, p
  equal with ==, every tick; the rest decision and the substeps equal; the drawn surface byte for byte; halo short 0; the
  absent brick clean; fewer cells stepped than the dense run. RED first: built with today's single ring count (one ring of
  2 cells up and down), it must show a short halo and differing fields.
- `SparseEqualsDenseAcrossSeams`, `AnyOriginKeepsTheWater` (+ origins for 2- and 4-tall), the GPU parity suite, CPU 330: unchanged.
- **Cost, on the bench, live:** `gpu_bricks`, stored cells and `gpu_device_ms` for 8, 4 and 2 at q 0.2, same carve, same
  flow; prediction above written before the run. Then the clip, for the owner: the look must not change.

**Built (2026-10-15).** As designed: `BrickOptions::height`, `WaterCoreGpu::s_brickHeight`, `pc.bly`, bricks laid y-slowest,
mode-0 sums per 8 x 2 x 8 unit gathered into the world-aligned unit map, `markNeed` walking each axis to the reach (its `rings`
argument is gone), `water_av_create brick_height` / `gpu_brick_height`.
- **Red first:** `FlatBricksEqualCubic` with the single ring count: 8 x 2 x 8 differed in every scene (the dam break 153 of 180
  ticks, halo short 42 bricks, 114 faces of flux into absent bricks; the stepped bed 240 of 240). 8 x 4 x 8 already passed - one
  4-cell ring covers these slow scenes' reach.
- **Green:** per-axis rings - all six scenes at 2 and 4 tall: 0 differing ticks (every f, u, v, w, p ==), the surface field byte
  for byte, the rest decisions equal, halo short 0, the absent brick clean. The 8^3 run is still bit-identical under the new
  layout and reduction order. `AnyOriginKeepsTheWater` now runs heights 8, 4 and 2 at five origins: every run's fills equal the
  first (aligned 8^3) run's, cell for cell. GPU water 37 pass (the known FLIP row fails, run-up 1.19 h0 as before); CPU 330 / 330 -
  on the 8 default and again on the 2 default.

**Measured live - the bench, q 0.2, 40 sweeps, the same carve, 7 min each, the last minute's median**
(`creekbench_pond_g9_q0.2_h{8,4,2}.json`, `_steady.mp4`):

| brick | stored cells | GPU per call | mid depth | mid surface speed | out / in | frame |
|---|---|---|---|---|---|---|
| 8 x 8 x 8 | 691,200 | 1.83 ms | 0.2189 m | 0.624 m/s | 1.0005 | 128 fps |
| 8 x 4 x 8 | 517,120 | 1.62 ms | 0.2189 m | 0.624 m/s | 1.0034 | 129 fps |
| **8 x 2 x 8** | **457,984 (-34 %)** | **1.57 ms (-0.26)** | 0.2189 m | 0.624 m/s | 1.0026 | 129 fps |

Predicted -35 % cells and -0.35 ms; measured -34 % and -0.26 ms (live, a call costs ~1.1 ms per million stored cells, against
1.44 headless). The water is the same at every height (depth and speed to the fourth digit; ledger 0). **The frame did not move**:
since 26.16 the water's GPU work no longer holds the frame up, so this buys budget (room for more water and the ripples inside
the 2 ms), not frames. **The default is now 2** (`s_brickHeight`); tests that mean 8^3 name it.

**What is left of the cost (the creek, 1.57 ms):** the bed under the water - with 2-tall bricks, bricks that hold only ground
are still stored for the halo; not storing them is predicted at 457,984 -> ~262,000 cells (26.18, next to design: an
all-Solid brick must read as Solid where it is absent, not as air). **Owner's constraint (2026-10-16):** everything in the world
is destructible - a buried voxel can be dug, mined or blasted open at any time. So "all bed" can never be a fixed property of a
place: it must be read from the world's CURRENT occupancy every time that changes, and a brick that a dig opens must be stored
again (and water must be able to run into the hole) before the next step reads it. 26.18's gate must dig under and beside a
running creek; the fixed ~0.4 ms of a call (80 sweep dispatches with a
barrier each - fusing them changes the solve and needs its own gate).

### 26.18 The ground under the water, not stepped - design, check, build (2026-10-16). Status: BUILT, ON by default; not signed off

**The ask (owner, 2026-10-16: "keep going", after agreeing the constraint below).** The next cost lever from 26.17: with 2-tall
bricks the creek still steps bricks that hold nothing but ground, kept only because they lie within the halo's reach of the
water (6 cells round it, which reaches down into the bed). Predicted (`tools/water_brick_predict.py`): 457,984 -> ~262,000
stored cells on the bench.

**The owner's constraint (2026-10-16), the first rule of this design:** everything in the world is destructible. Ground that is
buried and unseen today can be dug, mined or blasted open at any time, and water must then behave exactly as if that ground
had been open from the start. So "this brick is all ground" is never a property of a PLACE - it is read from the world's current
occupancy, every time the occupancy changes, and nothing the water does may depend on a stale answer.

**What exists (read, not assumed).** The GPU volume's ground is the grid's occupancy, sampled from the world by
`WaterCoreManager::refreshOccupancy`. That runs whenever the world's occupancy pool changes (its pack revision - ANY voxel
edit anywhere, not only in the box) or an edit callback fires; it re-samples the whole box, marks the GPU volume dirty, and the
next `stepGpu` downloads the water, re-uploads the grid (occupancy and water) and rebuilds the brick map from scratch. So a
dig is seen by the water on its next step today. **Its cost has never been measured** (a full box re-sample, a fenced
read-back and a full upload - on the creek 1.75 M cells) - and in a game where the player digs, it runs on every dig. Measured as
part of this work (below), whatever 26.18 does.

**The design.**
1. **A second sentinel: the GROUND brick (slot 1).** Slot 0 stays the absent brick (reads as dry air). Slot 1 is new: its
   occupancy is Solid in every cell, every other field zero; no thread owns it (its slot-table entry is -1, so `threadSite`
   skips it as it skips a free slot) and nothing writes it - `wc_bricks.comp` checks it as it checks slot 0 (any non-zero f, u,
   v, w or p = dirty). A map entry of 1 means "this brick is ground": every read of it returns what a stored all-Solid brick
   would hold after the solid faces are enforced.
2. **Which bricks are ground - a pure function of the current occupancy.** At every upload (that is, after every occupancy
   change) the host computes, per brick of the box, `allSolid` = every cell Solid (cells outside the box count as Solid, as
   `scatterSlots` already writes them). Nothing else marks it; nothing caches it across uploads.
3. **Residency.** A brick that holds water, or that a pump or a deposit needs, is always stored for real. A brick wanted only
   for the halo is GROUND (map 1) when `allSolid`, stored for real otherwise. Transitions handled both ways (real -> ground,
   ground -> real, ground -> absent). The column table lists brick columns with a REAL brick only.
4. **A dig.** The occupancy changes -> the volume is re-uploaded -> `allSolid` is recomputed -> the dug brick is no longer
   ground; if the water's halo reaches it, it is stored for real before the next step, with its new air, so water runs into the
   hole on that step. Digging never creates water (the standing rule): an opened brick is stored dry.
5. **Bit-identity.** The claim to test: every kernel reads a ground brick exactly as it reads a stored all-Solid one. Faces
   stored at a solid cell (its lower faces) are forced to zero by `enforceSolids`; if any kernel reads one between a write and
   that enforcement, the equality gate shows it and the design changes - it is not assumed.
6. **Switch:** `BrickOptions::groundBricks` (default `WaterCoreGpu::s_groundBricks`, ON once measured), so the gate compares
   with and without. The record echoes `gpu_ground_bricks` (map entries that are ground this call).

**Design check (Feature Design Keys).** *Voxel look / destructibility:* the ground's state is the world's, re-read on every
change; the water reads ground exactly as before (gated). *Chunks:* `allSolid` and residency are world-aligned and decide cost
only; the equality scenes straddle the chunk seam at world x 288 and negative z. *Generation pipeline:* none. *API:* one echo
field, one switch. *Default change:* after the gate and the bench, in the same commit as the numbers.

**Gates (red first):**
- `GroundBricksEqualStored` - dense 8^3 (every brick stored) against sparse 2-tall with ground bricks: the six 26.17 scenes plus
  (a) a pond under an OVERHANG (a solid ceiling over water: ground above water, its lower faces against water), (b) a creek over
  a stepped bed DUG mid-run: at tick 60 a pit is cut in the bed under the flowing water (the same occupancy change and re-upload
  for both volumes) - every f, u, v, w, p equal every tick before and after the dig, the water DRAINS into the pit (water in the
  pit cells > 0 after), mass equal; (c) the reverse - a pit FILLED with ground mid-run beside dry ground. RED first: the ground
  slot left reading as air (its occupancy not set) must differ and leak.
- `FlatBricksEqualCubic`, `SparseEqualsDenseAcrossSeams`, the parity suite, CPU 330: unchanged.
- **Live:** the bench at q 0.2 with ground bricks on vs off - stored cells, GPU per call, the water's depth/speed/ledger (same
  as 26.17's table); then **a dig under the running creek**: a microcube-wide pit cut in the bed every 0.5 s for 30 s while the
  frame time and the re-upload's time (new record field `gpu_upload_ms`) are logged - the water must drain into each pit, the
  ledger hold, and the frame's hitch per dig is reported as measured. If the re-upload is what costs, the follow-up is to
  refresh only the bricks a dig touches (designed then, not now).

**Built (2026-10-16).** As designed: slot 1 is the GROUND sentinel (occupancy Solid, filled at create and in every upload's
staging, never written, checked by `wc_bricks.comp` like slot 0); `Volume::allSolid` recomputed from the upload's occupancy;
`applyResidency` stores need-2 bricks for real, makes need-1 all-ground bricks ground, handles every transition; the column table
lists real bricks only; real slots start at 2. `BrickOptions::ground` / `s_groundBricks` (now ON), `water_av_create ground_bricks`,
record `gpu_ground_bricks`, plus `gpu_upload_ms`, `gpu_uploads`, `occ_refresh_ms` (below).
- `GroundBricksEqualStored`: the six 26.17 scenes + an overhang, a dig under the flowing creek at tick 60 and a pit filled in -
  0 differing ticks (every f, u, v, w, p ==, the surface byte for byte, the rest decisions), halo short 0, both sentinels clean;
  210 cells of water ran into the dug pit (both runs). The same new scenes with ground off: equal too.
- `AGroundReadAsAirIsCaught` (the control, red by design): the sentinel's occupancy left as air - 120 of 120 ticks differ, 1,260
  faces of flux into absent cells.
- GPU water 39 pass (the known FLIP row fails); CPU 330 / 330 - with ground off and again with it on by default.

**Measured live - the bench, q 0.2, 2-tall bricks, 7 min each** (`creekbench_pond_g9_q0.2_gb{0,1}.json`, `_steady.mp4`):

| ground bricks | stored cells | ground (not stepped) | GPU per call | mid depth | mid surface speed | out / in |
|---|---|---|---|---|---|---|
| off | 456,192 | 0 | 1.56 ms | 0.2189 m | 0.624 m/s | 0.995 |
| **on** | **282,240 (-38 %)** | 1,364 bricks | **1.35 ms** | 0.2189 m | 0.624 m/s | 1.002 |

Predicted ~262 k cells; measured 282 k. With 26.17: the creek's GPU per call 1.83 -> 1.35 ms (-26 %), the water unchanged.

**Digging under the running creek - measured** (`tools/water_creek_dig.py`; `creekbench_dig_gb1.json`,
`creekbench_dig_bank_trench{,_top}.json`, `creekbench_dig_bank_trench_top.mp4` - the clip to watch):
- **Every dig reached the water**: 20 pits in the bed under the flow, then two 12-voxel trenches in the banks beside it - one
  re-read per dig, every time (`gpu_uploads`). The water ran into every hole: all pit and trench columns wet; the overhead clip
  shows the bank turn to water as the trench opens. The books held through all of it (worst 2.2e-5 m^3 during a dig, 1e-6 at the
  end). No water appeared that the springs did not put in (stored rise = net inflow, to 1e-6).
- **But each dig froze the frame for ~145 ms**: `occ_refresh_ms` 67 (the WHOLE box, 1.75 M cells, re-sampled from the world)
  + `gpu_upload_ms` 77-89 (the water read back from the GPU and the whole volume re-uploaded). This path is not new - it is how
  the water has always seen a world change; 26.18 only made it measurable (the `allSolid` pass inside the upload is a small part
  of it). And it is triggered by the occupancy pool's pack revision, which is GLOBAL: an edit anywhere in the world - not only
  in the box - re-reads every volume. Whether chunk streaming also bumps it is not yet checked.

**Next - 26.19, a world change seen locally.** In a game where the player digs, a 145 ms freeze per dig near water is a defect,
and one per dig anywhere is worse. The design to write: re-read only what an edit touched (the edited voxels' cells, if they lie
in a volume's box), update those bricks' occupancy, ground status and residency on the device in place, and never download the
water for it - with the same equality gate (a local re-read == the full re-read, bit for bit) and the dig bench as the measure.

### 26.19 A dig without a freeze - measured, designed, checked, built (2026-10-16/17). Status: DONE, ON; signed off by the owner 2026-10-17 ("wow that looks way better" / "looks good")

**The ask (owner, 2026-10-16, on `creekbench_dig_bank_trench_top.mp4`): "that video certainly seems very very stuttery. we
need to fix that".**

**Measured first** (the clip, then the frame-pacing history around one dig):
- The clip: 38 freezes over 80 s, the long ones 0.7-1.1 s, one at every dig (frames unchanged, `mpdecimate`). The control,
  `creekbench_pond_g9_q0.2_gb1_steady.mp4` (the same creek, no digs): 0 freezes over 60 s. The stutter is the digs.
- One one-voxel dig on the main loop (`/api/debug/frame_pacing`): a frame of **1,116 ms in "API Drain"** (the dig command), then
  a frame of **148 ms in "Water"** (the re-read 26.18 measured). The clear command's own stage times (new `timing_ms` echo on
  `clear_region`): undo snapshot 0.05 ms, the voxel removal 0.09 ms, **the NPC navigation rebuild 1,051 ms** - the WHOLE world's
  nav grid rebuilt after every API fill or clear, in a world with no NPCs.

So the freeze has two causes, and only the second is the water's:
1. **The dig tool's nav rebuild (1,051 ms).** `fill_region` / `clear_region` (the routes the bench digs with) rebuilt the whole
   nav grid; a player's own dig (B key) and every other edit route already update only the touched columns
   (`NPCManager::onVoxelChanged` / `onRegionChanged`). **Fixed:** the two routes now call `onRegionChanged` for their box.
2. **The water's re-read (~148 ms per volume per world change),** and three things make it worse than "a dig near water":
   - it is triggered by the occupancy pool's pack revision, which bumps on ANY change - an edit anywhere, a chunk streaming in
     or out, and the camera crossing a chunk boundary (the pool's box re-centres: `setViewCentre`) - so walking around costs
     the water a freeze per chunk crossed;
   - every voxel edit also calls `markSolidsDirty()` (the occupancy callback, `Application.cpp`), a second full re-read, at a
     moment the pool may not yet hold the edit;
   - the full re-read is not only slow, it disturbs the water: the re-upload zeroes the pressure solve's warm start, the
     sub-cell remainders and the source rates, and rebuilds residency from the water plus one ring.

**The design - the world tells the water WHAT changed, and the water patches only that, in place.**
1. **The pool records what changed, per cube.** `VoxelLightOccupancyGpu::setChunk` diffs the new blob against the one it
   replaces, cube by cube (the solid bit, the mixed bit, and for cubes mixed in both, their 729 micro bits) - a pure function,
   `diffChunkOccupancy`, unit-tested - and records the box of the changed cubes (none: nothing recorded; a chunk re-set with
   the same contents - a remesh, a neighbour's edit - costs the water nothing). A chunk that arrives, leaves, or is forgotten by a
   box move records its whole 32^3 box (its cells go known <-> Unknown). Every pack keeps its list in a short history (64
   packs); `changesSince(rev, boxes)` returns the boxes packed after `rev`, or **false** - "re-read everything" - when the
   history no longer reaches back that far, when a pack dropped chunks (the pool was full: states changed with no chunk set), or
   when a pack recorded more boxes than the cap.
2. **The water re-reads only those cells.** `refreshOccupancy`, on a new revision, asks for the boxes since its last one. Each
   box that meets the volume's box becomes a cell range (padded one cell on every side: rounding can only add cells); those cells
   are re-sampled exactly as the full re-read samples them, compared with the cache, and only the ones that differ go on. No
   change: nothing more happens - no GPU work. `false`: today's full re-read (kept, and still what a new volume does).
3. **The GPU patches in place: no read-back, no re-upload.** `WaterCoreGpu::updateOccupancy(vol, cells, grid)` finishes the call
   in flight, writes the new occupancy into the host copy, recomputes `allSolid` for the bricks those cells lie in and the solid
   top of their columns, and copies the occupancy of the stored bricks among them to the device. Nothing else is touched - the
   water, the faces, the pressure, the remainders keep their values, as they would if the ground had changed under water that
   was not being re-read at all. A brick that was GROUND and is now not all ground stops being ground at the next call's
   residency (26.18 rule 4, unchanged: it is stored with its new air, dry); one that became all ground can become ground. The
   volume wakes.
4. **One signal.** The occupancy callback no longer calls `markSolidsDirty()` on the water volumes: the pool's change list is
   the one signal, and it arrives when the pool - which is what the water samples - actually holds the edit. The debug
   `water_av_*` `refresh` flag and volume creation keep the full re-read.
5. **What does not change.** Water in a cell that becomes solid stays where it is (as today's re-upload leaves it); digging
   never creates water (an opened cell is dry air until water flows in). The record gains `occ_local_refreshes`,
   `occ_full_refreshes`, `occ_cells_resampled` and `occ_cells_changed` next to `occ_refresh_ms` / `gpu_upload_ms`.

**Design check (Feature Design Keys).**
- *Destructibility (the owner's rule, 26.18):* which cells are ground is still read from the current world on every change; the
  change list only says WHERE to read. Anything the list cannot vouch for (history gone, pool full, cap passed) falls back to the
  full re-read - a slow answer, never a stale one.
- *Chunks:* the change list is keyed by world cubes; a chunk boundary bounds what a pack records (cost), never what the water
  reads. A dig across a chunk seam records one box per chunk; the gate digs across one.
- *Generation pipeline:* none. *API:* `timing_ms` on `clear_region`; four record fields. *Defaults:* the local path is the
  default once its gates pass - no switch is kept that leaves the slow path live, but the test hook `forceFullRefresh` keeps the
  comparison runnable.
- *Visual test:* the same dig clip, re-recorded at the same pose, with the freezes counted by the same `mpdecimate` measure.

**Gates (red first):**
- `ChunkOccupancyDiff` (unit): one cube cleared / one micro changed / a mixed cube made solid / identical blobs / a chunk with
  nothing before - the reported box is exactly the changed cubes (empty for identical).
- `LocalRefreshEqualsFull` (manager, CPU): a fake world edited at single cubes, a micro, a box across a chunk seam, a chunk
  arriving and leaving - after each, the local re-read's occupancy == a full re-read's, every cell. RED control: one change
  withheld from the list - they must differ.
- `OccupancyPatchedInPlace` (GPU): dense 8^3 (every brick stored) against sparse 2-tall + ground, both edited mid-run by
  `updateOccupancy` - a dig under the flowing creek, a deep dig opening a ground brick, a pit filled, ground placed into water -
  every f, u, v, w, p equal every tick; after each edit the device occupancy of every stored cell == the world, and every
  ground brick is all ground in the current world; the water drains into the dug pit. RED control: the sparse volume's
  `allSolid` left stale - the dug brick stays ground, the water must not reach the pit, and the equality fails.
- All existing GPU (39 + the known FLIP row) and CPU (330) water tests unchanged.
- **Live:** `tools/water_creek_dig.py` again, same digs, same pose: per dig `occ_refresh_ms`, `gpu_upload_ms`, the clear's
  `timing_ms`, the worst frame around it (frame pacing); the ledger; the water in every pit; then the clip's freezes counted.
  Prediction: the dig frame's water cost falls from ~148 ms to under 2 ms (729 cells re-sampled, a few kB copied, one settle),
  the API dig from ~1,051 ms to under 1 ms, and the clip shows no freeze at the digs. Plus a walk: the camera flown across 10
  chunk boundaries beside the running creek, `occ_full_refreshes` must not move.

**Built (2026-10-17).** As designed: `diffChunkOccupancy` + `OccupancyChangeLog` (pure, in `VoxelLightOccupancy`), owned by the
pool (`VoxelLightOccupancyGpu::changesSince`); `WaterCoreManager::setSolidsChangesQuery` + `refreshOccupancyLocal` (exact
integer cell ranges from the volume's cells-per-voxel, padded one cell); `WaterCoreGpu::updateOccupancy`; the occupancy callback
no longer marks the water dirty. Record: `occ_full_refreshes`, `occ_local_refreshes`, `occ_cells_resampled`, `occ_cells_changed`,
`gpu_patch_ms`, `gpu_patches`.
- `ChunkOccupancyDiffReportsExactlyTheChangedCubes`, `ChangeLogAnswersOnlyWhatItCanVouchFor`,
  `APoolThatDropsChunksSaysEverythingChanged`: pass (the history test's first draft expected the wrong answer for "since 5"
  with packs 6.. held - the test was corrected, not the log).
- `LocalRefreshEqualsFull`: 0 differing cells in all 8 scenes at 1/9 m and 1/3 m (one cube dug / added, one micro, an empty
  pack, a dig outside the box, a chunk arriving, a trench across the chunk seam, a chunk leaving). A one-cube dig re-samples
  1,331 of 336,798 cells at 1/9 m. Control: an unreported dig - 729 cells differ (1/3 m: 27). First run failed its own cost
  bound at 1/3 m (216 > 125 cells: float rounding added a cell) - fixed with integer ranges.
- `OccupancyPatchedInPlace`: 0 differing ticks in 4 scenes (dig under the creek, a deep shaft opening ground bricks, ground
  placed into the water, a pit filled); device occupancy == world in every stored cell and every ground brick all ground; 209
  and 100 cells of water ran into the dug holes. Control `APatchLeavingGroundStaleIsCaught`: 140 ticks differ, 96 stale cells.
- CPU 366 / 366 (water + occupancy), GPU water 41 pass (the known FLIP row fails).

**Measured live - the same trench, the same pose** (`creekbench_dig_bank_trench_top_2619.{json,mp4}`, 12 one-voxel digs 1.5 s
apart beside the running creek at q 0.2):

| | before 26.19 (+ the nav rebuild) | after |
|---|---|---|
| the dig command (`clear_region`) | ~1,051 ms (nav rebuild 1,051) | ~5 ms (nav 0.8) |
| the water's re-read | 67 ms (1.75 M cells) + 77-89 ms read-back + upload | 0.07 ms (1,331 cells, 729 changed) + 1.5-5 ms patch |
| worst frame around a dig | ~1,265 ms | 22-23 ms (14-15 ms of it the API drain) |
| freezes in the 80 s clip (`mpdecimate`, gaps > 90 ms) | 38 | **0** (30.0 changed frames/s) |
| whole-box re-reads during the digs | 12 | 0 |

The water ran into all 12 trench columns; the books: stored rise 3.053611 = net inflow 3.053614 m^3, ledger -6e-6 m^3. (The
creek had run ~6 min before this dig, the earlier clip's longer: 36.7 vs 46.8 m^3 stored at the start.)
**The walk:** the camera flown across 11 chunk boundaries beside the creek - 11 pool revisions, 0 whole-box re-reads, 0 cells
re-sampled, worst frame 6.5 ms. (Not measured before the change: that each crossing cost a full re-read follows from the code.)
The 60 s clip to watch: `creekbench_dig_bank_trench_top_2619.mp4` beside the old `creekbench_dig_bank_trench_top.mp4`.

### 26.20 Why the pond model runs deeper on the stepped bed - measured (2026-10-17). Status: ANSWERED - it does not; its flowing water holds air

**The question (26.15, open):** on the creek bench's 1/9 m carve the pond model runs 6 % (q 0.2 m^3/s) / 10 % (q 0.5) deeper
than the column model on the same ground. Physics the column model cannot hold (the 3-D flow over each drop), or a defect?

**A 1-D reference first** (`tools/water_stepped_reference.py`; the standard gradually-varied-flow march, Manning n 0.035, upstream
from the channel's free overfall; at each drop either energy conserved or momentum with a hydrostatic riser). On the bench's bed
(flat 22.2 m reaches, 1/9 m drops, 1:200 overall) **every drop is a free overfall** - the tailwater is too low to drown it, the
flow passes critical depth at each brink - so each reach is the backwater from its brink and the rule at the drop does not
matter (the two agree to 0.3 %):

| unit q (m^2/s) | Manning (smooth 1:200) | 1-D reference, stepped | column model, bench (26.15) | pond model, bench (26.15) |
|---|---|---|---|---|
| 0.1 (bench q 0.2) | 0.165 m | **0.190 m** (1.155x) | 0.189 | 0.199 (+5 % over the reference) |
| 0.25 (bench q 0.5) | 0.285 m | **0.306 m** (1.07x) | 0.305 | 0.336 (+10 %) |

The column model IS the textbook answer on this bed; the pond model is the one off it. So the question becomes which part of
the pond model adds the depth. Critical depth at q 0.1 is 0.10 m - under one 1/9 m cell - so the brink is barely resolved.

**The rig** (`tests/core/WaterSteppedBedTest.cpp`, diagnostics): the bench's bed in a straight 1/3 m wide channel, five reaches
plus a pit, both models ALONE on the same ground, frictionless side walls, measured over reaches 1-3 at steady state; every
setting from the environment (`SB_*`) so one variable moves per run.

**Predictions (written before the first run):**
1. Smooth control (no drops, the slope a tilted frame): both models at Manning (P1 measured the pond at 100.0 %).
2. The bench's bed: the rig reproduces 26.15 - column ~0.189 / pond ~0.199 at q 0.1.
3. Candidate A - `applyBedManning` brakes each face by its OWN speed (`v / (1 + dt g n^2 |v| / d^(4/3))`). Manning's law is
   written for the depth-mean speed; over a drop the layers differ (a fast jet over slow or reversed water near the bed), and
   the per-face form then adds drag (mean of v|v| over the column >= U|U|). If A is the cause, the depth-mean form
   (`WaterSolver::s_bedManningColumnSpeedForTest`, one factor per face column) brings the pond to the reference within ~2 % on
   the steps and changes nothing on the smooth control (a uniform profile).
4. Candidate B - the brink under-resolved at 1/9 m: if B, the pond at 1/18 m cells on the same bed moves toward the
   reference, and the gap is larger at q 0.1 (dc < 1 cell) than its share at q 0.25 would suggest.

**Results on the rig** (depth = the water IN each column, as the P1 rig measures; raw runs `steppedbed_2620.txt`):

| unit q | 1-D reference | column 1/3 m | column 1/9 m | pond (today) | pond, depth-mean Manning (A) | smooth control: pond / column |
|---|---|---|---|---|---|---|
| 0.1 | 0.190 | 0.188 | 0.189 | **0.188** | 0.188 | 1.000x / 0.999x Manning |
| 0.25 | 0.306 | 0.306 | 0.306 | **0.310** (+1.3 %) | 0.309 | 0.998x Manning |

- Prediction 1 held (both at Manning on the smooth bed). **Prediction 2 FAILED:** on the rig the pond model is NOT deeper - it
  carries the stepped creek at the reference depth. Candidate A changes nothing (0.1880 vs 0.1882) - rejected. Candidate B's
  1/18 m runs were stopped unfinished once the live gauge below answered the question (the depth is right at 1/9 m).
- So the bench's 6-10 % had to come from something the bench has and the rig does not: the GPU, the 2 m channel, or the GAUGE.

**The gauge.** `tools/water_creek_profile.py` (26.15) read `water_av_column`'s `surface_y` - the HIGHEST wet sub-column of the
voxel column's 81 (what debris reads, `WaterCoreManager::columnWater`) - minus the carve. `water_av_column` now also returns
`surface_mean`, `surface_lowest`, `wet_sub` / `sub`, and with `{mass: true}` `mass_depth` (the water in the column over its 1 m^2,
`columnMassDepth`, a GPU read-back); the profile tool takes `--stat highest|mean|mass`. Live, the same running creek, steady:

| bench q (m^3/s) | highest sub-column (26.15's gauge) | mean drawn surface | **water in the column** | 1-D reference | column model (26.15) |
|---|---|---|---|---|---|
| 0.2 | 0.1985 | 0.1958 | **0.1873** | 0.190 | 0.189 |
| 0.5 | 0.3306 | 0.3274 | **0.3058** | 0.306 | 0.305 |

**Answer: the pond model's WATER is at the textbook depth on the stepped bed (-1.4 % / 0.0 %), as the column model's is.**
26.15's "6 % / 10 % deeper" was the gauge: the highest sub-column adds 1-1.5 %, the rest is the drawn surface standing above the
water it holds.

**Why the surface stands high - air inside flowing water (a defect, both backends).** Probed live (x 40 / 60 / 70, one
sub-column, bottom up): fills 0.93 0.90 0.02 / 0.92 0.88 0.09 / 0.89 0.67 0.00 - cells with water above them 7-11 % empty. The
rig shows the same on the CPU reference, and on the SMOOTH channel too:

| rig (CPU) | cells with water above them, mean fill | the water's top above its amount |
|---|---|---|
| smooth, q 0.1 / 0.25 | 0.92 / 0.96 | 9 / 9 mm |
| steps, q 0.1 / 0.25 | 0.90 / 0.91 | 16 / 26 mm |

Moving water in the pond model is bulked with 4-10 % air below its surface: the amount and the flow are right (Manning, the books
exact), but what is DRAWN stands 9-26 mm (5-7 %) high, more over drops and at higher flow. This is §24.11's open row ("voids made
by moving water are closed by compaction, not prevented - the cure is a volume correction in the pressure solve"), measured at
last on flowing water. P1's "100.0 % of Manning" (26.5) measured the water, not the surface; it stands.

**Consequences.** P3's depth gate (26.13) is met when depth means the water: both flows within 1.5 % of the reference and the
column model - the bed comparison is CLOSED. Open, to design if the owner wants it before P3c: the air in moving water (a volume
correction in the projection, CPU + GPU) - it would bring the drawn creek down onto its water. P3c's gauges read `--stat mass`.

### 26.21 The air in moving water - design, check, build (2026-10-17). Status: DONE, ON by default (CPU + GPU); signed off by the owner 2026-10-17 ("looks good")

**Where the air comes from (read off the code, 2026-10-17).** The projection makes every LIQUID cell (f >= 0.5)
divergence-free, as if it were full. The fill moves by donor cell: a face carries `|v| dt / h x f_donor`. So a liquid cell
that takes water from a part-full donor (the surface cell above it on a falling surface, over every drop) and passes it on
at its own fill loses the difference every substep, and once it is under 1 nothing in moving water gives it back:
- the projection asks it for no net inflow (divergence-free = "already full");
- compaction (24.11) refills it only through a QUIET face in a QUIET cell (lateral speed < 0.1 x the one-cell free fall,
  0.15 m/s at 1/9 m) - a creek runs 0.6-1 m/s, so compaction never runs in it;
- the ceiling target, the wake rule (20.5) and the FLIP density pull (15.9) are for other cases (a solid above, a body's
  wake, particle transport).
On the smooth rig the air is frozen in from the spin-up (uniform flow neither makes it nor clears it: 0.92 / 0.96 full);
over the drops it is made all the time (0.90 / 0.91).

**The rule (the volume correction §24.11 named).** In the Eulerian projection, a BURIED part-full liquid cell asks for a
net inflow that returns its missing volume over a relaxation time T:

    buried:  fc = f + s < 1,  the cell above is liquid (fa + sa >= thr and fa >= the surface minimum depth),
             and no LATERAL face of the cell is to air (each of its 4 side neighbours is liquid or solid/body wall)
    rhs  -=  df / dt^2,   df = (1 - fc) x dt / T            (the missing fraction, a share of it per substep)

`SolverParams::volumeCorrectionTime` T (s; 0 = off). Water then moves DOWN into the cell from the liquid around it, and the
pressure field takes the volume out at the free surface: the surface falls by the air it held, mass exact (the transport
moves only what donors have - the rule asks for velocity, never writes f).

**Why each gate (each is a case that broke an earlier pull, read off 15.9 / 20.5 / 24.11 / #17 / #20):**
- *a liquid cell above:* the cell under a thin film (f_above < 0.5) is the SURFACE cell - its part-fullness is where the
  surface is, not air (the 24.11 distinction; the wave drain of 24.6).
- *no lateral air face:* a part-full cell beside air is a surface crossing the cell at a slope - a sheet over a sill (#17,
  the weir lost 15 % to compaction there), a climbing sheet (#20, the run-up), a falling jet thinner than a cell. Its
  part-fullness is shape, not air. A buried cell in a creek or a pond has water or rock on all four sides.
- *a relaxation, not a speed:* the 20.5 attempts pulled every buried void at a fixed speed (0.25 m/s) and the blasted pond
  rang (KE plateau 0.4-0.8) and the pump lost a tenth; a share of the deficit per substep is the standard volume-control
  form (Kim et al. 2007) and asks for (1 - f) h / T: 22 mm/s for a 10 % void at 1/9 m and T = 0.5 s.
- *no quiet gate:* the point is moving water.

**Predictions (written before the first run; rig `WaterSteppedBedTest`, CPU, SB_T 400):**
1. Smooth, q 0.1 / 0.25: buried cells 0.92 / 0.96 -> >= 0.99; the water's top above its amount 9 / 9 mm -> <= 2 mm;
   depth by mass unchanged within 1 % (1.000 / 0.998 x Manning).
2. Steps, q 0.1 / 0.25: 0.90 / 0.91 -> >= 0.98; 16 / 26 mm -> <= 5 mm; mass depth 0.188 / 0.310 within 2 %.
3. T: 0.5 s first. If the steps keep more than 5 mm at 0.5 s, 0.2 s, stated.
4. The CPU water suite (342) stays green unchanged - the weir (#17), the submerged pump, the run-up, the wave-energy rows,
   `TheLevelNeverJerks`, the droplets, mass exactness. Any test that moves gets its numbers stated here, not its bound edited.
5. Red test first: `WaterSteppedBedTest.MovingWaterHoldsNoAir` - the smooth rig at q 0.1, buried cells >= 0.99 and the top
   within 2 mm of the water; red today at 0.92 / 9 mm.
6. GPU: the same three gates in `wc_classify.comp` mode 0 (Eulerian branch), T in `param1`; the bench creek's mean drawn
   surface (`--stat mean`) comes down to within 3 mm of its mass depth (today +8.5 / +21.6 mm at q 0.2 / 0.5); the GPU water
   suite green; the frame cost unchanged within noise (a few flops per liquid cell in a kernel that already reads these).

**Design check (FeatureDesignKeys):** *voxel look* - water is the standing exception (14.1), unchanged; the surface now sits
where the water is. *Chunks* - a per-cell rule on the cell and its six neighbours, no chunk-derived quantity; the existing
seam / residency equality tests cover it (14.2). *Generation* - none (runtime). *API* - no new route; T is a solver param
(default 0.5 s once measured; 0 turns it off, for bisecting), `water_av_column {mass:true}` and `--stat mean|mass` already
read the result. *Visual test* - L2 on the rig (the air numbers above), L4 on the creek bench (mean drawn surface vs mass,
a side-by-side clip for the owner: before / after at the same pose). Rig vs shipped: the rig is the CPU reference on a
straight 1/3 m channel with frictionless walls (stated in 26.20); the bench is the shipped GPU path. **READY.**

**Built (CPU `WaterSolver::project`, GPU `wc_classify.comp` mode 0; `SolverParams::volumeCorrectionTime` 0.5 s,
`volumeCorrectionBand` 0.02; live `water_volume_correction {time_s, band}`, echoes the setting, time 0 = off).** Three changes
to the rule as designed, each forced by a measurement:
1. **Under a thin surface cell too** (`WaterSolver::s_volumeCorrectionUnderFilm`, the default). With only a liquid cell above,
   the short rig went 0.79 -> 0.93 full / 29 -> 8 mm, and every cell left part-full sat under a THIN cell: 0.93 under 0.41
   (should be 1.0 under 0.33 - the top cell held the water the one below lacked). Gated only on water above: 1.000 / 0.0 mm.
2. **The pull capped at the water above** (`gap = min(1 - fc, fa)`). Uncapped, a surface cell under the few-mm film the
   transport smears upward counted as buried and was asked for its whole missing share: the tiny-wave rows woke 10-13x
   (5 mm wave at top 0.50: peak KE 2.6e-5 -> 2.2e-4, over the settle band). Capped: every tiny-wave row back to its value.
3. **A dead band** (deficits under 2 % left alone). With none, the pump's books drifted 3e-5 m^3 (bound 1e-5; off: 2.3e-6) and
   the pocket/slosh run 9.9e-6 (bound 2e-6) - not a leak: `DiagPumpMassByStage` shows every stage but the transport moves the
   total by exactly 0, and the transport's own float rounding (once per cell per substep) grows with the near-full cells the
   pull keeps changing. And the short wave on a half-full cell (mode 4, top 0.50; off 0.66 per swing, bound 0.60) read
   0.48-0.58 at bands 0-1 %, 0.62 at 2 %, 0.68 at 3 % - noisy, not monotonic in T (0.3 / 0.5 / 1 / 2 s).

**Measured (rig, CPU, SB_T 400; the 26.20 runs as the "before"):**

| case | cells with water above them | the water's top above its amount | the water's depth (mass) |
|---|---|---|---|
| smooth q 0.1 | 0.92 -> **1.000** | 9 -> **0.0 mm** | 0.1647 -> 0.1655 m (1.005 x Manning) |
| smooth q 0.25 | 0.96 -> **0.988** | 9 -> **2.7 mm** | 0.2849 -> 0.2838 (0.994 x) |
| steps q 0.1 | 0.90 -> **0.977** | 16 -> **2.9 mm** | 0.1882 -> 0.1867 (reference 0.190) |
| steps q 0.25 | 0.91 -> **0.990** | 26 -> **2.5 mm** | 0.3104 -> 0.3079 (reference 0.306) |

Against the predictions: (1) smooth q 0.25 2.7 mm, over the 2 mm predicted; (2) steps q 0.1 0.977 full, under the 0.98
predicted; the rest held. Both misses are the 2 % band (2.2 mm at 1/9 m) - the price of item 3. The water's depth moved
-0.8 % to +0.5 %, inside the 1-2 % predicted.

**Red test** `WaterSteppedBedTest.MovingWaterHoldsNoAir` (10 m smooth channel, 60 s): RED 0.79 full / 29 mm (correction off),
GREEN 0.982 / 2.0 mm; the control (the same channel, off) must still show > 10 mm and the depth must match it within 2 %
(0.1465 vs 0.1476). Its bounds are 0.98 / 2.5 mm, not the 0.99 / 2 mm predicted - the band, stated in the test.

**Tests that moved (CPU water + occupancy suite, 343):** all pass. `WaterCouplingTest.SurfaceDoesNotOutrunTheWater` - its
CONTROL (the pre-19.5 surface rule must still fail the kinematic bound, proving the test sees flicker) fell 524 -> 222 because
the blasted pond now carries fewer pockets and films to trip it; the new rule still reads 0 violations; the control's
threshold 250 -> 100. Wave rows (top 0.50): mode 1 1.11 -> 1.07 kept per period (the spurious GAIN shrinks: the old pocket
collapse fed it), mode 4 0.66 -> 0.62 (bound 0.60). The droplet scrap's apex 2.607 -> 2.601 m (bound 2.6 - 1 mm of margin, a row to watch; it failed at 2.578
before the cap).

**Live, GPU (WaterBench_Creek, the shipped path; on / OFF / on again on the SAME running engine via
`water_volume_correction`, 150 s settle per phase; `creekbench_pond_g9_q{0.2,0.5}_vc_ab.json`):**

| bench q | phase | mean drawn surface - the water in the column | the water (mass depth) | water GPU ms (median) |
|---|---|---|---|---|
| 0.2 | on / off / on | **1.5** / 8.5 / **1.6 mm** | 0.1862 / 0.1873 / 0.1862 m | 1.66 / 1.68 / 1.42 |
| 0.5 | on / off / on | **2.4** / 21.4 / **2.4 mm** | 0.3053 / 0.3059 / 0.3054 m | 1.73 / 1.59 / 1.58 |

Prediction 6 held: the drawn creek sits within 3 mm of its water (it stood 8.5 / 21.4 mm high), the water itself moves
0.2-0.6 %, and the GPU cost is unchanged within its own phase-to-phase noise (1.42-1.73 ms with the rule on, 1.59-1.68 off; the
1.34 ms of 26.18's run is a different session, matched by "off" here). The FPS dip in the q 0.5 OFF phase (55 vs 96-100) was my
screenshot and window captures during that window, not the solver (its GPU time did not move).

**Capture trap found on the way:** `GET /api/screenshot` on this bench returns the frame WITHOUT the pond water (a dry gravel bed
where the window shows the creek); the window capture (`ffmpeg gdigrab`, what every clip uses) shows it. Judge pond water from
window captures only.

**Clip for the owner** (`creekbench_pond_g9_q0.5_airfix.mp4`, + `.json`): q 0.5, a fixed low pose over the centreline 2-3 m
before the x 55 drop, the correction OFF for 150 s (the air built back), switched ON 15 s into a 40 s recording, labelled in
frame. Drawn surface at three columns ahead: 15.711-15.714 m while off; within ~1 s of the switch 11-22 mm lower, then a further
~3-4 mm over 25 s (15.700 / 15.700 / 15.685). The one-second step is the demo's switch releasing 150 s of built-up air at once -
with the rule always on (the default) the air never builds, so there is no step in play (24.11's "level drops in one jerk" does
not apply). From this pose the change reads mostly as the bright reflection blocks moving; it is 1-2 cm on a 31 cm creek.
Rig runs: `steppedbed_2621.txt`; live A/B: `creekbench_pond_g9_q{0.2,0.5}_vc_ab.json`.

**CORRECTION (found 2026-10-17, after sign-off).** The report to the owner said "the full CPU and GPU water suite passes (343)".
The 343 were `phyxel_tests` only; the GPU water tests (`WaterCoreGpuParityTest`, `WaterBrickTest`, `WaterRippleGpuParityTest`)
are in `phyxel_integration_tests`, which was not run. Run since (from the repo root - it reads `shaders/` relative to the working
directory; from its own folder every GPU test fails to load a pipeline): **40 of 42 pass**, two fail:
- `WaterCoreGpuParityTest.GpuFlipBulkParityAndRunup` - the GPU particle run-up 1.19 h0 (bound > 1.22). **Not 26.21's:** the same
  1.19 with the correction off (rebuilt with time 0), and the correction never runs for particle transport. Pre-existing, open.
- `WaterBrickTest.FlatBricksEqualCubic` - the 8 x 4 x 8 (four-tall) dam break: "halo short 3" (water within reach of a brick that
  was not stored). **26.21's:** 0 with the correction off. Everything else in the scene is exact (sparse = dense, 0 differing
  ticks, no flux into absent bricks, mass 2.106996 both); the shipped 8 x 2 x 8 bricks (26.17) and the 8^3 pass every scene.
  Diagnosed with test-only hooks (`WaterCoreGpu::s_haloShort*ForTest`): all 3 come from FILM records (none from liquid, none
  from film turning liquid in place), each needing the brick diagonally (-x, +y) from a film brick whose film lies 1 cell from
  both those faces, travel 2. Either the halo check's per-face minima pair two different film cells into a corner that is not
  wet (a false alarm - the check is conservative by design), or the column carry (21.2, over-full water moved UP its column,
  not bounded by velocity) lifted a film past the travel allowance. **Open, not fixed; the test stays red** (its bound is a
  guard, not edited). Next step when it is taken up: per-cell positions of the film at the short calls.
  **Resolved by 26.28:** a false alarm - no water within reach by cell; the test now checks by cell.

### 26.22 P3c - the dam & pond bench as pond water: design, check, run (2026-10-17). Status: DONE; signed off by the owner 2026-10-17 ("video looks good")

**The question.** 25.38 ran the dam & pond bench on the COLUMN model: a creek fills a 6 x 6 m pond, the pond spills, a dam
across the creek at x 60 holds the flow back and overtops, the dam is broken and the held water surges down. P3c runs the same
bench, the same edits and the same gauges on the POND model (one water everywhere, 26), so P3d can retire the column regions.

**The volume - one, no engine change.** 26.13 wrote P3c's box as 118 x 5 x 10 m (4.3 M cells, over the 2 M cap of the CPU copy).
The water never leaves z 29-34 (the pond's width; the creek and the spill channel are 2 m inside it) nor rises above ~16.0, and
the pond's floor is y 14.0: **x 10-127, y 14-16, z 29-34 at 1/9 m = 1062 x 27 x 54 = 1,548,396 cells** - inside the cap
(y to 17 would be 2.06 M). GPU, sparse bricks (dry ground costs nothing, 26.18), Manning n 0.035 over the whole volume (it
replaces pond friction; in the pond's ~1 m of slow water it brakes far less than pond friction would - stated, measured below),
springs across the head's 1/9 m cells (as the creek bench), outfalls across the spill channel's end (x 127).

**The edits.** The bed at the dam (x 60) is 15.333 m - on the 1/3 m grid even after 26.15's 1/9 m carve - so 25.38's dam is
unchanged: one layer of Stone subcubes (y 15, sy 1) across z 31-32, crest 15.667; removed the same way. The pond volume
re-reads only the touched cubes (26.19's local re-read) and patches the GPU occupancy in place; water in a cell that turns
solid moves up its column (ledger rule 19.1), never deleted.

**Gauges (26.20: depth means the water, not the highest sub-column):** pond level = `water_av_column` `surface_mean` at the
pond's centre (still water: the drawn surface is the water's; 26.21); the dam's pool the same 5 m upstream of the dam; the
flow INTO the pond by the books - d(pond storage)/dt + d(outfall total)/dt, the pond storage the mass depth summed over its
36 columns (one `{mass: true}` call) - smoothed over 5 s; ledger = mass - (springs in - outfalls out) from `water_av_list`;
the water's GPU ms and the frame rate.

**Predictions (written before the first run; the column model's 25.38 numbers beside them):**

| | prediction (pond model) | column model, 25.38 |
|---|---|---|
| DP-T1 pond rise, 0.2-0.8 m of depth | Q / A = 5.56 mm/s +- 10 % (the creek delivers Q once its own storage is full) | 5.43 mm/s |
| spill | starts with the pond <= 3 cm over the channel's floor (15.0) | 1.8 cm |
| DP-T1 spilling | out / in 1.00 +- 0.02 over the last 30 s | 0.980 (still settling) |
| DP-T2 dam | the pond's inflow dips to 40-60 % of Q 60-90 s after the dam (the kinematic wave down 50 m, 25.38's physics), back to >= 80 % as the pool overtops | 46 % at 78 s, then 82-94 % |
| the dam's pool | crest 15.667 + the weir head of 0.2 m^3/s over 2 m (0.15 m) = 15.82 +- 0.03 | 15.83 |
| DP-T3 break | the pond's inflow 1.5-1.9 Q, decaying over ~2 min | 1.7-1.75 Q |
| ledger | <= 1e-4 m^3 at all times; unchanged within 1e-5 m^3 across each edit (edits never create water) | 1.6e-6 |
| cost | the water's GPU ms <= 2 ms median at steady spill (the creek alone: 1.4-1.7 ms; the pond adds ~36 m^2 of deep water) | - |

**Design check (FeatureDesignKeys).** *Look:* the pond model's water as signed off (26.14, 26.21). *Chunks:* one world-aligned
volume; edits reach it by the pool's change list (26.19), not by chunk. *Generation:* none (a bench). *API:* existing routes
only (`water_av_*`, `/api/world/subcube`). *Visual test:* L4 on the bench - a clip over the pond and the dam (FILL, DAM, BREAK)
for the owner; the control is 25.38's column run on the same bench and gauges. Rig vs shipped: the bench's shipped GPU path;
Manning in the pond is the one stated difference from a still pond's defaults. **READY.**

**Built:** `tools/water_dam_pond.py --run --pond [--sweeps N] [--record]` (no engine change). `--build` re-carved the pond: the
26.15 rebuild of the world had removed it; its last creek metres (x 110-111) now follow the 1/9 m carve (`cb.bed_top`).

**Measured (two full runs, q 0.2; `dam_pond_pondwater_s160.json` + `dam_pond_pondwater.mp4` at the then-automatic 160 sweeps, `dam_pond_pondwater_s40.json`
at 40):**

| | prediction | pond model, 160 sweeps | pond model, 40 sweeps | column model (25.38) |
|---|---|---|---|---|
| DP-T1 pond rise, 0.2-0.8 m | 5.56 mm/s +- 10 % | **5.45** (the creek delivering 0.197) | **5.42** (0.197) | 5.43 |
| spill starts | <= 3 cm over the floor | **5.4 cm** - MISSED (see below) | 5.4 cm | 1.8 cm |
| DP-T1 spilling, out / in last 30 s | 1.00 +- 0.02 | **0.981** | **0.981** | 0.980 |
| DP-T2 the pond's inflow after the dam | 40-60 % at 60-90 s, back >= 80 % | **27 % at 85 s** - deeper than predicted; back to 94 % | 27 % at 84 s; 95 % | 46 % at 78 s; 82-94 % |
| the dam's pool | 15.82 +- 0.03 | **15.824** | **15.824** | 15.83 |
| DP-T3 break | 1.5-1.9 Q | **1.96 Q** - just over | 1.95 Q | 1.7-1.75 Q |
| ledger, max over the run | <= 1e-4 m^3 | **1.4e-5** | **1.4e-5** | 1.6e-6 |
| ledger change across dam / break (2 s) | < 1e-5 | 6.7e-6 / 1.1e-5 | 5.3e-6 / 1.6e-6 | - |
| water GPU ms, median at steady spill | <= 2 ms | **3.17** - MISSED | **1.73** | - |

- **The spill reading.** The spill channel's first column reads dry, then 3.2 cm deep: at 1/9 m the front crossing the dry
  lip is a few cm thick, so the gauge cannot see a 1 cm spill; the pond was ~3 cm over the floor when it first wet and 5.4 cm
  at the first sample past 1 cm. At steady spill the pond stands 17.2 cm over the floor (a broad-crested weir needs 15 cm for
  0.2 m^3/s over 2 m - consistent). The prediction copied the column model's film-scale number; it was the wrong yardstick.
- **The deeper dip** (27 % vs 46 %): the timing matches (the kinematic wave down 50 m of creek, 25.38), the dam's pool rises
  the same, and the books are exact - the pond model's creek below the dam drains further before the overtopping flow
  arrives. A difference from the column model, not a defect found; the owner's look decides.
- **Edits never create water:** the ledger's change over the 2 s around each edit (1.6e-6 - 1.1e-5 m^3) is inside its ordinary
  2 s wander with no edit (median 2.2e-6, max 2.1e-5) - float noise, no water made or lost by placing or removing the dam.
- **The cost.** The automatic sweep count (1.5 x the box's longest side, clamped at 160 - right for a compact pool, 2026-10-08)
  gives this 1062-cell-long box 160 sweeps: 3.17 ms, over the 2 ms all-water budget. At 40 (what the creek bench has run since
  P3b) every number above is the same to 0.6 % and the cost is 1.73 ms. The still-water first guess (26.10) carries a long
  shallow box; its sweeps only have to cross the depth and the width. **Open: the automatic count's rule** - a default change,
  to be designed and measured on the pond rigs (the signed-off pond, the dam break, the run-up) before it changes.

### 26.23 The automatic sweep count - measured, designed, checked, built (2026-10-17). Status: BUILT, ON; not signed off

**The question.** `WaterCoreManager::create` gives a volume without an explicit `sweeps` 1.5 x its longest side, clamped 8-160
(2026-10-08: SOR needs O(N) sweeps for a Poisson problem N cells across; 40 left a residual of 0.48 on the 1/3 m Basin, before
the still-water first guess of 26.10). On the dam & pond bench (1062 cells long) that is 160 sweeps and 3.17 ms - over the 2 ms
budget - where 40 gave the same numbers at 1.73 ms (26.22). How many sweeps does a solve NEED, and on what does it depend?

**Measured - the end state cannot say** (`WaterCoreGpuParityTest.DISABLED_DiagSweepsNeeded`, 6 scenes x 10-320 sweeps vs a
640-sweep run): splashing scenes diverge from any difference - the dam breaks end 3-12 cm apart from the 640 run even at 320
sweeps. Only the steady channel (60 m at 1/9 m, pumped) reads cleanly: worst column 1.8 mm off at 40 sweeps, 1.7-2.4 mm at
80-320 (its own noise), 3.9 mm at 20, 14 mm at 10.

**Measured - the solve's own error** (`DISABLED_DiagSweepsOneStep`): the 640-sweep run stopped at three checkpoints per scene;
from each checkpoint's state ONE tick at each count and at 640, the face velocities compared on the faces touching liquid (the
pressure domain). CONTROL: 640 vs 640 = 0.0000 at every checkpoint (the solve is deterministic; the numbers are the solve's).
Worst checkpoint per scene, error / the fastest face:

| scene (cells: longest, height) | auto today | 10 | 20 | **40** | 80 | 160 |
|---|---|---|---|---|---|---|
| dam break 1 m (60, 6) | 90 | 4.6 % | 0.7 % | **0.0 %** | 0 | 0 |
| kicked pond 1/3 m (12, 9) | 18 | 2.7 % | 0.6 % | **0.0 %** | 0 | 0 |
| dam break 1/9 m (180, 27) | 160 | 17 % | 3.1 % | **0.4 %** | 0.04 % | 0 |
| pond 6 x 6 x 1 m at 1/9 m, kicked (54, 18), ticks 20 / 60 | 81 | 4.1 % | 0.6 % | **0.0 %** | 0 | 0 |
| channel 60 m at 1/9 m, pumped (540, 9) | 160 | 1.4 % | 0.5 % | **0.0 %** | 0 | 0 |
| tank 4 x 3 m deep at 1/9 m, kicked to its LID (36, 30) | 54 | 31-39 % | 28-39 % | 14-32 % | 29-39 % | 15-31 % |
| pond 6 x 6 x 1 m, tick 2 (spray at its lid) | 81 | 78 % | 20 % | 80 % | 80 % | 74 % |

- **Water with a free surface needs ~40 sweeps whatever the box's length** (12 to 540 cells): the free surface is a fixed
  pressure all along the top, so an error only travels ~a depth before it is pinned; the still-water guess (26.10) starts the
  solve at the hydrostatic answer. 40 is within 0.4 % of converged in every free-surface scene, 80 exact.
- **Water pressed against a solid lid converges at NO count here** (the 3 m tank kicked into its lid, the pond's spray at tick 2:
  14-80 % at 40 AND at 160; 640's own residual 1e-2). With no free surface over it, SOR needs O(N^2) sweeps - 160 does not buy
  it either. Not this item's to fix (a multigrid or a PCG pass for enclosed water - logged open).
- So the 1.5 x longest rule buys nothing past 40: on free-surface water it is wasted, on lidded water it is not enough.

**The rule.** The AUTOMATIC count = clamp(1.5 x longest, 8, **40**) (`WaterCoreManager::kGpuSweepsAutoMax`). An explicit `sweeps`
keeps 8-160 (a caller who measured a need can still ask). Volumes that change: every auto volume longer than 27 cells (the dam &
pond bench 160 -> 40, a 1/9 m pond 6 m across 81 -> 40, the 1 m dam-break rig 90 -> 40); the compact ponds (<= 27 cells) are
untouched (the 1/3 m kicked pond 18; the signed-off bench ponds - checked live below).

**Predictions:** (1) every CPU and GPU water test unchanged (the GPU parity tests pass their sweeps explicitly; the ones going
through the manager are compact) - both exes, counts stated; (2) the dam & pond bench at auto: the 26.22 numbers of the 40-sweep
run within 1 % and the water's GPU ms <= 2 (1.73 measured at 40); (3) the signed-off ponds on WaterBench_Small: their auto count
before / after, and if it changes, the slosh's KE decay and the surface within the run-to-run noise.

**Design check (FeatureDesignKeys):** *Look:* unchanged where the count is unchanged; elsewhere the solve stays within 0.4 % of
converged (measured above). *Chunks:* the box's size, not chunks. *Generation:* none. *API:* `water_av_create {sweeps}` unchanged
(explicit 8-160); `gpu_sweeps` in `water_av_list` echoes the count in effect; the default (auto) changes - stated here, the
header comment at the clamp says why 40. *Visual test:* L2 (the one-step table) + L4 on the benches (prediction 2, 3). **READY.**

**Built** (`WaterCoreManager::kGpuSweepsAutoMax` = 40; explicit counts still 8-160). **Measured:**
1. Tests: `phyxel_tests` Water* + Occupancy 343 / 343; `phyxel_integration_tests` Water* 40 / 42 - the same two failures as
   before the change, unchanged in their numbers (the GPU particle run-up 1.19 h0; the 4-tall dam break's halo short 3 - 26.21's
   CORRECTION). Held.
2. The dam & pond bench at the automatic count (now 40; `dam_pond_pondwater.json`, the 160-sweep run kept as
   `dam_pond_pondwater_s160.json`): rise 5.42 mm/s, out/in 0.984, dip 27 % at 84 s, pool 15.824, surge 1.96 Q, ledger 1.2e-5
   m^3 - the explicit-40 run's numbers within 1 % - and the water's GPU ms **1.46** at steady spill (160 sweeps: 3.17). Held.
3. The signed-off ponds: the demo ponds (x 99-104, 1/3 m: 18 cells) keep their 27 automatic sweeps - unchanged; at 1/9 m a 6 m
   pond goes 81 -> 40, which is the bench's 36 m^2 pond above (the same numbers) and the kicked 6 x 6 m pond of the one-step table
   (0.0 % at 40 from tick 20). Held.
**Open (logged, not this item's):** water pressed under a solid lid does not converge at any affordable count (14-80 % one-step
error at 40 and at 160) - an enclosed-water solver (multigrid / PCG) when a cave or a covered channel needs it.

### 26.24 P3d - the column regions removed: survey, design, check (2026-10-17). Status: DESIGN

**Survey (every creator and reader of the column water, by file and line: `docs/evidence/water_core_h1/p3d_column_survey.md`).**
- **Nothing in a normal session makes a column region.** The only creator is the debug route `water_columns` (`Application.cpp`
  handler -> `WaterCoreManager::createColumns`); boot, streaming, world generation, game.json and the MCP server never call it.
  Only bench scripts do. So removing them changes nothing a player sees; it removes a second kind of water from the code.
- **What reads regions each frame (all go with them):** the column step and its solids refresh; the SC load (bodies pressing
  on columns) and the SC cosmetic splash crown (droplets with a negative volume id that carry no water); the RC ripple forcing
  on region layers; region surface fields appended after the volumes' (`surfaceFields`, `rippleLayers` - removed together, so
  the volumes' layer indices stay valid); `columnBoxes()` fed to the debris as wet boxes; `|| !columnRegions().empty()` in the
  render gate; the H2 windows (`openWindow` / `closeWindow`, ghosts, transfers, held columns, `Av::window`, `Av::floorY`).
- **Shared with the pond model - KEPT:** `rippleWindow` and its 384-cell window (every volume's since 26.13), the view point,
  the flow forcing (`m_flowForcing`, volumes are forced at 1464-1497), `WaterSolver::setBedManning` (the pond's bed law), and
  the test `EveryVolumeGetsTheRippleWindow` (volumes only; moved out of `WaterColumnTest.cpp`).
- **`ShoreSolver` STAYS:** the sea's surf band (`ShoreBand`, `water_shore`, off by default, 10 tests) runs on it. It is the
  far / shore question of P5, not P3d. Only `ColumnWater`, the regions and the windows are column-only.
- **Not touched by P3d, stated:** the old CA water (`WaterManager`, sea level, the hydrology table) still runs in every normal
  session (`Application.cpp` 432-435) - that is the world's water today, retired by P4/P5 (the world record, far water), not
  here. Regions never read it.
- **Three measuring tools exist only on the column model:** `water_creek_current.py` (the current: profile, floats, Manning
  tuning - the owner's standing "tools to measure and tune current speed"), `water_creek_splash.py` (a stone into the creek -
  measured the columns' COSMETIC crown), `water_creek_ripples.py` (the flow-forcing A/B). Removing the columns without porting
  them would lose the creek's current, splash and ripple gauges (the owner's standing rule: ripples and splashes carry forward).

**P3d-1 - the three tools on pond water, measured, BEFORE anything is removed.**
- `water_creek_current.py --pond`: the profile from `water_av_column` (`surface_mean`, `mass_depth`, the surface `u`) against
  Manning; the floats as today (debris drifting on the pond creek - the debris already reads the volume's water); the tune step
  through a new route `water_av_manning {id, n}` (echoes the n in effect; clamped 0-0.2: 0 = pond friction, above 0.2 the bed
  stops a 0.1 m creek - stated at the clamp).
- `water_creek_splash.py --pond`: the stone into the pond creek; real droplets (`water_droplets` stats: born / landed / alive,
  m^3 - they carry water, unlike the columns' crown), the volume's ripple layer, the ledger exact across the splash.
- `water_creek_ripples.py --pond`: the forcing A/B on the volume's layer (forcing 0 = the control, the layer asleep).
**Predictions (P3d-1):** current - the profile at Manning within 10 % on the reaches' mid points as P3b measured (0.19 m at
q 0.2 on the stepped bed), floats drifting at 0.8-1.1 x the surface speed (K-T2's band); tune n 0.035 -> 0.05: at the same q the
mean speed x (0.035/0.05)^0.6 = 0.81 and the depth x 1.24 (Manning, wide channel: d ~ n^0.6 at fixed q). Splash - a 1/3 m stone
from 1.5 m: droplets born (> 0, the pond's real splash; the columns made 5-30 cosmetic ones), ripple layer wakes, ledger
change across the splash inside float noise (<= 2e-5 m^3). Ripples - forcing 0: the layer asleep (exact zero); forcing 200:
a visible flow pattern - its peak and slope histogram reported beside the column-era `ripples_sweep.json`. Clips for the owner.

**P3d-2 - the removal.** Delete `ColumnWater.{h,cpp}`, the regions and windows and every per-frame reader above, the
`water_columns` route and handler, `creek_splash` (the crown's knob) and the cosmetic-droplet branch, `Av::window`, `Av::floorY`
(set only by windows; volumes stand on voxel occupancy since 26.15's 1/9 m carve - the owner's option b superseded the line
bed of 25.21 a), `setGeneratorBedQuery` (its only user). Tests: `WaterColumnTest.cpp` goes (its ripple-window test moves to
`WaterRippleTest`), `ShoreSolverTest.APondSpillsWhenItTopsItsCrest` goes (a column-region regression); `ShoreSolverTest`'s
solver tests, `WaterCreekH0Test` and `WaterSteppedBedTest.DISABLED_DiagColumn` stay (ShoreSolver as the 1-D reference).
Tools: the column paths of `water_creek_bench.py`, `water_dam_pond.py`, `water_creek_rig.py`, `water_creek_live.py`,
`water_creek_profile.py` go; `--pond` becomes their only mode (the flag still accepted). Docs: §25's column items get a one-line
"removed in 26.24" pointer, not rewritten (Keep Docs Lean).
**Predictions (P3d-2):** both test exes as before minus the deleted tests (CPU 343 - 11 WaterColumn + 1 moved - 1 ShoreSolver;
GPU 40 / 42 with the two known failures); the creek bench `--pond` at q 0.2 and the dam & pond bench reproduce 26.21 / 26.23's
numbers (mass depth 0.186 m; rise 5.42 mm/s, pool 15.824, surge 1.96 Q) - the pond path never touched a region; a normal session
(WaterBench_Small pond demo) unchanged.

**Design check (FeatureDesignKeys).** *Look:* unchanged in play (no session made regions); the bench tools' clips are the pond's.
*Chunks:* removes code; adds a per-volume n (world-independent). *Generation:* none. *API:* `water_columns` retired (its users are
bench scripts, all ported); `water_av_manning {id, n}` added, echoing the state; no default changes. *Visual test:* L4 - the three
ported tools' clips on the pond creek for the owner (current floats, the stone's splash, the forcing A/B); L2 - both test exes and
the two benches' numbers after the removal. **READY.**

**P3d-1 run (2026-10-17; the creek bench at q 0.2 as pond water, 300 s settled; `--pond` on the three tools; `water_av_manning`
built).** Evidence `current_{profile,floats,tuned}_pond.*`, `ripples_pond_{sweep.json,ff0.mp4,ff200.mp4}`, `splash_pond_stone.*`.

| gauge | prediction | measured | column era (25.28 / 25.29 / 25.30) |
|---|---|---|---|
| profile, mid depth | the stepped bed's reference 0.190 m | **0.188 m** (mean speed 0.54, surface 0.64 m/s, Froude 0.40) | 0.109 m on the then 1/3 m carve |
| profile, wet stations | 40 / 40 | **32 / 40** - 8 stations read dry on a wet creek: OPEN, the gauge or the field | - |
| floats (1/9 m wood) | 0.8-1.1 x the water | **~0.23 m/s = 0.43 x the mean, 0.36 x the surface**, steady (5 m / ~21 s, riding at the surface) - MISSED | 0.40 m/s = 0.88 x |
| tune n 0.035 -> 0.05, 150 s | speed x 0.81, depth x 1.24 | speed x **0.93**, depth x **1.08** - MISSED (two candidates, unchecked: not settled - 24 % more depth over 100 m is ~9 m^3, > 45 s of inflow, and the wave takes longer; or the stepped bed - its drops hold part of the depth at critical flow, which n does not change; to check against the 1-D reference) | x 0.821 (smooth line) |
| ripples, forcing 0 | the layer asleep | **awake, 60 mm peak** - the prediction copied the columns (whose layer only the forcing fed); the volume's layer is also driven by its own 3-D surface motion | asleep |
| ripples, forcing 200 | a flow pattern | 107 mm peak, slopes p50 0.2 / p95 0.5 | `ripples_sweep.json` |
| splash, 1/3 m stone from 1.5 m | droplets > 0, the ledger unchanged | **0 droplets** (the stone in the clip reaches the bed through 19 cm; a small splash shows, no births), ripple 13 mm, ledger change **7.8e-7 m^3** | 5-30 COSMETIC droplets (the crown) |

**What this says.** The pond creek's DEPTH is right; its floats are not: they drift at ~0.4 of the water, the number the debris
law gave before 25.28's fix (0.24 of 0.50 m/s, waterline drag toward rest) - on the column water the fix holds (0.88), on pond
water something still brakes them. The debris reads the volume's surface velocity per column (`WaterCoreManager::columnWater`,
the mean of the wet sub-columns' u, w - the same field the profile reads at 0.64 m/s), so the brake is further along: OPEN,
investigated next (26.25). The splash and the ripple control are not defects shown, but predictions copied from the column
model's cosmetic crown and forced-only layer; the owner's look at the clips decides. **P3d-2 (the removal) waits** until the
floats ride the pond current: removing the columns now would remove the only water the current tools read right.

### 26.25 Floats drift at 0.4 of the pond current - found, measured, built (2026-10-17). Status: DONE, signed off by the owner 2026-10-17 (with 26.26)

**Measured, one switch at a time** (`water_coupling {exchange, solids}`, the creek bench at q 0.2 as pond water, four 1/9 m wood
floats, x 20 -> 40; `current_floats_pond_{exchange_off,solids_off,both_on}.*`):

| coupling | float drift | / the water's mean 0.54 m/s |
|---|---|---|
| both on (shipped) | 0.26 m/s | 0.47 |
| debris displacing water OFF | 0.30 m/s | 0.55 |
| **debris -> water momentum exchange OFF** | **0.57 m/s** (4 / 4 floats, 0.570-0.577) | **1.05** (0.89 of the surface; the column era 0.88) |

**Cause (read off the code, confirmed by the switch).** Every substep the GPU solver damps each body's velocity toward the
GROUND (`solver_integrate.comp`: `b.vel *= mat.linearDamp * GAMMA`, ~0.995 for wood - the material's air resistance); the water
block then pulls it back toward the current, and the water EXCHANGE (E2) hands that pull back to the pond water as momentum
taken from it (`exchanges[]` -> `WaterCoreManager::addMomentum`). So a float riding a current is a steady momentum sink on its
own patch of water - it slows the water around it, and reads that slowed water (`columnWater`, its own voxel column) as the
current: a self-consistent ~0.45. The column water took no exchange, so floats there were limited only by the damping
(0.88). Physically a float moving WITH the water feels no air resistance relative to it worth the name, and gives the water
nothing.

**The rule (the 25.28 / 24.10 pattern, a third time):** in water the material damping acts on the velocity RELATIVE TO THE WATER,
`v = w + (v - w) d`, w = (flow.x, 0, flow.z) while the body touches water (f > 0), d the material's damping factor; out of water
(f = 0) w = 0 - the old law exactly. (First built weighted by f, as the other water laws are: the red test went 0.4904 -> 0.4952
only - a half-sunk float kept half the brake. The material damping is air resistance standing in for numerical calm; on a slow
float it is nothing beside the water's own drag, so touching water replaces it whole.) GPU (`solver_integrate.comp`: the water surface and f read BEFORE the damping) and CPU
(`VoxelDynamicsWorld`, its wet bodies) alike (solver_shared.h: one law).

**Predictions.** (1) RED first, CPU: `WaterBuoyancy.FloatDriftsExactlyAtTheCurrent` - the 25.28 rig (a unit wood body in a
0.5 m/s current, flat water), 40 s: drift within 0.002 m/s of 0.5 - red today at ~0.490 (the damping's share), green after.
(2) The bench, exchange ON: floats >= 0.9 of the water's mean (today 0.47), and within 10 % of the exchange-off run (0.57).
(3) Nothing else moves: the CPU water / buoyancy / debris / dynamics tests and the GPU water tests as before (both exes); bodies
out of water bit-identical (f = 0 is the old law); `tools/debris_settle_bench.py` on DebrisLab (CLAUDE.md: required for any
`solver_*.comp` change) - the same verdicts as its baseline.
**Design check (FeatureDesignKeys):** *Look:* floats ride the current again (the clip for the owner). *Chunks:* a per-body law
on the tile's world-position flow - no chunk quantity. *Generation:* none. *API:* none; `water_coupling {exchange}` stays the
A/B. *Visual test:* L4 the float clip at the bench pose; L2 the red unit test; the settle bench. **READY.**

**Built (the damping rule, CPU + GPU).** Red -> green: `FloatDriftsExactlyAtTheCurrent` 0.4904 -> 0.4952 (weighted by f - see the
rule) -> **0.5000**. Unit suite 4279 pass; the failures besides it: `AtlasManagerTest.BuildAtlasFromSourcePNGs`,
`TransparentCullingCrossChunk.T16_...` (a cost ratio 0.0503 vs 0.05), `FineFaceMerge.SubcubeMerge_CrossCube...` - rendering and
assets, untouched by this session, not checked against an older build - and `WaterCouplingTest.BlastBesideThePondMovesItAndItSettles`,
which passes alone (0.290 m, every run) and fails only after the `*Damage*` tests (0.221 m, KE 0.51 vs 0.95): a damage test
leaves the blast's reference material changed - a test-ORDER defect, not this change's; open. GPU: 43 / 45, the two known.

**Then the live check MISSED (floats 0.30 m/s, predicted >= 0.9 x 0.54) - and the miss was the measurement.** Sampled during
steady drift (the float's reported velocity, the water's surface speed at its column and 2 m ahead, the exchange this frame):
every float moves AT the water round it (0.54-0.86 m/s, within a few % of the local surface speed; the exchange per frame ~0) -
the rule works. But its position advanced at only ~0.26 m/s of WALL time: **the engine ran at 7.4 FPS** (CPU 138 ms a frame,
GPU 16 ms), and the debris clamps its step, so its clock ran at ~half the wall's. `water_creek_current.py` times floats by the
wall clock - it measured the frame rate. Removing the four floats: **81.7 FPS**, CPU 12 ms; the volume's stored bricks 3,656 ->
2,111.

**Why four small floats cost 125 ms (a second defect, unmasked by the first fix):** debris displaces water while it moves faster
than 0.6 m/s (`Application.cpp`, `kDisplaceOnSpeed` 0.6 / off 0.3) - measured against the GROUND. Floats that now ride the
current at 0.55-0.86 m/s cross it and become moving solids in the pond (the body raster, the wake, the solid box over the
bricks they span, every frame) though they move WITH the water and displace nothing as they go. Before the damping rule the
floats were truly slow (0.26 m/s, under the threshold) - which is why the frame rate then was fine.

**Fixes (the same commit):**
1. **Displacement on the speed RELATIVE TO THE WATER** at the body (`WaterCoreManager::columnWater` - its own column's flow,
   the body's sub-column excluded). A float riding the current displaces nothing; a stone dropped into it still does (falling
   at metres a second relative to the water); a body at rest in still water - unchanged (flow 0).
2. **The tool measures in simulation time:** each float's drift = its own reported velocity averaged over x 25-65, beside the
   water's surface speed at its column - independent of the frame rate; the frame rate is printed with it.
**Predictions:** with four floats in the creek the frame rate stays within 10 % of none (~80 FPS); bricks ~2,100; floats at
0.9-1.1 x the water's surface speed at their columns; the dam & pond and creek numbers unchanged (no floats there).

**Measured after both fixes (the creek bench, q 0.2, four 1/9 m wood floats):** the floats in SIMULATION time **1.000** of the water
at their columns (0.629 / 0.629 m/s, 378 samples in x 25-65) - held; no body displacing (moving solids 0) - held. The frame rate
with the floats **still 8.6 FPS** (CPU 117 ms; 122.6 FPS before them) - MISSED: displacement was not the cost. One switch:

| | FPS | CPU ms | bricks stored |
|---|---|---|---|
| no floats | 124.7 | 8.0 | 2,000 |
| 4 floats, exchange ON | 7.4 | 135.9 | 3,418 |
| 4 floats, exchange OFF | 108.9 | 9.2 | 2,008 |
| 4 floats, exchange ON again | 8.0 | 124.3 | 3,417 |

**The cost is the exchange's path on a GPU volume** (`WaterCoreManager::applyMomentum`): on every frame with a record it reads the
WHOLE volume back to the CPU (`syncFromGpu` - 1.75 M cells here), applies the momentum on the CPU copy, and re-uploads it all
(`markWritten` - the stored bricks jump by the re-upload's dense residency). Written for the CPU ponds of §19 (E2); on the GPU it
costs ~115 ms a frame while anything moves in the water. Floats riding the current still send small records every frame (they
bob; the creek's speed changes along it). Pre-existing - the floats only made it visible. Fixed in **26.26**: the momentum applied
ON THE DEVICE, no round trip. (The 0.6 m/s displacement switch on the relative speed stays - it is right on its own: a float
riding a creek pushes nothing aside.)
**The debris settle bench** (CLAUDE.md's gate for a `solver_*.comp` change; DebrisLab, `docs/evidence/debris_settle/water26_25`): the
same verdict in every scenario as its last baseline `water25_k` - drop_layer, packed, crater, crater_subcube, box_single_straddle
SETTLE; drop_pile, blast, box_through_pile FAIL as before (drop_pile's numbers identical; blast 39 -> 41 rebounds, box_through_pile
19 -> 18 - run to run). Dry bodies take the old law exactly (f = 0); the lab is dry. Held.
**Status of 26.25:** the damping rule and the relative-speed displacement BUILT; floats ride the pond current exactly; the frame
cost of moving debris in a GPU pond is 26.26.

### 26.26 The debris -> water exchange on the device - design, build (2026-10-17). Status: DONE, signed off by the owner 2026-10-17 ("the video looks really good")

**What it must do (unchanged):** each record (position, radius, momentum m^3 x m/s) gives the water inside a sphere of the radius
(>= 1.5 cells) the momentum: per axis, du = momentum / W over the faces of the chosen cells, W = the sum of each face's fill-
weighted cell volume (`WaterSolver::addMomentum` - exact: the water gains exactly what the body lost), clamped at 20 m/s.
**How:** the records go up to the device each frame (a small buffer - ~10-50 records a frame, 32 bytes each) and a new mode of
`wc_face_ops` applies them inside the volume's next call, before the projection (where the CPU path's change landed):
- pass 1 - one workgroup per record: its threads walk the sphere's cells (<= 7^3 at the 1.5-cell minimum; bigger bodies more),
  sum Wu / Wv / Ww in shared memory (the same face rule as the CPU: each face once per record);
- pass 2 - the same walk adds du to each face. Two records touching one face: their adds race - applied with integer atomics on
  a fixed-point accumulator per touched face (a scratch lattice), then folded into the float faces in pass 3 - order-independent,
  so deterministic (the 26.6 rule: no reduction-order leak).
**Gates (red first):** `WaterCoreGpuParityTest.GpuMomentumMatchesCpu` - a pond on both backends, the same 20 records (overlapping
on purpose): faces within 1e-6 m/s of the CPU's, the volume's momentum gain = the records' sum to float precision (red: the GPU
has no such path - the test reads the round trip today and would pass by construction, so its red is the COST row below);
`DISABLED_DiagMomentumCost` - 4 floats' worth of records a frame into the creek's volume: < 0.2 ms (today ~115 ms); the bench:
4 floats in the creek, frame rate within 10 % of none; floats 0.9-1.1 x the water (26.25's sim-time tool); the GPU suite as before.
**Design check:** *Look:* none (the same momentum, no stall). *Chunks:* world-positioned records into world-aligned bricks - an
absent brick takes nothing (counted, as the CPU's `dry`). *Generation:* none. *API:* `water_coupling {exchange}` unchanged;
its stats gain `gpu_records`. *Visual test:* L2 the parity + cost rows, L4 the bench. **READY.**

**Built - simpler than designed.** `shaders/wc_momentum.comp`: one workgroup per record; pass 1 sums Wu / Wv / Ww over the record's
face set in shared memory, pass 2 adds du to each face of the set ONCE (the faces are enumerated, not the cells - no duplicates
within a record). The records are dispatched one after another (each dispatch ends with a barrier), so two records touching one
face add in a fixed order: deterministic WITHOUT the fixed-point scratch the design proposed. The records live in a host-visible
buffer written at the start of the volume's next call (`WaterCoreGpu::queueMomentum`, applied after the residency, before the
first substep; <= 256 a call, more counted as dropped); a face whose storage is an absent brick is skipped in W and in the add
alike. `WaterCoreManager::applyMomentum` queues for GPU volumes (no read-back, no re-upload); CPU volumes as before.
Found on the way: `createPipelines` stopped at the first particle kernel for a volume without particles (`break`), so a kernel
listed after them was never made - an access violation in the first test run; now only the particle kernels are skipped.

**Measured:**
- `WaterCoreGpuParityTest.GpuMomentumMatchesCpu` (20 overlapping records, one at the 20 m/s cap; dense and sparse): worst face
  difference **3.0e-8 m/s**, the momentum given the water identical to the CPU's (0.83134, -4.18000, -0.11860) in both storages.
- The bench, four 1/9 m floats in the creek at q 0.2:

| | FPS | CPU ms | water GPU ms | bricks |
|---|---|---|---|---|
| no floats | 127.6 | 7.9 | 1.58 | 2,010 |
| 4 floats, exchange ON | **123.5** (was 7.4) | 8.2 | 1.42 | 2,018 (was 3,418) |
| 4 floats, exchange OFF | 119.8 | 9.1 | 1.32 | 2,018 |

  and the floats in simulation time **1.002** of the water at their columns (0.630 / 0.629 m/s, 243 samples), FPS 118 meanwhile.
  Clip: `current_floats_pond.mp4` (the floats riding the creek, recorded at a healthy frame rate).
- Tests: `phyxel_tests` water / buoyancy / debris / dynamics 379 / 379; `phyxel_integration_tests` Water / Debris / Gpu 44 / 46 - the
  two known failures (the particle run-up, the 4-tall halo).
All the predictions held. **Status: BUILT, not signed off** (with 26.25: floats ride the pond current, and moving debris in a
GPU pond costs ~4 FPS, not ~115 ms a frame).

### 26.27 P3d-2 - the column water removed (2026-10-17). Status: BUILT, not signed off

**Removed** (as designed in 26.24): `ColumnWater.{h,cpp}`; the regions and windows in `WaterCoreManager` (the column step and
its solids refresh, the SC load, the cosmetic crown and its droplet branch, the region ripple forcing, the region surface fields,
`columnBoxes`, `openWindow` / `closeWindow` / `windowInfo`, `Av::window`, `Av::floorY`, `setGeneratorBedQuery`); the
`water_columns` route and handler; `creek_splash` and the `cosmetic_*` stats of `water_droplets`; the column paths of six tools
(`--pond` still accepted, a no-op); `water_creek_rig.py` and `water_creek_live.py`, which only compared the column model's bed
rules (removed with the owner's permission). Kept: `ShoreSolver` (the sea's surf band), the ripple window, the flow forcing.
Tests: `WaterColumnTest.cpp` goes, its `EveryVolumeGetsTheRippleWindow` moved to `WaterRippleTest`;
`ShoreSolverTest.APondSpillsWhenItTopsItsCrest` goes. Found on the way (the survey missed it):
`ShoreSolverTest.SparseRatesEqualDense` also built its creek on `ColumnWater` - rewritten on `ShoreSolver` directly, with the
springs and outfalls supplied by the test as `WaterCreekH0` does.

**Measured:**
- `phyxel_tests` (filter `Water*:*Shore*:*Buoyancy*:*Debris*:*Dynamics*:*Ripple*:*Creek*:*SteppedBed*`): **356 / 356**. The
  source count of matching enabled tests is 364 at HEAD and 356 now: exactly the 8 enabled `WaterColumnTest` tests and the one
  ShoreSolver test, plus the one moved. 26.26's "379" with the same filter is not reproduced by a source count of that commit
  (364) - unexplained, stated.
- `phyxel_integration_tests` (Water / Debris / Gpu): **44 / 46**, the two known failures (the particle run-up, the 4-tall halo).
- The creek bench at q 0.2, 300 s: water depth by mass **0.186 m** (prediction 0.186, 26.21), the highest sub-column 0.190.
- The dam & pond bench (2026-10-18, **Release**, idle GPU, `python tools/water_dam_pond.py --build` then `--run --q 0.2`;
  `docs/evidence/water_core_h1/dam_pond_pondwater.json`), against 26.23:

  | | 26.23 | 26.27 |
  |---|---|---|
  | pond rise, mm/s (prediction 5.56) | 5.42 | **5.42** |
  | out / in, last 30 s | 0.98 | **0.98** |
  | pool level max | 15.824 | **15.824** |
  | surge after the dam breaks, x Q | 1.96 | **1.95** |
  | ledger max abs, m^3 | - | **1.5e-5** |
  | GPU device ms, median steady spill | 1.46 | **1.44** |
  | FPS median | 106 | **119** |

  **Correction:** the first two runs (rise 4.06 / 4.08, 4.5-4.9 ms, 11 FPS) ran the **Debug** build: `phyxel up` starts the
  repo-root `phyxel.exe`, which is a copy of whichever configuration was built last, and that was Debug. I blamed another
  application sharing the GPU; that was wrong. A third run with the GPU idle and Debug still gave 4.21 mm/s, 8.8 ms, 12 FPS. Check
  the root exe (`md5sum phyxel.exe build/editor/*/phyxel.exe`) before any wall-clock bench.

### 26.28 The 4-tall halo failure - a false alarm in the check, not missing bricks (2026-10-18). Status: DONE (a test change; nothing for the owner to watch)

**The question (26.21's correction note):** the 8 x 4 x 8 dam break reported 3 bricks "halo short" - film within 2 cells of a
brick that was not stored - while every field matched the dense run bit for bit. Either the water really came near an unstored
brick, or the check paired two films into a corner.

**Measured** (a temporary diagnostic, the dam break at 4-tall, the downloaded cells at every call the check fired): the bricks
named short were (2,3,0) and (2,3,1) at ticks 149 and 153. The nearest water to each, cell by cell, was **4 cells** away (needed:
2); at the call's start, 5. The check had read the film brick's record as "-x 1, +y 1": one film cell 1 from the -x face, another
1 from the +y face - two different cells, which the per-face minima put together in the (-x, +y) corner. **No water was ever
within reach of an unstored brick.** The residency works from the same per-face record plus the travel, so it errs the same
(safe) way: it stores too much, never too little.

**The change (test only):** `WaterBrickTest` now checks the halo CELL BY CELL from the cells it already downloads every tick
(`cellHaloShort`: every water cell marks the bricks within 2 cells, liquid within 4 - the engine's reaches) and asserts that
count is 0. The engine's `haloShort` stays as it was, reported beside it and documented as an upper bound. The control still
catches a wrong halo: the frozen residency is short by **1060** by cell (1080 by the face check).

**Results:** `WaterBrickTest` 10 / 10; the 8 x 4 x 8 dam break 0 short by cell (3 by the face check). `phyxel_integration_tests`
(Water / Debris / Gpu) **45 / 46**; the one failure is the GPU particle run-up (1.19 h0, pre-existing - 26.21's correction note).
No engine code or shader changed (a header comment), so the unit suite and the benches are untouched.

### 26.29 The pond size ladder - where one pond stops fitting the 2 ms budget (2026-10-18). Status: MEASURED

**The question (owner):** "at what point a pond becomes a lake and more importantly when a pond becomes too big to fit into the
allotted frame budget" - then "we need to be sure. lets take some measurements." My estimate from the 26.11 cost law (the
solver only) was ~26 m (0.3 m deep), ~20 m (1 m), ~16 m (2 m) on a side. It counted the wrong cost.

**The rig:** `tools/water_pond_ladder.py` on WaterBench_Creek, **Release** (`md5sum` of the root exe = `build/editor/Release`),
the GPU otherwise idle (checked with `nvidia-smi` before the run; a first smoke run with another game holding the GPU at ~40 %
was discarded). One rectangular pond on the slab (z 0-27, clear of the creek), floor = the grass top, walls = the volume's box
faces (no shore - differs from a real pond); one sparse GPU volume at 1/9 m, pond friction, the default 8 x 2 x 8 bricks and
sweeps, auto-sleep off. Per rung: the water placed flat (REST; it sleeps within 0.5-1.2 s when it sleeps at all), then a
stone-sized impulse at the centre (STONE). Read: the solver's call (`gpu_device_ms`), the GPU's own pass timings
(`/api/debug/gpu_timing`: the whole frame and the `WaterSurface` pass), substeps, bricks. Control (no pond): frame 2.96 ms,
349 FPS. Evidence: `docs/evidence/water_core_h1/pond_ladder.json`.

| depth | pond | bricks | surface pass, ms | solver after a stone, median / max ms | surface + solver | FPS rest / stone |
|---|---|---|---|---|---|---|
| 0.3 m | 5 x 5 | 180 | 0.27 | 0.45 / 0.54 | 0.72 | 312 / 267 |
| 0.3 m | 10 x 10 | 720 | 1.01 | 0.74 / 0.99 | 1.75 | 247 / 211 |
| 0.3 m | 15 x 15 | 1,530 | 2.13 | 0.90 / 1.21 | 3.03 | 190 / 166 |
| 0.3 m | 20 x 20 | 2,645 | 3.64 | 1.26 / 2.09 | 4.90 | 136 / 117 |
| 0.3 m | 25 x 25 | 4,205 | 5.57 | 1.82 / 2.76 | 7.39 | 96 / 83 |
| 0.3 m | 32 x 28 | 5,920 | 7.92 | 2.08 / 3.10 | 10.0 | 73 / 64 |
| 0.3 m | 46 x 28 | 8,320 | 11.24 | 3.04 / 3.73 | 14.3 | 53 / 45 |
| 1 m | 5 x 5 | 294 | 0.27 | 0.72 / 1.13 | 0.99 | 296 / 250 |
| 1 m | 10 x 10 | 1,296 | 1.01 | 0.87 / 1.39 | 1.88 | 242 / 194 |
| 1 m | 15 x 15 | 2,754 | 2.12 | 1.22 / 1.79 | 3.34 | 185 / 152 |
| 1 m | 20 x 20 | 4,761 | 3.59 | 1.63 / 2.46 | 5.22 | 136 / 112 |
| 1 m | 25 x 25 | 7,569 | 5.59 | 2.24 / 3.46 | 7.83 | 96 / 83 |
| 1 m | 32 x 28 | 10,656 | 7.88 | 3.35 / 4.65 | 11.2 | 73 / 59 |
| 1 m | 46 x 28 | 14,976 | 11.41 | 4.65 / 8.11 (2 substeps) | 16.1 | 43 / 42 |
| 2 m | 5 x 5 | 504 | 0.28 | 0.75 / 1.15 | 1.03 | 312 / 236 |
| 2 m | 10 x 10 | 1,874 | 1.03 | 1.11 / 1.62 | 2.14 | 232 / 192 |
| 2 m | 15 x 15 | 4,284 | 2.22 | 1.62 / 2.88 | 3.84 | 144 / 142 |
| 2 m | 20 x 20 | 7,406 | 3.73 | 2.29 / 3.39 | 6.02 | 104 / 101 |
| 2 m | 25 x 25 | 11,774 | 5.69 | 3.68 / 5.45 | 9.37 | 79 / 76 |
| 2 m | 32 x 28 | 16,576 | 8.04 | 5.20 / 9.12 (2 substeps) | 13.2 | 59 / 58 |
| 2 m | 46 x 28 | - | refused: 2.8 M cells, over the 2 M volume cap | | | |

**What it says:**
1. **The drawn surface is the larger cost, and it is paid asleep.** The `WaterSurface` pass grows with the pond's AREA,
   ~**8.8 us per m^2** (~0.11 us per 1/9 m column), the same at every depth, the same asleep or awake, and (smoke run, 20 x 20)
   ~90 % of it with the camera turned away. Alone it passes 2 ms at ~**15 x 15 m**. A sleeping 1 km^2 lake would cost ~9 s
   of GPU a frame. Whatever it does per column, it does for every column of the pond, seen or not.
2. **The solver, awake, passes 2 ms** at ~30 m (0.3 m deep), ~23 m (1 m), ~16 m (2 m) - close to the estimate; it grows with
   the stored cells (depth) and only for water that is awake.
3. **Both together: the 2 ms budget holds one awake pond of ~10 x 10 m** (0.3-1 m deep: 1.75-1.88 ms; 2 m deep: 2.14). A
   sleeping pond fits to ~15 x 15 m - limited by the surface alone.
4. **Deep water does not fall asleep.** Placed flat and still, the 2 m ponds from 15 x 15 m up and the 1 m 46 x 28 m pond were
   still awake 30 s later; after a stone, the 1 m ponds from 15 x 15 m and every 2 m pond from 15 x 15 m were still awake 30 s
   later (the 0.3 m ponds re-slept in 12-14 s). In a box with vertical walls the waves reflect; a sloped shore would damp them.
   That part is a rig difference - but "never sleeps" at rest is not.
5. **One volume holds at most ~900 m^2 at 2 m depth** (the 2 M-cell box cap, a CPU-reference ceiling that the GPU path still
   applies).
6. The smoke run's "~1.8 ms more outside the named passes" (26.29 first look) was the other game's: on an idle GPU the frame
   grows by the surface pass and nothing else (the solver runs in its own submission).

**So, pond vs lake for this engine:** today ~10 m across awake and ~15 m asleep. Lakes need, in order: (a) the surface pass
costed by what is seen and by distance, not by area; (b) water that sleeps when it is still, at any depth; (c) a disturbance
that wakes a patch, not the whole body (P4); (d) volumes beyond the 2 M-cell box cap.

### 26.30 The drawn surface costed by what is seen - measure, then design (2026-10-18). Status: steps 1-3 BUILT, not signed off

**The owner's call (2026-10-18):** "start with A" - of 26.29's four needs, (a) first: the surface costed by what is seen and by
distance, not by the pond's area.

**What the surface does today** (`RenderCoordinator.cpp` ~4364, `WaterSurfaceMesh.cpp`, `WaterSurfaceRenderPipeline.cpp`):
every frame, the CPU rebuilds ONE mesh from every volume's surface field - a quad (4 vertices x 48 B, 6 indices) per 1/9 m column
plus side quads - and copies it into a HOST-VISIBLE buffer (system memory), which the GPU then reads to draw it all, seen or not.
The buffer holds 524,288 vertices: a surface over ~1,600 m^2 is cut off (`truncated`). The vertex shader only projects.

**Measured** (`tools/water_surface_split.py`, Release, idle GPU, resting 1 m ponds, the solver at 0;
`surface_split_before.json`):

| pond | vertices | surface pass GPU, facing / away | CPU mesh build | CPU frame, away (no pond 1.90) | GPU frame, away (no pond 1.84) |
|---|---|---|---|---|---|
| 10 x 10 m | 32,400 | 1.02 / 0.95 ms | 0.86 ms | 3.07 ms | 2.94 ms |
| 20 x 20 m | 129,600 | 3.60 / 3.45 ms | 3.47 ms | 7.42 ms | 5.45 ms |
| 32 x 28 m | 290,304 | 7.90 / 7.61 ms | 7.85 ms | 14.21 ms | 9.85 ms |

~27 ns per vertex on the GPU and ~27 ns per vertex to build on the CPU, plus the copy - for water that has not changed, drawn
whether it is seen or not. (Why the GPU's draw is that slow is a hypothesis, not measured: it reads 14 MB a frame from system
memory. Step 1 measures it by moving the data, one variable.)

**The design - in steps, each measured, the look unchanged until step 4:**
1. **Tiles, kept.** The surface is cut into world-aligned TILES of 4 x 4 m (36 x 36 columns; a cost structure only - a tile never
   changes what is drawn). Each tile's mesh is built by the same mesher over the same field (its corners read neighbours across
   the tile's edge, so the vertices are the ones the whole mesh has) and kept in DEVICE-LOCAL memory. A tile is rebuilt and
   re-uploaded only when its columns changed since its last build (the field's columns compared, or a per-volume step counter
   when the volume is asleep). Prediction: a sleeping pond costs ~0 CPU (nothing rebuilt) and its draw stops reading system memory.
2. **Culled.** Per frame, a tile is drawn only if its box is in the view frustum (and within the draw distance). Prediction:
   camera away -> the surface pass ~0; facing -> the pass follows the tiles on screen.
3. **Awake water.** A woken pond's columns change every frame, so every tile rebuilds: measured after step 2; if the CPU build is
   over the budget, the mesher moves to the GPU (the field is already there - the CPU reads it back today).
4. **By distance.** Far tiles drawn with fewer quads (a tile's columns merged 2 x 2, 4 x 4 ...), its edges matched to the finer
   neighbour so no seam opens. This one changes the look at distance - designed and shown to the owner on its own.

**Gate for steps 1-2 (red first):**
- `WaterSurfaceTileTest.TilesEqualTheWholeMesh` (the chunked-vs-whole shape of `FloraMarginTest`): the tiles' meshes together are
  the whole mesh's quads, vertex for vertex (positions, normals, depth, foam, flow, ripple), for fields that straddle tile edges,
  with side faces, several runs per column, and two volumes. Red: built before the tiler, failing on a tiler that cuts corners.
- `...AnUnchangedTileIsNotRebuilt` / `...AChangedColumnRebuildsItsTileOnly` (and the tiles whose corners read it).
- Live (L4): `water_surface_split.py --tag tiles` against `before`: asleep facing / away, the pass, the CPU frame; the look by
  pixels at a fixed pose (a capture before and after, no difference allowed in steps 1-2); `truncated` gone for 46 x 28 m.

**Steps 1-2 BUILT (2026-10-18).** `WaterSurfaceTiler` (`WaterSurfaceMesh.{h,cpp}`: `buildWaterSurfaceMeshRect`, tiles keyed by the
field's `source` = the volume's id, skipped whole while its `version` = the volume's step stands); `WaterSurfaceRenderPipeline`
keeps the tiles in two device-local arenas (96 MB vertices, 16 MB indices, first-fit, frees held back kFrames + 1 frames),
uploads only rebuilt tiles through a 24 MB-per-frame staging slot (the rest waits a frame, counted), and draws a tile only when
its box is inside the view's four side planes. The droplets stay on the per-frame ring. `water_render_core` gains
`{tiles: bool}` (off = the old whole mesh, the A/B control), `{cull: bool}` and a `tiles` record (tiles, rebuilt, kept,
fields_skipped, tiler_ms, drawn, culled, uploaded, upload_bytes, arena bytes).

- **Suites (Release, timed):** `phyxel_tests` water filter **360 / 360 in 4 min 36 s** (356 + the 4 tile tests); `phyxel_integration_tests`
  Water / Debris / Gpu **45 / 46 in 45 s** (the known particle run-up). (A Debug run of the unit filter was stopped after 46 min.)
- **Red first:** `WaterSurfaceTileTest` against a tiler that built each tile as its own little field: 6,612 quads differed from
  the whole mesh (the corners and side faces at tile edges). Green with the rect build: 4 / 4, `WaterSurfaceMeshTest` 6 / 6.
- **Measured** (`water_surface_split.py`, Release, idle GPU; `surface_split_tiles_nocull.json`, `surface_split_tiles_cull.json`):

| resting 1 m pond | surface pass GPU, facing / away | CPU frame, away | rebuilt a frame |
|---|---|---|---|
| 10 x 10 m before | 1.02 / 0.95 ms | 3.07 ms | all (0.86 ms) |
| 10 x 10 m tiles, no cull | 0.061 / 0.003 ms | 1.89 ms | 0 (the field skipped) |
| 10 x 10 m tiles, culled | 0.059 / 0.000 ms (0 of 9 tiles drawn) | 1.95 ms | 0 |
| 20 x 20 m before | 3.60 / 3.45 ms | 7.42 ms | all (3.47 ms) |
| 20 x 20 m tiles, culled | 0.157 / 0.000 ms | 3.25 ms | 0 |
| 32 x 28 m before | 7.90 / 7.61 ms | 14.21 ms | all (7.85 ms) |
| 32 x 28 m tiles, culled | 0.351 / 0.000 ms | 4.75 ms | 0 |

  The hypothesis held: with the vertices on the device the same draw costs ~0.003 ms off-screen (it was 0.95) - the old cost was
  the GPU reading the mesh from system memory. Facing, what is left is shading the pixels the pond covers.
- **Awake** (the 32 x 28 m pond in the no-cull run stayed awake - see below): every tile rebuilds every frame, tiler 9.0 ms CPU
  (the old build 7.85 ms + the comparison and the snapshot copy), 15.7 MB uploaded a frame; the pass 0.35 ms (was 7.9).
  Step 3's subject.
- **Left over at rest:** the CPU frame is still +1.35 ms (20 x 20) and +2.9 ms (32 x 28) above no pond with the tiler at ~0 -
  per-frame work over every column elsewhere (candidates, not measured: `surfaceFields()` copies every field each frame,
  `takeMotion` scans every column). Step 3 measures it.
- **The look** (`water_surface_ab.py`, 20 x 20 m resting, a fixed pose, window captures at one cadence): whole vs tiles 0.73 mean
  abs (controls: whole vs whole 0.65, tiles vs tiles 0.70; the water in frame: 5.6 without it) - PASS; repeated: 0.37 against
  0.34 / 0.35 - PASS. The difference images show the same time-moving shading noise everywhere, no tile edges. (A first run
  failed, 2.04 against 0.62: its whole-vs-tiles pair was ~2 s apart, the controls 0.5 s - the gap, not the tiles; the tool now
  keeps one cadence.)
- **Found on the way, for (b):** whether a still pond sleeps depends on timing. Water placed into an AWAKE volume, 1 m deep and 32
  x 28 m, never sleeps (kinetic energy ~2e-9, yet `quiet_ticks` stays 0 - another rest test fails at depth). Water placed into a
  volume that had already slept EMPTY is uploaded and never stepped - it stays asleep with the water in it until something (a
  stone) wakes it: a placement does not wake a GPU volume. So some of 26.29's "slept in 0.5 s" rows may be that race.

**Step 3 BUILT (2026-10-18), not signed off - the water's CPU, measured part by part.** `water_render_core`'s tiles record now
carries `water_cpu_ms` (the frame's whole water block; the manager's update and in it occupancy / step (the GPU call, the births) /
droplets / ripples (collect, the mask, the layer, submit) / sleep; the renderer feed; the motion scan) and the tiler's `build_ms`,
`threads`, `kept_within`; `tools/water_cpu_split.py` reads it on resting ponds and after a stone (`--wake`), the controls behind
switches (`tile_threads: 1`, `tile_flow_tolerance: 0`, `tile_measure` for the movement figures).

- **At rest, where the +1.35 / +2.9 ms went** (20 x 20 m asleep, measured before any change): the ripple mask rebuilt every frame
  0.70 ms, the fields copied to the renderer 0.42 ms, the motion scan 0.24 ms - 1.36 ms, the whole gap. Each now runs only when the
  field changed (its version): the mask also only when a column gained or lost water; the motion scan keeps a field's last result
  while its version stands; the renderer gets the volumes' own fields (pointers - no copy; `destroy` drops its pointer at once).
- **Awake at rest, why every tile rebuilt** (`tile_measure`): on the 32 x 28 m pond that never sleeps every height was bit-equal
  frame to frame - only the surface flow jittered, ~3e-7 m/s. A tile whose run counts, heights and body fractions are bit-equal is
  now kept while its flow and foam moved less than 1e-3 (m/s; foam 0..1) from its snapshot - the drift can never add up past it.
- **After a stone, the rebuilds** run over a small pool (the hardware's threads less two, at most 8); each tile is built from the
  field alone, so the tiles are the same for any count (pinned). The births were read as a 2.3 MB dense array zeroed and scanned
  every step (0.44 ms): now the packed list. The surface readback reset every column and then wrote the wet ones over: now once.
- **Red first:** `FlowJitterWithinTheToleranceKeepsTheTile` (control: tolerance 0 rebuilds every tile on the jitter);
  `AnyThreadCountBuildsTheSameTiles` (1 thread the control; 2/4/7 threads, 20 churned updates, vertex for vertex);
  `KeptSurfaceFieldsFollowStepsAndRemovals` (a stepped volume's field moves, the other's stands, a removed volume's field is gone
  before the next refresh). Its first run failed on the rig, not the code: the second pond sat outside the test's solid-free area
  and held no water - the test now asserts that control first.
- **Suites (Release):** `phyxel_tests --gtest_filter=*Water*` **308 / 308 in 3 min 27 s** (this filter; the 360 of steps 1-2 was a
  wider one); `phyxel_integration_tests` Water/Debris/Gpu **45 / 46 in 45 s** (the pre-existing FLIP run-up). **The look:**
  `water_surface_ab.py --tag 2630s3` whole vs tiles 0.84 mean abs against the controls 0.75 / 0.93 (water in frame: 6.27) - PASS.
- **Measured** (`cpu_split_*.json`, Release, idle GPU, 1 m deep, camera away; the CPU frame with no pond 1.85 ms):

| | CPU frame | water (update+feed+motion) | tiles (build) | upload a frame |
|---|---|---|---|---|
| 20 x 20 m asleep, before | 3.46 ms | 1.36 ms | 0 | 0 |
| 20 x 20 m asleep, step 3 | **1.93 ms** | **0.008 ms** | 0 | 0 |
| 32 x 28 m asleep, step 3 | **2.05 ms** | **0.01 ms** | 0 | 0 |
| 32 x 28 m awake at rest, before | 18.1 ms | 5.24 ms | 9.0 ms (all 63) | 15.7 MB |
| 32 x 28 m after a stone, exact + 1 thread (control) | 15.9 ms | 3.40 ms | 8.96 ms (63) | 15.7 MB |
| 32 x 28 m after a stone, tolerance, 1 thread | 10.3 ms | 3.01 ms | 4.62 ms (46) | 12.9 MB |
| 32 x 28 m after a stone, step 3 (8 threads) | **8.3 ms** | 3.69 ms | **1.56 ms** (45; build 0.77) | 12.6 MB |
| 20 x 20 m after a stone, step 3 | **5.5 ms** | 2.10 ms | 0.78 ms (30) | 7.0 MB |

  (The control's 3.40 ms water already has the births and readback cuts; the awake-at-rest row is the one 26.30 steps 1-2 left.)
- **What an awake 32 x 28 m pond still costs the CPU** (after a stone, 8.3 - 1.85 = 6.5 ms): the GPU call ~1.0-1.5 ms (0.65 ms of
  it the host's residency work), the surface readback ~1.1 ms (billed to ripples: the first reader after a step), the tiles 1.56 ms
  (0.8 of it the comparisons), the motion scan 0.5 ms, the tile upload's copy of 12.6 MB (~1.2 ms, the rest of the frame). The GPU
  solver itself runs 3.0 ms awake on this pond - over the 2 ms budget on its own (26.29) - so (c), the partial wake, is the larger
  lever for awake water; these CPU parts are next after it, or with it.


### 26.31 (c) The partial wake - a disturbance steps a patch, not the whole body (2026-10-18). Status: steps 1-2 BUILT (default off), not signed off

**The owner's call (2026-10-18):** "keep going with (c)" - of 26.29's needs, (c) after (a): a stone in a big pond must not wake
(and pay for) the whole body.

**Today** (mapped from the code): every pass of every step runs over every stored brick (cells over the slot list, columns over
the column table); sleep is one flag per volume, decided on the host from three whole-volume sums (KE / mass < 1e-6 and max
|df| < 1e-5 for 30 ticks); every wake (impulse, momentum record, deposit, moving solid, placement, occupancy patch, source)
wakes it all. The GPU already sums the rest test per 8 x 2 x 8 unit, world-aligned (`wc_reduce` modes 0 / 6) - and never reads
it back. An awake pond's cost is ~0.42 ms fixed + ~1.44 ms per million stored cells (26.16), the pressure sweeps a third of it.

**Measured first** (`tools/water_wake_map.py` - `water_av_units` copies the per-unit partials to the host, opt-in; Release,
idle GPU, 1 m deep, box-walled ponds; `wake_map_c_*.json`). Per brick COLUMN (8 x 8 cells, 0.89 m square, all its heights) the
volume's own sleep test; "+2" = those columns grown by 2 columns - what would be stepped:

| after a stone | 0.1 s | 1 s | 2 s | 4-10 s | 11 s | 15 s | whole volume today |
|---|---|---|---|---|---|---|---|
| 32 x 28 m, centre: +2 share | 10 % | 31 % | 49 % | 81-96 % | 24 % | 19 % | 3.0-3.6 ms, all 15 s |
| 46 x 28 m, corner: +2 share | 4 % | 10 % | 59 % (a reflection) | 37-72 % | 47 % | 23 % | 4.1-5.7 ms, all 15 s |
| 32 x 28 m, no stone | 0 | 0 | 0 | 0 | | | slept at ~1 s |

- In a walled box the waves fill it in ~4 s (sqrt(g h) = 3.1 m/s at 1 m) and reflect: for ~6 s most of a 32 x 28 m pond is
  moving and a partial wake saves little; it saves most of the first seconds and the long tail (moving 1-2 % of columns from
  11 s on, the margin making it ~20 %), where today the whole body pays until it sleeps - and a deep one did not (26.29).
  A lake larger than the waves' reach (they decay as they spread) is where it pays most; a box this size is near the worst.
- The test is sensitive: at a tenth of the thresholds ~80-100 % moves throughout; at 10x, 0-30 %. The thresholds stay the
  volume's own (a column sleeps by the test the whole body sleeps by today) - changing them is a separate, look-affecting call.
- The still pond (no stone) moved nowhere and slept at ~1 s: 26.29 / 26.30's "deep placed water never sleeps" did not repeat
  here - the placement race of 26.30, not water that keeps moving. (b) stays open for the race itself.

**The design.**
1. **The unit of activity is a brick column** (8 x 8 cells, all heights): a water column is awake or frozen whole - no frozen
   layer under moving water. Per call the host reads the unit map (as `water_av_units`, always on for sparse volumes: ~12 bytes
   a unit, ~160 KB a call at 46 x 28 m) and keeps a quiet count per column; a column is MOVING until it has passed the
   volume's test (KE / mass < keWake, max |df| < maxDeltaFQuiet) for restTicks. AWAKE = moving grown by a margin of 2 columns
   (1.8 m: a 3.1 m/s wave crosses it in ~0.6 s, ~35 calls - it wakes the columns ahead of it long before it reaches them).
   The volume is asleep when no column is awake (the whole-body test becomes the per-column one, the same thresholds).
2. **A frozen column is not stepped and is a wall to the awake ones.** Its threads return at once (a flag in the slot and
   column tables - every kernel already starts with `threadSite` / `threadColumn`), its f, velocities and pressure stay as
   they were. Between an awake and a frozen cell the face is closed: the matrix leaves the frozen neighbour out
   (`wc_classify`'s blocked test - Neumann, as a wall), the sweeps read no pressure from it, the face velocity is held at 0
   (as `enforceSolids`), transport carries nothing across (as a solid face). Mass is exact by construction: no flux crosses.
   The error is what the frozen columns would have done - by the test, below keWake / maxDeltaFQuiet - and the wall sits two
   columns beyond any moving water.
3. **Wakes are local.** An impulse, a momentum record, a deposit, a source, a moving solid or a placement wakes the columns it
   touches (grown by the margin) - not the body. A frozen column's own reads stay valid: the surface field, the reductions and
   the residency's water record still run over every stored brick (the surface is drawn, the mass is the whole body's).
4. **What does not change:** residency (which bricks are stored), the CPU reference (no partial wake - it is the parity
   oracle), the look of any column that moves. **Cost:** frozen threads still launch and return (a first build; slot ordering
   so frozen bricks are skipped by the dispatch is a later step if the measured share says so); the sweep count stays the
   volume's (the active extent's count is a later, measured step).

**Gate (red first):**
- `WaterPartialWakeTest` (integration, GPU): a 20 x 10 m pond, 1 m deep, a stone at one end; the same run with the partial
  wake ON and OFF (the control: the whole body). Predictions, written now: **mass equal** (relative 1e-6); **every column's
  surface within 1 mm** of the whole-body run at every sampled tick; **frozen share > 50 %** of columns over the first 0.5 s
  (the far end untouched); the far end's columns WAKE before the wave reaches them (their height never jumps by more than the
  1 mm bound). Red first: the interface faces left OPEN (frozen neighbours read as liquid) - mass drifts / the bound breaks.
- Live: `water_wake_map.py`-style run with solver ms against time, ON vs OFF, on 32 x 28 and 46 x 28 m; a pixel A/B of the
  first seconds after a stone (ON vs OFF at one cadence, as `water_surface_ab.py`); then the owner watches a stone in a big
  pond, both ways. The look must not change; the cost must follow the moving share.
- `debris_settle_bench.py` is not affected (no `solver_*.comp`); floaters read `columnWater` from the field, unchanged.

**Design keys.** Voxel look: unchanged (same cells, same surface). Chunks: the brick column is a COST structure; what is frozen
follows the water's own motion (world-derived), never a chunk; the bound above is what the equality test measures (it is no
longer bit-equality - the first cost structure that may change results, by at most the sleep test's own threshold). Generation:
none. API: `water_av_units` (measurement); `water_av_create {partial_wake}` + a manager default; the record reports
`awake_columns`, `frozen_columns`. Visual test: the gate above, the rig small (a 20 x 10 m pond fits in two chunks), the one
variable the switch.

**Step 1 BUILT (2026-10-18), not signed off - the frozen columns; the gate passes; the cost barely moves (measured).**
`WaterCoreGpu`: per brick column a quiet count from the unit map (now read every call when the partial wake is on), moving /
awake (+2 columns) / frozen, bit 30 of the slot and column tables; `water_core.glsl` `frozenCell` + the bit in `threadSite` /
`threadColumn`; the wall in `wc_classify` (blocked, liquid, the divergence through a face a frozen neighbour holds),
`wc_rbgs` (no pressure from it), `wc_fill_advect` and `wc_face_ops` (blocked). Every whole-volume wake now goes through
`wakeAll`; an impulse, a momentum record and a droplet's landing wake their own columns (`wakeBox`). API: `water_av_create
{partial_wake}`, `water_av_partial_wake {id, on | default}`, the record's `partial_wake`, `awake_columns`, `frozen_columns`,
`moving_columns`; `water_av_kernel_timing {on, reset}` (the GPU's per-stage stamps, a measurement). Default OFF.

- **The gate's predictions MISSED - and the floor showed why.** Written first: the surface within 1 mm of the whole-body run,
  mass equal to 1e-6. Measured: 214 mm and 1.8e-6. The control that was missing: the whole body against ITSELF is bit-equal
  (0), but against itself with the stone 1e-5 stronger it differs by 273 mm at the splash, 1.6 mm far from it (> 3 m) and
  2.8e-6 in mass - a splash amplifies any change at all. And the whole body LOSES water steadily (199.999726 ->
  199.998461 m^3 in 5 s on the still 20 x 10 m pond, ~0.25 L a tick) to the residue sweep, which a frozen column does not run:
  the "mass" difference was the frozen columns keeping water the whole body drops. So the gate compares the BOOKS (mass +
  residue dropped) and bounds each case by 2 x that floor (+ 1 mm, + 1e-7):

| `WaterPartialWakeTest` (20 x 10 m, 1 m, sync) | surface max | far (> 3 m) | books | frozen, first 0.5 s |
|---|---|---|---|---|
| push 0.4 (small waves): floor (OFF vs OFF, push +1e-5) | 6.34 mm | 0.238 mm | 2.2e-7 | |
| push 0.4: ON vs OFF | 6.48 mm | **0.245 mm** | **1.9e-7** | 63 % |
| push 0.4: OPEN interface (red) | 33.1 mm | **33.1 mm** | 2.1e-7 | |
| stone 4: floor | 273 mm | 1.59 mm | 2.8e-6 | |
| stone 4: ON vs OFF | 214 mm | **1.72 mm** | **2.2e-6** | 55 % |
| stone 4: OPEN interface (red) | 255 mm | **24.5 mm** | **2.2e-5** | |

  PASS: ON sits at the floor; the red control breaks it (the far field 33 / 24 mm, the books 10x). The 1 mm / 1e-6 bound of
  the design was not a measurement - the floor is.
- **Found and fixed on the way:** with the partial wake a stone's splash raining back (droplet deposits) woke the WHOLE
  volume ~1 s after the stone (`setDeposits` called the whole-volume wake): every column awake at 3.5 s; now the landing's
  column wakes - the awake set grows smoothly with the wave (216 -> 856 columns over 6 s).
- **The cost, live** (`water_wake_map.py --partial on|off`, 32 x 28 m, centre stone, Release): with 84 % of the columns
  frozen at 0.16 s the solver took 2.41 ms against 3.06 ms - a fifth, not four fifths. Per stage (`water_av_kernel_timing`,
  the whole body awake, ms a call): sweeps 1.4-1.5, the copies at the start of the substep (f -> fPrev / fOrig, u / v / w ->
  old: whole buffers) 0.74-0.82, the surface copy 0.41-0.43, transport 0.2-0.27, extrapolation 0.22-0.24, velocity advection
  0.10-0.14, the reductions 0.10-0.13, the rest < 0.1 each. A frozen brick's threads still launch and return, the whole-buffer
  copies cover it, and the 80 sweep dispatches launch over every brick. **Step 2:** the awake bricks moved into the low slots
  (a partition, the compaction's copies through a scratch buffer), the step's dispatches and copies over those only.
- Suites: `WaterPartialWakeTest` 1 / 1; `phyxel_integration_tests` Water/Debris/Gpu 46 / 47 (the FLIP run-up) with the switch
  off everywhere else - the shaders changed, the default path is the old one (frozenOn 0). GPU tests must run with the bench
  engine stopped: sharing the GPU, the gate took > 10 min (9 s alone).

**Step 2 BUILT (2026-10-18), not signed off - the awake bricks first; the step covers only them.** `partitionSlots` (after the
residency and the compaction, every call with a frozen column): the awake bricks are swapped into slots [2, 2 + awake) through a
scratch buffer (f, fLo, u, v, w, p, occ, src, fPrev, the bodies' field), the awake column entries go first; the step's
dispatches and copies then run over those slots and columns only (`stepSlots`, `stepCols`), the brick record, the reductions
and the surface over every stored brick. The sweeps' wall became one fill (the frozen bricks' liq to 0, once a call - they lie
together above the awake ones) instead of a frozen test per neighbour per sweep. Reported: `step_slots`, `partition_moves`.

- **Found on the way (red, then fixed):** the partition first moved a brick's water but not its fPrev - the rest test reads
  |f - fPrev| of every brick and the step now refreshes fPrev for the awake ones only, so a moved frozen brick read as moving
  and woke its neighbours: the whole 32 x 28 m pond awake 1.4 s after the stone where step 1 had 471 columns. fPrev moves with
  the brick now (the compaction too). And the per-neighbour frozen test in the sweeps cost ~0.4 ms with every column awake
  (3.44 against 3.06 ms) - the liq fill made the fully-awake pond cost what it did without the partial wake (3.03 vs 3.04).
- **Gate:** `WaterPartialWakeTest` unchanged and still at the floor (push: far 0.245 mm, books 1.9e-7; stone: far 1.69 mm, books
  2.2e-6; the red control breaks it), plus: the fewest slots a call stepped, 272 of 2,484 stored bricks (asserted below).
- **Measured** (`water_wake_map.py --partial on|off`, Release, idle GPU, 1 m deep, the solver's device ms, medians;
  `wake_map_c4_*.json`):

| after a stone | 0-1 s | 1-2 s | 2-4 s | 4-8 s | 8-12 s | 12-15 s | mean, 15 s |
|---|---|---|---|---|---|---|---|
| 32 x 28 m centre, OFF | 3.09 | 3.03 | 3.04 | 3.04 | 3.03 | 3.05 | 3.17 |
| 32 x 28 m centre, ON | **2.10** | **2.01** | 2.84 | 3.03 | 3.23 | 3.02 | 3.05 |
| (awake columns of 1,184) | 237 | 485 | 806 | 1,182 | 1,174 | 1,108 | |
| 46 x 28 m corner, OFF | 6.23 | 5.04 | 4.53 | 4.56 | 4.28 | 4.15 | 4.81 |
| 46 x 28 m corner, ON | **2.00** | **1.75** | **1.92** | **2.57** | 3.23 | 3.27 | **2.76** |
| (slots stepped of 13,320) | 916 | 1,586 | 2,410 | 4,794 | 7,658 | 8,854 | |

  In the walled 32 x 28 m box the waves fill it in ~4 s and reflect: from then on nearly every column moves by the test and
  the partial wake saves nothing (nor costs anything now). The 46 x 28 m pond, a corner stone: -43 % over 15 s, -60 to -70 %
  in the first 4 s. **What is left with 7 % of the bricks stepped (2.0 ms):** the work that still covers the whole volume each
  call - the surface field and its copy to the host (~0.45 ms), the brick record and the reductions, the 80 sweep dispatches'
  launches whatever their size, the per-call buffer clears; and the moving share itself, which the volume's own test keeps
  high (a tenth of a millimetre a second is "moving"). Next levers, measured first: the surface over the awake columns only
  (a frozen column's field is unchanged), the sweep count from the awake extent, the test's thresholds (a look decision).

**The owner's video (2026-10-18) - and two defects it caught.** `tools/water_partial_wake_video.py`: a 1 m stone
(`spawn_gpu_particle`) dropped into the 46 x 28 m pond twice, the partial wake OFF and ON, recorded from the window and put side
by side with the solver's live time and the awake columns burned in (`pw_look_side_by_side.mp4`, `pw_look.json`). The rig's
look only: a sand bed under the pond (grass blades showed through the water) and a stone kerb round it, re-laid every run.
- **A real stone woke the whole pond every frame.** A moving solid's upload (`setSolidsBox`) called the whole-volume wake: with
  the stone in the water all 1,664 columns stayed awake for the whole take (the impulse "stone" of the measurements never
  showed it). Now its box's columns wake (`wakeBox`; the box holds the bodies and the cells they left).
- **26.30 (steps 1-2): tile-shaped HOLES in the drawn water after a stone** (`hole_ab_2.5.png`: tiles left, the whole-mesh path
  right - same water, the probes read 17.0 m everywhere). The tile arenas handed out offsets in 256-byte steps but drew each tile
  from vertexOffset = offset / 48 (the vertex size): a wavy tile's arbitrary vertex count put the next tile mid-vertex and it
  drew garbage. A RESTING tile has 5,184 vertices - every offset a multiple of 48 by chance - so 26.30's still-pond A/B passed.
  The vertex arena now aligns to 768 bytes (lcm(256, 48); a `static_assert`); red first:
  `WaterSurfaceTileTest.EveryVertexArenaOffsetIsAWholeVertex` - 0 of 1,915 offsets mid-vertex, the 256-byte control 1,294 of 1,911.
- The final take (both fixes): OFF 4.1-5.8 ms after the impact (20.6 ms on it), ON 2.8-4.4 ms while the awake columns grow 262
  -> 1,580, the impact itself 4.0 ms; no holes (`pw_look_frame3.png`, `pw_look_frame11.png`).

**The owner's splash look (2026-10-18) - DECIDED: microcube droplets.** The owner on the video: "the splashes seem way too round
and numerous - really overdid it with the splash particles"; and "the water pulls away from the edges". Measured: a 1 m stone
throws ~20 L; at the 21.11 droplet size (1/9 of a cell = 1.2 cm at 1/9 m) that is ~11,000 droplets, each parcel bursting
isotropically into a ball. `tools/water_splash_variants_video.py` showed four variants side by side (`splash_look_grid.mp4`,
`splash_look_grid_2.6.png`): 1.2 cm ball 11,304 at the peak; 3.7 cm ball 745; **11 cm (one microcube) ball 375**; 3.7 cm thrown
along the splash (`water_droplets {lateral: 0.2}`) 502. **The owner chose the microcube** ("bottom left looks the best") - the
droplet size default is now 1 (a whole cell; `DropletStats::size`), the spread stays isotropic (`lateral` 1, kept as a knob).
The "edges" were the rig: the water filled exactly to the kerb's top, so a trough of a few mm showed the kerb's lit inner face as a
bright line - the probes read the water level at the wall; the kerb is now two blocks high.
**Open (the owner, same review): "the water looks a little spiky while it settles on top of the dropped stone"** - confirmed in
the frames (`splash_look_2.mp4`, 2.8-3.6 s): thin transparent blades above the stone at impact, triangles on its top face while
the film drains, gone by ~3.5 s. Not yet measured; the suspicion is the mesher's corner averaging across very different run
heights (the film on the stone, the splash column beside it, the pond) - to be checked against the field before any change.
**Measured (2026-10-18) - the suspicion was WRONG; the spikes are in the solver's water, not the mesher.**
`tools/water_stone_spikes.py` (the splash rig; `water_surface_dump {x1, z1, x2, z2, cells}` = the field the mesher reads per
sub-column, plus the tallest column's cells read back the same frame; `spikes*.json`):
- The FIELD stands up to ~1 m above the pond for ~1.5 s after the impact (`field_rise` 0.95-1.06 m, 2.0-3.6 s); the mesh only
  follows it (`mesh_rise` <= `field_rise` every sample). The ripple layer peaks at 7 cm - not it.
- The tall columns are not the stone counted as water (20.5's levelFill): their cells hold real water, f 0.85-1.0 all the way up,
  and the volume's top cell at the box's lid is OVER FULL - f 1.7, 3.6, 6.2, 7.3, 8.6 in successive samples.
- They follow the lid: the same stone on a 20 x 20 m pond, the box 2 m tall (1 m of air) vs 4 m (3 m of air) - the tallest column
  0.98 m vs **1.66 m** above the pond, and in the tall box it is a solid 11 cm pillar of full cells 17.06 -> 18.7 m standing on
  the stone 1.5 s after the impact, still GROWING (0.69 -> 1.66 m between 2.9 and 3.5 s) while the stone rests on the bed. A free
  11 cm pillar falls in ~0.5 s; this one is being fed from below.
- Suspect (not yet proven): the over-full carry (21.2 / 26.9, `wc_fill_advect` mode 5) - a cell with f > 1 - s sends the excess
  straight UP its column, and a settling stone's body fraction moving under water keeps making its cells over full, so the
  displaced water is pumped up the stone's columns to the lid instead of out to the sides. Next: count the carry per tick on the
  stone's columns (red: it rises with the pillar), then the fix candidate - the displaced excess goes to the neighbours, up only
  where nothing beside it can take it.

**26.32 - proved, three fixes tried, none kept (2026-10-18, the owner: "go ahead, prove it then try the fix").** Built, not
signed off; no behaviour change is left in the code.
- **The counter** (kept): `wc_fill_advect` mode 5 now adds up the excess each cell MAKES (its own fill over its room, before the
  carry from below reaches it) by kind, and what parks at a ceiling - `water_av_list` `carry_rate_m3` (a body's cell with a
  displacement rate q), `carry_body_m3` (a body's cell without one: fresh, walled in, or s >= 0.999), `carry_flow_m3` (no
  body: the gather over-filled it), `carry_parked_m3`, all since the volume was made; Bricks[counterOff + 2..5] (WC_COUNTERS 6;
  the brick records moved up behind them - leaving wc_bricks at counterOff + 2 first broke four WaterBrickTest equalities).
  `tools/water_stone_spikes.py` logs it; `tools/water_stone_spikes_summary.py` sums a run.
- **Proved:** the carry happens only while the stone moves and the field's columns stand only then - 20 x 20 m pond, 3 m of
  air: 1,654 L carried up out of the stone's cells in the half second after impact (137 L of flow over-fill, 211 L parked at
  the lid) for a 1,000 L stone; nothing once it rests. Split by kind (two drops): with a rate 1,247 / 880 L, without 1,331 /
  819 L. So it is the 26.5 carry, every substep, taking a moving body's displacement straight up its column - through the
  stone and out on its top - for the half the rate (20.2) was meant to shed sideways through the projection over the frame,
  and the column ledger (20.5 rule a, by design) for the other half.
- **Tried, and why each failed** (3 drops each way, 20 x 20 m, 3 m of air; peak m / s over 30 cm, OFF vs ON):
  1. *Excess of a body's cell to the four beside it first* (a two-pass exact gather): no better (OFF 0.94-1.08 m, 0.9-1.5 s;
     ON 0.85-1.21 m, 1.4-1.9 s). Under water the cells beside the stone are full - there is no room beside it.
  2. *A body's cell takes in only its room* (20.5's transport rule, which the 26.5 gather dropped): no better (OFF 0.88-3.02 m,
     ON 0.96-1.06 m; the carry out of the stone 1,800-3,000 L either way). The excess is not inflow; it is the stone moving
     into cells that hold water.
  3. *The carry leaves a cell with a rate alone* (the projection sheds it): no better (OFF 0.96-1.46 m, 1.0-2.0 s; ON
     0.75-2.87 m, 0.9-1.4 s).
- **Why the A/Bs cannot decide:** the stone (a GPU debris body) tumbles differently every drop - the same setting gave 0.75 and
  2.87 m. Three drops a side cannot resolve a fix from that spread. **Next, before any further fix:** a rig that repeats - a
  body moved through the water on a fixed path at a fixed speed (no tumbling), the pillar's height and lifetime its metric, the
  carry counter the mechanism's check.
- **Also seen:** a 0.84 m single-column jet 4 s after impact with NO carry at all (the stone at rest) - flow, probably the
  cavity closing behind the stone; its own item.

**26.33 BUILT (2026-10-18), not signed off - the surface over the awake columns only.** The owner's order: "1 and 2 next" (the
look, then the partial wake's whole-volume remainder). Measured first, on an idle GPU (the evening's first numbers were taken while
another application held the GPU at 98 % - discarded): the 46 x 28 m pond, a 1 m stone, the partial wake ON, 0.2-3 s after it
(~14 % of the columns awake), per call - the surface copy to the host **0.63 ms**, the sweeps 0.57, the call's start (residency,
partition, clears: the "sources" stamp) 0.23, the reductions 0.19, everything else under 0.11 each; 2.17 ms in all.
- **Built:** with frozen columns the surface kernel and its copy cover the awake column entries only (they lie first after the
  partition); the call records which, and `readSurface` updates those columns and keeps the rest of the host's field - a frozen
  column's surface cannot change. It refuses (the caller extracts the whole field) when the host's field is not this volume's last
  read, or a collected call's surface was never read (its columns would be lost). A/B: `water_av_partial_wake {surface}`.
- **Gate (red first):** `WaterPartialWakeTest.TheSurfaceOverTheAwakeColumnsKeepsTheFrozenOnes` - the stone run read every column vs
  awake-only: 180 of 180 reads partial; heights, runs, ground, body and foam BYTE-equal at every tick; the surface velocity within
  8.0e-7 m/s (a frozen column's u, w average the face it shares with an awake neighbour, which the awake side still moves - inside
  the tiler's 1e-3 m/s flow tolerance, asserted). Red: resetting the uncovered columns as the whole read does - 180 ticks differ.
  The first run caught exactly that velocity difference (880 columns over 100 ticks, all ~1e-6 m/s, all at awake/frozen borders).
- **Measured** (2 runs each, the same window): the surface copy 0.59-0.60 -> **0.07-0.10 ms**; the call 2.00-2.22 -> **1.29-1.61 ms**.
- **Left** (same window): the sweeps 0.46-0.56 (40 dispatches over the awake slots - launches more than work), the call's start
  0.14-0.21, the reductions 0.17-0.19 (whole volume), extrapolation 0.08-0.10.
- **The owner on pw_look3 (2026-10-18):** "really hard to see. the water is too clear, the camera is too far away, and it looks like
  the left one is better visually, but i really cant see much going on on the right one". Measured before re-shooting
  (`tools/water_wave_reach.py`, the same repeatable push, single columns at 1-16 m, two runs a side): OFF vs ON rms 11.3-11.6 vs
  11.5-12.1 mm at 1 m, 5.2-5.3 vs 5.4-5.5 at 2, 1.6 vs 1.7 at 4, 0.13 vs 0.14 at 8 - the same waves (`wave_reach_1/2.json`). Both
  sides looked empty because the waves die within ~4 m and the camera was ~30 m away; the halves differed because the GPU stone
  tumbles differently every drop. Re-shot `pw_look4_side_by_side.mp4`: low, inside the kerb, ~5 m from the same IMPULSE in both
  takes (`water_partial_wake_video.py --push impulse --pose 11,18.3,27.3,-150,-14`); the halves match frame for frame, OFF 3.9-4.9
  ms, ON 1.2-2.4 ms. The global turbidity override (`water_look {active, turbidity}`) barely changed the clear water - not used.
- **SIGNED OFF by the owner (2026-10-18) on pw_look4 - the partial wake's look** (26.31 + 26.33): "yeah. looks good. i can barely
  tell the difference. keep going". The default stays OFF until the owner says otherwise.

**26.34 MEASURED + a knob (2026-10-18), default unchanged - why the awake patch grows over the whole pond.** With the partial wake
ON a call costs 1.1-1.4 ms in the first 2 s after a push but 2.8-2.9 ms by 10-20 s (`wake_map_qs1.json`): the awake columns climb
145 -> 1,206 of 1,664 while the waves out there are 0.5 mm at 8 m and nothing at 16 m (`wave_reach_*.json`). The volume's own
instantaneous test says only 1-8 % of the columns move from 11 s on (`wake_map_c4_thresh.json`): a column that wakes lets the
sub-0.1 mm waves in, they pass the sleep test's thresholds (KE / mass >= 1e-6, max |df| >= 1e-5 a tick - ~0.07 mm/s of level),
the column counts as moving for 30 more ticks and wakes its 2-column margin: the patch creeps. At thresholds x 10 nothing moves
after ~11 s.
- **The knob:** `WaterCoreGpu::s_columnQuietScale` - the per-column FREEZE test's thresholds x this (the volume's own sleep test
  unchanged); `water_av_partial_wake {quiet_scale}` (clamped 0.1-1000, the reasons at the clamp); `--quiet-scale` on
  `water_wake_map.py`, `water_wave_reach.py`, `water_partial_wake_video.py`. Default 1 (today's test).
- **Measured, 46 x 28 m pond, the repeatable push, 20 s** (solver ms medians 0-2 / 2-5 / 5-10 / 10-15 / 15-20 s; mean):
  x1: 1.09 1.40 2.24 2.79 2.94, mean **2.34**, never asleep; x3: 1.00 1.41 1.54 0 0, mean **0.72**, asleep by ~10 s; x10: 1.05
  1.24 1.03 0 0, mean **0.48**. The waves (rms at 1 / 2 / 4 / 8 m, x3 ON vs OFF): 10.6 / 4.7 / 1.4 / 0.11 vs 11.8 / 4.9 / 1.5 /
  0.12 mm (OFF itself runs 10.4-11.8 at 1 m); x10: 9.2 / 4.6 / 1.4 / 0.11 vs 10.4 / 4.7 / 1.5 / 0.13. The frozen pond's level
  after 20 s: spread 0.08 mm (x3), 0.10 mm (x10), 0.01 mm (x1).
- **For the owner (a look decision):** `pw_look5_side_by_side.mp4` - OFF vs ON at x3, close, the same push, 20 s: the halves match
  frame for frame; OFF 3.9-4.6 ms throughout, ON 1.2-2.5 ms falling to ~1.0 ms as the patch shrinks (802 -> 174 columns).
- **x3 SIGNED OFF by the owner 2026-10-19** ("multiplier 3 looks fine") and made the default (`kColumnQuietScaleDefault = 3`; the
  tools default to it and restore it). The partial wake itself stays OFF by default - not yet the owner's call. Checked live: the
  route echoes `quiet_scale 3.0` at boot; the stone run (`wake_map_default3.json`) 13-15 s after the drop has 47-165 columns awake
  (1,019-1,137 frozen; at x1 the patch had crept to 1,206 awake by then), solver 0.7-1.4 ms. Water unit 310/310, integration 134/136 (the FLIP
  run-up; SceneIntegrationTest).

**26.35 MEASURED (2026-10-19) - what the pressure sweeps cost, one exact saving, and the sweep count for the owner.**
- **Where it goes** (`water_av_kernel_timing`, the push repeated so nothing sleeps, partial wake OFF - every stored brick swept):
  46 x 28 m pond (13,336 bricks), 40 sweeps: the sweeps 1.93 of 4.6 ms a call, 20: 0.98, 10: 0.50 - in proportion to the cells, so
  WORK, not launches. 12 x 8 m (1,020 bricks): 0.36 ms at 40, about half of it the 80 launches. A taller box adds no bricks (3,472 /
  3,476 / 3,480 for 1 / 2 / 3 m of air) - the air above is not stored, so skipping air bricks would save nothing.
- **Tried, no change:** the other colour returning before its neighbour reads (a warp holds both colours: the same lines read).
- **Kept (exact):** a neighbour in the thread's own brick read at id + 1 / 8 / 64 without its brick-map lookup (`wc_rbgs.comp`):
  1.93 -> 1.82 ms (-6 %), 0.36 -> 0.34. Bit for bit: SparseEqualsDenseAcrossSeams, FlatBricksEqualCubic.
- **The sweep count (a look decision, default unchanged at 40):** `water_wave_reach.py --sweeps 40,20,10` (two runs each) and
  `sweeps_mass_*.json`:
  - waves, rms at 1 / 2 / 4 / 8 m: 40: 11.5-12.5 / 5.1-5.4 / 1.6-1.7 / 0.13-0.14 mm; 20: 10.8-12.1 / 4.9-5.2 / 1.6 / 0.11-0.12 (the
    same within run noise); **10: broken** - 9.2-9.4 mm at 4 m, a false wave six times the real one.
  - the leftover pressure error (median): 40: 0.008-0.010, 20: 0.60-0.67, 10: 33-46.
  - 20 s at rest after four pushes, 1 m deep: water gained/lost 40: -0.00006 to -0.0003 %, 20: -0.0003 %; level spread 0.02-0.03 mm
    both. 3 m deep (23 x 14 m): under 0.000005 % both, flat both.
  - the creek (`water_creek_bench.py --q 0.1 --record 240`): mid depth 0.1515 vs 0.1513 m, speed by continuity 0.330 vs 0.331 m/s,
    surface speed 0.355 vs 0.354 m/s; solver over the last 60 s 1.25 -> 0.90 ms. Clips `creekbench_pond_g9_q0.1_s40/_s20.mp4`.
  - the saving at 20: ~0.9 ms on the full pond, ~0.35 ms on the creek, ~0.25 ms with the partial wake on (half of ~0.5).
- **20 sweeps SIGNED OFF by the owner 2026-10-19** on the creek clips side by side ("i honestly cant tell the difference" ... "yes"):
  `kGpuSweepsAutoMax` and `kGpuSweeps` 40 -> 20 (the automatic count 1.5 x the longest dimension, now clamped 8-20; an explicit
  `sweeps` still up to 160); `water_creek_bench.py --sweeps` defaults to 20. Live: a new volume echoes `gpu_sweeps 20`. Water unit
  310/310, integration 134/136 (the FLIP run-up; SceneIntegrationTest).
- **The partial wake ON BY DEFAULT, the owner's call 2026-10-19 ("yeah. default")**: `s_partialWakeDefault = true` (a volume
  created with an explicit `partial_wake` keeps its own; `water_av_partial_wake {default}` still switches it). Its cost where
  nothing ever freezes, measured interleaved on the same running creek (`tools/water_partial_wake_ab.py`, 6 cycles of 10 s, all
  452 columns awake): OFF 1.155-1.171 ms, ON 1.188-1.200 ms - **+0.036 ms (3 %), every cycle**, all of it in the reductions (the
  motion-map copy the freeze test reads). Separate runs drift ~0.2 ms (0.90 vs 1.08 for the same settings), so only interleaved
  numbers compare. The creek's flow with it on: depth 0.1511 m, speed 0.331 m/s (off 0.1511, 0.331). Water unit 310/310,
  integration 134/136 (the FLIP run-up; SceneIntegrationTest).

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

---

## 27. Water at every scale - simulated where it counts, appearance elsewhere (owner, 2026-10-19) - PLAN OF RECORD; supersedes §26 P4-P5 and §8

### 27.1 The decision

The owner, 2026-10-19: *"i also think that we can never reasonably expect to have real simulated water at large lake or ocean
level scales. the plan has to include methods that include simulation where it counts but cheap 'appearance' looking water."*

**Why, in numbers.** One 1/9 m cell is 1/729 m^3. A 100 x 100 m lake 2 m deep is ~15 M cells; a 1 km^2 lake 3 m deep ~2 G; the
sea is unbounded. Today's measured rates (26.29-26.35): one awake 46 x 28 m pond ~0.7-2.3 ms, the drawn surface ~8.8 us per m^2
before culling. A lake as cells does not fit any budget, now or with any likely optimisation. So the plan says plainly what
large water is.

### 27.2 What large water is (the three things, in plain words)

| | What it is | Where it lives | What it costs |
|---|---|---|---|
| **The record** | The water itself, at rest: per 1 m world column, where water is (bottom and top of each run, the top as a fraction of a cell); per body, its total amount, its level, its outlets. Always there, whether anyone is near or not. | chunk spans (`Chunk::WaterSpanLocal`, saved with the chunk) + body records (`WaterBodyTable` in `world_meta`) - both exist (§2.2, §16) | storage only; nothing per frame |
| **Simulated water** | The pond model (§26) - 1/9 m cells, velocities, pressure, splashes, droplets, ripples, displacement - running on a PATCH of a body: the part near the player that is disturbed or flowing. | sparse GPU bricks (26.12-26.17), woken from the record, written back to it | the 2 ms budget, shared by every patch |
| **Appearance water** | How the record is drawn where nothing is simulated: a surface at the record's level per column, with cosmetic motion - waves by wind and body size, scrolling flow on rivers, foam at shores - and less detail with distance. It moves no water and changes no record. | the span sheet renderer (`WaterRenderPipeline`, kept in §9) and far tiles (§8.5) | what is on screen, falling with distance |

This is **not a second kind of water.** There is one water - the record says how much and where; the pond model is how it
MOVES where it counts; appearance is how it is DRAWN where it does not move. A cell in a patch and the record's column under it
are the same water at two moments (awake, asleep), the books exact between them. Nothing else simulates water.

### 27.3 The rules this keeps

1. **Exists or not, never assumed.** Water is drawn only where the record (or a patch) holds it. A dug pit beside a lake is dry
   until simulated water flows in. No sea plane under the world, no shared level for unconnected bodies.
2. **Edits never create water.** An edit inside the record wakes a patch there (the water then moves physically); an edit
   outside it touches nothing.
3. **Appearance never moves water.** Cosmetic waves are a function of world position, time, wind and the body's class and size;
   they never change an amount, a level or a span.
4. **No visible seam.** Where a patch meets the record: at rest the simulated surface equals the record's level (gate: < 1 mm);
   the patch's waves have died out before its edge (26.33's wave reach: 0.13 mm at 8 m) and appearance motion fades in over
   the same band; the colour and shading are the same model. Measured at the seam, at a stated pose, in motion.
5. **Chunks are not visible.** The record is per world column; appearance motion depends on world position, never a chunk.
   Patches are a cost structure: the water in a patch must not depend on where the patch's edge fell (equality test: one patch
   vs two adjoining patches over the same water, the 25.32 shape).
6. **The budget decides how much is simulated, never whether water exists.** Over budget, the farthest or quietest patch sleeps
   first; its water goes back to the record.
7. **Features carry forward** wherever water is simulated (ripples, splashes, droplets, floaters, current tools).

### 27.4 The pieces (W1-W6), and what each needs

Each is designed in detail and design-checked before it is built; each ends with the owner's look in motion.

- **W1 - the record is the water (first).** Sparse GPU volumes write back to the record when they sleep and are woken from it as
  a patch. Exists: Phase D's write-back (a pure function of the cells, §16.2), `water_av_sleep` / `seed:"spans"` / `water_av_wake`,
  `WaterBodyTable`, edits under rule 3 (D2) - all for the OLD dense volumes. New: the same on bricks; a patch woken around a
  disturbance (bricks + margin), not the body's box; the patch's border to the rest of the body. **The open design question:**
  what the patch's edge is to the water beyond it. Candidates to measure: (a) a wall (waves reflect - wrong for a big lake);
  (b) the record's level held fixed at the edge, water exchanged with the record and the body's amount changed by the same
  amount (open water; the books exact); (c) a damping band inside the edge that absorbs outgoing waves, plus (b). Also: the
  record is per 1 m column, the cells per 1/9 m - what a 1/9 m film or step loses at sleep (measured, stated). Ends the 2 M-cell
  CPU box: the CPU copy is the patch. **Gates:** cells -> record -> cells round trip (books exact, the surface within 1 mm);
  a 60 x 60 m lake (beyond today's cap) with a stone in it - a patch wakes, waves, sleeps back, the rest never simulated; cost
  inside the budget; the seam rule 4 measured.
- **W2 - appearance water.** The span sheet draws resting and far water: levels from the record, motion cosmetic, detail
  falling with distance (26.30 step 4, a look decision), far tiles beyond the sheet (§8.5). At the patch edge it matches the
  simulated surface (rule 4). **Gates:** a resting lake drawn with no patch at a cost that falls with distance; the seam
  captured at the same pose with a patch awake and asleep; the dig-a-pit red test (dry).
- **W3 - where it counts.** Which water is simulated: within a radius of the player and camera; where something touched it (a
  body, a blast, an edit, a pour); flowing water near the player. Patches merge when they meet, sleep when quiet (26.31's freeze
  test) and when far. Budget-bound (rule 6). **Gates:** walking along a lake shore wakes and sleeps patches with no visible
  change and the books exact; the cost stays inside 2 ms with N patches.
- **W4 - the world on the new water.** Generation places lakes, rivers and the sea as records (D4's generation and the hydrology
  bake exist); the old CA water (`WaterManager`, sea level, the hydrology table) leaves normal sessions (the §9 delete ledger).
  **Gates:** the camera-walk probe 0 violations on Coast, River, Basin; the dig-a-pit red test; a generated lake with a stone in
  it behaves as W1's.
- **W5 - rivers at scale.** A far reach is a record with a discharge and appearance flow; a near reach is simulated, fed and
  drained at its ends from the record (the creek bench's springs and outfalls, from the record instead of pumps).
  **Gates:** the creek bench's numbers on a simulated reach inside a recorded river; current-speed tools on it; the seam.
- **W6 - shores and open sea.** A simulated band at the shore near the player (the pond model); the swell offshore as
  appearance; their meeting measured. §18 (the column shore band) is the reference, not the model - a column solver here
  would be a second kind of water and needs the owner's explicit call.

### 27.5 What this changes elsewhere

- §26.3 P4 ("rest and the world record") is W1 + W4; P5 ("far away") is W2, W3, W5, W6. §8 ("large bodies on top") is
  replaced by this section; its items (body table, spans everywhere, shore band, rivers, far tiles) are inside W1-W6.
- §0 item 3 refined by item 8 (appearance is how recorded water is drawn, never a source of it).
- Nothing is removed by this section; the delete ledger (§9) still governs removals.

### 27.6 Next

W1's detailed design, design-checked, measured first: what the old write-back does on today's sparse volumes, and the three
patch-edge candidates on a lake larger than today's cap.

### 27.7 Design-check of W1 (Feature Design Keys, 2026-10-19) - verdict NEEDS WORK

**What exists, read in the code** (`WaterCoreManager.cpp` 295-430, `WaterCore.cpp` 26-130, `Chunk.h` 65-69):
- `sleep(id)` reads the WHOLE volume back (`syncFromGpu`), turns the dense CPU grid into runs per 1 m world column
  (`columnRunsFromGrid` - the 81 sub-columns of each layer averaged), writes them as chunk spans, credits the body table per
  column, then **destroys the volume**. All or nothing: no part of a volume can sleep.
- `seedFromSpans` / `seed:"spans"` fills the volume's whole box from the spans: each run's height is spread evenly over all 81
  sub-columns of the voxel column, **with no occupancy test** - `placeBox` refuses rock ("never put water in rock"), the seed
  does not. On a 1/9 m bed (the creek's gravel steps, any microcube ground) a woken column puts water inside the ground.
  `WaterCoreWriteBackTest` (7 tests) has no sub-voxel ground, so nothing catches it.
- The CPU side is a dense box everywhere: `Av::grid`, `occCache`, the held-column mask, the full readback at sleep - this is the
  2 M-cell cap (`kMaxCellsPerVolume`). The GPU side is already sparse (bricks).
- Slept water is drawn by the span sheet (`WaterRenderPipeline`), masked out of a live volume's BOX - a different renderer and
  look from the pond surface. The benches run `water.enabled: false`; what a slept pond looks like there is not known.
- Auto-sleep (Phase D) writes back only when the whole volume is quiet; the partial wake (26.31) freezes columns but keeps
  their bricks and cells.

**1. Voxel look.** Simulated water keeps the signed-off 1/9 m surface. The risk is the hand-off: a woken column today is the
span's average spread flat over the voxel column (not the shape the water had), and slept water switches renderer. Both are
look changes at a seam - rule 4 of §27.3. *Unresolved:* how the record is drawn after sleep (proposal: the pond surface's own
tiles built from the record's levels, so sleeping changes nothing on screen; the sheet only beyond them - W2).

**2. Chunks.** Chunk-derived quantities: which chunk stores a column's span (storage - cost only); chunk residency (a
non-resident chunk's columns are HELD - never written, never woken: behaviour, but it is the §5.1 hold rule on world state,
not chunk identity); the writer refuses a column atomically across vertical chunks (exists). No appearance from chunks.
Cross-chunk reads: the record is read by world column across chunks - reading persistent world state by position, not a
chunk-local patch. **The new cost structure is the PATCH**, and its edge does change behaviour (a wall reflects, an open edge
does not). Rule: the patch's margin past the disturbance is at least the measured wave reach (0.13 mm at 8 m, 26.33), so the
edge condition is immaterial to the water inside within a stated tolerance. **Equality tests to write:** (i) the same push
with patch margins M and 2M - the water inside M equal within 0.1 mm (control: margin 0, a wall at the stone, must differ
by more - proves the metric sees the edge); (ii) a sleep / wake round trip of a body straddling a chunk seam and a brick seam
equals the same body inside one chunk, per column (the `SparseEqualsDenseAcrossSeams` shape).

**3. Generation.** W1 is runtime, not a generation stage. It writes the same spans generation writes (D4); a written chunk is
dirty and saved, and a saved chunk is not regenerated, so runtime water wins - as Phase D already does. Write-back is
per-column, a pure function of that column's cells; the seed per column of that column's runs: order-independent by
construction (each column alone). No per-world tuning (the margin and wake radius are engine constants).

**4. API.** Exists: `water_av_sleep {id, force}`, `water_av_create {seed:"spans"}`, `water_av_wake`. W1 adds (names to fix in
the design): sleep the frozen columns of a volume into the record (echo: columns and m^3 written, columns held and why, the
books before / after); wake a patch at a point with a radius (m, world; echo the patch's bricks, columns, m^3 woken); read the
record over a rect (runs per column, m, world Y; body ids). Clamps: the wake radius to the patch budget (cells), the margin to
at least the wave reach - each with the reason at the clamp. **Defaults:** sleeping frozen columns into the record is new
behaviour and changes what is drawn - OFF until the owner looks. Pinned tests that must stay green unchanged: the 7
`WaterCoreWriteBackTest`s, `WaterPartialWakeTest`, `GpuRestDecisionDeterministic`.

**5. Test plan.** "Works" =
(a) cells -> record -> cells on a 1/9 m stepped bed: the books exact (float), no water in a solid cell, the woken surface
within 1 mm of the slept one;
(b) a lake larger than today's cap (60 x 60 m, 1 m deep, a 2 m box: ~5.8 M cells, refused today) runs: a push wakes a patch,
waves, the patch sleeps back, the rest is never stepped, the mean GPU cost no more than the 46 x 28 m pond's 0.7 ms;
(c) the seam: equality test (i); at rest the record's level equals the drawn surface within 1 mm; the same pose captured
awake and asleep differs by no more than two awake frames do.
Depth: (a) L2 unit, (b)-(c) L3 GPU integration + L4 live engine, then the owner's look in motion.
**Red tests, predicted before running:** `SeedOnASteppedBedPutsNoWaterInRock` - RED today (solid cells with f > 0, their
m^3 reported); `ALakeOverTheOldCapRuns` - RED today (the create's refusal string); the margin equality (i) - its control must
fail as predicted or the metric is wrong.
**Test worlds:** (a) synthetic, one 8 x 8 m grid with a stepped bed, no engine; (b)-(c) the WaterBench_Creek pond area - a 60 m
lake cannot sit in one 32 m chunk, so it straddles a seam ON PURPOSE (test (ii) is the reason); one variable per run (the
margin, or sleep on / off).
**Rig vs shipped:** benches run `auto_sleep: false` and `water.enabled: false` (no span sheet), pushes are `water_av_impulse`
(repeatable), not stones; the partial wake ON, x3, 20 sweeps are the shipped defaults.

**Unresolved - what must be designed or measured before W1 is READY:**
1. **Measure first (step 0):** what today's `sleep` / `seed:"spans"` do on a sparse GPU volume (do they run; the readback's
   cost; the mass books; what is drawn after sleep in the bench) - the design starts from the numbers.
2. **The seed must respect the ground:** each run's water level-filled into the column's AIR cells from the bottom (still
   water is flat, so this is the shape it had); write it red first.
3. **The CPU side without a box:** name the sparse replacement for `Av::grid`, `occCache`, the held mask and the full readback -
   proposal: a GPU kernel that writes the runs of the columns being slept (only those bricks read back), the CPU keeping only
   the patch's cells. The brick map is still sized by a box (fine to a few hundred metres; kilometres page it in W3).
4. **How slept water is drawn** - the pond surface from the record (no change on screen) or the span sheet (a look change);
   the owner's call, shown side by side.
5. **The patch edge:** the three candidates (wall, fixed-level exchange with the record, a damping band + exchange) measured on
   lake (b) with the equality test - pick by numbers, then the owner's look.

### 27.8 W1 step 0 MEASURED (2026-10-19) - today's sleep and wake on the sparse GPU volumes

Tools: `water_av_census {id}` (new: m^3 in air / inside ground / unknown / over-full, a whole-volume read-back, debug only);
`tools/water_record_roundtrip.py` (rigs A-C; `--build-step` authors C); the bench engine on Release, idle GPU.
- **They run, and the books hold.** A placed flat pond (rig A, 12 x 8 m, 1 m) with the engine defaults (`auto_sleep` on, the
  partial wake on) **sleeps and writes back by itself 0.5-0.6 s after it is placed** (placed still, the still-water guess calls it
  asleep at once): 96.0000 m^3 placed -> 96.0 in the record -> 95.99999 woken (seed 37 ms) -> 95.99999 after 5 s awake. The
  creek's reach (rig B, 20 m of the 1/9 m carve): 23.3333 -> 23.33333 -> 23.33333, nothing in the ground - the creek's steps
  fall on voxel-column edges, so a column's floor is flat and B never tests a step inside a column.
- **RED, as predicted (`record_roundtrip_C.json`): the seed puts water inside the ground.** Rig C - a 1/9 m Stone step under 5
  of each column's 9 sub-columns (1,620 microcubes, verified in the world: 45 per voxel), 6 x 6 m, 1 m deep: 33.7778 m^3 placed,
  33.77781 in the record, woken 33.77781 of which **2.2222 m^3 in 1,620 solid cells** - every microcube cell - still there 5 s
  later (the books balance, the water is inside the rock). Prediction was 45/729 m^3 x 36 columns = 2.222: exact. The visible
  water is 2.22 m^3 short: **the woken surface stands ~6 cm low** over the 36 m^2.
- **The cost of sleeping a big body:** a forced sleep of the 46 x 28 m pond (1.88 M cells, 13,328 bricks, moving) took **105 ms**
  on the main thread - the whole-volume read-back and the dense CPU pass (a hitch every time a pond that size sleeps). Wake of
  the small rigs: 32-39 ms.
- **What is drawn after sleep, in the bench: nothing.** `w1_draw_awake.png` / `w1_draw_asleep.png` (same pose): awake the pond
  is drawn by the pond surface; asleep it is gone - the bench runs `water.enabled: false`, so the span sheet that draws the
  record is off. Where the sheet is on, slept water switches to its look. Either way, today a pond changes on screen the moment
  it sleeps - and with `auto_sleep` the default, that is 0.5 s after a still pond is placed.
- **So for W1:** item 2 (a seed that respects the ground) is confirmed and has its red test (rig C, 2.2222 m^3 -> 0); item 3 is
  measured (105 ms at 1.9 M cells - the read-back and the dense pass must go); item 4 is sharper than thought - the bench does
  not draw the record at all, so "how slept water is drawn" needs the pond surface built from the record (or the sheet turned
  on) before any auto-sleep is visible to the owner.

### 27.9 W1 item 2 BUILT (2026-10-20): the wake fills the column's air cells from the bottom

`seedGridFromRuns` level-fills each run's water into the column's AIR cells from the run's bottom up (a layer's room is its air
cells' 1 - f, the bottom cell only above the run's bottom; each air cell of a layer gets the same fraction of its room); what
cannot be placed below the grid's top is returned short, never silent. With no rock it is the old fill cell for cell. The
manager mirrors the world's occupancy into the grid before seeding (a CPU volume's grid never had it).
- **Red first:** `WaterCoreWriteBackTest.SeedOnASteppedBedPutsNoWaterInRock` (a 1/9 m step under 5 of 9 sub-columns, still water
  1 m deep, sleep -> wake; the flat column beside it is the control) failed on the old seed - 0.0617 m^3 in 45 rock cells
  (45/729, the live rig's per-column number) and cells off by 1.0 - and passes now: 0 rock cells, the books to 1e-6, every cell
  within 1e-5 of the slept state. The other 7 write-back tests unchanged and green.
- **Live (rig C, `record_roundtrip_C_fixed.json`):** 33.7778 m^3 placed -> 33.77781 recorded -> 33.77781 woken, **0 in the
  ground** (was 2.2222 in 1,620 cells); the woken surface 17.000 m at four points (was ~6 cm low). Rigs A and B unchanged
  (96.0000, 23.33333; `record_roundtrip_fixed.json`).
- Water unit 311/311; integration 134/136 (the FLIP run-up; SceneIntegrationTest). Built, not signed off - nothing on screen
  changes until slept water is drawn (item 4).
- *Known limit, stated:* a 1 m column is one level per run - two pools at different levels side by side inside one 1 m column,
  split by a sub-voxel wall, come back at one level (the mass exact). Still water rarely rests that way; measured if it shows.

### 27.10 W1 detailed design (2026-10-20) - the record is the water

**The unit: an 8 m tile.** A brick column is 8 cells (8/9 m); a record column is 9 cells (1 m). They line up every 72 cells =
**8 m = 9 brick columns = 8 voxel columns**. A world-aligned 8 x 8 m tile is the unit that sleeps into the record and wakes from
it: whole bricks, whole record columns, no partial brick or column ever. (A 32 m chunk is 4 x 4 tiles: storage only, rule 5.)

**A tile's three states** (per volume; the volume is the body's simulated part):
| State | Cells | Stepped | Drawn from | Becomes |
|---|---|---|---|---|
| AWAKE | in bricks | yes | the pond surface | FROZEN when its columns pass the freeze test (26.31, x3) |
| FROZEN | in bricks, kept | no (a wall to the awake) | the pond surface (its last field, 26.33) | AWAKE when a wake reaches it; RECORDED when every column in it has been frozen for `recordAfter` (s) and nothing is in flight over it (droplets, ripple rings, a body) |
| RECORDED | none - bricks freed | no | the record (item 4) | AWAKE when a wake reaches it: seeded from the record on the GPU (27.9's level-fill) |

A volume whose every tile is RECORDED is today's slept volume. Phase D's whole-volume `sleep` stays as the forced path.

**The patch edge (item 5): wake ahead, keep the wall.** Today a frozen column is a wall to the awake ones and a wake spreads by a
2-column margin (26.31); waves in a 46 x 28 m pond die to 0.13 mm by 8 m (26.33). W1 treats a RECORDED tile exactly like a FROZEN
one at the edge (a wall) and wakes tiles AHEAD of the moving water: a tile is woken (seeded) when the awake region's margin
reaches it, so a wave never meets the wall while it is larger than the freeze test. The open-water candidates - the record's
level held at the edge with exchange (b), a damping band (c) - are kept as the fallback for when the budget stops a wake from
spreading (W3); W1 measures whether it is needed: **gate** - the same push with the wake margin M and 2M, the water inside M equal
within 0.1 mm (control: margin 0 must differ by more).

**No full read-back, no CPU box (item 3), staged:**
- *W1b - per-tile kernels.* `wc_record.comp`: for a list of tiles, the runs per voxel column (27.9's rule, the 81 sub-columns per
  layer) into a small buffer - the only read-back at sleep (64 columns x a few runs per tile). `wc_seed.comp`: the inverse,
  level-filling the tile's air cells from uploaded runs. The record is written by the existing span writer and body credit
  (per column; mass in the spans' unit, so a round trip books zero).
- *W1d - the dense CPU mirror goes.* `Av::grid` / `occCache` / the 16 whole-volume read-backs (87 uses in `WaterCoreManager`) are
  re-pointed group by group, each measured first: write-back and seed -> W1b's kernels; debug reads (census, column cells, probe)
  -> a read-back of the bricks asked about; placement and sources -> the in-place GPU patch (26.19); the occupancy cache -> per
  brick (26.19's change list is per brick already); consumers that read the surface (debris, characters) -> the surface field
  (already per column from the GPU). Ends `kMaxCellsPerVolume`. The brick MAP is still sized by the box (4 B per brick: a 200 m
  box ~1 M entries); kilometres page the map in W3.

**Drawing (item 4) - THE OWNER'S CALL, shown side by side before any tile records live:** (A) the pond surface built from the
record for RECORDED tiles - per sub-column the run's top over the ground, through the same tiles and shading (26.30) - so
recording changes nothing on screen (gate: the same pose FROZEN vs RECORDED differs by no more than two awake frames);
(B) the span sheet, a different look. Recommended: (A) near the player; the sheet / far tiles beyond are W2.

**API.** `water_av_tiles {id}` - each tile's state, cells, m^3 (echo); `water_av_record {id, on, record_after}` - recording on /
off for a volume and the seconds a tile must stay frozen (clamped 1-600 s: below 1 s a tile records between two ticks of a wave
too small to freeze it; echo); `water_av_census` (built); `water_av_sleep` / `seed:"spans"` kept. **Default: recording OFF** until
the owner has seen (A) or (B) in motion; then the owner's call.

**Build order, each with its red test and the owner's look:**
1. **W1a - draw a recorded tile** ((A) and (B) side by side for the owner). Red: a forced-recorded tile beside a frozen one at the
   same pose - today it vanishes in the bench (27.8).
2. **W1b - the per-tile kernels + the tile life cycle** (recording behind the switch, OFF). Red: rig C and the flat pond recorded
   tile by tile -> woken -> the books exact, 0 in rock, the surface within 1 mm; and a GPU-vs-CPU parity row on the runs.
3. **W1c - wake ahead** + the margin equality test (above). Red: the control (margin 0) differing as predicted.
4. **W1d - the CPU mirror goes**, group by group, each group's tests unchanged. Red: `ALakeOverTheOldCapRuns` (60 x 60 m, 1 m
   deep in a 2 m box, ~5.8 M cells - refused today with the cap's message).
5. **W1e - the lake, live:** a push in the 60 x 60 m lake wakes tiles around it, waves, freezes, records; the rest never stepped;
   mean GPU no more than the 46 x 28 m pond's 0.7 ms; the books exact over the run; 105 ms whole-volume sleeps gone (a tile's
   record call measured). Then the owner's look in motion.

**Design-check of this design (2026-10-20):** voxel look - unchanged (the pond surface; (A) keeps it). Chunks - the tile and the
brick are world-aligned cost units; behaviour depends on them only through the edge, which the margin equality test pins; the
record is per world column. Generation - none (runtime; spans as Phase D). API - above, with echoes, clamps and the default.
Test plan - the red tests above, small rigs (rig C one chunk; the lake straddles a chunk seam on purpose), predictions stated.
Items 2-5 of 27.7 are answered by 27.9 (built) and this section; item 4 waits on the owner's look. **Verdict: READY for W1a**
(the drawing, for the owner's choice); W1b-W1e READY in outline, each re-checked against its measured first step.

### 27.11 W1a BUILT, SIGNED OFF (2026-10-20): recorded water drawn by the pond surface - the owner's choice A ("go with A")

- **What:** `recordSurfaceField` (`WaterSurfaceMesh`) wakes a tile's record into a scratch grid with 27.9's level-fill and extracts
  the pond surface from it - so a recorded tile draws what the same water drew awake at rest. `WaterCoreManager` keeps 8 m tiles
  within `radiusM` (48 m) of the view point that hold stored water outside every live volume, rebuilt when the record (the chunks'
  span revision), the ground (the occupancy revision) or the volumes change - at most 4 a call - and appends their fields after
  the volumes' in `surfaceFields()` (the volumes own their columns; the surface queries - floaters, characters - read the record's
  water too). The span sheet is masked over those tiles (`recordTileBoxes`), so the two never both draw. The feed is asked for
  even with no volume live. Route `water_av_record_draw {on, radius}` (radius m, clamped 0-256, the reason at the clamp; echoes
  the settings, tiles, wet tiles, builds, pending, build ms). **ON by default** - the owner chose it.
- **Red first:** `WaterCoreWriteBackTest.TheRecordDrawsTheSurfaceTheWaterHad` (the stepped bed, a part-full top cell) failed on a
  stub that draws nothing (0 vs 18 sub-columns) and passes: every sub-column's runs, tops, bottoms and ground within 1e-4 m of the
  awake field.
- **Live (`tools/water_record_draw_ab.py`, a 12 x 16 m pond 1 m deep with rig C's step, the same pose, viewport crop):** the pond
  sleeps into the record (192 columns, 189.78 m^3, spread 0.0 mm) and is drawn by 4 record tiles. Frame difference (mean / % of
  pixels > 48), two runs: awake vs awake 2.5 s apart (the noise floor - wind in the grass) 2.49 / 0.70 % and 6.80 / 3.23 %;
  **awake vs recorded 2.26 / 0.58 % and 2.40 / 0.64 %** - inside the noise; control, the record not drawn (today's bench: the pond
  vanishes) 15.93 / 16.5 % and 14.85 / 16.0 %. (A first run took the noise frame 0.5 s apart and under-read it - recorded read
  1.6-2.1x; fixed by matching the gap.) Frames `w1a_draw_r1_*.png`.
- **Cost:** a tile build ~1.3 ms (the scratch grid's occupancy is read cell by cell), the worst call 5.4 ms (4 tiles at once - when
  a pond sleeps or the camera crosses into new tiles); a call with nothing changed 0.004 ms.
- **Open, stated:** the bench runs `water.enabled: false`, so the sheet mask is untested live (a world with the sheet on must show
  no double surface - checked when W4 puts generated water on this path); chunks streaming in with stored water do not bump the
  span revision - their tiles are rebuilt only when the ground's revision moves (it does on a chunk load); the 4-tiles-a-call
  bound is a cost bound, a tile can draw a frame or two late.
- **Found recording the owner's clip, fixed:** (1) a gap - a sleep bumps the span revision, every tile in range went stale and
  was rebuilt 4 a call in grid order, so the pond's own tiles could wait their turn while the volume no longer drew: now `sleep`
  builds the tiles under the slept box at once, in the frame the volume stops drawing (`buildRecordTilesNow`, ~25 ms for 4
  tiles - a one-off at the sleep; a tile build reads its scratch grid's occupancy cell by cell, W1b's to cut), and other stale
  tiles are rebuilt nearest first (a stale tile keeps drawing its last field - late, never blank). (2) A rig artifact, not a
  defect: this world had lost the earlier rigs' kerb, so the pond was held only by the VOLUME's box edge (solid to the volume,
  open grass to the world) - the record, which reads the world, rightly drew the water's side face standing in the open (a
  green strip at the edge, the water band 4 % less blue after the sleep). With a real StoneBricks kerb round the pond
  (`--walls`, read back from the world) and the camera above it:
  - A/B, two runs (mean / % px > 48): awake vs awake 1.49 / 0.16 % and 1.17 / 0.11 %; **awake vs recorded 1.34 / 0.20 % and
    1.57 / 0.21 %**; the record not drawn 20.84 / 23.7 % and 20.79 / 23.6 % (`w1a_draw_walls_r*`).
  - **The clip for the owner: `w1a_sleep_into_record_walls.mp4`** - 20 s, the pond pushed at 1 s, forced to sleep into the record
    at 14.2 s (still moving: spread 2 mm), drawn from the record after. The water band per frame (blue excess): 25.24 before the
    sleep, 25.23 after; the largest frame-to-frame change in the clip 0.08 (at 12 s, not at the sleep); no blank frame.
- Water unit 312/312, integration 134/136 (the FLIP run-up; SceneIntegrationTest). **SIGNED OFF by the owner 2026-10-20** on
  `w1a_sleep_into_record_walls.mp4` ("looks good.").



### 27.12 W1b first slice BUILT (2026-10-20): frozen tiles sleep into the record and wake from it - recording OFF by default

**What:** the tile life cycle of 27.10 on a GPU volume with the partial wake. A world-aligned 8 m tile whose stored brick columns
have all been frozen for `s_recordAfterS` (2 s; counted by the manager every frame - a body whose columns all froze is asleep and
runs no call to count them; a body asleep as a whole counts as frozen everywhere) RECORDS: only its bricks are read back
(`WaterCoreGpu::readColumns`), its runs per voxel column written to the record (the span writer + the body credit, the spans'
unit), its columns marked recorded (`setColumnsRecorded`: their bricks freed at once and read as the GROUND sentinel - Solid, a
wall to the awake water - and never stored by the halo), the volume's drawn surface cleared there and the record tiles built in
the same frame (W1a draws it). A recorded column never moves by itself; when the awake water's margin or a wake box reaches it,
the manager WAKES the tile: the record read, level-filled into the tile's air cells (27.9), uploaded into fresh bricks AT ONCE
(`seedColumns`, one submission) at rest (quiet: frozen unless motion reaches it), its record columns cleared and the body debited
in the same breath, the volume's surface written from the seed so no frame is blank. Anything that writes into the water - a
push, placed water, debris momentum, a moving body - first wakes the recorded tiles it touches (+ 2 m), synchronously
(`wakeRecordedNear`). A whole-volume `sleep` leaves recorded columns alone (it would have written their empty cells over the
record). Not done in this slice: the per-tile record/seed as GPU KERNELS (the tile's bricks are read back and its runs computed
on the host - 6-8 ms a tile; the 27.10 kernels are the next cut), the dense CPU mirror (W1d).
**API:** `water_av_record_tiles {default?, after_s?, id?, on?}` (after_s clamped 0.25-600 s, the reasons at the clamp; echoes);
`water_av_list` gains `record_tiles`, `recorded_m3`, `recorded_tiles`, `tiles_recorded`, `tiles_woken`, `last_record_ms`,
`last_wake_ms`, `seed_unplaced_m3`. **Default OFF** (`s_recordTilesDefault`) until the owner has seen it.
**The gate: `WaterRecordTilesTest.FrozenTilesRecordAndWakeWithTheBooksExact`** (a 32 x 16 m pond 1 m deep = 4 x 2 tiles, an
in-memory record standing in for the chunk spans, record after 0.5 s): placed 511.9993 m^3 -> **all 8 tiles recorded, 0 m^3 in
cells, 511.9993 in the record** (7.0e-4 m^3 of films dropped by the residue sweep, counted); a touch in tile (0, 0) woke **exactly
1 tile**, its surface within 0.01 mm of the still level, and it **recorded again**; a push (strength 1) woke 9 tiles in all as its
waves met them (wake ahead); **no recorded tile's cells ever held water**; **the books - cells + residue + record = placed -
within 6.6e-6 m^3 at every tick**. Red control (the recorded columns left OPEN: stored by the halo as air, not frozen): **9.47 m^3
poured into the recorded neighbour**. Record 6-8 ms, wake 8-12 ms a tile (host; measured on a busy GPU - the owner was gaming).
**Found on the way (each a failed run first):** (1) a body whose columns all froze is asleep - the per-column quiet counts stopped,
nothing recorded until a push woke it -> counted per tile in the manager; (2) a body asleep as a WHOLE never ran the per-column
freeze -> counted as frozen everywhere; (3) a seed counted as motion woke its neighbours, which woke theirs (one touch woke all 8
tiles) -> seeds are at rest; (4) a seed queued for the next call left the water in neither place for a call (the books 192 m^3
short) -> uploaded at once; (5) a push into a recorded tile wrote into its empty cells and was lost -> writes wake first. The
walled 32 x 16 m box does not settle a strength-4 push inside 15 s even with recording OFF (225-328 columns moving - its walls
reflect, 26.29), so "tiles record again after a push" is reported there, not gated.
Water unit 312/312, integration 135/137 (the FLIP run-up; SceneIntegrationTest). Built, not signed off; live timing and the
owner's look wait for an idle GPU.

**27.12 live, on an idle GPU (2026-10-20, `tools/water_record_tiles_live.py`; the walled 46 x 28 m bench pond, 24 tiles):**
- **What works:** placed still, all 24 tiles record (the record holds the pond, the cells none) and the record draws it; a touch
  wakes one tile, which records again; **the books exact live - cells + record within 1.8e-4 m^3 of 1,288** (read in ONE call:
  `water_av_census` now returns `record_m3` beside the cells - read apart, tiles recorded between the reads and the first run's
  "1.29e3 off" was that); the ground sentinel stays 0 (new `sentinel_max_fuvw` in the census, gated in the test).
- **Found live and fixed (each a failed run first):** (6) the frozen time was counted in TICKS - live a tick is a frame (~400 fps),
  so "2 s" recorded tiles in 0.3-0.8 s -> counted in seconds; (7) a seeded column's `fPrev` was left at 0 (only stepped bricks
  refresh it; a seed is frozen) - the freeze test read |df| = 1 every call, the column counted as moving and its margin woke its
  neighbours: **one push woke all 24 tiles in under 2 s** -> the seed writes fPrev too; gated: the test's woken-within-0.5 s bound
  (4 = the pushed corner tile + its 3 neighbours) with a red control that skips the fPrev write (8 = every tile); (8) a droplet
  landing on a recorded tile queued its deposit into the GROUND sentinel's cells -> a landing wakes the tile first.
- **Cost:** a tile's record 4-9 ms, its wake 8-19 ms (host; main thread - a one-frame hitch per tile; the per-tile GPU kernels are
  the cut); a push's whole-volume re-upload 72 ms (pre-existing, `markWritten` - logged as its own item).
- **OPEN - after a strong push the pond with recording ON keeps moving and never records again.** Same push (strength 4), recording
  OFF vs ON: kinetic energy OFF 1.76 -> 1.5e-2 (4 s) -> 3.6e-5 (8 s) -> 3e-8 (asleep by ~27 s); ON 1.76 -> 2.9e-3 (4 s) then UP
  to 2-6e-2 over 6-19 s, ~1,300-1,400 columns "moving" at 25 s (`w1b_settle_ke.json`). It is sub-millimetre motion (6e-2 over this
  pond is ~mm/s; the clip `w1b_live_look.mp4` looks calm). Ruled out by measurement: whole-volume wakes (1, at placement), uploads
  and occupancy refreshes (none), the sentinel (0), the record-to-wake surface jump (0.000 mm - the tiles recorded flat), the
  books. NOT reproduced in the test even at the live tick (1/400 s) through the realtime path
  (`DISABLED_DiagEnergyAfterAPushOnTheBenchPond`, `W1B_DIAG_REALTIME=1`): there ON settles and all 24 tiles record again by 19.5 s.
  So the cause is in what the live engine has and the test does not (the real span writer and chunks, the real occupancy, the
  bench's params) - next: bisect it there. Recording stays OFF by default.
- **Bisected live (2026-10-20), the same push, recording ON:**
  - not the live-only features: the record drawn or not, ripples on or off, droplets on or off, all three off - the same
    (1,300-1,400 moving at 18-24 s); not tick jitter, not the partial tile at the box's edge (the test reproduces neither);
  - **WHERE: the moving water fills the tiles round the push and stops at a tile boundary** (unit-map picture, 20 s after: the
    left 4 tiles moving everywhere, the rest quiet) - **the recorded tiles are WALLS and the push's slosh is trapped in the basin
    they leave.** Test: every tile woken just before the push (the pond whole) - the energy falls to 8e-7 by 24 s, as with
    recording OFF; as usual it stays 2-7e-2. Waking a wider ring round the write (`water_av_record_tiles {write_wake_m}`, 8 m,
    16 m; default 2) does NOT fix it (1-4e-2 at 24 s): each tile that opens while the pond still sloshes takes a wall out of a
    moving basin. This is 27.10's predicted risk of candidate (a) - "a wall: waves reflect, wrong for a big lake" - now measured.
  - a second, smaller effect: woken water churns its fills slightly while at rest (|df| median 2.5e-5 a tick against the freeze
    test's 3e-5; the never-recorded pond 2.8e-6) - with the pond whole, 600-700 columns stay "moving" at 8e-7 J and nothing
    records again. The seed's exact level-fill is not the solver's own rest state; it relaxes once woken. Sub-threshold noise,
    but it keeps tiles awake.
  - **So W1c (the patch edge) is needed before recording can be on:** a recorded tile's edge must not reflect - the record's level
    held at the edge with exchange (b), or a damping band (c) - and the seed must be the solver's rest state. Measured next.
  - **CORRECTED by 27.13 (2026-10-21): the walls were not the cause.** A frozen column is a wall too, and recording OFF settles. The
    cause was the ORDER of the tile life cycle: run after the GPU call, a record or wake collected the call just submitted.

### 27.13 W1c (2026-10-21): the agitation was the life cycle's ORDER, not the walls - fixed; recording still OFF by default

**The bisection (live, the 27.12 push, the bench pond; every run against recording OFF, KE at 24-28 s; raw rows
`docs/evidence/water_core_h1/w1c_bisect.json` / `.jsonl`).** Each step a test-only switch, removed after it was measured:
| Variant (recording ON) | KE at 24-28 s | pressure residual |
|---|---|---|
| OFF (the control) | 3e-8 | 0.003-0.03, falling |
| shipped W1b (freed bricks, GROUND wall, seeded) | 2-6e-2 | 2 -> 14, climbing |
| bricks kept (a frozen-brick wall), seeded from the record | 4e-2 | climbing |
| bricks kept, NOT seeded (the exact cells come back) | 6e-2 | climbing |
| ... and no span writes | 3e-2 | 13 |
| the GPU flags only (no spans, body, field, draw, occupancy refresh) | 3e-2 | 11 |
| ... recorded columns not forced frozen / allowed to move / woken ahead 6 or 12 brick columns | 2-4e-2 | climbing |
| ... the flags ignored by the per-column freeze (no tile wakes after the push) | 5e-8 | 0.004 |
| ... the wake WITHOUT its `settle()` | 1e-7 | 0.004 |
| recording ON, never recording (after_s 600) | 5e-8 | as OFF |

Ruled out on the way: the seed (exact cells come back - still agitated), the wall type, the spans and chunk writes, the record
draw, the occupancy refresh, the wake's lag (OFF with frozen columns opening one call late settles: 1e-7), waking ahead, the
partition (a per-call check found no awake brick outside the stepped slots), the ground sentinel (0 throughout), the books
(exact). **The cause: `recordTilesStep` ran at the END of `stepGpu`, after the call was submitted; a record or wake `settle()`s,
which collected the call just submitted.** Live, the pressure solve then stopped converging and the energy climbed 25x over
4-19 s. **The fix: the tile life cycle runs before the call** - where the call in flight is collected anyway (the step's own first
act) - and after a pending upload (a record or wake marks the mirror stale: before the upload it would re-read over a push not yet
sent). A droplet's landing on a recorded tile (stepped after the call) now queues its tile, woken before the next call - its
deposit lands then too.

**Measured after the fix** (Release, idle GPU):
| Run | KE | tiles in the record | books (cells + record) |
|---|---|---|---|
| shipped, run 1 | 1.7e-8 at 41 s, 0 after | 24 of 24 by 41 s, stays | 1,287.999 m^3 (placed 1,288; OFF 1,287.998) |
| shipped, run 2 | 2.6e-8 at 51 s | 5-15, cycling (39 wakes by 51 s) | 1,287.996 m^3 |
| red control: the W1b order (`after_step_test`) | 6.5e-2 at 16 s, 2.2e-4 at 28 s | 0 | exact |
| OFF | 3.3e-8 at 28 s | - | 1,287.998 m^3 |
The ground sentinel 0 in every run. Tests: water unit 299/299; water integration 45/46 (the known GpuFlipBulkParityAndRunup).
`FrozenTilesRecordAndWakeWithTheBooksExact` unchanged (books 6.6e-6 m^3). **No deterministic red test yet:** the integration
test's realtime path (`update()`, async) never reproduced the agitation in either order - the early collect is necessary
(the live bisection) but something only the live engine has (most likely the renderer reading the volume's buffers between
calls) completes it. The red control is live: `water_av_record_tiles {after_step_test: true}`.

**Two items OPEN:**
1. **A collect between two calls breaks the freeze test.** Recording OFF with `{settle_after_test: true}` (a `settle()` right
   after each submission) - or an API census every 0.1 s - settles the energy (3.7e-8) but leaves 500-600 columns "moving" (OFF:
   0-120): the freeze test never passes, so those columns never freeze. Cause not found. Writes still wake tiles at once (a push,
   placed water - API time, between calls) and pay this; their wakes are rare, but the census and `pushSourcesToGpu` settle too.
2. **Woken tiles cycle in some runs** (run 2: ~100 columns "moving" at 1e-8 J, 39 wakes in 51 s, each 8-19 ms of host time).
   A woken pond column's cells differ from the solver's rest by ~1e-6 a cell (0.999999 vs the seed's 1.0, measured on a 1 m
   pond whose level sits on a cell face) - too small alone to explain it. Measured next, with the per-tile kernels (which cut the
   8-19 ms).

**What this changes in the plan:** W1c's edge candidates - fixed-level exchange (b), a damping band (c), waking ahead - are NOT
needed for this pond: with the order fixed, the frozen/recorded wall settles like recording OFF. They stay 27.10's fallback for
when the budget stops a wake from spreading (W3). Recording stays OFF by default until the owner has seen a push in a recording
pond in motion.

### 27.14 The look clip (2026-10-21): a strong push never freezes at quiet scale 3 - recording ON or OFF; three findings

Making the owner's clip of a push in a recording pond (`tools/water_record_tiles_live.py`, the 27.12 push, Release, idle GPU)
showed 27.13's "back in the record by 41 s" was one lucky run. Measured (`tools/water_record_push_settle.py`, 150 s, fresh engine):
| Variant | KE 100-141 s | moving columns | tiles in the record |
|---|---|---|---|
| recording ON, quiet scale 3 (shipped; this run began on 168 m^3 of a prior run's record - finding 3 - the clean 60 s runs agree) | 6e-9 - 8e-8, bouncing | 300-1650, bouncing | 0-1 |
| recording OFF, quiet scale 3 | 3e-9 - 2e-7, bouncing | 290-1430, bouncing | - |
| recording OFF, per-column freeze OFF | 1e-9 - 8e-10, falling | (all) | - |
| recording OFF, rest_ticks 300 / 1200 (5 / 20 s) | ~1e-9 | 900-1660 | - |
| recording ON, quiet scale 10 | 0 from 31 s | 0 from 21 s | 24 of 24 from 21 s, stays; books 1,287.9998 |

1. **The freeze never settles a strong push at quiet scale 3, recording or not** (26.34's default, signed off on pebbles). Every
   stuck column fails the |df| half (KE is below keWake in all of them): 3e-5 a tick is a surface rising ~0.2 mm/s, where the KE
   half at x3 passes ~2.4 mm/s. With the freeze off the waves decay (8e-10 by 130 s); with it on, columns freezing and waking
   keep re-exciting them. A longer quiet run (rest_ticks, a new measurement switch) does not help; quiet scale 10 does. At long
   rest a few isolated columns still spike to 1e-4-3e-4 in one tick (not droplets - none born; not the volume correction -
   toggled; residue drop 0 then): cause not found. **Owner's call:** quiet scale 10 (the pond goes still and records ~10 s after
   the waves; the last sub-mm ripples stop dead) vs 3 (it never records after a strong push).
2. **A visible seam when tiles wake** (both clips, ~10.8-12 s): recorded tiles' edges show as a raised wall/step (a bright bar
   along the far kerb, a step along the left) while the woken water next to them dips. Not investigated yet.
3. **A volume created over recorded water, unseeded, drops the record silently.** The clip harness destroyed a volume whose water
   was in the record (the world keeps it - correct), placed a new pond on top: the census read 2,576 m^3, and when the new tiles
   recorded over the old spans 1,288 m^3 vanished from the books. Not reachable from play today (writes wake, creates seed), but
   it breaks "no silent drop": a create must seed from - or refuse over - the record. The harness runs must start on an empty record.
Clips: `w1c_push_record_on.mp4` (quiet scale 3, clean world), `w1c_push_record_qs10.mp4` (quiet scale 10) + `.json`.

### 27.15 Holes and walls when recorded tiles wake (2026-10-21): three draw defects, fixed

The owner on the 27.14 clips: "water appears to disappear in several spots". Measured, not judged from stills:
`tools/water_clip_gaps.py` (every frame against a still reference, 12 px block chromaticity - the grass under the water sways
per pixel) and `tools/water_wake_holes.py` (the engine's own answer every frame: does ANY field - the volume's or a record
tile's - draw water at each tile's centre, and with `--edges` 0.3 m either side of every tile boundary; `water_av_watch` now
returns drawn + recorded per point, up to 128 points). Prediction for a correct hand-over: 0 hole frames.
| Defect | Cause | Fix |
|---|---|---|
| A: whole woken tiles bare for ~0.4 s (147-222 hole frames; up to 478 blocks a frame on the clip) | a woken tile's columns are frozen, and the partial surface (26.33) keeps a frozen column's last surface - empty while it was recorded | a seed sets `surfaceFullNext`: the next call's surface covers every column |
| B: one bare frame per woken tile (18 with A off) | `wakeTile` drew the seed into the field, but the next read was of the call submitted BEFORE the seed and overwrote it | the seed patch is re-applied until the surface comes from a call submitted after it (`surfCallId > afterCall`, a call counter) |
| C: a wall from the surface to the bed (yellow bed through it, a dark crease on top) beside recorded tiles once the woken water moved | the volume's field covers its whole box: its recorded columns read as DRY, so its mesh walled them off | recorded columns are marked "not this field's" (runs -1, the record's level in top[0]); the mesh ends there as at a box edge and a corner touching one takes the record's level, so the two surfaces meet |
Measured after: 0 hole frames at 124 points over 5,771 frames (`w1c_wake_holes_after.json`; before: 222, `_before.json`); the
clip scan flags only the push's splash and ring (`w1c_wake_fixed3_qs10.mp4`, quiet scale 10). Tests: water unit 312/312, water
integration 45/46 (the known GpuFlipBulkParityAndRunup). No deterministic test pins A-C yet (the probe is live; an integration
test of `drawnAtWorld` over a woken tile is the next step). Recording stays OFF by default; the quiet scale is still the owner's call.

### 27.16 A line sweeping the pond as it goes INTO the record (2026-10-22): defect C the other way, fixed

The owner on `w1c_wake_fixed3_qs10.mp4`: "a weird visual artifact that moves across the water ... around the 2 second mark".
Frames 2.45-2.65 s show a thin dark line moving toward the camera and a yellow speck at the far wall - while the placed pond's
tiles recorded (0 -> 18 -> 24 between 2.33 and 2.92 s). 27.15's probe watched only tiles WAKING (it started once all 24 were
recorded), so it never saw the going-in. Cause: `recordTile` cleared the tile's columns in the volume's field to DRY; the -1
marking (27.15 C) came only at the volume's next step, so meanwhile the live water beside the tile walled it off - and a pond
asleep as a whole never steps again, so the dry columns stayed. Fix: `recordTile` marks them the record's at once (one helper,
`markRecordedColumns`, used by it and by `surfaceFields`).
Measurement added: `water_av_watch` rows end with `dryRecordedColumns()` (recorded-tile columns a volume's field draws as dry;
0 when correct); `tools/water_wake_holes.py --going-in` watches from the placement until every tile is recorded.
| Run | Wall frames | Worst columns | Holes |
|---|---|---|---|
| going in, before (`w1c_going_in_before.json`) | 411 (2.40 s to the end) | 104,328 | 0 after the water is placed |
| going in, after (`w1c_going_in_after.json`) | 0 | 0 | 0 after the water is placed |
| waking, after (`w1c_wake_holes_after2.json`) | 0 | 0 | 0 |
(The 4 hole frames per point at 0.002-0.014 s in the going-in runs are before the water exists.) The re-filmed clip
`w1c_wake_fixed4_qs10.mp4` (same rig, quiet scale 10) shows no line or speck at 2-3 s frame by frame; the mouse pointer is in its
shot (a capture slip, not the water). Tests: water unit 312/312, water integration 45/46 (the known GpuFlipBulkParityAndRunup).
The owner on the re-filmed clip (2026-10-22): "looks good".

### 27.17 The draw hand-over pinned by a test (2026-10-22)

`WaterRecordTilesTest.DrawHandoverHasNoHolesOrWallsEitherWay` (integration, GPU): the 32 x 16 m pond of 27.10 with the record
drawn, driven through the LIVE path (`setRealtime` + `update`, 1/240 s ticks - a frame between a call's submission and its
collection). Every tick: 30 points (tile centres, 0.3 m either side of every inner tile boundary) must be drawn by some field
(else a HOLE tick), and `dryRecordedColumns()` must be 0 (else a WALL tick). Going in (all 8 tiles record), then a touch-wake and
a push beside recorded tiles (7 wakes). Red controls, one fix undone each (`WaterCoreManager::s_handoverRevertForTest`):
| Undone | Hole ticks | Wall ticks |
|---|---|---|
| nothing (shipped) | 0 | 0 |
| 27.16 recordTile clears to dry | 0 | 1,378 |
| A no full surface after a seed | 0 | 0 |
| B no seed patch | 1 | 0 |
| C recorded columns unmarked | 0 | 1,680 |
| A + B | 665 | 0 |
A alone is hidden by B's seed patch (it writes the seed into the field and a partial surface read keeps a frozen column's value);
A + B reproduces the bare tiles seen live. Both fixes stay. Through `step()` (W1C_HANDOVER_STEP=1) only C shows - a step refreshes
the field every tick, so the hand-over gaps live only on the frame path; the test defaults to it.

### 27.18 A create over recorded water: refused unseeded (2026-10-22) - 27.14 finding 3 closed

Only `water_av_create` with `seed:"none"` (the default) reached it (the flood create always seeds). Measured live,
`tools/water_create_over_record.py` (the 27.12 pond recorded, its volume destroyed - the record keeps the water - then a create
over the same box): before, the unseeded create was accepted, a pond placed into it booked **2,576 m^3** (cells + record) and when
it recorded the record read 1,288 - one copy gone without a word (`w1c_create_over_record_before.json`). Now an unseeded create
over recorded water is **refused**, naming the m^3 (`recorded_m3`, `WaterCoreManager::recordedWaterIn`); `seed:"spans"` takes the
record into the volume - seeded 1,288.0000, every tile recorded again in 2.6 s, the record within 3e-5 m^3 of what was placed at
every stage (`_after.json`). Refused rather than seeded silently: the caller asked for an empty volume; the error says how to get
the water instead. Not changed: a `seed:"spans"` volume still leaves the spans in place while awake (Phase D - the world keeps its
water if the game stops), so a census of one reads cells + spans double until it writes back; the recording path is exact.
Tests: water unit 312/312, integration 46/47 (the known GpuFlipBulkParityAndRunup).

### 27.19 Where a tile's record / wake call spends its time (2026-10-22): measured, two whole-volume costs cut

The plan's next step was the per-tile GPU kernels (27.10: cells -> runs on the GPU). Measured first - `water_av_record_tiles`
now echoes `phases` (each call split; `phases_reset:true`), live on the bench push (46 x 28 m, quiet scale 10): computing the
runs is 0.3 ms. The time was elsewhere:
| Call | Phase | Before (last / max ms) | After |
|---|---|---|---|
| wake | the GPU seed upload (`seedColumns`) | 8.9 / 10.3 | 2.4-5.6 (settle 0-0.8, host 1.7-4.0, submit 0.4-1.0) |
| record | the record tile drawn (`buildRecordTilesNow`) | 4.9 / 7.1 | 2.5-5.8 |
| record | the tile's cells read | 1.5 / 4.5 | 0.6-1.4 (one 3.7: a slow submit) |
Cuts (no behaviour change - the record-tile tests, the hand-over test and the live books unchanged): `seedColumns` built a dense
fill of the WHOLE volume (1.9 M floats, zeroed, walked twice) for one tile - now over the tile's cells only; its touched-brick list
did a linear find per wet cell - now sort + unique. The record tile's occupancy came from 140 k `m_state` calls - inside a 1/9 m
volume's box it now reads the volume's occupancy cache (the same lookup, cached; only while current). Live: wake max 11.4 -> 6.6
ms, books 2.5e-4 of 1,288 m^3, 24/24 tiles back in the record (`w1c_phases.json` before, `w1c_phases_split.json` after).
**Still over the 2 ms budget**, and now per cell, not per volume: the seed's fills (0.7-1.8 ms, a storage index per cell), the
fresh-brick uploads (0.5-1.4), the record tile's occupancy rows outside the volume's box (1.5-4.4: the ground row below and air
above, 46 k `m_state` calls) and a GPU read that sometimes waits 2-4 ms. Those are what the per-tile kernels (wc_seed.comp
filling from uploaded runs) and a bulk occupancy read would cut - re-scoped by this measurement.

---

### 27.20 The limits pass: ponds and creeks on today's code (2026-10-23). Status: MEASURED

**Why (owner, 2026-10-23):** the ladder is ponds, then creeks, then lakes, rivers, shores and oceans, each step knowing where the
one before it stops - "do the limits pass on ponds and creeks". The 26.29 ladder came before the surface tiles and view culling
(26.30), the partial wake (26.31-26.33), quiet scale x3 and 20 sweeps (26.34-26.35), so it is re-measured here; creeks had never
been put on a ladder.

**The rigs** (all on WaterBench_Creek, Release, one engine, GPU idle - `nvidia-smi` 2 % before; auto-sleep OFF; engine defaults
otherwise, recording OFF as shipped). "Water ms" = the solver's GPU time (`gpu_device_ms`, median) + the `WaterSurface` pass
(median over 240 frames). The no-water GPU frame is 1.3-3.8 ms depending on the pose.
- *Pond size*: `tools/water_pond_ladder.py` unchanged (walled boxes, a stone at the centre). Evidence: `pond_ladder_2026-10-23.json`.
- *Pond count*: `tools/water_pond_count.py` (new) - N ponds of 5 x 5 m, 0.3 m deep, each its own volume, the camera seeing all of
  them; asleep, one stirred, all stirred. Evidence: `pond_count.json`.
- *Creeks*: `tools/water_creek_limits.py` (new) - from dry, a spring at the head, an outfall at the foot, Manning n 0.035, polled
  until the outflow is within 3 % of the inflow (or 480 s), then a 15 s steady window. Three creeks: the bench creek (100 m, 2 m
  wide, 1:200) and two carved beside it by `--carve`: steep (60 m, 2 m, 1:20) and wide (60 m, 6 m, 1:200). Ladders: length 25 /
  50 / 100 m at 0.2 m^3/s; flow 0.05-1.6 m^3/s on each creek. Evidence: `creek_limits.json` (bench, steep, length),
  `creek_limits_wide.json`. The camera sits beside the head, 2.5 m past the far bank.
- *A harness fault found and fixed during the run:* a GPU volume holds at most **64 sources** (`WaterCoreGpu::kMaxSources`). The
  wide creek needs 54 outfall + 54 spring cells; the engine refused 44 springs with an error the tool did not read, so that first
  run got 19 % of its flow (discarded; still in `creek_limits.json`, out/in 0.18). The tool now checks every reply and spreads 10
  springs over a wide head; the wide creek was re-run.

**Ponds - one pond** (water ms after a stone, median, max in brackets where it passes 2; asleep it is the surface alone):

| depth | 5 x 5 | 10 x 10 | 15 x 15 | 20 x 20 | 25 x 25 | 32 x 28 | 46 x 28 |
|---|---|---|---|---|---|---|---|
| 0.3 m | 0.58 | 0.74 | 0.93 | 1.02 | 1.13 | 1.30 (2.1) | 1.46 (2.1) |
| 1 m | 0.62 | 0.83 | 1.12 | 1.48 | 1.95 (2.8) | 2.30 (2.9) | 2.68 (3.9) |
| 2 m | 0.68 | 0.99 | 1.41 (2.3) | 1.99 (2.2) | 2.85 (4.4) | 3.67 (5.2) | refused (cap) |
| asleep, any depth | 0.03 | 0.06 | 0.10-0.13 | 0.16-0.21 | 0.25-0.33 | 0.35-0.50 | 0.47-0.51 |

1. **One awake pond now fits 2 ms to the volume cap at 0.3 m deep, to ~25 x 25 m at 1 m and ~20 x 20 m at 2 m** (26.29: ~10 x 10 m
   at every depth). The peaks just after the stone pass 2 ms from ~15 x 15 m at 2 m and ~25 x 25 m at 1 m.
2. **A sleeping pond costs ~0.37 us per m^2 of drawn surface** (26.29: 8.8 us - 24x less); the largest pond asleep is 0.5 ms. A
   sleeping pond is now limited by the volume cap, not the budget.
3. **The hard ceiling is the volume cap, 2 M cells** - 46 x 28 m fits at 1 m deep, not at 2 m (~900 m^2 at 2 m).
4. **Deep, large ponds still do not fall asleep after one stone**: at 1 m and 2 m, 25 x 25 m and larger were awake 36 s later (the
   walled box reflects its waves; quiet scale x3). 0.3 m ponds slept in 9-12 s; 1 m 15-20 m in 31-33 s; 2 m up to 20 m in 10-14 s.

**Ponds - many ponds** (5 x 5 m, 0.3 m):

| ponds | water ms, all asleep | one stirred | all stirred | fps asleep (no water: 496) |
|---|---|---|---|---|
| 1 | 0.01 | 0.56 | 0.56 | 601 |
| 4 | 0.03 | 0.58 | 1.65 | 472 |
| 16 | 0.05 | 0.40 | 6.4 | 316 |
| 32 | 0.08 | 0.43 | 12.7 | 210 |
| 64 | 0.20 | 0.66 | 24.5 | 131 |

5. **Each awake pond costs ~0.35-0.55 ms whatever its size** - the call's own cost. So **~4 small ponds awake at once** fit 2 ms,
   and one big awake pond is cheaper than four small ones.
6. **Sleeping ponds cost the GPU almost nothing, but the frame rate falls with their number** - 601 fps with one, 131 with 64,
   while the GPU frame stays 1.3-1.8 ms: the frame is CPU-bound, ~0.09 ms of CPU per sleeping volume per frame. Cause NOT measured
   (per-volume host work every frame). This is the first limit for a world with many small ponds.

**Creeks** (steady flow; water ms median, max in brackets; depth and surface speed mid-reach, Manning's prediction in brackets -
its speed is the section mean, which a surface speed should exceed):

| creek | q m^3/s | water ms | depth m | surface speed m/s | dry to steady | channel |
|---|---|---|---|---|---|---|
| bench 100 m, 2 m, 1:200 | 0.05 | 1.67 (2.3) | 0.10 (0.07) | 0.26 (0.35) | 444 s | in banks |
| | 0.2 | 1.68 (2.4) | 0.20 (0.17) | 0.67 (0.61) | 240 s | in banks |
| | 0.4 | 1.50 (2.1) | 0.28 (0.25) | 0.85 (0.80) | 192 s | in banks |
| | 0.8 | 1.71 (2.6) | 0.42 (0.38) | 1.10 (1.06) | 187 s | over the bank |
| | 1.6 | 2.00 (3.3) | 0.62 (0.57) | 1.17 (1.39) | 370 s | over the bank |
| steep 60 m, 2 m, 1:20 | 0.05 | 1.62 (2.2) | 0.07 (0.04) | 0.40 (0.70) | 176 s | in banks |
| | 0.4 | 1.67 (2.0) | 0.19 (0.13) | 1.41 (1.60) | 69 s | in banks |
| | 1.6 | 1.97 (2.5) | 0.38 (0.29) | 2.72 (2.78) | 44 s | over the bank |
| wide 60 m, 6 m, 1:200 | 0.1 | 3.27 (3.9) | 0.09 (0.06) | 0.19 (0.30) | 373 s | in banks |
| | 0.4 | 3.17 (4.6) | 0.17 (0.13) | 0.52 (0.52) | 182 s | in banks |
| | 1.6 | 2.67 (4.1) | 0.34 (0.30) | 0.91 (0.90) | 119 s | in banks |

Length (bench creek, 0.2 m^3/s): 25 m 1.35 ms, 50 m 1.28 ms, 100 m 1.67 ms (dry to steady 92 / 142 / 240 s). Every rung is in the
evidence files.

7. **A 2 m creek fits 2 ms at every flow tried, up to 100 m long and 2.7 m/s** - the cost is mostly the call's own (~1 ms solver +
   ~0.5 ms surface) and grows slowly with length (25 -> 100 m: +0.3 ms). It reaches 2 ms at 1.6 m^3/s, where the bench channels
   overflow (they are cut 1/3-0.9 m deep - a bench limit, not the engine's).
8. **A 6 m creek does not fit: 2.7-3.5 ms** at every flow - the solver 1.5-2.0 ms and the surface 1.5-1.7 ms (thin water across
   6 m costs the surface pass ~3x the 2 m creek's; at 1.6 m^3/s, deeper, it falls to 0.6 ms - cause not measured).
9. **The longest creek one volume holds is ~100-110 m** (2 m wide, 1:200): the box must span the drop, so the 2 M-cell cap binds
   sooner on steeper or wider creeks (60 m at 1:20 is 1.84 M cells; 60 m x 6 m is 1.75 M).
10. **Shallow water runs deeper and slower than Manning** - up to one 1/9 m cell deeper and 30-45 % slower below ~0.15 m (steep at
    0.05 m^3/s: 0.07 m against 0.04, 0.40 m/s at the surface against a 0.70 mean). From ~0.2 m deep it agrees within 10-20 %
    (speeds at 0.4-1.6 m^3/s within 2-10 %). Water under ~2 cells deep is at the grid's resolution floor.
11. **The books are exact in every run** (stored - (in - out) within 2e-5 m^3); every run took 1-2 substeps.
12. **A creek from dry takes 44-444 s to run steady** - a world creek cannot start dry in front of the player (W4 will need creeks
    placed at their running depth).
13. **A volume holds at most 64 sources** - a creek wider than ~3.5 m at 1/9 m cannot have a spring and an outfall on every cell.

**What this says for the next steps.** The limits, in the order they bite: (a) **the call's own cost** - ~0.4-0.5 ms per awake
pond and ~1.5 ms per creek whatever the size - so the number of awake bodies, not their size, spends the budget; (b) **the CPU per
sleeping volume** (finding 6); (c) **the 2 M-cell cap per volume** - one pond ~1,300 m^2 at 1 m, one creek ~100 m; (d) **wide
shallow water's surface cost** (finding 8); (e) **deep ponds that do not settle** (finding 4 - the quiet-scale decision). Lakes
(W1d / W1e) remove (c); (a) and (b) decide how many bodies a world can have.

### 27.21 The costs nobody sees (2026-10-23): where the time goes, and the sleeping-pond frame fixed. Status: BUILT (cost only), not signed off

**Why (owner, 2026-10-23):** "the simulation aspect of water should be driving function and appearance, but water under the surface of
a pond is basically not seen ... maybe there is some work to be done still to be able to avoid performance bottlenecks on simulation
algorithms that cant be seen" - then "we need to nail down some of these performance issues first". Measured before anything changed:
`tools/water_cost_split.py` (new) on WaterBench_Creek, Release, GPU idle. Evidence: `cost_split.json`, `debris_tiles_check.json`.

**1. Depth is the solver's cost, stage by stage** (a 32 x 28 m walled pond, 4 s after a stone; `water_av_kernel_timing`, ms per call -
the stamps add a barrier each, so totals read a little high):

| partial wake | depth | solver | sweeps | transport | surface copy | extrapolate | reductions | velocity advect | forces |
|---|---|---|---|---|---|---|---|---|---|
| ON (shipped) | 0.3 m | 0.89 | 0.26 | 0.07 | 0.10 | 0.12 | 0.12 | 0.03 | 0.03 |
| | 1 m | 1.62 | 0.40 | 0.17 | 0.24 | 0.14 | 0.13 | 0.10 | 0.07 |
| | 2 m | 2.73 | 0.78 | 0.36 | 0.30 | 0.25 | 0.19 | 0.15 | 0.10 |
| OFF | 0.3 m | 1.63 | 0.43 | 0.17 | 0.44 | 0.17 | 0.09 | 0.11 | 0.09 |
| | 1 m | 2.32 | 0.64 | 0.26 | 0.44 | 0.23 | 0.13 | 0.19 | 0.09 |
| | 2 m | 3.50 | 1.10 | 0.52 | 0.43 | 0.37 | 0.14 | 0.22 | 0.15 |

The drawn surface does not grow with depth (27.20: the same at every depth). The stages that do - sweeps, transport, extrapolation,
advection, forces - are per-cell work on cells under the surface: at 2 m ~1.7 of the 2.73 ms. The deep water must move (it carries
the wave and sets its speed), but nothing in it is seen at 1/9 m. The candidate is tall cells below a fine surface band (Irving et
al. 2006; Chentanez & Mueller 2011 on the GPU) - same water, same rules, coarser below the band; it changes how the water is
stored, so it is a design for the owner (next).

**2. The sleeping-pond frame (27.20 #6) was not the water - it was the debris water map.** The update scopes (`/api/debug/frame_pacing`,
new scopes 'API Commands', 'Debris Begin', 'Dirty Chunks', 'Physics Step', 'Debris Feeds') put 5.8 of the 7.4 ms frame of 64 sleeping
ponds in `DebrisRuntime::beginFrame`. Its water map (the surface debris floats on, one 32 x 32 tile per chunk column) rebuilt EVERY
frame (a) every tile under any volume box - asleep or not - and (b) every tile in the old water's (WaterManager) camera-following region
- empty here (0 m^3) but still rebuilt - and each rebuilt column asked every volume's field in turn (64 fields x 1,024 columns x
10-13 tiles). Fixed, cost only:
- a live tile keeps its cells while its **signature** stands: `WaterCoreManager::fieldsSignature` = the (source, version) of every
  field owning a column there (the version the surface tiler already trusts - an asleep volume's stands; 0 = unknown: rebuilt),
  valid outside the old water's region or while that region holds no water (`WaterManager::anyWet` false - every column there reads
  dry); otherwise rebuilt every frame as before;
- a tile with no field over it reads the old water alone, not every field first.
- The coupling switch is in the signature (off reads dry).

| sleeping 5 x 5 m ponds | frame before | frame after | Debris Begin before | after | fps before | after |
|---|---|---|---|---|---|---|
| 0 | 1.61 ms | 1.49 ms | 0.61 | 0.15 | 703 | 697 |
| 1 | 1.81 | 1.54 | 0.69 | 0.19 | 552 | 651 |
| 16 | 3.01 | 1.58 | 1.83 | 0.18 | 268 | 589 |
| 64 | 7.08 | 1.64 | 5.52 | 0.24 | 116 | 587 |

**The gate** (`tools/water_debris_tiles_check.py`, `water_av_debris_tiles {check}`): every kept tile is ALSO rebuilt fresh and compared
byte for byte, over 16 ponds with wood floats dropped into 4, stones in 4 others, 2 destroyed and 1 created: **29,931 kept tiles, 0
different**. **Red control** (`{red: true}`, TEST ONLY - the signature blind to the versions): 33,473 checked, **5,273 different** -
the check sees stale water when there is some. Tests: water unit 312/312; the full unit suite 4,283 passed, 3 failed -
`WaterCouplingTest.BlastBesideThePondMovesItAndItSettles` only in the full run (passes alone and in the water set: order-dependent, it
does not use the debris map), `AtlasManagerTest.BuildAtlasFromSourcePNGs`, `FineFaceMerge.SubcubeMerge_CrossCubeSplitsOnLightBoundary
BetweenCubes`; integration 136/138 - the known `GpuFlipBulkParityAndRunup` and `SceneIntegrationTest.AddSceneThenTransitionToIt`
(fails alone too; the scene system, untouched here). Nothing about how water moves or looks changed.

**3. Left, by size** (the 27.20 order): the call's own cost per awake body (~0.35-0.55 ms: 4 awake ponds = 1.65 ms) - the reductions,
the surface copy, the launches, one call per volume; the depth (1 above); wide shallow water's surface pass (27.20 #8).

### 27.22 Tall cells - the water under the band, one cell per column (2026-10-23). Status: DESIGN

**The ask (owner, 2026-10-23):** "do it" - after 27.21: depth costs the solver per cell, and the cells under the surface are not seen.

**In plain words.** Today every 1/9 m cell of a pond is stored and solved, from the surface to the bed. With tall cells, only a
BAND of 1/9 m cells under the surface is kept as it is. Below the band's floor, each 1/9 m column of water is ONE cell - a tall
cell - from the bed up to the floor. It holds full water (nothing in it is ever air), its pressure grows with depth the way still
water's does, and it moves sideways as one piece (one velocity on each of its four sides, the same at every height). It is the same
water and the same rules, stored coarser where nobody looks - the vertical version of the partial wake.

**Why the motion stays right.** A wave's motion reaches down about half its wavelength. Short waves (ripples, a stone's rings,
splashes: wavelength under ~1 m) barely touch the water below a 0.5 m band - they live entirely in the band, as today. Long waves (a
whole-pond slosh, a seiche) move the whole depth, but for them the water moves sideways almost uniformly with depth and the pressure
is hydrostatic - exactly what a tall cell holds. That split is the shallow-water theory's own assumption for long waves, so the
slosh period and the long-wave speed (sqrt(g x depth)) stay right because the full depth is still there. What a tall cell cannot
show is detail below the band: a stone sinking past the floor or an underwater blast moves coarser water there. References: Irving,
Guendelman, Losasso & Fedkiw 2006 (tall cells under a uniform band); Chentanez & Mueller 2011 (the restricted tall cell grid, real
time on the GPU) - we take their split and keep our own fine-cell solver above it.

**What it saves (predicted from 27.21's stage split, not yet measured).** At 2 m deep ~1.8 ms of the solver's 2.73 ms is per-cell
stages; with a 4-cell (0.44 m) band the stepped layers fall from ~22 to ~9:

| depth | today (measured, 32 x 28 m, after a stone) | with tall cells (predicted) |
|---|---|---|
| 1 m | 1.62 ms | ~1.4 ms |
| 2 m | 2.73 ms | ~1.7 ms |
| 5 m (a lake; today refused by the box cap at this size) | ~6 ms (extrapolated) | ~1.8 ms |

It matters little for 1 m ponds and most for lakes - deep water is what the lake step (W1d / W1e) is made of. The bricks below the
floor are never stored, so device memory falls the same way.

**The design.**
1. *The floor - one per volume.* A world height `floorY` (a brick boundary, so on the 2/9 m grid of flat bricks). Columns whose bed
   is below the floor have a tall cell from the bed to the floor and fine cells above it; columns whose bed is at or above it are fine
   all the way, as today. One floor per volume means a tall cell's sides only ever meet another tall cell or the ground, never a fine
   cell - the stencils stay simple. It suits ponds and lakes (a flat surface); a creek or river with a sloping surface keeps no tall
   cells (it is shallow anyway; rivers at scale, W5, would need a floor per region - stated, not built).
2. *The band - the owner's choice - and the floor from world state (design-check item 1).* `floorY` = the body's REST LEVEL minus
   the band B, rounded down to the volume's brick boundary (world-aligned; 2/9 m for the default flat bricks). The rest level is the
   water's own: the body record's `level` (`WaterBodyRecord`, world Y) when the volume has one, else the level its water came to rest
   at (its still surface at creation or its last sleep). NOT the volume's lowest surface: that depends on the volume's extent and on
   which tiles are awake - a cost unit - and would make the deep water behave differently for the same pond simulated as a whole or
   as a patch. The floor moves DOWN only, during a call, when a trough comes within one brick of B above it (a big slosh, a drain);
   it returns up to the rest rule when the water sleeps. Moving down only makes more of the water fine - toward the reference, never
   away. Proposed default B = 0.5 m; the clip shows 0.44 / 0.67 / 0.89 m (whole flat bricks) against all-fine for the owner to pick.
3. *What a tall cell stores* (per 1/9 m column, 64 per brick column, in a column-indexed buffer): its bed height, its pressure at the
   top (P_T, at the centre of the 1/9 m cell just under the floor - so the fine cell above sees it as an ordinary neighbour), its
   four side velocities (one u on its x face, one w on its z face - lower faces as the lattices store them; the upper ones are the
   neighbours'), its water volume V (exactly H x h^2 when full - the books), and its kinetic energy for the sleep test. The vertical
   velocity at its top face is the fine v lattice's face at the floor; at the bed it is 0.
4. *Pressure.* Inside a tall cell p(y) = P_T + g (y_T - y). Its row in the Poisson system is the divergence of its volume: the top
   face to the fine cell above (gradient (p_above - P_T) / h, as any fine face) plus four side faces, each open over the overlap of
   the two columns' water below the floor (max of the beds up to the floor), the pressure difference across it P_TA - P_TB (both
   tall, same floor: the hydrostatic parts cancel) over h. The tall cells join the red-black sweeps (colour by column parity, after
   the fine colours) - a symmetric, diagonally dominant row, so the sweeps converge as they do now; the still-water first guess
   (26.9) continues down through P_T.
5. *Velocity.* The side velocities are updated by the pressure difference (as the fine faces), advected across the tall layer as
   depth-averaged velocity (semi-Lagrangian in x-z), damped by the pond friction / Manning at the bed like the fine faces. The fine
   cell above a tall cell reads the tall cell's side velocity as its "cell below" value in its own advection.
6. *Water - the books exact.* A tall cell is always full. Its volume changes only through its faces (side fluxes, top flux); any
   mismatch with H x h^2 (the projection is not exact to the last bit) is carried into the fine cell above it, the way an over-full
   fine cell's excess is carried up today (26.5) - so the total stays exact.
7. *Regrid, per call.* Down (item 2): the top brick of every tall cell becomes fine cells (f = 1, velocity = the tall cell's side
   velocity, pressure hydrostatic from P_T). Up (only while the water sleeps or after it has, back to the rest rule; and only if every
   fine cell of that brick layer in the tall columns is full): their fills and depth-averaged velocities go into the tall cells, the
   fills' remainder carried up. The rest rule changes only with the rest level, so the floor does not chatter.
8. *Obstructions lower the floor - never a fine column among tall ones (design-check item 2).* A column fine to the bed inside the
   tall region would put fine cells against a tall cell's side - a second kind of stencil. Instead the volume's floor goes below
   every obstruction, for as long as it is there: a solid cell between a column's bed and the floor (an overhang, a rock arch, a
   cave roof) -> the floor below its lowest cell; a source or outfall -> below its cell; a moving solid (debris, a stone, a body)
   -> below its lowest cell minus one brick (20's coupling needs cells round it). An obstruction near the bed leaves no tall cells
   at all - the cost of today, the behaviour exact. The volume's first call is all fine, then regridded. A blast below the floor
   goes into the tall cells' side velocities (coarser; stated).
9. *Everything that reads cells.* The surface field (the column's runs: the tall cell is the bottom of the lowest run, joined to the
   fine run above; nothing drawn changes - the surface is in the band); the record (readColumns writes a tall cell as that run;
   seedColumns, waking a recorded tile (27.12), builds its tall cells straight from the record's spans under the floor instead of
   fine cells); the ripples and the debris map (they read the surface); the partial wake (a tall cell freezes with its brick column;
   its side velocities' kinetic energy and its top face's |dV| go into the unit map the freeze test reads, design-check item 4 -
   otherwise a sloshing deep layer under a quiet band would freeze); the reductions (mass includes V, kinetic energy the side
   velocities); the residency (no brick is stored below the floor in a tall column). The 2 M-cell box cap counts the box, not the
   stored cells - tall cells do not raise it (W1d does).
10. *API (metres, world Y).* `water_av_create {tall_band_m}` (omitted = the engine default; 0 = off, all fine); `water_av_tall
    {default}` sets the engine default (for A/B runs, as `water_av_partial_wake {default}`) and echoes it. `water_av_list` echoes
    `tall_band_m`, `floor_y`, `tall_columns`, `tall_m3`, `regrids`, `floor_reason` (rest / trough / obstruction). Clamp at entry:
    B = 0 (off) or 2 bricks (4/9 m) to 8 m - a thinner band puts the floor inside a stone's own ring (its motion reaches ~0.3 m),
    above 8 m nothing would be tall in any pond; written at the clamp. The default stays OFF in this design: no pinned test moves.
    Turning it ON is the owner's call after the clip, and then every GPU water test runs with it (most rigs are shallow and make no
    tall cells; the deep rows - the 26.29 ladder rigs, the slosh rows - are re-pinned in that commit).

**Tests, red first.**
- `WaterTallCellTest.OffIsBitIdentical` - tall_band 0 runs bit for bit as today (every GPU water test unchanged).
- `StillWaterStaysStill` - a 2 m pond with tall cells at rest: velocity 0, surface flat to 0.1 mm, pressure hydrostatic to 1 %.
- `TheBooksAreExact` - every tick through a slosh, a stone, a pump in the band and a drain that regrids the floor down twice: stored
  (fine + tall) - (in - out) within the float rounding of today's runs.
- `LongWaveMatchesAllFine` - a whole-pond slosh (24 x 8 m, 1 m and 2 m deep): the period within 5 % and the amplitude after 3
  periods within 10 % of the all-fine run. RED control: the tall layer frozen as a wall (a false bottom at the floor) - the period
  ~1.4-2x long, outside the gate (the control that the test sees depth).
- `ShortWaveMatchesAllFine` - a stone in a 2 m pond: the ring height at 1 m and 3 m within 10 % of all-fine at B = 0.5 m.
- `RegridIsExact` - the floor moved down and up by hand: the fields above it unchanged at the regrid, the books exact, and the
  surface over the next 2 s within 1 mm of a run that did not regrid (a regrid must not show as a twitch).
- `TallCellsAcrossChunkSeam` (the chunk-independence shape, design-check item 3) - the same pond placed inside one chunk and
  translated by exactly one chunk (32 m = 36 bricks: the same brick alignment) so it straddles a seam: every cell's f, u, v, w and
  every tall cell's V, P_T and side velocities bit-identical between the two runs, every tick.
- `TheFloorIsTheWaters` (design-check item 1) - one pond as one volume vs the same pond with its far half frozen (the partial wake)
  and vs inside a volume twice its size: the same floor_y; the surface within the partial wake's own measured floor (26.31).
- `ObstructionsLowerTheFloor` - a rock arch, a pump and a sinking stone below the floor: floor_reason = obstruction, the floor below
  each, no fine column among tall cells (the kernels assert it - a counter that must stay 0).
- L4 (live, WaterBench_Creek, design-check item 7): the pond 24 x 24 m at x 4-27, z 2-25 (inside the chunk at x 0-31, z 0-31), 1 m
  and 2 m deep, a stone at the centre - `tools/water_cost_split.py --depth` with tall cells off and on, INTERLEAVED on the same water
  (separate runs drift ~0.2 ms). Prediction written before the run: 2 m 2.4 -> 1.6 ms (+-0.3), 1 m 1.5 -> 1.3 ms. Control: tall
  cells on with B larger than the depth - must cost what off costs (the floor below the bed: no tall cells; proves the saving is the
  tall cells, not the run). Then the owner's clip: all-fine vs B = 0.44 / 0.67 / 0.89 m side by side (a stone, a push, floating wood).

**The rig vs shipped:** off by default; the integration rigs are walled boxes (no shore, so long waves reflect and a 2 m box does not
sleep - 27.20 #4) at 1 m and 2 m, 1/9 m, the engine's other defaults (partial wake on, 20 sweeps, quiet scale 3). A real pond's
sloped shore damps the slosh, so the walled rows are the hard case for the long-wave gate.

**Build order** (each step committed, the gates above red first): TC1 the storage and the floor with no tall cells made (bit-
identical); TC2 still water in tall cells (hydrostatics, books); TC3 the dynamics (pressure, side velocities, advection, regrid) and
the wave gates; TC4 everything that reads cells (record, surface, partial wake, reductions, residency); TC5 the cost, measured, and
the owner's clip.

### 27.22a Design-check of tall cells (Feature Design Keys, 2026-10-23) - NEEDS WORK -> seven items folded in above -> READY

1. **Voxel aesthetic.** Nothing drawn changes: the surface, the ripples, the foam, the floats all read the band. Water stays
   1/9 m cells where it is seen; the tall cell is storage below the band (like the bricks, 26.6). Off by default, so no look moves
   until the owner picks B from the clip. The detail lost - motion under the band - is stated (a sinking stone lowers the floor
   under itself, item 8, so even that keeps cells round it).
2. **Chunk independence.** Quantities derived from a chunk: none. Bricks and tall cells are world-aligned (a brick boundary is a
   chunk boundary, 26.6). The one cost-unit quantity was the FLOOR: the first draft took it from the volume's lowest surface - a
   function of the volume's extent and of which tiles are awake, which would change how the same deep water moves (the "per-chunk
   decides behaviour" mistake, with a volume for a chunk). **Fixed (item 1):** the floor comes from the water's rest level (the body
   record), moving down only for troughs and obstructions, i.e. only toward the all-fine reference. No cross-chunk lookup. Equality
   tests named: `TallCellsAcrossChunkSeam` (translated by one chunk, bit-identical) and `TheFloorIsTheWaters` (whole vs frozen half
   vs larger volume: the same floor).
3. **Procedural generation.** No stage: tall cells are how the simulation stores water, not what the world holds. It consumes the
   ground (occupancy) and the body records the bake / write-back already make; it changes nothing upstream. W4 (generated lakes as
   records) will WAKE into tall cells through seedColumns (item 9) - the record stays the source of truth. No per-world tuning: B
   is an engine setting like quiet_scale, not world content.
4. **API.** Metres and world Y; omitted = the engine default; `water_av_tall {default}` for A/B; every state echoed in
   `water_av_list` including WHY the floor is where it is (`floor_reason`); the clamp [4/9 m, 8 m] or 0 with the reason at the
   site. Default unchanged (OFF) - no pin moves now; the ON commit re-pins the deep rows (item 10). **Fixed:** the first draft had no
   engine-default knob, no reason echo and no upper clamp.
5. **Visual test plan.** "Works" = (a) still water flat to 0.1 mm, hydrostatic to 1 %; (b) the books exact every tick; (c) a slosh's
   period within 5 % and amplitude within 10 % of all-fine; (d) a stone's ring within 10 % at 1 m and 3 m; (e) a regrid invisible
   (1 mm); (f) the 2 m pond's solver time down by the predicted ~0.8 ms; (g) the owner's clip. Depth: L3 (integration rigs on the
   GPU, all-fine as the oracle - the same code, so a difference is the tall cells') and L4 (live, interleaved). Red tests: the
   false-bottom control (reports the slosh period off by ~1.4-2x), `{red}` controls for the seam and floor tests (a floor taken
   from the volume's lowest surface must FAIL `TheFloorIsTheWaters`). Test world: rigs inside one chunk (24 x 8 m slosh box, 24 x
   24 m pond at x 4-27, z 2-25); one variable per row (B, or depth); predictions written in the plan (the cost table, the period);
   controls stated. Rig vs shipped: walled boxes (no shore) - the hard case. **Fixed:** the first draft's L4 pond (32 x 28 m from
   x 2) crossed the chunk at x 32, had no written prediction and no control; the regrid had no visibility gate; the freeze test did
   not see the tall layer's motion (item 9).

**Verdict: READY** for TC1 (with the seven items above). The decision left to the owner: B, from the clip at TC5.

### 27.22b Tall cells TC1-TC3 built (2026-10-23) - the ledger. Status: BUILT, OFF by default, not signed off

**TC1** (`848e76da`): the push block packs three flags into one int so the tall layer's `tallY` / `tallOff` fit the guaranteed 128
bytes; one height per dense column in the Bricks buffer; `tallH` / `cellVolume` (1 everywhere while off). The GPU water suite 46/47 as
before, the bit-identical rows included.

**The representation actually built - simpler than 27.22's.** A tall cell is the ORDINARY cell at the tall layer (the bottom cell of the
floor's brick), stretched down to its bed: its `f` holds its water as a VOLUME in cells (0..H), so every flow, sum and two-sum stays
additive and the books stay exact; the bricks under the tall layer read the existing GROUND sentinel. A kernel needs the height only
where a cell's size enters: the pressure row (side faces weigh their open overlap in cells), the transport (a tall donor's fraction is
f / H, a side face carries its overlap, a tall cell holds H), compaction, the surface (the run starts at the bed) and the velocity
advection (nothing is sampled under the floor; the tall layer advects sideways only). With H = 1 every one of these is the old
arithmetic exactly (x 1.0, / 1.0). `wc_tall.comp` COLLAPSES a column into its tall cell (exact, the remainders into fLo) in the call's
prologue, before the residency maps the bricks below to ground, and EXPANDS it back after the residency stores them again.
`WaterCoreGpu::setTall / clearTall`; download spreads tall cells back into cells for the host's dense grid (write-back, census and
the CPU surface fallback see ordinary water); upload resets them; readColumns / record tiles refuse a tall volume (TC4).

**TC3 - three tries at the pressure, measured (`WaterTallCellTest`, `DISABLED_DiagStoneCallByCall`):**
1. *Tall cells as rows of the same solve, nothing else changed:* matches all-fine for a second, then a pond-wide "breathing" grows
   (KE x300 by 3 s). The tall layer couples sideways 13x as strongly as up - a 2-D problem under the band that the band's 20 sweeps do
   not converge; the unconverged divergence became volume errors the pressure-based refill rules (26.21 volume correction,
   compaction) fed back.
2. *Hydrostatic tall cells, not unknowns (explicit, the floor velocity from continuity):* with the band's solved pressure - blew up
   in ONE call (the band's dynamic push to follow the floor fed back: the classic added-mass instability); with the columns'
   hydrostatic pressure - still water exact, but the stone blew up within 2 s (a hydrostatic deep layer answers the stone's crater
   as if it were a long wave).
3. *Shipped: (1) + the tall cell kept EXACTLY full by the transport (a shortfall taken from the water above it, two-sum; no
   volume correction or compaction for it) + 20 red-black sweep pairs of the tall layer alone after the band's sweeps (one cell a
   column).* Stable in every rig.

**The gates (RED controls in brackets):**

| gate | result |
|---|---|
| still 2 m pond (B 0.44 m) | max speed 2.9e-5 m/s, surface spread 0.015 mm, books exact (reduction + counted residue = start to 1e-9) - PASS |
| regrid: collapse -> step -> expand | 0 cells differ by > 1e-4 from just before (worst 1.5e-5, one call stepped) - PASS |
| long pulse 24 m channel, speed between 12 and 20 m | 1 m: fine 2.05 m/s, tall 2.05 (+0.0 %); 2 m: 3.38 / 3.38 (+0.0 %); height at 20 m +0.3 / +5.8 % - PASS [false bottom: -36 % / -68 %] |
| stone in an 8 x 8 m 2 m pond, max ring height | 1 m -11.2 %, 3 m +15.2 % against the 10 % gate - **FAILS narrowly** |
| the same pond across a chunk seam (translated one chunk) | 0 sites differ - PASS |
| the GPU water suite with tall cells off | every row as before (49/51: the known FLIP row + the stone gate above) |

The stone table (B and the tall sweeps; the 3 m ring is a 0.6 mm signal): no tall sweeps - unstable (+600 %); 5 - 1 m within 5 %, 3 m
+41-55 %; 20 - 1 m -8 to -15 %, 3 m +15 to +40 %. Neither a thicker band nor more sweeps brings the 3 m ring inside 10 % - the tall
layer sends a slightly larger long-wave front ahead of the ring.

**Cost (GPU, the integration rigs, partial wake OFF - every stored brick steps):** 8 x 8 m 2 m pond after a stone 0.93 -> 0.96 ms (no
gain: the tall layer's 40 extra dispatches eat it); **24 x 24 m 2 m pond 3.79 -> 2.63 ms (-31 %)** (40 tall sweeps: 2.95 ms, no
better). The saving grows with the pond's area; the live number (with the partial wake, the surface) is TC5's.

**Left:** TC4 (record tiles, seedColumns, the partial wake's freeze test reading the tall layer - its KE already counts), TC5 (L4 on
the bench interleaved, the owner's clip at B = 0.44 / 0.67 / 0.89 m), and the stone gate's decision: the owner's eyes on the clip, not
a loosened number.

### 27.22c Tall cells live (TC5, 2026-10-23): no gain with the shipped settings. Status: MEASURED, tall cells stay OFF

`tools/water_tall_l4.py` (new) on WaterBench_Creek, Release: a walled 24 x 24 m pond at x 4-27, z 2-25 (inside one chunk), a stone at
the centre, the shipped settings (partial wake ON, 20 sweeps, quiet scale 3), off and on (B 0.5 m) INTERLEAVED on the same water.
Evidence: `tall_l4.json`. Found and fixed on the way: a sleeping volume is never stepped and the call that decides sleep returns before
the decision (26.16), so `updateTall` never saw rest and never made a tall cell - the rest level is now read while the volume sleeps
(`WaterCoreManager::captureTallRest`, from `update`).

| depth | off (solver, median) | on, B 0.5 m | prediction (27.22a) |
|---|---|---|---|
| 1 m | 1.63 / 1.64 ms | 1.73 / 1.77 ms (all 46,656 columns tall, 5,508 bricks vs 6,276) | 1.5 -> 1.3 |
| 2 m | 2.30 / 2.30 ms | 2.44 / 2.37 ms | 2.4 -> 1.6 |
| control: B past the bed | - | 1.64 / 2.39 ms ("the band reaches the bed": no tall cells) | = off |

**The prediction failed.** With the partial wake the step already covers only the moving patch; the tall layer's own 40 dispatches a
substep (20 sweep pairs) are a fixed cost about as large as the per-cell work it removes. In the integration rig (partial wake off,
every brick stepped) the same 24 m 2 m pond went 3.79 -> 2.63 ms - the saving is real per cell, the dispatch count eats it live.
**Open:** at 2 m the tall cells switched themselves off mid-run (`tall_reason` back to "waiting for rest" - a trough or obstruction
rule fired; not yet traced).

**What it says.** The cost that bites now is not cells but LAUNCHES: every awake volume pays ~40 sweep dispatches a substep plus the
rest of the chain (~0.35-0.55 ms a call whatever its size - 27.20 #5), and tall cells add 40 more. Until the sweeps are fewer, larger
dispatches (or the tall layer's sweeps are folded into the band's), tall cells cost about what they save. They stay OFF by default;
the code, the gates and the tools stay.

### 27.22d Tall cells live, fixed (2026-10-23): depth stops costing past ~1 m. Status: BUILT, OFF by default, not signed off

**The ask (owner):** "does water pond depth affect performance? if yes, we need to figure out how and why to fix that" - "yes do it".
27.22c's live run was not a fair test: the tall cells switched themselves off at 2 m and never started at 3 m. Four defects, each found
by its numbers (`tall_offs` / `tall_last_off`, new echoes), fixed:
1. **A write deleted the water under the floor.** `upload()` cleared the tall state only at its END, after its residency had read
   every brick under the floor as ground - the grid's water there was never stored. Every write to a tall pond (placing water, an
   impulse: download -> edit -> upload) lost one brick layer (128 m^3 in a 24 x 24 m pond). The tests never uploaded while tall.
   Fixed: reset first. Gate `WaterTallCellTest.TheBooksSurviveAWrite` - RED control (`s_uploadKeepsTallForTest`, the old order): 128 ->
   42.7 m^3; fixed: 128.000003 -> 127.999980.
2. **The trough rule fired on a stone's crater.** It turned the tall cells off when the lowest surface came within B minus a brick
   of the floor (a 0.34 m crater in a 2 m pond), and they stayed off until the next rest. Now: within one brick of the floor, and the
   floor steps DOWN two bricks (expand, collapse lower at the next call) - back to the rest rule at the next rest (`tallDrop`).
3. **A deep pond never "rested".** The rest level was read only while the volume slept; a walled 3 m pond does not sleep (27.20 #4).
   Now: read when the surface is FLAT (the wet tops within 5 mm).
4. **The rest level was read too early.** The bench fills a pond in layers; the first layer is flat, and a 2 or 3 m pond got a 1 m
   pond's floor. Now the water the field holds is kept with the rest level; a change of more than half a cell over the surface (water
   added or taken) reads it again.

**Live, interleaved** (`tools/water_tall_l4.py`, the 24 x 24 m pond inside one chunk, a stone at the centre, shipped settings, B
0.5 m; evidence `tall_l4_d.json`):

| depth | off (solver) | tall cells | bricks off -> on |
|---|---|---|---|
| 1 m | 1.71 / 1.75 ms | 1.77 / 1.71 ms | 6,276 -> 5,510 |
| 2 m | 2.40 / 2.43 ms | 2.02 / 1.94 ms (-18 %; the crater stepped the floor down once) | 9,550 -> 6,460 |
| 3 m | 3.14 / 3.16 ms | **1.87 / 1.89 ms (-40 %)** | 13,334 -> 5,526 |

The books exact in every run (1,728.0000 -> 1,728.0000 m^3 at 3 m). **With tall cells a 3 m pond costs what a 1 m pond does.** The
tall layer's own 20 sweep pairs cost ~0.18 ms of it (`tall_sweeps_cost.json`: 3 m 1.95 ms at 20, 1.80 at 5, 1.82 at 0) - kept at 20,
the stone gate needs them. Tests: GPU water 50/52 (the known FLIP row; the stone gate's narrow miss, 27.22b), water unit 312/312.

**5. A floating body turned them off.** The first build's rule turned tall cells off for any moving body anywhere in the volume - a
float at the surface included. Now the GPU volume keeps the lowest cell a body (or its wake) holds (`solidLowY`, from setSolids /
setSolidsBox) and the floor steps down below it only when a body comes within a brick of the floor.

**The clip for the owner** (`tools/water_tall_video.py`, `tall_look_grid.mp4`: the 24 x 24 m 2 m pond, four wood floats, the same
push - radius 0.5 m, strength 4 - four times, 2 x 2): all fine / tall B 0.44 / 0.67 / 0.89 m. That push's crater stepped the floor
down to 0.78 m above the bed in all three tall takes (the trough rule) - so the three tall panels run the same band (1.22 m) and show
whether tall-cell water looks like today's, not the difference between bands. The books: 1,152.0000 (fine) vs 1,151.9993-9999 m^3.
The panels' solver times swing +-0.4 ms take to take (the 0.44 and 0.67 m takes had the same floor: 2.78 vs 2.03 ms) - the cost
is the interleaved table above, not the clip.

**Left for the owner:** the look (the clip), then whether tall cells go ON by default (and at which B).

### 27.22f Tall cells ON by default at B = 0.5 m (2026-10-23) - the owner's call. Status: SIGNED OFF (the look), ON

**The owner on `tall_look_grid.mp4`:** "to be honest, i am having a hard time telling the difference between anyone of them ...
visually the water looks the same across all of it" - then "yes turn it on at 0.5 m". `WaterCoreManager::s_tallBandDefault` 0 ->
0.5 m. (The owner also noted the floats ride a little high - the same in every panel, not tall cells; the old §20.6 item, "another
time".)

**Two fixes the default flushed out:**
1. **The partial wake froze nothing after a write.** `WaterPartialWakeTest.AStoneWakesAPatch...` (red with tall on: 3 % frozen in the
   first 0.5 s, the gate > 50 %). The rest tests read |f - fPrev| over EVERY stored brick, but fPrev is refreshed only for the stepped
   ones; a regrid (the re-collapse after each write's upload) changed f in every frozen column and read as motion - the whole pond
   awake for half a second after every stone. `wc_tall.comp` now writes fPrev with f (collapse and expand); the reductions' |df| is
   a fraction of the cell (a tall cell's change / H). Green: 67 % / 59 % frozen.
2. **A shallow pond paid for nothing.** A 1 m pond's tall layer is 3 cells; its own sweeps cost more than it saves (1.66 -> 1.77 ms
   live). A volume now makes tall cells only when its median tall column is at least 6 cells (0.67 m) - `kTallMinCells`, reason
   "too shallow to pay".

**Live, the defaults (`tall_l4_on.json`, `tall_l4_min.json`; off vs on interleaved, the 24 x 24 m pond, a stone):**

| depth | off | on (0.5 m) |
|---|---|---|
| 1 m | 1.65 / 1.76 ms | 1.74 / 1.71 ms - all fine ("too shallow to pay") |
| 2 m | 2.42 / 2.33 ms | 1.99 / 1.95 ms (-18 %) |
| 3 m | 3.15 / 3.16 ms | 1.87 / 1.87 ms (-40 %) |

Tests (tall on by default): GPU water 50/52 (the known FLIP row; the stone gate's narrow miss, 27.22b), water unit 312/312.

### 27.22g TC4 - tall cells and recorded tiles together (2026-10-23). Status: BUILT, ON with the default

Lakes are deep and record: the two could not run together (tall cells refused a record-tiled volume). Now: `readColumns` (a tile into
the record) spreads each tall cell back into its column's cells, as `download` does; `seedColumns` (a tile woken from the record)
collapses the seeded columns into their tall cells before the upload (the bricks under the floor read the ground). The rest rule counts
the record's water too (a recorded tile leaves the surface field - it read as water lost). Gate
`WaterRecordTilesTest.TallCellsRecordAndWakeWithTheBooksExact`: a 32 x 16 m pond 2 m deep, recording on - tall cells made, all 8 tiles
recorded (1,023.9999 of 1,024.0000 m^3 in the record), a touch seeds a tile back, a push - cells + record = placed within 5.6e-6 m^3
every tick. RED control (the seed not collapsed, `s_seedSkipsTallCollapseForTest`): 85.3 m^3 lost. GPU water 51/53 (the known FLIP row,
the stone gate), unit 312/312.

### 27.23 W1d step 0 - what a volume costs by its box (2026-10-23). Status: MEASURED, DESIGN

**Why:** lakes (W1e) are past the 2 M-cell cap (`kMaxCellsPerVolume`). Before lifting it: what does the cap hide? Measured live
(WaterBench_Creek, Release; `nvidia-smi` and the process's working set around one `water_av_create`, empty boxes):

| box | cells | create | GPU memory | process memory |
|---|---|---|---|---|
| 24 x 24 x 2 m | 0.84 M | 141 ms | +102 MB | +83 MB |
| 46 x 28 x 2 m | 1.88 M | 291 ms | +231 MB | +236 MB |

**~123 bytes of GPU and ~125 bytes of process memory per cell OF THE BOX**, water or not. A 60 x 60 x 2 m lake box would take ~720 MB
of GPU memory and ~730 MB of process memory; a 200 m lake ~8.6 GB of GPU memory. Three things scale with the box, not the water:
1. **The GPU slot pool** - a sparse volume reserves a slot for EVERY brick of its box (`capacity = mapCount`, "never full"): ~26
   per-cell buffers x 4 B plus the solid field, and a host-visible staging copy.
2. **The CPU mirror** - `Av::grid` (f, u, v, w, occupancy), `occCache`, and the CPU reference solver's arrays, allocated for a GPU
   volume too (126 uses in `WaterCoreManager` read or write it; 27.10's W1d list).
3. **Writes as whole-volume round trips** - an impulse, placed water, a droplet landing or a debris exchange downloads the whole volume
   to the mirror, edits it on the CPU and uploads it all back (a lake: every splash a 100+ ms hitch).

**Design (W1d, in this order - each measured, each group's tests unchanged):**
- **W1d.1 the slot pool sized by the water:** capacity starts at what the first upload stores (+ a margin) and DOUBLES when the
  residency needs more (a new pool, the stored slots copied, the tables rewritten - rare, counted, echoed `gpu_brick_capacity`,
  `pool_grows`); `heldByCapacity` stays the never-lose-water backstop. Gate: a 46 x 28 x 2 m box holding a 10 x 10 m 0.3 m pond
  allocates ~1/20 of today's memory; `SparseEqualsDenseAcrossSeams` bit-identical across a grow.
- **W1d.2 writes in place on the GPU:** impulses as a velocity record (the momentum kernel, du given instead of computed), placed
  water / droplet deposits as fills uploaded into the bricks they touch (the seed path, 27.12), debris exchange already on the device
  (26.26). Gate: each write's books exact; an impulse into a 46 x 28 m pond under 1 ms (today a whole round trip); the impulse's waves
  within float rounding of today's at 1 / 2 / 4 m (`water_wave_reach.py`).
- **W1d.3 the CPU mirror goes** for GPU volumes, group by group: debug reads (census, column cells, probe, columnMassDepth) -> a
  read-back of the bricks asked about; record / sleep / seed -> already per tile (27.12-27.19); occupancy -> per brick (26.19);
  then `Av::grid` is not allocated for a GPU volume and `kMaxCellsPerVolume` applies to CPU volumes only. Red: `ALakeOverTheOldCapRuns`
  (60 x 60 m, 1 m deep in a 2 m box, 5.8 M cells - refused today with the cap's message).
- **W1e the lake, live** (27.10): a push wakes tiles round it, waves, freezes, records; the rest never stepped; the books exact;
  memory by the awake water, not the box.

**Design keys:** cost only - no look or behaviour changes (each step's gates are bit-identical or within float rounding of today's);
world-aligned bricks and tiles as before; the API echoes the pool (`gpu_brick_capacity`, `pool_grows`) and the write path
(`writes_in_place`, `writes_round_trip`); no defaults change. **Verdict: READY for W1d.1.**

**W1d.1 BUILT (2026-10-23): the GPU slot pool follows the water.** A sparse volume with no fixed capacity starts with 510 slots and
DOUBLES when a residency needs more (`WaterCoreGpu::growPool`: new device buffers, every stored slot copied with its number, the tables
rewritten, the kernels rebound - `bindPipelines` split out of `createPipelines`). Upload and seed grow before their residency; a step
that could not store everything holds those faces for one call (the capacity backstop, counted) and grows at the start of the next -
nothing recorded ever points at a freed buffer. Found on the way: `upload` kept the staging pointer it took before the grow and wrote
into the freed buffer (the still pond lost 127.99 of 128 m^3) - re-taken after it. Echo `gpu_pool_grows`. Measured live (same boxes as
above): **24 x 24 x 2 m empty 102 -> 15 MB of GPU memory; 46 x 28 x 2 m empty 231 -> 25 MB; the same box holding a 10 x 10 m 0.3 m
pond 85 MB** (two grows, 4,095 slots for 2,119 stored bricks; the water exact, 30.0000 m^3). Process memory 236 -> 95 MB empty - the
CPU mirror (W1d.3). GPU water 51/53 (the known two) - the bit-identical sparse-vs-dense rows now run through grows.

**W1d.2 BUILT (2026-10-23): impulses and blasts in place on the device.** Measured first: one stone impulse on the 46 x 28 m 1 m pond
cost ~85 ms in the call (the whole-volume download) + ~115 ms on the next frame (the whole-volume upload). Now
`WaterCoreGpu::queueImpulse` / `queueRadialImpulse` - two record types in the momentum kernel (flagged by the last float), applied at the
start of the next call where the upload used to land: the CPU's rules (a fixed dv on every face within the radius; the blast's
falloff, up-bias and speed cap on the faces that border water), a face written only where its owner brick is stored. The manager's
`addImpulse` / `addRadialImpulse` take this path for GPU volumes (echo `writes_in_place` / `writes_round_trip`;
`s_writesRoundTripForTest` keeps the old path for comparison). Gates: `WaterTallCellTest.TheImpulseRulesOnTheDevice` - the same water,
the CPU rules on a downloaded copy vs the device records applied without a step: 15,088 faces kicked, none differs by more than 7.6e-5
m/s (float rounding of the face positions - the device measures from the grid's origin; a wrong face would be off by up to 3 m/s);
`WritesGoInPlace` - a 20 x 10 m pond, stone + blast + their calls 62.4 -> 7.0 ms, books exact. (A surface-vs-surface comparison
against the round trip is NOT a fair gate: the round trip's upload also resets the pressure, the remainders and the halo - it moved
the surface 104 mm by itself.) Live, the 46 x 28 m pond: no upload after an impulse (`gpu_uploads` stays 1; it was +1 per impulse,
115 ms). Placed water and sources still round-trip (next). GPU water 53/55 (the known two), unit 312/312.

### 27.24 W1e step 0 - the lake, measured (2026-10-23). Status: MEASURED; the rest sums by the awake water BUILT; ONE OWNER'S CALL

**The cap (W1d):** a GPU fill volume's cap is now `kMaxCellsPerGpuVolume` = 16 M cells (its CPU mirror, ~50 B a cell, until W1d.3);
the CPU reference and FLIP keep 2 M. `WaterTallCellTest.ALakeOverTheOldCapRuns` (red: refused before): a 60 x 60 m lake 2 m deep in a
3 m box, 7.87 M cells, created in 0.9 s, filled, at rest, a stone - the books 7,200 -> 7,199.9998 m^3; the CPU reference still refused.

**1. A stone wakes the whole lake - at the shipped freeze test (x3) - with waves nobody can see.** `DISABLED_DiagLakeRipple` (the lake,
the stone at the centre; max |surface - rest| in 2 m rings; partial wake on, recording off):

| t | awake columns (of 4,624) | ripple mm at 0-2 / 4-6 / 8-10 / 12-14 / 16-18 m |
|---|---|---|
| 2 s | 1,017 | 10.9 / 2.5 / 0.0 / 0.0 / 0.0 |
| 4 s | 1,851 | 2.0 / 1.3 / 0.11 / 0.10 / 0.006 |
| 6 s | 3,018 | 0.8 / 0.37 / 0.05 / 0.001 / 0.013 |
| 10-15 s | 4,400-4,580 | 0.03 -> 0.001 everywhere |

The patch outruns the visible ripple: the long-wave front (sqrt(g d) = 4.4 m/s at 2 m) carries sub-micron to 0.05 mm of motion, and
the freeze test's |df| half (3e-5 a tick at x3 = the level moving 0.2 mm/s) counts it as moving - every stuck column fails |df|, never
KE (26.35's finding, now the lake's whole cost: 24 ms a call by 10 s). The same run at **x10**: the patch peaks at 1,490 columns (5 s)
and is back to 130-200 by 9-15 s; **x30**: peaks at 790, 110-170 after. The ripples at 0-10 m are the same at x3 / x10 / x30 to the
printed 0.01 mm until the x3 run's patch covers the lake. Live (WaterBench_Creek, `tools/water_lake_l4.py`, 60 x 28 m - see 4):
x10 + recording: the stone's tiles wake, wave, freeze, every tile records, the volume sleeps by ~25 s; mean 2.8 ms over the 30 s
(4.6 -> 3.9 -> 1.7 -> 1.8 -> 1.3 ms medians by 5 s, then 0), books 3,360.0000 -> 3,359.9998 m^3. x3 + recording: ~2,000 of 2,176
columns still awake at 30 s, 6-10 ms a call (`lake_l4_60x28_qs3_rec.json`). **Owner's call (26.35 item 1, open since 2026-10-19):
quiet scale 10** - the lake goes still and records ~10 s after the waves; the last sub-0.05 mm ripples stop where they are.

**2. BUILT - the rest sums follow the awake water.** Profiled at x10 after the patch shrank (130-200 awake): the call still cost
10-12 ms, `reduce` 3.3 -> 6.6 ms of it - the rest reductions (26.31: "the record, the reductions and the surface cover every stored
brick") ran one thread per cell of all ~38,000 stored bricks, five passes, every call. A frozen column is not stepped (no kernel
writes its cells), so its units cannot change while it is frozen: `wc_reduce` keeps the per-unit sums (KE, max |df|, mass) in a
device unit map (world-aligned, like the gathered map); a CACHED call reduces the awake bricks only (slots [0, stepSlots) after the
partition) and mode 6 reads a frozen column's units from the map; the three max-|velocity| passes (mode 1) ride mode 0's spare lane
(the same sites, the same thread id). Anything that writes frozen cells forces a full call (`unitsFullNext`: upload, seed, a tall
regrid - it runs with frozenOn 0 -, a pool grow, an occupancy change). The residual (report only) covers the solved cells.
`WaterCoreGpu::s_unitCache` (default ON - a cost change only), `water_av_reduce_cache {on}` (interleaved timing), echo
`gpu_reduce_slots` / `gpu_reduce_cached`. Gate `WaterTallCellTest.TheRestSumsFollowTheAwakeWater` (red first): a 40 x 10 m pond with
the partial wake - a stone, tall cells made while columns are frozen, a blast, water uploaded into the frozen far end - cache on vs
off: **330 calls, every call's KE, max |df|, mass, max speed, substeps, awake / frozen counts equal float for float, the end state
bit for bit**; 2,301 vs 4,398 slots reduced a call. RED CONTROL: the cache that ignores the refresh misses up to 43.95 m^3. The lake
at x10: `reduce` 3.3-6.6 -> 1.1-1.6 ms; the call at 130-200 awake 10-12 -> 6.8-9 ms (the sync test harness - see 5).

**3. What still scales with the stored bricks, not the awake ones:** the brick record (~1 ms), residency (~0.9), the reductions' box
parts (~1.5: the unit gather and fold over the box's units), the surface copy (~0.5). Recording is what removes them - a recorded
tile has no bricks (the live x10 run above: 0 bricks, 0 ms once recorded). With recording OFF (the default) a lake at rest costs
these every call.

**4. Two defects found, OPEN:**
- **A box whose floor lies over air outside the box never settles live.** The 60 x 60 m box (y 16-18) spans the creek bench's trench
  (x 10-109, z 31-32, y 15 cleared, below the box): at x10 + recording the whole lake stayed awake (4,400-4,600 columns, 12-15 ms)
  where the same lake in the test, and the 60 x 28 m box beside the trench, settle. Not investigated: the box's bottom boundary over
  out-of-box air (a real lake's box takes its bed in).
- **Destroying an awake volume lost two whole tiles of water.** The 60 x 28 m run destroyed mid-wake (tiles recording / waking): the
  next create seeded 3,103.9998 m^3 from the record, not 3,359.9998 - 256 m^3, two 8 x 8 x 2 m tiles. Not investigated yet.
- (Measurement, not a defect: the integration harness steps SYNC, device ms ~2x the async path's - the recorded lake 5 vs 2 ms at the
  same awake count, `LAKE_RT=1` in `DISABLED_DiagLakeRecorded`. Quote live or async numbers.)

**5. Tall cells in the lake:** the stone's crater sets off the trough rule (27.22d) - the floor steps down 0.44 m and the stored bricks
go 28,450 -> 37,882, so most of the saving goes with the first splash (sync test, x3, all awake: tall on 14.1 ms median vs off 9.6).
Not changed; the trough rule's reach is the lever (a crater is not a trough).

## 28. The launch count (2026-10-23): measured - it is not the cost. Status: MEASURED; line sweeps built, OFF

**The ask (owner, 2026-10-23):** "yes" - after 27.22c concluded the dispatch count was the bottleneck. Measured before building
anything big.

**1. One call, counted** (`tools/water_launch_split.py`, new; `gpu_dispatches` / `gpu_barriers` in `water_av_list`, new): a 5 x 5 m
0.3 m pond after a stone - **90 dispatches, 113 barriers, 0.41 ms**; a 24 x 24 m 1 m pond - 98 dispatches, 1.55 ms. The sweeps are the
largest stage in both (0.13-0.19 of 0.41 ms; 0.40 of 1.55 ms). Evidence: `launch_split.json`.

**2. Line sweeps - tried, no gain** (`wc_rbgs_line.comp`, new pipeline; `water_av_line_sweeps {count, omega}`; OFF by default): each
column solved exactly along y, the columns in a checkerboard. The 46 x 28 m pond (partial wake off, `water_wave_reach.py --line`):

| solve | leftover pressure error (median) | solver ms | waves 1 / 2 / 4 / 8 m (rms mm) |
|---|---|---|---|
| 40 point sweeps | 0.011 | 4.10 | 11.6 / 5.0 / 1.6 / 0.13 |
| 20 point sweeps (shipped) | 0.62 | 3.18 | 14.1 / 5.3 / 1.7 / 0.12 |
| 4 line sweeps, omega 1.0 | 1.9 | 3.39 | 11.5 / 4.9 / 1.6 / 0.14 |
| 8 line sweeps, omega 1.3 | 0.57 | 4.52 | 12.9 / 5.0 / 1.6 / 0.09 |

The same accuracy as 20 point sweeps needs 8 line sweeps (16 dispatches) and costs MORE: a line dispatch walks a column serially
and does ~3x the work of a point dispatch. On the small pond: 16 line dispatches 0.141 ms against 40 point ones at 0.131.

**3. What a launch costs by itself** (`WaterCoreGpu::diagLaunchCost`, `WaterTallCellTest.DISABLED_DiagLaunchCost`): 1,000 empty
dispatches - **0.70 us each with a barrier, 0.14 without** (64 workgroups 0.78 / 0.23; 1,024 workgroups 1.45 / 0.92). A small pond's
113 barriers are therefore ~0.08 ms of its 0.41 ms. **The 27.22c conclusion was wrong: the per-call cost is kernel WORK, not
launches** - spread over every stage, over every stored cell. The 5 x 5 m pond steps 210 bricks (26,880 cells) for ~6,100 cells of
water: 4.4x, the halo (the extrapolation's reach + a call's travel, 26.6) round shallow water. Stepping every awake body in one
interleaved submission (sharing the barriers) would save at most that ~20 %.

**What it says:** the cost per awake body is (a) the halo's cells and (b) what each stage does per cell; neither is a launch count,
and none of the three structural levers tried this week (tall cells 27.22, line sweeps, batching) moves it on today's sizes. The
small-body limits of 27.20 stand as measured.

## Appendix A. §0a's running log as it stood on 2026-10-19 (moved verbatim from §0a)

**Was: "0a. Where things stand, and what is next (2026-10-14)"**

**Signed off by the owner:** the small-pond feel - blasts, debris both ways, floaters, the drawn surface (2026-10-09,
§19.8); the ripple layer's black/white-square fix (2026-10-11, "new ripples look really great", §22.12.2). Accepted for
now: the ripple layer itself (§22.9); the first droplet cut ("honestly doesnt look bad. maybe more particles would be
nice", §22).

**Built, not signed off:** moving solids M0-M2 (§20.12-20.14); clear water S0 (§21.10); the droplet pool (§21.12);
waves that keep their energy, pond friction, size-dependent stone drag, the level-jerk fix (§24.6-24.11).

**The scenarios (§3), latest ledger:** S2 pump/overflow, S4 breach, S5 drain, S11 rest/reload PASS; S3 dam break front
and seiche PASS, run-up at 1 m FAIL; S1 pour rests in 3.4-3.6 s against a 3 s gate; S8 push built and signed off
(near-side 0.286 m against 0.3); S6 only partly (no crate; debris yes); S12 shore PASS on the column solver (look not
signed off, G paused). **Never run:** S7 wading, S9 waterfall, S10 stream over rocks, S13 scoop, S14 pipe, C1-C5
containers.

**Open small-body items, by weight** (each with its section):
- *Feel and correctness:* voids made by moving water are closed by compaction, not prevented - the cure is a volume
  correction in the pressure solve (§24.11); short waves on a half-full cell keep 0.53-0.82 per swing (§24.11); the
  ±2 cm terraces / cells stuck at f = 0.5 (§19.8 - causes FOUND and fixed 2026-10-14 in §26.9, signed off); a 0.08-0.10 m/s phantom current on a still GPU
  pond (§19.6 - not re-measured since 26.10 made still GPU water settle); wood floats high and a floating block's pond never sleeps (§19.3, §24.8; cure M4 two-way force, §20.6);
  added mass not modelled (§24.10); **OPEN (the owner, 2026-10-18): spiky water on a dropped stone while it settles** -
  cause PROVED (§13 end, 26.32): the over-full carry takes a moving body's displaced water straight up its column, out on the
  stone's top (11 cm pillars to the box's lid for ~1-2 s); three fixes tried, none better, none kept; blocked on a repeatable
  rig (a body on a fixed path - the tumbling GPU stone gave 0.75 and 2.87 m at one setting). A 0.84 m late jet with no carry
  (the cavity closing?) is a second, unmeasured item.
- *Look:* F2 sign-off and F3 deletions (§17.3); pond whitewater S4 not wired to the breaking ledger (§21.4, §22.12);
  1.5-2.3 % black facets at steep tilts from a second cause (§22.12.2).
- *Parity and tests:* the CPU-GPU kick gap was not re-measured after the theta fix (§19.2, §24.5 F2); D-T5 droplet GPU
  parity not written (§21.12); FLIP run-up parity (§15.15).
- *Not started:* wading (E4), containers, pipes, scoop, AV merge/split, the §23 liquids.
- **Owner preference, 2026-10-11 (not designed):** "ideally splash particles would not 'take' from the volume of the
  pond. it should jsut be a few splash particles for affect." - today every droplet is pond water, ledger-exact (§12
  #5, §21.2). Making splashes cosmetic is a design change to write and check when the owner asks for it.

**Is the still pond ready to build on? Yes, for flowing water.** The owner signed off its feel; what remains open is
polish (look, short-wave loss, terraces) or features that do not gate a stream (wading, containers, pipes, other
liquids). What DOES gate a stream is listed in §25 and is the first part of it: pond friction brakes flows (the weir
reads 17 % low with it on, §24.8), and nothing has ever measured water that keeps moving. **Now: §26 - one water model, the pond's, everywhere** (owner, 2026-10-13: "ponds need to be the foundation ... is there
water at that voxel?"). §25's tiers built the creek on a second kind of water (the column solver) and needed "windows" to
stitch it to the pond's; §26 replaces that: water is cell fills on the 1/9 m lattice everywhere, stored only where it is
(and its neighbours), on the GPU. Built under §25 and kept as targets or parts: the creek bench and the dam & pond bench and
their numbers (§25.24-25.38), Manning at the bed and the generator-line floor (25.39), the GPU ripple step (25.35), the
measuring tools. **§26 P1 DONE and signed off (2026-10-13, §26.5):** the cause was the fill transport (face by face, each
move capped at the receiver's room); the all-faces-at-once transport is the default on CPU and GPU (100.0 % of Manning's
depth and speed). **§26 P2a BUILT (26.8):** the GPU core stores water in bricks that follow it, bit-identical to a box;
the creek's cost gate is OPEN. **26.9 SIGNED OFF (owner 2026-10-14, "videos look good"):** the surface at the liquid
threshold fixed in BOTH backends (the surface jump, films thrown over a slow surface, float-rounding losses - 19.8's "cells
stuck at f = 0.5"). **26.10 (2026-10-14, built, not signed off):** the still-water first guess is ON - its resting-body loss
was the float store rounding at f = 0.5 (and the GPU compiler folding every "exact difference"), fixed with a per-cell
remainder; GPU still water now settles and sleeps (`GpuRestDecisionDeterministic` green, the live pond asleep in 2 s).
**26.11 (2026-10-14): the cost** - the creek call ~1.6 -> 1.02 ms at the engine's 27 sweeps (GPU 0.79 ms), measured
per stage with GPU timestamps: only the water's columns cross to the host now, and the extrapolation ping-pongs without
copies. **26.12 P2b BUILT (2026-10-14): the engine on bricks** - engine GPU volumes are sparse; on the creek bench's box
(583,200 cells) a frame went from 21 ms (one moving body) + 7.1 ms (update) to 0.36 + 2.2 ms, a forced read-back 257 -> 14 ms.
**26.13-26.14 (2026-10-14): P3a BUILT, P3b first run; the owner signed off the clips 2026-10-15 (the creek as pond water
and the 26.10 A/B: "they all look really good")** - the creek bench runs as POND water live (one sparse GPU volume,
springs, outfalls, Manning at the bed, a ripple window): steady out/in 0.991, ledger 7e-5 m^3, ripples on. Two things open
before it can replace the column creek: the cost (~6 ms a call at 1,216 bricks - 3x the budget) and the depth (the pond
model flows over the carve's REAL 1/3 m steps, the column run flowed over the line - not the same creek; 26.14).
**26.15 (2026-10-15): the bench re-carved at 1/9 m (owner's choice (b))** - on the same voxel bed the pond model runs 6 % (q 0.2)
/ 10 % (q 0.5) deeper than the column model, both deeper than Manning (the drops cost head); the source totals' float drift
fixed (the books exact again live); the call's time measured live - the GPU's own work is 1.85 ms, the frame loses ~7 ms
because the main thread WAITS at the fence behind the renderer.
**26.16 (2026-10-15): the water's call is no longer waited on** - submitted each frame, collected the next (bit-identical
water, one frame of lag in what the host sees): the creek bench's frame 12.9 -> 7.4 ms (77 -> 135 fps; 5.5 ms with the water
paused); a lost-count fault found on the way (re-sending sources in async) and fixed.
**26.17 (2026-10-15): flat bricks** - bricks 8 x 2 x 8 by default, bit-identical to 8^3: the creek bench stores 34 % fewer cells,
GPU 1.83 -> 1.57 ms a call, the water the same to the fourth digit; the frame unchanged (the water no longer holds it up).
**26.18 (2026-10-16): ground bricks** - halo bricks that are all ground are read from a sentinel, not stepped; which ones is
re-read from the world on every change (everything is destructible): creek GPU 1.56 -> 1.35 ms, the water identical; digs under
and beside the running creek drain into the holes, books exact. Found: every world change freezes the frame ~145 ms per volume
(the whole box re-read and re-uploaded) - and any edit anywhere triggers it.
**26.19 (2026-10-16): a dig without a freeze** - the owner saw the dig clip stutter: measured, 1,051 ms of each dig was the
API's whole-world NPC nav rebuild (fixed: the touched columns only) and ~148 ms the water's full re-read; designed: the pool
records which cubes changed, the water re-reads and patches only those, in place.
Built 2026-10-17: the same trench clip went from 38 freezes (~1.27 s per dig) to 0; worst frame per dig ~22 ms; the water's
part 1.5-5 ms; walking across chunks costs the water nothing. **Signed off by the owner 2026-10-17** ("wow that looks way better").
**26.20 (2026-10-17) ANSWERED:** the pond's water is at the textbook depth on the stepped bed (0.187 / 0.306 m vs 0.190 /
0.306); 26.15's "deeper" was the gauge (the highest sub-column) plus AIR inside flowing water (cells under the surface 4-10 %
empty, CPU and GPU) - the drawn creek stands 9-26 mm above its water.
**26.21 (2026-10-17) DONE, signed off by the owner ("looks good") - the air in moving water:** the owner chose "fix the air first". A volume
correction in the pressure solve (CPU + GPU, on by default): a part-full cell with water above it and water or rock on all four
sides asks for the inflow that refills it over 0.5 s (only what the water above can give; deficits under 2 % left alone). Rig:
cells under water 0.90-0.96 -> 0.98-1.00 full, the water's top 9-26 -> 0-2.9 mm above its amount, the water's depth within 1 %.
Live: the drawn creek 8.5 / 21.4 -> 1.5 / 2.4 mm above its water at q 0.2 / 0.5, GPU cost unchanged. Clip
`creekbench_pond_g9_q0.5_airfix.mp4` (off -> on at 15 s).
**26.22 (2026-10-17) P3c DONE, signed off by the owner ("video looks good"):** the dam & pond bench on pond water (one 1.55 M-cell GPU volume, no engine
change): pond rise 5.42-5.45 mm/s (Q/A 5.56), spilling out/in 0.981, the dam's pool 15.824, break surge 1.95 Q, books exact
(1.4e-5 m^3; edits inside float noise) - the column model's numbers, except a deeper dip after the dam (27 % vs 46 %). Cost:
3.17 ms at the automatic 160 sweeps, 1.73 ms at 40 with the same numbers. Clip `dam_pond_pondwater.mp4`.
**26.23 (2026-10-17) BUILT, not signed off - the automatic sweep count:** measured the solve's own one-step error: water with a
free surface is within 0.4 % of converged at 40 sweeps whatever the box's length (12-540 cells); water under a solid lid does not
converge at 160 either. The automatic count now stops at 40 (was 1.5 x the longest side, up to 160): the dam & pond bench 3.17 ->
1.46 ms with the same numbers; the signed-off demo ponds unchanged (27). Open: an enclosed-water solver for lidded water.
**26.24-26.26 (2026-10-17):** P3d designed (no normal session creates a column region); the creek's current / splash / ripple
tools ported to pond water (`--pond`); porting them found (26.25) floats drifting at ~0.4 of the pond current - the material
damping braked them toward the ground and the debris -> water exchange handed the brake to the water; fixed (floats now ride at
1.000 of the water) - which exposed (26.26) that the exchange reads back and re-uploads a whole GPU volume every frame anything
moves in it: 4 floats in the creek, 124 -> 7 FPS. Pre-existing.
26.26 BUILT: the exchange on the device - 4 floats 7 -> 123 FPS, floats at 1.002 of the water. **26.25 + 26.26 signed off by the
owner 2026-10-17** (`current_floats_pond.mp4`: "the video looks really good").
**26.27 (2026-10-17/18) BUILT, not signed off:** P3d-2 - the column water removed; tests, the creek bench and the dam & pond
bench (Release: rise 5.42 mm/s, out/in 0.98, surge 1.95 Q, 1.44 ms GPU, 119 FPS) all as predicted. The first dam runs' miss was
a DEBUG build, not the other application.
**26.28 (2026-10-18) DONE:** the 4-tall halo failure was a false alarm in the check (two films in different cells read as one in a
corner; the nearest water was 4 cells away, reach 2) - the test now checks by cell; integration 45 / 46 (the particle run-up).
**26.29 (2026-10-18) MEASURED:** the pond size ladder - one awake pond fits the 2 ms budget to ~10 x 10 m, a sleeping one to ~15 x
15 m; the drawn surface (~8.8 us per m^2, paid asleep and off-screen) is the larger cost; deep ponds never fall asleep.
**26.30 (2026-10-18) DESIGN:** the owner chose (a) first. Measured: a resting pond's surface costs ~27 ns per vertex on the GPU
and ~27 ns per vertex to rebuild on the CPU every frame, seen or not (32 x 28 m: 7.9 ms GPU, +12.3 ms CPU). Design: kept
device-local tiles rebuilt only when changed (1), culled to the view (2), then awake water (3), then distance (4, a look change).
**26.30 steps 1-2 BUILT (2026-10-18), not signed off:** kept device-local tiles + view culling - a resting 32 x 28 m pond's
surface pass 7.9 -> 0.35 ms facing and 0 ms away, its CPU frame 14.2 -> 4.75 ms; the look unchanged by pixels (2 runs).
**26.30 step 3 BUILT (2026-10-18), not signed off:** the water's CPU measured part by part - a sleeping pond's water now 0.01 ms (was
1.36 ms at 20 x 20 m; its frame 1.93 ms against 1.85 with no pond); a 32 x 28 m pond after a stone 15.9 -> 8.3 ms CPU frame (tiles
kept within a flow tolerance, rebuilt over 8 threads, births and readback cut). Awake water's rest: the GPU solver 3.0 ms (needs (c)).
**26.31 (2026-10-18) DESIGN, measured:** (c) the partial wake - after a stone the moving share (+2 columns margin) is 10 % at
0.1 s, 81-96 % at 4-10 s in a 32 x 28 m box (the waves fill it and reflect), ~20 % from 11 s; the whole body pays 3-5.7 ms
throughout today. Design: brick columns frozen when quiet (the volume's own test), a wall to the awake ones, wakes local.
**26.31 step 1 BUILT (2026-10-18), default off:** frozen brick columns, a wall to the awake ones; the gate passes against the
measured floor (a splash amplifies any change: the whole body against itself with the stone 1e-5 stronger differs by 273 mm);
the cost barely moves (2.41 vs 3.06 ms with 84 % frozen) - frozen bricks still get threads and whole-buffer copies.
**26.31 step 2 BUILT (2026-10-18), default off:** the awake bricks partitioned into the low slots, the step over those only - a
46 x 28 m pond after a corner stone 4.81 -> 2.76 ms over 15 s (1.75-2.0 ms the first 4 s against 4.5-6.2); a walled 32 x 28 m
box gains only the first 2 s (its reflections wake it all); the fully-awake pond costs what it did. Left: ~2 ms of whole-volume work.
**26.32 (2026-10-18) PROVED, NOT FIXED - the spiky water on the stone** (the owner's open item, see the list above): the 26.5
carry, every substep, sends a moving body's displacement straight up its column (1,654 L out of a 1,000 L stone's cells in half a
second; half where the body had a rate the projection was to shed, half the column ledger by design). Three fixes, no better
within the drop-to-drop spread; the measurement kept (`water_av_list` carry_*_m3, `water_surface_dump`, `water_stone_spikes.py`).
**26.33 BUILT (2026-10-18), not signed off:** with the partial wake the surface kernel and its copy cover the awake columns only (the
host keeps the frozen ones; byte-equal heights, gated): the copy 0.6 -> 0.07-0.10 ms, a call after a stone 2.0-2.2 -> 1.3-1.6 ms.
The owner's look re-recorded on an idle GPU (`pw_look3_side_by_side.mp4`: OFF median 4.80 ms, ON 3.69).
**The partial wake's look SIGNED OFF by the owner 2026-10-18 (`pw_look4`).** **26.34 (2026-10-18):** the awake patch creeps over the
whole pond at the sleep test's thresholds (sub-0.1 mm waves count as moving); a knob for the freeze test (`quiet_scale`): at x3 the
mean solver time over 20 s after a push 2.34 -> 0.72 ms, the waves the same within run noise, the frozen pond flat to 0.08 mm.
**x3 signed off by the owner 2026-10-19 and made the default.** **26.35:** the sweeps are work, not launches; an exact 6 % saving; 20 sweeps measure the same as 40 (waves, volume, creek speed) for ~0.35-0.9 ms. 20 sweeps and the partial wake ON by default, both the owner's calls 2026-10-19. **Next:** the partial wake's +0.036 ms where nothing freezes (the motion-map copy), the reductions; then the sweeps' launches and the reductions. 26.32 waits for a repeatable rig. The §23 slime experiment stays queued.
