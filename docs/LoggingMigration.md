# Logging Migration Quick Reference

## Common Patterns

### Pattern 1: Simple Console Output
```cpp
// OLD:
std::cout << "[DEBUG] Message here" << std::endl;

// NEW:
LOG_DEBUG("ModuleName", "Message here");
```

### Pattern 2: Error Output
```cpp
// OLD:
std::cerr << "Error: Something failed!" << std::endl;

// NEW:
LOG_ERROR("ModuleName", "Something failed!");
```

### Pattern 3: Formatted Output
```cpp
// OLD:
std::cout << "[DEBUG] Value: " << x << ", " << y << std::endl;

// NEW:
LOG_DEBUG_FMT("ModuleName", "Value: " << x << ", " << y);
```

### Pattern 4: Multi-line Output
```cpp
// OLD:
std::cout << "[DEBUG] Position: (" << pos.x << "," << pos.y << "," << pos.z << ")" << std::endl;

// NEW:
LOG_DEBUG_FMT("ModuleName", "Position: (" << pos.x << "," << pos.y << "," << pos.z << ")");
```

### Pattern 5: Commented Debug Code
```cpp
// OLD:
// std::cout << "[DEBUG] Debug info..." << std::endl;

// NEW (keep as TRACE level, enable/disable via config):
LOG_TRACE("ModuleName", "Debug info...");
```

---

## Module Name Mapping

Based on file location, use these module names:

| File/Directory | Module Name |
|----------------|-------------|
| `src/Application.cpp` | `Application` |
| `src/vulkan/*` | `Vulkan` or `VulkanDevice` |
| `src/physics/*` | `Physics` or `PhysicsWorld` |
| `src/core/Chunk*` | `Chunk` or `ChunkManager` |
| `src/core/Cube*` | `Cube` |
| `src/scene/*` | `Rendering` or `SceneManager` |
| `src/utils/PerformanceProfiler.cpp` | `Performance` |
| Mouse/hover code | `HoverDetection` |
| Face culling code | `FaceCulling` |
| Buffer updates | `BufferUpdate` |
| Collision detection | `Collision` |

---

## Tag to Level Mapping

Map old debug tags to new log levels:

| Old Tag | New Level | Example |
|---------|-----------|---------|
| `[DEBUG]` | `DEBUG` | `LOG_DEBUG()` |
| `[BUFFER DEBUG]` | `TRACE` or `DEBUG` | `LOG_TRACE("BufferUpdate", ...)` |
| `[PHYSICS]` | `DEBUG` | `LOG_DEBUG("Physics", ...)` |
| `[COLLISION]` | `TRACE` or `DEBUG` | `LOG_TRACE("Collision", ...)` |
| `[CHUNK]` | `DEBUG` | `LOG_DEBUG("Chunk", ...)` |
| `[HOVER DEBUG]` | `DEBUG` | `LOG_DEBUG("HoverDetection", ...)` |
| `[POSITION DEBUG]` | `TRACE` | `LOG_TRACE("Position", ...)` |
| `[CULLING]` | `DEBUG` | `LOG_DEBUG("Culling", ...)` |
| Info messages | `INFO` | `LOG_INFO()` |
| Warnings | `WARN` | `LOG_WARN()` |
| `ERROR:` | `ERROR` | `LOG_ERROR()` |

---

## Step-by-Step Migration

### Step 1: Add Include
```cpp
#include "utils/Logger.h"
```

### Step 2: Initialize (in main or Application::initialize)
```cpp
Utils::Logger::loadConfig("logging.ini");
```

### Step 3: Replace Debug Output

Example file: `VulkanDevice.cpp`

**Find:**
```cpp
std::cout << "[DEBUG] Loading texture atlas: " << atlasPath << std::endl;
```

**Replace with:**
```cpp
LOG_DEBUG("Vulkan", "Loading texture atlas: " + atlasPath);
// Or with formatting:
LOG_DEBUG_FMT("Vulkan", "Loading texture atlas: " << atlasPath);
```

### Step 4: Replace Error Output

**Find:**
```cpp
std::cerr << "Failed to create swapchain!" << std::endl;
```

**Replace with:**
```cpp
LOG_ERROR("Vulkan", "Failed to create swapchain!");
```

### Step 5: Update Commented Debug Code

**Find:**
```cpp
// std::cout << "[BUFFER DEBUG] Updating buffer..." << std::endl;
```

**Replace with:**
```cpp
LOG_TRACE("BufferUpdate", "Updating buffer...");
```

Then control via `logging.ini`:
```ini
[Modules]
BufferUpdate=OFF  # Enable only when debugging buffers
```

---

## Search & Replace Patterns (for your IDE)

Use these regex patterns for bulk migration:

### Pattern 1: Simple Debug Output
**Find:** `std::cout << "\[DEBUG\] (.+?)" << std::endl;`
**Replace:** `LOG_DEBUG("MODULE", "$1");`
*(Then manually replace MODULE with correct name)*

### Pattern 2: Error Output
**Find:** `std::cerr << "(.+?)" << std::endl;`
**Replace:** `LOG_ERROR("MODULE", "$1");`

### Pattern 3: Commented Debug
**Find:** `// std::cout << "\[(.*?)\] (.+?)" << std::endl;`
**Replace:** `LOG_TRACE("MODULE", "$2");`

---

## Priority Files for Migration

Convert these files first (most debug output):

1. ✅ **Application.cpp** (started)
2. **VulkanDevice.cpp** (~30 debug statements)
3. **Chunk.cpp** (~15 commented debug lines)
4. **ChunkManager.cpp** (~10 commented debug lines)
5. **PhysicsWorld.cpp** (~15 commented debug lines)
6. **RenderPipeline.cpp** (~8 debug statements)

---

## Testing After Migration

### Test 1: Default Configuration
```bash
# Run with default INFO level
./VulkanCube.exe
# Should see INFO messages, no DEBUG/TRACE
```

### Test 2: Enable Debug
Edit `logging.ini`:
```ini
[General]
global_level=DEBUG
```
Run again, should see DEBUG messages.

### Test 3: Module-Specific Debug
```ini
[General]
global_level=INFO

[Modules]
Physics=DEBUG
Vulkan=WARN
```
Only Physics DEBUG messages should appear.

### Test 4: File Output
```ini
[General]
file_output=true
log_file=test.log
```
Check that `test.log` is created with output.

---

## Rollback Plan

If you need to revert:
1. Backup files before migration: `git stash` or `git branch migration_backup`
2. Keep old code commented initially:
```cpp
// OLD: std::cout << "[DEBUG] ..." << std::endl;
LOG_DEBUG("Module", "...");
```
3. Test thoroughly before removing old code

---

## Common Issues

### Issue: Logs not appearing
**Solution:** Check log level and module configuration

### Issue: Too much output
**Solution:** Increase level to WARN or INFO

### Issue: Performance degradation
**Solution:** Disable TRACE logs or use `isLevelEnabled()` for expensive operations

### Issue: File not created
**Solution:** Check write permissions, verify `file_output=true`

---

## Example Full Migration

**Before (VulkanDevice.cpp):**
```cpp
std::cout << "[DEBUG] Loading texture atlas: " << atlasPath << std::endl;
if (!pixels) {
    std::cerr << "Failed to load texture atlas: " << atlasPath << std::endl;
    std::cerr << "stb_image error: " << stbi_failure_reason() << std::endl;
    std::cout << "[WARNING] Creating fallback checkerboard texture..." << std::endl;
    // Fallback code...
} else {
    std::cout << "[DEBUG] Successfully loaded texture atlas: " 
              << texWidth << "x" << texHeight << " channels=" << texChannels << std::endl;
}
```

**After:**
```cpp
LOG_DEBUG_FMT("Vulkan", "Loading texture atlas: " << atlasPath);
if (!pixels) {
    LOG_ERROR_FMT("Vulkan", "Failed to load texture atlas: " << atlasPath);
    LOG_ERROR_FMT("Vulkan", "stb_image error: " << stbi_failure_reason());
    LOG_WARN("Vulkan", "Creating fallback checkerboard texture");
    // Fallback code...
} else {
    LOG_DEBUG_FMT("Vulkan", "Successfully loaded texture atlas: " 
                  << texWidth << "x" << texHeight << " channels=" << texChannels);
}
```

Control via config:
```ini
[Modules]
Vulkan=INFO  # Only show warnings and errors, hide debug texture loading
```

---

## Need Help?

- See full documentation: `docs/LoggingSystem.md`
- Check examples in: `src/Application.cpp` (partially converted)
- Header file: `include/utils/Logger.h`
