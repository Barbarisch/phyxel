# Multi-Chunk Rendering System

## Overview

The multi-chunk rendering system allows you to easily scale from rendering a single 32x32x32 chunk to 10, 50, or even 100+ chunks while maintaining optimal performance. Each chunk is rendered with its own Vulkan buffer and draw call.

## Key Features

- **Scalable**: Define 10 chunks or 50 chunks with equal ease
- **Efficient**: Each chunk has its own Vulkan buffer and draw call
- **Compatible**: Maintains your existing 5-bit shader design (relative positions 0-31)
- **Flexible**: Easy world-space positioning via chunk origins

## Architecture

### Chunk Structure
```cpp
struct Chunk {
    std::vector<InstanceData> cubes;          // 32x32x32 cubes with relative positions
    VkBuffer instanceBuffer;                  // Vulkan buffer for this chunk
    VkDeviceMemory instanceMemory;
    void* mappedMemory;                       // For easy updates
    uint32_t numInstances = 32 * 32 * 32;   // Always 32768
    glm::ivec3 worldOrigin;                  // World-space origin
    bool needsUpdate;                        // Update flag
};
```

### ChunkManager
- Manages all chunks in the world
- Handles Vulkan buffer creation and memory management
- Provides utilities for chunk creation and updates

## Usage Examples

### Creating 10 Linear Chunks
```cpp
// Initialize chunk manager
chunkManager->initialize(device, physicalDevice);

// Create 10 chunks in a line
auto origins = MultiChunkDemo::createLinearChunks(10);
chunkManager->createChunks(origins);
```

### Creating a 5x5 Grid (25 chunks)
```cpp
auto gridOrigins = MultiChunkDemo::createGridChunks(5, 5);
chunkManager->createChunks(gridOrigins);
```

### Creating a 3D Grid 3x3x3 (27 chunks)
```cpp
auto grid3DOrigins = MultiChunkDemo::create3DGridChunks(3, 3, 3);
chunkManager->createChunks(grid3DOrigins);
```

## Rendering Integration

### Basic Render Loop
```cpp
void renderChunks(VkCommandBuffer commandBuffer) {
    // Bind vertex buffer once
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    
    // Render each chunk
    for (const Chunk& chunk : chunkManager->chunks) {
        // Bind chunk's instance buffer
        vkCmdBindVertexBuffers(commandBuffer, 1, 1, &chunk.instanceBuffer, &offset);
        
        // Update push constants with chunk origin
        PushConstants pc{chunk.worldOrigin};
        vkCmdPushConstants(commandBuffer, pipelineLayout, 
                          VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
        
        // Draw chunk (32,768 instances)
        vkCmdDraw(commandBuffer, 8, chunk.numInstances, 0, 0);
    }
}
```

### Shader Updates (cube.vert)
```glsl
layout(push_constant) uniform PushConstants {
    ivec3 chunkOrigin;  // World-space chunk origin
} pc;

void main() {
    // Extract relative position (0-31) from packed data
    uint relativeX = packedData & 0x1F;
    uint relativeY = (packedData >> 5) & 0x1F;
    uint relativeZ = (packedData >> 10) & 0x1F;
    
    // Convert to world position
    vec3 worldPos = vec3(relativeX, relativeY, relativeZ) + vec3(pc.chunkOrigin);
    
    // Continue with existing logic...
}
```

## Performance Characteristics

| Chunks | Total Cubes | Memory Usage | Draw Calls |
|--------|-------------|--------------|------------|
| 1      | 32,768      | ~0.5 MB      | 1          |
| 10     | 327,680     | ~5 MB        | 10         |
| 25     | 819,200     | ~12.5 MB     | 25         |
| 50     | 1,638,400   | ~25 MB       | 50         |
| 100    | 3,276,800   | ~50 MB       | 100        |

## Memory Layout

Each chunk uses approximately 0.5 MB:
- 32,768 instances × 16 bytes per instance = 524,288 bytes
- Plus Vulkan buffer overhead

## Integration Steps

1. **Add ChunkManager to Application**
   ```cpp
   std::unique_ptr<ChunkManager> chunkManager;
   ```

2. **Initialize in setup**
   ```cpp
   chunkManager = std::make_unique<ChunkManager>();
   chunkManager->initialize(device, physicalDevice);
   ```

3. **Create chunks**
   ```cpp
   auto origins = MultiChunkDemo::createLinearChunks(10);
   chunkManager->createChunks(origins);
   ```

4. **Update render loop**
   - Loop through chunks
   - Bind each chunk's instance buffer
   - Issue draw call per chunk

5. **Update shader**
   - Add push constant for chunk origin
   - Add chunk origin to relative position

## Benefits

- **Easy Scaling**: Change from 10 to 50 chunks by changing one number
- **Performance**: Each chunk rendered optimally with dedicated buffer
- **Compatibility**: No changes needed to existing packing logic
- **Flexibility**: Chunks can be positioned anywhere in world space
- **Memory Efficiency**: Only allocate memory for chunks you need

## Next Steps

- Implement frustum culling per chunk
- Add chunk streaming for infinite worlds
- Implement LOD system for distant chunks
- Add chunk modification utilities
