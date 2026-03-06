# Quick Start: Extract WindowManager
## Your First Refactoring - Step by Step Guide

**Time Required:** 2 hours  
**Risk Level:** ⭐ Very Low (isolated functionality)  
**Benefit:** Reduce Application.cpp by ~200 lines, learn the refactoring pattern

---

## Why Start Here?

WindowManager is the **perfect first refactoring** because:
- ✅ **Isolated:** Window code doesn't interact with gameplay logic
- ✅ **Clear interface:** Just create window, handle resize, poll events
- ✅ **Easy to test:** Run the game - if window appears, it works!
- ✅ **Low risk:** If something breaks, it's obvious immediately
- ✅ **Quick:** Can complete in one focused session

---

## Step-by-Step Instructions

### Step 1: Create Branch (2 minutes)

```bash
cd G:\Github\phyxel
git checkout -b refactor/window-manager
git status  # Verify you're on new branch
```

**Expected output:**
```
On branch refactor/window-manager
nothing to commit, working tree clean
```

---

### Step 2: Create New Files (5 minutes)

**Create header file:**
```bash
# Windows cmd:
type nul > include\ui\WindowManager.h

# Or just create the file in VS Code
```

**Copy this into `include\ui\WindowManager.h`:**

```cpp
#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <string>
#include <functional>

namespace Phyxel {
namespace UI {

/**
 * Manages GLFW window creation, events, and lifecycle
 * Extracted from Application class for better separation of concerns
 */
class WindowManager {
public:
    using ResizeCallback = std::function<void(int width, int height)>;
    
    WindowManager();
    ~WindowManager();
    
    // Initialization
    bool initialize(int width, int height, const std::string& title);
    void cleanup();
    
    // Window operations
    bool shouldClose() const;
    void pollEvents();
    void setTitle(const std::string& title);
    void setSize(int width, int height);
    
    // State getters
    GLFWwindow* getHandle() const { return window; }
    int getWidth() const { return width; }
    int getHeight() const { return height; }
    bool wasResized() const { return resized; }
    void acknowledgeResize() { resized = false; }
    
    // Callback registration
    void setResizeCallback(ResizeCallback callback) { resizeCallback = callback; }
    
private:
    GLFWwindow* window = nullptr;
    int width = 800;
    int height = 600;
    std::string title = "Phyxel";
    bool resized = false;
    
    ResizeCallback resizeCallback;
    
    static void framebufferResizeCallbackStatic(GLFWwindow* window, int w, int h);
};

} // namespace UI
} // namespace Phyxel
```

**Create implementation file:**
```bash
type nul > src\ui\WindowManager.cpp
```

**Copy this into `src\ui\WindowManager.cpp`:**

```cpp
#include "ui/WindowManager.h"
#include "utils/Logger.h"

namespace Phyxel {
namespace UI {

WindowManager::WindowManager() = default;

WindowManager::~WindowManager() {
    cleanup();
}

bool WindowManager::initialize(int w, int h, const std::string& t) {
    width = w;
    height = h;
    title = t;
    
    LOG_INFO("WindowManager", "Initializing GLFW window system");
    
    if (!glfwInit()) {
        LOG_ERROR("WindowManager", "Failed to initialize GLFW");
        return false;
    }
    
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    
    window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!window) {
        LOG_ERROR("WindowManager", "Failed to create GLFW window");
        glfwTerminate();
        return false;
    }
    
    LOG_INFO("WindowManager", "Window created: {}x{} '{}'", width, height, title);
    
    // Set user pointer for callbacks
    glfwSetWindowUserPointer(window, this);
    
    // Register framebuffer resize callback
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallbackStatic);
    
    return true;
}

void WindowManager::cleanup() {
    if (window) {
        LOG_INFO("WindowManager", "Destroying window");
        glfwDestroyWindow(window);
        window = nullptr;
    }
    glfwTerminate();
}

bool WindowManager::shouldClose() const {
    return window && glfwWindowShouldClose(window);
}

void WindowManager::pollEvents() {
    glfwPollEvents();
}

void WindowManager::setTitle(const std::string& t) {
    title = t;
    if (window) {
        glfwSetWindowTitle(window, title.c_str());
        LOG_DEBUG("WindowManager", "Window title changed to: '{}'", title);
    }
}

void WindowManager::setSize(int w, int h) {
    width = w;
    height = h;
    if (window) {
        glfwSetWindowSize(window, width, height);
        LOG_DEBUG("WindowManager", "Window size changed to: {}x{}", width, height);
    }
}

void WindowManager::framebufferResizeCallbackStatic(GLFWwindow* window, int w, int h) {
    auto* manager = reinterpret_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (!manager) return;
    
    manager->width = w;
    manager->height = h;
    manager->resized = true;
    
    LOG_DEBUG("WindowManager", "Framebuffer resized: {}x{}", w, h);
    
    if (manager->resizeCallback) {
        manager->resizeCallback(w, h);
    }
}

} // namespace UI
} // namespace Phyxel
```

---

### Step 3: Update CMakeLists.txt (2 minutes)

Open `CMakeLists.txt` and find the `SOURCES` section. Add the new file:

```cmake
set(SOURCES
    # ... existing files ...
    
    # UI Module
    src/ui/WindowManager.cpp
    src/ui/ImGuiRenderer.cpp
    
    # ... rest of files ...
)
```

**Location:** Around line 20-40, alphabetically grouped by directory.

---

### Step 4: Update Application.h (5 minutes)

Open `include\Application.h` and make these changes:

**1. Add new include at top (after other includes):**

```cpp
#include "vulkan/RenderPipeline.h"
#include "scene/SceneManager.h"
#include "ui/WindowManager.h"  // <-- ADD THIS LINE
#include "physics/PhysicsWorld.h"
```

**2. Replace window-related members:**

Find this section (around line 95-100):
```cpp
    // Window management
    GLFWwindow* window;
```

Replace with:
```cpp
    // Window management
    std::unique_ptr<UI::WindowManager> windowManager;
```

**3. Remove these member variables** (they're now in WindowManager):
```cpp
    // DELETE these lines:
    int windowWidth;
    int windowHeight;
    std::string windowTitle;
```

**4. Keep the getter methods for now** (we'll use windowManager internally):
```cpp
    // Configuration - KEEP THESE
    void setWindowSize(int width, int height);
    void setTitle(const std::string& title);
```

---

### Step 5: Update Application.cpp (15 minutes)

Open `src\Application.cpp` and make these changes carefully:

**Change 1: Constructor** (around line 30-60)

Find:
```cpp
Application::Application() 
    : window(nullptr),
      isRunning(false),
      windowWidth(800),
      windowHeight(600),
      windowTitle("Phyxel - Voxel Physics Engine"),
```

Replace with:
```cpp
Application::Application() 
    : windowManager(nullptr),
      isRunning(false),
```

**Change 2: initializeWindow()** (around line 1072-1098)

Find the entire `initializeWindow()` function and replace it with:

```cpp
bool Application::initializeWindow() {
    LOG_INFO("Application", "Initializing window");
    
    windowManager = std::make_unique<UI::WindowManager>();
    
    if (!windowManager->initialize(800, 600, "Phyxel - Voxel Physics Engine")) {
        LOG_ERROR("Application", "Failed to initialize window manager");
        return false;
    }
    
    // Register resize callback
    windowManager->setResizeCallback([this](int w, int h) {
        LOG_DEBUG("Application", "Window resized to {}x{}", w, h);
        projectionMatrixNeedsUpdate = true;
    });
    
    // Set GLFW window user pointer for callbacks (Application*, not WindowManager*)
    glfwSetWindowUserPointer(windowManager->getHandle(), this);
    
    LOG_INFO("Application", "Window initialization complete");
    return true;
}
```

**Change 3: Update all window references**

Use Find & Replace (Ctrl+H) in Application.cpp:

| Find | Replace | Notes |
|------|---------|-------|
| `window` | `windowManager->getHandle()` | Be careful - review each one! |
| `windowWidth` | `windowManager->getWidth()` | |
| `windowHeight` | `windowManager->getHeight()` | |
| `windowTitle` | `"Phyxel"` | Or remove if unused |

**Important:** Some replacements may not apply - review each one! For example:
- Inside `initializeWindow()` - already updated above
- Inside `cleanup()` - needs special handling

**Change 4: Update run() method** (around line 192)

Find:
```cpp
void Application::run() {
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
```

Replace with:
```cpp
void Application::run() {
    while (!windowManager->shouldClose()) {
        windowManager->pollEvents();
```

**Change 5: Update cleanup()** (around line 314-365)

Find the window cleanup section:
```cpp
    // Cleanup window
    if (window) {
        glfwDestroyWindow(window);
        window = nullptr;
    }
    glfwTerminate();
```

Replace with:
```cpp
    // Cleanup window
    windowManager.reset();  // Calls WindowManager destructor which handles cleanup
```

**Change 6: Update setWindowSize and setTitle**

Find:
```cpp
void Application::setWindowSize(int width, int height) {
    windowWidth = width;
    windowHeight = height;
}

void Application::setTitle(const std::string& title) {
    windowTitle = title;
}
```

Replace with:
```cpp
void Application::setWindowSize(int width, int height) {
    if (windowManager) {
        windowManager->setSize(width, height);
        projectionMatrixNeedsUpdate = true;
    }
}

void Application::setTitle(const std::string& title) {
    if (windowManager) {
        windowManager->setTitle(title);
    }
}
```

**Change 7: Remove framebufferResizeCallback**

Find and **DELETE** the entire callback function (it's now in WindowManager):
```cpp
void Application::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    // DELETE THIS ENTIRE FUNCTION
}
```

---

### Step 6: Build and Test (10 minutes)

```bash
# Clean and rebuild
cmake --build build --clean-first

# If you get errors, read them carefully and fix one at a time
```

**Common errors and fixes:**

1. **"WindowManager.h: No such file"**
   - Check file is in `include\ui\WindowManager.h`
   - Check CMakeLists.txt has `src\ui\WindowManager.cpp`

2. **"'window' was not declared"**
   - You missed a replacement - use `windowManager->getHandle()`

3. **Linker errors about GLFW**
   - Should be fine if GLFW was already linked

**If build succeeds:**
```bash
# Run the game!
.\build\Debug\Phyxel.exe

# Or use your existing run script:
.\run_debug.bat
```

---

### Step 7: Test Everything (10 minutes)

**Manual testing checklist:**
- [ ] Window opens and displays correctly
- [ ] Window title shows "Phyxel - Voxel Physics Engine"
- [ ] Can move camera with WASD
- [ ] Can look around with mouse
- [ ] Can resize window (drag corner)
- [ ] Window resizing updates viewport correctly
- [ ] No crashes or errors in console
- [ ] Logging shows WindowManager messages

**What to look for in logs:**
```
[INFO] [WindowManager] Initializing GLFW window system
[INFO] [WindowManager] Window created: 800x600 'Phyxel - Voxel Physics Engine'
[INFO] [Application] Window initialization complete
```

---

### Step 8: Commit Your Work (5 minutes)

```bash
# Check what changed
git status
git diff

# Stage all changes
git add include/ui/WindowManager.h
git add src/ui/WindowManager.cpp
git add include/Application.h
git add src/Application.cpp
git add CMakeLists.txt

# Commit with descriptive message
git commit -m "Refactor: Extract WindowManager from Application

- Created UI::WindowManager class for GLFW window management
- Moved window initialization, cleanup, and event handling
- Reduced Application.cpp by ~150 lines
- Added resize callback system for better decoupling
- All tests passing, window functionality unchanged"

# View your commit
git log -1 --stat
```

---

## Verification Checklist

Before considering this refactoring complete:

- [ ] ✅ Code compiles without errors or warnings
- [ ] ✅ Application runs and window appears
- [ ] ✅ Window resizing works
- [ ] ✅ Camera controls work
- [ ] ✅ No crashes during 2-minute test play
- [ ] ✅ Git commit created with clear message
- [ ] ✅ Application.cpp reduced by ~150 lines
- [ ] ✅ New files follow project structure

---

## What You've Accomplished

🎉 **Congratulations!** You've successfully:
- ✅ Created your first extracted module
- ✅ Learned the refactoring pattern
- ✅ Reduced Application.cpp complexity
- ✅ Made the codebase more AI-friendly
- ✅ Created reusable window management code

**Before:**
```
Application.cpp: 2,645 lines (doing everything)
```

**After:**
```
Application.cpp: ~2,495 lines
WindowManager.cpp: 104 lines
WindowManager.h: 46 lines
Total: Same functionality, better organized!
```

---

## Next Steps

### Option A: Merge and Move On
```bash
git checkout main
git merge refactor/window-manager
git push
```

### Option B: Keep Refactoring (Recommended)
Stay on branch and continue with next easy module:

**Next easiest refactorings:**
1. **CoordinateUtils** (3 hours) - Centralize coordinate conversions
2. **TextureManager** (1 hour) - Centralize texture operations  
3. **PerformanceMonitor** (2 hours) - Extract performance overlay

See `docs/ArchitectureOverview.md` for details on next steps.

---

## Troubleshooting

### Build fails with "cannot open source file"
**Solution:** Check file paths use backslashes on Windows
```cmake
src/ui/WindowManager.cpp  # Wrong on Windows
src\ui\WindowManager.cpp  # Correct (or forward slashes work too in CMake)
```

### Window doesn't appear
**Solution:** Check `initializeWindow()` is called in `Application::initialize()`

### Resize doesn't work
**Solution:** Verify callback is registered in `initializeWindow()`

### Mouse/keyboard don't work
**Solution:** Check `glfwSetWindowUserPointer` sets `this` (Application*), not windowManager

---

## Summary

This refactoring took ~2 hours and:
- ✅ Extracted 150 lines into focused module
- ✅ Improved code organization
- ✅ Made Application class smaller
- ✅ Created reusable component
- ✅ No functionality changes (safe!)

**You're now ready for bigger refactorings!** The pattern is:
1. Create new files
2. Copy code with clear interface
3. Update CMakeLists.txt
4. Update callers
5. Build, test, commit

Apply this same pattern to the next module! 🚀
