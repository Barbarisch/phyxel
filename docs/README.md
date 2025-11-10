# Phyxel Engine Documentation Index

## Overview

Phyxel is a high-performance 3D voxel rendering engine built with Vulkan API, featuring dual-pipeline rendering, physics integration, and scalable multi-chunk world management.

## Core Documentation

### Rendering Systems
- **[DynamicSubcubeRenderPipeline.md](DynamicSubcubeRenderPipeline.md)** - Complete guide to the dual-pipeline architecture
  - Static subcube rendering (16-byte instances, grid-aligned)
  - Dynamic subcube rendering (32-byte instances, physics-enabled)
  - Shader implementation and buffer management
  - Performance characteristics and usage scenarios

### World Management
- **[MultiChunkSystem.md](MultiChunkSystem.md)** - Scalable chunk-based world architecture
  - Chunk organization and coordinate systems
  - Cross-chunk occlusion culling
  - Performance optimization strategies
  - Memory management and scalability

- **[CoordinateSystem.md](CoordinateSystem.md)** - Understanding coordinate transformations
  - World vs chunk vs local coordinates
  - Conversion formulas and algorithms
  - Edge cases and boundary handling

### Performance Optimization
- **[ChunkUpdateOptimization.md](ChunkUpdateOptimization.md)** - Engine optimization strategies
  - Face culling algorithms
  - Instance data management
  - GPU buffer optimization
  - Performance profiling results

## Quick References

### Technical References
- **[CoordinateQuickRef.md](CoordinateQuickRef.md)** - Quick lookup for coordinate conversions
- **[IndexingReference.md](IndexingReference.md)** - Array indexing patterns and formulas
- **[LoggingReference.md](LoggingReference.md)** - Logging usage guide and module reference

### Development Tools
- **[LoggingSystem.md](LoggingSystem.md)** - Comprehensive logging system documentation
  - Configuration and usage guide
  - Log levels and module control
  - Best practices and examples
  
- **[LOGGING_SUMMARY.md](LOGGING_SUMMARY.md)** - Migration status and quick start
  - Implementation summary
  - All migrated files (19 total, ~250+ statements)
  - Common usage patterns

### Code Organization & Refactoring 🔧 NEW
- **[ArchitectureOverview.md](ArchitectureOverview.md)** - ⭐ **START HERE** for refactoring
  - Visual architecture diagrams (before/after)
  - Quick-start guides for first refactoring
  - Incremental refactoring workflow
  
- **[CodebaseRefactoringAnalysis.md](CodebaseRefactoringAnalysis.md)** - Comprehensive refactoring plan
  - Analysis of 4 largest files (Application, Chunk, VulkanDevice, ChunkManager)
  - Specific module extraction recommendations
  - 6-phase refactoring roadmap
  - Benefits for AI-assisted development
  
- **[RefactoringExamples.md](RefactoringExamples.md)** - Practical code examples
  - Complete code templates for new modules
  - Migration checklists
  - Testing strategies
  - Common pitfalls and solutions

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    Phyxel Engine                            │
├─────────────────────────────────────────────────────────────┤
│  Application Layer                                          │
│  ├── Input Handling (Mouse/Keyboard)                       │
│  ├── Frame Timing & Performance Profiling                  │
│  └── Main Render Loop Coordination                         │
├─────────────────────────────────────────────────────────────┤
│  Rendering System                                           │
│  ├── Static Pipeline (Grid-aligned cubes & subcubes)       │
│  ├── Dynamic Pipeline (Physics-enabled subcubes)           │
│  ├── VulkanDevice (Buffer management & GPU resources)      │
│  └── RenderPipeline (Shader programs & pipeline state)     │
├─────────────────────────────────────────────────────────────┤
│  World Management                                           │
│  ├── ChunkManager (Global chunk coordination)              │
│  ├── Chunk (32³ cube containers)                          │
│  ├── Cube/Subcube (Individual voxel objects)              │
│  └── Global Dynamic Subcubes (Physics objects)            │
├─────────────────────────────────────────────────────────────┤
│  Physics Integration                                        │
│  ├── PhysicsWorld (Bullet Physics wrapper)                │
│  ├── Rigid Body Management                                 │
│  └── Position Synchronization                              │
├─────────────────────────────────────────────────────────────┤
│  Scene Management                                           │
│  ├── SceneManager (Instance data generation)               │
│  ├── Camera System (View/Projection matrices)              │
│  └── Ray Picking (Mouse interaction)                       │
└─────────────────────────────────────────────────────────────┘
```

## Key Features

### Dual-Pipeline Rendering
- **Static Pipeline**: Optimized for grid-aligned objects with compressed 16-byte instances
- **Dynamic Pipeline**: Full-precision 32-byte instances for physics objects
- **Seamless integration**: Both pipelines render in the same frame

### Physics Integration
- **Bullet Physics**: Industry-standard physics simulation
- **Smooth positioning**: Continuous position updates from physics world
- **Lifecycle management**: Automatic cleanup of expired objects

### Scalable Architecture
- **Multi-chunk system**: Unlimited world size with O(1) chunk lookup
- **Cross-chunk culling**: Efficient face culling across chunk boundaries
- **Performance optimization**: 40+ FPS with 32,000+ cubes

### Interactive Features
- **Ray picking**: Precise mouse-cube intersection
- **Hover highlighting**: Real-time color changes
- **Camera controls**: WASD movement with mouse look

## Getting Started

1. **Read the core documentation** in order:
   - Start with [CoordinateSystem.md](CoordinateSystem.md) for fundamentals
   - Continue with [MultiChunkSystem.md](MultiChunkSystem.md) for world management
   - Then [DynamicSubcubeRenderPipeline.md](DynamicSubcubeRenderPipeline.md) for rendering

2. **Build and run** the engine:
   - Follow instructions in main [README.md](../README.md)
   - Experiment with interactive controls
   - Monitor performance metrics

3. **Explore the code**:
   - Start with `src/Application.cpp` for the main loop
   - Examine `src/core/ChunkManager.cpp` for world management
   - Study `shaders/` for rendering implementation

## Development Guidelines

### Adding New Features
- **Rendering effects**: Modify `RenderPipeline` classes
- **Physics objects**: Extend `PhysicsWorld` integration
- **World features**: Add to `ChunkManager` or `Chunk` classes
- **UI elements**: Integrate with ImGui system

### Performance Considerations
- Use static pipeline for grid-aligned objects
- Use dynamic pipeline only for physics objects
- Profile performance with built-in timing system
- Consider memory usage in large worlds

### Code Organization
- Keep rendering logic in `vulkan/` directory
- Put world management in `core/` directory
- Add physics features to `physics/` directory
- Maintain cross-platform compatibility

## Future Enhancements

### Planned Features
- **Level-of-Detail (LOD)**: Distance-based detail reduction
- **Advanced culling**: Frustum and occlusion culling for dynamic objects
- **Material systems**: Multiple textures and materials
- **Lighting systems**: Dynamic lighting and shadows

### Available Expansion Points
- **Instance data bits**: 11 unused bits in static pipeline for features
- **Shader variants**: Specialized shaders for different object types
- **Physics integration**: Advanced physics features and optimization
- **Networking**: Multi-player world synchronization

See individual documentation files for detailed technical information and implementation guides.
