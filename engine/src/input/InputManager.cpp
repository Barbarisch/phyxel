#include "input/InputManager.h"
#include "core/GameSettings.h"
#include "utils/Logger.h"
#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iomanip>

namespace Phyxel {
namespace Input {

InputManager::InputManager()
    : cameraPos(50.0f, 50.0f, 50.0f)
    , cameraFront(0.0f, 0.0f, -1.0f)
    , cameraUp(0.0f, 1.0f, 0.0f)
    , yaw(-90.0f)
    , pitch(0.0f)
    , lastX(400.0f)
    , lastY(300.0f)
    , currentMouseX(0.0)
    , currentMouseY(0.0)
    , mouseDeltaX(0.0f)
    , mouseDeltaY(0.0f)
    , firstMouse(true)
    , mouseCaptured(false)
    , mouseSensitivity(0.3f)
    , cameraSpeed(5.0f)
    , window(nullptr)
{
    // Seed the action map with engine defaults so gameplay queries
    // (isActionPressed) work before any settings file is loaded — the editor
    // relies on this. Standalone hosts override per-binding via bindAction.
    seedDefaultActionBindings();
    LOG_INFO("InputManager", "InputManager created");
}

InputManager::~InputManager() {
    cleanup();
}

bool InputManager::initialize(GLFWwindow* windowHandle) {
    if (!windowHandle) {
        LOG_ERROR("InputManager", "Cannot initialize with null window handle");
        return false;
    }
    
    window = windowHandle;
    
    // Set this InputManager as the window user pointer for callbacks
    // glfwSetWindowUserPointer(window, this);
    
    // Register GLFW callbacks
    // glfwSetCursorPosCallback(window, mouseCallbackStatic);
    // glfwSetMouseButtonCallback(window, mouseButtonCallbackStatic);
    
    LOG_INFO("InputManager", "InputManager initialized successfully");
    return true;
}

void InputManager::cleanup() {
    if (window) {
        // Clear callbacks
        glfwSetCursorPosCallback(window, nullptr);
        glfwSetMouseButtonCallback(window, nullptr);
        window = nullptr;
    }
    
    keyActions.clear();
    mouseActions.clear();
    keyPressed.clear();
    
    LOG_INFO("InputManager", "InputManager cleaned up");
}

void InputManager::processInput(float deltaTime) {
    if (!window) return;
    
    // Process camera movement (WASD + Space/Shift)
    processCameraMovement(deltaTime);
    
    // Process keyboard actions (function keys, etc.)
    processKeyboardActions();
}

void InputManager::processCameraMovement(float deltaTime) {
    if (scriptingConsoleMode) return;
    if (ImGui::GetIO().WantCaptureKeyboard) return;

    float speed = cameraSpeed * deltaTime;

    // Forward/Backward (W/S)
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        cameraPos += speed * cameraFront;
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
        cameraPos -= speed * cameraFront;
    }

    // Left/Right (A/D)
    glm::vec3 right = glm::normalize(glm::cross(cameraFront, cameraUp));
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
        cameraPos -= right * speed;
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        cameraPos += right * speed;
    }

    // Up/Down (Space/Z)
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
        cameraPos += cameraUp * speed;
    }
    if (glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS) {
        cameraPos -= cameraUp * speed;
    }
}

void InputManager::processKeyboardActions() {
    // When ImGui wants keyboard input (e.g. text fields), skip game key actions
    // except ESC which should always work for closing dialogues/menus.
    bool imguiWantsKeyboard = ImGui::GetIO().WantCaptureKeyboard;

    // Process all registered key actions
    for (auto& [keyCombo, action] : keyActions) {
        // In scripting console mode, only allow the toggle key (Grave Accent)
        if (scriptingConsoleMode && keyCombo.key != GLFW_KEY_GRAVE_ACCENT) {
            continue;
        }

        // When ImGui captures keyboard, only allow ESC
        if (imguiWantsKeyboard && keyCombo.key != GLFW_KEY_ESCAPE) {
            continue;
        }

        int keyState = glfwGetKey(window, keyCombo.key);
        
        // Check if key is pressed
        if (keyState == GLFW_PRESS) {
            // Check if this is a new press (not a repeat)
            if (!keyPressed[keyCombo]) {
                int currentMods = currentModifiers();

                // Check if modifiers match exactly
                if (currentMods == keyCombo.modifiers && action.callback) {
                    LOG_DEBUG("InputManager", "Action triggered: {}", action.name);
                    action.callback();
                }
                
                keyPressed[keyCombo] = true;
            }
        } else {
            // Key released
            keyPressed[keyCombo] = false;
        }
    }
}

void InputManager::mouseCallbackStatic(GLFWwindow* window, double xpos, double ypos) {
    InputManager* manager = reinterpret_cast<InputManager*>(glfwGetWindowUserPointer(window));
    if (manager) {
        manager->handleMouseMove(xpos, ypos);
    }
}

void InputManager::handleMouseMove(double xpos, double ypos) {
    // Update current mouse position
    currentMouseX = xpos;
    currentMouseY = ypos;
    
    // Notify external systems (e.g., mouse velocity tracker)
    if (mousePositionCallback) {
        mousePositionCallback(xpos, ypos);
    }
    
    // Only process mouse movement for camera if right mouse button is held
    if (!mouseCaptured) {
        lastX = xpos;
        lastY = ypos;
        return;
    }
    
    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
        return;
    }
    
    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos; // Reversed since y-coordinates go from bottom to top
    if (invertY_) yoffset = -yoffset;  // settings "Invert Y" (look up/down flip)

    lastX = xpos;
    lastY = ypos;

    mouseDeltaX += xoffset;
    mouseDeltaY += yoffset;

    xoffset *= mouseSensitivity;
    yoffset *= mouseSensitivity;
    
    yaw += xoffset;
    pitch += yoffset;
    
    // Debug output (periodic)
    static int debugCounter = 0;
    if (++debugCounter % 10 == 0 && mouseCaptured) {
        LOG_INFO("InputManager", "Camera look: yaw={:.1f}° pitch={:.1f}° (delta: {:.2f}, {:.2f})", yaw, pitch, xoffset, yoffset);
    }
    
    // Constrain pitch
    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;
    
    // Update camera vectors
    updateCameraVectors();
}

void InputManager::mouseButtonCallbackStatic(GLFWwindow* window, int button, int action, int mods) {
    InputManager* manager = reinterpret_cast<InputManager*>(glfwGetWindowUserPointer(window));
    if (manager) {
        manager->handleMouseButton(button, action, mods);
    }
}

void InputManager::handleMouseButton(int button, int action, int mods) {
    if (scriptingConsoleMode) return;

    // Handle right mouse button for camera rotation
    // Allow when viewport is hovered (even though ImGui "wants" the mouse for the viewport window)
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        bool mouseOverViewport = m_viewportHovered || !ImGui::GetIO().WantCaptureMouse;
        if (action == GLFW_PRESS && mouseOverViewport) {
            mouseCaptured = true;
            firstMouse = true; // Reset to avoid jump
            LOG_INFO("InputManager", "*** RIGHT MOUSE PRESSED - CAMERA LOOK MODE ENABLED ***");
        } else if (action == GLFW_RELEASE) {
            mouseCaptured = false;
            LOG_INFO("InputManager", "*** RIGHT MOUSE RELEASED - CAMERA LOOK MODE DISABLED ***");
        }
    }

    // Process registered mouse actions — allow when viewport hovered or ImGui doesn't want mouse
    bool mouseAvailable = m_viewportHovered || !ImGui::GetIO().WantCaptureMouse;
    if (action == GLFW_PRESS && mouseAvailable) {
        // Look for exact match with button and modifiers
        MouseButtonKey key{button, mods};
        auto it = mouseActions.find(key);

        if (it != mouseActions.end()) {
            const MouseAction& mouseAction = it->second;
            if (mouseAction.callback) {
                LOG_DEBUG("InputManager", "Mouse action triggered: {}", mouseAction.name);
                mouseAction.callback();
            }
        }
    }
}

void InputManager::updateCameraVectors() {
    glm::vec3 front;
    front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    front.y = sin(glm::radians(pitch));
    front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    cameraFront = glm::normalize(front);
}

void InputManager::setCameraPosition(const glm::vec3& pos) {
    cameraPos = pos;
}

void InputManager::setCameraFront(const glm::vec3& front) {
    cameraFront = glm::normalize(front);
}

void InputManager::getCurrentMousePosition(double& x, double& y) const {
    x = currentMouseX;
    y = currentMouseY;
}

void InputManager::setMousePositionCallback(MousePositionCallback callback) {
    mousePositionCallback = callback;
}

void InputManager::registerAction(int key, const std::string& name, ActionCallback callback) {
    KeyboardKey keyCombo{key, 0};
    keyActions[keyCombo] = KeyAction{name, 0, callback};
    LOG_DEBUG("InputManager", "Registered action '{}' for key {}", name, key);
}

void InputManager::registerActionWithModifier(int key, int modifiers, const std::string& name, ActionCallback callback) {
    KeyboardKey keyCombo{key, modifiers};
    keyActions[keyCombo] = KeyAction{name, modifiers, callback};
    LOG_DEBUG("InputManager", "Registered action '{}' for key {} with modifiers", name, key);
}

void InputManager::registerMouseAction(int button, int modifiers, const std::string& name, ActionCallback callback) {
    MouseButtonKey key{button, modifiers};
    mouseActions[key] = MouseAction{name, modifiers, callback};
    LOG_DEBUG("InputManager", "Registered mouse action '{}' for button {} with modifiers {}", name, button, modifiers);
}

// ---------------------------------------------------------------------------
// Action map (rebindable keys)
// ---------------------------------------------------------------------------

void InputManager::seedDefaultActionBindings() {
    for (const auto& kb : Core::GameSettings::defaultKeybindings()) {
        actionBindings_[kb.action] = KeyboardKey{kb.key, kb.modifiers};
    }
}

void InputManager::bindAction(const std::string& action, int key, int modifiers) {
    actionBindings_[action] = KeyboardKey{key, modifiers};
    LOG_DEBUG("InputManager", "Bound action '{}' -> key {} (mods {})", action, key, modifiers);
}

void InputManager::clearActionBindings() {
    actionBindings_.clear();
}

bool InputManager::isActionPressed(const std::string& action) const {
    auto it = actionBindings_.find(action);
    if (it == actionBindings_.end()) return false;
    // Reuse isKeyPressed for the scripting-console / ImGui gating. Modifiers are
    // intentionally not enforced for level queries (see header).
    return isKeyPressed(it->second.key);
}

int InputManager::getActionKey(const std::string& action) const {
    auto it = actionBindings_.find(action);
    return it == actionBindings_.end() ? GLFW_KEY_UNKNOWN : it->second.key;
}

int InputManager::getActionModifiers(const std::string& action) const {
    auto it = actionBindings_.find(action);
    return it == actionBindings_.end() ? 0 : it->second.modifiers;
}

int InputManager::currentModifiers() const {
    if (!window) return 0;
    int mods = 0;
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS) mods |= GLFW_MOD_CONTROL;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) mods |= GLFW_MOD_SHIFT;
    if (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS) mods |= GLFW_MOD_ALT;
    return mods;
}

int InputManager::scanPressedKey() const {
    if (!window) return GLFW_KEY_UNKNOWN;
    // Curated set of capturable keys — mirrors what GameSettings::keyToString /
    // stringToKey can name, so a captured key always round-trips to settings.json.
    // Modifier keys are included (Sprint/Crouch bind to Shift/Ctrl by default).
    static const int kCapturable[] = {
        GLFW_KEY_A, GLFW_KEY_B, GLFW_KEY_C, GLFW_KEY_D, GLFW_KEY_E, GLFW_KEY_F,
        GLFW_KEY_G, GLFW_KEY_H, GLFW_KEY_I, GLFW_KEY_J, GLFW_KEY_K, GLFW_KEY_L,
        GLFW_KEY_M, GLFW_KEY_N, GLFW_KEY_O, GLFW_KEY_P, GLFW_KEY_Q, GLFW_KEY_R,
        GLFW_KEY_S, GLFW_KEY_T, GLFW_KEY_U, GLFW_KEY_V, GLFW_KEY_W, GLFW_KEY_X,
        GLFW_KEY_Y, GLFW_KEY_Z,
        GLFW_KEY_0, GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4,
        GLFW_KEY_5, GLFW_KEY_6, GLFW_KEY_7, GLFW_KEY_8, GLFW_KEY_9,
        GLFW_KEY_F1, GLFW_KEY_F2, GLFW_KEY_F3, GLFW_KEY_F4, GLFW_KEY_F5, GLFW_KEY_F6,
        GLFW_KEY_F7, GLFW_KEY_F8, GLFW_KEY_F9, GLFW_KEY_F10, GLFW_KEY_F11, GLFW_KEY_F12,
        GLFW_KEY_SPACE, GLFW_KEY_ESCAPE, GLFW_KEY_ENTER, GLFW_KEY_TAB, GLFW_KEY_BACKSPACE,
        GLFW_KEY_LEFT_SHIFT, GLFW_KEY_RIGHT_SHIFT, GLFW_KEY_LEFT_CONTROL, GLFW_KEY_RIGHT_CONTROL,
        GLFW_KEY_LEFT_ALT, GLFW_KEY_RIGHT_ALT,
        GLFW_KEY_UP, GLFW_KEY_DOWN, GLFW_KEY_LEFT, GLFW_KEY_RIGHT,
        GLFW_KEY_DELETE, GLFW_KEY_INSERT, GLFW_KEY_HOME, GLFW_KEY_END,
        GLFW_KEY_PAGE_UP, GLFW_KEY_PAGE_DOWN, GLFW_KEY_GRAVE_ACCENT, GLFW_KEY_MINUS,
        GLFW_KEY_EQUAL, GLFW_KEY_LEFT_BRACKET, GLFW_KEY_RIGHT_BRACKET, GLFW_KEY_SEMICOLON,
        GLFW_KEY_APOSTROPHE, GLFW_KEY_COMMA, GLFW_KEY_PERIOD, GLFW_KEY_SLASH, GLFW_KEY_BACKSLASH,
    };
    for (int key : kCapturable) {
        if (glfwGetKey(window, key) == GLFW_PRESS) return key;
    }
    return GLFW_KEY_UNKNOWN;
}

void InputManager::setScriptingConsoleMode(bool enabled) {
    scriptingConsoleMode = enabled;
    if (enabled) {
        // Reset mouse capture state to prevent getting stuck in look mode
        mouseCaptured = false;
        firstMouse = true;
    }
    
    // Ensure cursor is visible in both modes as requested
    if (window) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}

void InputManager::setYawPitch(float newYaw, float newPitch) {
    yaw = newYaw;
    pitch = newPitch;
    
    // Constrain pitch
    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;
    
    updateCameraVectors();
}

bool InputManager::isKeyPressed(int key) const {
    if (!window) return false;
    // If scripting console is open, block all key queries to prevent game input leakage
    // Exception: We might want to allow some keys, but generally the console consumes input.
    // The toggle key (Grave Accent) is handled via registerAction/processKeyboardActions which bypasses this.
    if (scriptingConsoleMode) return false;
    if (ImGui::GetIO().WantCaptureKeyboard) return false;

    return glfwGetKey(window, key) == GLFW_PRESS;
}

bool InputManager::isMouseButtonPressed(int button) const {
    if (!window) return false;
    // If scripting console is open, block mouse button queries
    if (scriptingConsoleMode) return false;
    // Same rule as the mouse-callback path (see handleMouseButton): the
    // editor's 3D view lives inside an ImGui viewport window, so ImGui
    // "wants" the mouse whenever the cursor is over it — but gameplay clicks
    // there must still register (attack/heavy via the control schemes).
    // Only block when a real UI panel has the mouse.
    if (ImGui::GetIO().WantCaptureMouse && !m_viewportHovered) return false;

    return glfwGetMouseButton(window, button) == GLFW_PRESS;
}

} // namespace Input
} // namespace Phyxel
