# Phyxel Coordinate System Documentation

## Overview

This document provides detailed information about the coordinate system, indexing schemes, and memory layout used throughout the Phyxel engine. Understanding these details is critical for proper cube placement, hover detection, and avoiding coordinate transformation bugs.

## Table of Contents

1. [Coordinate System Fundamentals](#coordinate-system-fundamentals)
2. [Chunk System Architecture](#chunk-system-architecture)
3. [Index Calculation and Memory Layout](#index-calculation-and-memory-layout)
4. [Common Pitfalls and Solutions](#common-pitfalls-and-solutions)
5. [Debugging Coordinate Issues](#debugging-coordinate-issues)
6. [API Reference](#api-reference)

---

## Coordinate System Fundamentals

### World Coordinate System

Phyxel uses a **right-handed coordinate system** with the following conventions:

- **X-axis**: Points to the right (positive X = east)
- **Y-axis**: Points upward (positive Y = up) 
- **Z-axis**: Points toward the viewer (positive Z = north, out of screen)
- **Origin**: Located at world position (0, 0, 0)

### Coordinate Ranges

- **World Coordinates**: Unlimited range, can be negative
- **Chunk Coordinates**: Integer division of world coordinates by 32
- **Local Coordinates**: 0-31 range within each chunk dimension

---

## Chunk System Architecture

### Chunk Organization

Each chunk represents a **32×32×32** voxel grid in world space:

```
Chunk Size: 32³ = 32,768 cubes per chunk
Memory per Chunk: ~393 KB (32,768 × 12 bytes per cube)
```

### Coordinate Conversion Functions

> Verified against current source: `ChunkManager::worldToChunkCoord/worldToLocalCoord/chunkCoordToOrigin`
> (`engine/include/core/ChunkManager.h`) are thin static forwarders to `Utils::CoordinateUtils`
> (`engine/include/utils/CoordinateUtils.h`, `engine/src/utils/CoordinateUtils.cpp`), which **already
> implements proper floor division/modulo for negative coordinates** — the naive `/32`/`%32` shown in
> older drafts of this doc is not what ships. The "Common Pitfalls" section below documents the bug
> this replaced; treat it as historical rationale, not an open risk.

```cpp
// ChunkManager::worldToChunkCoord() → Utils::CoordinateUtils::worldToChunkCoord()
glm::ivec3 worldToChunkCoord(const glm::ivec3& worldPos) {
    glm::ivec3 chunk;
    chunk.x = worldPos.x >= 0 ? worldPos.x / 32 : (worldPos.x - 31) / 32;
    chunk.y = worldPos.y >= 0 ? worldPos.y / 32 : (worldPos.y - 31) / 32;
    chunk.z = worldPos.z >= 0 ? worldPos.z / 32 : (worldPos.z - 31) / 32;
    return chunk;
}

// ChunkManager::worldToLocalCoord() → Utils::CoordinateUtils::worldToLocalCoord()
glm::ivec3 worldToLocalCoord(const glm::ivec3& worldPos) {
    glm::ivec3 local;
    local.x = ((worldPos.x % 32) + 32) % 32;
    local.y = ((worldPos.y % 32) + 32) % 32;
    local.z = ((worldPos.z % 32) + 32) % 32;
    return local;
}

// ChunkManager::chunkCoordToOrigin() → Utils::CoordinateUtils::chunkCoordToOrigin()
glm::ivec3 chunkCoordToOrigin(const glm::ivec3& chunkCoord) { 
    return chunkCoord * 32; 
}
```

### Example Coordinate Mapping

```
World Position: (35, 17, 63)
├── Chunk Coordinate: (1, 0, 1)     // (35/32, 17/32, 63/32)
├── Local Position: (3, 17, 31)     // (35%32, 17%32, 63%32)
└── Chunk World Origin: (32, 0, 32) // (1*32, 0*32, 1*32)
```

---

## Index Calculation and Memory Layout

### ⚠️ CRITICAL: Loop Order vs Index Formula

This is the most important concept to understand, as it was the source of the X/Z axis flipping bug.

#### Cube Creation Loop Order (X-Major) — historical framing, formula still current

> **Post chunk-storage-rewrite correction:** there is no longer a literal `ChunkManager::populateChunk()`
> triple-nested loop. A freshly filled chunk goes through `Chunk::populateWithCubes()`
> (`engine/src/core/Chunk.cpp`), which does an **O(1) palette-store fill**
> (`voxelManager.fillAllVoxels("Default")` against `ChunkVoxelStore`,
> `engine/include/core/ChunkVoxelStore.h`) rather than materializing 32,768 heap `Cube` objects one
> at a time. The **Z-minor index formula below is still exactly what's live** — `ChunkVoxelStore`'s
> own doc comment states it explicitly ("flat index (z + y*32 + x*1024)") — so the memory-layout
> reasoning in this section remains correct; only the "created via an explicit X-major loop" framing
> is now conceptual/historical rather than literal code.

**Conceptual memory layout** (still how the flat index decodes): `[x=0,y=0,z=0], [x=0,y=0,z=1], [x=0,y=0,z=2], ..., [x=0,y=1,z=0], ...`

#### Index Calculation Formula (current source: `Chunk::localToIndex()`)

> **Correction:** this is a static method on **`Chunk`** (`engine/include/core/Chunk.h`,
> `engine/src/core/Chunk.cpp`), not on `ChunkManager` — `ChunkManager` has no `localToIndex`.

```cpp
// CORRECT: Z-minor indexing (Z changes fastest) — Chunk::localToIndex()
static size_t localToIndex(const glm::ivec3& localPos) { 
    return localPos.z + localPos.y * 32 + localPos.x * 32 * 32; 
}

// WRONG: X-minor indexing (would cause axis flipping)
// return localPos.x + localPos.y * 32 + localPos.z * 32 * 32;
```

#### Mathematical Explanation

For a 3D array stored as 1D memory with dimensions (X=32, Y=32, Z=32):

- **Z-minor formula**: `index = z + y*32 + x*32²`
- **Stride values**: Z-stride=1, Y-stride=32, X-stride=1024

This formula ensures that adjacent Z values are stored contiguously in memory, matching the innermost loop traversal.

### Visual Memory Layout

```
Array Index:    0    1    2   ...   31   32   33  ...
Local Coords: (0,0,0) (0,0,1) (0,0,2) ... (0,0,31) (0,1,0) (0,1,1) ...
              └─ Z changes fastest ─┘    └─ Y increments ─┘
```

---

## Common Pitfalls and Solutions

### 1. Axis Flipping Bug

**Problem**: Using X-minor indexing with X-major loop order causes X and Z axes to appear flipped.

**Symptoms**:
- Hover detection works but highlights wrong cubes
- Coordinate transformations appear to swap X and Z
- Debug output shows correct coordinates but wrong visual results

**Solution**: Ensure index formula matches loop order (use Z-minor indexing).

### 2. Negative Coordinate Handling

**Historical problem (already fixed in current source — see note above):** Integer division
behaves differently for negative numbers.

```cpp
// Potential issue with negative world coordinates
worldPos = (-1, 5, 3)
chunkCoord = worldPos / 32  // Results in (-1, 0, 0), not (0, 0, 0)
localPos = worldPos % 32    // Results in (31, 5, 3) due to modulo behavior
```

**Solution**: Use proper floor division for negative coordinates:

```cpp
static glm::ivec3 worldToChunkCoord(const glm::ivec3& worldPos) {
    return glm::ivec3(
        worldPos.x >= 0 ? worldPos.x / 32 : (worldPos.x - 31) / 32,
        worldPos.y >= 0 ? worldPos.y / 32 : (worldPos.y - 31) / 32,
        worldPos.z >= 0 ? worldPos.z / 32 : (worldPos.z - 31) / 32
    );
}
```

### 3. Bounds Checking

**Always validate local coordinates**:

```cpp
if (localPos.x >= 0 && localPos.x < 32 &&
    localPos.y >= 0 && localPos.y < 32 &&
    localPos.z >= 0 && localPos.z < 32) {
    // Safe to calculate index
    size_t index = ChunkManager::localToIndex(localPos);
}
```

---

## Debugging Coordinate Issues

### Debug Output Template

Use this template for coordinate debugging:

```cpp
std::cout << "[DEBUG] World pos: (" << worldPos.x << "," << worldPos.y << "," << worldPos.z << ")\n"
          << "        Chunk coord: (" << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z << ")\n"
          << "        Local pos: (" << localPos.x << "," << localPos.y << "," << localPos.z << ")\n"
          << "        Array index: " << index << "\n"
          << "        Chunk origin: (" << chunkOrigin.x << "," << chunkOrigin.y << "," << chunkOrigin.z << ")" << std::endl;
```

### Validation Functions

```cpp
// Validate index calculation consistency
bool validateIndexMapping(const glm::ivec3& localPos) {
    size_t calculatedIndex = ChunkManager::localToIndex(localPos);
    
    // Reverse calculation to verify
    int z = calculatedIndex % 32;
    int y = (calculatedIndex / 32) % 32;
    int x = calculatedIndex / (32 * 32);
    
    return (x == localPos.x && y == localPos.y && z == localPos.z);
}
```

---

## API Reference

### Core Coordinate Functions

#### `ChunkManager::worldToChunkCoord()`
- **Purpose**: Convert world position to chunk coordinate
- **Input**: World position (unlimited range)
- **Output**: Chunk coordinate (integer division by 32)
- **Usage**: Finding which chunk contains a world position

#### `ChunkManager::worldToLocalCoord()`
- **Purpose**: Convert world position to local position within chunk
- **Input**: World position
- **Output**: Local position (0-31 range)
- **Usage**: Finding position within a specific chunk

#### `Chunk::localToIndex()`
- **Purpose**: Convert 3D local position to 1D array index
- **Input**: Local position (0-31 range)
- **Output**: Array index (0-32767 range)
- **Formula**: `z + y*32 + x*32²`
- **Critical**: Must use Z-minor ordering (this is a static method on `Chunk`, not `ChunkManager` —
  corrected 2026-07-21; verified against `engine/include/core/Chunk.h`)

### Performance Characteristics

- **Coordinate Conversion**: O(1) - Simple arithmetic operations
- **Chunk Lookup**: O(1) - Hash map with spatial hashing
- **Cube Access**: O(1) - Direct array indexing
- **Total Lookup Time**: O(1) - End-to-end constant time

### Memory Usage

```
Single Cube: 12 bytes (3×vec3 color + position data)
Full Chunk: 393,216 bytes (32,768 cubes × 12 bytes)
Chunk Overhead: ~100 bytes (metadata, buffers)
```

---

## Performance Optimization Notes

### Cache Efficiency

The Z-minor indexing provides excellent cache locality:

- **Sequential Z access**: Optimal (stride = 1)
- **Sequential Y access**: Good (stride = 32)  
- **Sequential X access**: Poor (stride = 1024)

### DDA Algorithm Integration

The coordinate system is optimized for the DDA (Digital Differential Analyzer) ray casting algorithm:

- **Voxel traversal**: Uses integer coordinates matching chunk boundaries
- **Coordinate caching**: `CubeLocation` struct avoids repeated conversions
- **Cross-chunk support**: Seamless traversal across chunk boundaries

---

## Version History

- **v1.0**: Initial coordinate system implementation
- **v1.1**: Fixed X/Z axis flipping bug by correcting index formula
- **v1.2**: Added comprehensive documentation and validation functions

---

## Detailed: Relative-Coordinate Strategy & InstanceData Bit-Packing

> Merged from the former `CoordinateSystemDetailed.md`. Covers how positions are *stored*
> on the GPU (as opposed to the index math above, which is about CPU array layout).

### Relative-position strategy (memory optimization)

Instead of storing absolute world positions per face, the static pipeline stores
**chunk-relative coordinates** with only 5 bits per axis. There are three nested coordinate
spaces, each expressed relative to its parent:

| Space | Transform to parent | Storage |
|-------|---------------------|---------|
| **Chunk → World** | `worldPos = chunkOrigin + chunkRelativePos` | push constant (`chunkBaseOffset`, 12 B/chunk, shared by every face) |
| **Cube → Chunk** | `chunkRelativePos = vec3(x,y,z)`, x,y,z ∈ [0,31] | 15 bits (5+5+5) in `packedData` |
| **Sub/Microcube → Cube** | `cubeOffset = subcubePos*(1/3) + microcubePos*(1/9)` | bits in `packedData` |

Net effect: **15 bits of position per face instead of 96 bits** (3× float32), an ~87%
reduction, because the chunk origin is factored out into a single shared push constant.

### InstanceData layout (current — 24 bytes)

> ⚠️ Correction (2026-07-21, re-verified against `engine/include/core/Types.h`): earlier drafts of
> this material described `InstanceData` as 8, 16, then 20 bytes. The live struct is **24 bytes** —
> a `tint` field was added on top of the 20-byte (packedData/textureIndex/reserved/light×3) layout
> for the per-voxel tint + state work (`docs/VoxelAppearanceModel.md` Phase 1, shipped):

```cpp
struct InstanceData {
    uint32_t packedData;      // 4 bytes — packed cube pos + face + scale/subcube grid
    uint16_t textureIndex;    // 2 bytes
    uint16_t reserved;        // 2 bytes — flags: emissive/transparent/alpha/mirror/damage bits
    uint32_t light;           // 4 bytes — baked light field
    uint32_t light2;          // 4 bytes
    uint32_t light3;          // 4 bytes
    uint32_t tint;            // 4 bytes — bits 0-23: 0xRRGGBB tint multiplier; bits 24-31: state
}  // Total: 24 bytes
```
`static_voxel.vert` decodes `tint` as `inTint`: low 24 bits are the RGB multiplier, high 8 bits are
the per-voxel `state` (0=normal, 1=flaming, 2=smoldering, 3=charred, 4=wet) — confirmed in
`shaders/static_voxel.vert` lines ~29, 61-62, 365-371. There is **no separate tint-palette UBO/SSBO**;
despite `docs/VoxelAppearanceModel.md` §5 describing an 8-bit palette-index plan, what shipped is a
direct full-precision multiplier field (simpler, and it doubles as the state carrier).

### `packedData` bit field (decoded in `static_voxel.vert`)

```
Bits [0-14]:  Cube position (5+5+5 = 15 bits for a 32³ chunk)
Bits [15-17]: Face ID (3 bits for 6 faces)
Bits [18-19]: Scale level (0 = cube, 1 = subcube, 2 = microcube)
Bits [20-25]: Parent subcube grid position (6 bits = 2+2+2 for a 3³ grid)
Bits [26-31]: Microcube grid position (6 bits = 2+2+2 for a 3³ grid)
```

Shader reconstruction (sketch):

```glsl
uint chunkX = packedData & 0x1F;          // 0-31
uint chunkY = (packedData >> 5) & 0x1F;
uint chunkZ = (packedData >> 10) & 0x1F;
vec3 basePos = pushConstants.chunkBaseOffset + vec3(chunkX, chunkY, chunkZ);

uint scaleLevel = (packedData >> 18) & 0x3u;
float scale = 1.0 / pow(3.0, float(scaleLevel));   // 1.0, 0.333, 0.111
```

### Microcube two-level hierarchy (why subcube grid is relative to the cube, micro to the subcube)

Microcubes are 1/9 scale = a 9×9×9 grid within a parent cube, which would need 4 bits/axis
(12 bits) if stored as one flat grid — more than the bit budget allows. Instead the scheme
stores a microcube **relative to its parent subcube**:

- Parent subcube grid position → which of 27 subcubes (0-2 per axis, 6 bits).
- Microcube grid position → which of 27 microcubes *within that subcube* (0-2 per axis, 6 bits).

```glsl
if (scaleLevel == 2) {  // Microcube
    vec3 subcubeOffset  = vec3(parentSubX, parentSubY, parentSubZ) * (1.0/3.0);
    vec3 microcubeOffset = vec3(microX, microY, microZ) * (1.0/9.0);
    worldPos = basePos + subcubeOffset + microcubeOffset;
}
```

This keeps the cube/subcube/microcube hierarchy within `packedData` (no per-face size
increase from subdivision) while still giving 27 subcubes × 27 microcubes = **729 microcubes
per cube** — ample for sub-voxel detail.

## Indexing reference

> Merged from the former `IndexingReference.md`. The loop-order/stride material above is the
> canonical statement; this section adds the general derivation, a worked numeric example, and
> the bit-shift optimization not covered above.

### General 3D→1D derivation

For a 3D array with dimensions `(width=X, height=Y, depth=Z)` stored row-major with Z
innermost (Phyxel's X-major populate order):

```
General form: index = x*(height*depth) + y*depth + z
For 32³:      index = x*32*32 + y*32 + z = x*1024 + y*32 + z
```

i.e. the innermost loop variable (Z) gets coefficient 1; this is the Z-minor formula stated
above (`z + y*32 + x*1024`).

### Worked example (forward + reverse round-trip)

```cpp
glm::ivec3 pos(5, 10, 15);
size_t index = pos.z + pos.y*32 + pos.x*1024;   // 15 + 320 + 5120 = 5455

// Reverse:
int z = 5455 % 32;          // 15
int y = (5455 / 32) % 32;   // 170 % 32 = 10
int x = 5455 / 1024;        // 5
// → (5, 10, 15) ✓
```

### Bit-shift optimization

Because 32 = 2⁵ and 1024 = 2¹⁰, the multiplies can be replaced by shifts:

```cpp
size_t index = z + (y << 5) + (x << 10);   // equivalent to z + y*32 + x*1024
```

### Related files

> Paths corrected 2026-07-21 — `docs/` and `engine/` are sibling directories at repo root, so a
> `../include/...` link (as if `docs/` were inside `engine/`) 404s; the real prefix is `../engine/include/...`.

- [`Chunk.h`](../engine/include/core/Chunk.h) / [`Chunk.cpp`](../engine/src/core/Chunk.cpp) — `localToIndex`/`indexToLocal` (index calculation)
- [`ChunkVoxelStore.h`](../engine/include/core/ChunkVoxelStore.h) — palette-compressed static voxel storage (post chunk-storage-rewrite); same flat z-minor index
- [`ChunkManager.h`](../engine/include/core/ChunkManager.h) / [`ChunkManager.cpp`](../engine/src/core/ChunkManager.cpp) — `worldToChunkCoord`/`worldToLocalCoord`/`chunkCoordToOrigin` forwarders
- [`CoordinateUtils.h`](../engine/include/utils/CoordinateUtils.h) / [`CoordinateUtils.cpp`](../engine/src/utils/CoordinateUtils.cpp) — actual floor-division/modulo implementation
- [`Math.cpp`](../engine/src/utils/Math.cpp) — alternative indexing helpers
- [`Application.cpp`](../editor/src/Application.cpp) — coordinate debugging code (lives under `editor/`, not repo-root `src/`)

## See Also

- [DDA Algorithm Implementation](../editor/src/Application.cpp) - Ray casting code (path corrected 2026-07-21; line anchor not re-verified)
- [ChunkManager API](../engine/include/core/ChunkManager.h) - Core chunk management
