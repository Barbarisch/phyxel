# Dynamic Voxel Physics System

## Overview

When voxels are broken (left-click), they become physics-driven **dynamic voxels** that fall, bounce, and collide with the world and each other. The engine uses a **two-tier architecture**:

- **VoxelDynamicsWorld** (CPU) — Custom sequential-impulse rigid body engine. Handles all broken voxels, furniture, and compound physics objects with full OBB collision. This is the **sole CPU physics backend** — Bullet Physics has been removed from active builds.
- **GPU Compute** (Vulkan) — Massively parallel AVBD rigid-body debris physics via `GpuParticlePhysics` (the XPBD pipeline it replaced was deleted 2026-10-04). Lower per-particle cost, scales to 5000+ particles with minimal FPS impact.

Both systems render through the same dynamic voxel pipeline (see [VoxelRenderPipelines.md](VoxelRenderPipelines.md)).

```
          Player breaks voxel
                  │
                  ▼
    ┌─────────────────────────┐
    │ VoxelManipulationSystem │
    │    breakCube()          │
    └───────────┬─────────────┘
                │
        ┌───────┴──────────┐
        │  Routing Decision│
        │  (FPS-based)     │
        ├──────────┬───────┤
        ▼          ▼
┌──────────────┐  ┌──────────┐
│VoxelDynamics │  │ GPU AVBD │
│   World      │  │(Compute) │
│   (CPU)      │  │          │
└──────┬───────┘  └────┬─────┘
       │               │
       ▼               ▼
 DynamicObject    ParticleBuffer
  Manager          (SSBO)
       │               │
       ▼               ▼
  CPU face buf    GPU face buf
       │               │
       └──────┬────────┘
              ▼
     Dynamic Render Pipeline
      (dynamic_voxel.vert)
```

## Routing

When a voxel breaks, `VoxelManipulationSystem` decides which backend to use via **FPS-based fallback** — VoxelDynamicsWorld is always preferred for its realistic rigid body simulation, and GPU particles are only used when performance demands it:

1. **FPS check**: If the smoothed FPS is at or above the threshold (`GPU_FALLBACK_FPS_THRESHOLD`, default 30 FPS), use VoxelDynamicsWorld.
2. **Per-frame budget**: At most `MAX_VOXEL_BREAKS_PER_FRAME` new CPU objects per frame to avoid spikes.
3. **FPS below threshold**: Route to GPU compute to avoid further frame rate degradation.

The smoothed FPS uses an exponential moving average (~20-frame window) to avoid jitter from single-frame spikes.

## VoxelDynamicsWorld (CPU Physics)

A purpose-built sequential-impulse physics engine for all CPU-side dynamic voxels, furniture, and compound rigid bodies.

### Architecture

- **World**: `VoxelDynamicsWorld` in `engine/src/physics/VoxelDynamicsWorld.cpp`
- **Bodies**: `VoxelRigidBody` — compound OBB rigid body with sleeping, damping, restitution, friction
- **Terrain**: `VoxelOccupancyGrid` registered per-chunk; queried via AABB each substep
- **Contact solver**: Sequential impulse (PGS), 10 iterations per substep
- **Threading**: Integrate, contact generation (terrain phase), and contact prepare run in parallel via `std::async` on `hardware_concurrency` threads; PGS solve is sequential
- **Manager**: `DynamicObjectManager` — wraps VoxelDynamicsWorld, handles lifecycle (spawn, expire, position sync, face generation)
- **PhysicsWorld**: Thin wrapper around `VoxelDynamicsWorld`; provides `stepSimulation`, `setGravity`, `getVoxelWorld()`

### Contact Generation Pipeline

Each substep:
1. **Build awake list + cache AABBs** — one AABB computed per body, reused in both terrain and body-body phases
2. **Body vs terrain** (parallel) — each body's AABB queries registered `VoxelOccupancyGrid`s; only nearby terrain voxels are tested
3. **Body vs kinematic obstacles** (sequential) — character segment boxes; wakes sleeping bodies on overlap
4. **Body vs body** (spatial hash broadphase) — bodies bucketed into 2-unit 3D cells; only pairs sharing a cell are narrowphase-tested, reducing average complexity from O(N²) to O(N) for sparse scenes

### Lifecycle

1. **Spawn**: `addGlobalDynamicCube/Subcube/Microcube()` → `VoxelDynamicsWorld::createVoxelBody()`
2. **Update**: `updateGlobalDynamicCubes(dt)` — decrements lifetime, removes expired cubes, cleans up physics bodies
3. **Position sync**: reads `VoxelRigidBody::position` and `orientation`, writes to cube's physics position
4. **Face generation**: `FaceUpdateCoordinator` generates `DynamicSubcubeInstanceData` per visible face
5. **Render**: CPU-side face buffer uploaded to Vulkan, drawn via `vkCmdDrawIndexed` (6 indices per face)

### Properties

| Property | Value |
|----------|-------|
| Max bodies | Unlimited (soft limit ~500 active before perf degrades) |
| Default lifetime | 30s for debris, `FLT_MAX` for furniture |
| Collision | OBB–OBB and OBB–AABB (terrain) via SAT |
| Substeps | 3 per frame (configurable) |
| Gravity | -9.81 m/s² |
| Sleep threshold | 0.02 m/s linear, 0.05 rad/s angular |
| Sleep delay | 1.2 seconds below threshold |
| Broadphase | Spatial hash, 2-unit cells |
| Thread count | `hardware_concurrency` (configurable via `setThreadCount`) |

### Scale Support

| Scale | Size | Type |
|-------|------|------|
| 1.0 | Full cube | `Cube` via `addGlobalDynamicCube()` |
| 0.333 | Subcube (1/3) | `Subcube` via `addGlobalDynamicSubcube()` |
| 0.111 | Microcube (1/9) | `Microcube` via `addGlobalDynamicMicrocube()` |

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

Measured with `tools/perf_stress_test.py --mode voxel` — all bodies spawned at a single point (worst case: all bodies piled and awake).

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

### Architecture

- **System**: `GpuParticlePhysics` in `engine/src/core/GpuParticlePhysics.cpp`
- **Storage**: GPU SSBO (`ParticleBuffer`, 96 bytes × 10,000 slots). Each particle is one OBB
  rigid body (`SolverBody`, 208 B).
- **Physics**: fixed 60 Hz ticks (up to 4 catch-up ticks per frame). Every tick runs the AVBD
  pipeline below (`recordComputeCommandsNew`).
- **Rendering**: the compute expand pass writes face instances directly; they are drawn via
  `vkCmdDrawIndirect`.
- **Collision world**: a 512×256×512 occupancy bitfield of static voxels, updated by
  `ChunkManager` on every place/break/stream.

The legacy XPBD pipeline (`particle_integrate/collide/sort_scan.comp`, height-map collision,
gravity −18) and the orphaned `solver_jacobi/apply/graph_color.comp` shaders were **deleted
2026-10-04** (`docs/DebrisInteractionPlan.md` D4). AVBD is the only pipeline.

### Per-tick pipeline (live)

| # | Shader | Purpose |
|---|--------|---------|
| 1 | `solver_sync_in.comp` | GpuParticle → SolverBody. Velocity = (pos − prevPos)/dt. Consumes wake bits |
| 2 | `solver_integrate.comp` | Gravity and damping, inertial prediction. `startAtRest`: slow bodies start from x⁻. Character shove |
| 3 | `particle_grid_*`, `particle_scan_*`, `particle_sort_*` | Spatial hash broadphase (parallel prefix sum) |
| 4a | `solver_narrowphase.comp` | Box-box SAT with clipped face manifolds (≤4 points) or an edge contact |
| 4b | `solver_voxel.comp` | Box vs static voxels: 26 surface samples, `voxel_contact.glsl` (≤6 contacts) |
| 5 | `solver_csr_*`, `solver_prefix_sum.comp` | Body→constraint adjacency |
| 6 | `solver_body_color.comp` ×32 | Jones-Plassmann graph colouring (32 colours) |
| 7 | (`solver_dual.comp` → `solver_primal.comp` ×33) ×8 | AVBD: per-constraint dual update, then per-colour 6×6 LDL body solves. The 33rd sweep solves UNCOLORED bodies Jacobi-style |
| 7b | `solver_hardcontact.comp` | Safety projection out of static voxels when overlap exceeds 1 cm. Velocity-neutral |
| 8 | `solver_sync_out.comp` | SolverBody → GpuParticle. Velocity from displacement. Sleep counter / freeze |
| 9 | `solver_warmstart_save.comp` | Persist λ, κ, stick flag and friction anchors per contact feature |
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
| Occupancy grid | 512×256×512 bits (8 MB), world x/z ±256, y −64..191 |

### Runtime switches (A/B only; defaults are the shipped behaviour)

`POST /api/debug/gpu_physics {"flags": N, "cold_scale": s}`. Bits: 1 = cold normal rows at
m/dt², 2 = startAtRest, 4 = velocity-neutral hard contact, 8 = post-stabilisation (rejected:
measured worse), 16 = static friction. **Default 23.** `cold_scale` multiplies cold-contact
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
| `/api/debug/dynamic_stats` | GET | `cpu_dynamic` (CPU dynamic cubes+subcubes+microcubes, sleeping included) / `cpu_dynamic_cap`, `gpu_active` / `gpu_cap` (the Bullet-era `bullet_*` keys always read 0 and were removed 2026-10-04) |
| `/api/debug/spawn_voxel_body` / `clear_voxel_bodies` | POST | Bare, unrendered `VoxelDynamicsWorld` bodies — the only headless CPU-solver benchmark harness (`perf_stress_test.py --mode voxel`). `clear_voxel_bodies` removes EVERY body in that world, furniture included |
| `/api/debug/particle_timing` | GET | GPU physics timing ring buffer (300 frames) |
| `/api/debug/spawn_bullet_cube` | POST | Spawn VoxelDynamicsWorld dynamic cubes (count, scale, material, lifetime, velocity) |
| `/api/debug/spawn_gpu_particle` | POST | Spawn GPU particles (count, scale, material, lifetime, velocity) |
| `/api/debug/clear_dynamics` | POST | Remove all CPU dynamic objects and GPU particles instantly; echoes `cpu_cleared`, `gpu_cleared` |
| `/api/debug/spawn_gpu_lattice` | POST | Deterministic grid of GPU debris (nx/ny/nz, scale, gap — 0 = touching, spin, jitter, seed) |
| `/api/debug/gpu_physics` | POST | Freeze / single-step the GPU solver (`frozen`, `step`), solver fix switches (`flags`, `cold_scale`) |
| `/api/debug/settle_probe` | POST | Per-tick settling analysis (`op` start/stop/status, `floor_y`, `series_last`, `bodies`, `csv`) → SETTLES/FAILS verdict |

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
python tools/perf_stress_test.py --mode mixed --settle 3
python tools/perf_stress_test.py --mode all
```

### Modes

| Mode | Description |
|------|-------------|
| `gpu` | Ramp GPU particles: 100→10,000 (quick: 100→5,000) |
| `cpu` | Ramp rendered CPU dynamic cubes (`spawn_bullet_cube`): 25→300 (the `MAX_DYNAMIC_OBJECTS` cap); was `bullet` before 2026-10-04 |
| `voxel` | Ramp bare, unrendered VoxelDynamicsWorld bodies (`spawn_voxel_body`): 50→5,000 |
| `mixed` | Fill CPU dynamic cubes to 50% of their cap, then ramp GPU |
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
| `engine/include/core/DynamicObjectManager.h` | CPU dynamic object manager (lifecycle, rendering) |
| `engine/src/core/DynamicObjectManager.cpp` | Spawn, expire, position sync — reads VoxelRigidBody state |
| `engine/src/scene/VoxelManipulationSystem.cpp` | Hybrid routing (break → VoxelDynamicsWorld or GPU) |
| `editor/src/Application.cpp` | Debug spawn handlers, timing API handlers |
| `engine/src/core/EngineAPIServer.cpp` | HTTP route registration for debug endpoints |
| `shaders/particle_integrate.comp` | XPBD integration compute shader |
| `shaders/particle_collide.comp` | Collision detection compute shader |
| `shaders/particle_expand.comp` | Face instance generation compute shader |
| `shaders/particle_types.glsl` | Shared particle struct definition |
| `tools/perf_stress_test.py` | Automated performance stress tester |
| `engine/deprecated/bullet/` | Archived Bullet-dependent classes (not compiled) |

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
