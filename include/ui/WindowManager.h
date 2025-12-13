#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <string>
#include <functional>

namespace VulkanCube {
namespace UI {

/**
 * Manages GLFW window creation, events, and lifecycle
 * Extracted from Application class for better separation of concerns
 */
class WindowManager {
public:
    using ResizeCallback = std::function<void(int width, int height)>;
    using CursorPosCallback = std::function<void(double xpos, double ypos)>;
    using MouseButtonCallback = std::function<void(int button, int action, int mods)>;
    using KeyCallback = std::function<void(int key, int scancode, int action, int mods)>;
    
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
    void setCursorVisible(bool visible);
    
    // State getters
    GLFWwindow* getHandle() const { return window; }
    int getWidth() const { return width; }
    int getHeight() const { return height; }
    bool wasResized() const { return resized; }
    void acknowledgeResize() { resized = false; }
    
    // Callback registration
    void setResizeCallback(ResizeCallback callback) { resizeCallback = callback; }
    void setCursorPosCallback(CursorPosCallback callback) { cursorPosCallback = callback; }
    void setMouseButtonCallback(MouseButtonCallback callback) { mouseButtonCallback = callback; }
    void setKeyCallback(KeyCallback callback) { keyCallback = callback; }
    
private:
    GLFWwindow* window = nullptr;
    int width = 800;
    int height = 600;
    std::string title = "Phyxel";
    bool resized = false;
    
    ResizeCallback resizeCallback;
    CursorPosCallback cursorPosCallback;
    MouseButtonCallback mouseButtonCallback;
    KeyCallback keyCallback;
    
    static void framebufferResizeCallbackStatic(GLFWwindow* window, int w, int h);
    static void cursorPosCallbackStatic(GLFWwindow* window, double xpos, double ypos);
    static void mouseButtonCallbackStatic(GLFWwindow* window, int button, int action, int mods);
    static void keyCallbackStatic(GLFWwindow* window, int key, int scancode, int action, int mods);
};

} // namespace UI
} // namespace VulkanCube
