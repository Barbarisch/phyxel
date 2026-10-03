# Debris Settling Plan — kill the "bubbling crater"

**Status (2026-10-03):** measured, root-caused and **fixed**. The user signed it off watching
it in real time ("actually looks really good").
- The bubbling crater is gone. On the bench, 4 of 6 scenarios pass every check.
- The other 2 (a collapsing tower and the real blast) pass every check except a residual
  2–12 % of bodies that are still force-frozen while creeping. See §R.
- **This file is the current-state doc for GPU debris settling.** The live pipeline and
  contact model are summarised in [DynamicVoxelPhysics.md](DynamicVoxelPhysics.md)
  "GPU Compute Path". Sections §0–§6 below are the original
plan plus the baseline. They are kept for history: some hypotheses turned out right, and the
biggest causes were NOT on the list.

## §R. Result: what was actually wrong, what fixed it, how we know

**Test world:** DebrisLab project (`C:\Users\jack\Documents\PhyxelProjects\DebrisLab`, no world
block, so its terrain lives only in its own `worlds/default.db`):
- an 8-thick Stone slab, top face at y = 16;
- one scenario per 32-voxel chunk;
- pre-carved pits.

Rebuild it with `debris_settle_bench.py --build-lab`.

**Recreating DebrisLab on another machine.** The project lives outside the repo.
1. Make a folder containing:
   - `game.json` = `{"name":"DebrisLab","version":"1.0","water":{"enabled":false},
     "camera":{"position":{"x":48,"y":40,"z":60},"yaw":-90,"pitch":-30}}`. **Never add a
     `world` block**: it regenerates terrain on every launch and refills the pits.
   - `engine.json` (window and render distance; copy any project's).
   - `.phyxel/config.json` = `{"apiPort": 8090}`.
   - an empty `worlds/` folder.
2. Launch the editor with `--project <folder>`.
3. Run `python tools/debris_settle_bench.py --build-lab`. It fills the slab, carves the pits,
   saves, then verifies the terrain from the world, not from the API responses.

**Watch it in real time:**
- `python tools/debris_settle_demo.py [scenario…]` plays the scenarios at normal 60 Hz with
  the camera framed.
- `python tools/debris_drop_here.py [N]` drops N cubes in front of the current camera.

**Driven by stepping, not wall time:** `POST /api/debug/gpu_physics {"frozen","step"}`. Results do
not depend on Debug frame rate, and frames land on exact ticks.

**Gate:** `python tools/debris_settle_bench.py [--frames]` exits 0 only when every scenario
passes. It is the done-gate for any future solver change.

**Contact sheets:** `tools/debris_settle_contact_sheet.py <before> <after>` (same camera, same ticks).

### Before → after (`lab-baseline` → `fixed-2026-10-03`, Debug, 8 s of sim each)

| scenario | before | after |
|---|---|---|
| drop_layer (control) | FAILS: 24 mm lift injected, 20/36 force-frozen while **hovering** up to 11 cm | **SETTLES**: flush rest, asleep 1.27 s, 0 forced |
| packed 6×6×6 block | FAILS: exploded into a heap, never asleep | **SETTLES**: the block simply stands; asleep 0.98 s |
| crater (6×4×6 pit of cubes) | FAILS: **erupted out of the pit**, 15 rebounds/body, never asleep | **SETTLES**: stays in the pit, 0 rebounds, asleep 0.98 s |
| crater_subcube (1,296 ⅓-cubes) | FAILS: erupted, 238 colour-skipped bodies, tunnelled | **SETTLES**: asleep 0.98 s, 0 skipped |
| drop_pile (falling 5×6×5 tower) | FAILS: 3.5 rebounds/body after impact, never asleep | FAILS on one check only: 4–20 of ~175 sleeps were force-freezes (varies run to run) |
| blast (real `/api/damage/apply`) | FAILS: 12 rebounds/body, cubes erupting | FAILS on one check only: 1–6 of ~70 force-frozen |

In every scenario, after the impact phase: **0 rebounds, 0 injected energy, 0 hard-contact
pushes, 0 tunnelling, 0 skipped bodies.** Physics/destruction unit tests: 137/137 pass.
Analyzer tests: 8/8 pass.

### The defects, in order of how much they mattered (each one measured)

1. **Static contacts used the wrong geometry** (`solver_voxel.comp`, since the AVBD port).
   - The contact point was the centre of the voxel's face, not where the body touched, so
     every resting contact applied a wrong torque. Cubes tilted, rocked, rested on edges and
     rolled.
   - Normals came from a per-voxel SAT that ignored neighbours, so faces *between* floor
     voxels pushed bodies sideways or down. That produced the sideways kicks out of craters
     and the tunnelling through 9 m of rock.
   - The warm-start key was the voxel id, which reset contact stiffness every time a body
     slid across a voxel boundary.
   - **Fix:** `voxel_contact.glsl`. Each body is tested at 26 sample points against a
     26-neighbourhood signed distance to the solid. Only exposed or diagonal escapes count,
     the true lever arm is used, and the feature key is (sample, escape direction).
2. **This tick's motion was counted twice in every contact.**
   - Contacts are detected at the *predicted* position, but the constraint adds the motion
     since the tick start (`J·Δq`) again.
   - A cube falling at 6 m/s "penetrated" an extra 10 cm and was shoved back out. Every
     impact bounced, scaling with speed, and in a churning pile everything is moving.
   - **Fix:** store the penetration at tick start, `C0 = pen_pred + J·Δq_pred`, in both
     contact emitters.
3. **Speculative gaps were scaled by (1−α) = 0.01**, so a contact 2 cm away acted as touching.
   Bodies stopped short and hovered. **Fix:** `stabilizedC0` applies α to penetration only.
4. **The dynamic-dynamic manifold was broken in three ways** (`solver_narrowphase.comp`):
   - body A's reference face was scored against the wrong direction, picking A's *far* face
     (contacts nearly two box widths deep);
   - the clip planes pointed inward for every −axis reference face;
   - per-point depth was `depth − d`, which double-counted.

   Contacts were also dropped the moment two cubes separated, while depth+5 mm pushed them
   *to* 5 mm apart. Every resting stack cycled between losing its contact and regaining it cold.
5. **The hard-contact safety pass created launch velocity.** It moved `pos` only, and
   velocity is derived from position, so a 1 cm push became 0.6 m/s upward. It also fired on
   every resting body. **Fix:** same geometry as the contact pass; it now acts only above
   1 cm and is velocity-neutral (moves `initial` with `pos`).
6. **No static friction** (audit P4, open since July).
   - Friction rows started at stiffness 1 and were never anchored. `C_init_t` was always 0,
     and the saved `stick` flag was never read.
   - Friction could slow sliding but never *hold*, so stacks and leaning cubes crept for
     seconds and were force-frozen mid-slide.
   - **Fix:** stiff cold friction rows plus anchored static friction (AVBD §3.3).
7. **Bodies skipped by colouring** (audit D1, open since July). Fix: 32 colours, 32
   Jones-Plassmann rounds, and a Jacobi sweep for uncoloured bodies.
8. Smaller items:
   - stiff cold normal contacts (`massPenalty`);
   - slow bodies start the solve from rest (`startAtRest`);
   - friction μ was inverted (`1 − 0.36f` was the dead legacy shader's velocity-retention
     factor, which made Ice grippier than Stone).

**Tried and rejected, with data:**
- post-stabilisation (`SOLVER_FLAG_POST_STAB`): more forced sleeps on drop_layer, crater and crater_subcube;
- 10× and 50× cold stiffness: stacks better, impacts violent, pile exploded at 50×.

Both remain as runtime A/B switches: `POST /api/debug/gpu_physics {"flags","cold_scale"}`.
Default flags = 23.

### Still open
- **Residual force-freezes in tumbling scenes.** In drop_pile and blast, 2–12 % of bodies lean
  or rock against neighbours at a few cm/s and get frozen by the lax sleep tier. Single-run
  counts vary (blast debris is randomised; GPU atomics are not bit-deterministic). Next
  levers: static-friction anchoring for edge contacts, `matchNearest` (H4b), and a
  per-contact restitution and damping review.
- **Debris lit black inside freshly carved pits.** Debris samples the baked light at its spawn
  cell, and carved cells are not re-baked. This is a lighting issue, not physics.
- Not yet run in Release, in a real streamed world, or with a character shoving debris.
  The character-shove path (`solver_integrate.comp`) is unchanged but untested against the
  new contacts.
- `MAX_COLORS = 32` means 33 primal dispatches per iteration. Measure in Release before
  calling it free.

### Why this took so many sessions (from the git and doc record, 2026-04 → 2026-10)
1. **There was never a measurement.**
   - Every "it settles" claim was a screenshot or a one-off CSV of *separated cubes dropped
     onto a flat floor*, the one case that mostly worked. Examples: 2026-05-31 "NOW WORKING",
     and 2026-07-31 "FULL stop … no hover".
   - Nothing could tell "fixed" from "hidden", and nothing tested a packed pile or a crater.
   - The first instrument was built today, and within an hour it showed the *control*
     failing.
2. **Symptoms were tuned instead of mechanisms fixed.** Six solver architectures in six
   months: Bullet → XPBD → Jacobi → coloured Jacobi → AVBD → AVBD plus sleep. Constants were
   retuned again and again: gravity −9.81↔−18, Baumgarte 0.2→0.1, ALPHA 0.9→0.99 with the
   comment "lower values inject too much energy → popcorn", and PENALTY_MIN 50000→1. Each
   retune moved the symptom around.
3. **Safety nets were added on top and became the defects.**
   - The hard-contact pass (2026-05-09) was a "pragmatic safety net". It turned out to be an
     energy source and caused the hover bug.
   - The lax sleep tier (2026-07-31) froze the bubbling instead of fixing it, so the
     verification looked green.
4. **The real bugs were in contact geometry, which nobody looked at.**
   - All previous work targeted the solver: iterations, penalties, warm starts, sleep.
   - The defects that mattered most (#1–#4 above) are in how contacts are *generated*:
     wrong points, wrong faces, wrong reference frame, motion counted twice.
   - The solver was solving the wrong problem correctly.
   - The AVBD audit (2026-07-02) checked the solver against the paper and found it faithful,
     which was true and misleading. Its two real findings (D1, P4) were left unfixed.
5. **Defects interacted, so single fixes looked like failures.** Fixing one of them never
   produced a calm pile while the others remained, so each partial fix was judged "didn't
   work" and reverted or tuned around. Only with per-tick counters (rebounds, injected energy,
   hard-contact pushes, skipped bodies, creep before sleep) could each fix be shown to remove
   *its own* signature.

## 0. Measurement: how to prove it, and how we'll know it is fixed

**Instrument:**
- `Core::DebrisSettleAnalyzer` (`engine/{include,src}/core/DebrisSettleAnalyzer.*`, pure CPU,
  8 unit tests in `tests/core/DebrisSettleAnalyzerTest.cpp`).
- `GpuParticlePhysics` settle probe. While it runs, it copies every physics tick's particle
  state, the solver-header counters and the graph colours / constraint counts into a
  host-visible ring (one slot per frame in flight, read after the fence). It is off by
  default and costs nothing when off.
- New shader counters in the solver-state header: `SS_HARDCONTACT_FIRES`,
  `SS_HARDCONTACT_DEPTH_UM`, `SS_WAKE_REQUESTS`.

**What it measures, every tick:**
- **Injected energy.** ΣE = Σ m(½v² + g·y) + ½Iω² of a closed pile can only fall, so any
  rise is energy the solver created. It is reported as the lift height of the whole pile.
- **Rebounds.** Counted when a body goes from not rising to rising faster than 0.3 m/s.
- **Kicks.**
- **Sleep quality:** clean sleep (30 genuinely still ticks) vs. FORCE-frozen by the lax tier.
- **Wakes.**
- **Silent solver failures:** bodies skipped by colouring, dropped constraints, and
  hard-contact push-outs with their depth.
- **Tunnelling:** bodies more than 1 m below the lowest legitimate rest surface.
- **Per-body view:** the fastest awake bodies with colour, constraint count and tilt.

**API:**
- `POST /api/debug/settle_probe {"op":"start|stop|status", "floor_y", "series_last", "bodies", "csv"}`
- `POST /api/debug/spawn_gpu_lattice` for deterministic packed or separated grids.

**Bench:** `python tools/debris_settle_bench.py [--only …] [--seconds N] [--tag T]`. It runs
six scenarios on CharacterTestbed, writes `docs/evidence/debris_settle/<tag>/`, prints a table,
and exits 0 only if every scenario passes. **This is the done-gate for the fixes.**

| scenario | what varies vs. its control |
|---|---|
| `drop_layer` (control) | 36 separated cubes dropped 2 m: no packing, no walls |
| `drop_pile` | 150 cubes, loose 5×6×5 drop (the July Phase-2 verification case) |
| `packed` | 216 cubes spawned touching, at rest, flat ground (packing only) |
| `crater` | the same packing inside a 6×4×6 pit (adds static walls) |
| `crater_subcube` | 1,296 touching ⅓-scale pieces in a 4×3×4 pit (real debris size) |
| `blast` | real `/api/damage/apply` r = 3.5, e = 600 (`DamageSystem` debris) |

**Pass criteria** (Analyzer `Config`, all must hold):
- mean rebounds per body after 1 s ≤ 0.01;
- injected energy after 1 s ≤ 1 mm of pile lift;
- all asleep by 4 s;
- 0 forced sleeps;
- 0 colour-skipped bodies and 0 dropped constraints;
- 0 hard-contact push-outs after 1 s;
- 0 tunnelled bodies.

### Baseline: `docs/evidence/debris_settle/baseline-2026-10-03/` (Debug, CharacterTestbed)

| scenario | bodies | verdict | rebounds / body (after 1 s) | max rebounds | injected lift mm (after 1 s) | all asleep | forced sleeps | hard-contact pushes after 1 s | deepest push mm | tunnelled |
|---|---|---|---|---|---|---|---|---|---|---|
| drop_layer | 36 | FAILS | 0.00 | 0 | 30 (24) | 2.55 s | 20/36 | 1,350 | 2.7 | 0 |
| drop_pile | 150 | FAILS | 3.6 (3.5) | 21 | 0 (0)* | never | 65/124 | 63,668 | 97 | 38 |
| packed | 216 | FAILS | 7.7 (4.3) | 36 | 106 (0)* | never | 59/113 | 79,672 | 97 | 66 |
| crater | 144 | FAILS | 34.9 (29.2) | 91 | 4,833 (3,477) | never | 22/33 | 30,101 | 301 | 10 |
| crater_subcube | 1,296 | FAILS | 26.9 (25.1) | 223 | 64,493 (53,529) | never | 47/60 | 487,140 | 694 | 31 |
| blast | 326 | FAILS | 13.2 (12.0) | 176 | 7,239 (2,423) | never | 93/129 | 185,005 | 515 | 19 |

\* When bodies tunnel out of the world, their potential-energy loss outweighs the injection,
so the system sum under-reads. Rebounds, hard-contact pushes and tunnelling are the robust
signals in those rows.

**Visual** (`…/frames/`): 144 cubes placed at rest inside a 6×4×6 pit had **erupted up out of
the hole** by t ≈ 6 s and were scattered across the grass by t ≈ 31 s. At that point 117/144
were still awake, rebounding ~5/tick, with hard-contact pushes on 26 bodies/tick. File names
carry the actual capture times: each Debug capture costs ~6 s of sim.

### Mechanisms already caught by the instrument
1. **Hover / anti-gravity (control scenario).** After landing, 8–12 cubes hover **tilted 8.2°**
   on an edge with **0 contact constraints**. The hard-contact pass fires on every one of them
   **every tick**: the primal applies gravity (≈ −1.8 mm), then hard-contact pushes the edge out
   of the ground (≈ +2.7 mm). Velocity is derived from displacement, so the body drifts upward
   at +0.05–0.1 m/s and climbs to 9–11 cm above the floor. Then the lax tier force-freezes it in
   mid-air. This is the long-open `project_debris_hover_bug`, now reproduced with numbers.
   Confirms **H1**. The contact pass (pre-solve, with `accelWeight` gravity back-off) and the
   hard-contact pass (post-solve) disagree about whether the body touches the ground (**H8**).
2. **Tunnelling through 9 m of rock.** In every stacked scenario bodies leave the world through
   a floor that is solid from y = 8 to y = 16 (verified with `/api/world/voxel`). Hard-contact
   push-outs reach 10–70 cm deep, i.e. bodies are pushed deep into the terrain and then expelled
   along the wrong axis. **The July Phase-2 claim that "150-cube drops all sleep" does not
   reproduce today** (`drop_pile`: 38 of 150 tunnelled, never all asleep). That scenario differs
   slightly (y = 20 vs. 40), and whether it is a regression is not established.
3. **Colour overflow (H2) exists but is minor** at these sizes: max 1–4 skipped bodies, 362 in
   the 1,296-piece crater. **No constraint drops** were observed.

Operational notes: Debug carving (`/api/world/clear`) stalls the game loop for 40–120 s while
it remeshes, so the bench waits and verifies the carve. A Debug screenshot costs ~6 s of sim
time.
**Symptom (user report):** GPU debris packed together never loses energy. A crater full of
loose voxels looks like it is *bubbling*: pieces keep bouncing off each other instead of
falling, colliding a few times, and settling like real rubble. Reference behaviour:
[`sbobyn/three-avbd`](https://github.com/sbobyn/three-avbd) (AVBD, same algorithm family as
ours) and [`Stink-O/box3d-godot`](https://github.com/Stink-O/box3d-godot) (Box3D Soft Step).
Both show piles that hit, tumble briefly and go dead still.

**Scope:** the GPU debris solver `GpuParticlePhysics` (`shaders/solver_*.comp`). That is the
path blast/break debris takes (`DamageSystem::spawnDebris` → `queueSpawn`). The CPU
`VoxelDynamicsWorld` already had its Box3D overhaul (`docs/PhysicsRestOverhaul.md` Phase 1)
and is out of scope unless the harness shows otherwise.

Prior reading (all still accurate unless noted): `docs/PhysicsRestOverhaul.md` (Phase 2 = GPU
sleep), `docs/AvbdSolverAudit.md` (R1–R5; R1 was never done).

---

## 1. Why the 2026-07-31 sleep work did not fix this

Sleep in GPU Phase 2 only freezes a body once it is **already** slow (< 0.05 m/s for 30 ticks,
or < 0.15 m/s for 180 ticks). Any awake body above 0.5 m/s that touches a sleeper wakes it.
Phase 2 was verified with a **drop** of separated cubes onto flat ground, which is the easy
case. A crater is the hard case: dozens of bodies spawned **packed and touching** (subcube
debris is spawned on an exact 3×3×3 lattice, `DamageSystem.cpp:271-281`), inside a hole whose
walls are static voxels. If the solver keeps adding energy, nothing drops under the sleep
thresholds, and each bounce over 0.5 m/s wakes the neighbours that did fall asleep. Sleep
**hides** a settled pile. It cannot settle one that keeps gaining energy. The fix has to stop
the energy from being added in the first place. We must **not** "fix" this by loosening sleep
thresholds. That would be the same freeze-hack mistake Phase 1 removed from the CPU solver.

## 2. Where the energy comes from — ranked hypotheses (grounded in source)

The single fact behind most of them: **velocity is not solved, it is derived from position**
(`solver_sync_out.comp`: `solvedVel = (b.pos - b.initial) / dt`). So **any displacement during
the tick turns into kinetic energy**, including displacement that only fixes an overlap. A
1 cm push-out at 60 Hz becomes 0.6 m/s of launch velocity, more than the 0.5 m/s wake
threshold.

| # | Suspect | Evidence | Why it bubbles | Predicted signature |
|---|---------|----------|----------------|---------------------|
| **H1** | **Hard-contact projection turns push-out into launch velocity** | `solver_hardcontact.comp:173-187` moves `b.pos` out by the MTV and writes `b.vel`. But `sync_out` ignores `b.vel` and re-derives velocity from `pos - initial`, so the push-out **is** the velocity. Its "kill velocity into the surface" line has no effect | Every body the soft solver leaves overlapping the terrain or crater wall gets kicked out at depth/dt. Crater debris sits against static walls on many sides | Bounce events correlate 1:1 with hard-contact fires; bottom layer and wall-adjacent pieces pop |
| **H2** | **Bodies skipped by graph coloring (audit D1, still unfixed)** | `solver_body_color.comp:57` ignores neighbour colours ≥ 12; `findLSB` can assign ≥ 12; anything uncolored after 16 Jones-Plassmann rounds stays `UNCOLORED`. Primal only dispatches colours 0–11 (`GpuParticlePhysics.cpp:1011`) | A packed 3-D cluster has up to 26 neighbours per body. A skipped body gets **zero** primal updates and follows its gravity prediction into its neighbours. The next tick resolves that overlap, and the resolution becomes velocity (see top) | Count of skipped bodies > 0 only in packed clusters; skipped bodies show the highest speeds |
| **H3** | **Overlap correction feeds velocity (no post-stabilization)** | `C_n = -(1-α)·C_init + J·Δq` with α = 0.99 corrects 1 % of the initial overlap per tick, and all of that correction lands in `pos - initial` → velocity. There is no split between "solve for velocity" and "fix the leftover error" | Small by itself at α = 0.99, but it adds up across a packed pile and **never decays to zero** while overlaps keep being re-created (H2, spawn overlap) | Steady micro-churn of 0.05–0.2 m/s: the "stragglers" Phase 2 needed the lax sleep tier to hide |
| **H4** | **New contacts start soft (κ = 1)** | `PENALTY_MIN = 1.0`; a contact with no warm-start entry starts almost without stiffness and ramps by β·\|C\| per iteration. In a churning pile, contacts keep breaking and re-forming, so many are cold every tick | A cold contact lets the body sink in during that tick. The ramped stiffness then pushes it back out, and that push-out becomes rebound velocity (see top) | Bounce events cluster on contacts with `warmstart miss`; better with a higher κ_start |
| **H5** | **Chain-waking** | `WAKE_IMPACT_SPEED = 0.5` m/s, set by narrowphase whenever an awake body touches a sleeper above that speed | One bubbling body keeps waking the sleepers around it, so the cluster never stays asleep | Wake bits/tick stay > 0 long after the impact |
| **H6** | Spawn packing / overlap | Subcube debris on an exact lattice, plus ±0.3·speed jitter, plus spin of ±4 rad/s (`DamageSystem.cpp:98`). Rotated cubes on a touching lattice **overlap immediately** | Starts the pile with built-in overlap energy (fed through H1/H3/H4) | Energy rises in the first ticks after spawn, before any impact |
| **H4b** | **Feature-ID flicker resets warm-started stiffness** | Warm start is keyed by exact contact feature (`solver_types.glsl` hash). three-avbd `docs/FINDINGS.md` measured this on the same algorithm: a clip vertex renamed between frames dropped one contact's κ from ~19,000 to 1 while its twin kept the high value. That 1.4e-3 rad kick walked a 20-box stack over in 2 s. Their fix is `matchNearest`: on a key miss, inherit from the pair's previous contact nearest in A-local anchor, within 5 % of the smaller box | Packed rotated cubes sit on clip-plane boundaries constantly, so stiffness keeps flickering, and each flicker is a kick | `WS_MISS` stays high in a pile that looks static; kicks are asymmetric (body tilts) |
| **H8** | **The starting guess pushes resting bodies into their supports** | `solver_integrate.comp` starts every iteration from `inertial = pos + vel·dt` and only backs out gravity (`accelWeight`). An unconverged velocity from last tick is still extrapolated. three-avbd FINDINGS Stage 8m found exactly this: the extrapolation "turns jitter into a push down into the supports"; walls swelled 5–7 cm and buckled. Their shipped fix is `startAtRest`: if `\|v\| + \|ω\|·r < 0.1 m/s`, start the iterations from x⁻ (last position) instead. Cutoffs of 0.01–0.03 m/s still failed; 0.1–0.3 m/s stood 60 s | Each tick's leftover velocity becomes the next tick's overlap → corrected → velocity again. A self-sustaining loop exactly at sleep-threshold speeds | Churn plateau at 0.05–0.2 m/s (the "stragglers" Phase 2 needed the lax tier for); goes away when iterations start from x⁻ |
| H7 | Fidelity drift vs AVBD reference | `GAMMA` also scales damping (audit P6). No static-friction anchoring (audit P4: three-avbd keeps the old anchors while `stick` is set; ours saves `stick` but nothing reads it), so packed cubes slide and roll instead of sticking. Friction is an average (reference: `sqrt(μA·μB)`) | Probably second-order | Tangential sliding in settled piles |

Unknown until measured: which of H1, H2, H4/H4b and H8 dominates. **The plan is measure first, then fix one
hypothesis at a time with an A/B switch.** That is how Phase 1's red baseline proved its
diagnosis.

---

## 3. The test method (Phase A — build this BEFORE any fix)

Today there are **zero automated tests of the GPU solver** (`grep GpuParticlePhysics tests/`
finds nothing). Every past "it settles" claim was a screenshot or a one-off CSV, and every one
used separated drops, not a packed crater. The harness has three layers.

### A1. Solver telemetry counters (GPU → CPU, every tick)

Use the spare header slots of the solver-state buffer (`solver_types.glsl`: `[4..7]` are free
before `HASH_BASE = 8`; widen `HASH_BASE` if more are needed) and read them back on the
existing readback path. Per tick:

| Counter | Written by | Answers |
|---------|-----------|---------|
| `HARDCONTACT_FIRES`, `HARDCONTACT_MAX_DEPTH` (float bits via atomicMax on uint) | `solver_hardcontact` | H1 |
| `COLOR_SKIPPED` (color ≥ 12 or UNCOLORED, among awake bodies) | primal / a tiny count pass | H2 |
| `CONSTRAINTS_DROPPED` (past `MAX_CONSTRAINTS`) | narrowphase / voxel emitters | audit D2 |
| `WS_MISS` (contacts created cold) | narrowphase / voxel (already counts hits) | H4 |
| `WAKE_SET` | narrowphase / integrate | H5 |
| Sum of KE (fixed-point atomicAdd), `MAX_SPEED`, `AWAKE`, `SLEEPING` | `sync_out` | the settle curve |
| **`REBOUNDS`**: bodies whose vertical velocity went from ≤ 0 last tick to > +0.3 m/s this tick while they had a contact | `sync_out` (needs `prevVel.y`, which already exists on `SolverBody`) | **the bubbling metric itself** |

Expose these as `GET /api/debug/gpu_solver_stats` (last tick + a ring buffer of N ticks) and
keep them permanently. They are cheap, and the audit already flagged that silent failures here
are the core problem.

### A2. Headless GPU rigs (deterministic, run in CI-style integration tests)

New `tests/integration/GpuDebrisSettleTest.cpp` on `VulkanPhysicsTestFixture` (it already
creates a real `VkDevice`; needs a thin `Vulkan::VulkanDevice` adapter so
`GpuParticlePhysics::initialize` can run headless). Each rig: hand-written occupancy (no
ChunkManager), fixed spawn list with a seeded RNG, step with a fixed dt = 1/60 through a
one-shot command buffer, read back particles + counters every tick. **Each rig fits inside one
chunk, varies one thing, writes down its prediction first, and has a control** (CLAUDE.md
test-rig rule).

| Rig | Setup | Control it pairs with |
|-----|-------|-----------------------|
| R0 `SingleDrop` | 1 cube dropped 3 m onto flat floor | baseline sanity (must already pass) |
| R1 `SeparatedDrop150` | the Phase-2 scene: 150 cubes, gaps between them | the case that already works |
| **R2 `PackedBlockRelease`** | 6×6×6 cubes spawned **touching**, zero velocity, on flat floor | R1. Same count, only the packing differs |
| **R3 `CraterRelease`** | 9×9×4 bowl carved into solid occupancy, filled with touching subcube-scale debris, zero velocity | R2. Same packing, only the static walls differ (isolates H1) |
| **R4 `BlastCrater`** | Replays `DamageSystem::spawnDebris` exactly (lattice + outward velocity + jitter + spin) into the R3 bowl | R3. Same geometry, adds the real spawn energy (isolates H6) |
| R5 `RotatedPacking` | R2 with a random spin on each cube at spawn | R2 (isolates spawn overlap) |
| R6 Stress (scale axis) | R4 with N = 27 → 729 → 5,000 pieces; crater depth 2 → 8 | must hold **at every N**, not on average |

**Invariants, asserted every tick (not just at the end).** These encode "falls, collides a few
times, settles":

1. **Energy only goes down after the impact window.** Let t₀ = the tick the last body first
   touches something. For t > t₀ + 0.25 s: `KE(t) ≤ KE(t−1) · (1 + 1e-3) + ε` (allowing
   float noise). Gravity can only add energy to a body in free fall, so the check excludes
   bodies that have no contact this tick.
2. **Bubbling is gone.** `REBOUNDS` summed over t > t₀ + 0.5 s ≤ 0.01·N, with each body
   rebounding at most 3 times in total. Real rubble bounces 1–3 times; bubbling is hundreds.
3. **Full rest in bounded time.** 100 % `SLEEPING` by t₀ + 3 s for R2–R5 (R6: + 5 s), **with
   the lax sleep tier disabled for the test**. Sleep must come from the physics, not from the
   force-freeze safety net.
4. **No silent solver failures:** `COLOR_SKIPPED == 0`, `CONSTRAINTS_DROPPED == 0` on every
   tick; `HARDCONTACT_FIRES == 0` after t₀ + 0.5 s (hard-contact stays a rare safety net).
5. **The pile stays a pile:** no body centre below the floor or inside a solid cell,
   horizontal spread from the crater centre < (crater radius + 2), and nothing launched
   above crater rim + 1 m after t₀.
6. **Stays still** (existing Phase-2 property): after full sleep, +600 ticks are
   bit-identical.

Each test writes its tick series to CSV (`build/test_out/gpu_settle/<rig>.csv`: t, KE,
maxSpeed, awake, rebounds, hardcontact, skipped, wakes) so a red run can be diagnosed by
plotting, not by guessing.

**Expected red baseline (write it down before running):** R0, R1 green. R2 red on
invariants 2–3. R3 worse than R2 (H1). R4 the worst, with an early energy rise (H6). Any
COLOR_SKIPPED > 0 in R2–R6 confirms H2. If R2 is green and only R3/R4 are red, H1 is the
lead. If R2 is already red without walls, H2/H3/H4 are. This table is the first deliverable
and decides the order of the fixes in §4.

### A3. Live L4 verification (in the engine, after the rigs are green)

- New `POST /api/debug/physics_rig {"rig":"crater","n":..,"seed":..}` replays R3/R4 in the
  running engine on a flat test world (CharacterTestbed interior, x125–140, away from the
  floating-platform edge). Plus the **real thing**: a fireball / `apply_damage` crater blast.
- Evidence: the `gpu_solver_stats` ring buffer (raw JSON saved to
  `docs/evidence/debris_settle_*.jsonl`), plus a before/after screenshot sequence at a stated
  camera pose (t = 0.5 s, 1 s, 2 s, 4 s after the blast). Also a short `gif`/frame strip so the
  "bubbling" is visible in the before capture and gone in the after one (CLAUDE.md: the defect
  must be **in frame** in both). Release build for timings. Debug is fine for behaviour.
- three-avbd's `docs/FINDINGS.md` is a model for this kind of harness. It compares a CPU oracle
  with the GPU solver to max-pose-diff 0, and runs long-horizon stack and ring "stands N
  seconds / drift X cm" tests. Our R-rigs follow the same idea.
- Side-by-side reference: record the three-avbd box-pile demo at a similar body count and put it
  next to ours. That shows "settles like the reference" directly, not just against a number.

---

## 4. Fixes (Phase B) — one at a time, each behind a runtime toggle, red → green on the rigs

Every fix gets a push-constant / `SolverTuning` flag settable via
`POST /api/debug/gpu_solver_tuning`, so A/B runs need no rebuild. Fixes are ordered by
expected value. Re-order them according to what the A2 baseline shows.

### B1. Stop push-out from becoming velocity (H1, H3) — *post-stabilization*
This is the AVBD reference's own answer, and the one Box3D's relax pass and PhysX's
`maxDepenetrationVelocity` express in their own ways: **error correction moves the position
but must not appear in the velocity.**
- **Hard-contact pass:** apply the MTV to **both** `b.pos` and `b.initial`, so `pos - initial`
  is unchanged and the push-out adds no velocity. Then project out the into-surface component
  of the *derived* velocity, the way the shader's comment already intends (move the intent
  into `sync_out`, which is where velocity is actually decided).
- **Main solve (post-stabilize, optional):** solve the iterations with α = 1 (no error
  correction), take velocity = `(pos - initial)/dt` **at that point**, then run 1 extra primal
  pass with α = 0 (full correction) whose displacement updates `pos` only and is excluded from
  velocity. This is the avbd-demo**2d** default (`postStabilize: true`). The 3D reference does
  **not** use it: it relies on α = 0.99, and three-avbd's parallel params (α = 0.95, no
  post-stab) measured lower error. So this only goes in if the rigs show H3 still matters after
  B1-hard-contact + B2 + B7. Gate: `postStabilize` flag.
- **Safety cap:** clamp the velocity contribution of overlap correction to a max
  depenetration speed (start at 1 m/s, PhysX-style), so a deep spawn overlap separates calmly
  instead of exploding.

### B2. Every awake body is solved every iteration (H2, audit D1)
- Colour pass: build the used-colour mask over **all** neighbour colours, and add overflow
  colours (dispatch colours ≥ 12 in extra primal passes, up to the max actually used, read from
  a counter), **or** the paper's §4 double-buffered primal so a colour conflict falls back to a
  Jacobi update instead of a skip. Also raise the JP round count or loop until colouring is
  complete.
- Invariant 4 (`COLOR_SKIPPED == 0`) becomes a permanent assertion in the R6 stress rig.
- Raise or adapt `MAX_CONSTRAINTS`, and count drops (audit D2). A packed crater is exactly
  where 6 constraints/body is exceeded.

### B3. Cold contacts start stiff, and warm start survives feature flicker (H4, H4b)
- `massPenalty` (three-avbd GPU default): seed new normal rows at
  `κ₀ = max(PENALTY_MIN, m_lighter/dt²)` instead of 1.0.
- `matchNearest` (three-avbd GPU default): on a warm-start key miss, inherit λ/κ/anchors from
  that pair's previous contact nearest in A-local anchor position (≤ 5 % of the smaller box).
  This needs the previous frame's contacts per pair. The hash already holds them under the
  pair-prefixed key, so the lookup is a probe over the pair's ≤ 8 feature slots.
- Split `GAMMA` into `WARMSTART_GAMMA` and an explicitly named `LINEAR_DAMP_SCALE` (audit P6),
  so tuning one no longer changes the other.

### B7. Slow bodies start their iterations from rest (H8) — `startAtRest`
- In `solver_integrate.comp`: if `|v| + |ω|·r < 0.1 m/s`, set the starting iterate to x⁻
  (`b.initial`) instead of `inertial − gravity backoff`. Keep `inertial` as the energy minimum,
  so gravity still acts through the solve. Faster bodies keep the current guess (three-avbd: a
  start-from-x⁻ applied to every body made a sliding box go 2.26 m instead of the Coulomb 3.16 m).
  This replaces our velocity-magnitude `accelWeight` heuristic with the reference's measured
  rule. It is a one-line, toggleable change, so **run it first in the A/B sweep**.

### B4. Spawn without built-in energy (H6)
- `DamageSystem::spawnDebris`: shrink spawn extents by a skin (or space the lattice by 1.02×)
  so freshly spawned debris does not overlap. Spin only grows to its full ±4 rad/s value after
  the body separates, or is reduced for interior lattice pieces. The blast's look must stay the
  same, so verify with the R4 capture, not just the numbers.

### B5. Wake only on meaningful impacts (H5)
- Raise the wake threshold relative to the sleeper (relative normal speed, not the awake body's
  absolute speed), and require the impact **impulse** (λ) to exceed a threshold, so resting
  jitter can never wake anything. Only after B1–B4. Re-check Phase-2's bombardment-wake test
  so real impacts still wake.

### B6. Reference-fidelity follow-ups (H7, lower priority)
- Static-friction anchoring (paper §3.3, audit P4: the `stick` flag is saved but nobody reads
  it), so packed cubes stop rolling. Optional per-body inertia tensor for long subcube rods.

### Suggested A/B order (cheapest first; re-order once the A2 baseline shows which counters are hot)
B7 (startAtRest, one line) → B1 hard-contact (`initial` shift, a few lines) → B3 massPenalty →
B2 colour overflow → B3 matchNearest → B4 spawn → B5 wake → B1 post-stab only if still needed.

### Success = the user's description, encoded
After the fixes above: in R2–R6 and the live blast, every piece falls, has a few collisions (≤ 3
rebounds), and the whole pile is asleep with exactly zero velocity within 3 s, **with the lax
force-freeze disabled**. The live capture shows no bubbling, and the side-by-side looks like
three-avbd's pile. Then decide whether the lax tier can be retired (it should only have been
hiding H1–H4).

---

## 5. Validation ledger

| Item | Contract | Required layer | Red test |
|------|----------|----------------|----------|
| GPU debris settling | packed/crater debris loses energy monotonically after impact, ≤ 3 rebounds/body, 100 % asleep ≤ 3 s without lax freeze | **L2** (headless rigs, every-tick invariants) + **L4** (live blast, stats JSONL + frame strip) | R2–R4 red baseline captured first (§3 A2) |
| Solver silent failures | 0 skipped bodies, 0 dropped constraints at N = 5,000 | L2 | R6 at N = 5,000 shows counters > 0 today |

## 6. Risks / things to watch

- **Behaviour retune.** Post-stabilization makes debris "deader". Check that blasts still
  throw debris convincingly (R4 capture before/after at t = 0.25 s).
- **Character shove** (`solver_integrate.comp` re-anchors `inertial`) must still push debris.
- **Determinism of the rigs.** GPU atomics make the order nondeterministic across runs, so
  assert invariants with tolerances, never bit-exact trajectories (except "stays still after
  sleep").
- **Perf.** Overflow colours add dispatches. Measure with the existing GpuProfiler scopes on
  R6 in Release (do not judge timings in Debug).
