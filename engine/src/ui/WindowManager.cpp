#include "ui/WindowManager.h"
#include "utils/Logger.h"
#include <imgui.h>

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
    
    // Register callbacks
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallbackStatic);
    glfwSetCursorPosCallback(window, cursorPosCallbackStatic);
    glfwSetMouseButtonCallback(window, mouseButtonCallbackStatic);
    glfwSetKeyCallback(window, keyCallbackStatic);
    glfwSetScrollCallback(window, scrollCallbackStatic);
    
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

void WindowManager::setCursorVisible(bool visible) {
    if (window) {
        glfwSetInputMode(window, GLFW_CURSOR, visible ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
        LOG_DEBUG("WindowManager", "Cursor visibility set to: {}", visible);
    }
}

void WindowManager::setFullscreen(bool fullscreen) {
    if (!window) return;
    if (fullscreen == fullscreen_) return;

    if (fullscreen) {
        // Save windowed position/size for restore
        glfwGetWindowPos(window, &windowedX_, &windowedY_);
        windowedW_ = width;
        windowedH_ = height;

        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        LOG_INFO("WindowManager", "Switched to fullscreen {}x{}", mode->width, mode->height);
    } else {
        glfwSetWindowMonitor(window, nullptr, windowedX_, windowedY_, windowedW_, windowedH_, 0);
        LOG_INFO("WindowManager", "Switched to windowed {}x{}", windowedW_, windowedH_);
    }
    fullscreen_ = fullscreen;
}

void WindowManager::framebufferResizeCallbackStatic(GLFWwindow* window, int w, int h) {
    auto* manager = reinterpret_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (!manager) return;
    
    manager->width = w;
    manager->height = h;
    manager->resized = true;
    
    LOG_INFO("WindowManager", "Framebuffer resized callback triggered: {}x{}", w, h);
    
    if (manager->resizeCallback) {
        manager->resizeCallback(w, h);
    }
}

void WindowManager::cursorPosCallbackStatic(GLFWwindow* window, double xpos, double ypos) {
    auto* manager = reinterpret_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (manager && manager->cursorPosCallback) {
        manager->cursorPosCallback(xpos, ypos);
    }
}

void WindowManager::mouseButtonCallbackStatic(GLFWwindow* window, int button, int action, int mods) {
    auto* manager = reinterpret_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (manager && manager->mouseButtonCallback) {
        manager->mouseButtonCallback(button, action, mods);
    }
}

void WindowManager::keyCallbackStatic(GLFWwindow* window, int key, int scancode, int action, int mods) {
    auto* manager = reinterpret_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (manager && manager->keyCallback) {
        manager->keyCallback(key, scancode, action, mods);
    }
}

void WindowManager::reinstallScrollCallback() {
    if (window) {
        glfwSetScrollCallback(window, scrollCallbackStatic);
    }
}

void WindowManager::scrollCallbackStatic(GLFWwindow* window, double xoffset, double yoffset) {
    auto* manager = reinterpret_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (!manager) return;
    manager->m_scrollDelta += static_cast<float>(yoffset);
    // Forward to ImGui directly (avoid ImGui_ImplGlfw_ScrollCallback which chains back to us)
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseWheelEvent(static_cast<float>(xoffset), static_cast<float>(yoffset));
    if (manager->scrollCallback) {
        manager->scrollCallback(xoffset, yoffset);
    }
}

} // namespace UI
} // namespace Phyxel
