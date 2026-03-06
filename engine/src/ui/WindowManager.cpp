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
    
    // Register callbacks
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallbackStatic);
    glfwSetCursorPosCallback(window, cursorPosCallbackStatic);
    glfwSetMouseButtonCallback(window, mouseButtonCallbackStatic);
    glfwSetKeyCallback(window, keyCallbackStatic);
    
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

} // namespace UI
} // namespace Phyxel
