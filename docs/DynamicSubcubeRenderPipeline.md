# Dynamic Voxel Render Pipeline

## Overview

The Phyxel engine uses a **dual-pipeline architecture** to render voxels:

1. **Static Pipeline** — Grid-aligned cubes/subcubes/microcubes baked into per-chunk instance buffers. Uses indexed drawing with an 8-vertex cube + 36-index buffer and per-instance packed data.
2. **Dynamic Pipeline** — Physics-driven voxels simulated on the GPU via compute shaders (XPBD integration, occupancy-grid collision, face expansion). Rendered with non-indexed indirect draw.

Both pipelines share the same fragment shader and texture atlas, but differ fundamentally in how vertices are generated and draw calls are issued.

```
                     ┌──────────────────────────┐
                     │   Per-Frame Render Loop   │
                     │     (drawFrame)           │
                     └────────────┬─────────────┘
                                  │
              ┌───────────────────┼───────────────────┐
              ▼                   ▼                   ▼
   ┌──────────────────┐ ┌──────────────────┐ ┌──────────────────┐
   │   Shadow Pass    │ │  GPU Compute     │ │  Scene Pass      │
   │                  │ │  (particles)     │ │                  │
   └──────────────────┘ └──────────────────┘ └──────────────────┘
                                  │                   │
                                  │           ┌───────┴────────┐
                                  │           ▼                ▼
                                  │  ┌─────────────┐  ┌──────────────┐
                                  │  │Static Geom  │  │Dynamic Voxels│
                                  │  │(indexed     │  │(indirect     │
                                  │  │ draw)       │  │ draw)        │
                                  │  └─────────────┘  └──────────────┘
                                  │                          ▲
                                  └──────────────────────────┘
                                    face buffer output
```

---

## Rendering Method Reference

### Key Invariants for Future Changes

When modifying the rendering pipeline, keep these critical facts in mind:

| Property | Static Pipeline | Dynamic (GPU) Pipeline |
|---|---|---|
| **Draw type** | `vkCmdDrawIndexed` | `vkCmdDrawIndirect` (non-indexed) |
| **Topology** | `TRIANGLE_LIST` | `TRIANGLE_LIST` |
| **Vertices per face** | 36 indices / 8 vertices (full cube) | **6 vertices per face instance** |
| **Vertex source** | Binding 0: 8-vertex cube (vertex IDs 0-7) | Binding 0: same 8-vertex buffer (unused by shader — vertexID comes from indirect vertexCount) |
| **Instance source** | Binding 1: `InstanceData` (8 bytes, packed) | Binding 1: `DynamicSubcubeInstanceData` (64 bytes, from GPU face buffer) |
| **Vertex shader** | `cube.vert` — unpacks position from `packedData` bits | `dynamic_voxel.vert` — procedurally generates face quads from `vertexID` 0-5 |
| **Index buffer** | Used (36 indices define all 6 faces) | **Bound but ignored** — `vkCmdDrawIndirect` is non-indexed |
| **Cull mode** | `VK_CULL_MODE_FRONT_BIT` | `VK_CULL_MODE_FRONT_BIT` |
| **Front face** | `VK_FRONT_FACE_COUNTER_CLOCKWISE` | `VK_FRONT_FACE_COUNTER_CLOCKWISE` |
| **Surviving winding** | CW from outside (front-face culled) | CW from outside (front-face culled) |

### Winding Order (Critical)

The pipeline uses **front-face culling** (`CULL_FRONT`) with `FRONT_FACE_COUNTER_CLOCKWISE`.
This means **clockwise-wound triangles survive** (they are classified as back-facing, thus not culled).

In `dynamic_voxel.vert`, each face quad is split into two triangles via a corner remap:

```glsl
// Corner bit layout: bit0 = axis1, bit1 = axis2
//   corner 0 = (0,0), corner 1 = (1,0), corner 2 = (0,1), corner 3 = (1,1)
const uint cornerRemap[6] = uint[6](0u, 2u, 1u, 1u, 2u, 3u);
//  Triangle 1: corners 0 → 2 → 1  (CW from outside)
//  Triangle 2: corners 1 → 2 → 3  (CW from outside)
```

**If you change `cullMode` or `frontFace`, you must also update `cornerRemap` to match.**

### Indirect Draw Buffer Layout

The GPU path uses `VkDrawIndirectCommand` (16 bytes):

```
offset 0:  vertexCount    = 6   (fixed — 6 vertices per face, two triangles)
offset 4:  instanceCount  = N   (written by particle_expand.comp via atomicAdd)
offset 8:  firstVertex    = 0
offset 12: firstInstance  = 0
```

- `vertexCount` is set to **6** at initialization and never changes.
- `instanceCount` is zeroed each frame (via `vkCmdFillBuffer` at offset 4, size 4), then atomically incremented by the expand compute shader (6 face slots per active particle).
- The expand shader allocates face slots via `atomicAdd(instanceCount, 6)`.

**If you change the number of vertices per face (e.g. switching to triangle strips), update both the indirect buffer init in `GpuParticlePhysics.cpp` and the `cornerRemap` in the vertex shader.**

---

## GPU Compute Pipeline (Dynamic Voxels)

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

---

## Static Pipeline

### Vertex + Index Buffers (Binding 0)

- **Vertex buffer**: 8 vertices, each containing a single `uint32_t vertexID` (0-7).
- **Index buffer**: 36 × `uint16_t` indices defining 12 triangles (2 per face, 6 faces).

The static vertex shader (`cube.vert`) uses `vertexID` to index into hard-coded corner positions for the unit cube. The index buffer selects which corners form each triangle.

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

Each chunk has its own instance buffer. A push constant provides the chunk's world-space origin offset.

---

## Render Frame Order

`RenderCoordinator::drawFrame()` executes in this order:

1. **Shadow pass** — depth-only render to shadow map
2. **GPU particle compute** — integrate → collide → expand (writes face buffer + indirect count)
3. **Begin scene render pass** (offscreen framebuffer)
4. **Static geometry** — bind static pipeline, per-chunk indexed draws
5. **Dynamic voxels** — bind dynamic pipeline, single indirect draw from GPU face buffer
6. **Entities** — animated characters, NPCs (instanced character pipeline)
7. **Debug lines** — raycast visualization, FOV cones
8. **Debris** — CPU-side particle system (DebrisRenderPipeline, separate vertex format)
9. **End scene render pass**
10. **Post-process pass** — fullscreen quad to swapchain
11. **UI** — ImGui overlay

---

## CPU Fallback Path

If GPU particle physics is not active, `renderDynamicSubcubes()` falls back to the legacy CPU path:

```cpp
// CPU path uses drawIndexed with 6 indices per face (from the shared index buffer)
vulkanDevice->drawIndexed(currentFrame, 6, faces.size());
```

This path reads `DynamicSubcubeInstanceData` from a host-visible buffer updated each frame by Bullet Physics position readback. It uses the **same** pipeline and shaders as the GPU path, but the index buffer **is** used here (6 indices = 1 face = 2 triangles).

> **Note**: The CPU fallback uses `drawIndexed` (indexed) while the GPU path uses `drawIndirect` (non-indexed). The vertex shader handles both correctly because the `vertexID` range (0-5 for 6 vertices) maps to the same corner remap regardless of whether the ID comes from the index buffer or direct vertex count.

---

## Common Pitfalls

1. **Vertex count vs index count**: The GPU path draws 6 **vertices** per instance (non-indexed). The CPU path draws 6 **indices** per instance (indexed). Changing one without the other will break the other path.

2. **Face buffer alignment**: Each face instance is exactly 64 bytes (16 × uint32). The expand shader writes via raw `uint[]` indexing. If you change the struct layout, update both the C++ struct and the `writeFace()` function in `particle_expand.comp`.

3. **Indirect buffer zeroing**: Only `instanceCount` (offset 4, size 4) is zeroed each frame. `vertexCount` at offset 0 is set once during initialization. Do not zero the entire buffer.

4. **Binding 0 vertex buffer**: Both static and dynamic pipelines bind the same 8-vertex buffer at binding 0. The dynamic pipeline's vertex shader ignores the actual vertex data and uses `gl_VertexIndex` (mapped to `vertexID` input) generated by the indirect draw's vertex count. Do not remove the vertex buffer binding — it is required by the pipeline layout.

---

## Collision System (particle_collide.comp)

### Overview

The collide pass handles two types of collision for GPU particles:

1. **Voxel occupancy grid** — 3D bitfield (512×256×512, 8 MB) tracks which world voxels are solid. Six-direction axis-aligned checks push particles out of occupied cells with restitution bounce and friction.
2. **Character AABB** — A single player-character bounding box uploaded each frame from the CPU. Particles bounce off the character and receive its velocity on contact.

### Bindings

| Binding | Buffer | Access | Description |
|---|---|---|---|
| 0 | `ParticleBuffer` | read/write | Particle positions, velocities, flags |
| 1 | `OccupancyBuffer` | read-only | 3D voxel occupancy bitfield |
| 2 | `CharacterBuffer` | read-only | Player character AABB (48 bytes) |

### Character AABB Buffer (CharacterCollider — 48 bytes, std430)

```
offset  0: vec3  center       (12 bytes) — AABB world-space center
offset 12: float pad0         (4 bytes)
offset 16: vec3  halfExtents  (12 bytes) — half-size per axis
offset 28: float pad1         (4 bytes)
offset 32: vec3  velocity     (12 bytes) — character's linear velocity
offset 44: float active       (4 bytes)  — 1.0 = enabled, 0.0 = disabled
```

**CPU side** (`GpuParticlePhysics::setCharacterAABB`): Written each frame from `Application::update()` using the animated character's Bullet `controllerBody` position and velocity. The `getPosition()` returns feet position; center = feet + (0, halfHeight, 0).

### Sleeping Particle Wake-Up

Particles that come to rest on the ground enter a **sleep state** (`PARTICLE_SLEEPING` flag). Sleeping particles skip physics entirely for performance. However, the character AABB overlap check runs **before** the sleep early-exit:

```glsl
// Wake sleeping particles if the character AABB overlaps them
if ((p.flags & PARTICLE_SLEEPING) != 0u) {
    if (charActive > 0.5) {
        // ... AABB overlap test ...
        if (overlapping) {
            p.flags &= ~PARTICLE_SLEEPING;  // wake up
            p.prevPosition = p.position;     // zero velocity on wake
        } else {
            return;  // still sleeping, no overlap
        }
    } else {
        return;  // no character, stay asleep
    }
}
```

**Without this wake-up**, sleeping debris is invisible to the character — the player walks straight through it. This was a critical bug: the sleep early-return skipped the character collision check entirely.

### Character AABB Collision Response

When a particle overlaps the character AABB:

1. **Shortest-axis push-out** — Find the minimum penetration across all 6 directions (+X, -X, +Y, -Y, +Z, -Z) and push the particle out along that axis.
2. **Restitution bounce** — Reverse and scale velocity on the push-out axis by `pc.restitution`.
3. **Velocity transfer** — Add `charVelocity * dt * 0.5` to the particle velocity, giving pushed debris a "kick" in the character's movement direction.
4. **Wake** — Clear the sleep flag so the particle stays awake.

### Buffer Creation Requirements

All SSBO buffers read by compute shaders **must** be created with `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT`:

```cpp
// CORRECT — GPU can read as SSBO
bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

// WRONG — createPersistentStagingBuffer() uses TRANSFER_SRC_BIT only
// GPU reads garbage/zeros, no validation error, particles pass through character
dev->createPersistentStagingBuffer(size, buf, mem, &mapped);  // DO NOT USE for SSBOs
```

`createPersistentStagingBuffer` is designed for staging (CPU→GPU copy source). It creates buffers with `VK_BUFFER_USAGE_TRANSFER_SRC_BIT` which **cannot** be bound as storage buffers. For host-written SSBOs, manually create the buffer with:
- `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT`
- `VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT`

The occupancy grid and character AABB buffers both use this pattern.

---

## Common Pitfalls

1. **Vertex count vs index count**: The GPU path draws 6 **vertices** per instance (non-indexed). The CPU path draws 6 **indices** per instance (indexed). Changing one without the other will break the other path.

2. **Face buffer alignment**: Each face instance is exactly 64 bytes (16 × uint32). The expand shader writes via raw `uint[]` indexing. If you change the struct layout, update both the C++ struct and the `writeFace()` function in `particle_expand.comp`.

3. **Indirect buffer zeroing**: Only `instanceCount` (offset 4, size 4) is zeroed each frame. `vertexCount` at offset 0 is set once during initialization. Do not zero the entire buffer.

4. **Binding 0 vertex buffer**: Both static and dynamic pipelines bind the same 8-vertex buffer at binding 0. The dynamic pipeline's vertex shader ignores the actual vertex data and uses `gl_VertexIndex` (mapped to `vertexID` input) generated by the indirect draw's vertex count. Do not remove the vertex buffer binding — it is required by the pipeline layout.

5. **SSBO buffer usage flags**: Any buffer bound as an SSBO in a compute shader **must** have `STORAGE_BUFFER_BIT`. Using `createPersistentStagingBuffer` (TRANSFER_SRC only) produces silent failures — the shader reads zeros, no Vulkan validation error is raised, and the feature silently does nothing.

6. **Sleeping particles skip collision**: The sleep early-return in `particle_collide.comp` skips ALL collision checks. Any new collision source (e.g. NPC AABBs, projectiles) must either be checked before the sleep gate or must wake particles in a pre-check, as the character AABB does.

## See Also

- [CoordinateSystem.md](CoordinateSystem.md) — World vs local coordinates
- [MultiChunkSystem.md](MultiChunkSystem.md) — Chunk management overview
- [SubsystemArchitecture.md](SubsystemArchitecture.md) — Engine subsystem overview
