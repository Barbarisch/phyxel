# Voxel Render Pipelines

This document covers the low-level Vulkan rendering details for all three voxel pipelines. For the conceptual model (what cubes/subcubes/microcubes are, static vs kinematic vs dynamic), see [VoxelSystem.md](VoxelSystem.md) first.

## Overview

Three separate Vulkan pipelines render voxels. All three share the same fragment shader (`voxel.frag`) and texture atlas, but differ in vertex format, draw call type, and how UV offsets are computed.

| Property | Static | Kinematic | GPU Particle |
|---|---|---|---|
| **Shader** | `static_voxel.vert` | `kinematic_voxel.vert` | `dynamic_voxel.vert` |
| **Draw type** | `vkCmdDrawIndexed` | `vkCmdDrawIndexed` | `vkCmdDrawIndirect` (non-indexed) |
| **Instance data** | `InstanceData` (8B) | `KinematicFaceData` (40B) | `DynamicSubcubeInstanceData` (64B) |
| **UV offset** | GPU decodes from packed grid bits | CPU pre-computes per face in `buildFaces()` | GPU decodes from `localPosition` ivec3 |
| **Face culling** | CPU-side per chunk (only exposed faces) | None (all 6 faces per voxel) | None (all 6 faces per particle) |
| **Owner** | `ChunkManager` / `Chunk` | `KinematicVoxelPipeline` + `KinematicVoxelManager` | `GpuParticlePhysics` |
| **Physics** | None (world-static) | Bullet kinematic body (AABB box) | GPU XPBD compute |
| **Persistence** | SQLite world DB | None (reconstructed at load) | None (transient) |

```
                     ┌──────────────────────────┐
                     │   Per-Frame Render Loop   │
                     │     (drawFrame)           │
                     └────────────┬─────────────┘
                                  │
         ┌────────────────────────┼──────────────────────┐
         ▼                        ▼                       ▼
┌─────────────────┐    ┌──────────────────┐    ┌──────────────────┐
│   Shadow Pass   │    │  GPU Compute     │    │  Scene Pass      │
│                 │    │  (particles)     │    │                  │
└─────────────────┘    └──────────────────┘    └────────┬─────────┘
                                │                        │
                                │          ┌─────────────┼─────────────┐
                                │          ▼             ▼             ▼
                                │  ┌──────────────┐ ┌──────────┐ ┌──────────────┐
                                │  │   Static     │ │Kinematic │ │GPU Particles │
                                │  │  (indexed)   │ │(indexed) │ │ (indirect)   │
                                │  └──────────────┘ └──────────┘ └──────────────┘
                                │                                       ▲
                                └───────────────────────────────────────┘
                                   face buffer output (expand.comp)
```

---

## Render Frame Order

`RenderCoordinator::drawFrame()` executes in this order:

1. **Shadow pass** — depth-only render to shadow map
2. **GPU particle compute** — integrate → collide → expand (writes face buffer + indirect count)
3. **Begin scene render pass** (offscreen framebuffer)
4. **Static geometry** — bind static pipeline, per-chunk indexed draws
5. **Kinematic objects** — bind kinematic pipeline, one indexed draw per object with per-object push constant transform
6. **Dynamic voxels** — bind GPU particle pipeline, single indirect draw from GPU face buffer
7. **Entities** — animated characters, NPCs (instanced character pipeline)
8. **Debug lines** — raycast visualization, FOV cones
9. **Debris** — CPU-side particle system (DebrisRenderPipeline, separate vertex format)
10. **End scene render pass**
11. **Post-process pass** — fullscreen quad to swapchain
12. **UI** — ImGui overlay

---

## Static Pipeline

### Vertex + Index Buffers (Binding 0)

- **Vertex buffer**: 8 vertices, each containing a single `uint32_t vertexID` (0–7).
- **Index buffer**: 36 × `uint16_t` indices defining 12 triangles (2 per face, 6 faces).

The static vertex shader (`static_voxel.vert`) uses `vertexID` to index into hard-coded corner positions for the unit cube. The index buffer selects which corners form each triangle.

### Instance Buffer (Binding 1)

`InstanceData` — 8 bytes per instance, packed:

```
Bits  0-4:  X position (0-31 within chunk)
Bits  5-9:  Y position (0-31 within chunk)
Bits 10-14: Z position (0-31 within chunk)
Bits 15-17: Face ID (0-5)
Bits 18-19: Scale level (0=cube, 1=subcube, 2=microcube)
Bits 20-25: Parent subcube encoded position
Bits 26-31: Microcube encoded position
+ uint16 textureIndex
+ uint16 reserved
```

Each chunk has its own instance buffer. A push constant provides the chunk's world-space origin offset. The GPU decodes grid positions from the packed bits to compute the per-face UV offset for subcubes and microcubes.

### Winding Order

The pipeline uses **front-face culling** (`CULL_FRONT`) with `FRONT_FACE_COUNTER_CLOCKWISE`. Clockwise-wound triangles survive (classified as back-facing, not culled). This is consistent across all three pipelines.

---

## Kinematic Pipeline

### Overview

Kinematic objects are groups of voxels that move together under a shared world transform — doors, furniture, fragments. The CPU pre-builds all face data once when the object is created; only the transform changes each frame.

### Instance Buffer (Binding 1)

`KinematicFaceData` — 40 bytes per face, pre-built by `KinematicVoxelManager::buildFaces()`:

```
offset  0: vec3  localPosition  (12 bytes) — voxel center in object-local (hinge) space
offset 12: vec3  scale          (12 bytes) — (1,1,1)=cube, (1/3,1/3,1/3)=subcube, (1/9,1/9,1/9)=microcube
offset 24: vec2  uvOffset       (8 bytes)  — pre-computed UV offset within parent cube face
offset 32: uint32 textureIndex  (4 bytes)
offset 36: uint32 faceId        (4 bytes)  — 0=+Z, 1=-Z, 2=+X, 3=-X, 4=+Y, 5=-Y
```

All 6 faces are emitted for every voxel (no inter-voxel face culling). The `uvOffset` is computed on the CPU from each voxel's `parentFrac` field, which stores the voxel's position within its parent cube in [0,1) normalized coords.

### Transform

Each object's world transform is passed as a push constant (`mat4`, 64 bytes). The vertex shader applies it to `localPosition` to get the final world-space vertex position. One draw call is issued per object.

### Physics Collider

`KinematicVoxelManager::add()` creates a Bullet kinematic box body (AABB-sized) by default. The collider tracks the object's world transform via `syncCollidersToPhysics()` each frame, giving other physics objects something to collide against.

**Important:** Pass `skipCollider=true` when the owning system (e.g. `DynamicFurnitureManager`) already manages a `btRigidBody` for the object. Two overlapping colliders at the same position causes Bullet to eject the dynamic body. See `KinematicVoxelManager.h` for details.

---

## GPU Particle Pipeline (Dynamic Voxels)

### Pipeline Stages

Executed **before** the render pass each frame, in command buffer order:

1. **`particle_integrate.comp`** — XPBD position integration with gravity, angular velocity, sleep detection. Reads/writes `ParticleBuffer`.
2. **`particle_collide.comp`** — Voxel occupancy-grid collision (512×256×512 bitfield) and character AABB collision. Floor/ceiling/wall bounce with restitution and friction. Wakes sleeping particles on character overlap. Reads/writes `ParticleBuffer`, reads `OccupancyBuffer` and `CharacterBuffer`.
3. **`particle_expand.comp`** — Generates 6 face instances per active particle into the face buffer. Writes `FaceBuffer` and atomically increments `IndirectCmd.instanceCount`.

Memory barriers separate each stage to ensure writes complete before reads.

### Buffer Layout

| Buffer | Size | Usage |
|---|---|---|
| `ParticleBuffer` | 96 bytes × MAX_PARTICLES (10,000) | SSBO: position, rotation, velocity, material, lifetime |
| `FaceBuffer` | 64 bytes × MAX_FACE_SLOTS (60,000) | SSBO → vertex input: one `DynamicSubcubeInstanceData` per face |
| `IndirectCmd` | 16 bytes | `VkDrawIndirectCommand` with GPU-written instance count |
| `MatTexTable` | materialCount × 6 × 4 bytes | Material → texture index lookup (6 faces per material) |
| `Occupancy` | 512 × 256 × 512 bits (8 MB) | 3D voxel occupancy bitfield for collision |
| `CharacterBuffer` | 48 bytes | Player AABB: center, halfExtents, velocity, active flag |

### DynamicSubcubeInstanceData (64 bytes per face)

Written by `particle_expand.comp`, read by `dynamic_voxel.vert` as vertex input binding 1:

```
offset  0: vec3  worldPosition   (12 bytes) — physics center position
offset 12: uint16 textureIndex + uint16 reserved (4 bytes)
offset 16: uint32 faceID        (4 bytes)  — 0=+Z, 1=-Z, 2=+X, 3=-X, 4=+Y, 5=-Y
offset 20: vec3  scale          (12 bytes) — (1.0, 1.0, 1.0) for full cubes
offset 32: vec4  rotation       (16 bytes) — quaternion (x, y, z, w)
offset 48: ivec3 localPosition  (12 bytes) — subcube grid pos for UV selection
offset 60: uint32 reserved      (4 bytes)
```

### Indirect Draw Buffer

```
offset 0:  vertexCount    = 6   (fixed — 6 vertices per face, two triangles)
offset 4:  instanceCount  = N   (written by particle_expand.comp via atomicAdd)
offset 8:  firstVertex    = 0
offset 12: firstInstance  = 0
```

`vertexCount` is set to **6** at initialization and never changes. `instanceCount` is zeroed each frame (via `vkCmdFillBuffer` at offset 4, size 4), then atomically incremented by the expand shader.

### CPU Fallback Path

If GPU particle physics is not active, `renderDynamicSubcubes()` falls back to the legacy CPU path:

```cpp
// CPU path uses drawIndexed with 6 indices per face (from the shared index buffer)
vulkanDevice->drawIndexed(currentFrame, 6, faces.size());
```

This path reads `DynamicSubcubeInstanceData` from a host-visible buffer updated each frame by Bullet Physics position readback. It uses the **same** pipeline and shaders as the GPU path, but the index buffer **is** used here (6 indices = 1 face = 2 triangles).

> **Note**: The CPU fallback uses `drawIndexed` (indexed) while the GPU path uses `drawIndirect` (non-indexed). The vertex shader handles both because the `vertexID` range (0–5) maps to the same corner remap regardless of whether the ID comes from the index buffer or direct vertex count.

### Character AABB Collision

When a particle overlaps the character AABB:

1. **Shortest-axis push-out** — Find the minimum penetration across all 6 directions and push the particle out along that axis.
2. **Restitution bounce** — Reverse and scale velocity on the push-out axis by `pc.restitution`.
3. **Velocity transfer** — Add `charVelocity * dt * 0.5`, giving pushed debris a kick in the character's movement direction.
4. **Wake** — Clear the sleep flag.

Sleeping particles check character AABB overlap **before** the sleep early-exit — without this, the player walks straight through resting debris.

---

## Common Pitfalls

1. **Vertex count vs index count**: The GPU particle path draws 6 **vertices** per instance (non-indexed). The CPU fallback draws 6 **indices** per instance (indexed). Changing one without the other breaks the other path.

2. **Face buffer alignment**: Each GPU particle face instance is exactly 64 bytes (16 × uint32). The expand shader writes via raw `uint[]` indexing. If you change the struct layout, update both the C++ struct and the `writeFace()` function in `particle_expand.comp`.

3. **Indirect buffer zeroing**: Only `instanceCount` (offset 4, size 4) is zeroed each frame. `vertexCount` at offset 0 is set once during initialization. Do not zero the entire buffer.

4. **Binding 0 vertex buffer**: Both static and GPU particle pipelines bind the same 8-vertex buffer at binding 0. The GPU particle shader ignores it and uses `gl_VertexIndex` from the indirect draw's vertex count. Do not remove the vertex buffer binding — it is required by the pipeline layout.

5. **SSBO buffer usage flags**: Any buffer bound as an SSBO in a compute shader **must** have `STORAGE_BUFFER_BIT`. Using `createPersistentStagingBuffer` (TRANSFER_SRC only) produces silent failures — the shader reads zeros, no Vulkan validation error is raised.

6. **Sleeping particles skip collision**: The sleep early-return in `particle_collide.comp` skips ALL collision checks. Any new collision source (e.g. NPC AABBs, projectiles) must check before the sleep gate or wake particles in a pre-check, as the character AABB does.

7. **Kinematic duplicate colliders**: Registering a `KinematicVoxelObject` with `skipCollider=false` when the owning system already has a `btRigidBody` at the same position causes violent Bullet ejection. Always use `skipCollider=true` in those cases.

---

## See Also

- [VoxelSystem.md](VoxelSystem.md) — Conceptual model: voxel sizes, static/kinematic/dynamic states, fragment routing
- [DynamicVoxelPhysics.md](DynamicVoxelPhysics.md) — Hybrid Bullet+GPU physics system, routing logic, performance data
- [CoordinateSystem.md](CoordinateSystem.md) — World vs local coordinates
- [MultiChunkSystem.md](MultiChunkSystem.md) — Chunk management overview
- [SubsystemArchitecture.md](SubsystemArchitecture.md) — Engine subsystem overview
