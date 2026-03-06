#pragma once

#include <glm/glm.hpp>
#include <GLFW/glfw3.h>
#include <functional>
#include <unordered_map>
#include <string>

namespace Phyxel {
namespace Input {

/**
 * @brief Manages keyboard, mouse input, and camera controls
 * 
 * InputManager centralizes all input handling and camera state management.
 * It processes GLFW input events, updates camera position/rotation, and
 * provides a callback system for game actions.
 */
class InputManager {
public:
    // Callback types for application-level actions
    using ActionCallback = std::function<void()>;
    using MousePositionCallback = std::function<void(double, double)>;
    
    InputManager();
    ~InputManager();
    
    // Initialization
    bool initialize(GLFWwindow* window);
    void cleanup();
    
    // Main update function (called each frame with deltaTime)
    void processInput(float deltaTime);
    
    // Camera access (read-only for rendering)
    const glm::vec3& getCameraPosition() const { return cameraPos; }
    const glm::vec3& getCameraFront() const { return cameraFront; }
    const glm::vec3& getCameraUp() const { return cameraUp; }
    float getYaw() const { return yaw; }
    float getPitch() const { return pitch; }
    
    // Camera control
    void setCameraPosition(const glm::vec3& pos);
    void setCameraFront(const glm::vec3& front);
    void setCameraSpeed(float speed) { cameraSpeed = speed; }
    
    // Mouse state access (for hover detection, etc.)
    void getCurrentMousePosition(double& x, double& y) const;
    bool isMouseCaptured() const { return mouseCaptured; }
    
    // Mouse position update callback (for external systems like mouse velocity tracker)
    void setMousePositionCallback(MousePositionCallback callback);

    // Query state
    bool isKeyPressed(int key) const;
    bool isMouseButtonPressed(int button) const;
    glm::vec2 getMouseDelta() const { return glm::vec2(mouseDeltaX, mouseDeltaY); }
    void resetMouseDelta() { mouseDeltaX = 0; mouseDeltaY = 0; }
    
    // Action registration (Application registers what happens on key press)
    void registerAction(int key, const std::string& name, ActionCallback callback);
    void registerActionWithModifier(int key, int modifiers, const std::string& name, ActionCallback callback);
    
    // Mouse button action registration
    void registerMouseAction(int button, int modifiers, const std::string& name, ActionCallback callback);
    
    // Direct camera orientation (for testing/debugging)
    void setYawPitch(float yaw, float pitch);

    // Scripting Console Mode
    void setScriptingConsoleMode(bool enabled);
    bool isScriptingConsoleMode() const { return scriptingConsoleMode; }
    
    // Instance callback handlers (public for WindowManager delegation)
    void handleMouseMove(double xpos, double ypos);
    void handleMouseButton(int button, int action, int mods);
    
private:
    // GLFW callbacks (static, redirect to instance)
    static void mouseCallbackStatic(GLFWwindow* window, double xpos, double ypos);
    static void mouseButtonCallbackStatic(GLFWwindow* window, int button, int action, int mods);
    
    // Input processing helpers
    void processCameraMovement(float deltaTime);
    void processKeyboardActions();
    
    // Update camera front vector from yaw/pitch
    void updateCameraVectors();
    
    // Camera state
    glm::vec3 cameraPos;
    glm::vec3 cameraFront;
    glm::vec3 cameraUp;
    float yaw;
    float pitch;
    
    // Mouse state
    double lastX, lastY;
    double currentMouseX, currentMouseY;
    float mouseDeltaX, mouseDeltaY;
    bool firstMouse;
    bool mouseCaptured;
    float mouseSensitivity;
    
    // Camera movement
    float cameraSpeed;
    
    // Action callbacks
    struct KeyAction {
        std::string name;
        int modifiers; // GLFW modifier flags (0 for none)
        ActionCallback callback;
    };

    bool scriptingConsoleMode = false;
    
    // Helper to create unique key for keyboard key+modifiers combination
    struct KeyboardKey {
        int key;
        int modifiers;
        
        bool operator==(const KeyboardKey& other) const {
            return key == other.key && modifiers == other.modifiers;
        }
    };
    
    // Hash function for KeyboardKey
    struct KeyboardKeyHash {
        std::size_t operator()(const KeyboardKey& k) const {
            return std::hash<int>()(k.key) ^ (std::hash<int>()(k.modifiers) << 1);
        }
    };
    
    std::unordered_map<KeyboardKey, KeyAction, KeyboardKeyHash> keyActions;
    
    // Input state tracking (for key repeat prevention)
    std::unordered_map<KeyboardKey, bool, KeyboardKeyHash> keyPressed;
    
    struct MouseAction {
        std::string name;
        int modifiers;
        ActionCallback callback;
    };
    
    // Helper to create unique key for button+modifiers combination
    struct MouseButtonKey {
        int button;
        int modifiers;
        
        bool operator==(const MouseButtonKey& other) const {
            return button == other.button && modifiers == other.modifiers;
        }
    };
    
    // Hash function for MouseButtonKey
    struct MouseButtonKeyHash {
        std::size_t operator()(const MouseButtonKey& k) const {
            return std::hash<int>()(k.button) ^ (std::hash<int>()(k.modifiers) << 1);
        }
    };
    
    std::unordered_map<MouseButtonKey, MouseAction, MouseButtonKeyHash> mouseActions;
    
    // Mouse position callback
    MousePositionCallback mousePositionCallback;
    
    // Window handle
    GLFWwindow* window;
};

} // namespace Input
} // namespace Phyxel
