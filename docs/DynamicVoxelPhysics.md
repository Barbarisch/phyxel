# Dynamic Voxel Physics System

## Overview

> **Current state (2026-10-07, [DebrisInteractionPlan.md](DebrisInteractionPlan.md) complete).**
> Every broken piece is **GPU debris**. There is no CPU debris path and no FPS-based routing: the
> CPU single-box path (`DynamicObjectManager`, `addGlobalDynamicCube`) was deleted in Plan 1d.
> Debris collides with everything that moves (characters, CPU bodies, doors, held items):
> - blasts and spells push it;
> - it pushes characters back;
> - it floats or sinks in water;
> - it emits sleep/impact events, which drive audio and gatherable rubble;
> - it runs in shipped games.

The engine has two physics worlds, split by role:

- **GPU AVBD debris** (`GpuParticlePhysics`, Vulkan compute) simulates every broken voxel.
  - Every break goes through `DamageSystem::spawnBreakDebris`: the B key, Python breaks,
    `/api/damage/apply` blasts, spells, chops and derez.
  - The solver is owned by `DebrisRuntime`, the same class in the editor and in shipped games.
- **CPU `VoxelDynamicsWorld`** (sequential impulse) holds furniture, fragments, trees and item
  props (whole compound bodies), plus the static-terrain occupancy grids that characters ground
  against. It does NOT simulate break debris any more.

```
  break (B key / Python / blast / spell / derez)
        |
        v
  DamageSystem::spawnBreakDebris --> GpuParticlePhysics (AVBD, owned by DebrisRuntime)
                                          |  movers in: characters, CPU bodies, doors, held items
                                          |  (kinematic AVBD bodies, one-way into the GPU)
                                          |  readback out: sleep/wake/impact events, push on characters
                                          v
                                   particle_expand.comp --> dynamic_voxel.vert
```

## Routing

There is none. `VoxelManipulationSystem::breakCube` hands the piece to
`DamageSystem::spawnBreakDebris` through a GPU-solver provider. If the GPU solver is missing, it
fails LOUD (see "All debris is GPU debris" below): voxels still break, but no debris spawns.

## VoxelDynamicsWorld (CPU Physics)

A purpose-built sequential-impulse physics engine for furniture, fragments and compound rigid
bodies. It does not handle break debris, which has been GPU-only since Plan 1d.

### Architecture

- **World**: `VoxelDynamicsWorld` in `engine/src/physics/VoxelDynamicsWorld.cpp`
- **Bodies**: `VoxelRigidBody` — compound OBB rigid body with sleeping, damping, restitution, friction
- **Terrain**: `VoxelOccupancyGrid` registered per-chunk; queried via AABB each substep
- **Contact solver**: soft-step sequential impulse (Box3D-style, warm-started, island sleep; see
  [PhysicsRestOverhaul.md](PhysicsRestOverhaul.md) Phase 1)
- **Threading**: Integrate, contact generation (terrain phase), and contact prepare run in parallel via `std::async` on `hardware_concurrency` threads; PGS solve is sequential
- **Debris coupling**:
  - CPU bodies push GPU debris one-way (Plan 3c, `DebrisMoverFeed`).
  - A blast or `POST /api/physics/impulse` pushes both worlds with the same impulse law (Plan 4).
- **PhysicsWorld**: Thin wrapper around `VoxelDynamicsWorld`; provides `stepSimulation`, `setGravity`, `getVoxelWorld()`

### Contact Generation Pipeline

Each substep:
1. **Build awake list + cache AABBs** — one AABB computed per body, reused in both terrain and body-body phases
2. **Body vs terrain** (parallel) — each body's AABB queries registered `VoxelOccupancyGrid`s; only nearby terrain voxels are tested
3. **Body vs kinematic obstacles** (sequential) — character segment boxes; wakes sleeping bodies on overlap
4. **Body vs body** (spatial hash broadphase) — bodies bucketed into 2-unit 3D cells; only pairs sharing a cell are narrowphase-tested, reducing average complexity from O(N²) to O(N) for sparse scenes

### Properties

| Property | Value |
|----------|-------|
| Max bodies | Unlimited (soft limit ~500 active before perf degrades) |
| Lifetime | `FLT_MAX` for furniture |
| Collision | OBB–OBB and OBB–AABB (terrain) via SAT |
| Substeps | 3 per frame (configurable) |
| Gravity | -9.81 m/s² |
| Sleep threshold | 0.02 m/s linear, 0.05 rad/s angular |
| Sleep delay | 1.2 seconds below threshold |
| Broadphase | Spatial hash, 2-unit cells |
| Thread count | `hardware_concurrency` (configurable via `setThreadCount`) |

### Body Creation API

```cpp
// From VoxelDynamicsWorld:
VoxelRigidBody* createVoxelBody(const glm::vec3& worldPos,
                                 const glm::vec3& halfExtents,
                                 float mass,
                                 float restitution = 0.2f,
                                 float friction    = 0.6f);

// Apply impulse (e.g. explosion force):
body->applyCentralImpulse(glm::vec3(0, 5.0f, 0));

// Apply at off-center point (torque + linear):
body->applyImpulse(impulse, worldPoint);
```

### Furniture Integration

`DynamicFurnitureManager` activates furniture as `VoxelRigidBody` instances when broken free:
- Furniture bodies have `lifetime = FLT_MAX` — they never expire and remain as sleeping obstacles indefinitely
- `AnimatedVoxelCharacter` uses `overlapsAnyBody()` for collision, which correctly hits sleeping furniture bodies
- Bodies are registered with the character's kinematic obstacles each frame for push impulses

### Performance Characteristics (Debug Build)

Measured with `tools/perf_stress_test.py --mode voxel` (bare, unrendered bodies) — all bodies spawned at a single point (worst case: all bodies piled and awake).

| Count | FPS avg | CPU ms | Notes |
|-------|---------|--------|-------|
| 0 | 166 | 6.5 | Baseline |
| 50 | 114 | 23 | Healthy |
| 100 | 127 | 20 | Healthy |
| 200 | 86 | 219 | Manageable |
| 500 | 0.7 | 1353 | Unplayable |

**Key caveat**: The stress test is a worst case — all bodies spawn at the same point and remain awake in a dense pile. In real gameplay most bodies are spread out or sleeping, so practical limits are significantly higher.

**Spatial hash note**: The broadphase is efficient for sparse distributions (furniture around a room). When all bodies are piled in the same cells it degrades to O(N²) — the dense pile case is an inherent constraint of the spatial hash, not a bug.

## GPU Compute Path

> **Current state (2026-10-03):** the live solver is **AVBD** (Augmented Vertex Block Descent,
> SIGGRAPH 2025), not XPBD. Debris settles naturally: it falls, collides a few times and goes
> still. The design, the defects that made it bubble for six months, and the measurement that
> proves the fix are in **[DebrisSettlingPlan.md](DebrisSettlingPlan.md) §R**.
>
> **The gate for ANY change to the solver shaders or `GpuParticlePhysics`** is
> `python tools/debris_settle_bench.py` run against the **DebrisLab** project (see "Testing"
> below). It must exit 0 for the four passing scenarios, and must not regress the other two.
> Never judge settling from a screenshot of separated cubes dropped onto flat ground: that is
> the one case that always looked fine.

**All debris is GPU debris, so a missing solver is LOUD (2026-10-04).** If `GpuParticlePhysics`
fails to initialize — or is forced off with `PHYXEL_DISABLE_GPU_DEBRIS=1` / `--disable-gpu-debris`
(the flag exists because `launch_engine` passes arguments, not environment) — voxels still break,
but: exactly one `ERROR` "GPU debris DISABLED (<reason>)" is logged; `/api/debug/gpu_physics`
answers `enabled:false`, `disabled_reason`, `refused_spawns`; `/api/damage/apply` reports
`debris:0` + `debris_refused:N`; derez removes the character without debris. Check it with
`python tools/gpu_debris_disabled_check.py --expect disabled|enabled` (live, DebrisLab).

### Architecture

- **System**: `GpuParticlePhysics` in `engine/src/core/GpuParticlePhysics.cpp`
- **Storage**: GPU SSBO (`ParticleBuffer`, 96 bytes × 10,000 slots). Each particle is one OBB
  rigid body (`SolverBody`, 208 B).
- **Physics**: fixed 60 Hz ticks (up to 4 catch-up ticks per frame). Every tick runs the AVBD
  pipeline below (`recordComputeCommandsNew`).
- **Rendering**: the compute expand pass writes face instances directly; they are drawn via
  `vkCmdDrawIndirect`.
- **Collision world (since 2026-10-05, DebrisInteractionPlan 1c):** the SAME micro-resolution
  occupancy CPU physics and lighting use — `VoxelLightOccupancy`'s packed pool (`occupancy.glsl`),
  sourced from each chunk's `VoxelOccupancyGrid` and recentred on the viewer (1024×512×1024 box).
  Debris rests on 1/3 slabs and 2-micro fences at their true height (`tools/debris_subvoxel_rest_check.py`).
  A body whose contact samples need occupancy the pool does not have (outside the box, chunk not
  resident) is **held** for that tick and counted (`frozen_unknown`; the settle analyzer's
  `held_unknown_occupancy` check). The old 512×256×512 cube bitfield and its writers were deleted
  in 1c step 4, so writers must keep `VoxelOccupancyGrid` right (1c step 5 audits them).

The legacy XPBD pipeline (`particle_integrate/collide/sort_scan.comp`, height-map collision,
gravity −18) and the orphaned `solver_jacobi/apply/graph_color.comp` shaders were **deleted
2026-10-04** (`docs/DebrisInteractionPlan.md` D4). AVBD is the only pipeline.

### Per-tick pipeline (live)

| # | Shader | Purpose |
|---|--------|---------|
| 1 | `solver_sync_in.comp` | GpuParticle → SolverBody. Velocity = (pos − prevPos)/dt. Consumes wake bits |
| 2 | `solver_integrate.comp` | Gravity and damping, inertial prediction. `startAtRest`: slow bodies start from x⁻. Water buoyancy, drag and current (flag 64). The old character shove (D7) is deleted: movers are kinematic AVBD bodies |
| 3 | `particle_grid_*`, `particle_scan_*`, `particle_sort_*` | Spatial hash broadphase (parallel prefix sum) |
| 4a | `solver_narrowphase.comp` | Box-box SAT with clipped face manifolds (≤4 points) or an edge contact |
| 4b | `solver_voxel.comp` | Box vs static voxels: 26 surface samples, `voxel_contact.glsl` (≤6 contacts) |
| 5 | `solver_csr_*`, `solver_prefix_sum.comp` | Body→constraint adjacency |
| 6 | `solver_body_color.comp` ×32 | Jones-Plassmann graph colouring (32 colours) |
| 7 | (`solver_dual.comp` → `solver_primal.comp` ×33) ×8 | AVBD: per-constraint dual update, then per-colour 6×6 LDL body solves. The 33rd sweep solves UNCOLORED bodies Jacobi-style |
| 7b | `solver_hardcontact.comp` | Safety projection out of static voxels when overlap exceeds 1 cm. Velocity-neutral |
| 8 | `solver_sync_out.comp` | SolverBody → GpuParticle. Velocity from displacement. Sleep counter / freeze. Writes SLEEP/WAKE/IMPACT events |
| 9 | `solver_warmstart_save.comp` | Persist λ, κ, stick flag and friction anchors per contact feature. Sums each mover's push (6a) |
| — | `particle_expand.comp` | Six face instances per active particle |

### Contact model (what makes debris settle; do not regress)

- **Static contacts sample the body, not the voxel.** Each body has 26 surface points (8
  corners, 12 edge midpoints, 6 face centres). A point inside solid escapes toward the nearest
  EMPTY cell in its 26-neighbourhood. That gives exact distance-to-free-space within one cell,
  so internal faces between voxels never produce a normal, and concave pit edges escape
  diagonally. The contact point is the sample itself, so lever arms are true.
- **Penetration is stored at tick start**: `C_init = pen_pred + J·Δq_pred`. Contacts are
  detected at the predicted iterate, but the solver measures motion from the tick start.
  Storing the predicted depth counted every tick's motion twice, so every impact bounced.
- **α error correction applies to penetration only** (`stabilizedC0`). A speculative gap counts
  in full, so bodies rest **flush** and never hover at the margin.
- **Static friction is anchored** (AVBD §3.3). Cold friction rows start stiff, and a sticking
  contact keeps its anchor across ticks: a world point for static contacts, local arms for
  body pairs.
- **Warm-start keys are stable**: (sample, escape direction) for static contacts and the
  clipped-manifold feature for body pairs. Contacts persist up to `COLLISION_MARGIN` (2 cm)
  apart.

### Properties

| Property | Value |
|----------|-------|
| Max particles | 10,000 (`MAX_PARTICLES`) |
| Max constraints | 60,000 (`MAX_CONSTRAINTS`). Overflow is counted by the settle probe |
| Solver | AVBD, 8 iterations, α = 0.99, β = 1e5, γ = 0.999, 32 colours plus a Jacobi fallback |
| Gravity | −9.81 m/s² |
| Fixed timestep | 16.667 ms (60 Hz), at most 4 ticks per frame |
| Default lifetime | 30 s (debris from `DamageSystem`: 25 s) |
| Friction | Coulomb μ = the material's `friction` (Stone 0.8, Ice 0.1) |
| Static collision | the shared micro occupancy pool (1c): 1024×512×1024 box following the viewer |

**Known open solver items** (from the retired AVBD audit vs. Giles, Diaz, Yuksel, *Augmented
Vertex Block Descent*, SIGGRAPH 2025, DOI 10.1145/3731195): P3 restitution is plumbed but no solve
pass reads it; P6 `GAMMA` doubles as damping (`solver_integrate.comp`); R2 the iteration count
(`SOLVE_ITERATIONS` 8) was never tuned.

**Not built: GPU compound bodies** (user-approved plan, 2026-08-07, in git history as
`docs/GpuCompoundBodies.md`). Blocker: `SolverBody` is ONE OBB with a scalar `invInertia`;
compounds need an inertia tensor, pair-expansion narrowphase, persistent manifolds and pose readback.

### Runtime switches (A/B only; defaults are the shipped behaviour)

`POST /api/debug/gpu_physics {"flags": N, "cold_scale": s}`. Bits: 1 = cold normal rows at
m/dt², 2 = startAtRest, 4 = velocity-neutral hard contact, 8 = post-stabilisation (rejected:
measured worse), 16 = static friction, 32 = kinematic contacts (movers are AVBD bodies; off =
movers touch nothing), 64 = water. **Default 119** (`SOLVER_FLAGS_DEFAULT` in
`shaders/solver_shared.h`, static_asserted). `cold_scale` multiplies cold-contact
stiffness (default 1; 10× fixes stacks but makes impacts violent).

### Particle Sleep and Wake

A body freezes in `solver_sync_out.comp` (flag `PARTICLE_SLEEPING`; its encoded velocity
becomes exactly 0, and from the next tick invMass = 0) under either tier:
- **strict tier**: under 0.05 m/s now, after 30 ticks under 0.15 m/s;
- **lax tier**: 180 ticks under 0.15 m/s.

The lax tier is a safety net. In the bench it fires on 0 % of bodies in packed piles and
craters, and on 2–12 % in collapsing towers or blasts.

Sleepers stay in the broadphase as static supports. An awake body hitting one faster than
0.5 m/s, or a moving character overlapping one, sets a wake bit.

### Interaction (DebrisInteractionPlan, all phases done 2026-10-07)

The plan doc holds the design, the measurements and the open items. In short:

| What | How | API / switch |
|------|-----|--------------|
| Movers push debris | Character limb boxes (3a), CPU bodies (3c), and doors, animated parts and held items (3b) are kinematic AVBD bodies: at most 512 boxes, nearest the camera first | `gpu_kinematic_box` (scripted box), the `gpu_physics` kinematic counters; flag 32 |
| Impulses | One law for both worlds; blasts and spells push existing debris | `POST /api/physics/impulse` |
| Debris pushes characters | Per-character summed contact impulse, read back 2 frames late, applied by `AnimatedVoxelCharacter::applyDebrisPush` (75 kg, ≤ 3 m/s per call, ≤ 4 m/s, decays) | `debris_events {"push_back": bool}` |
| Events | SLEEP / WAKE / IMPACT, ≤ 1024 per frame (overflow counted), driving the `debris.impact` / `debris.settle` sounds | `POST /api/debug/debris_events` |
| Gather rubble | Settled pieces near a point go to the inventory by VOLUME (a ⅓ piece = 1/27 cube). G key | `POST /api/debug/debris_gather` |
| Water | Per-material `physics.buoyancy` (materials.json, default 0.38); drag; current from `WaterManager::columnWater` | `debris_events` → `water{}`; flag 64 |
| Shipped games | `DebrisRuntime` and `registerDebrisCommands` are shared by the editor and `GameShell`; ON by default | off: game.json `debris.enabled=false` or `--disable-gpu-debris` |

### Testing (the settling gate)

- **DebrisLab project** (`C:\Users\jack\Documents\PhyxelProjects\DebrisLab`). It is a folder
  holding only `game.json` (water off, **no `world` block**: a world block regenerates terrain
  on every launch and would refill the pits), `engine.json` and `.phyxel/config.json`
  (`apiPort` 8090).
  - Create it, launch the editor with `--project <dir>`, then run
    `python tools/debris_settle_bench.py --build-lab`.
  - That authors an 8-thick Stone slab (top face y = 16) with one scenario per 32-voxel chunk
    and pre-carved pits, then saves it to the project DB.
- **Bench**: `python tools/debris_settle_bench.py [--only …] [--frames] [--flags N] [--cold-scale s]`.
  - It freezes the solver and steps exact ticks (`/api/debug/gpu_physics`), so results do not
    depend on frame rate.
  - It runs the settle probe (`/api/debug/settle_probe`), a per-tick analysis: injected
    energy, rebounds, clean vs. forced sleeps, creep before sleep, hard-contact pushes,
    colour-skipped bodies, tunnelling.
  - It writes `docs/evidence/debris_settle/<tag>/` and prints a pass/fail table.
- **Watch it**:
  - `python tools/debris_settle_demo.py [scenario…]` plays the scenarios in real time.
  - `python tools/debris_drop_here.py [N]` drops N cubes in front of the camera.
- **Compare**: `python tools/debris_settle_contact_sheet.py <before-tag> <after-tag>`.

## Performance Characteristics (Debug Build)

### GPU Compute

| Count | FPS avg | FPS min | CPU ms | Notes |
|-------|---------|---------|--------|-------|
| 0 | 193 | 72 | 5.8 | Baseline |
| 100 | 107 | 98 | 9.4 | Fixed pipeline overhead (~4ms) |
| 500 | 75 | 69 | 13.3 | Stable range |
| 1000 | 76 | 71 | 13.3 | Negligible per-particle cost |
| 2000 | 71 | 46 | 14.6 | Still above 60 avg |
| 5000 | 77 | 73 | 13.0 | **5000 particles, still >60 FPS** |

**Key insight**: The GPU compute cost is almost entirely **fixed overhead** from dispatching the 5-pass pipeline. Going from 100→5000 particles adds <4ms CPU time.

## Debug/Stress Testing API

### Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/debug/engine_timing` | GET | FPS, CPU/GPU frame time, draw calls, culling stats, detailed subsystem timings |
| `/api/debug/dynamic_stats` | GET | `gpu_active` / `gpu_cap` (the CPU debris counters were deleted with the CPU debris path, Plan 1d) |
| `/api/debug/spawn_voxel_body` / `clear_voxel_bodies` | POST | Bare, unrendered `VoxelDynamicsWorld` bodies — the only headless CPU-solver benchmark harness (`perf_stress_test.py --mode voxel`). `clear_voxel_bodies` removes EVERY body in that world, furniture included |
| `/api/debug/particle_timing` | GET | GPU physics timing ring buffer (300 frames) |
| `/api/debug/spawn_gpu_particle` | POST | Spawn GPU particles (count, scale, material, lifetime, velocity) |
| `/api/debug/clear_dynamics` | POST | Remove all GPU debris instantly; echoes `gpu_cleared` |
| `/api/debug/spawn_gpu_lattice` | POST | Deterministic grid of GPU debris (nx/ny/nz, scale, gap — 0 = touching, spin, jitter, seed) |
| `/api/debug/gpu_physics` | POST | Freeze / single-step the GPU solver (`frozen`, `step`), solver fix switches (`flags`, `cold_scale`) |
| `/api/debug/settle_probe` | POST | Per-tick settling analysis (`op` start/stop/status, `floor_y`, `series_last`, `bodies`, `csv`) → SETTLES/FAILS verdict |
| `/api/debug/gpu_kinematic_box` | POST | Scripted kinematic mover box (`id`, `center`, `half`, `rotation`, `velocity`, `angular_velocity`, `ttl`, `remove`) |
| `/api/debug/occupancy_diff` | POST | Store = grid = pool check over a ≤ 64³ box |
| `/api/debug/debris_events` | POST | Event counters plus `recent`, settled count, `water{}`, `push_back{}`; `{"push_back": bool}` toggles push-back |
| `/api/debug/debris_gather` | POST | Gather settled rubble (`x,y,z`, `radius` ≤ 16, `max` ≤ 256, `inventory`) |
| `/api/physics/impulse` | POST | Push existing debris and CPU bodies (`x,y,z`, `radius`, `impulse` N·s, `up_bias`, optional cone, `worlds`) |

### Spawn Parameters

```json
{
    "x": 32.0, "y": 20.0, "z": 32.0,
    "material": "Stone",
    "scale": 1.0,
    "count": 100,
    "lifetime": 120.0,
    "velocity": {"x": 0, "y": 0, "z": 0}
}
```

- `scale`: 1.0 (full cube), 0.333 (subcube), 0.111 (microcube)
- `lifetime`: Seconds until auto-removal (default 30s)
- `count`: Max 2000 per call for GPU

### engine_timing Response

```json
{
    "fps": 234.5,
    "cpuFrameTime": 4.27,
    "gpuFrameTime": 4.27,
    "drawCalls": 4,
    "vertexCount": 79312,
    "visibleInstances": 294912,
    "culledInstances": 0,
    "physicsActive": 0,
    "gpuActive": 0,
    "gpuCap": 10000,
    "detailed": {
        "totalFrameTime": 4.45,
        "physicsTime": 0.0,
        "instanceUpdateTime": 0.0,
        "commandRecordTime": 1.0,
        "gpuSubmitTime": 0.1,
        "presentTime": 0.37
    }
}
```

## Automated Stress Tester

`tools/perf_stress_test.py` — Automated performance profiling script.

### Usage

```bash
python tools/perf_stress_test.py --mode gpu --quick --settle 2
python tools/perf_stress_test.py --mode voxel --quick --settle 2
python tools/perf_stress_test.py --mode all
```

### Modes

| Mode | Description |
|------|-------------|
| `gpu` | Ramp GPU particles: 100→10,000 (quick: 100→5,000) |
| `voxel` | Ramp bare, unrendered VoxelDynamicsWorld bodies (`spawn_voxel_body`): 50→5,000 |
| `scale` | Compare full/subcube/microcube performance |
| `sustained` | Hold 10,000 GPU particles for 30 seconds |
| `all` | Run all modes sequentially |

## Key Source Files

| File | Purpose |
|------|---------|
| `engine/include/physics/PhysicsWorld.h` | Thin wrapper exposing `VoxelDynamicsWorld*` via `getVoxelWorld()` |
| `engine/include/physics/VoxelDynamicsWorld.h` | World API (create/remove bodies, grids, queries) |
| `engine/src/physics/VoxelDynamicsWorld.cpp` | Simulation loop, broadphase, parallel phases |
| `engine/include/physics/VoxelRigidBody.h` | Rigid body state, sleeping, AABB, impulse API |
| `engine/include/physics/VoxelContactSolver.h` | SAT contact generation, PGS solver |
| `engine/src/physics/VoxelContactSolver.cpp` | OBB–OBB, OBB–AABB, face clipping, impulse solve |
| `engine/include/core/GpuParticlePhysics.h` | GPU particle system header (spawn API, constants) |
| `engine/src/core/GpuParticlePhysics.cpp` | GPU compute pipeline setup, dispatch, spawn queue |
| `engine/{include,src}/core/DebrisRuntime.*` | Owns the GPU solver in the editor AND in shipped games: mover feeds, events/audio, water tiles, push-back |
| `engine/{include,src}/core/DebrisApiCommands.*` | The shared debris API handlers (`registerDebrisCommands`) |
| `engine/include/core/DebrisMoverFeed.h` | CPU bodies / doors / held items → kinematic mover boxes |
| `engine/src/scene/VoxelManipulationSystem.cpp` | Break → `DamageSystem::spawnBreakDebris` (GPU only) |
| `shaders/solver_shared.h` | Constants and flag bits shared by C++ and GLSL (one file, no mirror) |
| `editor/src/Application.cpp` | Debug spawn handlers, timing API handlers |
| `engine/src/core/EngineAPIServer.cpp` | HTTP route registration for debug endpoints |
| `shaders/solver_*.comp`, `shaders/voxel_contact.glsl` | The AVBD solver passes and the shared voxel-contact model |
| `shaders/particle_expand.comp` | Face instance generation compute shader |
| `shaders/particle_types.glsl` | Shared particle struct definition |
| `tools/perf_stress_test.py` | Automated performance stress tester |
| `tools/debris_settle_bench.py` | The settling gate (DebrisLab) |

---

## Future Performance Work

### 1. Dense-pile sleep cascading
When bodies settle into a pile, they keep nudging each other and cannot sleep despite being nearly stationary. A cascading sleep policy — where a body surrounded only by sleeping/static neighbors is put to sleep immediately regardless of the timer — would collapse settled piles in <1 second and dramatically reduce the awake body count in practice.

### 2. Substep reduction for debris
The default of 3 substeps is conservative. Single-voxel debris bodies that don't need tight constraint stability could use 1 substep, cutting the physics budget by ~3×. Could be a per-body flag or a global mode.

### 3. Island detection
Group bodies into connected-component islands (connected by active contacts). Sleep an entire island when all members are below threshold. Eliminates per-body timer jitter in settled piles and enables skipping entire sleeping islands from broadphase.

### 4. Parallel spatial hash construction
The spatial hash is currently built and queried sequentially. For >500 awake bodies, construction and pair-testing could be parallelized using per-thread hash maps merged before narrowphase.

### 5. Release build benchmarking
All numbers above are Debug builds. Release mode eliminates bounds checks, enables SIMD auto-vectorization, and typically yields 3–5× physics throughput improvement.

---

## See Also

- [VoxelRenderPipelines.md](VoxelRenderPipelines.md) — Rendering architecture for all three voxel pipelines (static, kinematic, GPU particle)
- [VoxelSystem.md](VoxelSystem.md) — Conceptual model: voxel sizes, static/kinematic/dynamic states
- [SubsystemArchitecture.md](SubsystemArchitecture.md) — Engine subsystem overview
- [CoordinateSystem.md](CoordinateSystem.md) — World coordinate conventions
