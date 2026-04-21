# Voxel System

This document explains the core voxel model in Phyxel: the three voxel sizes, and the three lifecycle states a voxel can be in. Read this before diving into the rendering or physics details.

---

## Voxel Sizes

All voxels in Phyxel are axis-aligned cubes. There are three sizes, each a subdivision of the previous:

| Name | Scale | Side length | Notes |
|------|-------|-------------|-------|
| **Cube** | 1.0 | 1 world unit | Standard terrain and building block |
| **Subcube** | 1/3 | ~0.333 world units | Fine detail within a cube cell |
| **Microcube** | 1/9 | ~0.111 world units | Ultra-fine detail, rarely used |

Subcubes and microcubes occupy a grid within their parent cube cell. A subcube at grid position `(gx, gy, gz)` (where each axis is 0–2) has its center at `parentCubeCenter + (gx - 1) / 3`. Microcubes use a 9-slot grid within their parent subcube.

### Texture Mapping

The texture atlas is designed around full cubes — each material has one texture per face. Subcubes and microcubes show only their **slice** of the parent cube's texture, not a scaled-down version of it. A subcube at grid `(1, 2, 0)` on the +Z face will show the center-bottom third of that face's texture.

This is consistent across all three rendering paths (static, kinematic, GPU particle). See [VoxelRenderPipelines.md](VoxelRenderPipelines.md) for how each path implements it.

---

## Voxel Lifecycle States

Every voxel is in exactly one of three states at any time. The state determines which system owns it, how it renders, and whether it persists.

```
                ┌──────────────┐
                │    Static    │  ← default state for placed voxels
                │   (Chunk)    │
                └──────┬───────┘
                       │  activate furniture / door system
                       ▼
                ┌──────────────┐
                │  Kinematic   │  ← moves as a rigid group
                │  (Object)    │
                └──────┬───────┘
                       │  break / shatter
                       ▼
                ┌──────────────┐
                │   Dynamic    │  ← individual physics simulation
                │  (Particle)  │
                └──────────────┘
```

Transitions are one-way in normal gameplay — voxels flow from static → kinematic → dynamic. Kinematic objects can be re-staticized (e.g. deactivating furniture writes voxels back to the chunk).

---

## Static Voxels

**Owner:** `ChunkManager` / `Chunk`
**Renderer:** static pipeline (`static_voxel.vert`)
**Persistence:** SQLite world database

Static voxels are baked into 32×32×32 chunk instance buffers. This is the default state for any placed voxel. The chunk system performs CPU-side face culling — only exposed faces are uploaded to the GPU, keeping buffer sizes minimal.

Key properties:
- Stored by chunk-local coordinate index: `z + y*32 + x*1024`
- All three sizes (cube/subcube/microcube) are valid static voxels
- Persisted in `worlds/default.db` (or per-scene DB) on save
- Cannot move — world position is implicit from chunk origin + local coords

See [CoordinateSystem.md](CoordinateSystem.md) and [MultiChunkSystem.md](MultiChunkSystem.md).

---

## Kinematic Objects

**Owner:** `KinematicVoxelManager`
**Renderer:** kinematic pipeline (`kinematic_voxel.vert`)
**Persistence:** none — reconstructed from template/definition at load

A kinematic object is a **group of voxels that moves as a rigid unit**. All voxels in the group share a single world transform (position + rotation) that is updated each frame by an owning system.

Uses include:
- Doors (animated swing via `DoorSystem`)
- Dynamic furniture (activated via `DynamicFurnitureManager`)
- Breakable fragments (intermediate state before full shattering)
- Drawbridges, elevators, or any other moving structure

The `KinematicVoxelObject` stores voxels in **hinge-local space** — positions relative to the object's pivot point. The CPU pre-builds one `KinematicFaceData` entry per face (6 per voxel) including pre-computed UV offsets for sub-tile texture mapping. No face culling is performed between voxels in the same object.

Physics: by default `KinematicVoxelManager::add()` creates a Bullet kinematic box body sized to the object's AABB. Pass `skipCollider=true` when the owning system (e.g. `DynamicFurnitureManager`) manages its own `btRigidBody` — two overlapping colliders cause violent ejection.

---

## Dynamic Particles (GPU)

**Owner:** `GpuParticlePhysics`
**Renderer:** GPU particle pipeline (`dynamic_voxel.vert`)
**Persistence:** none — transient, cleared between sessions

Dynamic particles are individually simulated voxels running on the GPU via Vulkan compute shaders (XPBD integration). They are used for:
- Debris from broken voxels or shattered furniture
- Small fragments (< 4 voxels) that don't justify a full kinematic body

Physics runs entirely on the GPU: `particle_integrate.comp` → `particle_collide.comp` → `particle_expand.comp`. The expand stage writes 6 face instances per active particle directly into the face buffer for rendering, bypassing the CPU entirely.

Capacity: up to **10,000 particles** simultaneously. Excess fragments are silently discarded. Particles sleep when at rest and are woken by nearby character overlap.

See [DynamicVoxelPhysics.md](DynamicVoxelPhysics.md) for full routing logic (how the engine decides between kinematic vs GPU particle for a given fragment).

---

## Fragment Routing

When a kinematic object shatters, fragments are routed based on size:

| Fragment size | Destination |
|---------------|-------------|
| ≥ 4 voxels | New `KinematicVoxelObject` (rigid body fragment) |
| < 4 voxels | GPU particle (XPBD debris) |

This keeps the kinematic object count manageable while still producing visible small debris.

---

## See Also

- [VoxelRenderPipelines.md](VoxelRenderPipelines.md) — How each state is rendered (shaders, instance data, draw calls)
- [DynamicVoxelPhysics.md](DynamicVoxelPhysics.md) — Physics routing, Bullet vs GPU particle decision logic
- [CoordinateSystem.md](CoordinateSystem.md) — World and chunk-local coordinate conventions
- [MultiChunkSystem.md](MultiChunkSystem.md) — Chunk management, streaming, persistence
