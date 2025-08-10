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

```cpp
// Convert world position to chunk coordinate
static glm::ivec3 worldToChunkCoord(const glm::ivec3& worldPos) { 
    return worldPos / 32; 
}

// Convert world position to local position within chunk
static glm::ivec3 worldToLocalCoord(const glm::ivec3& worldPos) { 
    return worldPos % 32; 
}

// Convert chunk coordinate to world origin
static glm::ivec3 chunkCoordToOrigin(const glm::ivec3& chunkCoord) { 
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

#### Cube Creation Loop Order (X-Major)

In `ChunkManager::populateChunk()`, cubes are created with **X-major ordering**:

```cpp
for (int x = 0; x < 32; ++x) {        // X changes SLOWEST (outermost loop)
    for (int y = 0; y < 32; ++y) {    // Y changes medium speed
        for (int z = 0; z < 32; ++z) { // Z changes FASTEST (innermost loop)
            // Cube creation...
        }
    }
}
```

**Memory Layout**: `[x=0,y=0,z=0], [x=0,y=0,z=1], [x=0,y=0,z=2], ..., [x=0,y=1,z=0], ...`

#### Index Calculation Formula (Must Match Loop Order)

The `localToIndex()` function **must match the loop order**:

```cpp
// CORRECT: Z-minor indexing (Z changes fastest)
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

**Problem**: Integer division behaves differently for negative numbers.

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

#### `ChunkManager::localToIndex()`
- **Purpose**: Convert 3D local position to 1D array index
- **Input**: Local position (0-31 range)
- **Output**: Array index (0-32767 range)
- **Formula**: `z + y*32 + x*32²`
- **Critical**: Must use Z-minor ordering to match loop order

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

## See Also

- [MultiChunkSystem.md](MultiChunkSystem.md) - Multi-chunk rendering architecture
- [DDA Algorithm Implementation](../src/Application.cpp#L1200) - Ray casting code
- [ChunkManager API](../include/core/ChunkManager.h) - Core chunk management
