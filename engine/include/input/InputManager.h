#pragma once

#include <glm/glm.hpp>
#include <GLFW/glfw3.h>
#include <functional>
#include <unordered_map>
#include <string>
#include <vector>

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
    // Force always-on mouse look (FPS-style) without holding the right button.
    // Standalone games enable this during play; the editor leaves it RMB-gated.
    void setMouseCaptured(bool captured) {
        if (captured && !mouseCaptured) firstMouse = true; // avoid a jump on enable
        mouseCaptured = captured;
    }

    // Mouse position update callback (for external systems like mouse velocity tracker)
    void setMousePositionCallback(MousePositionCallback callback);

    // Invert vertical mouse look (settings "Invert Y").
    void setInvertY(bool invert) { invertY_ = invert; }
    bool getInvertY() const { return invertY_; }

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

    // --- Action map (rebindable keys) ----------------------------------------
    // InputManager is the single source of truth for action -> key. Gameplay
    // queries stay rebind-aware by asking isActionPressed("MoveForward") instead
    // of hardcoding GLFW_KEY_W. The map is seeded with
    // Core::GameSettings::defaultKeybindings() at construction, so it is always
    // valid (the editor works with no config); standalone hosts override each
    // binding from loaded settings via bindAction().
    void bindAction(const std::string& action, int key, int modifiers = 0);
    void clearActionBindings();

    // Level-triggered: is the key bound to `action` currently held? Mirrors the
    // gating of isKeyPressed (false in scripting-console mode / when ImGui wants
    // the keyboard). Modifiers are stored for display/rebind but NOT enforced
    // here — so Shift+W still counts as MoveForward while Sprint is held.
    bool isActionPressed(const std::string& action) const;

    // Current binding for an action (rebind UI / display). Returns
    // GLFW_KEY_UNKNOWN / 0 when the action is unbound.
    int getActionKey(const std::string& action) const;
    int getActionModifiers(const std::string& action) const;

    // Raw scan for the FIRST capturable key currently held — used by the
    // rebind key-capture UI. Deliberately bypasses the scripting-console / ImGui
    // gating that isKeyPressed applies (capture is an explicit user intent).
    // Modifier keys (Shift/Ctrl/Alt) ARE capturable, so Sprint can bind to Shift.
    // Returns GLFW_KEY_UNKNOWN when nothing is held.
    int scanPressedKey() const;

    // GLFW_MOD_* flags for the modifier keys currently held (raw glfw query).
    int currentModifiers() const;

    // Direct camera orientation (for testing/debugging)
    void setYawPitch(float yaw, float pitch);

    // Scripting Console Mode
    void setScriptingConsoleMode(bool enabled);
    bool isScriptingConsoleMode() const { return scriptingConsoleMode; }

    // Viewport hover state — set by editor each frame so input gating can
    // distinguish "mouse over the 3D viewport" from "mouse over an ImGui panel".
    void setViewportHovered(bool hovered) { m_viewportHovered = hovered; }
    bool isViewportHovered() const { return m_viewportHovered; }
    
    // Instance callback handlers (public for WindowManager delegation)
    void handleMouseMove(double xpos, double ypos);
    void handleMouseButton(int button, int action, int mods);
    // Text input: GLFW char callback delivers a typed Unicode codepoint. Buffered
    // per frame for the UISystem's text widgets; cleared each frame (clearTypedChars).
    void handleChar(unsigned int codepoint) { typedChars_.push_back(codepoint); }
    const std::vector<unsigned int>& getTypedChars() const { return typedChars_; }
    void clearTypedChars() { typedChars_.clear(); }
    
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
    bool m_viewportHovered = false;
    
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

    // Action name -> bound key+modifiers (rebindable). See bindAction /
    // isActionPressed. Seeded from Core::GameSettings::defaultKeybindings().
    std::unordered_map<std::string, KeyboardKey> actionBindings_;
    void seedDefaultActionBindings();

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

    // Typed Unicode codepoints this frame (text input); cleared per frame.
    std::vector<unsigned int> typedChars_;
    bool invertY_ = false;  // settings "Invert Y"

    // Window handle
    GLFWwindow* window;
};

} // namespace Input
} // namespace Phyxel
