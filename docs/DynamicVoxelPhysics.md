# Dynamic Voxel Physics System

## Overview

When voxels are broken (left-click), they become physics-driven **dynamic voxels** that fall, bounce, and collide with the world and each other. The engine uses a **two-tier architecture**:

- **VoxelDynamicsWorld** (CPU) — Custom sequential-impulse rigid body engine. Handles all broken voxels, furniture, and compound physics objects with full OBB collision. This is the **sole CPU physics backend** — Bullet Physics has been removed from active builds.
- **GPU Compute** (Vulkan) — Massively parallel XPBD particle physics via `GpuParticlePhysics`. Lower per-particle cost, scales to 5000+ particles with minimal FPS impact.

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
│VoxelDynamics │  │ GPU XPBD │
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

### Architecture

- **System**: `GpuParticlePhysics` in `engine/src/core/GpuParticlePhysics.cpp`
- **Storage**: GPU SSBO (`ParticleBuffer`, 96 bytes × 10,000 slots)
- **Physics**: 5-pass XPBD compute pipeline per frame
- **Rendering**: Compute expand pass writes face instances directly to GPU buffer; drawn via `vkCmdDrawIndirect`

### Compute Pipeline (per frame)

| Pass | Shader | Purpose |
|------|--------|---------|
| 1 | `particle_grid_clear.comp` | Zero the 3D occupancy grid |
| 2 | `particle_grid_build.comp` | Build occupancy bitfield from chunk voxel data |
| 3 | `particle_integrate.comp` | XPBD integration: gravity, velocity, angular velocity, sleep detection |
| 4 | `particle_collide.comp` | Voxel grid collision, character AABB collision, inter-particle collision |
| 5 | `particle_expand.comp` | Generate 6 face instances per active particle into face buffer |

### Properties

| Property | Value |
|----------|-------|
| Max particles | 10,000 (`MAX_PARTICLES`) |
| Max face slots | 60,000 (`MAX_FACE_SLOTS`, 6 per particle) |
| Default lifetime | 30 seconds (configurable via spawn API) |
| Gravity | -18.0 m/s² (heavier feel for voxel debris) |
| Fixed timestep | 16.667ms (60 Hz physics) |
| Sleep threshold | 5e-4 velocity² |
| Collision | Axis-aligned grid + inter-particle sphere checks |
| Occupancy grid | 512×256×512 bits (8 MB) |

### Scale Support

GPU particles support the same three scale tiers. Scale is stored in `SpawnParams.scale` (vec3) and written to the particle buffer. The collide shader uses scale-aware half-extents for AABB collision, and the expand shader generates correctly-sized face geometry.

### Spawn API

Particles are queued on the CPU via `GpuParticlePhysics::queueSpawn(SpawnParams)`:

```cpp
struct SpawnParams {
    glm::vec3 position;
    glm::vec3 velocity;
    glm::vec3 angularVelocity;
    glm::vec3 scale;        // (1.0, 1.0, 1.0) for full cubes
    std::string materialName;
    float lifetime;          // seconds until auto-removal
};
```

Queued spawns are uploaded to the GPU at the start of the next compute dispatch.

### Particle Sleep and Wake

Particles with velocity² below the sleep threshold for several frames enter sleep state. Sleeping particles skip physics (integrate + collide) for performance. They are woken if the player character's AABB overlaps them (e.g., player walks through a pile of debris).

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
| `/api/debug/dynamic_stats` | GET | VoxelDynamicsWorld active/total counts, GPU active/cap counts |
| `/api/debug/particle_timing` | GET | GPU physics timing ring buffer (300 frames) |
| `/api/debug/spawn_bullet_cube` | POST | Spawn VoxelDynamicsWorld dynamic cubes (count, scale, material, lifetime, velocity) |
| `/api/debug/spawn_gpu_particle` | POST | Spawn GPU particles (count, scale, material, lifetime, velocity) |
| `/api/debug/clear_dynamics` | POST | Remove all dynamic objects and GPU particles instantly |

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
| `voxel` | Ramp VoxelDynamicsWorld bodies: 25→500 |
| `mixed` | Fill VoxelDynamicsWorld to 50% of soft cap, then ramp GPU |
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
