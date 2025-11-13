# Coordinate System - Detailed Analysis

## Current System Architecture

### Relative Position Strategy (Memory Optimization)

**Key Insight:** Instead of storing absolute world positions, the system uses **chunk-relative coordinates** with only 5 bits per axis.

### Three-Level Coordinate System

#### Level 1: Chunk Coordinates (Absolute World Space)
```cpp
glm::ivec3 worldOrigin;  // Chunk's (0,0,0) corner in world space
// Example: worldOrigin = (64, 0, 32)
// This chunk covers world positions [64-95, 0-31, 32-63]
```

#### Level 2: Cube Coordinates (Chunk-Relative)
```cpp
// Stored in InstanceData.packedData bits [0-14]
uint chunkX = packedData & 0x1F;           // 5 bits = 0-31 range
uint chunkY = (packedData >> 5) & 0x1F;    // 5 bits = 0-31 range  
uint chunkZ = (packedData >> 10) & 0x1F;   // 5 bits = 0-31 range
```

**Shader reconstruction:**
```glsl
vec3 chunkRelativePos = vec3(float(chunkX), float(chunkY), float(chunkZ));
vec3 basePos = pushConstants.chunkBaseOffset + chunkRelativePos;
// Result: absolute world position of cube
```

#### Level 3: Subcube Coordinates (Cube-Relative)
```cpp
// Stored in InstanceData.packedData bits [19-24]
uint subcubeLocalX = (subcubeData >> 1) & 0x3u;  // 2 bits = 0-2 range
uint subcubeLocalY = (subcubeData >> 3) & 0x3u;  // 2 bits = 0-2 range
uint subcubeLocalZ = (subcubeData >> 5) & 0x3u;  // 2 bits = 0-2 range
```

**Shader reconstruction:**
```glsl
const float SUBCUBE_SCALE = 1.0 / 3.0;
vec3 subcubeOffset = vec3(subcubeLocalX, subcubeLocalY, subcubeLocalZ) * SUBCUBE_SCALE;
vec3 worldPos = basePos + subcubeOffset;
// Result: absolute world position of subcube within parent cube
```

## Memory Bandwidth Analysis

### Static Pipeline Bandwidth

**Per Face Data:**
```cpp
struct InstanceData {
    uint32_t packedData;      // 4 bytes
    uint16_t textureIndex;    // 2 bytes
    uint16_t reserved;        // 2 bytes
}  // Total: 8 bytes
```

**Draw call for 10,000 visible faces:**
- Instance data: 10,000 × 8 bytes = **80 KB**
- Push constants: 12 bytes (vec3)
- Total GPU upload: **~80 KB**

**Why this is efficient:**
1. **Position compression**: 15 bits instead of 96 bits (3 × float32)
2. **Relative coordinates**: Push constant shared across all faces in chunk
3. **No redundant data**: Each face stored once

### Alternative: Absolute World Coordinates

If we stored absolute positions:
```cpp
struct InstanceDataAbsolute {
    vec3 worldPosition;       // 12 bytes (3 × float32)
    uint32_t faceID;          // 4 bytes
    uint16_t textureIndex;    // 2 bytes
    uint16_t reserved;        // 2 bytes
}  // Total: 20 bytes (2.5× larger!)
```

**Same 10,000 faces:**
- Instance data: 10,000 × 20 bytes = **200 KB** (2.5× more bandwidth)

## Extending to Microcubes

### Challenge: Bit Budget Exhausted

Current packing for subcubes:
```
Bits [0-14]:  Cube position (5+5+5 = 15 bits for 32³ chunk)
Bits [15-17]: Face ID (3 bits for 6 faces)
Bits [18]:    Subcube flag (1 bit)
Bits [19-24]: Subcube local position (6 bits for 3³ grid = 2+2+2)
Bits [25-31]: Reserved (7 bits)
```

**Microcube requirements:**
- Microcubes = 1/9 scale = 9×9×9 grid within parent cube
- Need 4 bits per axis (9 values = 0-8)
- Total: 4+4+4 = **12 bits** for microcube position

**Problem:** Already using 24 bits, only 7 bits reserved, need 12 bits!

### Solution Options

#### Option 1: Two-Level Hierarchy (Recommended)
Store microcube relative to its parent **subcube**, not parent cube:

```
Bits [0-14]:  Cube position (5+5+5)
Bits [15-17]: Face ID (3 bits)
Bits [18-19]: Scale level (2 bits: 0=cube, 1=subcube, 2=microcube)
Bits [20-25]: Parent subcube position (6 bits = 2+2+2 for 3³ grid)
Bits [26-31]: Microcube local position (6 bits = 2+2+2 for 3³ grid)
```

**Logic:**
- Cube position → points to parent cube (unchanged)
- Parent subcube position → which of 27 subcubes (0-2 per axis)
- Microcube local position → which of 27 microcubes within that subcube (0-2 per axis)

**Shader reconstruction:**
```glsl
uint scaleLevel = (packedData >> 18) & 0x3u;
float scale = 1.0 / pow(3.0, float(scaleLevel));  // 1.0, 0.333, 0.111

if (scaleLevel == 2) {  // Microcube
    // Extract parent subcube position
    uint parentSubX = (packedData >> 20) & 0x3u;
    uint parentSubY = (packedData >> 22) & 0x3u;
    uint parentSubZ = (packedData >> 24) & 0x3u;
    
    // Extract microcube position within subcube
    uint microX = (packedData >> 26) & 0x3u;
    uint microY = (packedData >> 28) & 0x3u;
    uint microZ = (packedData >> 30) & 0x3u;
    
    // Calculate offsets
    vec3 subcubeOffset = vec3(parentSubX, parentSubY, parentSubZ) * (1.0/3.0);
    vec3 microcubeOffset = vec3(microX, microY, microZ) * (1.0/9.0);
    
    worldPos = basePos + subcubeOffset + microcubeOffset;
}
```

**Cost:** Still 8 bytes per face!

#### Option 2: Increase Instance Data Size

```cpp
struct InstanceDataExtended {
    uint32_t packedData;      // 4 bytes - cube position + face ID
    uint32_t hierarchyData;   // 4 bytes - subcube + microcube positions
    uint16_t textureIndex;    // 2 bytes
    uint16_t reserved;        // 2 bytes
}  // Total: 12 bytes (1.5× current size)
```

**Hierarchy data layout:**
```
Bits [0-1]:   Scale level (2 bits)
Bits [2-7]:   Subcube position (6 bits = 2+2+2)
Bits [8-13]:  Microcube position (6 bits = 2+2+2)
Bits [14-31]: Reserved for future subdivision levels
```

**Bandwidth impact:**
- 10,000 faces × 12 bytes = **120 KB** (vs current 80 KB)
- Still better than absolute coordinates (200 KB)
- **+50% bandwidth cost** for microcube support

## Recommendation: Option 1 (Two-Level Hierarchy)

**Rationale:**
1. **Maintains 8-byte size** - no bandwidth increase
2. **Uses all 32 bits efficiently** - minimal waste
3. **Supports 3-level hierarchy** - cube → subcube → microcube
4. **Easy to extend** - just check scale level bits

**Trade-off:**
- Slightly more complex shader logic (2 offset calculations)
- Limits microcubes to 3×3×3 grid per subcube (not 9×9×9 per cube)

**Why this is acceptable:**
- Microcubes are still 1/9 scale relative to parent cube
- Total microcubes per cube: 27 subcubes × 27 microcubes = **729 microcubes**
- More than enough detail for voxel art and smooth transitions

## Performance Impact Assessment

### Current System (Cubes + Subcubes)
- **8 bytes/face**
- 10,000 visible faces = 80 KB/chunk
- Push constant: 12 bytes

### With Microcubes (Option 1)
- **8 bytes/face** (unchanged!)
- Slightly more complex shader math (2 multiplications + 2 additions)
- No bandwidth increase

### Shader Performance
Modern GPUs handle this easily:
- **ALU cost**: ~4 extra instructions per vertex
- **Latency hiding**: Texture fetches dominate shader time anyway
- **Minimal impact**: < 1% performance difference expected

## Coordinate System Summary

**Three coordinate spaces, each relative to its parent:**

1. **Chunk Space** → World Space
   - Transform: `worldPos = chunkOrigin + chunkRelativePos`
   - Storage: Push constant (12 bytes per chunk)

2. **Cube Space** → Chunk Space  
   - Transform: `chunkRelativePos = vec3(x, y, z)` where x,y,z ∈ [0,31]
   - Storage: 15 bits (5+5+5)

3. **Subcube/Microcube Space** → Cube Space
   - Transform: `cubeOffset = subcubePos*(1/3) + microcubePos*(1/9)`
   - Storage: 12 bits (6+6)

**Total position storage per face: 15 bits (vs 96 bits for absolute coords)**

**Savings: 87% reduction in position data!**
