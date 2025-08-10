# Phyxel Coordinate System Quick Reference

## ⚠️ CRITICAL FORMULA

**For X-major loop order (X outermost, Z innermost):**

```cpp
// CORRECT index formula (Z-minor):
size_t index = z + y*32 + x*32*32;

// WRONG index formula (would cause X/Z axis flip):
size_t index = x + y*32 + z*32*32;
```

## Loop Order vs Index Formula

| Loop Order | Innermost Loop | Index Formula | Name |
|------------|----------------|---------------|------|
| `for(x) for(y) for(z)` | Z (fastest) | `z + y*32 + x*1024` | Z-minor |
| `for(z) for(y) for(x)` | X (fastest) | `x + y*32 + z*1024` | X-minor |

**Rule: The innermost loop variable gets coefficient 1 in the index formula.**

## Coordinate Conversions

```cpp
// World to chunk coordinate
glm::ivec3 chunkCoord = worldPos / 32;

// World to local coordinate  
glm::ivec3 localPos = worldPos % 32;

// Local to array index
size_t index = localPos.z + localPos.y*32 + localPos.x*1024;
```

## Validation Check

```cpp
// Verify index mapping works correctly:
bool isValid = validateIndexMapping(localPos);

bool validateIndexMapping(const glm::ivec3& pos) {
    size_t idx = pos.z + pos.y*32 + pos.x*1024;
    int z = idx % 32;
    int y = (idx/32) % 32; 
    int x = idx / 1024;
    return (x==pos.x && y==pos.y && z==pos.z);
}
```

## Debug Template

```cpp
std::cout << "World: (" << worldPos.x << "," << worldPos.y << "," << worldPos.z << ")\n"
          << "Local: (" << localPos.x << "," << localPos.y << "," << localPos.z << ")\n"  
          << "Index: " << index << std::endl;
```

## Common Mistakes

- ❌ Using X-minor formula with X-major loop order
- ❌ Forgetting to validate negative coordinates  
- ❌ Missing bounds check on local coordinates
- ❌ Assuming index formula without checking loop order

## Memory Stride

- Z-stride: 1 (best cache performance)
- Y-stride: 32 
- X-stride: 1024 (worst cache performance)
