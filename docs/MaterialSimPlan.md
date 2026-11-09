# Material Simulation Plan — Noita-style voxel materials, alongside WaterCore

**Status: P1 BUILT 2026-11-03 — P1a (CPU reference core) §6b; P1b (GPU port bit-exact, `/api/matsim` lab, raw-cell
view, live rig) §6c. P1c PRESSURE BUILT 2026-11-04 (§6d): U-tubes and communicating vessels level at every cell size,
GPU bit-exact. Plan re-audited against Noita's principles + realism (§0b): new P1d (speed ∝ cell size, owner
decision), P4 density/viscosity, P4b world-as-material, P8 reactions. P2 not started. 2026-11-04: §6e
shows the CA cannot carry pressure at physical speed (routing never converges); §6f experiment E1, MatSim's
own particle fluid (PBF), levels the U-tube at physical speed and period at 1/3 and 1/9 m. 2026-11-05: §6g
experiment E2, the hybrid (resting cells, moving particles, whole-body wake/settle) works at 1/3 m. 2026-11-05:
§6h the particle step runs on the GPU (1/9 m at 7-12 ms a frame) and E2 works at 1/9 m too. §6i: E2 runs
live in the lab rig (L4) at 1/3 and 1/9 m.** Supersedes the same-day `LiquidCaLab.md`
note, folded in here and deleted. Context: [`NoitaGapAnalysis.md`](NoitaGapAnalysis.md) (why: N1–N4,
Tiers 2–3) and [`WaterCore.md`](WaterCore.md) (the water system this must live beside: §4.2 method
choice, §30.0b locality, §51 levels).

## 0. The owner's direction (2026-11-03)

- **Two systems, side by side, for now.** *"develop this new noita system along side the new water
  system. i dont want to interfere with its work and want to hopefully find a good marriage of the
  two systems some day."*
- **Find where things break.** *"figure out the cost and baseline at what scale things break down
  and figure out how to reach the goal of fully realized large worlds with water bodies of massive
  scale or small scale... how to scale simulation quality, and when we need to take shortcuts."*
- **Quality follows the camera.** *"localize quality to the camera and where the camera is
  moving."*
- **Waves must be grounded.** *"the look of waves would still be grounded in the shape and reality
  of the world, so waves crash against shores or follow currents. I dont want waves for the sake of
  waves. waves eminating from no where and just clipping through voxels is not acceptable."*
- **Plan first, no coding.** Measurements are allowed if needed, but a WorldForge session is
  running.

## 0b. The bar: Noita's principles, in 3D, made realistic (owner, 2026-11-04)

*"remember we should be trying to recreate Noita design principals in 3D (were realistic). if the
plan doesnt live up to that, then we need to make sure the plan is being updated accordingly"*

The principles are `NoitaGapAnalysis.md` §1, N1–N9. **Realistic** adds a bar Noita doesn't need:
where Noita's rules are a 2D game's approximation, MatSim's must match measured physics, with
grounded constants and an analytic check.

| | Principle | Where this plan meets it | Gap found 2026-11-04 → change |
|---|---|---|---|
| N1 | Local rules are the core | the Margolus CA (§6a), P1 built | Noita's liquid rules (down, diagonal, sideways) never push water *up*. Realistic water must: communicating vessels, siphons, fountains. **→ P1c, the pressure rule, built (§6d).** |
| N2 | Every material has a phase and properties | phases + a material table; repose angles grounded | Viscosity is missing (lava, oil, mud flow at different rates), and so is per-material density. **→ P4 gets density and viscosity rows, grounded like the repose angles.** |
| N3 | Materials react | "later, their own plans" | That's too vague for Noita's defining feature. **→ named phase P8 (heat, fire, reactions) on this grid, with the reaction table as data.** |
| N4 | Matter is conserved | R5, exact integer ledger, in-flight particles counted | none; holds |
| N5/N6 | Bodies are material; one shared grid, two-way coupling | P4: debris displaces cells | The world itself is only a read-only solid mask. In Noita the terrain *is* the simulated material: dig it and it falls, dissolve it, burn it. **→ P4b (new): disturbed world voxels become MatSim matter (undercut sand and gravel terrain slumps), and matter at rest re-bakes into voxels (`NoitaGapAnalysis` Tier 1).** |
| N7 | Only simulate what changes; the world keeps going off-screen | sleep (exact); F2 "deferred, never skipped" | none in design; P5 proves it |
| N8 | Readable, consistent rules | determinism, A/B knobs | none |
| N9 | Creatures and objects are material | not in scope | stays with `NoitaGapAnalysis` Tier 4 |
| real | **Speeds are physical and do not depend on cell size** | — | **The biggest realism gap.** Matter moves at most one cell per tick, so a fixed 60 Hz tick caps flow at h × 60 m/s, from 60 m/s at 1 m down to 6.7 m/s at 1/9 m. Measured: the dam-break front runs 46.7 / 4.5 / 0.81 m/s by cell size, and U-tube levelling takes 2.6 / 18.5 / 81 s (§6d). It also breaks R2: cell size can't be a tier while it changes outcomes. **→ P1d (new, owner decision): ticks per second ∝ cells per metre, the CFL condition every grid fluid solver obeys, so the cap is the same in m/s at every size, then measure what is left. Fast matter as P3 particles is Noita's answer for momentum and stays.** |
| real | Hydrostatics | — | built, §6d |

---

## 1. Goal, scope, and the rules it inherits

**Goal.** A **material simulation (MatSim)**: voxels with a *phase* that move by local rules on the
GPU, in Noita's model. Fast cells leave the grid as ballistic particles and re-enter when they land.
Liquids, powders, gases and (later) fire share one grid, so they interact. It has to scale from a
spilled bucket to world-sized play, with quality concentrated around the camera.

**Scope order:**
1. powders and liquids, the core;
2. liquids as *materials* (oil, lava, acid, blood, thrown water);
3. gases;
4. heat and fire;
5. reactions.

Water *bodies* (ponds, lakes, rivers, sea) stay WaterCore's. Whether MatSim can also carry a large
body cheaply is a **question this plan measures** (§5 M3), not an assumption.

**Inherited rules — binding from day one:**
- **R1 — Locality (WaterCore §30.0b).**
  - *Where things happen* decides what is simulated; *camera distance* decides cost and detail.
  - The camera never changes an outcome.
  - Distance is one function of world position, with continuous fall-off.
- **R2 — Tiers (`PerformanceTiers.md` rule 1).**
  - Tiers change cost and appearance, never the outcome: where matter ends up, how much there is,
    what you can walk on.
  - Reported as a whole frame, CPU and GPU.
  - The design carries a tier table.
- **R3 — Existence never follows the camera** (the water camera-invariant, violated three times
  before). Check it by observation: the camera-walk probe.
- **R4 — Chunks invisible.** Behaviour is a function of world position and state, never of chunk
  identity. Pin it with a chunked-vs-whole-region equality test.
- **R5 — Conservation is exact.**
  - Integer fill units, not floats. This is the lesson of WaterCore §50.6d, where float pressure
    caused a runaway.
  - In-flight particles count.
  - A ledger route proves it.
- **R6 — No voxel floats up as if gravity were off** (owner 2026-10-25), except a material
  defined to (gas).
- **R7 — A second kind of simulated water needs the owner's explicit approval** (WaterCore §0a).
  MatSim liquids are experimental until that approval. They never write WaterCore's state.
- **R8 — Red before green; small one-variable rigs with a control; stress phase; auditor.**
  `FeatureDesignKeys.md` gate per phase.

## 2. Coexistence with WaterCore — the separation contract

**Now (P0–P6): no shared code paths, only shared read-only data.**

| Shared thing | MatSim's access | Note |
|---|---|---|
| Micro occupancy pool (`PackedOccupancyPool`) | read | the same solids view debris, lighting and WaterCore use (WaterCore P5) |
| WaterCore's surface-field *layout* (`SurfaceColumn`, 16 floats) | copied format, own buffers | lets MatSim liquids be shaded by the owner-signed water shading **without touching WaterCore** |
| `WaterLocality` distance function (§30.0b L1) | read / call | one viewer-distance function for both systems (R1.3) |
| `water_common.glsl` shading | include, unmodified | a shading change needs the water session's agreement |
| GPU debris (`GpuParticlePhysics`) | read poses (P4) | displacement source; the solver is not modified (any `solver_*.comp` change = settle bench) |

**Never, until the marriage design:**
- MatSim writes no WaterCore buffer, record, span or volume.
- WaterCore does not read MatSim.
- Where both could be present at the same place, the lab rig keeps them apart (WaterCore off in
  MatSim rigs, except the side-by-side control rig).

**The marriage, designed later (P7) at four named seams, so the design keeps them possible:**
- **(a) Mass hand-off.** MatSim liquid reaching a WaterCore body transfers mass into it, and
  WaterCore spray landing on dry ground becomes MatSim liquid. The ledgers stay exact across both.
- **(b) The record as the common currency.** WaterCore's spans and body records are the at-rest
  truth for water. MatSim liquids at rest get a record of the same shape.
- **(c) One disturbance list.** WaterCore's S1 disturbance centres and MatSim's active regions
  come from the same events.
- **(d) One wave-appearance layer** (§4), shared by both, owned jointly.

## 3. The scale model — quality by place, cost by camera

Four **forms** (matching WaterCore §51's levels where they overlap). One matter: the form is only
how much it is simulated and how it is drawn.

| Form | Where | What runs | Outcome guarantee (R1, R2) |
|---|---|---|---|
| **F1 Live** | where things happen (an edit, blast, spill, moving body) AND within the near band of the camera | full-rate CA at the fine cell (1/3 m default; §6 D3) plus the particle layer | reference |
| **F2 Deferred** | where things happen but beyond the near band | the **same** CA at the **same** cell size, stepped in batches later: it accumulates a step debt and catches up within a bounded lag | **identical end state.** Same steps, same deterministic order, only later. *Deferred, never skipped.* |
| **F3 Resting** | anywhere nothing moves | nothing. Asleep in the grid (MatSim) or the record (water bodies) | trivially identical |
| **F4 Far look** | beyond the far band | appearance only (a skin, impostors) drawn from F3's state | draws state; never makes or moves it |

**Why F2 matters.** A CA's end state depends on its step sequence: a sand pile stepped at quarter
rate is a *different pile*. So a far region can't simply run "coarser" or "slower" without breaking
R2. It must run the same steps, deterministically, at its own pace. The plan's open question is how
long a lag the player can never observe. A region the camera approaches must finish its catch-up
before it enters the near band; that's where camera motion comes in:

**Camera-motion look-ahead.** The near band is a shape stretched along the camera's velocity, not
a disc around its position. Regions ahead of the camera get:
- catch-up priority;
- early GPU residency;
- early wake.

That hides WaterCore's measured 179–517 ms patch-wake stall class (§51.2) before it's ever on
screen. It changes cost only (R1), never what runs.

### 3a. Cell size is switchable at runtime: 1, 1/3, 1/9 m (owner, 2026-11-03)

The owner wants to switch between the three engine scales live, for both liquids and powders.

**Design:**
- **Fill is integer and nests exactly.** One fill unit = a fixed fraction of a 1/9 m cell. A full
  1/9 cell = U units, a 1/3 cell = 27·U, a 1 m cell = 729·U. For example U = 256 gives a 1 m cell
  186,624 units, well within 32 bits.
- **Switching is a conserving remap:**
  - **Split** (coarse → fine) distributes a cell's units over its 27 children: bottom-up for
    liquid and powder, so a half-full 1 m cell becomes the lower half of its 1/3 cells; top-down
    for gas.
  - **Merge** (fine → coarse) sums the children's units.
  - The ledger total is bit-identical before and after: a red/green test.
  - In-flight particles are untouched.
- **Scope of a switch:** in the lab it's one global setting per active region (A/B). A per-region
  mix (fine near the camera, coarse beyond) is the same remap at a boundary, and is the candidate
  shortcut §3 calls an exception.
- **Honest consequence:** a different cell size runs a different simulation, so a switch changes
  the trajectory and can change the end state (a pile's shape). Mass is always exact. So the
  switch is a **lab knob and measurement axis** (every M2 rung runs at all three sizes). Using it
  as a quality tier needs the owner's exception (R2).

**A coarse-cell form (1 m cells, 27→1 aggregation) is deliberately NOT a form.** It would change
outcomes (R2). It exists only as an owner-approved exception, if measurement (§5) proves F2's
catch-up can't keep up at some scale. That is the "when we need to take shortcuts" decision, made
with numbers and named as an exception, never silently.

**Knobs** (the tier table's rows, all distances from world position, never chunk counts):
- near band radius and look-ahead length;
- far band;
- F2 maximum lag and catch-up budget (ms per frame);
- particle pool size;
- active-region memory cap;
- surface-detail fall-off.

The tier table (`PerformanceTiers.md` §4a) gets filled in from §5's measurements.

### 3b. Sand and other powders — the same machine, different rules

The owner's concern: even 1/3 m seems large for sand. Two things make it workable, and both are
the same answers as for water:

1. **A cell is a parcel, not a grain** — as in Noita, where a "sand pixel" is far larger than a
   grain. The look of grains comes from shading and texture (the existing Sand and Gravel
   materials), and the *surface* comes from fill.
   - Powder cells carry a fill fraction like water, so a pile's surface sits at sub-cell height.
   - A slope at 1/3 m cells can be drawn as a smooth slope, not a staircase, from a surface field
     (the same idea as WaterCore's surface field, with a sand shading path).
   - At rest, settled sand re-bakes into real static subcubes or microcubes (`NoitaGapAnalysis`
     Tier 1). The world already renders 1/3 and 1/9 natively, and sand at rest is then ordinary
     world.
2. **Sand is cheaper per unit volume than water, so 1/9 near the camera may be affordable.** A
   disturbed water body moves through its depth. A sand pile moves only in a thin sliding layer
   on its surface, then sleeps quickly.
   - The active set is the skin, not the volume, so the cost falls on far fewer cells.
   - This is a hypothesis: M2's sand rungs measure the active-cell count against time.

**What is shared vs per-material:**
- **Shared:** the grid, the integer fill, the passes, sleep, the particle hand-off (a blast throws
  sand as particles that land as fill), deferred stepping (§3) and the resolution switch (§3a).
- **Per material:**
  - powders don't level; they stop at an **angle of repose**, a grounded per-material value from
    the granular-flow literature, cited at P1's design-check, never invented;
  - powders have no pressure;
  - wetness changes them (wet sand is cohesive and holds steeper; P4 with water);
  - powders compact.

**The resolution choice may differ by material:** sand at 1/9 near the camera, water at 1/3. The
switch is per material class as well as global, and the remap rules above apply per class.

## 4. Waves grounded in the world (the appearance layer)

**The rule (owner, 2026-11-03, corrected):** *every wave has to make sense in the world's shape.*
On an ocean with a coastline, the waves come in against the shore. Waves "floating" at an angle to
a jagged coastline are wrong.

The physics behind it: waves slow in shallower water, so crests **refract** until they run nearly
parallel to the depth contours. This is why waves meet a beach almost head-on, wrap round
headlands, fan into bays, and fade behind rocks and in sheltered coves. So **wave direction, crest
shape, height and breaking are derived from the world's geometry** — the depth and the distance
and direction to the shore, both built from the voxels:
- offshore, a direction may come from the wind;
- approaching land, depth turns the crests to the coastline;
- waves steepen in the shallows and break along the real shore;
- waves never appear inside or through a solid voxel.

A single global wave pattern is ruled out by this, not just tuned. It is what the owner rejected in
WaterCore §45.3 ("a pattern ignores the shores"); Gerstner waves clipping walls is the open defect
in `WaterRethink.md` §1.4.

| Source | Where its truth comes from | Behaviour |
|---|---|---|
| **Disturbances** (splash, wading, a body entering) | near: the simulation's own surface (WaterCore ripples; MatSim surface motion). Far: rings drawn from the disturbance list | rings spread from the event and stop at solids |
| **Wind** | `WindSystem` (the engine's wind — today *not connected to water*, `WaterRethink.md` §1.4) × **fetch**, the open-water distance upwind across *this* body's actual shape; WaterCore already bakes SMB fetch and Cox–Munk per body | waves only where there is fetch: a sheltered cove is calm, a long lake builds chop downwind |
| **Current** | the record's discharge and flow direction (rivers); MatSim cell flow bias | ripples and foam advected along the flow, never against it |
| **Shore and depth: the governing input** | a **shoreline distance field plus depth** built from the occupancy (distance and direction to the nearest shore, water depth per column) | crests **refract toward the depth contours** (Snell's law with the shallow-water wave speed √(g·d)), so they arrive square to the local coastline; they shoal (grow) in the shallows, break along the real shore at the depth-limited ratio (McCowan, already in `water_common.glsl`), wrap round headlands, and are sheltered behind obstacles; **zero amplitude inside or through a solid voxel**, by construction, from the same field |

Waves are appearance: they move no mass. Simulated motion stays the simulation's. This layer is the
**same need** as WaterCore's track B, W2 and W6 (the shore band, far water). So it's designed once,
**with the water session**, as seam (d). Until then, MatSim prototypes it only on MatSim liquids in
the lab. Acceptance:
- **a shore-alignment metric:** along a jagged test coastline, the angle between each crest and
  the local shore contour, measured at fixed distances from shore. It must fall toward parallel as
  depth decreases, whatever the offshore wind direction (red: today's single-direction pattern);
- **a wave-in-solid metric of zero:** sample the wave displacement at every cell the occupancy marks
  solid, at every distance band;
- **a fetch test:** the same wind on a cove and an open lake must differ the way fetch predicts;
- **the owner's eye in motion.**

## 5. The measurement program — where it breaks

Three kinds of measurement, scheduled around the other sessions (§7).

### M0 — desk baseline (now; no builds, no runs)
Collect the numbers already recorded, into one table:
- WaterCore's §27.20 limits (an awake pond fits 2 ms to ~25×25×1 m) and §51.2 (the 40×40 m patch,
  3.5–3.9 s of GPU over 40 s; the wake stall);
- GPU debris (`DynamicVoxelPhysics.md`: fixed-overhead-dominated up to 10 k bodies);
- the render-density result (`ContinuousLodPlan.md` §7b M4);
- the shadow costs (`NearShadowCascade.md`);
- the tier budgets.

Output: the **budget left for MatSim per tier at a realistic scene** (laptop RTX 1000 Ada and, if
available, the RTX 4090 target), with the gaps where nothing was measured stated.

### M1 — headroom runs (needs an engine; only when the WorldForge session is idle)
- Whole-frame CPU and GPU at the realistic scenes WorldForge produces: a town, forest and lake;
  Release; fixed poses.
- These give the actual free GPU ms and VRAM per tier, against which every MatSim number is read.
- No new code. Uses existing routes (`engine_timing`, `gpu_timing`).

### M2 — the scale ladder (after P1 exists; in the lab worktree)
The ladder, each rung asserting R5 (mass exact) and R3/R1 (camera walk, deferred = live end state):

| Rung | Scenario | Measures |
|---|---|---|
| 1 | a bucket (~0.02 m³) poured from a ledge | particle ↔ grid handshake, arc, splash |
| 2 | a 4×4 m puddle / crater fill | spread, sleep latency |
| 3 | a one-chunk basin, dam break, U-tube | front speed vs Ritter √(g·h₀); leveling time; vs WaterCore control |
| 4 | a 40×40×3 m patch (the §51 patch) | ms per tick awake, asleep, after a 2 m splash; vs WaterCore §51.2 |
| 5 | N simultaneous active regions (1 → 64) | the fixed cost per region; the dispatch-count cost |
| 6 | a 200 m body of MatSim liquid, F2 deferred, camera flying across | catch-up lag vs look-ahead; does F2 keep up? **The shortcut decision's evidence** |
| 7 | 1 km (record-only plus F4) | cost independent of size? |
| S1 | sand poured from a ledge onto a flat floor | the pile's angle vs the grounded angle of repose; active cells against time (the skin hypothesis, §3b) |
| S2 | a sand bank undercut (a blast at its base) | slump; mass exact; sleep latency |
| S3 | sand + water (P4) | wet sand holds steeper; water drains through or around |

**Every rung runs at all three cell sizes (1, 1/3, 1/9 m, §3a),** recording cost and the look at
each. **Hardware:** the laptop (RTX 1000 Ada) is the measurement machine for now; owner,
2026-11-03: "testing on a less capable system is still worthwhile data". The RTX 4090 rows stay
empty until it's available.

**Breakdown points to find and record:**
- cells per GPU-ms;
- the active-cell count where a tier's share is exceeded;
- VRAM per active cell;
- the F2 catch-up rate vs camera speed (sprint, fast travel);
- the particle-pool exhaustion rate in a blast;
- the readback and dispatch fixed costs.

### M3 — the "can MatSim carry a large body?" question
Rungs 4, 6 and 7 compared side by side with WaterCore at the same sizes. This answers whether the
marriage puts MatSim *inside* WaterCore's patches (a cheaper form-1 solver), *beside* it (spills
and materials only), or neither. Numbers first, owner decides.

## 6. Phases (each: design-check → red test → build → stress → auditor → owner sees it in motion)

| Phase | What | Gate / evidence |
|---|---|---|
| **P0** | this plan; M0 desk table; owner decisions §8 | owner sign-off |
| **P1** | the CA core on the GPU: phases static/powder/liquid; checkerboard or Margolus passes; integer fill; per-brick sleep; solids from occupancy. Lab world, one chunk | mass exact; a still basin fully asleep; R4 seam-equality test; determinism (two runs bit-identical) |
| **P1c** (§0b, built §6d) | pressure: a hydrostatic head field and pressure-driven flow | U-tube and four-vessel levels = the volume level at every cell size; sleep exact; GPU bit-exact |
| **P1d** (§0b, owner decision) | speed: ticks per second ∝ cells per metre (CFL), so flow speeds are the same in m/s at every cell size | dam-break front vs Ritter and U-tube levelling time agree across 1, 1/3, 1/9 m; whole-frame cost of the extra ticks per tier |
| **P2** | the look: MatSim writes the surface-field layout; shaded by `water_common.glsl`; raw-cell debug view | owner compares raw vs shaded, MatSim 1/3 m vs WaterCore 1/9 m, same pose |
| **P3** | momentum: the particle layer (eject over a threshold, land into fill) | pour arcs, a stone throws a crown; mass exact including in flight |
| **P4** | materials: oil on water (density swap), sand sinking in water, per-material density and viscosity (grounded), sand plus water, a GPU debris body displacing cells | 100 % of columns layered; displaced volume = submerged volume; lava/oil/water flow rates in the order their viscosities predict |
| **P4b** (§0b) | the world is material: disturbed world voxels (undercut sand/gravel terrain, a blast crater rim) become MatSim matter; matter at rest re-bakes into static voxels | ledger exact across world ↔ MatSim; an undercut bank slumps to its repose angle; a re-baked pile renders and collides as world |
| **P5** | scale: F2 deferred stepping, look-ahead, residency; M2 ladder | deferred end state = live end state (bit-exact); the tier table filled |
| **P6** | grounded waves (§4) on MatSim liquids, in the lab | wave-in-solid = 0; the fetch test; owner's eye |
| **P7** | **marriage design** with the water session: seams (a)–(d); M3's answer | joint design-check; owner approval (R7) |
| **P8** (§0b) | gases, heat, fire, reactions (NoitaGapAnalysis Tier 3) on the same grid; the reaction table is data | its own design-check; grounded ignition/boiling points; mass exact through every reaction |

### 6a. P1 detailed design (2026-11-03) — the CA core

**Split:**
- **P1a** — a CPU reference core and its unit tests. Pure C++, headless, and the source of truth.
- **P1b** — the GPU port, bit-exact against P1a, plus the lab API and the raw-cell view.

**Grid:**
- An axis-aligned **lab box** in world space, at `cells_per_m` ∈ {1, 3, 9}, i.e. a cell size h of
  1, 1/3 or 1/9 m.
- One `uint32` per cell: `material` (8 bits) | `fill` (24 bits).
- Material 0 = empty with fill 0; material 255 = SOLID.
- **Fill units nest exactly** (§3a): U = 256 per full 1/9 cell, so FULL(h) = 256·(9h)³. That is
  256 / 6,912 / 186,624 for 1/9 / 1/3 / 1 m.
- The solid mask comes from a world-position predicate. In P1a it's a test callback; in P1b it's
  the shared occupancy pool (`phxOccupancyState` / CPU `packedPoolOccupancyState`).
- A cell is SOLID if **any** micro inside it is solid, so liquid never enters matter. At 1 m cells
  a thin sub-voxel slab blocks the whole cell. That's stated, and it's part of why behaviour
  differs by cell size (§3a).
- The box's outside is solid: a closed lab.

**Update — a Margolus block cellular automaton (2×2×2 blocks):**
- One thread updates one block. It **reads** the previous grid anywhere and **writes only its own
  8 cells** of the next grid (ping-pong buffers).
- **Why it is order-independent:** the 8 cells each block writes are disjoint from every other
  block's, and every read comes from the previous, read-only grid. So the result can't depend on
  thread or block evaluation order. No atomics touch cells.
- **Mass is conserved by construction:** every rule moves fill between the block's own cells.
- The block offset alternates (0,0,0) / (1,1,1) by tick, so material crosses block boundaries.
- Block parity is taken from **world** cell coordinates.
- Every rule reads at most one cell beyond its block.
- Integer math only. Any choice (remainder order) uses a hash of (world cell, tick, seed).

**Rules per block, in order:**
1. **Gravity** (liquid and powder). In each of the 4 columns, move fill from top to bottom:
   `min(top.fill, FULL − bottom.fill)`, if the bottom is empty or the same material.
2. **Liquid level.**
   - In each layer, take the cells that are this liquid or empty, not solid, and **supported**
     (the cell below is full or solid; for the bottom layer that cell is read from the previous
     grid, below the block).
   - Equalise their fill only when max − min > 1 unit. That deadband gives an exact rest state.
   - Remainders are handed out in hash order.
3. **Powder slide (angle of repose).**
   - Take a powder column in the block whose surface (fill heights in units of h) stands above a
     neighbouring column's by more than tan(θ) × the horizontal distance (1 cell, or √2 for a
     diagonal).
   - Move just enough fill diagonally down to bring the drop to tan(θ). No randomness.
   - **θ, grounded:** dry sand 34°, wet sand 45° (P4), snow 38°, ashes 40°, crushed gravel 45°.
     Source: Wikipedia *Angle of repose*, table citing T. J. Glover, *Pocket Ref* (Sequoia, 1995).
     The snow value cites Rikkers & Rodriguez, *Telluride Magazine* 2009. Fetched 2026-11-03.
   - The value lives in the MatSim material table, not as a shader literal.

**Sleep (cost only):**
- Each block reports whether **any rule was eligible** to move anything.
- Bricks are 8³ cells, **world-aligned**.
- A brick whose own blocks and whose 26 neighbour bricks reported nothing eligible on both offsets
  is skipped.
- The one-brick halo covers the rules' one-cell reach.
- Skipping is exact: a block with no eligible rule writes back its own input. So sleep changes
  cost, never the result, and is pinned by a test.

**Ledger:** total fill per material as `uint64` (per-workgroup partials, summed on the host).

**Resolution remap (§3a):** on the host, from a readback. Split is bottom-up; merge is a sum.

**Edits under the simulation (P1 scope):**
- `resolidify` re-reads the solid mask on demand.
- A cell that turns solid while holding fill keeps that fill as **trapped** and frozen. It stays in
  the ledger and is reported as `trapped_units`, never deleted.
- Real displacement into neighbouring cells is P4.

**Unknown occupancy:**
- If the pool can't answer for a cell (UNKNOWN), the cell is treated as SOLID, so nothing leaks
  into unknown space.
- `unknown_cells` is counted and reported every tick, loudly.
- `create` **refuses** a box that isn't fully known at creation.
- ⚠️ The pool's coverage follows the viewer (32×16×32 chunks), so at scale "unknown" would make
  behaviour camera-dependent (R3). That's P5's problem to solve with MatSim's own solid copy of
  its region. In P1 the lab box sits inside one resident chunk next to the camera.

**Known limitation, stated up front:** rule 2 is diffusion, so a wide basin levels in a time that
grows roughly with width² (the old CA's flaw, §8). P1 **measures** this, and the fixes are
separate decisions (P3 particles; a later pressure or heightfield rule). A structural fact: under
Margolus, matter moves at most one cell per tick, so no front can exceed h × 60 m/s at the 60 Hz
tick. That's 6.7 m/s at 1/9 m — about the Ritter dam-break front speed 2√(g·h₀) = 6.3 m/s for
h₀ = 1 m. So at 1/9 m, the grid itself caps how fast a flood can travel.

**Lab API** (`POST /api/matsim/<action>`; distances in world m; omitted = unchanged; every mutating
response echoes `ledger`, `cells_per_m` and `box`):

| Action | Fields | Behaviour on bad input |
|---|---|---|
| `create` | `min{x,y,z}`, `size{x,y,z}` (m), `cells_per_m` | `cells_per_m` ∉ {1, 3, 9}, cells > 16.7 M, or the box not fully known: **rejected** with the reason (no clamping, no partial create) |
| `fill_box` | `material` (name), `min`, `size`, `fraction` 0–1 | units written, and solid cells skipped, both echoed |
| `step` | `n` 0–100,000 | ticks run and eligible blocks echoed |
| `realtime` | bool | — |
| `set_cell_size` | `cells_per_m` | ledger echoed before and after (must match) |
| `resolidify` | — | — |
| `ledger`, `stats` | — | eligible blocks, `unknown_cells`, `trapped_units`, ms per tick |
| `view` | raw cells on/off | — |
| `clear` | — | — |

No existing default changes. MatSim does nothing until `create`, pinned by
`MatSimDefaults.InertUntilCreated`.

**Raw-cell view (P1b):**
- An expand pass writes exposed cell faces in the existing `DynamicSubcubeInstanceData` format,
  drawn by the dynamic-voxel pipeline with its own buffers.
- A liquid or powder cell is drawn **at its fill height** (scaled in y), not as a full cube, so the
  raw view is honest about where the matter is.
- Colour comes from the material's texture (Water/Sand).
- This is P2's deliberately ugly "blob" baseline.

**P1 tests (written first; each prints its numbers when it fails):**

| Test | Layer | Fails with |
|---|---|---|
| `MatSimLedger.ExactOver10kTicks` (dam break, both phases) | L2 | per-material Δ units at the first tick it is non-zero |
| `MatSimRemap.RoundTripIsExact` (1 → 3 → 9 → 3 → 1) and `SplitIsBottomUp` | L2 | Δ units; the first cell out of place |
| `MatSimSleep.StillBasinReachesZeroEligible` | L2 | eligible blocks at tick N |
| `MatSimSleep.SkippingEqualsDense` (the same scene stepped with sleep on vs off) | L2 | the first differing world cell, and the tick |
| `MatSimDeterminism.TwoRunsBitIdentical` | L2 | the first differing cell |
| `MatSimWorldAlignment.ShiftedBoxesAgreeInWorldSpace` (the same scene in boxes offset by an even cell count — the `FloraMarginTest` shape) | L2 | the first differing world cell |
| `MatSimPowder.PileStandsAtRepose` (sand poured onto a floor) | L2 | the measured pile slope vs tan 34° ± one cell over its run |
| `MatSimDefaults.InertUntilCreated` | L1 | — |
| P1b `MatSimGpuParity.GpuEqualsCpuAfterNTicks` (integration, Release) | L4 | the first differing cell |
| P1b live rig: ledger, sleep and unknown cells over the API; dam-break and U-tube times recorded | L4 | — |

**Controls:**
- The ledger test runs once with a test-only leak switch (one rule drops one unit). It must report
  exactly that Δ, which proves the ledger can see a loss.
- The parity and alignment comparators run once against a deliberately one-tick-shifted run, which
  must differ.
- The sleep test runs once with a perturbation, which must show eligible > 0.

**Lab rig:**
- One flat-stone chunk at world chunk (0, 0, 0), built by direct voxel placement (a hand-placed
  test rig, stated; not generator output).
- A 16 × 8 × 16 m lab box inside that chunk: a stone floor, a dam wall, a U-tube with a 1 m
  connector, and a ledge.
- One variable per run: the cell size or the material.
- **Predictions, written before the run:**
  - the ledger is exact at every cell size;
  - the still basin sleeps within 10 s of simulated time;
  - the sand pile's slope is tan 34° within one cell;
  - the dam-break front is **slower** than Ritter at every cell size (diffusion, plus the
    one-cell-per-tick cap at 1/9);
  - U-tube levelling takes longer than 10 s at 1/3 m.

**Rig vs shipped:**
- Nothing ships. MatSim is lab-only, and WaterCore and the old water are off in the rig.
- The box edges are solid walls (the real world is open), so levelling near the edges is
  optimistic.
- The 60 Hz tick is the lab default. A different rate changes front speeds proportionally under the
  one-cell cap.

**Performance tiers (P1):**
- Not fitted yet. The knobs exist from the start: box size, cells per m, tick rate, sleep, and the
  later deferred stepping.
- P1b reports GPU ms per tick for an awake and an asleep box at all three cell sizes, plus the
  whole frame (`engine_timing`, `gpu_timing`) on the laptop.

### 6b. P1a built (2026-11-03) — the CPU reference core

**Code:** `engine/include/core/matsim/MatSimCore.h`, `engine/src/core/matsim/MatSimCore.cpp`
(pure C++, no engine dependencies, nothing calls it yet). Tests: `tests/core/MatSimCoreTest.cpp`.

**Red before green:** all tests were written first and run against a stub; 14 of 15 failed
(`InertUntilCreated` passes on a stub by definition). The ledge test (below) was shown red against
a powders-only slide: 0 % of the water left the ledge.

**Audit (an independent read-only review, 2026-11-03) found one defect, now fixed red→green:**
- **Matter leaked through edge-touching walls.** Level grouped every supported cell of a block
  layer, and slide moved fill between diagonal cells, so a staircased diagonal wall leaked: up to
  65 % of the water crossed it. `MatSimWalls.StaircaseDiagonalWallIsWatertight` (all three cell
  sizes, water and sand) was red. The fix: level runs per **face-connected** group, and slide needs a
  face-connected path of non-solid cells inside the block. Mass was exact throughout; the defect
  was matter crossing a wall.
- **Tests the audit judged vacuous or weak, now tightened:**
  - the repose tolerance (±1 cell) couldn't tell sand from snow. It is now ±0.03 on the median
    per-cell flank drop, for sand, snow and gravel, and the slopes must increase in that order;
  - one unknown-cell assertion could never fail. It now lifts the fog and checks nothing went in;
  - sleep vs dense now also runs after an edit to a sleeping corner and through a resolidify that
    raises and removes a wall mid-run;
  - the trapped split/merge paths are now round-tripped (`TrappedContentsSurviveRoundTrip`).
- **The premise behind exact sleep, now written down:** a block that changed nothing two ticks ago
  changes nothing now, although the tick hash differs. It holds because no current rule's
  *eligibility* depends on the hash (gravity lowers height, slide lowers the column-height variance,
  level's test is max − min > 1); the hash only orders remainders and pairs. **A future hashed or
  probabilistic rule that can be a no-op on some ticks would silently break sleep**, so it must keep
  that property or drop the two-tick sleep test.
- **Semantics noted, not changed:** a split frees trapped contents into the newly open children of
  a solid coarse cell (the finer grid shows room the coarse one hid). `tick_` restarts at 0 on
  `create`, so two boxes created at different times are not in phase. P1b must decide whether the
  tick is world time.

**Two departures from §6a's design, both found while building it:**
1. **Rule 3 (slide) applies to liquids too, at θ = 0.** Rule 2 only levels *supported* cells (so
   no floating sheets). That means water on a ledge's lip has nowhere to go: it never pours off.
   Slide with tan θ = 0 is Noita's "diagonal down" rule, and integer halving gives it a natural
   1-unit deadband. Pinned by `MatSimLiquid.PoursOffLedge` (red: 0 % → green: 100 % below the
   ledge in 600 ticks).
2. **"Asleep" means nothing eligible on two consecutive ticks**, one per block offset. One quiet
   tick says nothing about the other offset's blocks.

**Measured** (standalone CPU harness, Release `/O2`; numbers from the test printouts):

| Test | Result |
|---|---|
| `MatSimLedger.ExactOver10kTicks` (water and sand dam break; 1 and 1/3 m × 10k ticks, 1/9 m × 3k) | exact on every tick; the control leak of 1 unit is seen as exactly −1 |
| `MatSimRemap.RoundTripIsExact` (1 → 3 → 9 → 3 → 1, pillar + 1/9 m shelf) | ledger and every cell bit-identical |
| `MatSimSleep.StillBasinReachesZeroEligible` (4 × 2 × 4 m, uneven) | asleep at tick 228 = **3.8 s** (prediction: < 10 s); 0 blocks evaluated after; a dropped cell wakes it |
| `MatSimSleep.SkippingEqualsDense` (40 × 9 × 40 cells, 1500 ticks, then a drop and two resolidifies) | identical every tick; 1.96 M block evaluations skipped |
| `MatSimDeterminism.TwoRunsBitIdentical` | identical; a different seed differs (control) |
| `MatSimWorldAlignment.ShiftedBoxesAgreeInWorldSpace` (even and odd offsets, negative coordinates) | identical every tick for 600 ticks; a one-tick shift is seen (control) |
| `MatSimPowder.PileStandsAtRepose` (4 × 12 × 4-cell column at 1/3 m; median per-cell flank drop) | sand **0.675** (tan 34° = 0.675), snow **0.781** (tan 38° = 0.781), gravel **1.000** (tan 45°); at rest by tick 87–111. Peak-to-toe averages are lower (0.664 / 0.692) because the crest is flat and the toe cell partial; the first one or two steps below the crest are steeper (0.95–1.0), from the 2-cell block window |
| `MatSimWalls.StaircaseDiagonalWallIsWatertight` | 0 units cross, all three cell sizes, water and sand (red before the fix: up to 1.13 M of 1.74 M units) |
| `MatSimStress.RandomScenesLedgerSleepAndSwitch` (6 seeds × 720 ticks; water, sand, gravel; live switches 9/3/1) | ledger exact and sleep == dense on every tick; **24 of 36 switches refused** |
| `MatSimStress.DegenerateInputsAndChurn` | NaN, out-of-box and over-full refused; 1000 create–clear cycles exact |

**Finding — merges refuse once materials mix.** A merge refuses when a coarse cell's children hold
two materials; in the mixed stress scenes that was 2 of every 3 switches. Refusal is exact (nothing
changes), but a live coarse switch over mixed matter needs a rule — likely P4's layered cells. That
is an owner decision, not something to fix silently.

**Left for P1b:** the dam-break front speed and U-tube times (§6a predictions) are live-rig
measurements. CPU ms per tick is not recorded as a performance number: the CPU core is the
reference, not the runtime.

### 6c. P1b built (2026-11-03) — the GPU port, the lab API, the raw-cell view, the live rig

**Code:**
- `shaders/matsim_block.comp` (the rules, a line-for-line port of `MatSimCore::evalBlock`) and
  `shaders/matsim_dilate.comp` (the sleep mask); both are in `build_shaders.bat`.
- `engine/*/core/matsim/MatSimGpu.*` steps a device copy of the core's stepping state (cells, tick,
  both sleep mark sets): `upload → step(n) → download`.
- `engine/*/core/matsim/MatSimLab.*` is the lab service.
- Glue: `/api/matsim/<action>` in `EngineAPIServer.cpp`, `registerMatSimCommands` and the
  per-frame update in `Application.cpp`, `renderMatSimDebug` in `RenderCoordinator.cpp`.
- Rig: `tools/matsim_lab.py`, on the `MatSimLab` project (port 8120).
- Tests: `tests/integration/MatSimGpuParityTest.cpp` (9 tests).

**How the port stays exact:**
- The CPU core owns the state and every edit: fill, resolidify, cell-size switch, trapped contents,
  the expected ledger. The GPU only steps.
- The kernel is integer-only and 32-bit. The slide thresholds come from the CPU's own
  `slideThreshold()` through a material table. The tick's hash term is the CPU's `tickMix()`, sent as
  a push constant, so there is no int64 in the shader.
- The remainder sort is an insertion sort under the same strict order as the CPU's `std::sort`.
- Cells ping-pong between 2 buffers and marks rotate over 3, so a tick binds one of 6 descriptor sets.
  There is one submission and one fence per 256 ticks, and timestamps give GPU ms.

**API, beyond §6a's table:**
- `create` takes `backend` (`gpu` by default when a device exists, else `cpu`), `seed` and `sleep`.
- `parity {n}` (live L4): runs the same n ticks on the CPU reference and on the device and compares
  them cell by cell, plus the sleep marks and the per-tick stats. It refuses if the step did not
  actually run on the device.
- `slices {axis, min?, size?}` (read-only): m³ per slice per material. The front and level
  measurements are read from it.
- A refusal changes nothing (a live test confirms this). Every response echoes `ledger`
  (units, expected, exact), `cells_per_m` and `box`.

**Departures from §6a, stated:**
1. **Ledger:** computed on the host after a download, not from per-workgroup partials. That's cheap
   at lab sizes; P5 revisits it.
2. **Raw-cell view:** built on the CPU from a download and fed to the dynamic-voxel pipeline. There is
   no GPU expand pass. At 1/9 m that costs the whole frame (see below), so the view is a debug tool;
   the GPU expand pass is P2's.
3. **Water is drawn with the `Ice` texture**, because the voxel shader has no Water material. It's
   the same stand-in the FLIP debug draw uses.
4. **Live parity at 1/9 m runs 30 ticks, not 300.** The CPU reference runs on the engine's main
   thread, and in Debug 300 ticks of 1.49 M cells outlast the API's wait. The integration test
   covers 300 ticks of that box.

**Red before green:**
- **Mutation runs.** Each mutant was compiled into the live `.spv` and run against the suite, then the
  real kernels were restored:
  - *slide without the face-path check:* caught by 5 of 6 tests;
  - *sleep dilation reading only tick −1:* caught by 4 of 6 (stats and cells differ by tick 2).
- **An equivalent mutant.** Flipping the remainder sort's tie-break changes nothing unless two 32-bit
  hashes in one block collide. Its only "catch" came from a test whose own limit was wrong: a
  16 × 16 m basin cannot sleep within 600 ticks. That limit is now 9,000 ticks, and the basin sleeps
  at tick 1,734 (28.9 s), consistent with levelling time growing with width² (4 × 4 m took 3.8 s).
- **The lab test.** `LabRealtimeViewTracksDevice` compares a GPU-backed lab with a CPU-backed lab
  across 120 realtime frames with the view on. It was red (frame 2: 126 vs 271 faces), then green
  after the fix below.

**Audit (independent, read-only, 2026-11-03):**
- **The kernels:** no way found for them to drift from the CPU. Checked: index mapping, rule order,
  hash inputs, integer widths, signed shifts, block parity, buffer-role rotation, barriers, stats
  slots.
- **Lab defects, all fixed:**
  - (1) the realtime view rebuilt from a stale host copy;
  - (2) `parity` could compare CPU against CPU after a failed upload;
  - (3) a failed download was treated as current, losing device ticks;
  - (4) no transfer→host barrier before reading staging;
  - and: an unchecked view-buffer creation.
- **Test gaps, as reported:**
  - the lab layer was untested (now `LabRealtimeViewTracksDevice`);
  - there was no rule-level fault control (the mutation runs above answer it);
  - some branches are unreachable: mixed liquids (only Water exists), equal-hash ties, and dispatch
    rows beyond 1 (2.1 M blocks is at most 32.8 k groups).

**Integration results** (Debug build, this laptop's GPU; all 9 pass):

| Test | Result |
|---|---|
| `GpuEqualsCpuAfterNTicks` (the lab scene at 1, 1/3 and 1/9 m, odd negative origin) | cells, both mark sets, per-tick stats and ledger identical: 1,500 / 1,500 / 900 ticks |
| `ControlShiftedRunDiffers` | a one-tick shift and a different seed are both seen |
| `SleepIsExactAndSkipsOnGpu` | dense on the GPU = CPU; the 16 × 2 × 16 m basin is asleep at 28.9 s and then evaluates 0 blocks |
| `EditsBetweenRunsStayExact` | resolidify plus fills plus switches 3 → 9 → 3 → 1 → 3 between GPU runs = a pure CPU run; ledger exact |
| `StressRandomScenes` (6 seeds; odd extents; n > one chunk) / `DegenerateBoxes` (1³, columns, slabs, n = 0) | identical |
| `LabBoxAtNinthMetre` (144 × 72 × 144 = 1.49 M cells) | identical over 300 ticks |
| `LabRealtimeViewTracksDevice` | the GPU lab's faces = the CPU lab's every frame; ticks and ledger equal |

**GPU ms per tick** (timestamps; the wall clock agrees):

| Box (16 × 8 × 16 m) | dense (sleep off) | awake (sleep on) | asleep |
|---|---|---|---|
| 1 m, 2,048 cells | 0.019 | 0.019 | 0.004 |
| 1/3 m, 55,296 cells | 0.021 | 0.022 | 0.004 |
| 1/9 m, 1.49 M cells | 0.028 | 0.028 | 0.011 |

Live inside the running engine (another workload on the GPU), the same boxes measured 0.04 / 0.12 /
0.26 ms awake.

**Whole frame** (Debug, 1/9 m box in realtime, laptop): view off **71.7 FPS** vs **68.7 FPS** for the
MatSim-idle control, so the simulation costs nothing measurable. View on: **4.7 FPS, 211 ms CPU
frame**, all of it the CPU view (a 1.49 M-cell download and about 85 k faces rebuilt per frame).
- At that frame rate realtime falls behind (4 ticks per frame at most), and it is counted:
  `realtime_ticks_dropped` = 359 after about 7 s.
- The first live run read 0 there, because `Application` clamped `dt` to 0.05 s before the lab saw it,
  which hid the lag. Now the lab gets the real frame time, verified live.

**Live rig** (`tools/matsim_lab.py --run`, evidence `docs/evidence/matsim_p1b_live.json`):
- The rig is hand-placed (`/api/world/fill`), not generator output: the MatSimLab project, a Stone
  slab in chunk (0, 0, 0), and a 16 × 8 × 16 m box at (8, 16, 8) holding the dam, the ledge and the
  U-tube.
- Verified in the world by voxel probes, and in the sim: 0 m³ crossed the dam before the break.
- The lab answered `created: false` on a fresh engine (inert until create).
- All 10 refusal cases were refused with reasons, and the box was unchanged. They were: unknown space
  (the reason names the first unknown cell), cell size 2, 27 M cells, an off-lattice min, material
  "Lava", fraction 2, n of 100,001 and −1, a mistyped field, and an unknown action.

| | 1 m | 1/3 m | 1/9 m |
|---|---|---|---|
| ledger exact at every sample | ✓ | ✓ | ✓ |
| live parity (cells, marks, stats) | ✓ 300 ticks | ✓ 300 ticks | ✓ 30 ticks |
| dam-break front (Ritter 2√(g·3 m) = 10.85 m/s) | **46.7 m/s ✗** (cap 60) | 4.5 m/s ✓ | 0.81 m/s ✓ |
| U-tube arms (m) after 120 s, from 4.8 / 0 | 4.00 / **1.00**, stuck | 4.32 / 0.67, asleep at 120.7 s | 4.24 / 0.74, still moving |
| asleep | 120.2 s | 120.7 s | not within 300 s |

**Predictions vs results:**
- **Ledger exact:** held.
- **Front slower than Ritter:** held at 1/3 and 1/9 m; **failed at 1 m**. A thin film runs a whole
  metre per tick, close to the 60 m/s cap.
- **U-tube levels in more than 10 s:** **wrong in kind**. It never levels. The far arm fills only to
  the connector's height (exactly 1.00 m at 1 m cells), then the box sleeps with the arms 3 m apart.
  The CA has no pressure: this is §8's risk, now measured.

**Visual:** the raw-cell view of the dam break with a sand pile on the ledge, at 1/3 m (tick 93) and
1/9 m (tick 26), from the same pose (16, 34, −6), yaw 90°, pitch −45°. Captures are in
`screenshots/screenshot_20261103_2200{14_592,41_529}.png`. Water is shown at fill height and the
sand slumps into terraces. P2 compares the raw view against the shaded one.

**Decisions for the owner (new):**
1. **Pressure.** Pick one:
   - (a) a pressure or communicating-vessel rule in the CA;
   - (b) leave it to P3's particles;
   - (c) accept MatSim as "spills and materials" with WaterCore owning bodies (M3).
   Until then a MatSim U-tube or siphon is wrong at every cell size.
2. **Cell size changes outcomes.** The same dam break runs at 46.7 / 4.5 / 0.81 m/s at 1 / 1/3 /
   1/9 m. Under the tier rule (tiers change cost and looks, never outcomes), cell size can't be a
   quality knob as it stands. It needs a speed-normalised rule, or the live switch stays a lab tool.
3. **Carried over from P1a:** a merge refuses over mixed materials (§6b).
4. **Carried over from P1a:** whether the tick is world time (it resets on `create`).

**Environment (this clone):** the engine and the test executables need
`%APPDATA%\uv\python\cpython-3.12-windows-x86_64-none` on PATH (`python312.dll`). Without it,
`phyxel.exe` exits silently: MCP `launch_engine` "succeeds" with no engine, and `build_project`
reports failure only at test discovery. Debug only; Release was not built.

### 6d. P1c built (2026-11-04) — the pressure rule

Owner, 2026-11-04: *"add a pressure rule to the CA"*. This answers §6c decision 1 with (a).

**Why not the textbook compression trick.** The usual CA pressure stores pressure as extra mass,
with water slightly compressible. That was rejected on arithmetic before any code was written:
- level accuracy needs the per-cell compression C to be far larger than the column depth in cells;
- an invisible volume loss needs C ≪ FULL / depth.

At 1/9 m, FULL = 256 in a 24-bit fill, and no C satisfies both. An 8 m column would stand about
0.3 m low, or the arms would differ by about a metre.

**What was built: a head field plus flow from high head to low head.** Water stays
incompressible, and volume = mass / FULL exactly.

*The head pass.* Every cell, every tick, a gather from the previous tick.
- Each liquid cell open above is a **free surface** at elevation E = (y − box floor) × FULL + fill.
- Full cells, cells closed above, and air cells carry **two pointers** to surfaces they reach
  through liquid. Partial free surfaces are atmospheric and pass nothing on.
  - **Max:** the highest surface (ties: fewest hops, then lowest index). Its E is the cell's
    **head**, the same through a connected body. Every push and every "pressurised" test uses it.
  - **Route:** the nearest surface (fewest hops) among those within the deadband (FULL/64) of that
    head, i.e. the surfaces that can absorb a hole. Hole routing uses its hops.
- Each source's E is read from the source cell itself that tick, so a falling surface is never
  stale.
- Hops are capped at 254, the reach of the pressure (28 m of path at 1/9 m, 254 m at 1 m).
- Shaders: `matsim_pressure.comp`, line for line with `MatSimCore::pressurePass`.
- The first version had one pointer chosen by E − hop cost × hops, plus hysteresis. The live rig
  broke it (defect 9 below).

*Rule 4 (pressure), in the block.* A full cell (full to within the deadband) whose head exceeds a
neighbour's surface pushes fill into it:
- **into a hole** (a cell closed above): only from the side nearer the source (fewer hops). Holes
  travel upstream to the high surface instead of diffusing;
- **into an open cell:** from below (a rising arm, a fountain); sideways only if the cell is
  unsupported (a jet out of a wall) or within the deadband of full (a top-off);
- **amount:** half the excess, capped by Torricelli through one face, v = Cd √(2 g Δh), with
  Cd = 0.65 (Wikipedia, *Torricelli's law*, citing tec-science 2019, fetched 2026-11-04). A non-fall
  push needs more than twice the deadband.

*Existing rules defer to pressure.* A closed hole beside a full cell, under a head higher than its
surface by more than the deadband, is skipped by gravity, level and slide. The pressure rule
fills it instead. In the same block, pressure never pushes back what gravity just dropped.

*Sleep.* It stays exact: a cell whose pointer or head word changes marks its brick in this tick's
set, which this tick's process mask dilates. A far surface that moves wakes everything that
points at it. "At rest" now means no cell moved and no head changed (`StepStats::lastTickQuiet`).

*Knobs and limits.*
- `create {pressure: false}` gives exactly the P1 rules (the A/B control). A pressure-off parity
  test confirms they are unchanged on the GPU.
- The head word has 30 bits, so a box taller than 5,752 cells at 1 m is refused (more at finer
  sizes).

**Red before green** (`tests/core/MatSimCoreTest.cpp`, `MatSimPressure.*`):
- the U-tube test against the rules without pressure: at rest at tick 2 with the arms 4 m apart, at
  all three sizes, for both arm ratios;
- the pressure-off control keeps that failure: the far arm holds 0.000 m.

**Defects found while building it, each shown by a test, then fixed:**
1. **"At rest" fired while heads were still propagating.** It is now defined on moves and head
   changes. The still-basin P1 test had the same blind spot; it was fixed the same way. Its wake
   control steps two ticks, because at the box top one block offset reaches outside the box.
2. **Every hole routed to one highest cell, so one column drained the whole arm.** At 1/9 m the
   U-tube had not levelled after 200 s. The hop cost spreads holes to their nearest surface.
   - Banding equal heights instead caused a limit cycle at the band edges.
   - Hop costs of FULL/64 and FULL/16 left the arms 3–15 cm apart.
3. **Pointer jitter between near-equal surfaces kept holes wandering sideways forever** (1/3 m
   never rested). Fixed by the hysteresis.
4. **A connector cell one unit short of full counted as atmospheric and cut the pipe.** Level's
   remainders leave such cells. Fixed by the top-off.
5. **A first fix for 4 hid a real near-full surface; reverted.**
6. **A partial cell under the divider, with no full neighbour, cut the pipe.** Fixed: every cell
   closed above carries heads.
7. **Holes flagged as pressurised that had no possible donor deadlocked.** Fixed: only holes
   beside a full cell are deferred to pressure.
8. **Gravity and pressure undid each other inside one block.** Fixed by the 2-deadband push
   threshold and the never-undo-a-fall rule.

Defects 6–8 were found only by the four-vessel stress test: one far chamber stayed at 0.44 m
while the others reached 0.95 m.

9. **Found live, not by the unit tests: the single hop-cost pointer.** The rig's U-tube is
   16 × 7 m in plan, against 5 × 1 m in the unit tests. There:
   - the arms ended 2–3 cm apart at every size (left 2.61 m, right 2.58–2.59 m, level 2.60 m);
   - the box never came to rest at 1/3 m or 1/9 m;
   - the dam half alone rested in 10 s.

   The CPU reference reproduced it exactly (2.610 / 2.589). The trace showed:
   - two neighbouring cells under the right surface held different sources, one the far left arm
     (13 hops, about 2 cm higher), the other the surface directly above (1 hop);
   - one pushed up, the other let the water fall back, and level carried it between them, so the
     water circulated forever;
   - the same discontinuity gave a level bias of about path difference / 256 m, which **grows
     with vessel size** (about 0.4 m at 100 m).

   Fix: the two pointers above. `MatSimPressure.WideUTubeRestsLevelAtEverySize` was red with the
   live numbers, then green: rest at 27.7 s and 153 s, the arms 1.1 cm and 4 mm apart at 1 and
   1/3 m. It also cost something: the narrow U-tube at 1/9 m now rests at 170 s (was 81 s).

**Audit (independent, read-only, 2026-11-04).** No CPU/GPU drift was found in the rules. It found:
- **D1, fixed:** with pressure off, the GPU downloaded a pointer buffer it never wrote. The
  pressure-off parity test was red ("head pointer of box cell 0: cpu 4294967295 gpu 0"), then
  green.
- **D2, fixed:** a missing dependency between a tick's mark clear (a transfer) and the previous
  tick's dilate reading those marks.
- **R1, fixed:** `tickHz` is validated (30–960), so the Torricelli product can't wrap.
- **R2, tested:** a hole or air pocket under water must fill. `HoleUnderWaterFills` passes at all
  three sizes; a mutant with no hole routing fails it, leaving 5, 27 and 18 cells hanging.
- **R3, fixed:** `adoptStepped` range-checks pointers.
- **Test gaps:** the 1/9 m GPU U-tube parity now runs 5,200 ticks, past rest.
- **Still untested:**
  - the Torricelli rate itself, since any K passes;
  - jets out of a wall;
  - a sealed pipe;
  - a non-default `tickHz`.

**Results** (final two-pointer rule). All 27 MatSim unit tests pass in the CPU Release `/O2`
harness. All 10 GPU parity tests pass in the engine's Debug build.

| Scene | 1 m | 1/3 m | 1/9 m |
|---|---|---|---|
| U-tube 2 \| 2 m arms, 4 m in the left (level 2.000 m) | 2.000 / 2.000, rest 3.2 s | 2.003 / 1.997, rest 29.1 s | 1.999 / 2.001, rest 170 s |
| U-tube 1 \| 3 m (level 1.000 m) | 0.987 / 1.004, rest 2.4 s | 1.000 / 1.000, rest 19.5 s | 1.000 / 1.000, rest 159 s |
| four vessels 1, 2, 1, 3 m (level 0.714 m) | 0.713 0.726 0.707 0.709, rest 8.2 s | 0.716 0.710 0.715 0.717, rest 39.9 s | — |
| the live rig's U-tube, 16 × 8 × 7 m (level 2.600 m) | 2.605 / 2.594, rest 27.7 s | 2.602 / 2.598, rest 153 s | (integration and live) |

GPU (Debug, laptop, a 16 × 8 × 16 m awake box), in ms per tick:

| | 1 m | 1/3 m | 1/9 m |
|---|---|---|---|
| pressure off | 0.026 | 0.029 | 0.033 |
| pressure on | 0.043 | 0.052 | 0.098 |

The head pass runs over every cell every tick, asleep or not.

The P1 tests still pass with pressure on by default:
- ledger exact for 10 k ticks;
- sleep equals dense, including after edits and resolidify;
- determinism, world alignment and watertight staircase walls;
- the repose angles (sand 0.675, snow 0.781, gravel 1.000);
- the random stress scenes, with 24 of 36 merges still refused as before.

The new tests add:
- sleep equals dense in a U-tube, cells **and** the head field, every tick for 2,400 ticks;
- the box-height refusal at create and at a switch.

**Predictions vs results:**
- **Levels within max(1/4 cell, 3 cm): held.** The largest error is 1.8 cm (1 m cells, unequal arms).
- **At rest at every size: held.**
- **Levelling time (no number was predicted):** it scales with the cell size. That is the
  one-cell-per-tick cap again (§0b, the P1d decision). A real U-tube of this size sloshes with a
  period of a few seconds (T = 2π√(L/2g)); here it is overdamped. At 1 m it settles in about 2.5 s;
  at 1/9 m it takes about 81 s.

**Live rig** (`tools/matsim_lab.py --run`, final rule, Debug, evidence `docs/evidence/matsim_p1c_live.json`):

| | 1 m | 1/3 m | 1/9 m |
|---|---|---|---|
| ledger exact at every sample; dam holds (0 m³ past it) | ✓ | ✓ | ✓ |
| live parity: cells, marks, both pointers, head words, stats (in 50-tick or 5-tick calls) | ✓ 300 ticks | ✓ 300 ticks | ✓ 30 ticks |
| U-tube arms (m) at the end, level 2.600 | 2.607 / 2.592, rest at 28.2 s | 2.601 / 2.599, rest at 148.7 s | **3.47 / 1.61 at 300 s, still moving** |
| dam-break front (Ritter 10.85 m/s) | 46.7 m/s | 4.5 m/s | 0.87 m/s |

The refusals all refuse, with the box unchanged.

**Open:** at 1/9 m the wide U-tube levels in the right direction but slowly. The gap went from
4.7 m to 1.86 m in 300 s, roughly exponential with a time constant of about 5 minutes. The small
1/9 m U-tube in the unit tests rests in 170 s. Throughput through the 1 m gap is far below what the
per-face Torricelli cap and one-hole-per-two-ticks would allow, and the cause is not yet found.
The candidates are hole routing to the nearest absorber and the push/absorb thresholds. That
investigation was run next: **§6e**. The cause is the route field (count-to-infinity toward a
moving absorber set), not the tick rate. The dam-break front is unchanged by pressure, as expected: pressure moves no water across
an open floor.

**Stated limits:**
- Pressure reaches 254 hops.
- The equilibrium mismatch is bounded by about two deadbands (FULL/32), whatever the vessel size:
  3 cm at 1 m, 1 cm at 1/3 m, 3.5 mm at 1/9 m.
- The head pass costs every cell every tick, asleep or not, about 3× the asleep cost at 1/9 m.
  Restricting it to liquid regions is P5's.
- A falling full column carries a head, as if it were standing water. It is harmless in the tests;
  P3's particles replace free fall.
- Mixed liquids share no head (only Water exists).

### 6e. Experiment: why pressure is slow (2026-11-04, after P1c; no rule change shipped)

**Question.** Is P1c's slow levelling (2|2 U-tube: 191 / 1,746 / 10,204 ticks at 1 / 1/3 / 1/9 m)
the one-cell-per-tick cap (P1d), or the rule? **Physical reference:** two 2 m² tanks through a
1 m² orifice (Cd 0.65), Δh₀ = 4 m, equalise in t ≈ 2·A_eff·√Δh₀ / (Cd·A·√(2g)) ≈ 1.4 s (84 ticks
at 60 Hz) — real water also overshoots and sloshes, which no CA rule here can.

**Rule time, not tick time.** Ticks to rest grow ~9× then ~6× per 3× finer cell. P1d (ticks ∝
cells/m) only rescales time — the CA is tick-based, only the orifice constant reads `tickHz` — so
at 60·cpm Hz the same runs would take 3.2 / 9.7 / 18.9 s. Better, still size-dependent and 2-13×
slower than water. **The rule is the bigger lever; P1d stays a separate, later question.**

All diagnostics are `MatSimDiag.DISABLED_*` in `tests/core/MatSimCoreTest.cpp`
(`--gtest_also_run_disabled_tests`); experiments were env-gated CPU builds, all reverted.

| # | Hypothesis / variant | Result (2\|2 U-tube) | Verdict |
|---|---|---|---|
| 1 | Torricelli cap on interior pushes fragments holes → fill a closed hole whole | 1/3 m rest 1,753 vs 1,746 | no effect alone |
| 2 | Slice dump, 1/3 m tick 300 | route hops a checkerboard of `-1` and 59/60 **rising 1 per tick** in the connector (count-to-infinity); absorbers = only surface cells within db (1/64 cell) of the max, while the surface ripples by tenths of a cell | **cause found**: holes descend a field that never converges |
| 3 | Absorber band sweep (route accepts surfaces within F/k of the head) | table below | band trades speed for a final gap; best bulk only 2.7× faster |
| 4 | Stall classes (1/3 m): 60-80% of holes have full neighbours but none nearer the source; holes average 0.2 cell and never merge | — | holes queue single file |
| 5 | Holes MERGE (closed X takes from any liquid nearer the source) | 1/3 m rest 874; 1/9 m rest 6,097 (gap 0.8 mm) — **but 1 m never moves**: a thin far-arm surface pours back into the outlet's hole every tick | 2× faster, broke 1 m |
| 6 | Surface decides itself whether it absorbs (one consistent field) | alone: deadlock 2.77 m apart; with merge: rest 1,064 | worse |
| 7 | Merge only closed→closed | 1 m 123, 1/3 m 1,214, four vessels 146 / 1,292, wide 714 / 5,597 (all faster, all level) — **1/9 m never rests**: ~1,300 cells/tick of 1-2-unit holes fizz in the connector, hops rising 1/tick, ~10 k head changes/tick | closest; fails rest at 1/9 m |
| 8 | + a closed cell within db of full counts as full | 5 runs never rest (1 m, 1/3 m): it contradicts gravity/level's pressurised test | worse; reverted |

Absorber band sweep (#3; band = surfaces within F/k of the head are route targets):

| band | 1/3 m t90 | 1/3 m rest | 1/3 m gap | 1/9 m t90 | 1/9 m rest | 1/9 m gap |
|---|---|---|---|---|---|---|
| F/64 (= db, P1c) | 1,340 | 1,755 | 5.5 mm | 9,090 | none by 12,000 | 0.9 mm |
| F/16 | 730 | none | 27 mm | 5,020 | none | 4.8 mm |
| F/8 | 530 | 726 | 0.9 mm | 4,560 | none | 7.9 mm |
| F/4 | 710 | none | 67 mm | 4,540 | none | 6.9 mm |
| F/2 | 530 | none | 172 mm | 4,000 | none | 36 mm |
| F | 610 | none | 349 mm | 3,310 | none | 39 mm |

(The 1/9 m baseline here ran with #1 in place; P1c as committed rests at 10,204.)

**What the data says.**
1. The crawl's throughput is bounded by a routing field that propagates one hop per tick toward
   a target set (the highest surface cells) that moves every few ticks. On short paths (1 m) it
   converges; on long paths (1/3, 1/9 m: 30-100+ hops) it never does. Every local fix tried moved
   the failure (a cycle, a stall, a final gap) somewhere else.
2. Even the best variant is ~15× (1/3 m) to ~40× (1/9 m) slower than the physical reference in
   ticks, before P1d.
3. A **perfectly** routed crawl would be near-physical in bulk (a 9×9-cell connector carries ~40
   cells/tick at one hole per cell per 2 ticks vs the Torricelli ~70), so the mechanism is not
   hopeless — its routing is.

**Candidate next experiments (none chosen; more data first):**
- **(a) Non-local transfer through the connected body** (prior art: Dwarf Fortress's water
  pressure teleports water through full cells to a lower outlet). Fast and size-independent, but
  ignores constrictions (a 1-cell pipe would carry as much as its outlet surface).
- **(b) A stable routing potential**: hops to surfaces chosen with hysteresis or bucketed by E,
  plus a separate exact-final-approach rule; #3/#7 suggest the final < 1-band approach needs its
  own mechanism.
- **(c) Flux / virtual pipes** (Mei, Decaudin, Hu 2007, heightfield water: per-face flux
  accelerated by head difference) — the only option with momentum (sloshing, wave speed √(gh)),
  and the most realistic; open-surface water is 2.5-D-friendly, closed pipes need a 3-D extension.
- Each must be judged on the same harness: the 2|2 / 1|3 / wide U-tubes and four vessels at all
  three sizes, rest + level + ledger, against the 1.4 s reference.

### 6f. Experiment E1: MatSim's own particle fluid (2026-11-04)

**Why.** §6e: the CA cannot carry pressure at physical speed. Noita's own split is cells for
resting matter and particles for moving matter (P3). Realism adds what Noita skips:
incompressibility, so moving water must be particles **with a pressure solve**. The engine's debris
solver is rigid bodies with contacts: 10 k cubes in a U-tube pile like gravel. The owner's direction
(2026-11-04): MatSim is its own experiment and builds its own solver, independent of WaterCore.

**Method: Position Based Fluids** (Macklin & Müller, "Position Based Fluids", SIGGRAPH 2013). Per
substep: gravity → predict → neighbour grid → N iterations of the density constraint
C_i = ρ_i/ρ₀ − 1 (poly6 density, spiky gradient, λ with relaxation ε, tensile s_corr) → solid
collision → velocity from positions → XSPH viscosity. Chosen because it is position-based like the
engine's AVBD debris solver, unconditionally stable at large steps, and maps to one GPU pass per
iteration later.

**Ground rules of E1:**
- One particle per lifted cell, at the cell centre, carrying that cell's fill as **integer units**
  (mass = units / FULL). The ledger is cells + particles, exact at every step.
- Kernel radius h = 2 cells; ρ₀ = the density of a particle inside a perfect lattice.
- Solid cells within h act as static boundary particles at their centres (Akinci et al. 2012, voxel
  form), so a particle at a wall sees the same density as one inside the body. The box outside is
  solid.
- Substeps: CFL, the fastest particle moves ≤ 0.4 cell per substep.
- Deposit: each particle's units go into its cell; overflow passes upward (then sideways), so the
  deposit is integer-exact.
- E1 lifts the whole body explicitly (`lift`), so it tests the solver alone. Automatic wake/settle
  (E2) comes after the solver is shown to work.
- CPU reference first (deterministic, sequential); a GPU port is judged on tolerances, not bit
  equality (float reduction order).

**Predictions (written before the code):**
- 2|2 U-tube (4 m in the left arm, 1 m connector) at 1/3 and 1/9 m: arm heights within
  max(½ cell, 3 cm) of each other and of 2.000 m once at rest; the right arm reaches 90 % of its
  final height in **≤ 3 s** of simulated time (physical reference 1.4 s; overshoot and sloshing are
  expected and correct).
- 1 m: SPH needs several particles across the narrowest passage; at 1 m the connector is one
  particle tall. **Prediction: 1 m fails or is crude.** That is a resolution floor, not a bug.
- Ledger exact (cells + particles) at every frame and after the deposit.
- **Control:** zero density iterations → no pressure; the particles collapse into a dense layer and
  the right arm stays far below level (< 50 % of 2 m).
- Cost: measured, CPU Debug and Release, per particle per frame.

**E1 results (CPU reference, Debug, 2026-11-04; `tests/core/MatSimParticlesTest.cpp`):**

| | 1 m | 1/3 m | 1/9 m | reference |
|---|---|---|---|---|
| particles | 13 | 351 | 9,477 | |
| far arm at 90 % | 2.85 s | **1.17 s** | **1.10 s** | ~1.1-1.4 s (quarter period / Torricelli) |
| slosh period | no slosh | **4.23 s** | **4.35 s** | 4.49 s (T = 2π√(L_eff/2g), L_eff 10 m) |
| first surge | 2.00 m | 3.06 m | 3.19 m | (undamped ideal: 4 m) |
| mean far arm, last 4 s | 2.000 m | 1.965 m | 1.996 m | 2.000 m |
| ledger (cells + particles), every frame and after deposit | exact | exact | exact | |
| CPU Debug cost | 0.5 ms/frame | 21.5 ms/frame | 1,424 ms/frame (4 substeps; ~150 µs per particle-frame) | |

The CA's best (§6e) needed 874-10,204 ticks (15-170 s) and grew with every finer cell; the
particles' timing is the same at 1/3 and 1/9 m and matches U-tube theory to 3-6 %. **Speed no
longer depends on cell size**, which is the §0b realism gap P1d was created for. The particles also
have momentum: the U-tube surges past level and sloshes at its natural period, which no CA rule did.

**Predictions vs results:**
- Level within max(½ cell, 3 cm): **held** at 1/3 and 1/9 m (time-averaged; the arms are still
  sloshing at 10-20 s, as real water would).
- 90 % within 3 s: **held** (1.17 / 1.10 s).
- "At rest within 10 s": **failed, and was wrong physics.** A real U-tube of this size sloshes for
  many periods; the test now checks the period instead (within 20 % of theory).
- 1 m crude: **held** (a one-particle connector: it creeps level with no slosh).
- Control (0 iterations): **held on the particles** (no particle ever enters the far arm). The
  first version measured after the deposit and failed (1.19 m): the deposit's overflow search
  spread the collapsed pile into both arms. Lesson: deposit only water that has settled.

**Defects found and fixed:**
1. The tensile term (s_corr) pushes fluid pairs apart even at exact rest density, with no
   counterpart at walls: a full box at rest with gravity 0 accelerated from frame 1 (max speed
   4.8e-3 → 0.135 cells/s over 40 frames, linear); with it off the lattice stays at exactly 0. Off by
   default; the unilateral constraint already prevents the free-surface clumping it exists for.
2. A first reading of "11 % compression" was a measuring artifact (the arm measure counts
   particles above 1 m; the top layer straddles it). Layer means sit within 3 % of the lattice.

**Open defect: resting jitter.** With gravity, a still tank's particles churn: median 1.0 m/s at
c = 0.01, drifting ~1.7 cells/s. It does not fall with iterations (4 → 30), Jacobi
under-relaxation (ω 0.25-1), or relaxation ε (which only makes the water compressible), and it
grows with smaller substeps (a fixed position error per substep). Zero gravity is still; it is not
a wall effect (interior particles of a 3 m-deep tank churn too). XSPH c = 0.1 (now the default)
cuts it 4× without changing the period or the surge; c = 0.3 cuts it 13× but lowers the first
surge 10 %. **Cause not yet found.** It matters for E2: water has to settle before it deposits
back into cells, and instantaneous speed cannot be the rest test while it persists.

**E1, day 2 (2026-11-05): the resting churn, diagnosed; the dam break.**

Resting churn, ruled out one by one (diagnostics `MatSimParticles.DISABLED_*`):
- not walls (interior particles of a 3 m-deep tank churn as much);
- not pressure propagating down the column (median 1.0-1.4 m/s at 2, 4 and 8 layers alike);
- not one substep's gravity kick (≈ 0.08 m/s; quartering g changes the churn only ~1.5×);
- not voids in the one-sided constraint (two-sided is worse: 2.0-4.6 m/s);
- not the projection: a cube compressed 10 % goes from worst C 0.376 to 0.004 in one iteration.

**Cause:** PBF constrains positions, never velocities. Each substep the stiff projection picks one
of many nearby rest arrangements of a disordered packing, and that jump becomes velocity
(jump / dt), so the noise grows as the substep shrinks. **Fix:** a velocity-level divergence solve
after the position solve (DFSPH, Bender & Koschier 2015), 2 passes by default. It halves the churn;
with XSPH c = 0.1 the still-tank median goes 1.0 → 0.22 m/s, and the U-tube surge (3.07 m) and
period (~4.5 s) are unchanged. c = 0.3 would cut it to ~0.1 m/s but over-damps the slosh (second
surge 2.07 m), so it is not the default. The residual is volume-preserving swirl; a speed-only rest
test still cannot work, so E2's settle test must average over time.
The velocity solve alone also carries pressure (density iterations 0, divergence on: the far arm
reached 1.22 m), so the no-pressure control now turns both off.

**Dam break** (`DamBreakFrontAtOneThirdMetre`, the rig's geometry 1 m deep: 3 m deep reservoir,
front timed x 4.5 → 12 m; prediction 70-105 % of Ritter, 1/3 and 1/9 m within 10 %):

| | 1 m | 1/3 m | 1/9 m |
|---|---|---|---|
| CA (P1, §6c) | 46.7 m/s | 4.5 m/s | 0.87 m/s |
| **particles (E1)** | 6.82 m/s (63 %) | **8.18 m/s (75 %)** | **8.18 m/s (75 %)** |

Ritter 2√(g·3 m) = 10.85 m/s is frictionless, an upper bound. The 1/3 and 1/9 m fronts agree to
the measurement's resolution (frame-quantised timing: ±1 frame ≈ ±2 %). 1 m is below the
resolution floor again. **Not yet validated:** an experimental front speed for this geometry
(bed friction lowers it below Ritter); 75 % is plausible but unsourced. Cost at 1/9 m (6,561
particles, Debug CPU): 1.9 s per frame.

**Next (E2 onward, in order):** ~~find the jitter's cause~~ (done, above); ~~settle → deposit~~ (§6g) (displacement-window and
density test, then automatic wake on a disturbance); ~~the dam break against Ritter~~ (done, above); the GPU port
(the CPU cost above is the reason; one pass per iteration); then the hybrid with the resting CA.

### 6g. Experiment E2: the hybrid, cells at rest and particles in motion (2026-11-05)

**Goal.** Noita's split, made realistic: resting water is cheap CA cells, moving water is the §6f
particle fluid. Water must **settle** from particles into cells and **wake** from cells into
particles, with the ledger exact across every conversion, and no flapping between the two.

**The rule that held: a body is all cells or all particles.** A connected body of water is never
split along a moving seam:
- **Wake** (`MatSimParticles::wake`, before each particle step): a face-connected cell-water body
  wakes whole when any of its cells has a head excess over an open or part-empty neighbour of more
  than `wakeHeadCells` (1 cell; an empty neighbour counts from its top), holds at least half a cell
  over an empty cell (falling), or touches a particle.
- **Settle** (`settle`, after each particle step): particles within h of each other form a group. A
  group settles when ≥ `settleFraction` (80 %) of its members are ≥ 1 s old with a 1 s-averaged
  speed < `settleSpeed` (0.35 m/s), **and** its free surface (3×3-column volume averages, columns
  capped by a solid excluded) is level to within `settleLevelCells` (0.8 cell, below the wake
  threshold).
- **Level deposit**: a settling group's water is laid down as resting water sits, bottom-up and
  layer by layer over its footprint (completed to one top layer), flat to one unit,
  integer-exact.
- Frame order: wake → particle step → settle → CA step (pressure on). Cell water counts as
  boundary mass (fill/FULL) for the particles.

**What failed on the way (each measured, each fixed by a rule above):**

| # | First rule | Failure, measured | Fix |
|---|---|---|---|
| 1 | per-particle settle, average starts at 0 | everything settled on frame 1, so the CA levelled the U-tube alone (the test passed for the wrong reason, caught by the 0.02 s settle time) | 1 s age gate; test requires ≥ 1.8 m at 3 s |
| 2 | per-cell wake | slow water settled at slosh turning points and re-woke; partial cells became light particles (705 from 351 cells of water) | wake only ≥ ½ full; then the body rule |
| 3 | no pressure wake next to particles | the CA moved cell water into particle cells; boundary mass blasted particles out (a 112 m/s "front") | cells and particles never share a cell; the body rule |
| 4 | absorb cell water into particles | heavy particles skewed PBF density; arms 0.27 m off | the body rule |
| 5 | a group settles when every member is slow | the resting tail at ~0.3 m/s kept 354 particles busy for 40 s | 80 % of members |
| 6 | no surface check | settled at a turning point with the arms ~0.3 m apart, woken straight back | free surface level within 0.8 cell |
| 7 | surface from the top particle per column | ±0.5 cell jitter per column | 3×3 volume averages |
| 8 | deposit into each particle's own cell | a cell over an empty one tripped the falling trigger (4-18 cells a cycle) | land / level deposit; falling needs an empty cell below |
| 9 | per-particle deposit | ~1-cell surface roughness read as pressure (13-19 trigger cells a cycle) | level deposit over a completed footprint |

**Results** (`tests/core/MatSimParticlesTest.cpp`, 1/3 m, CPU Debug):

| test | result |
|---|---|
| `StillTankSettlesIntoCells` | all cells at 1.02 s, surface flat to 0.000 cell, ledger exact |
| `UTubeSettlesLevel` (lifted, settle only) | far arm 2.48 m at 3 s; all cells at 13.7 s; arms 2.000 / 2.000 m |
| `WakeDamBreakFromCells` | front 8.04 m/s (all-particle 8.18; control, CA only, 2.42) |
| `WakeUTubeFromCells` | wakes at 1 s (6 pressure triggers lift 354 cells), far arm 2.74 m at 3 s (control 0.42 m), all cells at 8 s, **no re-wake to 40 s**, arms 2.000 / 2.000 m, ledger exact every frame |

**Stated limits (known, not yet tested):**
- **A disturbance wakes its whole body.** Fine for these rigs; a pebble in a lake would lift the
  lake. Scale needs a region-limited wake and a clean particle/cell interface, the hard part this
  rule avoided.
- The settle thresholds (0.35 m/s, 80 %, 0.8 cell) are fitted to measured distributions at 1/3 m,
  not derived. A principled rest test (a body's centre-of-mass height going still) is open.
- The last cell of levelling is the CA's (P1c pressure rule), which is cell-size dependent:
  0.33 m at 1/3 m, 0.11 m at 1/9 m.
- Only 1/3 m is tested for E2. 1/9 m (CPU Debug ~1.4 s a frame) waits for the GPU port.
- Whole-box scans per frame (wake, groups): O(cells + particles), CPU only.

**Next:** ~~the GPU port of the particle step~~ (done, §6h) (the cost is the blocker for 1/9 m and for live
runs), then E2 at 1/9 m and live in the lab rig; region-limited wake.

### 6h. The particle step on the GPU; E2 at 1/9 m (2026-11-05)

**Built.** `MatSimParticlesGpu` (engine/{include,src}/core/matsim/MatSimParticlesGpu.*) runs one
frame of `MatSimParticles::step` on the device: `shaders/matsim_pbf.comp`, one kernel per stage
picked by a mode push constant (predict + collide; the neighbour grid as a counting sort: count,
3-pass scan, scatter, then a per-bin insertion sort back to ascending index; density lambda /
correction / apply; velocity; divergence kappa / correction / apply; viscosity + commit). One
submission per frame; the particles and the box cells are uploaded each call, positions and
velocities downloaded. `MatSimParticles::setGpu()` routes `step()` there; lift, wake, settle,
deposit and the averaged velocity stay on the CPU (the CPU stays the owner). The CPU summation
order is kept (bins dy, dz, dx; ascending index per bin; boundary y, z, x), so the port tracks the
CPU closely, but it is float and not bit-exact: it is accepted on the measured physics.

**Red first.** `MATSIM_GPU_STUB=1` records no substeps (the particles freeze), a permanent negative
control: with it the tracking and dam-break tests fail (front -1, tracking 4.3 cells off at 30
frames). Then green, `tests/integration/MatSimParticlesGpuTest.cpp` (this laptop's GPU, Debug host build):

| test | GPU result | CPU reference / prediction |
|---|---|---|
| tracking, 1/3 m U-tube | 2.4e-6 cells after 1 frame (the CPU moved 0.008), 1e-3 to 7e-3 after 30 | predicted < 1e-3 and < 0.1 |
| U-tube 1/3 m | t90 1.13 s, period 4.35 s, level, ledger exact | CPU 1.2 s, 4.23-4.35 s |
| U-tube 1/9 m (9,477 particles) | t90 1.08 s, period 4.22 s, level, ledger exact | CPU 1.1 s |
| dam break 1/3 and 1/9 m | 8.18 / 8.18 m/s (75 % of Ritter) | CPU 8.18 / 8.18 |
| hybrid U-tube 1/3 m | wake 0.3 s, far arm 2.74 m at 3 s, all cells at 11.5 s, 2.000 / 2.000 m, no re-wake | §6g: 8 s on the CPU |
| **hybrid U-tube 1/9 m** (first run ever) | wake 0.8 s, far arm 3.13 m at 3 s, **all cells at 33.5 s, 2.000 / 2.000 m, no re-wake to 40 s, ledger exact** | §6g prediction: may fail, thresholds fitted at 1/3 m |
| cost, 1/9 m U-tube | 7-12 ms a frame wall (5-10 ms device) | CPU Debug 4,000+ ms (Release not measured) |

**What 1/9 m showed, in order (each measured):**
1. **The hybrid did not settle at 1/9 m** (10,056 particles at 40 s, arms level to 1 cm). Settle
   statistics (`lastSettleStats()`): the speed test passed (93 % slow), the surface test failed
   (spread 4-8 cells against 0.8).
2. **Single-column volume is noisy.** A column counts a whole vertical line of particles over a
   non-lattice packing: 22-32 cells within one x slice of a level 27-cell arm. The 3 x 3 patch was
   fixed in cells (1 m at 1/3 m, 1/3 m at 1/9 m). Fix kept: the patch is metric,
   `settlePatchM = 1 m` (still 3 x 3 at 1/3 m; 9 x 9 at 1/9 m), by summed-area tables. Not
   enough alone: the spread stayed 2-8 cells.
3. **The averaged profile showed a real wave**: a hump mid-arm in both arms (0.3 m), with its
   period (~1.1 s for a 2 m arm) about the 1 s averaging window, so the oscillation cancelled in
   the averaged velocity and only the surface test could see it.
4. **The fluid gained energy at 1/9 m** (`DISABLED_SloshDecay`): 1/3 m decays to ~0.05 J/kg; 1/9 m
   keeps bursting (0.05 to 0.26 J/kg at 48 s; with viscosity 0.3 to 2.75 J/kg, both arms 0.15 m
   high at once: the whole body breathing).
5. **It is the solve's convergence** (`DISABLED_EnergyVsSolve`, 60 s, after 20 s): 4 iterations at
   CFL 0.4: KE max 3.40 J/kg, waviness 1.46 m. 8 / 12 / 16 iterations: 0.087 / 0.086 / 0.089, under
   0.025 m. CFL 0.2: 0.091. No divergence solve: 0.103. The column is 3x deeper in cells at 1/9 m;
   4 Jacobi iterations do not hold it. **Default changed to 8 iterations**, cheaper than the bursting
   4 (6.7 vs 8.1 ms a frame; the bursts forced extra substeps). One run per calm variant, 3 of 3
   runs bursting at 4.
   CPU suite re-run at 8 (`MatSimParticles.*`, 7/7 pass): U-tube t90 1.13 s, period 4.48 s
   (was 4.23-4.35 at 4; theory 4.49), dam break 8.18 m/s, hybrid U-tube far arm 2.70 m at 3 s and
   2.000 / 2.000 m, still tank all cells at 1.02 s.

**Limits, stated:**
- The iteration count is empirical (stable at 8 for a 27-cell column). A deeper body in cells (a
  lake, or 1/27 m) may need more; the principled form (iterate to a density-error tolerance, or
  scale with depth in cells) is open.
- Settle is now the cost at 1/9 m: 59 ms a frame (CPU, std::map columns and union-find), against 6 ms for the
  GPU step. The wake scan is 4 ms, the CA 11 ms.
- Particles still cling to the divider faces (seen at y up to 43 cells, 1/9 m, 4 iterations; not
  re-measured at 8).
- Whole-box uploads each frame (cells 4 bytes each): fine for these rigs, not for a world.
- No live engine run yet (the lab rig does not use the particles).

**Settle cost, fixed (Release, the honest build; Debug overstated std::map 6x):** the 1/9 m
hybrid frame measured wake 0.6, GPU step 5.0, settle 9.5, CA 1.4 ms; settle's 9.5 was 9.3 ms of
`groups()` (union-find over particle pairs within h). Groups are now the 26-connected components
of the cells holding a particle (linear in occupied cells): **settle 1.1 ms**, every result the
same to the frame (1/9 m hybrid all cells at 33.5 s, 2.000 / 2.000 m; CPU suite 34/34, GPU 5/5).
**A 1/9 m hybrid frame is ~8.3 ms** (wake 0.7, step 5.0, settle 1.1, CA 1.5). The Release CPU
particle step is 899 ms a frame at 1/9 m, the GPU 11.6 ms (77x).

**Next:** ~~the particles live in the lab rig (L4)~~ (§6i), then a region-limited wake.

### 6i. E2 live in the lab rig (L4), 2026-11-05

**Built.** `POST /api/matsim/create` takes `"particles":"hybrid"` (default `"off"`, the CA lab
unchanged), with optional `settle_speed` (m/s, 0.35) and `wake_head_cells` (1.0). A hybrid box
steps wake -> particles (`MatSimParticlesGpu`, else the CPU reference, reported as
`particles_backend`) -> settle -> CA (on the host) per 60 Hz tick. The ledger, `slices`, `stats`
(particle count, wake triggers, settle blockers with the lowest/highest surface column, per-part
ms) and the raw-cell view (each particle a cube of its volume) count the particles.
`set_cell_size` is refused while water is in flight; `parity` is refused for a hybrid box.
- **Ledger, a bug found and fixed:** lift and deposit go through `MatSimCore::setCell`, which books
  each one as an edit, so the core's expected ledger followed the transfers and the first lab
  ledger read "inexact" (expected empty). Adding the particles back would make the check
  tautological (a unit dropped in a deposit would move both sides). The lab records how far its
  own transfers moved the expected ledger and subtracts it: expected changes only with API edits.
- Contract test (`MatSimParticlesGpuTest.LabHybridDamBreak`, through `MatSimLab::handle` only):
  front 8.33 m/s at 1/3 and 1/9 m (control, particles off: 2.34 m/s and never reaching 12 m),
  ledger exact at every read, 6 faces per particle in the view, set_cell_size refused in flight.

**Live (Release, the MatSimLab project, port 8120; the rig is hand-placed, `tools/matsim_lab.py
--hybrid`; evidence `docs/evidence/matsim_e2_hybrid_live{_third,}.json`).** The engine was
started from a shell with the Python runtime on PATH: `launch_engine` (MCP) started a process
that died at once with `python312.dll` missing.

First run, 1/3 m: the dam held (0 m3 past the wall), the front ran 7.72 m/s (71 % of Ritter), the
U-tube arms levelled (2.6005 / 2.6017 m), the ledger was exact at every sample, **but nothing
settled in 120 s** (11,547 particles). An offline replica of the rig's geometry
(`DISABLED_LiveRigReplica`) reproduced it; the new stats showed why:
1. **Wall columns read low.** Per-column volume was 1-2 cells low within ~2 cells of every wall
   (particles pack thinner there; the interior packs denser) while the arms were level to 2 mm.
   Every earlier rig was 1 m deep, all columns against a wall, so it cancelled. Leaving out the
   wall-adjacent column was not enough (the band is ~h wide).
2. **Fix kept: the level test is geometric.** A column's surface is its top particle (+0.5),
   averaged over the 1 m patch: it does not depend on how densely the water packs. The U-tube
   body then settled.
3. **A thin sheet reads one particle layer uneven.** The dam-side water spread to a 1.6-cell sheet:
   columns hold 1 or 2 layers, spread 0.97-0.99 cells against the 0.8 limit. **settleLevelCells
   0.8 -> 1.25** (fitted): one particle layer plus margin; the level deposit lays the water flat,
   and one cell is the cells' own resolution. The old "below wakeHeadCells" bound came from the
   per-particle deposit.

After the fix (CPU 34/34, GPU 9/9 incl. the replica: all cells at 20 s):

| live rig, hybrid | 1/3 m | 1/9 m |
|---|---|---|
| cells / water particles at peak | 55,296 / 10,373 | 1.49 M / 262,351 |
| dam holds before the break | 0 m3 past the wall, reservoir stays cells (7,581 woken = the U-tube arm) | 0 m3 past the wall, reservoir stays cells (204,183 woken = the U-tube arm) |
| front (Ritter 10.85 m/s) | 7.72 m/s (71 %) | 8.61 m/s (79 %) |
| all cells again | 32.9 s, no re-wake in the next 5 s | 32.8 s, no re-wake in the next 5 s |
| U-tube arms (volume level 2.600 m) | 2.6000 / 2.6000 m | 2.6000 / 2.6000 m |
| ledger exact at every sample | yes | yes (0 step timeouts) |
| worst wall ms per tick | 28 | 398 |

**Visual** (`docs/evidence/matsim_e2_hybrid_view.json`, pose (16, 34, -6) yaw 90 pitch -45, the
raw-cell view at 1/3 m, camera looking north over the dam half; the separation wall hides the
U-tube): `screenshots/screenshot_20261105_221431_096.png`, 0.5 s after the break, the reservoir as
particle cubes collapsing (9,579 particles, 57,474 faces); `screenshot_20261105_221458_921.png`,
40 s, a flat sheet of cells over the dam half and a patch on the ledge where the surge climbed it
(0 particles). **A lab bug found on the way:** the view was rebuilt only under realtime, so a box
stepped by the API kept its create-time faces (783 at every capture); `update()` now redraws a
dirty view either way.

**Cost at 1/9 m, live, per tick (204,183 particles, 5 substeps):** particle step ~100 ms (89 ms on
the device), CA 72 ms (1.49 M cells on the host: a hybrid box does not use the GPU CA), wake 25,
settle 24: ~225 ms a tick, ~14x slower than real time. The 1/9 m rig steps in 10-tick calls (the
API waits 5 s for the game loop).

**Limits, stated:**
- settleLevelCells 1.25 is fitted; a body may settle with up to ~1 cell of slosh left (0.33 m at
  1/3 m) and the deposit flattens it (a small, bounded loss of slosh energy).
- The CA of a hybrid box runs on the CPU, and every tick uploads the whole box's cells: the 1/9 m
  cost is the box, not the water. The GPU CA and the particles should share the device copy.
- Whole-body wake: the 1/9 m U-tube woke 204 k particles at once.

**Video (2026-11-06), `docs/evidence/matsim_video/hybrid_3.mp4` and `hybrid_9.mp4`** (+ `.srt`,
`.json`), made by `tools/matsim_hybrid_video.py`:
- **What it shows:** the same rig and fills as above, in the raw-cell view (cells and particles as
  cubes; Water is textured with the Ice stand-in). The crop is the viewport, at pose (0, 38, 16),
  yaw 0, pitch -60, with the reservoir front-left and the U-tube's filled arm back-right.
- **Timing:** the sim is stepped by the API 2 ticks per frame and the engine screenshot is taken
  after each step, so every frame is exactly 1/30 s of sim time. Both videos play at real speed,
  though 1/9 m took 1,519 s of wall time for 37 s of sim.
- **Captions:** sim time, particles in flight, and the ledger state (every 5th frame).

| | 1/3 m | 1/9 m |
|---|---|---|
| peak particles | 10,271 | 258,829 |
| ledger | exact at every sample | exact at every sample |
| first 0-particle sample after the break | 33.0 s | 31.5 s |

The U-tube arm was filled one-sided, so it sloshes from frame 0.

**A view bug found by the 1/9 m video:**
- **Symptom:** the dam half was missing from the first take.
- **Cause:** each particle was drawn with six faces against a cap of 393,216 faces. At 1/9 m that
  cut 1,147,674 faces and dropped every particle past the first ~65 k, the U-tube's (red: `tools/matsim_view_cut_probe.py 9`
  at tick 30, 256,815 particles, `view_faces_cut` 1,147,674).
- **Fix:** a particle face is now skipped when the cell beside the particle's own cell holds a
  particle, a wall, or a full cell. Green, same probe and same particle count: 161,095 faces,
  0 cut. At 1/3 m it went from 57,438 faces to 5,150, with 0 cut before and after.
- **Not a physics change:** the probe's particle count is identical before and after.
- **What the camera sees:** a clump's surface is now drawn per cell, not per particle.

**Live frame rate (2026-11-07), `tools/matsim_hybrid_fps.py` → `docs/evidence/matsim_video/fps_{3,9}.json`:**
- **Setup:** the same rig, the box in realtime mode (the game loop steps it, at most 4 ticks a
  frame, the rest dropped), the raw-cell view on, the video's camera, Release, the committed
  6bfd479f code.
- **Measured:** sampled every 0.5 s. The engine's `fps` field is misleading here, because frames
  with no tick run at about 700 fps. So the worst frame in each window is the number reported.

| | 1/3 m | 1/9 m |
|---|---|---|
| sim speed while particles fly (1.0 = real time) | 1.00 (min 0.994, 0 ticks dropped) | 0.067 (3,345 ticks dropped in 60 s) |
| worst frame per 0.5 s, median / max | 15.1 / 21.1 ms (~60 fps with a hitch) | 979 / 1,757 ms (~1 fps) |
| hybrid tick, median | 10.2 ms | 241 ms |
| control: the same view, no box | 781 fps | 661 fps |

1/3 m runs live at real time near 60 fps. 1/9 m is about 1 fps and 15x slower than real time:
the device-resident hybrid (the CA on the GPU, no per-tick upload) is the lever.

**The ledge "specks" (owner, from the videos at video-time ~7 s at 1/3 m and ~9 s at 1/9 m):**
- **What the owner saw:** a few particle cubes on the raised ledge snap into a grid of small,
  evenly spaced cubes.
- **Cause:** spray lands on the ledge, settles, and the CA spreads it into a film a fraction of
  a cell deep. The next splash touches the film, and the contact wake lifts the whole body one
  particle per cell, at the cell centre, carrying that cell's fill. That is a lattice of specks
  of side h·cbrt(fill), floating half a cell up. The video stats show it: +30, then +74
  particles at those moments.
- **Two fixes tried, both REJECTED as defaults and kept as knobs (off):**
  - `wakeContactMinFill`: contact wakes only a cell at least this full.
  - `liftAtCentroid`: a lifted particle starts at y + fill/2.
  - Red first: `ThinFilmTouchedByADrop...` lifted 324 of 324 film cells, and the half-cell
    particle sat at 1.5 rather than 1.25. Green on the unit tests, and the CPU suite (36/36) and
    GPU suite (8/8) passed.
- **The 1/3 m video caught it:** it did not settle; particles grew to 35,635 at 35 s. The
  live-rig replica A/B (`DISABLED_LiveRigReplica`, `MATSIM_CONTACT_MIN`, `MATSIM_LIFT_CENTRE`)
  at 1/3 m, 36 s:

| contact wakes | lift at | result |
|---|---|---|
| any contact (the default) | cell centre (the default) | 0 particles by 18 s |
| any contact | centroid | 0 particles by 18 s |
| half-full cells only | cell centre | 5,601 particles at 30 s (3,618 under a quarter cell); 0 only at 36 s |
| half-full cells only | centroid | blew up: 34,127 particles, up to 675 cells/s, 16 substeps |

- **Mechanism:** a film the drops cannot wake stays cells. The CA spreads it wide and thin, and
  a later trigger lifts it whole through a connected deeper cell, as thousands of near-empty
  particles. Starting those at the centroid puts them on the floor's boundary particles, which
  throw them.
- **Lesson:** near-empty particles are the fragile part. The unit tests passed and the GPU tests
  passed; only the video, and then the replica, showed it.
- **Next for the specks:** a mass-conserving lift that packs a body's thin cells into particles
  of about one cell's water each, placed at their centroid. No near-empty particles, no lattice.

**The packing lift, tried 2026-11-07 (owner: "do the speck fix first"): REJECTED as a default.**
Four rules were built, each red first, all kept as knobs (off by default) with their tests:

| knob (`ParticleConfig`) | rule | red test (shown failing first) |
|---|---|---|
| `packThinBelow` | wake() packs cells under this fill into face-connected clusters of >= one cell, bounded to 2 cells from the seed; the particle starts inside the member cell nearest the centroid | `WokenThinFilmIsPackedIntoCellSizedParticles` (324 slivers -> 44 particles, smallest 1/4 cell), `PackedParticlesNeverStartInsideSolid` |
| `wakeContactMinSpeed` | only a particle at least this fast wakes by contact | `RestingParticleDoesNotWakeAFilmItTouches` |
| `absorbGroupMax` | a group of <= N particles in or right above cell water of its liquid deposits into it, and never wakes by contact | `DropJoinsAPoolButAnInflowWakesIt` |
| `wakePressureMinFill` | the pressure trigger reads only cells at least this full | `ThinCellsDoNotTriggerThePressureWake` (12 triggers -> 0) |

Each rule answered a failure the previous one exposed on the replica (`DISABLED_LiveRigReplica`, new
env knobs `MATSIM_PACK`, `MATSIM_CONTACT_SPEED`, `MATSIM_ABSORB`, `MATSIM_CPM`; traces
`MATSIM_TRACE`, `MATSIM_TRACE_SLIVER`, `MATSIM_TRACE_COL`):
- packing alone: two packed films on the ledge woke each other in turn forever (a resting
  particle counts as contact) -> the speed gate;
- with the gate: drips off the ledge film re-woke the 2,142-cell pool by contact every ~1 s to
  36 s (whole-body wake) -> absorb;
- an unbounded cluster followed a film down the ledge wall and put its particle inside the ledge
  (y 53.5), re-lifted every 2 frames -> bounded clusters, in-cell placement;
- a film draining over the ledge edge read as under head -> the pressure fill floor.

**Result, all four on, 1/3 m:**

| | old rules | all four on |
|---|---|---|
| replica: all particles settled by | 16 s | 24 s |
| replica: woken particles under 1/4 cell | 60 | 692 |
| live video (`hybrid_3_speckfix_rejected.mp4`): settled by | 33 s | 20 s |
| the ledge at 7.5-9.5 s (`speckfix_ledge_old_vs_packed.png`) | ~40 tiny specks in a grid | 9 bigger cubes, **still a grid**, static 7.5-9.5 s |

Ledger exact in every run; the dam-break fronts, U-tubes and lab tests unchanged.

- **What the video showed:** the lattice is not a lift artifact. The CA spreads a film to a
  uniform depth, and any deterministic lift of a uniform film tiles. The rest of the slivers are
  CA drips: a thin film creeps over the ledge edge and falls as cells of 0.02-0.08 fill (under
  the falling trigger's half cell), and the moving pool below touches them.
- **The fix belongs in the CA:** water should not spread thinner than a droplet. A minimum
  film depth (a lateral spread only above a fill floor, the remainder held as a puddle or
  bead), measured against the U-tube and dam-break levels. Built in §6j.

**Next:** ~~the CA's thin-film rule~~ (§6j); keep the hybrid on the device (the CA on the GPU, no
per-tick upload); then a region-limited wake.

### 6j. The CA thin-film floor (2026-11-07)

**The rule** (`Config::filmFloor`, a fraction of a full cell; 0 = off and the old rules exactly;
lab `create {"film_floor": f}`, host CA only, a GPU CA box refuses it):
- **Level rule:** a 2x2 layer group that rests entirely on solid and whose level would fall under
  the floor gathers its water into its `k = total / floor` fullest cells (at least one; fullest
  first, ties by hash), levelled among them; the rest go empty. Once gathered it is a fixed point.
  A body of water (cells over full water) is never touched.
- **Slide rule (liquids):** a slide never makes a new liquid cell thinner than the floor, and
  never splits a falling cell (empty below) into a remnant thinner than the floor.
- Falls and the pressure rule are untouched. Mass-conserving by construction (every move is a
  pair of `setc`s); diagnostic counters `filmGathers()` / `filmHeld()`.

A cell fraction, not metres, on purpose: the specks are a render-scale problem (a particle draws
as a cube of side `h * cbrt(units / F)`), and a physical 5 mm puddle depth is 0.015 of a 1/3 m
cell, which would lift as specks again. The floor is a voxel-aesthetic knob; at 0.25 the
smallest bead draws 0.63 of a cell wide at any cell size.

**Red first** (each failed on the unimplemented knob with its control passing; `MatSimFilm.*`):

| test | control (floor 0) | floor 0.25 |
|---|---|---|
| `SpillStopsAtTheFilmFloor`: one full cell on a 5x5 m floor, 1/3 m | 225 cells, thinnest 0.0036 | 4 cells, all exactly 0.25 |
| `AThinFilmBeadsUp`: a 0.05 film over 9x9 cells | 81 cells | 17 cells, thinnest 0.10 |
| `LedgeDripsAreNotSlivers`: PoursOffLedge's scene, airborne cells under 1/4, summed per tick | 79 | 0 (88.6 % still pours off) |

The first ledge prediction failed (84 vs 79 sliver-cell-ticks with only the creep rule): the
slivers were not film creep but **the slide rule fanning a falling cell sideways in mid-air**
(trace: 0.125, then 0.188 / 0.094 / 0.062 pieces off one falling sheet). Hence the second slide
clause (no sub-floor new cell anywhere, no sub-floor remnant of a falling cell).

**Stress, the U-tubes** (`UTubesStillLevelWithTheFloor`, floor 0.25): 2|2 and 1|3 at 1 and
1/3 m level within the old tolerances and rest (196 / 1,720 / 152 / 1,214 ticks); the wide U-tube
levels (2.601 / 2.598 m against 2.600 at 1/3 m). **Open finding:** the wide U-tube at 1/3 m
does not come to rest on 2 of 12 seeds with the floor (seeds 1 and 5; 0 of 12 without). The
cells still moving are over full water (a 213-unit hole swapping between x 0 and 1 under the
surface, and 1-2 unit surface wobble), the film rule fires **0 times** after tick 12,000, and the
same cells handed to a fresh core rest in 75 / 78 ticks **with or without** the floor: the cycle
lives in the pressure rule's head field, and the floor only steers the spill into it
(`MatSimDiag.DISABLED_FilmWideUTubeMoves`, env `MATSIM_REST_ONLY`, `MATSIM_SEED`, `MATSIM_FILM`).
The test asserts what the floor owns (levels, ledger, no film act after levelling) and prints rest.
A pressure-rule limit cycle is now a known base-rule issue to hunt separately.

**The replica** (`DISABLED_LiveRigReplica`, `MATSIM_FILM`, 1/3 m):

| | floor 0 | floor 0.25 |
|---|---|---|
| woken particles under 1/4 cell | 60 | **0** |
| smallest particle in flight, 6-12 s | 0.031 cell | 0.25 cell |
| woken in all | 11,801 | 11,750 |
| all settled by | 16 s | 16 s |

Ledger exact on every sample of both runs.

**Live (L4), 1/3 m** (`python tools/matsim_hybrid_video.py --cpm 3 --film 0.25` ->
`docs/evidence/matsim_video/hybrid_3_film0.25.{mp4,srt,json}`, the same pose and rig as `hybrid_3.mp4`):
all particles settled by **14.5 s** (old rules 33 s), ledger exact on every sampled frame, CA ~3 ms
and wake ~1.1 ms a tick at rest. The ledge at 8.5 and 9.5 s (`filmfloor_ledge_old_vs_film.png`:
top row 8.5 s, bottom 9.5 s; left old, right floor 0.25; a crop of the same frames of each video):
the old run shows ~40 specks in a regular 6x7 lattice plus a row down the ledge wall; the floor run
shows ~5 irregular beads of about 2/3 of a cell, **no lattice**. Water on the ledge now beads.

**Status:** the rule is built, tested red-first and default OFF (`filmFloor = 0`, lab
`film_floor` 0). Every existing test is unchanged (CPU MatSim 46/46, GPU 16/16, dam-break fronts
8.18 / 8.33 m/s). Candidate to become the hybrid's default once the 1/9 m video is checked and the
pressure-rule cycle above is understood.

**Live (L4), 1/9 m (2026-11-08)** (`--cpm 9 --film 0.25` -> `hybrid_9_film0.25.{mp4,srt,json}`, same
pose and rig as `hybrid_9.mp4`). The ledge, crops of the same frames (left old, right floor):
- `filmfloor_ledge_old_vs_film_9.png` (video 9 s top, 10 s bottom): spray landing, irregular specks
  in **both**; at 10 s the floor run's ledge is partly joined patches.
- `filmfloor_ledge_old_vs_film_9_late.png` (12 s top, 16 s bottom): at 12 s the old run shows the
  fine **regular lattice**, the floor run irregular larger beads; at 16 s the old ledge looks empty
  (its film is too thin to draw) and the floor run keeps a maze of beads at least 1/4 cell deep.

**A regression the 1/9 m video found: the floor run never stays at rest.** Old: all settled at
31.5 s, 0 particles after. Floor: 52,995 -> 132 particles at 31.7 s, then ~58,000 again, cycling
every ~1.2 s to the end (35.9 s); ledger exact throughout. The replica (`MATSIM_CPM=9`, new env
`MATSIM_SEED`, trace `MATSIM_TRACE_LATE`) reproduces it on some seeds, so it is not chance:

| 1/9 m, rest after 30 s | seed 1 | seed 2 | seed 3 | seed 4 |
|---|---|---|---|---|
| floor 0 | rests (live) | rests | rests | rests |
| floor 0.25 | **cycles (live)**; rests (replica) | rests | **cycles** | **cycles** |
| floor 0.25 + `absorbGroupMax` 4 | rests (replica) | - | rests | rests |

(The GPU particle step is not bit-reproducible, so the live run and the replica of one seed diverge.)
The cycle, traced on seed 3: the big settle leaves 69-169 particles of ~0.3 cell on the raised
ledge (y 162, solid below), drifting at 0.2-1.4 cells/s; one drips off the ledge edge (falling at
19-37 cells/s); its contact wakes the whole connected pool (57,835 cells from ~10 contact cells);
the pool settles in ~1 s, the ledge beads stay, repeat. It is §6i's "drips off the ledge re-wake the
pool" again: the floor turns the ledge film into beads big enough to keep moving and drip. §6i's
`absorbGroupMax` (a group of <= N particles at cell water deposits into it and never wakes it by
contact) stops it on all 3 replica seeds tried. The lab now takes `absorb_group_max` (create;
echoed; negative refused; red-first `MatSimLabApi.AbsorbGroupMaxIsPassedAndChecked`) and the video
script `--absorb N`.

**Live with the guard (L4, 1/9 m, seed 1 = the live cycler)** (`--cpm 9 --film 0.25 --absorb 4` ->
`hybrid_9_film0.25_absorb4.{mp4,srt,json}`): all settled at **31.7 s and 0 particles to the end
(35.9 s)**, ledger exact on every frame (old rules: 31.5 s). `filmfloor_9_end_old_film_absorb.png`
(video 35 s; left old, middle floor, right floor + absorb): the old ledge looks dry (its film is
too thin to draw), the floor run's ledge holds moving particle beads, the guarded run's ledge holds
settled cell beads; the pool is flat in all three. N = 1 live, 3 replica seeds.

**Status:** the floor is still default OFF; at 1/9 m it must not ship without the drip guard
(`absorbGroupMax`). Floor 0.25 + absorb 4 is the candidate pair, not yet measured at 1/3 m live.

**Next:** the pair at 1/3 m (replica + video); the pressure-rule limit cycle (seeds 1 and 5 of the wide U-tube at 1/3 m); keep the hybrid on the
device (the CA on the GPU, which then needs the film rule ported); then a region-limited wake (a
single drip should never lift a 58,000-cell pool).

**Owner idea (2026-11-08), recorded for later: draw settled water as a smooth surface.** Noita's
water shows its pixels only where it moves (the surface, splashes, bubbles). Here: the CA drives
behaviour, and a body at rest renders as one smoothed surface, with particles drawn only in motion.
It would also remove the speck problem at its root (a resting body is never drawn as cubes). It is
**not** a frame-rate fix: at 1/9 m the frame is the simulation (§6i: 240 ms tick vs 18-53 k faces;
the same view without the box renders at 661 fps).

### 6k. The region-limited wake (2026-11-08)

**Why now (owner, 2026-11-08):** "damping on particles could be smartly applied ... unless the only
way to get accurate looking water is to wake up half a million particles instead of a small subset
near the disturbance, then i am questioning whether this is doable at all?" The whole-body wake is
§6g's stated shortcut ("a pebble in a lake would lift the lake"), not a requirement. Moved ahead of
the GPU CA work: it decides whether the hybrid can scale at all.

**Design.** Two knobs, both default OFF (the old rules exactly):
- `wakeRadiusM` (> 0): a trigger cell lifts only the cells of its body within this many metres
  (face-connected steps through the body), not the whole body.
- `caSeesParticles`: for each CA step, empty cells that hold a particle are walls to the CA (made
  solid for the step, restored after; they hold nothing, so the ledger is untouched). Particles
  already feel cell water (it is boundary mass in the density sum, §6g). Without this the cell
  water around a woken hole pours into it through the particles, and the hole edge reads as head
  over an empty cell, which the pressure trigger wakes: the region would grow by R every tick.
  The wake's pressure and falling triggers count particle cells as full for the same reason.
- With a radius, contact wakes only from particles faster than `wakeContactMinSpeed` (§6i knob):
  particles at rest in the hole touch its cell wall forever. Motion that reaches the wall grows the
  region; motion that has died out does not (the owner's damping, as a rule: the woken region is
  where the energy is).
Mass crosses the interface only through wake (cells -> particles) and settle (particles -> cells),
both integer-exact, as before.

**Known loss, stated up front:** ripples do not travel past the woken region (the CA has no
waves). A real drop's rings would cross the lake; here they stop at about R. §4's appearance layer
(waves drawn on the surface) is where those belong.

**Rig (CPU, 1/3 m, `RegionalWakeDropIntoStillPool`):** a 12 x 3 x 12 m box, a still pool 1 m deep
(3,888 cells), a 2 x 2 x 2-cell drop (8 particles, 2 m/s down) over its centre; absorb off. 20 s.
**Control:** today's whole-body wake, same rig.

**Prediction, written before the code:**
- control: the first contact lifts the whole pool (>= 3,888 cells); peak particles >= 3,888.
- regional (R = 1 m, contact speed >= 0.5 m/s, CA sees particles): peak particles <= 400 (about 10 %
  of the pool), all back in cells by 10 s, final surface spread <= 1 cell, ledger exact every frame.
- the risk: the region creeps outward (every tick a new edge) and ends as the whole pool. The test
  reports peak particles and total cells lifted, so creep shows.

**Result (CPU, Debug, 1/3 m, 20 s; raw lines from the test's printout).**

| | peak particles | cells lifted in total | back in cells | end state |
|---|---|---|---|---|
| control, whole-body wake | 3,908 | 78,140 | **never** (re-wakes the whole pool every ~1 s) | still 3,908 particles at 20 s |
| regional, first build (R + CA walls + instantaneous contact) | 4,353 | 8,180 | 6.45 s | it **crept** over the whole pool twice |
| regional, final | **608** | **1,315** | **3.52 s**, at rest to the end | level, ledger exact |

The prediction of peak <= 400 **missed** (608, 16 % of the pool); the claim that matters held: a
drop woke a sixth of the pool, not the pool, and the pool came back to rest, while today's rule
never rests in this rig. Three things the per-wake trace (`DISABLED_RegionalWakeTrace`,
`MATSIM_TRACE=1`) found, each now part of the knob set:
1. **Contact creep:** ~10 of ~3,000 woken particles jitter past 0.5 m/s at any instant, and each
   touch of the cell wall lifted another 1 m ball. Fix (`wakeContactAveraged`): contact compares
   the particle's *time-averaged* velocity (the settle EMA, bias-corrected for a young particle).
   Jitter averages out; an inflow does not. This is the owner's damping, applied to the wake.
2. **Pressure creep:** cells inside the woken hole that hold no particle centre (particles sit
   about a cell apart) read as an open hole 2+ cells below the pool surface (52 trigger cells
   with no fast particle). Fix: a cell is *held* if a particle is within one cell of it; held
   cells are CA walls and count as full for the pressure and falling triggers.
3. **Never settled (438 particles, all slow, for 12 s+):** the radius walks through the body, so
   the rim of the region was lifted low and cell water stayed on top of it, held up by the CA
   walls; the level test read those columns as a surface 1.7 cells low. Fix: the region is whole
   columns (a lifted cell lifts the same liquid stacked above it).
After the deposit, two short pressure re-wakes (27 and 6 trigger cells) level the region against
the pool; each is radius-limited too.

**1/9 m through the lab (prediction, written before the run; `LabPoolDropNinthMetre`, GPU particle
step, host CA):** a 1/3 m cube of water dropped from 1 m above a still pool 1 m deep, 12 x 12 m
(104,976 cells). The lab takes `wake_radius_m` (0 = whole body, the default; > 0 turns on the
bundle above, contact speed `wake_contact_min_speed` default 0.5 m/s; negative, or a radius with
contact speed 0, refused; red-first `MatSimLabApi.WakeRadiusIsPassedAndChecked`). Predicted:
control peaks >= 104,976 particles; regional (R 1 m) peaks <= 10,500, is all cells within 10 s,
ledger exact at every read, and its particle step while water flies is >= 5x cheaper.

**1/9 m, first run: FAILED on every count** (raw, `LabPoolDropNinthMetre`): control 105,198
particles, never rests; regional started at 1,632 particles and **crept to 50,322** by 10 s, never
rested, and **the ledger broke**; the CA got dearer (188 -> 300-430 ms: blocking costs 27 cells a
particle). The library replica with the ledger checked after every phase
(`DISABLED_PoolDropPhases`, GPU particle step) reproduced it and found four things, each fixed in
turn with the run repeated:
1. **Ledger: settle overwrote cell water** (-256 units = one 1/9 m cell, after settle at frame 442).
   Settle's room loop grew a column up into a cell that already held water and did not count it.
   Unreachable under the whole-body wake (no cell water ever sat by a settling group). Fixed
   for all modes; red-first `SettleKeepsCellWaterItGrowsInto` (lost exactly one cell).
2. **Creep: the region had no way to lose energy.** The splash crater dipped > 2 cells below the
   pool (pressure trigger, +1 m of pool), the bigger region sloshed against its cell wall, which
   reflected every wave, and its moving particles woke more by contact. Fix, the owner's damping:
   a **sponge** (`spongeTau`, `spongeCells` 2): a particle with cell water of its liquid within 2
   cells has its velocity scaled by exp(-dt / 0.1 s) each frame. The cells are water at rest; a
   wave that reaches them should leave the simulation (in a lake it would run on as ripples the CA
   does not have), not bounce back. Result: no creep (peak 1,794), ledger exact, but a ~1 s cycle.
3. **The cycle, part 1: a one-cell spike.** The room loop stopped at the first column that made
   room, so the drop's extra water came back as one nearly full cell a cell above the pool. The
   head field takes the highest free surface of a body, read 10.00 cells over a 8.93-cell pool,
   and every top cell showed > 1 cell of excess (24 trigger cells, every ~1 s). Fix, all modes:
   the room loop adds a whole layer over the footprint; red-first
   `SettleSpreadsTheTopLayerOverTheFootprint` (3,456 units in one cell, wanted <= 865).
4. **The cycle, part 2: a 2 % void.** Full cells two cells down sat next to an interior cell 0.96-0.98
   full; its level (7.98) under the head (9.05) read as 1.07 cells of excess and woke ~1,000 cells to
   fill 0.02 of a cell. Fix (`wakePressureMinRoom` 0.5): the pressure trigger counts a neighbour only
   if it has at least half a cell of room (the falling trigger's scale). The CA fills small voids.
After all four (`MATSIM_SPONGE=0.1 MATSIM_MINROOM=0.5`): **the drop woke 1,632 cells (1.6 % of the
pool), all back in cells at 1.45 s, at rest to 10 s, ledger exact.** The lab's `wake_radius_m` now
turns on the whole bundle (sponge 0.1 s, settable as `sponge_tau`; min room 0.5; both echoed).

**1/9 m lab result with the bundle** (`LabPoolDropNinthMetre`, Debug, GPU particle step, host CA;
mean ms per tick while water flies, sampled every 0.1 s):

| | peak particles | back in cells | ledger | wake / step / settle / CA ms |
|---|---|---|---|---|
| whole-body (control) | 105,003 | 1.5 s | exact | 39.6 / 20.1 / 51.2 / 203.0 |
| regional, R 1 m | **1,632** | 1.5 s | exact | 45.6 / 10.4 / **1.4** / 241.1 |

Against the prediction: peak <= 10,500 **held** (1.6 % of the pool); all cells within 10 s
**held**; ledger exact **held**; particle step >= 5x cheaper **missed** (2x: the GPU step uploads
the whole box's cells every tick, a floor independent of the particle count). Two findings that
change the picture:
- **The settle fixes (1, 3) also cured the control**: the whole-body run now rests at 1.5 s too
  (before the fixes it re-woke the pool every ~1 s and never rested, at 1/3 and 1/9 m).
- **The tick did not get cheaper** (~300 ms Debug either way). It is the cells, not the
  particles: the CA's pressure pass gathers over every cell of the box every tick (~220 ms for
  420 k cells, Debug, even at rest), and wake() scans the whole box every tick (~47 ms). The
  regional wake fixes *what moves* (a local splash, 1.6 k particles, instead of the whole pool
  turning to particles); the cost at rest is the next lever: a pressure pass and wake scan that
  skip sleeping bricks, the CA on the device, and measuring in Release.

**1/3 m rerun with the final bundle and the settle fixes** (`RegionalWakeDropIntoStillPool`, CPU
suite run 2026-11-08): regional **peak 168 particles, 160 cells lifted, all cells at 1.12 s**,
spread 0.003 cells, ledger exact; the whole-body control now lifts the pool once (3,888) and rests
at 1.10 s (it never rested before the settle fixes). Suites: CPU MatSim 49/49, GPU 19/19.

**Cost at rest, two exact skips (2026-11-08).** Both are proven equal to the full computation, so
they change cost and never outcomes:
- **Wake scans only where something changed** (`wakeSkipQuiet`, lab `wake_skip_quiet`, default on
  in the lab): falling and pressure triggers are tested only in bricks the core marked in its last
  two ticks (cells or heads) or that hold a particle, dilated by one brick. A trigger that survives
  two quiet ticks would already have fired. `WakeSkipQuietEqualsTheFullScan` runs both scans in
  lockstep (regional pool drop, whole-body U-tube, a particle drop onto a sleeping pool) and
  compares lifts, trigger counts, cells and particles every frame: 240/240 identical on each.
  Mutation check: with the particle-brick rule removed, rigs 1 and 2 still passed; rig 3 (only the
  particle rule can see that contact) failed at frame 25 (3,888 vs 0 lifted). Rig 3 was added for that.
- **The pressure pass is skipped on a tick after a quiet tick** (core, under `sleep`): no move, no
  head change and no edit last tick means the field's inputs are unchanged. Two quiet ticks also
  skip the block loop (every block would only be copied). `QuietTicksSkipThePressurePassExactly`:
  dense vs sleep, to rest, an edit, and back, cells + heads + head pointers equal every tick, 1,197
  passes skipped (red first: 0).

1/9 m lab pool drop with both (Debug): **at rest the tick went from ~300 ms to under 1 ms** (wake
0.1-0.3 ms, CA 0.0 ms; before: wake ~50, CA ~260). Not yet cheap: while anything moves, one
changing brick costs the full-box pressure pass (~290-340 ms in the regional run until the CA has
levelled the deposit, ~5.5 s). Next lever: a pressure pass limited to the bodies that changed.

Still open: N = 1 rig per cell size, one drop. Not yet measured: 1/9 m (where the cost was), the live
rig's dam break with the regional rule (does a big real flow still wake what it should?), and
what it looks like.

### 6l. The pressure pass only where its inputs changed (2026-11-08)

**Measured first.** The 1/9 m regional pool drop (CPU, Debug, `MATSIM_TRACE=1 MATSIM_CPM=9
DISABLED_RegionalWakeTrace`), with two new diagnostics per tick: the bricks whose head words
changed, and the pass's wall time. Prediction written before the run: the head field churns
across the pool while the surface moves, so a dirty-brick pass would save little. **Wrong.** On
frames 18-60, 0-4 of 1,125 bricks changed a head word. At the peak (frame 108), 201 bricks
changed. The pass still cost ~190-250 ms every tick. It could not be skipped: the particles'
block/unblock (6k) marks bricks every tick, so no tick is "quiet".

**The rule (exact).** A cell's words (`q`, `m`, `h`) are a function of:
- its own cell and its 6 face neighbours (cells and pointers);
- the source cells those neighbours' pointers name, and those sources' own pointers.

`marks1_` already holds every brick changed since the last pass: cells moved in the block step,
head words changed by the pass, and every edit (`wakeCells`). So recompute only:
- bricks face-adjacent to a marked brick;
- bricks face-adjacent to a brick whose cells point into a marked brick.

Every other brick would write what it holds. `Config::pressureIncremental` (default on, used
only under `sleep`; off gives the whole-box pass as the A/B control).

The pointer half needs to know which bricks each brick's cells point into. The first version
scanned every cell's two pointers each tick. That computed 65 of 1,125 bricks but still cost
~120 ms in Debug: the scan and the copy of clean bricks dominated. The second version keeps
`srcBricks_`: per brick, the list of bricks its pointers name. The list is rebuilt only when that
brick is computed (a clean brick's pointers cannot change). It is dropped on reset, `clear`,
`adoptStepped` (pointers from the GPU) and any full pass, so the next pass starts full. Dirty
bricks are computed into `qn_/mn_` from the old `q_/m_` (Jacobi, as before), then copied back.
Clean bricks are not touched.

**Equality test, red first.** `MatSimSleep.PressureIncrementalEqualsTheFullPass` compares, every
tick, cells + heads + head pointers against the full pass on four rigs:

| rig | ticks | bricks computed vs full |
|---|---|---|
| U-tube 2\|2 m, 1/3 m | 2,400 | 23.0 % |
| uneven basin + an edit at t 900 | 1,500 | 21.9 % |
| pool churned like the hybrid (3³ blocked cells sweeping the surface, cells dropped in) | 600 | 45.3 % |
| lidded pipe 7 bricks long, open at one end; that open surface drops 200 units at t 300, refills at t 600 | 900 | 22.5 % |

Mutation check: with the pointer rule removed, the first three rigs still passed, because they
are a few bricks across and the face dilation covers every pointer. Only then was the long pipe
added. It fails at tick 300 (heads differ) without the rule, and passes with it. This held for
both versions of the rule.

**Result, 1/9 m pool drop (Debug, CPU, the same run as above).** Once the drop's particles are
in flight, the pressure pass goes from ~190-250 ms to **~30-35 ms** a tick (65 bricks computed).
The first frames, when the wake lifts the region, still compute 400-800 bricks (~165 ms).
Release not measured yet.

**Found on the way: another rig that never rests.** The same pipe, filled above the lid (an open
reservoir 5 cells higher than the pipe's top), never rests at 1/3 m. Cells within the deadband
of full (F 6,912, deadband 108) trade 1-11 units back and forth along the pipe every tick: 304
cells differ between t 300 and t 302, ~80 eligible blocks a tick. Small deficits wander out of
the reservoir (cells at 6,519-6,740 near x 5-7). This is the pressure-rule limit cycle already
open (wide U-tube at 1/3 m), in a simpler rig. The test uses the resting variant (filled level
with the lid).

**Release (2026-11-08).** Same CPU trace, `MATSIM_FULL_PRESSURE=1` as the A/B (whole-box pass);
identical outcomes (peak 1,352 particles, all water back to cells at 1.05 s):

| phase | full pass | incremental |
|---|---|---|
| particles in flight (frames 12-60) | 24-29 ms | **3.5-3.8 ms** |
| the deposit levelling across the pool (frames 66-180) | 26-41 ms | 2-32 ms |
| at rest (frame 186 on) | 0 (skipped) | 0 |

GPU lab rig, Release (`LabPoolDropNinthMetre`), mean ms while particles fly:

| | wake | particle step | settle | CA |
|---|---|---|---|---|
| whole-body (105,003 particles) | 8.9 | 5.6-6.0 | 6.5-9.2 | 2.0 |
| regional R 1 m (1,632 particles) | 2.5 | 6.2-6.6 | 0.4 | 9.6 |

Two findings:
- **The GPU particle step has a floor of ~6 ms at 1/9 m** that does not shrink with the
  particle count: it uploads the whole box's cells every tick. In Debug the regional step looked
  2x cheaper; in Release it is the same. The test's `stepMs` assertion claimed "cheaper" and failed
  in Release. It now pins "no dearer than 1.25x" plus the wake scan cheaper (which does scale).
  Lever: upload only the bricks that changed.
- **The dominant cost of the regional run is now after the particles are gone.** From 2 to 5 s,
  wake + CA cost 40-80 ms a tick (CA 32-64 ms) while the CA spreads the deposited water across the
  whole 12 m pool. The extra volume is a 1/3 m cube over 144 m²: a 0.26 mm rise, about 16 units a
  cell (F = 6,912), invisible, yet the level rule carries it to every column over ~3 s. That
  speed is about right physically (a 1 m deep shallow-water wave travels ~3 m/s), but the wave is
  0.26 mm tall. The whole-body run lays it flat at settle and is done by 3 s. Candidate knob
  (looks, not outcomes): let the level rule leave differences below a visible threshold.

**The GPU step's floor is dispatch latency, not the upload.** Device timestamps (Release): 4.10 ms
for 1,632 particles, 3.44 ms for 105,003. The cell upload is 1.3 MB (< 1 ms). Each substep records
~40 dispatches, each followed by a barrier: prediction, grid build and sort (8), 8 solver
iterations x 3, the velocity pass, 2 divergence iterations x 3, and the commit. At 1-2 substeps
that is ~50-80 us per dispatch on this laptop GPU, whatever the count. Levers (not taken):
fused kernels; iterations as a tier knob for small particle counts.

**The CA of a hybrid box on the device (`hybrid_ca`, 2026-11-08).** The device CA (P1b, parity-
exact) costs 0.11 ms a tick at 1/9 m on a 1.49 M-cell box with every block awake
(`MatSimGpuParity.TimingAwakeVsAsleep`, Release). The host CA cost 32-64 ms in the levelling tail.
A hybrid box now steps its CA through `MatSimParticles::stepCells(core, stepOne)`, with the lab
passing a device stepper:
- it uploads the core (with the particles' walls already placed by `blockEmpty`), steps 1 tick,
  and downloads;
- on any device error it steps the host instead and reports `hybrid_ca_error`, so no tick is lost.

Lab knob `hybrid_ca`:
- `cpu`: the host CA, which costs ~0 ms at rest thanks to the skips.
- `gpu`: the device CA every tick.
- `auto` (the default when the device CA is available and `film_floor` is 0): the device CA when
  the last tick marked more than 16 bricks, else the host.

`film_floor > 0` forces `cpu` (the port has no film rule). Stats: `hybrid_ca`, `ca_ticks_gpu`,
`ca_ticks_cpu`.

Exactness: `MatSimParticlesGpuTest.DeviceCaEqualsHostCaInTheHybrid`. On a 1/3 m regional pool
drop, every frame a copy of the core takes the host step and the core the device step through
the same `stepCells`. Cells, tick, heads, both head pointers, both mark sets and eligible counts
were equal for 300/300 frames (66 with particle walls, 213 with cells moving). Control: a 2-tick
device step against a 1-tick host step differs.

Lab pool drop 1/9 m, regional, Release, with `auto`: CA in the levelling tail **7-13 ms**
(was 32-64 ms), mostly the transfers. Same outcomes (peak 1,632, all water back to cells at 1.5 s,
ledger exact); the test's wall time went from 17.8 s to 9.0 s. The wake scan (10-16 ms a tick
while the pool levels) is now the largest cost of the tail.

**The wake scan while the pool levels (2026-11-08).** Measured first, with per-phase timers
(`WakeStats` prep/scan/lift/pack + `scannedCells`, lab stats `last_wake`). Prediction: the O(N)
masks and whole-domain loops around the scan dominate. **Refuted:** in the regional tail those are
~0.1 ms; the scan itself was the whole cost: the levelling ripple marks every brick, so the
dilated scan visits all 279,936 water cells (13.6 ms, ~49 ns a cell, and it finds nothing: no
lift). The cost was `core.at()` (out of line, a bounds check, ~8 calls a cell). `testCell` now
reads the core's array by local index (the wake's index is the core's).

- Equality: the per-frame wake trace of `PoolDropPhases` + `HybridUTubeAtOneNinthMetre` (lifted,
  trigger counts, first wake, arm levels, peaks; run-to-run identical before the change) is
  identical after it.
- Release, lab pool drop 1/9 m, regional: scan 13.6 -> 3.8-4.8 ms at 280k cells (~15 ns a
  cell); levelling-tail wake 8.7-13.7 -> 2.8-5.0 ms. Whole-body wake at 1 s: 10.9 -> 8.6 ms,
  where prep (`groups()` over 105k particles, 6.1 ms) is now the larger part.

**The device CA's transfers (2026-11-08).** Measured with a split (`MatSimGpu::lastXferMs`, lab
stats `ca_gpu_last_ms`), Release, 1/9 m pool, levelling tail: up host 0.3-0.6 + copy 1.5-2.5,
**step on the device 0.04-0.05 ms**, down copy 1.2-1.9 + host 1.1-1.5 + adopt 1.0 ms. The CA is
all transfer. With no particles in flight nothing touches the core between a download and the
next upload, so that upload re-sends what the device holds.

- `MatSimCore::generation()`: bumped by every change to state a device copy holds (`wakeCells`,
  through which every cell edit marks; `step`, `adoptStepped`, `resetPressure`, `wakeAll`,
  `create`, `clear`, `resolidify`, `setCellSize`, the writable `config()`). Values come from a
  process-wide counter, so equal generations mean the same state even across core copies.
- `MatSimGpu`: `download` records the generation, `step` forgets it (the device is then ahead),
  `upload` copies nothing when the core is still at it (`setSkipCurrentUpload(false)` = always copy).
  Safe for the ping-pong pointer buffers: the pressure kernel writes every cell's `qNext`/`mNext`.
- Lockstep `DeviceCaEqualsHostCaInTheHybrid`: 300/300 frames equal with 233 uploads skipped
  (asserted > 30), plus a new case: an undownloaded device step, then an upload, must copy.
  Mutations, each caught: no reset in `step` -> the new case fails; no bump in `wakeCells` ->
  frames differ at frame 4.
- Release, levelling tail: CA 5.6-7.1 -> 3.8-4.5 ms (upload 0). Outcomes unchanged (peak
  1,632, all cells at 1.5 s, ledger exact).

Still open: the download (~3.5 ms: copy, the host copies out of staging, `adoptStepped`'s moves);
the wake only needs cells, heads and marks, the pointers only before a host step. An exact scan
skip while the ripple runs; the whole-body wake's `groups()` (6 ms at 105k particles); the limit
cycle.

## 7. Working alongside the other sessions

- **Code lives in a separate git worktree and branch** (`feat/matsim-lab`, off `main`), with its
  own build directory. It doesn't touch WorldForge or water files. New files only, plus a
  registration hook for the `/api/matsim/*` routes.
- **One heavy job at a time on the laptop.** Engine OR full test suite OR full build
  (`memory: laptop-memory-kill`). MatSim builds and runs wait for the WorldForge session to be idle.
  I ask before starting any. A full build in a fresh worktree is ~38 min.
- **Engine on its own port** (not 8090, which is reserved on this laptop; not WorldForge's).
- **Shared-file changes are proposals, never edits:** `water_common.glsl`, the occupancy pool, the
  debris solver. They go to the owning session through the owner.
- **Docs:** this file, `NoitaGapAnalysis.md` and `CollisionProxyPlan.md` live on `feat/matsim-lab`
  (worktree `../phyxel-matsim`), moved out of the shared tree on 2026-11-03, so no WorldForge commit
  sweeps them up.

## 8. Risks

- **Determinism on the GPU** (F2's premise). Margolus/checkerboard updates with a seeded per-cell
  hash are deterministic in principle. Atomics and float maths are not. Integer fill helps. Prove it
  in P1 (two runs bit-identical) before anything relies on it.
- **The look at 1/3 m.** Shading may not hide a coarse surface up close. P2 decides this early and
  cheaply, before the expensive phases.
- **Pressure behaviour.** A pure CA levels slowly over distance and won't push water up a U-tube
  without an extra rule. Rung 3 measures how bad that is. It's the main reason MatSim may stay "spills
  and materials" (M3).
- **F2 can't keep up at fast travel speeds.** Then the shortcut exception (coarse cells, or freezing
  far regions until visited) is the owner's call, with numbers.
- **Two water-like systems drifting apart in look.** Prevented by sharing `water_common.glsl` and
  the layout, and by designing the wave layer jointly (seam d).

## 9a. Decided (owner, 2026-11-03)
- §8's recommendations accepted:
  - M0 (desk) starts now;
  - "deferred, never skipped" is the scaling principle;
  - the coarse-cell shortcut is an approved exception only;
  - the wave layer is raised with the water session at P6, not before;
  - builds and runs wait for the owner's idle signal.
- New branch `feat/matsim-lab` in a separate worktree; the plan docs live there.
- Cell size: **switchable live between 1, 1/3 and 1/9 m** (§3a), not a fixed 1/3.
- Hardware: the laptop now; the 4090 later.

## 9. Decisions for the owner (before P1) — as first asked; see 9a

1. **Cell size:** 1/3 m as MatSim's fine cell (recommended for cost), with 1/9 m only as the P2
   look A/B.
2. **"Deferred, never skipped"** as the scaling principle, so far matter ends up exactly where near
   matter would. The coarse-cell shortcut only as an approved exception with evidence. Agree?
3. **The 4090 desktop:** is it available for the M1 and M2 measurements? The tier table needs the
   target machine as well as the laptop.
4. **The wave layer:** should I raise seam (d) with the water session now, as a shared design so
   neither builds its own, or wait until P6?
5. **Scheduling:** M0 (desk) can start now with no impact on anyone. M1 and P1 wait until you say
   the WorldForge session is idle.
