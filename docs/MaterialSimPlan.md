# Material Simulation Plan — Noita-style voxel materials, alongside WaterCore

**Status: P1 BUILT 2026-11-03 — P1a (CPU reference core) §6b; P1b (GPU port bit-exact, `/api/matsim` lab, raw-cell
view, live rig) §6c. Two §6a predictions FAILED live (the 1 m front, the U-tube) — owner decisions in §6c. P2 not started.** Supersedes the same-day `LiquidCaLab.md`
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
