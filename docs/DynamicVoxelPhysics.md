# Dynamic Voxel Physics System

## Overview

When voxels are broken (left-click), they become physics-driven **dynamic voxels** that fall, bounce, and collide with the world and each other. The engine uses a **hybrid architecture** with two physics backends:

- **Bullet Physics** (CPU) — Traditional rigid body simulation via `DynamicObjectManager`. Higher fidelity OBB collision, but CPU-bound and costly above ~75 objects.
- **GPU Compute** (Vulkan) — Massively parallel XPBD particle physics via `GpuParticlePhysics`. Lower per-particle cost, scales to 5000+ particles with minimal FPS impact.

Both systems render through the same dynamic voxel pipeline (see [DynamicSubcubeRenderPipeline.md](DynamicSubcubeRenderPipeline.md)).

```
          Player breaks voxel
                  │
                  ▼
    ┌─────────────────────────┐
    │ VoxelManipulationSystem │
    │    breakCube()          │
    └───────────┬─────────────┘
                │
        ┌───────┴───────┐
        │ Hybrid Router │ (proximity + cap check)
        ├───────┬───────┤
        ▼       ▼       
   ┌────────┐  ┌──────────┐
   │ Bullet │  │ GPU XPBD │
   │  (CPU) │  │(Compute) │
   └───┬────┘  └────┬─────┘
       │             │
       ▼             ▼
  DynamicObject   ParticleBuffer
   Manager         (SSBO)
       │             │
       ▼             ▼
  CPU face buf    GPU face buf
       │             │
       └──────┬──────┘
              ▼
     Dynamic Render Pipeline
      (dynamic_voxel.vert)
```

## Hybrid Routing

When a voxel breaks, `VoxelManipulationSystem` decides which backend to use:

1. **Distance check**: If the break position is within 10 blocks of the player, prefer Bullet (better visual fidelity at close range).
2. **Bullet cap check**: If `DynamicObjectManager::getActiveBulletCount() >= MAX_DYNAMIC_OBJECTS` (300), overflow to GPU regardless of distance.
3. **Otherwise**: Route to GPU compute.

This ensures nearby debris has precise OBB collision while distant debris uses the scalable GPU path.

## Bullet Physics Path

### Architecture

- **Manager**: `DynamicObjectManager` in `engine/src/core/DynamicObjectManager.cpp`
- **Storage**: `std::vector<std::unique_ptr<Cube>>` (and Subcube/Microcube variants)
- **Physics**: Each dynamic cube gets a `btRigidBody` via `PhysicsWorld::createBreakawayCube()`
- **Rendering**: CPU reads `btRigidBody` transforms each frame, writes `DynamicSubcubeInstanceData` to a host-visible Vulkan buffer

### Lifecycle

1. **Spawn**: `addGlobalDynamicCube(cube)` — cube has `breakApart()` called, gets a rigid body with gravity
2. **Update**: `updateGlobalDynamicCubes(dt)` — decrements lifetime, removes expired cubes, cleans up physics bodies
3. **Position sync**: `updateGlobalDynamicCubePositions()` — reads `btRigidBody` world transform, writes to cube's physics position
4. **Face generation**: `FaceUpdateCoordinator` generates `DynamicSubcubeInstanceData` per visible face, respecting `getDynamicScale()` for size
5. **Render**: CPU-side face buffer uploaded to Vulkan, drawn via `vkCmdDrawIndexed` (6 indices per face)

### Properties

| Property | Value |
|----------|-------|
| Max objects | 300 (`MAX_DYNAMIC_OBJECTS`) |
| Default lifetime | 30 seconds |
| Collision type | OBB (oriented bounding box) |
| Gravity | -9.81 m/s² |
| Sleep | Bullet's built-in deactivation |

### Scale Support

Dynamic Bullet cubes support three scale tiers:

| Scale | Size | Type |
|-------|------|------|
| 1.0 | Full cube | `Cube` via `addGlobalDynamicCube()` |
| 0.333 | Subcube (1/3) | `Subcube` via `addGlobalDynamicSubcube()` |
| 0.111 | Microcube (1/9) | `Microcube` via `addGlobalDynamicMicrocube()` |

Scale is stored per-object via `setDynamicScale()` and read by `FaceUpdateCoordinator` for correct face geometry.

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

Particles with velocity² below the sleep threshold (`5e-4`) for several frames enter sleep state. Sleeping particles skip physics (integrate + collide) for performance. They are woken if the player character's AABB overlaps them (e.g., player walks through a pile of debris).

## Performance Characteristics (Debug Build)

Measured on the CharacterTestbed project with the automated stress tester (`tools/perf_stress_test.py`).

### Bullet Physics

| Count | FPS avg | FPS min | CPU ms | Notes |
|-------|---------|---------|--------|-------|
| 0 | 227 | 188 | 4.5 | Baseline |
| 25 | 231 | 198 | 4.4 | No impact |
| 50 | 221 | 195 | 4.6 | Minimal |
| 100 | 31 | 5 | 85 | **Major drop** — physics step dominates |
| 200 | 2.4 | 2.2 | 432 | Unplayable |
| 300 | 1.8 | 1.7 | 546 | Unplayable |

**Breakpoint**: ~75-100 objects before FPS drops below 60. CPU physics step cost grows super-linearly due to Bullet's broadphase + narrowphase pair checks. This is expected for a Debug build; Release builds will be significantly faster.

### GPU Compute

| Count | FPS avg | FPS min | CPU ms | Notes |
|-------|---------|---------|--------|-------|
| 0 | 193 | 72 | 5.8 | Baseline |
| 100 | 107 | 98 | 9.4 | Fixed pipeline overhead (~4ms) |
| 500 | 75 | 69 | 13.3 | Stable range |
| 1000 | 76 | 71 | 13.3 | Negligible per-particle cost |
| 2000 | 71 | 46 | 14.6 | Still above 60 avg |
| 5000 | 77 | 73 | 13.0 | **5000 particles, still >60 FPS** |

**Key insight**: The GPU compute cost is almost entirely **fixed overhead** from dispatching the 5-pass pipeline. Going from 100→5000 particles adds <4ms CPU time. The pipeline cost is dominated by compute dispatch/synchronization, not per-particle work.

### Hybrid Advantage

The hybrid routing strategy is well justified by the data:
- Bullet handles <75 near-player objects with full OBB fidelity
- GPU handles bulk/distant debris up to 5000+ with minimal overhead
- Combined, the engine can support hundreds of simultaneous breaking events

## Debug/Stress Testing API

### Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/debug/engine_timing` | GET | FPS, CPU/GPU frame time, draw calls, culling stats, detailed subsystem timings |
| `/api/debug/dynamic_stats` | GET | Bullet active/total/cap, GPU active/cap counts |
| `/api/debug/particle_timing` | GET | GPU physics timing ring buffer (300 frames) |
| `/api/debug/spawn_bullet_cube` | POST | Spawn Bullet dynamic cubes (count, scale, material, lifetime, velocity) |
| `/api/debug/spawn_gpu_particle` | POST | Spawn GPU particles (count, scale, material, lifetime, velocity) |
| `/api/debug/clear_dynamics` | POST | Remove all Bullet objects and GPU particles instantly |

### Spawn Parameters (both endpoints)

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
- `lifetime`: Seconds until auto-removal (default: 30s for Bullet, 30s for GPU)
- `count`: Max 300 for Bullet, 2000 per call for GPU
- Objects spawn in a 3D grid pattern centered at (x, y, z)

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
    "bulletActive": 0,
    "bulletCap": 300,
    "gpuActive": 0,
    "gpuCap": 10000,
    "detailed": {
        "totalFrameTime": 4.45,
        "physicsTime": 0.0,
        "instanceUpdateTime": 0.0,
        "commandRecordTime": 1.0,
        "gpuSubmitTime": 0.1,
        "presentTime": 0.37,
        "frustumCullingTime": 0.0,
        "occlusionCullingTime": 0.0,
        "faceCullingTime": 0.0
    }
}
```

### dynamic_stats Response

```json
{
    "bullet_active": 15,
    "bullet_total": 100,
    "bullet_cap": 300,
    "gpu_active": 500,
    "gpu_cap": 10000
}
```

- `bullet_active`: Physics-awake (non-sleeping) Bullet bodies
- `bullet_total`: All Bullet bodies including sleeping ones

## Automated Stress Tester

`tools/perf_stress_test.py` — Automated performance profiling script.

### Usage

```bash
python tools/perf_stress_test.py --mode gpu --quick --settle 2
python tools/perf_stress_test.py --mode bullet --quick --settle 2
python tools/perf_stress_test.py --mode mixed --settle 3
python tools/perf_stress_test.py --mode all
```

### Modes

| Mode | Description |
|------|-------------|
| `gpu` | Ramp GPU particles: 100→10,000 (quick: 100→5,000) |
| `bullet` | Ramp Bullet objects: 25→300 |
| `mixed` | Fill Bullet to 50% cap, then ramp GPU |
| `scale` | Compare full/subcube/microcube performance |
| `sustained` | Hold 10,000 GPU particles for 30 seconds |
| `all` | Run all modes sequentially |

### Options

| Flag | Default | Description |
|------|---------|-------------|
| `--mode` | `gpu` | Test mode |
| `--quick` | off | Use fewer ramp steps |
| `--settle` | 3.0 | Seconds to wait after spawning before measuring |
| `--output` | auto | Output file prefix (auto-generates timestamped name) |

### Output

- **CSV**: Per-step measurements (FPS, CPU/GPU time, draw calls, active counts)
- **JSON**: Summary with FPS breakpoints (60/30/15 thresholds)
- **Console**: Formatted table with breakpoint summary

### Methodology

Each test step:
1. Spawns the target count of particles (cumulative, with 120s lifetime to prevent expiration)
2. Waits `settle_time` seconds for physics to stabilize
3. Samples `engine_timing` endpoint 10 times over 1 second
4. Records avg/min/max FPS, CPU frame time, active object counts
5. Calls `clear_dynamics` between test modes

## Key Source Files

| File | Purpose |
|------|---------|
| `engine/include/core/GpuParticlePhysics.h` | GPU particle system header (spawn API, constants) |
| `engine/src/core/GpuParticlePhysics.cpp` | GPU compute pipeline setup, dispatch, spawn queue |
| `engine/include/core/DynamicObjectManager.h` | Bullet dynamic object manager (caps, counts) |
| `engine/src/core/DynamicObjectManager.cpp` | Bullet lifecycle (update, expire, position sync) |
| `engine/src/scene/VoxelManipulationSystem.cpp` | Hybrid routing (break → Bullet or GPU) |
| `editor/src/Application.cpp` | Debug spawn handlers, timing API handlers |
| `engine/src/core/EngineAPIServer.cpp` | HTTP route registration for debug endpoints |
| `shaders/particle_integrate.comp` | XPBD integration compute shader |
| `shaders/particle_collide.comp` | Collision detection compute shader |
| `shaders/particle_expand.comp` | Face instance generation compute shader |
| `shaders/particle_types.glsl` | Shared particle struct definition |
| `tools/perf_stress_test.py` | Automated performance stress tester |

## See Also

- [DynamicSubcubeRenderPipeline.md](DynamicSubcubeRenderPipeline.md) — Rendering architecture for dynamic voxels
- [SubsystemArchitecture.md](SubsystemArchitecture.md) — Engine subsystem overview
- [CoordinateSystem.md](CoordinateSystem.md) — World coordinate conventions
