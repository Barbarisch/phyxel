# Refactoring Examples & Migration Guide
## Practical Code Examples for Phyxel Refactoring

This document provides concrete code examples for the refactoring recommendations in `CodebaseRefactoringAnalysis.md`.

---

## Example 1: Extracting WindowManager from Application

### Before: Mixed in Application.h
```cpp
class Application {
private:
    GLFWwindow* window;
    int windowWidth;
    int windowHeight;
    std::string windowTitle;
    bool framebufferResized = false;
    
    bool initializeWindow();
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
    void setWindowSize(int width, int height);
    void setTitle(const std::string& title);
    // ... 40 other methods
};
```

### After: Dedicated WindowManager

**include/ui/WindowManager.h**
```cpp
#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <string>
#include <functional>

namespace VulkanCube {
namespace UI {

class WindowManager {
public:
    using ResizeCallback = std::function<void(int width, int height)>;
    
    WindowManager();
    ~WindowManager();
    
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
} // namespace VulkanCube
```

**src/ui/WindowManager.cpp**
```cpp
#include "ui/WindowManager.h"
#include "utils/Logger.h"

namespace VulkanCube {
namespace UI {

WindowManager::WindowManager() = default;

WindowManager::~WindowManager() {
    cleanup();
}

bool WindowManager::initialize(int w, int h, const std::string& t) {
    width = w;
    height = h;
    title = t;
    
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    
    window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!window) {
        LOG_ERROR("WindowManager", "Failed to create GLFW window");
        return false;
    }
    
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallbackStatic);
    
    LOG_INFO("WindowManager", "Window initialized: {}x{}", width, height);
    return true;
}

void WindowManager::cleanup() {
    if (window) {
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
    }
}

void WindowManager::setSize(int w, int h) {
    width = w;
    height = h;
    if (window) {
        glfwSetWindowSize(window, width, height);
    }
}

void WindowManager::framebufferResizeCallbackStatic(GLFWwindow* window, int w, int h) {
    auto* manager = reinterpret_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    manager->width = w;
    manager->height = h;
    manager->resized = true;
    
    if (manager->resizeCallback) {
        manager->resizeCallback(w, h);
    }
}

} // namespace UI
} // namespace VulkanCube
```

**Updated Application.h**
```cpp
#include "ui/WindowManager.h"

class Application {
private:
    std::unique_ptr<UI::WindowManager> windowManager;
    // ... other members
    
    bool initializeWindow();  // Now just creates windowManager
};
```

**Updated Application.cpp**
```cpp
bool Application::initializeWindow() {
    windowManager = std::make_unique<UI::WindowManager>();
    
    if (!windowManager->initialize(windowWidth, windowHeight, windowTitle)) {
        return false;
    }
    
    // Register resize callback
    windowManager->setResizeCallback([this](int w, int h) {
        windowWidth = w;
        windowHeight = h;
        projectionMatrixNeedsUpdate = true;
    });
    
    return true;
}

void Application::run() {
    while (!windowManager->shouldClose()) {
        windowManager->pollEvents();
        // ... rest of game loop
    }
}
```

**Benefits:**
- Application.cpp reduced by ~150 lines
- WindowManager testable independently
- Can mock for headless testing
- Reusable in other projects

---

## Example 2: Extracting CameraController

### Before: Scattered in Application

```cpp
class Application {
private:
    glm::vec3 cameraPos;
    glm::vec3 cameraFront;
    glm::vec3 cameraUp;
    float yaw;
    float pitch;
    float lastX, lastY;
    bool firstMouse;
    glm::mat4 cachedViewMatrix;
    glm::mat4 cachedProjectionMatrix;
    
    void initializeCamera();
    static void mouseCallback(GLFWwindow* window, double xpos, double ypos);
    void updateCameraFrustum(const glm::mat4& view, const glm::mat4& proj);
    // Movement in processInput()
};
```

### After: Dedicated CameraController

**include/scene/CameraController.h**
```cpp
#pragma once
#include <glm/glm.hpp>
#include "utils/Frustum.h"

namespace VulkanCube {
namespace Scene {

class CameraController {
public:
    CameraController();
    
    // Initialization
    void setPosition(const glm::vec3& pos);
    void setOrientation(float yaw, float pitch);
    void setProjection(float fov, float aspect, float near, float far);
    
    // Movement (call from input system)
    void moveForward(float deltaTime);
    void moveBackward(float deltaTime);
    void moveLeft(float deltaTime);
    void moveRight(float deltaTime);
    void moveUp(float deltaTime);
    void moveDown(float deltaTime);
    
    // Look controls
    void rotate(float deltaX, float deltaY, float sensitivity = 0.1f);
    
    // Getters
    glm::vec3 getPosition() const { return position; }
    glm::vec3 getFront() const { return front; }
    glm::vec3 getUp() const { return up; }
    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix() const;
    Utils::Frustum getFrustum() const;
    
    // Speed controls
    void setMoveSpeed(float speed) { moveSpeed = speed; }
    float getMoveSpeed() const { return moveSpeed; }
    
private:
    // Position and orientation
    glm::vec3 position{0.0f};
    glm::vec3 front{0.0f, 0.0f, -1.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    glm::vec3 worldUp{0.0f, 1.0f, 0.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    
    // Euler angles
    float yaw = -90.0f;
    float pitch = 0.0f;
    
    // Camera settings
    float moveSpeed = 32.0f;
    float fov = 45.0f;
    float aspectRatio = 16.0f / 9.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    
    // Cached matrices
    mutable glm::mat4 viewMatrix;
    mutable glm::mat4 projectionMatrix;
    mutable Utils::Frustum frustum;
    mutable bool viewDirty = true;
    mutable bool projectionDirty = true;
    
    void updateVectors();
    void updateFrustum() const;
};

} // namespace Scene
} // namespace VulkanCube
```

**src/scene/CameraController.cpp**
```cpp
#include "scene/CameraController.h"
#include <glm/gtc/matrix_transform.hpp>

namespace VulkanCube {
namespace Scene {

CameraController::CameraController() {
    updateVectors();
}

void CameraController::setPosition(const glm::vec3& pos) {
    position = pos;
    viewDirty = true;
}

void CameraController::setOrientation(float y, float p) {
    yaw = y;
    pitch = glm::clamp(p, -89.0f, 89.0f);
    updateVectors();
    viewDirty = true;
}

void CameraController::setProjection(float f, float aspect, float n, float far) {
    fov = f;
    aspectRatio = aspect;
    nearPlane = n;
    farPlane = far;
    projectionDirty = true;
}

void CameraController::moveForward(float deltaTime) {
    position += front * moveSpeed * deltaTime;
    viewDirty = true;
}

void CameraController::moveBackward(float deltaTime) {
    position -= front * moveSpeed * deltaTime;
    viewDirty = true;
}

void CameraController::moveLeft(float deltaTime) {
    position -= right * moveSpeed * deltaTime;
    viewDirty = true;
}

void CameraController::moveRight(float deltaTime) {
    position += right * moveSpeed * deltaTime;
    viewDirty = true;
}

void CameraController::moveUp(float deltaTime) {
    position += worldUp * moveSpeed * deltaTime;
    viewDirty = true;
}

void CameraController::moveDown(float deltaTime) {
    position -= worldUp * moveSpeed * deltaTime;
    viewDirty = true;
}

void CameraController::rotate(float deltaX, float deltaY, float sensitivity) {
    yaw += deltaX * sensitivity;
    pitch = glm::clamp(pitch + deltaY * sensitivity, -89.0f, 89.0f);
    updateVectors();
    viewDirty = true;
}

glm::mat4 CameraController::getViewMatrix() const {
    if (viewDirty) {
        viewMatrix = glm::lookAt(position, position + front, up);
        viewDirty = false;
        updateFrustum();
    }
    return viewMatrix;
}

glm::mat4 CameraController::getProjectionMatrix() const {
    if (projectionDirty) {
        projectionMatrix = glm::perspective(glm::radians(fov), aspectRatio, nearPlane, farPlane);
        projectionMatrix[1][1] *= -1; // Vulkan clip space
        projectionDirty = false;
        updateFrustum();
    }
    return projectionMatrix;
}

Utils::Frustum CameraController::getFrustum() const {
    getViewMatrix();       // Ensure view is up to date
    getProjectionMatrix(); // Ensure projection is up to date
    return frustum;
}

void CameraController::updateVectors() {
    front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    front.y = sin(glm::radians(pitch));
    front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    front = glm::normalize(front);
    
    right = glm::normalize(glm::cross(front, worldUp));
    up = glm::normalize(glm::cross(right, front));
}

void CameraController::updateFrustum() const {
    frustum = Utils::Frustum(getViewMatrix(), getProjectionMatrix());
}

} // namespace Scene
} // namespace VulkanCube
```

**Updated Application**
```cpp
class Application {
private:
    std::unique_ptr<Scene::CameraController> camera;
    
    void processInput() {
        float deltaTime = timer->getDeltaTime();
        
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            camera->moveForward(deltaTime);
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            camera->moveBackward(deltaTime);
        // ... etc
    }
    
    static void mouseCallback(GLFWwindow* window, double xpos, double ypos) {
        auto* app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
        static double lastX = xpos;
        static double lastY = ypos;
        
        app->camera->rotate(xpos - lastX, lastY - ypos);
        lastX = xpos;
        lastY = ypos;
    }
};
```

---

## Example 3: Extracting ChunkPhysicsManager

### Before: Embedded in Chunk.cpp (700 lines)

```cpp
class Chunk {
private:
    btRigidBody* chunkPhysicsBody;
    btCollisionShape* chunkCollisionShape;
    
    struct CollisionEntity {
        btCollisionShape* shape;
        // ... complex tracking
    };
    
    class CollisionSpatialGrid {
        // ... 300 lines of spatial grid logic
    };
    
    CollisionSpatialGrid collisionGrid;
    
    void rebuildPhysicsBody();
    void addCollisionEntity(/* many params */);
    void removeCollisionEntity(/* many params */);
    // ... 20 more collision methods
};
```

### After: Dedicated ChunkPhysicsManager

**include/physics/ChunkPhysicsManager.h**
```cpp
#pragma once
#include <glm/glm.hpp>
#include <memory>
#include <vector>

// Forward declarations
class btRigidBody;
class btCollisionShape;
class btCompoundShape;

namespace VulkanCube {
namespace Physics {

class PhysicsWorld; // Forward declaration

/**
 * Manages physics collision shapes for a single chunk
 * Handles spatial grid optimization and Bullet physics integration
 */
class ChunkPhysicsManager {
public:
    ChunkPhysicsManager(const glm::ivec3& chunkWorldOrigin);
    ~ChunkPhysicsManager();
    
    // Initialization
    void initialize(PhysicsWorld* world);
    void cleanup();
    
    // Cube collision management
    void addCubeCollision(const glm::ivec3& localPos);
    void removeCubeCollision(const glm::ivec3& localPos);
    
    // Subcube collision management
    void addSubcubeCollision(const glm::ivec3& localPos, const glm::ivec3& subcubePos);
    void removeSubcubeCollision(const glm::ivec3& localPos, const glm::ivec3& subcubePos);
    void removeAllSubcubesAt(const glm::ivec3& localPos);
    
    // Batch operations
    void rebuildAllCollisions();
    void clearAllCollisions();
    
    // State queries
    bool hasCubeCollision(const glm::ivec3& localPos) const;
    bool hasSubcubeCollision(const glm::ivec3& localPos, const glm::ivec3& subcubePos) const;
    size_t getTotalCollisionCount() const;
    
    // Physics body access
    btRigidBody* getPhysicsBody() const { return physicsBody; }
    
private:
    struct CollisionEntity {
        btCollisionShape* shape;
        enum Type { CUBE, SUBCUBE } type;
        glm::vec3 worldCenter;
        float boundingRadius;
        
        CollisionEntity(btCollisionShape* s, Type t, const glm::vec3& center);
        ~CollisionEntity();
    };
    
    class CollisionSpatialGrid {
    public:
        static constexpr int GRID_SIZE = 32;
        
        void addEntity(const glm::ivec3& pos, std::shared_ptr<CollisionEntity> entity);
        void removeEntity(const glm::ivec3& pos, std::shared_ptr<CollisionEntity> entity);
        void removeAllAt(const glm::ivec3& pos);
        std::vector<std::shared_ptr<CollisionEntity>>& getEntitiesAt(const glm::ivec3& pos);
        void clear();
        
        size_t getTotalEntityCount() const { return totalEntities; }
        
    private:
        std::vector<std::shared_ptr<CollisionEntity>> grid[GRID_SIZE][GRID_SIZE][GRID_SIZE];
        size_t totalEntities = 0;
    };
    
    glm::ivec3 chunkOrigin;
    PhysicsWorld* physics = nullptr;
    btRigidBody* physicsBody = nullptr;
    btCompoundShape* compoundShape = nullptr;
    CollisionSpatialGrid spatialGrid;
    
    void createPhysicsBody();
    void updateCompoundShape();
    glm::vec3 localToWorld(const glm::ivec3& localPos) const;
};

} // namespace Physics
} // namespace VulkanCube
```

**Updated Chunk.h**
```cpp
#include "physics/ChunkPhysicsManager.h"

class Chunk {
private:
    std::unique_ptr<Physics::ChunkPhysicsManager> physicsManager;
    
public:
    void initializePhysics(Physics::PhysicsWorld* physics) {
        physicsManager = std::make_unique<Physics::ChunkPhysicsManager>(worldOrigin);
        physicsManager->initialize(physics);
    }
    
    void addCube(const glm::ivec3& localPos, const glm::vec3& color) {
        // ... add to voxel storage
        if (physicsManager) {
            physicsManager->addCubeCollision(localPos);
        }
    }
};
```

**Benefits:**
- Chunk.cpp reduced by ~700 lines
- Physics logic isolated and testable
- Can benchmark physics independently
- Easier to swap physics backends

---

## Example 4: Coordinate Utilities Extraction

### Before: Scattered across multiple files

```cpp
// In Chunk.cpp
glm::ivec3 worldPosToLocal(const glm::ivec3& worldPos) const {
    return worldPos - worldOrigin;
}

// In ChunkManager.cpp
glm::ivec3 worldPosToChunkCoord(const glm::ivec3& worldPos) {
    return glm::ivec3(
        worldPos.x < 0 ? (worldPos.x - 31) / 32 : worldPos.x / 32,
        worldPos.y < 0 ? (worldPos.y - 31) / 32 : worldPos.y / 32,
        worldPos.z < 0 ? (worldPos.z - 31) / 32 : worldPos.z / 32
    );
}

// Similar functions duplicated in Application.cpp
```

### After: Centralized Utilities

**include/utils/CoordinateUtils.h**
```cpp
#pragma once
#include <glm/glm.hpp>

namespace VulkanCube {
namespace Utils {

/**
 * Coordinate conversion utilities for chunk-based world
 * Chunk size: 32x32x32 cubes
 * Subcube grid: 3x3x3 per cube
 */
class CoordinateUtils {
public:
    static constexpr int CHUNK_SIZE = 32;
    static constexpr int SUBCUBE_DIVISIONS = 3;
    
    // Chunk coordinates
    static glm::ivec3 worldToChunkCoord(const glm::ivec3& worldPos);
    static glm::ivec3 chunkCoordToWorld(const glm::ivec3& chunkCoord);
    
    // Local positions within chunk
    static glm::ivec3 worldToLocal(const glm::ivec3& worldPos, const glm::ivec3& chunkOrigin);
    static glm::ivec3 localToWorld(const glm::ivec3& localPos, const glm::ivec3& chunkOrigin);
    
    // Subcube positions
    static glm::ivec3 worldToSubcubePos(const glm::vec3& worldPos);
    static glm::vec3 subcubePosToWorld(const glm::ivec3& cubeWorldPos, const glm::ivec3& subcubePos);
    
    // Validation
    static bool isValidLocalPos(const glm::ivec3& localPos);
    static bool isValidSubcubePos(const glm::ivec3& subcubePos);
    
    // Distance calculations
    static float chunkDistance(const glm::ivec3& coord1, const glm::ivec3& coord2);
    static float cubeDistance(const glm::ivec3& world1, const glm::ivec3& world2);
};

} // namespace Utils
} // namespace VulkanCube
```

**src/utils/CoordinateUtils.cpp**
```cpp
#include "utils/CoordinateUtils.h"
#include <cmath>

namespace VulkanCube {
namespace Utils {

glm::ivec3 CoordinateUtils::worldToChunkCoord(const glm::ivec3& worldPos) {
    return glm::ivec3(
        worldPos.x < 0 ? (worldPos.x - (CHUNK_SIZE - 1)) / CHUNK_SIZE : worldPos.x / CHUNK_SIZE,
        worldPos.y < 0 ? (worldPos.y - (CHUNK_SIZE - 1)) / CHUNK_SIZE : worldPos.y / CHUNK_SIZE,
        worldPos.z < 0 ? (worldPos.z - (CHUNK_SIZE - 1)) / CHUNK_SIZE : worldPos.z / CHUNK_SIZE
    );
}

glm::ivec3 CoordinateUtils::chunkCoordToWorld(const glm::ivec3& chunkCoord) {
    return chunkCoord * CHUNK_SIZE;
}

glm::ivec3 CoordinateUtils::worldToLocal(const glm::ivec3& worldPos, const glm::ivec3& chunkOrigin) {
    glm::ivec3 local = worldPos - chunkOrigin;
    
    // Handle negative wrapping
    if (local.x < 0) local.x += CHUNK_SIZE;
    if (local.y < 0) local.y += CHUNK_SIZE;
    if (local.z < 0) local.z += CHUNK_SIZE;
    
    return local;
}

glm::ivec3 CoordinateUtils::localToWorld(const glm::ivec3& localPos, const glm::ivec3& chunkOrigin) {
    return chunkOrigin + localPos;
}

bool CoordinateUtils::isValidLocalPos(const glm::ivec3& localPos) {
    return localPos.x >= 0 && localPos.x < CHUNK_SIZE &&
           localPos.y >= 0 && localPos.y < CHUNK_SIZE &&
           localPos.z >= 0 && localPos.z < CHUNK_SIZE;
}

bool CoordinateUtils::isValidSubcubePos(const glm::ivec3& subcubePos) {
    return subcubePos.x >= 0 && subcubePos.x < SUBCUBE_DIVISIONS &&
           subcubePos.y >= 0 && subcubePos.y < SUBCUBE_DIVISIONS &&
           subcubePos.z >= 0 && subcubePos.z < SUBCUBE_DIVISIONS;
}

float CoordinateUtils::chunkDistance(const glm::ivec3& coord1, const glm::ivec3& coord2) {
    glm::ivec3 delta = coord2 - coord1;
    return std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z) * CHUNK_SIZE;
}

float CoordinateUtils::cubeDistance(const glm::ivec3& world1, const glm::ivec3& world2) {
    glm::ivec3 delta = world2 - world1;
    return std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
}

} // namespace Utils
} // namespace VulkanCube
```

**Usage:**
```cpp
#include "utils/CoordinateUtils.h"

// In Chunk
glm::ivec3 localPos = Utils::CoordinateUtils::worldToLocal(worldPos, worldOrigin);

// In ChunkManager
glm::ivec3 chunkCoord = Utils::CoordinateUtils::worldToChunkCoord(worldPos);
Chunk* chunk = getChunkAt(chunkCoord);
```

---

## Migration Checklist Template

Use this for each refactoring:

### Pre-Refactoring
- [ ] Create feature branch: `refactor/[module-name]`
- [ ] Document current behavior (write tests if needed)
- [ ] Identify all dependencies with grep:
  ```bash
  grep -r "ClassName" src/ include/
  ```
- [ ] Create new files: `include/[category]/[Module].h`, `src/[category]/[Module].cpp`
- [ ] Add to CMakeLists.txt

### During Refactoring
- [ ] Copy code to new files
- [ ] Update namespaces
- [ ] Add forward declarations
- [ ] Update includes in old file
- [ ] Replace old code with new class usage
- [ ] Compile frequently (`cmake --build build`)
- [ ] Fix compilation errors incrementally

### Post-Refactoring
- [ ] All files compile without warnings
- [ ] Run existing tests: `ctest --test-dir build`
- [ ] Test in game (visual validation)
- [ ] Check for performance regressions
- [ ] Update documentation
- [ ] Code review
- [ ] Merge to main

---

## Testing Strategy

### Unit Testing New Modules

**Example: WindowManager Test**
```cpp
#include "ui/WindowManager.h"
#include <gtest/gtest.h>

TEST(WindowManagerTest, InitializeCreatesWindow) {
    UI::WindowManager manager;
    ASSERT_TRUE(manager.initialize(800, 600, "Test"));
    EXPECT_NE(manager.getHandle(), nullptr);
    EXPECT_EQ(manager.getWidth(), 800);
    EXPECT_EQ(manager.getHeight(), 600);
}

TEST(WindowManagerTest, ResizeCallbackFires) {
    UI::WindowManager manager;
    manager.initialize(800, 600, "Test");
    
    bool callbackFired = false;
    manager.setResizeCallback([&](int w, int h) {
        callbackFired = true;
    });
    
    // Simulate resize
    glfwSetWindowSize(manager.getHandle(), 1024, 768);
    glfwPollEvents();
    
    EXPECT_TRUE(callbackFired);
    EXPECT_EQ(manager.getWidth(), 1024);
}
```

### Integration Testing

**Example: Camera + Input Integration**
```cpp
TEST(CameraIntegrationTest, MovementChangesPosition) {
    Scene::CameraController camera;
    camera.setPosition(glm::vec3(0, 0, 0));
    
    glm::vec3 initialPos = camera.getPosition();
    camera.moveForward(1.0f);  // 1 second at default speed
    
    EXPECT_NE(camera.getPosition(), initialPos);
    EXPECT_GT(camera.getPosition().z, initialPos.z);  // Moved forward (negative Z)
}
```

---

## CMakeLists.txt Updates

After creating new modules:

```cmake
# In CMakeLists.txt

# Add new source files
set(SOURCES
    # Existing files...
    
    # UI Module
    src/ui/WindowManager.cpp
    
    # Scene Module  
    src/scene/CameraController.cpp
    
    # Utils Module
    src/utils/CoordinateUtils.cpp
    
    # Physics Module
    src/physics/ChunkPhysicsManager.cpp
)

# Headers are usually auto-discovered, but can be explicit
set(HEADERS
    # Existing...
    
    include/ui/WindowManager.h
    include/scene/CameraController.h
    include/utils/CoordinateUtils.h
    include/physics/ChunkPhysicsManager.h
)

# Rest of CMake configuration unchanged
```

---

## Common Pitfalls & Solutions

### Pitfall 1: Circular Dependencies
**Problem:** Module A includes Module B, Module B includes Module A

**Solution:** Use forward declarations
```cpp
// In ModuleA.h
class ModuleB; // Forward declaration

class ModuleA {
    ModuleB* moduleB; // Pointer only
};

// In ModuleA.cpp
#include "ModuleB.h" // Full include only in .cpp
```

### Pitfall 2: Header Bloat
**Problem:** New header includes 20 other headers

**Solution:** Minimize includes in headers
```cpp
// BAD: In .h file
#include <vector>
#include <glm/glm.hpp>
#include "ChunkManager.h"
#include "PhysicsWorld.h"

// GOOD: In .h file
#include <glm/fwd.hpp>  // GLM forward declarations
class ChunkManager;      // Forward declaration
class PhysicsWorld;      // Forward declaration

// In .cpp file - full includes
#include <vector>
#include "ChunkManager.h"
#include "PhysicsWorld.h"
```

### Pitfall 3: Breaking Existing Code
**Problem:** Refactoring changes behavior subtly

**Solution:** Characterization tests before refactoring
```cpp
// Write test that captures current behavior FIRST
TEST(ChunkTest, ExistingBehaviorTest) {
    Chunk chunk(glm::ivec3(0));
    chunk.addCube(glm::ivec3(5, 5, 5), glm::vec3(1, 0, 0));
    
    // Document exact current behavior
    EXPECT_EQ(chunk.getCubeCount(), 1);
    EXPECT_NE(chunk.getCubeAt(glm::ivec3(5, 5, 5)), nullptr);
    
    // After refactoring, this test should still pass
}
```

---

## Success Metrics

Track these metrics before and after each refactoring:

### Compilation Metrics
- **Full rebuild time:** `cmake --build build --clean-first` (timed)
- **Incremental build time:** Change one file, rebuild
- **Target:** <30s full rebuild, <5s incremental

### Code Metrics
- **Lines per file:** `wc -l src/**/*.cpp`
- **Target:** Average <500 lines
- **Cyclomatic complexity:** Use tools like `lizard`
- **Target:** <15 per function

### Dependency Metrics
- **Include depth:** How many files are transitively included
- **Tool:** `clang -H` or `include-what-you-use`
- **Target:** <20 includes per file

---

## Next Steps

1. **Start with Quick Win:** Extract WindowManager (2 hours)
2. **Validate approach:** Test, benchmark, code review
3. **Document learnings:** What worked, what didn't
4. **Continue incrementally:** One module per week
5. **Track metrics:** Ensure improvements are measurable

Remember: **Incremental refactoring is safer than big rewrites!**
