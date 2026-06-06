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

## Mirror Reflection Pipeline

Reflective surfaces (the `Mirror` material, `isMirror: true` in `materials.json`) use a planar-reflection system layered on top of the static pipeline. It was a hard feature to get right — most of the gotchas below cost real debugging time, so read them before touching this code.

### How a mirror is detected

- `isMirror` is baked into **every face** of a mirror voxel as **bit 10** of `InstanceData.reserved` (`ChunkRenderManager`).
- `Chunk::recomputeRenderFlags()` (runs only on `rebuildFaces`, never per-frame) caches `m_hasMirror` and the local position of the first mirror voxel.
- `RenderCoordinator::scanForMirrorVoxels()` reads those cached flags each frame — O(visible chunks), not a per-voxel scan.

The current implementation supports **one mirror plane per frame** (the first mirror voxel found) with a **hard-coded `+Z` normal**. The reflection plane is placed on the voxel's **visible `+Z` face** (`local.z + 1.0`), not its center — using the center misregisters reflections by half a voxel.

### Three stages per frame (only when a mirror is visible)

1. **Reflection pass** (`renderReflectionPass`) — runs **before** the main scene pass. Renders the scene from a mirrored virtual camera into an offscreen color+depth target (`PostProcessor` reflection framebuffer) using `reflectionScenePipeline` (shares `voxel.frag`). `voxel.frag` discards mirror faces (bit 10) so the mirror wall itself does not appear in its own reflection. The reflected view-projection is stored in the UBO (`reflectedViewProj`).
2. **Main scene pass** — opaque static/kinematic/dynamic geometry + entities, exactly as normal. `voxel.frag` discards mirror faces here too (they are drawn separately in stage 3).
3. **Mirror geometry pass** (`renderMirrorGeometry`) — runs **inside** the scene pass, after all opaque/entity geometry. Draws only the mirror voxel faces with `mirrorPipeline` (`mirror_voxel.frag`). The fragment shader projects each mirror-surface world position through `reflectedViewProj` (projective texturing) to sample the reflection target, then applies a faint tint + edge darkening so the surface reads as a mirror rather than a hole.

### The reflected view matrix

```cpp
glm::mat4 reflectedView = camera->getViewMatrix() * reflMat;   // reflMat = Householder reflection about the mirror plane
```

Build it by **composing the reflection into the main view** — **not** `glm::lookAt(reflectedEye, reflectedFront, reflectedUp)`. `lookAt` re-derives the camera's right axis with `cross(front, up)`, which absorbs the reflection's `det = −1` sign. That produces two bugs at once: the reflection comes out **horizontally mirrored**, and the view ends up `det = +1` so triangle winding does **not** flip (breaking the cull-mode assumption below).

### Cull modes (coupled to the reflected view)

| Pipeline | Cull | Why |
|---|---|---|
| Main static voxel | `FRONT_BIT` | Engine convention (see Static Pipeline → Winding Order) |
| **Mirror geometry** (`mirrorPipeline`) | `FRONT_BIT` | Mirror faces are drawn from the **main** camera with the same winding as every other voxel — must match the main pipeline. `BACK_BIT` shows the mirror's back side / culls it from the viewing side. |
| **Reflection scene** (`reflectionScenePipeline`) | `BACK_BIT` | The reflected view has `det = −1`, which flips winding, so the covering triangles survive under the **opposite** cull. This is only correct because `reflectedView = mainView * reflMat`; if it is ever rebuilt with `lookAt`, revert to `FRONT_BIT`. |

### Near plane: do NOT pin it at the mirror

The reflection projection uses a **small near plane near the virtual camera** (`reflNear = 0.3`), not a near plane at the mirror surface. A perspective near plane is perpendicular to the view direction; pinning it at the mirror means that when you look at the mirror **at an angle** it tilts relative to the mirror plane and clips a wedge of the floor nearest the base — a dark "see-through" band along the bottom edge.

**Trade-off:** the small near plane also renders geometry directly *behind* the mirror into the reflection. For a mirror on a solid wall this is invisible and a continuous floor simply fills the bottom edge correctly. For a mirror with a real room behind it, this leaks — the exact fix is **oblique near-plane clipping** (Lengyel) or a `gl_ClipDistance` world-plane clip at the mirror surface. Left as a future enhancement.

### Shader knobs (`mirror_voxel.frag`)

`MIRROR_TINT` (faint cool silver) and `MIRROR_REFLECT` (~0.90) plus a `mix(0.78, 1.0, cosTheta)` edge darkening. A perfect untinted 100% reflector reads as a hole, not a surface — these small imperfections are deliberate.

### Characters in reflections

Instanced characters (the player + animated NPCs) reflect via the shared helper `RenderCoordinator::renderInstancedCharacters(cmd, viewProj, pipeline)`. The instanced character pipeline takes its **view-projection as a push constant**, so the reflection pass simply passes `clippedProj * reflectedView` and a dedicated `reflectionInstancedCharacterPipeline` whose only difference is **`FRONT_BIT` culling** (the reflected view's `det = −1` flips winding — same coupling as `reflectionScenePipeline`).

The character instance buffer is single + host-visible, so the helper rebuilds and re-uploads it on every call. When both the reflection pass and the main pass run in one frame they upload **byte-identical** data (same character state, same batch offsets), so the redundant upload is harmless — both draws read the same final buffer contents at GPU execution. Only instanced characters are reflected; non-instanced/ragdoll entities are not (yet).

### Kinematic objects in reflections

Kinematic voxels (doors, furniture, fragments) also reflect. `KinematicVoxelPipeline` has a `renderReflection()` that mirrors `render()` but binds a **BACK_BIT** reflection pipeline (`m_reflectionPipeline`, opposite of the main pass's FRONT_BIT for the flipped reflected winding). Unlike characters, these read view/proj from the **descriptor set**, so the reflection pass passes `VulkanDevice::getReflectionDescriptorSet(frame)` — the set whose UBO holds the reflected camera. No push-constant change is needed (the per-object world transform is already in world space). The shared instance buffer is only rebuilt on object add/remove (in the main pass), so a newly added/removed kinematic object can lag one frame in the reflection; moving objects (the common case) reflect correctly every frame.

### What does *not* reflect yet

Dynamic voxels / debris (the GPU-particle + CPU-Bullet `renderDynamicSubcubes` paths). Same approach would work — a BACK_BIT reflection pipeline + replay the indirect/CPU draws with the reflected descriptor set — but debris counts can reach ~10k particles, so it is best paired with a reduced-resolution reflection target. Deferred.

---

## Common Pitfalls

1. **Vertex count vs index count**: The GPU particle path draws 6 **vertices** per instance (non-indexed). The CPU fallback draws 6 **indices** per instance (indexed). Changing one without the other breaks the other path.

2. **Face buffer alignment**: Each GPU particle face instance is exactly 64 bytes (16 × uint32). The expand shader writes via raw `uint[]` indexing. If you change the struct layout, update both the C++ struct and the `writeFace()` function in `particle_expand.comp`.

3. **Indirect buffer zeroing**: Only `instanceCount` (offset 4, size 4) is zeroed each frame. `vertexCount` at offset 0 is set once during initialization. Do not zero the entire buffer.

4. **Binding 0 vertex buffer**: Both static and GPU particle pipelines bind the same 8-vertex buffer at binding 0. The GPU particle shader ignores it and uses `gl_VertexIndex` from the indirect draw's vertex count. Do not remove the vertex buffer binding — it is required by the pipeline layout.

5. **SSBO buffer usage flags**: Any buffer bound as an SSBO in a compute shader **must** have `STORAGE_BUFFER_BIT`. Using `createPersistentStagingBuffer` (TRANSFER_SRC only) produces silent failures — the shader reads zeros, no Vulkan validation error is raised.

6. **Sleeping particles skip collision**: The sleep early-return in `particle_collide.comp` skips ALL collision checks. Any new collision source (e.g. NPC AABBs, projectiles) must check before the sleep gate or wake particles in a pre-check, as the character AABB does.

7. **Kinematic duplicate colliders**: Registering a `KinematicVoxelObject` with `skipCollider=false` when the owning system already has a `btRigidBody` at the same position causes violent Bullet ejection. Always use `skipCollider=true` in those cases.

8. **Rebind vertex binding 0 in every custom pass**: The static draw is `drawIndexed(36, numInstances)` — the full 36-index cube buffer is replayed for each one-face instance, and the shader collapses it onto a single quad, relying on face culling to leave a covering set of triangles. Entity, kinematic, dynamic, mirror, and reflection passes all rebind vertex **binding 0** to their own buffers. Any pass that issues the static draw **must call `vulkanDevice->bindVertexBuffers(frameIndex)` first** to restore the shared cube geometry. If you forget, the chunk faces pull cube vertices from whatever buffer was left bound (a previous frame's, or another pass's) and you get **torn geometry / per-quad triangle holes** that depend on view angle and frame timing. This bit both the reflection pass (runs at frame start, inherits the previous frame's binding) and the mirror geometry pass (runs after the entity pass). The main scene pass already does this — see the comment in `renderScene`.

---

## See Also

- [VoxelSystem.md](VoxelSystem.md) — Conceptual model: voxel sizes, static/kinematic/dynamic states, fragment routing
- [DynamicVoxelPhysics.md](DynamicVoxelPhysics.md) — Hybrid Bullet+GPU physics system, routing logic, performance data
- [CoordinateSystem.md](CoordinateSystem.md) — World vs local coordinates
- [MultiChunkSystem.md](MultiChunkSystem.md) — Chunk management overview
- [SubsystemArchitecture.md](SubsystemArchitecture.md) — Engine subsystem overview
