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
