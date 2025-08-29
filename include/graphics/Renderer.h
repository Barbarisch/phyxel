#pragma once

#include <memory>
#include <chrono>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "core/Types.h"
#include "utils/Frustum.h"

// Forward declarations
struct GLFWwindow;
namespace VulkanCube {
    namespace Vulkan {
        class VulkanDevice;
        class RenderPipeline;
    }
    namespace UI {
        class ImGuiRenderer;
    }
    namespace Scene {
        class SceneManager;
    }
    class ChunkManager;
    class PerformanceProfiler;
}

namespace VulkanCube {
namespace Graphics {

/**
 * Renderer class manages all Vulkan rendering operations
 * Handles frame rendering, pipelines, and GPU resource management
 */
class Renderer {
public:
    Renderer();
    ~Renderer();

    // Lifecycle
    bool initialize(GLFWwindow* window, int windowWidth, int windowHeight);
    void cleanup();
    void resize(int newWidth, int newHeight);

    // Frame rendering
    void renderFrame(
        const glm::mat4& viewMatrix,
        const glm::mat4& projectionMatrix,
        Scene::SceneManager* sceneManager,
        ChunkManager* chunkManager,
        UI::ImGuiRenderer* imguiRenderer,
        PerformanceProfiler* performanceProfiler
    );

    // Vulkan resource access (for systems that need direct access)
    Vulkan::VulkanDevice* getVulkanDevice() const { return vulkanDevice.get(); }
    Vulkan::RenderPipeline* getStaticPipeline() const { return renderPipeline.get(); }
    Vulkan::RenderPipeline* getDynamicPipeline() const { return dynamicRenderPipeline.get(); }

    // Performance metrics
    const FrameTiming& getFrameTiming() const { return frameTiming; }
    const std::vector<DetailedFrameTiming>& getDetailedTimings() const { return detailedTimings; }

    // Rendering configuration
    void setProjectionMatrixNeedsUpdate() { projectionMatrixNeedsUpdate = true; }
    void setRenderDistance(float distance) { 
        renderDistance = distance; 
        projectionMatrixNeedsUpdate = true; 
    }
    float getRenderDistance() const { return renderDistance; }

private:
    // Core Vulkan systems
    std::unique_ptr<Vulkan::VulkanDevice> vulkanDevice;
    std::unique_ptr<Vulkan::RenderPipeline> renderPipeline;         // Static cubes and static subcubes
    std::unique_ptr<Vulkan::RenderPipeline> dynamicRenderPipeline;  // Dynamic subcubes with physics

    // Window and swapchain
    GLFWwindow* window;
    int windowWidth;
    int windowHeight;

    // Frame state
    uint32_t currentFrame = 0;
    std::chrono::high_resolution_clock::time_point frameStartTime;

    // Cached matrices
    glm::mat4 cachedProjectionMatrix;
    bool projectionMatrixNeedsUpdate = true;
    float renderDistance = 96.0f;  // Default render distance

    // Performance tracking
    FrameTiming frameTiming;
    std::vector<DetailedFrameTiming> detailedTimings;
    uint32_t lastVisibleInstances = 0;
    uint32_t lastCulledInstances = 0;

    // Frustum culling
    Utils::Frustum cameraFrustum;
    glm::vec3 cameraPosition;  // Store camera position for distance culling
    void updateCameraFrustum(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix);
    std::vector<uint32_t> getVisibleChunks(ChunkManager* chunkManager);

    // Internal rendering methods
    bool initializeVulkan();
    bool loadShaders();
    void renderStaticGeometry(ChunkManager* chunkManager, const std::vector<uint32_t>& visibleChunks);
    void renderDynamicSubcubes(ChunkManager* chunkManager, const std::vector<uint32_t>& visibleChunks);
    void updatePerformanceStats(ChunkManager* chunkManager, Scene::SceneManager* sceneManager);
};

} // namespace Graphics
} // namespace VulkanCube
