#pragma once

#include <memory>
#include <vector>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

// Forward declarations
struct ImGuiContext;
namespace VulkanCube {
    class Timer;
    class PerformanceProfiler;
    struct FrameTiming;
    struct DetailedFrameTiming;
    namespace Physics { class PhysicsWorld; }
    namespace Vulkan { class VulkanDevice; class RenderPipeline; }
}

namespace VulkanCube::UI {

class ImGuiRenderer {
public:
    ImGuiRenderer();
    ~ImGuiRenderer();

    // Initialization and cleanup
    bool initialize(GLFWwindow* window, Vulkan::VulkanDevice* vulkanDevice, Vulkan::RenderPipeline* renderPipeline);
    void cleanup();

    // Frame lifecycle
    void newFrame();
    void endFrame();
    void render(uint32_t currentFrame, uint32_t imageIndex);

    // Overlay rendering
    void renderPerformanceOverlay(
        bool showOverlay,
        Timer* timer,
        PerformanceProfiler* performanceProfiler,
        const FrameTiming& frameTiming,
        const std::vector<DetailedFrameTiming>& detailedTimings,
        Physics::PhysicsWorld* physicsWorld,
        const glm::vec3& cameraPos,
        uint64_t frameCount
    );

private:
    ImGuiContext* m_context;
    bool m_initialized;
    
    // Vulkan objects
    Vulkan::VulkanDevice* m_vulkanDevice;
    Vulkan::RenderPipeline* m_renderPipeline;
    GLFWwindow* m_window;
};

} // namespace VulkanCube::UI
