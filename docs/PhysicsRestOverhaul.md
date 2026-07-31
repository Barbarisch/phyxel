# Physics Rest Overhaul — voxels come to a FULL stop

**Goal:** Phyxel's dynamic voxels (furniture, felled trees, break debris, fragments, GPU debris
piles) settle like Box3D's demos — stacks come to a literal, provable, zero-motion rest, stay
that way, and cost ~nothing while resting.

**Reference:** Erin Catto's Box3D ("Soft Step" solver, the 3D successor of Box2D v3's TGS Soft),
studied via the `Stink-O/box3d-godot` fork (`src/contact_solver.c`, `src/solver.c`,
`src/island.c`). The relevant recipe is fully portable; no code is copied.

---

## 1. Why our voxels never fully rest — diagnosis (2026-07-31)

Two independent solvers, two independent failure sets.

### CPU `VoxelDynamicsWorld` (furniture, felled trees, break debris, coherent fragments)

Sequential-impulse PGS, 10 iterations, one 1/60 substep. Four textbook resting-contact defects:

| # | Defect | Evidence | Consequence |
|---|--------|----------|-------------|
| 1 | **No warm starting** — accumulated impulses zeroed every substep | `VoxelContactSolver.cpp` `prepareContact`: `cp.lambdaN = cp.lambdaT1 = cp.lambdaT2 = 0.0f`; no persistent manifold anywhere | A resting stack re-derives the entire force balance from zero each frame; 10 cold Gauss-Seidel iterations can't converge multi-contact stacks → perpetual micro-bounce |
| 2 | **Raw Baumgarte bias into real velocity** | `bias = BAUMGARTE/dt * max(0, depth - SLOP)` folded into `targetVelocityN` | Penetration correction becomes genuine upward velocity ("breathing"), which also keeps the sleep velocity test from ever passing |
| 3 | **Unstable manifolds** — contacts rebuilt each substep, no feature identity, min-axis SAT normal can flip between near-tied axes | contact gen keeps points in a ±10 mm band, cap 4, no matching | The constraint set the solver sees changes frame to frame; impulse solution jumps |
| 4 | **Per-body sleep, no islands, and sleeping bodies generate NO contacts** | `generateContacts()` awake-list excludes sleeping bodies entirely | (a) bodies in a pile can't sleep while neighbours jiggle; (b) **a falling body passes clean through a sleeping body** (no contact is ever generated) — a real, observable bug |

The shipped "fix" is an admitted freeze-hack: the position-based sleep fallback
(`VoxelRigidBody.h` `SLEEP_POS_EPS/SLEEP_POS_TIME` — "a body that has not actually MOVED sleeps
even if contact-solver jitter keeps spiking its velocities"). It hides the jitter after ~1 s but
allows up to 5 cm of visible wobble until then, and does nothing for defects 3–4.

### GPU `GpuParticlePhysics` AVBD (mass debris/destruction)

The AVBD dual/primal path with penalty warm-starting works and piles do settle *approximately*
(2026-05-31 verification: 800 cubes, stable pile). Remaining gaps:

- **No sleep system at all.** Nothing ever becomes literally still; the solver runs the full
  dual/primal loop on settled piles forever (GPU cost + residual micro-motion). The
  `accelWeight` gravity-gating heuristic in `solver_integrate.comp` approximates rest but does
  not freeze anything and interacts with the warm-start equilibrium.
- **Rest-height offset** — debris rests visibly above the ground
  (memory `project_debris_hover_bug`); a rest-position correctness bug, deferred but part of
  "rest looks right".

---

## 2. What Box3D actually does (the recipe)

Extracted from `contact_solver.c` / `solver.c` / `island.c`:

### 2.1 Soft constraints instead of Baumgarte

```c
// b3MakeSoft(hertz, zeta, h):
omega = 2π·hertz
a1 = 2ζ + h·ω
a2 = h·ω·a1
a3 = 1/(1+a2)
biasRate     = ω / a1
massScale    = a2·a3
impulseScale = a3
```

Normal solve (biased pass), separation `s` (negative = penetrating):

```c
if (s > 0)          bias = s / h;                                  // speculative
else if (useBias)   bias = max(massScale·biasRate·s, -maxPushSpeed);
impulse = -normalMass·massScale·(vn + bias) - impulseScale·totalImpulse;
```

`massScale`/`impulseScale` are the crucial part vs. plain Baumgarte: they blend the new impulse
against the accumulated one so the bias force is *critically damped* — penetration is pushed out
without injecting net energy. Defaults (Box2D v3 lineage): contact hertz 30 (clamped to ¼ of the
substep rate), damping ratio ζ = 10, **static contacts use 2× hertz** (stiffer against the
ground), push-out speed cap ~3 m/s.

### 2.2 Substep structure with a bias-free relax pass

Per substep: integrate velocities → warm start (apply cached impulses) → solve with bias →
integrate positions → **relax: re-solve with `biasRate=0, massScale=1, impulseScale=0`** →
(after all substeps) restitution as a separate pass → store impulses for next step.

The relax pass is the anti-jitter core: any velocity the bias injected this substep is removed
again while still respecting the contacts, so at equilibrium bodies carry ~zero residual
velocity into the sleep test.

### 2.3 Restitution as a separate final pass

Only applied where approach speed at contact creation exceeded a threshold AND the contact
actually generated impulse — resting contacts never bounce.

### 2.4 Island-based sleeping (the "full stop")

- Per body: `sleepVelocity = max(|v| + reach·|ω|, positionFactor·Δpos/h)`; below threshold →
  `sleepTime += dt`, else reset.
- An **island** (bodies connected through contacts/joints; statics excluded) sleeps only when
  *every* member has `sleepTime ≥ TIME_TO_SLEEP` (0.5 s class). On sleep: velocities zeroed,
  island moved to a sleeping set that costs nothing per step.
- Wake: contact from an awake body, island touched by a destroyed/moved neighbour, explicit
  wake — waking wakes the *whole island*.

This is why Box3D piles look supernaturally calm: they are not simulating at all.

---

## 3. Plan

### Phase 1 — CPU soft-step solver (`VoxelContactSolver` + `VoxelDynamicsWorld`)

The straight Box3D port. Deterministic, unit-testable (L2 red-before-green), touches
furniture/fells/debris — the most user-visible resting bodies.

1. **Persistent manifold cache + warm starting.** `unordered_map<pairKey, CachedManifold>` owned
   by the world; pair key = body ids (+ terrain cell / box indices); points matched by contact
   position (2 cm tolerance). Prepare stays parallel (no body writes); a sequential warm-start
   pass applies `λ·n + λ₁·t₁ + λ₂·t₂` before iterating. Impulses stored back after the solve.
2. **Soft constraints.** Replace `BAUMGARTE/dt` bias with `makeSoft(contactHertz=min(30,
   0.25/h), ζ=10)` dynamic-dynamic and `makeSoft(2·contactHertz, ζ)` vs terrain/static;
   push-out cap 3 m/s; speculative branch for separated (s>0) points (contact gen already
   emits a ±10 mm band).
3. **Relax pass + restitution pass.** Substep becomes: integrate vel → contacts → prepare →
   warm start → N biased iterations (normal+friction) → integrate positions → M bias-free relax
   iterations → restitution pass (threshold 1 m/s, captured approach speed) → store manifolds.
   Start N=8, M=4 (one substep; Box3D amortizes over 4 substeps — we can move to true
   sub-substepping later if quality demands it).
4. **Island sleep + wake-on-contact.** Union-find over this substep's dynamic-dynamic contact
   pairs; island sleeps when all members qualify (velocity thresholds; `SLEEP_TIME` 1.2 → 0.5 s
   now that jitter is gone). Sleeping bodies **stay in the broadphase as contact candidates**:
   awake-vs-sleeping contact with approach speed > wake threshold wakes the sleeper (and, via
   its stored at-sleep touching set, its neighbours); below the threshold the sleeper is
   treated as static so things can rest ON sleeping bodies without waking them. Fixes the
   pass-through bug. On sleep, each body stores the ids it was touching → transitive wake.
5. **Keep** the position-fallback sleep as a safety net (it should become nearly unreachable),
   the anti-tunneling speed clamp, water coupling (wet bodies never sleep), and the kinematic
   obstacle wake rules.

**Non-goals Phase 1:** true 4× sub-substepping, twist/rolling friction, terrain-edit wake
(voxel removed under a sleeping body — pre-existing, tracked as follow-up), CCD.

### Phase 2 — GPU AVBD sleep (separate session)

Per-body `sleepTime` persisted in the particle (survives sync_in), neighbour-gated sleep via the
existing CSR adjacency (a body may sleep only if all contact neighbours also qualify —
approximate islands, converges over frames), frozen bodies skip integrate/narrowphase/solve and
are pinned; wake on: contact from a fast awake body, occupancy change in their cells, character
segment push, nearby spawn. Plus the rest-height (hover) fix. Runtime-verified with a
rest-metric readback endpoint (max/avg speed, % asleep).

---

## 4. Validation (planned up front, per CLAUDE.md)

Contract: **"fully at rest" = every body in the pile asleep with exactly-zero velocity within a
time budget, no tunneling, no visible pre-sleep wobble, and staying put indefinitely.**

Required layer: **L2** (deterministic structural invariants on the real solver) for Phase 1,
plus **L4** runtime confirmation in-engine. Red tests first: `tests/core/RestingContactTest.cpp`

| Test | Invariant | Expected today |
|------|-----------|----------------|
| SingleBoxSettlesToFullRest | box sleeps ≤ 2.5 s after drop, velocity == 0, rest height = floor + 0.5 ± 1 cm | jitter-marginal |
| StackFullyRests | 5-stack: all asleep ≤ 4 s, zero kinetic energy, stack coherent (no interpenetration > 1 cm) | RED (jitter + per-body sleep) |
| NoVisibleWobbleWhileSettling | after touchdown, max per-step COM motion < 1 mm | RED (Baumgarte breathing ≈ cm-scale) |
| FallingBodyLandsOnSleeper | dropped box must NOT tunnel a sleeping box; rests on top; sleeper wakes on impact | **RED — tunnels today** |
| PyramidStress (stress phase) | 21-box pyramid: all asleep ≤ 8 s, XZ drift < 0.3 m, active count 0 afterwards | RED |
| SleepIsForever | slept pile stepped +10 s: bit-identical positions | red via pre-sleep drift |

L4 (after green): in-engine — fell a tree + break a wall + shove furniture; confirm piles go
literally still (active count 0 via debug stats), impacts wake correctly, characters can still
shove furniture awake.

## 4b. Status (2026-07-31)

Phase 1 **implemented and L2-verified**:
- Red baseline captured first (3/6 red): stack collapsed into itself and the dropped box
  tunneled through the sleeper (diagnosis #4 confirmed at runtime), pyramid scattered 5 m.
- After the port: **all 6 RestingContact tests green** — piles fully sleep with exactly-zero
  velocity, world goes idle (active count 0), slept piles are bit-identical +10 s later,
  impacts wake the island and it resettles.
- Full unit suite: 3052 tests, only failures are 2 `AIEndToEndTest` network tests (retired
  LLM model id, HTTP 404 — environmental, unrelated). All 47 physics-related integration
  tests (TreeCollapse/CoherentCollapse/ChopKerf/PhysicsIntegration) green.
- `SceneIntegrationTest` failures observed are in loader code that carries pre-existing
  uncommitted changes from the water workstream (not physics; baseline not A/B'd).
- **L4 runtime pass still owed** (fell a tree / shove furniture / watch active-count → 0 in
  the editor): blocked this session because a running WaterLab engine held the `phyxel.exe`
  link lock. Also still owed: perf sanity via PhysicsBenchmarks (solve now runs 10 biased +
  4 relax iterations, but warm starting cuts convergence work at rest to ~zero).

## 5. Risks

- **Behavior retune:** furniture/fell/debris all feel slightly different (stiffer contacts,
  faster sleep). Existing tests (WaterBuoyancy, CoherentCollapse, TreeCollapse, ChopKerf,
  PhysicsIntegration) are the regression net and must stay green.
- **Determinism:** manifold cache iteration must not reorder solve; keyed storage, contact order
  stays vector order.
- **Perf:** manifold cache adds a hash lookup per contact; sleeping-as-candidates adds broadphase
  entries (but solve cost for them is zero and sleeping bodies skip terrain phase). Net win at
  rest (active count → 0).
- **Wake correctness:** too-eager wake = piles never sleep; too-lazy = floating furniture when
  support removed. Wake threshold + stored touching-set transitive wake; terrain-edit wake is a
  named follow-up.
