# Phyxel Logging System Documentation

## Overview

The Phyxel engine includes a comprehensive, production-ready logging system with:
- Multiple log levels (TRACE, DEBUG, INFO, WARN, ERROR, FATAL)
- Per-module logging control
- Console and file output
- Thread-safe operations
- Configurable formatting
- Runtime configuration via config file

---

## Quick Start

### Basic Usage

```cpp
#include "utils/Logger.h"

// Simple logging
LOG_INFO("Application", "Engine started successfully");
LOG_ERROR("Vulkan", "Failed to create swapchain");
LOG_DEBUG("Physics", "Rigid body created");

// Formatted logging (C++11 style)
LOG_INFO_FMT("Rendering", "Frame time: " << frameTime << "ms");
LOG_DEBUG_FMT("Chunk", "Position: (" << x << "," << y << "," << z << ")");
```

### Initialization

```cpp
// In Application::initialize() or main()
#include "utils/Logger.h"

// Load configuration from file
Utils::Logger::loadConfig("logging.ini");

// Or configure programmatically
Utils::Logger::setGlobalLevel(LogLevel::DEBUG);
Utils::Logger::enableModule("Physics", LogLevel::TRACE);
Utils::Logger::enableFileOutput(true, "game.log");
```

---

## Log Levels

| Level | Purpose | When to Use |
|-------|---------|-------------|
| **TRACE** | Most detailed | Function entry/exit, data flow, loop iterations |
| **DEBUG** | Debug information | Variable values, state changes, intermediate results |
| **INFO** | General information | Important events, milestones, normal operations |
| **WARN** | Warnings | Potential issues, deprecated API usage, recoverable problems |
| **ERROR** | Errors | Recoverable errors, failed operations, invalid state |
| **FATAL** | Fatal errors | Unrecoverable errors, crashes, critical failures |
| **OFF** | No logging | Disable logging for specific modules |

---

## Module System

### Predefined Modules

The system uses these module categories throughout the codebase:

- `Application` - Main application logic, user interactions, cube operations
- `Rendering` - Rendering pipeline, frame rendering, Vulkan initialization  
- `Vulkan` - Vulkan device, validation, and graphics operations
- `Physics` - Bullet Physics operations and rigid body management
- `Chunk` - Chunk operations, collision system, voxel management
- `ChunkManager` - Global chunk management and dynamic cubes
- `WorldStorage` - Database persistence and chunk loading/saving
- `Scene` - Scene management and instance data
- `UI` - ImGui rendering and interface
- `Performance` - Performance profiling and metrics
- `ForceSystem` - Force propagation and material properties
- `WorldGenerator` - Procedural world generation
- `Main` - Application startup and initialization

### Creating Custom Modules

Simply use any string as a module name:

```cpp
LOG_INFO("MyCustomModule", "Custom module message");
LOG_DEBUG("NetworkSync", "Syncing player position...");
LOG_TRACE("AI", "Pathfinding iteration complete");
```

### Module Configuration

```cpp
// Enable specific module at DEBUG level
Utils::Logger::enableModule("Physics", LogLevel::DEBUG);

// Disable noisy module
Utils::Logger::disableModule("ImGui");

// Set module-specific level
Utils::Logger::setModuleLevel("Vulkan", LogLevel::WARN); // Only warnings and errors

// Enable all modules at INFO level
Utils::Logger::enableAllModules(LogLevel::INFO);
```

---

## Configuration File

### File Format (`logging.ini`)

```ini
# Phyxel Engine Logging Configuration

[General]
global_level=INFO          # Default level for all modules
console_output=true        # Print to console
file_output=false          # Write to log file
log_file=phyxel.log       # Log file path
timestamps=true            # Show timestamps
colors=true                # Use colored output
module_names=true          # Show module names
thread_ids=false           # Show thread IDs

[Modules]
# Override levels for specific modules
Application=INFO
Vulkan=WARN               # Only warnings and errors
Physics=DEBUG             # Enable debug output
Rendering=INFO
Chunk=DEBUG
HoverDetection=OFF        # Disable completely
```

### Loading Configuration

```cpp
// Load at startup
if (!Utils::Logger::loadConfig("logging.ini")) {
    LOG_WARN("Application", "Failed to load logging config, using defaults");
}

// Save current configuration
Utils::Logger::saveConfig("logging_backup.ini");
```

---

## Advanced Usage

### Conditional Logging

Only log if level is enabled (avoids expensive string formatting):

```cpp
if (Utils::Logger::isLevelEnabled(LogLevel::DEBUG, "Physics")) {
    std::ostringstream oss;
    // Expensive formatting here
    for (auto& body : rigidBodies) {
        oss << body.getPosition() << ", ";
    }
    LOG_DEBUG("Physics", oss.str());
}
```

### Formatted Output

Use the `_FMT` macros for inline formatting:

```cpp
// Simple variables
LOG_INFO_FMT("Chunk", "Loaded chunk at (" << x << "," << y << "," << z << ")");

// Complex expressions
LOG_DEBUG_FMT("Performance", "FPS: " << (1000.0 / frameTime) << " (" << frameTime << "ms)");

// Floating point precision
LOG_INFO_FMT("Position", "Camera: (" << std::fixed << std::setprecision(2) 
             << pos.x << ", " << pos.y << ", " << pos.z << ")");
```

### File Output

```cpp
// Enable file logging
Utils::Logger::enableFileOutput(true, "debug.log");

// Change file dynamically
Utils::Logger::setOutputFile("error.log");

// Flush immediately (e.g., before crash)
Utils::Logger::flush();
```

### Thread Safety

The logger is fully thread-safe:

```cpp
// Safe to call from multiple threads
std::thread worker([]{
    LOG_INFO("Worker", "Background task started");
    // ... work ...
    LOG_INFO("Worker", "Background task completed");
});
```

---

## Migration Guide

### Converting Old Debug Code

**Before:**
```cpp
std::cout << "[DEBUG] Loading texture: " << path << std::endl;
std::cerr << "ERROR: Failed to create buffer!" << std::endl;
```

**After:**
```cpp
LOG_DEBUG("Vulkan", "Loading texture: " + path);
LOG_ERROR("Vulkan", "Failed to create buffer!");
```

**Before (formatted):**
```cpp
std::cout << "[PHYSICS] Created rigid body at (" << pos.x << "," << pos.y << "," << pos.z << ")" << std::endl;
```

**After:**
```cpp
LOG_DEBUG_FMT("Physics", "Created rigid body at (" << pos.x << "," << pos.y << "," << pos.z << ")");
```

### Removing Commented Debug Code

Instead of:
```cpp
// std::cout << "[DEBUG] Buffer updated..." << std::endl;
```

Use conditional logging:
```cpp
LOG_TRACE("BufferUpdate", "Buffer updated");
```

Then control it via configuration:
```ini
[Modules]
BufferUpdate=OFF  # Disable when not debugging
```

---

## Best Practices

### 1. Choose Appropriate Levels

```cpp
// ✅ Good - use INFO for important events
LOG_INFO("Application", "Engine initialized successfully");

// ❌ Bad - don't use DEBUG for critical information
LOG_DEBUG("Application", "Engine started"); // User won't see this by default
```

### 2. Use Descriptive Modules

```cpp
// ✅ Good - specific module names
LOG_DEBUG("ChunkLoading", "Loaded chunk from database");
LOG_DEBUG("ChunkCulling", "Culled 15 invisible chunks");

// ❌ Bad - too generic
LOG_DEBUG("System", "Did something");
```

### 3. Provide Context

```cpp
// ✅ Good - includes useful context
LOG_ERROR_FMT("Vulkan", "Failed to create swapchain: format=" << format 
              << ", size=" << width << "x" << height);

// ❌ Bad - too vague
LOG_ERROR("Vulkan", "Swapchain creation failed");
```

### 4. Use Levels Appropriately

```cpp
LOG_TRACE("Physics", "Step simulation iteration 5/60");  // Very detailed
LOG_DEBUG("Physics", "Rigid body count: 127");           // Debug info
LOG_INFO("Physics", "Physics world initialized");         // Important event
LOG_WARN("Physics", "Low frame rate detected");          // Potential problem
LOG_ERROR("Physics", "Failed to add rigid body");        // Actual error
LOG_FATAL("Physics", "Physics world corrupted");         // Critical failure
```

### 5. Disable Expensive Logging

```cpp
// For expensive operations, check if logging is enabled first
if (Utils::Logger::isLevelEnabled(LogLevel::TRACE, "Profiling")) {
    std::string detailedStats = generateDetailedProfilingReport(); // Expensive
    LOG_TRACE("Profiling", detailedStats);
}
```

---

## Development Workflow

### During Development

**logging.ini:**
```ini
[General]
global_level=DEBUG         # See most logs
console_output=true
file_output=true           # Save to file for analysis
log_file=development.log

[Modules]
MyNewFeature=TRACE        # Very detailed for active development
Vulkan=DEBUG              
Physics=DEBUG
```

### For Release Builds

**logging.ini:**
```ini
[General]
global_level=WARN         # Only warnings and errors
console_output=true
file_output=true          # Keep file output for bug reports
log_file=game.log

[Modules]
# All modules inherit WARN level
```

### For Debugging Specific Issues

**logging.ini:**
```ini
[General]
global_level=INFO

[Modules]
Physics=TRACE             # Enable detailed physics logging
Collision=TRACE           # Enable collision detection details
Performance=OFF           # Disable performance stats
```

---

## Performance Considerations

### Overhead

- **TRACE/DEBUG disabled**: Nearly zero overhead (compile-time checks)
- **INFO and above**: Minimal overhead (~microseconds per log)
- **File output**: Buffered I/O, minimal performance impact

### Optimization Tips

1. **Use appropriate levels**: Don't log TRACE in release builds
2. **Check before formatting**: Use `isLevelEnabled()` for expensive operations
3. **Avoid logging in hot loops**: Log summary statistics instead
4. **Disable noisy modules**: Turn off modules that spam logs

```cpp
// ❌ Bad - logs in hot loop
for (int i = 0; i < 1000000; i++) {
    LOG_TRACE("Loop", "Iteration " + std::to_string(i));
}

// ✅ Good - log summary
int processedCount = 0;
for (int i = 0; i < 1000000; i++) {
    // ... work ...
    processedCount++;
}
LOG_INFO_FMT("Processing", "Processed " << processedCount << " items");
```

---

## Troubleshooting

### No Logs Appearing

1. Check log level: `Utils::Logger::setGlobalLevel(LogLevel::DEBUG);`
2. Check module is enabled: `Utils::Logger::enableModule("MyModule");`
3. Verify console output: `Utils::Logger::enableConsoleOutput(true);`

### Too Many Logs

1. Increase global level: `global_level=WARN` in config
2. Disable specific modules: `MyNoisyModule=OFF`
3. Disable console output: `console_output=false`, use file only

### Config Not Loading

1. Check file path: File should be in executable directory
2. Check file format: Use INI format with `[Section]` headers
3. Check for errors: Config loading failures are logged

---

## Examples

See the migrated logging in these files:
- `src/Application.cpp` - Application initialization, user interactions, cube operations
- `src/vulkan/VulkanDevice.cpp` - Vulkan operations and validation callbacks
- `src/vulkan/RenderPipeline.cpp` - Pipeline creation and shader loading
- `src/graphics/Renderer.cpp` - Rendering initialization and frame rendering
- `src/physics/PhysicsWorld.cpp` - Physics world management
- `src/core/Chunk.cpp` - Chunk operations and collision system
- `src/core/ChunkManager.cpp` - Global chunk and dynamic cube management
- `src/core/WorldStorage.cpp` - Database persistence operations
- `src/scene/SceneManager.cpp` - Scene management
- `src/ui/ImGuiRenderer.cpp` - UI rendering
- `src/utils/PerformanceProfiler.cpp` - Performance metrics

All console output has been migrated to use the centralized Logger system.

---

## Future Enhancements

Potential future features:
- Asynchronous logging (log queue + background thread)
- Log rotation (automatic file rotation by size/date)
- Remote logging (send logs to network server)
- Structured logging (JSON format)
- Performance profiling integration
- Log filtering by regex patterns

---

**Questions?** Check the header file `include/utils/Logger.h` for full API documentation.
