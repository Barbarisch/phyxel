# Logging Quick Reference

This document provides a quick reference for the logging implementation across the Phyxel codebase.

---

## Log Level Guidelines

| Level | When to Use | Example |
|-------|-------------|---------|
| **TRACE** | Very detailed debugging, hot paths, collision details | Collision entity tracking, spatial grid operations |
| **DEBUG** | Development debugging, state changes, operation details | Chunk loading, physics body creation, buffer updates |
| **INFO** | Important events, milestones, successful operations | Initialization complete, world loaded, features enabled |
| **WARN** | Potential issues, deprecated usage, recoverable problems | Missing config file, performance warnings |
| **ERROR** | Failures, exceptions, invalid state | Failed to create buffer, database error |
| **FATAL** | Unrecoverable errors, crashes | (Reserved for critical failures) |

---

## Module Categories & Usage

### Core Application
- **Application** - User interactions, cube operations, physics integration
  - Levels: INFO (normal), DEBUG (interactions), ERROR (failures)
  - Examples: Cube placement, breaking, subdivision, hover detection
  
- **Main** - Application startup and initialization
  - Levels: INFO (startup), ERROR (init failures)

### Graphics & Rendering
- **Rendering** - Frame rendering, Vulkan initialization
  - Levels: INFO (init), DEBUG (operations), TRACE (frame details), ERROR (failures)
  - Examples: Renderer initialization, frame rendering steps
  
- **Vulkan** - Vulkan device, validation callbacks
  - Levels: INFO (device selection), DEBUG (validation), ERROR (creation failures)

### World Management
- **Chunk** - Voxel operations, collision system, physics
  - Levels: DEBUG (operations), TRACE (collision details), ERROR (failures)
  - High TRACE usage for spatial grid and collision debugging
  
- **ChunkManager** - Global chunk management, dynamic cubes
  - Levels: DEBUG (operations, selective updates)
  - Examples: Dynamic cube tracking, position updates
  
- **WorldStorage** - SQLite database operations
  - Levels: INFO (init), DEBUG (save/load), ERROR (database errors)
  - Examples: Chunk persistence, database operations
  
- **WorldGenerator** - Procedural world generation
  - Levels: DEBUG (generation), ERROR (validation)

### Physics & Forces
- **Physics** - Bullet Physics world operations
  - Levels: DEBUG (body creation), ERROR (failures)
  
- **ForceSystem** - Force propagation and material properties
  - Levels: DEBUG (calculations, propagation)

### Scene & UI
- **Scene** - Scene management, instance data
  - Levels: DEBUG (updates), ERROR (failures)
  
- **UI** - ImGui rendering
  - Levels: DEBUG (operations), ERROR (init failures)
  
- **Performance** - Performance profiling
  - Levels: DEBUG (metrics), INFO (summaries)

---

## File-by-File Log Usage

### Application.cpp (~110 statements)
```cpp
Categories: "Application"
Levels:
  - INFO: Feature toggles, render distance changes
  - DEBUG: Cube interactions, hover detection, subdivision
  - DEBUG_FMT: Position tracking, physics details
  - ERROR: Invalid operations, missing components
```

### Chunk.cpp (~54 statements)
```cpp
Categories: "Chunk"
Levels:
  - DEBUG: Voxel map updates, collision updates, physics sync
  - DEBUG_FMT: Position logging, entity counts
  - TRACE: Collision entity tracking (very detailed)
  - TRACE_FMT: Spatial grid operations, neighbor updates
  - ERROR: Validation failures, shape mismatches
```

### ChunkManager.cpp (11 statements)
```cpp
Categories: "ChunkManager"
Levels:
  - DEBUG: Selective updates (break, place, subdivision)
  - DEBUG_FMT: Dynamic cube operations, position tracking
```

### WorldStorage.cpp (35 statements)
```cpp
Categories: "WorldStorage"
Levels:
  - INFO: Database initialization, connection
  - DEBUG: Chunk save/load operations, transaction results
  - DEBUG_FMT: Chunk coordinates, counts
  - ERROR: Database errors, transaction failures
  - ERROR_FMT: SQL errors with details
```

### Renderer.cpp (25 statements)
```cpp
Categories: "Rendering"
Levels:
  - INFO: Renderer initialization
  - DEBUG: (reserved)
  - TRACE: Frame rendering steps (very detailed)
  - ERROR: Initialization failures, frame errors
```

### ForceSystem.cpp (8 statements)
```cpp
Categories: "ForceSystem"
Levels:
  - DEBUG: Force calculations, propagation
  - DEBUG_FMT: Bond breaking, force values
```

### Other Files
- **main.cpp**: ERROR for initialization failures
- **Material.cpp**: DEBUG for material operations
- **WorldGenerator.cpp**: DEBUG for generation, ERROR for validation
- **DynamicCube.cpp**: DEBUG for material application
- **PhysicsWorld.cpp**: DEBUG for physics operations
- **SceneManager.cpp**: DEBUG for scene updates
- **PerformanceProfiler.cpp**: DEBUG for metrics
- **ImGuiRenderer.cpp**: DEBUG for UI operations
- **VulkanDevice.cpp**: DEBUG for validation callbacks
- **RenderPipeline.cpp**: INFO/ERROR for pipeline operations
- **Timer.cpp**: Minimal logging

---

## Common Debugging Scenarios

### Debugging Cube Breaking/Physics
```ini
[Modules]
Application=DEBUG        # See user interactions
Chunk=DEBUG             # See collision updates
Physics=DEBUG           # See rigid body creation
ForceSystem=DEBUG       # See force propagation
```

### Debugging World Loading/Saving
```ini
[Modules]
WorldStorage=DEBUG      # See database operations
ChunkManager=DEBUG      # See chunk management
Chunk=DEBUG            # See chunk state
```

### Debugging Rendering Issues
```ini
[Modules]
Rendering=DEBUG        # See renderer operations
Vulkan=DEBUG          # See Vulkan calls
Scene=DEBUG           # See scene updates
```

### Maximum Detail (Collision System)
```ini
[Modules]
Chunk=TRACE           # Very detailed collision tracking
Application=DEBUG     # User interactions
Physics=DEBUG         # Physics operations
```

### Performance Profiling
```ini
[Modules]
Performance=DEBUG     # Performance metrics
Rendering=INFO       # Keep rendering quiet
Chunk=WARN          # Reduce noise
```

---

## Configuration Tips

### Development Setup
```ini
[General]
global_level=DEBUG
file_output=true
log_file=development.log

[Modules]
# Enable debug for active work areas
Application=DEBUG
Chunk=DEBUG
```

### Release/Production
```ini
[General]
global_level=WARN
file_output=true
log_file=game.log

[Modules]
# Only warnings and errors
```

### Debugging Crashes
```ini
[General]
global_level=DEBUG
file_output=true      # Capture to file before crash

[Modules]
Application=DEBUG
Physics=DEBUG
Chunk=DEBUG
```

---

## Migration Notes

All `std::cout` and `std::cerr` statements in application code have been converted to use LOG_* macros. The only remaining console output is:
1. **Logger.cpp** itself (needs console output for logging)
2. **External libraries** (Bullet3 - not our code)

Total conversion: ~250+ statements across 19 files.

---

**Last Updated**: After complete logging migration (November 2025)
