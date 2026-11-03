# Material Simulation Plan — Noita-style voxel materials, alongside WaterCore

**Status: PLAN, 2026-11-03. Nothing built, nothing run.** Supersedes the same-day `LiquidCaLab.md`
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
| **P2** | the look: MatSim writes the surface-field layout; shaded by `water_common.glsl`; raw-cell debug view | owner compares raw vs shaded, MatSim 1/3 m vs WaterCore 1/9 m, same pose |
| **P3** | momentum: the particle layer (eject over a threshold, land into fill) | pour arcs, a stone throws a crown; mass exact including in flight |
| **P4** | materials: oil on water (density swap), sand plus water, a GPU debris body displacing cells | 100 % of columns layered; displaced volume = submerged volume |
| **P5** | scale: F2 deferred stepping, look-ahead, residency; M2 ladder | deferred end state = live end state (bit-exact); the tier table filled |
| **P6** | grounded waves (§4) on MatSim liquids, in the lab | wave-in-solid = 0; the fetch test; owner's eye |
| **P7** | **marriage design** with the water session: seams (a)–(d); M3's answer | joint design-check; owner approval (R7) |
| later | gases, heat, fire, reactions (NoitaGapAnalysis Tier 3) on the same grid | their own plans |

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
