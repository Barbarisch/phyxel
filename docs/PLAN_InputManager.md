# InputManager Extraction - Implementation Plan

**Created:** November 9, 2025  
**Estimated Time:** 6 hours  
**Complexity:** Medium  
**Risk:** Medium (touches core game loop)

---

## Overview

Extract all input handling and camera control from Application.cpp into a dedicated `input::InputManager` class. This will centralize keyboard, mouse, and camera logic while maintaining the current gameplay functionality.

---

## Scope Analysis

### Functions to Extract (~300 lines total)

#### 1. **processInput()** (~150 lines)
**Current responsibilities:**
- ESC key → Exit application
- F1 key → Toggle performance overlay
- F2 key → Save world to database
- F3 key → Toggle force system debug
- T key → Test frustum culling positions (camera teleport)
- G key → Spawn test dynamic subcube
- C key → Place new cube
- P key → Debug coordinate system
- O key → Toggle breaking forces
- WASD → Camera movement (forward/back/left/right)
- Space/Shift → Camera vertical movement (up/down)

**Refactoring strategy:**
- Keep action bindings (what keys do) in InputManager
- Delegate actual actions to Application or other systems
- Use callback/event system for key actions

#### 2. **mouseCallback()** (~58 lines)
**Current responsibilities:**
- Track current mouse position (for hover detection)
- Update mouse velocity tracker (for force calculations)
- Camera rotation when right mouse button held
- Yaw/Pitch calculation and constraints
- Update camera front vector

**Refactoring strategy:**
- Move all camera rotation logic to InputManager
- Keep mouse tracking for external systems
- Expose camera state through getters

#### 3. **mouseButtonCallback()** (~42 lines)
**Current responsibilities:**
- Right mouse button → Toggle camera look mode
- Left click → Break cube/subcube (delegates to breakHovered*)
- Left click + Ctrl → Subdivide cube
- Middle click → Subdivide cube

**Refactoring strategy:**
- Keep button state tracking in InputManager
- Use callbacks to notify Application of actions
- Decouple input detection from game logic

---

## Camera State

### Data Currently in Application.h:
```cpp
// Camera variables
glm::vec3 cameraPos = glm::vec3(50.0f, 50.0f, 50.0f);
glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);

// Mouse and camera control
float yaw = -90.0f;
float pitch = 0.0f;
float lastX = 400.0f;
float lastY = 300.0f;
bool firstMouse = true;
bool mouseCaptured = false; // For right-click camera rotation

// Current mouse position (for hover detection)
double currentMouseX = 0.0;
double currentMouseY = 0.0;
```

### Decision: Camera Ownership
**Option A:** InputManager owns camera state (position, rotation)
- ✅ Cleaner separation
- ✅ Camera fully encapsulated
- ❌ Application needs camera for rendering (getters needed)

**Option B:** Application owns camera, InputManager updates it
- ✅ Application maintains control
- ❌ Tighter coupling
- ❌ InputManager needs camera reference

**Recommendation:** **Option A** - InputManager owns camera
- Provide read-only access through getters
- Future: Extract Camera class separately if needed

---

## Architecture Design

### Proposed Class Structure

```cpp
namespace VulkanCube {
namespace Input {

class InputManager {
public:
    // Callbacks for application-level actions
    using ActionCallback = std::function<void()>;
    
    // Constructor/Destructor
    InputManager();
    ~InputManager();
    
    // Initialization
    bool initialize(GLFWwindow* window);
    void cleanup();
    
    // Main update function (called each frame)
    void processInput(float deltaTime);
    
    // Camera access (read-only for rendering)
    const glm::vec3& getCameraPosition() const { return cameraPos; }
    const glm::vec3& getCameraFront() const { return cameraFront; }
    const glm::vec3& getCameraUp() const { return cameraUp; }
    
    // Mouse state access (for hover detection)
    void getCurrentMousePosition(double& x, double& y) const;
    bool isMouseCaptured() const { return mouseCaptured; }
    
    // Action registration (Application can register what happens on key press)
    void registerAction(int key, const std::string& name, ActionCallback callback);
    
    // Camera control
    void setCameraPosition(const glm::vec3& pos);
    void setCameraSpeed(float speed) { cameraSpeed = speed; }
    
private:
    // GLFW callbacks (static, redirect to instance)
    static void mouseCallbackStatic(GLFWwindow* window, double xpos, double ypos);
    static void mouseButtonCallbackStatic(GLFWwindow* window, int button, int action, int mods);
    
    // Instance callback handlers
    void handleMouseMove(double xpos, double ypos);
    void handleMouseButton(int button, int action, int mods);
    
    // Camera state
    glm::vec3 cameraPos;
    glm::vec3 cameraFront;
    glm::vec3 cameraUp;
    float yaw;
    float pitch;
    
    // Mouse state
    double lastX, lastY;
    double currentMouseX, currentMouseY;
    bool firstMouse;
    bool mouseCaptured;
    float mouseSensitivity;
    float cameraSpeed;
    
    // Input state tracking (for key repeat prevention)
    std::unordered_map<int, bool> keyStates;
    
    // Action callbacks
    struct KeyAction {
        std::string name;
        ActionCallback callback;
    };
    std::unordered_map<int, KeyAction> registeredActions;
    
    // Window handle
    GLFWwindow* window;
};

} // namespace Input
} // namespace VulkanCube
```

---

## Implementation Steps

### Step 1: Create InputManager Class (1 hour)
1. Create `include/input/InputManager.h`
2. Create `src/input/InputManager.cpp`
3. Implement basic structure and constructor
4. Add camera state members
5. Add mouse state members

### Step 2: Move Camera Logic (1.5 hours)
1. Copy camera initialization from Application
2. Implement camera movement (WASD + Space/Shift)
3. Implement camera rotation (mouse look)
4. Add camera getters for Application

### Step 3: Move Mouse Callbacks (1 hour)
1. Implement static callback redirectors
2. Move mouseCallback logic
3. Move mouseButtonCallback logic
4. Register callbacks with GLFW

### Step 4: Implement Action System (1.5 hours)
1. Create action registration mechanism
2. Move keyboard input handling
3. Implement key state tracking (prevent repeats)
4. Register all actions from Application

### Step 5: Update Application (1 hour)
1. Add InputManager member to Application
2. Remove old input/camera code
3. Register all key actions (F1, F2, G, etc.)
4. Update rendering to use InputManager camera
5. Update mouse hover to use InputManager mouse position

### Step 6: Build, Test, Commit (1 hour)
1. Build and fix any compilation errors
2. Test all input functionality:
   - Camera movement (WASD + Space/Shift)
   - Camera rotation (right mouse)
   - All function keys (F1, F2, F3, etc.)
   - Cube interactions (left click, ctrl+click, middle click)
3. Commit with detailed message

---

## Testing Checklist

### Camera Controls
- [ ] W/A/S/D movement works
- [ ] Space (up) and Shift (down) work
- [ ] Right mouse button enables camera look
- [ ] Mouse movement rotates camera
- [ ] Pitch is constrained to ±89°
- [ ] Camera look disables when right mouse released

### Keyboard Actions
- [ ] ESC exits application
- [ ] F1 toggles performance overlay
- [ ] F2 saves world to database
- [ ] F3 toggles force system debug
- [ ] T cycles through test camera positions
- [ ] G spawns test dynamic subcube
- [ ] C places new cube
- [ ] P prints debug coordinate info
- [ ] O toggles breaking forces

### Mouse Actions
- [ ] Left click breaks cube/subcube
- [ ] Ctrl+Left click subdivides cube
- [ ] Middle click subdivides cube
- [ ] Mouse hover detection still works

---

## Potential Challenges

### Challenge 1: Action Callbacks
**Problem:** Application methods need to be called from InputManager  
**Solution:** Use std::function callbacks or delegate pattern

### Challenge 2: Delta Time
**Problem:** Camera movement needs deltaTime  
**Solution:** Pass deltaTime to processInput()

### Challenge 3: Mouse Velocity Tracker
**Problem:** Application uses mouseVelocityTracker in mouseCallback  
**Solution:** Keep tracker in Application, InputManager notifies position updates

### Challenge 4: Current Hovered Location
**Problem:** Mouse position used for hover detection in Application  
**Solution:** Provide getCurrentMousePosition() getter

---

## Future Enhancements

After this refactoring, we could:

1. **Extract Camera class** separately from InputManager
   - `camera::Camera` with position, orientation, view matrix
   - InputManager updates Camera
   - Renderer uses Camera

2. **Add input mapping/rebinding**
   - Configurable key bindings
   - Support for multiple input profiles

3. **Add gamepad support**
   - Extend InputManager for controller input
   - Unified input abstraction

4. **Event system**
   - Replace callbacks with event queue
   - Better decoupling of systems

---

## Files Affected

### New Files:
- `include/input/InputManager.h` (~100 lines)
- `src/input/InputManager.cpp` (~200 lines)

### Modified Files:
- `include/Application.h` - Add InputManager member, remove input/camera members
- `src/Application.cpp` - Remove processInput/mouseCallback/mouseButtonCallback, register actions
- `CMakeLists.txt` - Auto-detected by GLOB_RECURSE

---

## Success Criteria

✅ All camera controls working identically to before  
✅ All keyboard shortcuts functioning  
✅ All mouse interactions working  
✅ Application.cpp reduced by ~250 lines  
✅ Clean separation of input concerns  
✅ Build successful  
✅ No runtime errors  

---

## Ready to Implement?

This is a meaty refactoring. The plan is solid, but it requires:
- Careful extraction of interconnected logic
- Callback/action system implementation
- Thorough testing of all input modes

**Estimated time:** 6 hours (actual may vary based on complexity)

**Recommendation:** Start fresh with full focus, not at the end of a session!
