# Dynamic Subcube Render Pipeline

## Overview

The Phyxel engine implements a **dual-pipeline architecture** for rendering subcubes, optimizing for both memory efficiency and physics integration. This system allows for both grid-aligned static subcubes and physics-enabled dynamic subcubes to coexist and render efficiently.

## Architecture

### Dual Pipeline Design

The engine uses two specialized rendering pipelines:

1. **Static Pipeline**: Handles grid-aligned subcubes using compressed data structures
2. **Dynamic Pipeline**: Handles physics-enabled subcubes with full-precision positioning

```
┌─────────────────┐    ┌─────────────────┐
│  Static Cubes   │    │ Dynamic Subcubes│
│ & Static        │    │ (Physics-based) │
│ Subcubes        │    │                 │
└─────────────────┘    └─────────────────┘
         │                       │
         ▼                       ▼
┌─────────────────┐    ┌─────────────────┐
│ Static Pipeline │    │Dynamic Pipeline │
│ (cube.vert)     │    │(dynamic_subcube │
│                 │    │    .vert)       │
└─────────────────┘    └─────────────────┘
         │                       │
         └───────────┬───────────┘
                     ▼
            ┌─────────────────┐
            │   Final Render  │
            │                 │
            └─────────────────┘
```

## Data Structures

### Static Subcubes - InstanceData (16 bytes)

```cpp
struct InstanceData {
    uint32_t packedData;   // 15 bits position (5+5+5), 6 bits face mask, 11 bits future features
    glm::vec3 color;       // RGB color (12 bytes)
    // Total: 16 bytes (43% size reduction from original 28 bytes)
};
```

**Bit Layout in packedData:**
- Bits 0-4: X position (5 bits, 0-31 range)
- Bits 5-9: Y position (5 bits, 0-31 range) 
- Bits 10-14: Z position (5 bits, 0-31 range)
- Bits 15-20: Face mask (6 bits, one per face)
- Bits 21-31: Available for future features (11 bits)

### Dynamic Subcubes - DynamicSubcubeInstanceData (32 bytes)

```cpp
struct DynamicSubcubeInstanceData {
    glm::vec3 worldPosition;  // Full precision world position for physics objects
    glm::vec3 color;          // RGB color
    uint32_t faceID;          // Face ID (0-5)
    float scale;              // Scale factor (1/3 for subcubes, 1.0 for full cubes)
    // Total: 32 bytes - larger but allows arbitrary positioning with correct scale
};
```

## Shader Implementation

### Dynamic Subcube Vertex Shader (`dynamic_subcube.vert`)

**Input Attributes:**
```glsl
layout(location = 0) in uint vertexID;          // Face corner ID (0–3 for quad corners)
layout(location = 1) in vec3 inWorldPosition;   // Per-instance: world position of subcube
layout(location = 2) in vec3 inInstanceColor;   // Per-instance color
layout(location = 3) in uint inFaceID;          // Per-instance: face ID (0-5)
layout(location = 4) in float inScale;          // Per-instance: scale factor
```

**Key Features:**
- **Procedural face generation**: Creates cube faces based on `faceID` and `vertexID`
- **Dynamic scaling**: Uses `inScale` for proper subcube sizing (1/3 for subcubes)
- **Physics positioning**: Uses actual physics position from `inWorldPosition`
- **Centered positioning**: `(faceOffset - 0.5) * scale` centers faces around world position

## Management System

### ChunkManager Integration

**Global Dynamic Subcube Storage:**
```cpp
class ChunkManager {
    std::vector<std::unique_ptr<Subcube>> globalDynamicSubcubes;
    std::vector<DynamicSubcubeInstanceData> globalDynamicSubcubeFaces;
};
```

**Key Methods:**
- `addGlobalDynamicSubcube()`: Adds new physics-enabled subcube
- `updateGlobalDynamicSubcubePositions()`: Syncs positions from Bullet Physics
- `rebuildGlobalDynamicSubcubeFaces()`: Generates render instances
- `updateGlobalDynamicSubcubes()`: Handles lifecycle and cleanup

### Rendering Flow

```cpp
void Application::renderDynamicSubcubes() {
    // 1. Get all dynamic subcube faces from ChunkManager
    const auto& faces = chunkManager->getGlobalDynamicSubcubeFaces();
    
    // 2. Update GPU buffer with latest physics positions
    vulkanDevice->updateDynamicSubcubeBuffer(faces);
    
    // 3. Bind dynamic render pipeline
    vkCmdBindPipeline(..., dynamicRenderPipeline->getGraphicsPipeline());
    
    // 4. Bind vertex and dynamic instance buffers
    vulkanDevice->bindDynamicSubcubeBuffer(currentFrame);
    vulkanDevice->bindIndexBuffer(currentFrame);
    
    // 5. Bind descriptor sets
    vulkanDevice->bindDescriptorSets(currentFrame, dynamicRenderPipeline->getGraphicsLayout());
    
    // 6. Draw all dynamic subcubes with instanced rendering
    vulkanDevice->drawIndexed(currentFrame, 6, faces.size());
}
```

## Physics Integration

### Position Synchronization

The system maintains smooth physics integration through continuous position updates:

```cpp
void ChunkManager::updateGlobalDynamicSubcubePositions() {
    for (auto& subcube : globalDynamicSubcubes) {
        if (subcube && subcube->getRigidBody()) {
            btRigidBody* body = subcube->getRigidBody();
            btTransform transform = body->getWorldTransform();
            
            // Get physics world position
            btVector3 btPos = transform.getOrigin();
            glm::vec3 newWorldPos(btPos.x(), btPos.y(), btPos.z());
            
            // Store smooth floating-point physics position
            subcube->setPhysicsPosition(newWorldPos);
        }
    }
    
    // Rebuild face data for rendering
    rebuildGlobalDynamicSubcubeFaces();
}
```

### Lifecycle Management

**Automatic Cleanup:**
- Time-based expiration system for temporary subcubes
- Automatic rigid body removal from physics world
- RAII memory management with unique_ptr

## Performance Characteristics

### Memory Efficiency

| Component | Static Subcubes | Dynamic Subcubes |
|-----------|----------------|------------------|
| Instance Data Size | 16 bytes | 32 bytes |
| Memory Reduction | 43% vs original | Full precision |
| Positioning | Grid-aligned | Arbitrary |
| Use Case | Subdivision | Physics objects |

### Rendering Performance

- **Separate pipelines**: Optimized shaders for each use case
- **Instanced rendering**: Single draw call per subcube type
- **Buffer management**: Fixed-capacity buffers with reallocation support
- **Face culling**: Optimized for static (neighbor culling) vs dynamic (all faces)

### Scalability

- **Static subcubes**: Limited by chunk grid (32³ per chunk)
- **Dynamic subcubes**: Unlimited global capacity (configurable buffer size)
- **Mixed rendering**: Both types render simultaneously without interference

## Usage Scenarios

### Static Subcubes
- **Cube subdivision**: Breaking cubes into 27 smaller pieces for detail
- **LOD systems**: Multiple detail levels within chunks
- **Grid-aligned structures**: Buildings, terrain features

### Dynamic Subcubes
- **Destruction debris**: Broken pieces that fall with physics
- **Projectiles**: Physics-based moving objects
- **Particle systems**: Small physics objects with lifetimes
- **Interactive objects**: Physics-enabled game elements

## Implementation Example

### Creating Dynamic Subcubes

```cpp
// Create a dynamic subcube with physics
auto dynamicSubcube = std::make_unique<Subcube>(
    worldPosition,           // World position
    glm::vec3(1.0f, 0.0f, 0.0f), // Red color
    glm::ivec3(1, 1, 1)     // Local position within parent
);

// Enable physics
dynamicSubcube->setDynamic(true);
dynamicSubcube->setLifetime(5.0f); // 5 second lifetime

// Add to global management
chunkManager->addGlobalDynamicSubcube(std::move(dynamicSubcube));
```

### Rendering Integration

```cpp
void Application::drawFrame() {
    // ... other rendering ...
    
    // Update physics positions
    chunkManager->updateGlobalDynamicSubcubePositions();
    
    // Render static content first
    renderStaticCubesAndSubcubes();
    
    // Render dynamic subcubes second
    renderDynamicSubcubes();
    
    // ... cleanup ...
}
```

## Technical Benefits

1. **Performance**: Separate optimized pipelines for different use cases
2. **Flexibility**: Dynamic subcubes support arbitrary positions and scales
3. **Physics Integration**: Seamless integration with Bullet Physics world
4. **Memory Efficiency**: Compressed data for static subcubes where precision isn't needed
5. **Scalability**: Global management allows unlimited dynamic subcubes
6. **Maintainability**: Clear separation of concerns between static and dynamic rendering

## Future Enhancements

### Planned Features
- **LOD system**: Automatic detail level switching based on distance
- **Culling optimizations**: Frustum and occlusion culling for dynamic subcubes
- **Instanced physics**: Batch physics updates for better performance
- **Temporal coherence**: Position interpolation for smoother rendering

### Available Bits in InstanceData
The static pipeline has 11 unused bits (21-31) in packedData for future features:
- Material IDs (3 bits = 8 materials)
- Animation state (2 bits = 4 states)
- Damage levels (2 bits = 4 levels)
- Special flags (4 bits for various features)

## See Also

- [CoordinateSystem.md](CoordinateSystem.md) - Understanding world vs local coordinates
- [MultiChunkSystem.md](MultiChunkSystem.md) - Chunk management overview
- [ChunkUpdateOptimization.md](ChunkUpdateOptimization.md) - Performance optimizations
