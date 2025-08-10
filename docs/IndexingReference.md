# 3D Array Indexing and Memory Layout Reference

## Critical Implementation Details for 3D Voxel Arrays

This document contains the mathematical and implementation details for converting between 3D coordinates and 1D array indices in the Phyxel engine. **Understanding these formulas is essential to prevent axis flipping bugs.**

---

## Array Indexing Formulas

### Standard 3D to 1D Index Conversion

For a 3D array with dimensions `(width, height, depth)` stored as a 1D array:

```
General Formula: index = x*height*depth + y*depth + z

For 32×32×32 chunks: index = x*32*32 + y*32 + z
                           = x*1024 + y*32 + z
```

### Loop Order Determines Formula

**The index formula MUST match the loop order used to populate the array.**

#### X-Major Loop Order (Used in Phyxel)
```cpp
for (int x = 0; x < 32; ++x) {        // Outermost loop
    for (int y = 0; y < 32; ++y) {    // Middle loop  
        for (int z = 0; z < 32; ++z) { // Innermost loop
            array[index++] = createCube(x, y, z);
        }
    }
}
```

**Resulting Memory Layout:**
```
Index:  0    1    2    3   ...  31   32   33  ...
Coord: (0,0,0)(0,0,1)(0,0,2)(0,0,3)...(0,0,31)(0,1,0)(0,1,1)...
```

**Required Index Formula (Z-Minor):**
```cpp
index = z + y*32 + x*32*32  // Z changes fastest
```

#### Alternative: Z-Major Loop Order (NOT used in Phyxel)
```cpp
for (int z = 0; z < 32; ++z) {        // If Z were outermost
    for (int y = 0; y < 32; ++y) {    
        for (int x = 0; x < 32; ++x) { // If X were innermost
            array[index++] = createCube(x, y, z);
        }
    }
}
```

**Would require X-Minor formula:**
```cpp
index = x + y*32 + z*32*32  // X would change fastest
```

---

## Mathematical Verification

### Forward Calculation (3D → 1D)
```cpp
glm::ivec3 pos(5, 10, 15);  // Example position
size_t index = pos.z + pos.y * 32 + pos.x * 32 * 32;
             = 15 + 10 * 32 + 5 * 1024
             = 15 + 320 + 5120
             = 5455
```

### Reverse Calculation (1D → 3D)
```cpp
size_t index = 5455;
int z = index % 32;                    // z = 5455 % 32 = 15
int y = (index / 32) % 32;             // y = (5455/32) % 32 = 170 % 32 = 10  
int x = index / (32 * 32);             // x = 5455 / 1024 = 5
// Result: (5, 10, 15) ✓ Matches original
```

### Validation Function
```cpp
bool validateCoordinateMapping(const glm::ivec3& pos) {
    // Forward calculation
    size_t index = pos.z + pos.y * 32 + pos.x * 32 * 32;
    
    // Reverse calculation  
    int z = index % 32;
    int y = (index / 32) % 32;
    int x = index / (32 * 32);
    
    // Verify round-trip accuracy
    return (x == pos.x && y == pos.y && z == pos.z);
}
```

---

## Stride Analysis

### Memory Stride Values
```cpp
Z-stride = 1        // Adjacent Z elements are 1 index apart
Y-stride = 32       // Adjacent Y elements are 32 indices apart  
X-stride = 1024     // Adjacent X elements are 1024 indices apart
```

### Cache Performance Implications

**Best Cache Performance (Sequential Access):**
- Iterating through Z values: `stride = 1` (optimal)

**Good Cache Performance:**
- Iterating through Y values: `stride = 32` (acceptable)

**Poor Cache Performance:**
- Iterating through X values: `stride = 1024` (cache misses likely)

**Optimal Access Patterns:**
```cpp
// GOOD: Z-major traversal (cache-friendly)
for (int x = 0; x < 32; ++x) {
    for (int y = 0; y < 32; ++y) {
        for (int z = 0; z < 32; ++z) {
            accessCube(x, y, z);  // Sequential memory access
        }
    }
}

// BAD: X-major traversal (cache-unfriendly)  
for (int z = 0; z < 32; ++z) {
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            accessCube(x, y, z);  // Large stride access
        }
    }
}
```

---

## Common Bug: Formula-Loop Mismatch

### The X/Z Axis Flipping Bug

This bug occurs when the index formula doesn't match the loop order:

**Incorrect Implementation:**
```cpp
// Loop order: X-major (X outermost, Z innermost)
for (int x = 0; x < 32; ++x) {
    for (int y = 0; y < 32; ++y) {
        for (int z = 0; z < 32; ++z) {
            cubes.push_back(Cube(x, y, z));
        }
    }
}

// WRONG: X-minor formula with X-major data
size_t index = x + y*32 + z*32*32;  // Assumes X changes fastest
```

**Result:** 
- Cube at logical position (1,0,0) gets stored at array index 1
- Cube at logical position (0,0,1) gets stored at array index 1024
- When accessing index 1 expecting (1,0,0), you get (0,0,1) instead
- **Visual effect**: X and Z axes appear swapped

**Correct Implementation:**
```cpp
// Same loop order: X-major
for (int x = 0; x < 32; ++x) {
    for (int y = 0; y < 32; ++y) {
        for (int z = 0; z < 32; ++z) {
            cubes.push_back(Cube(x, y, z));
        }
    }
}

// CORRECT: Z-minor formula matches X-major data  
size_t index = z + y*32 + x*32*32;  // Z changes fastest, matches innermost loop
```

---

## Debugging Techniques

### Index Validation Test
```cpp
void testIndexMapping() {
    std::cout << "Testing coordinate-to-index mapping...\n";
    
    // Test corner cases
    std::vector<glm::ivec3> testCases = {
        {0, 0, 0},     // Origin
        {31, 31, 31},  // Max corner
        {0, 0, 31},    // Z-max edge
        {31, 0, 0},    // X-max edge
        {0, 31, 0},    // Y-max edge
        {15, 15, 15}   // Center
    };
    
    for (const auto& pos : testCases) {
        size_t index = ChunkManager::localToIndex(pos);
        
        // Reverse calculation
        int z = index % 32;
        int y = (index / 32) % 32;
        int x = index / (32 * 32);
        
        bool valid = (x == pos.x && y == pos.y && z == pos.z);
        
        std::cout << "Pos(" << pos.x << "," << pos.y << "," << pos.z << ") "
                  << "→ Index " << index << " "
                  << "→ Pos(" << x << "," << y << "," << z << ") "
                  << (valid ? "✓" : "✗") << "\n";
    }
}
```

### Memory Layout Visualization
```cpp
void visualizeMemoryLayout() {
    std::cout << "First 100 array indices and their coordinates:\n";
    for (size_t i = 0; i < 100; ++i) {
        int z = i % 32;
        int y = (i / 32) % 32;
        int x = i / (32 * 32);
        std::cout << "Index " << std::setw(2) << i 
                  << " → (" << x << "," << y << "," << z << ")\n";
    }
}
```

---

## Implementation Checklist

When implementing 3D array indexing:

- [ ] **Document the loop order** used to populate the array
- [ ] **Match the index formula** to the loop order  
- [ ] **Test with corner cases** (0,0,0), (max,max,max), edges
- [ ] **Validate round-trip accuracy** (3D→1D→3D should be identity)
- [ ] **Consider cache performance** for access patterns
- [ ] **Add bounds checking** for array access
- [ ] **Document the coordinate system** (right-handed, axis directions)

---

## Performance Notes

### Index Calculation Cost
```cpp
// Z-minor indexing: 2 multiplications + 2 additions
size_t index = z + y*32 + x*1024;

// Can be optimized with bit shifts for powers of 2:
size_t index = z + (y << 5) + (x << 10);  // 32 = 2^5, 1024 = 2^10
```

### Memory Access Patterns
- **Best**: Sequential Z access (unit stride)
- **Good**: Block Y access (stride 32)
- **Worst**: Scattered X access (stride 1024)

---

## Related Files

- [`ChunkManager.h`](../include/core/ChunkManager.h): Index calculation implementation
- [`ChunkManager.cpp`](../src/core/ChunkManager.cpp): Chunk population loops
- [`Math.cpp`](../src/utils/Math.cpp): Alternative indexing functions
- [`Application.cpp`](../src/Application.cpp): Coordinate debugging code
