#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <chrono>

#include "core/WaterManager.h" // Core::WaterSurfaceCell (the instance layout)

namespace Phyxel {
namespace Graphics {

class Camera;

// Per-cell water surface renderer (Phase 2, see docs/WaterSystem.md). Draws one
// instanced translucent 1x1 quad per simulated surface cell at its fill height,
// visualizing the actual water field (flow, pools, bodies at any height) rather than
// a single flat sea plane. Modeled on VfxRenderPipeline: own quad vertex buffer +
// dynamic instance buffer, alpha blend, depth-test/no-write, no culling.
//
// Instances are Core::WaterSurfaceCell (sloped per-corner top + column depth) — exactly
// what WaterManager::surfaceCells() produces.
class WaterCellRenderPipeline {
public:
    WaterCellRenderPipeline();
    ~WaterCellRenderPipeline();

    void initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                    VkRenderPass renderPass, VkExtent2D swapChainExtent);
    void cleanup();

    void render(VkCommandBuffer commandBuffer, const Camera& camera,
                const glm::mat4& projectionMatrix, const std::vector<Core::WaterSurfaceCell>& cells);

    void recreatePipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent);

private:
    void createDescriptorSetLayout();
    void createPipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent);
    void createBuffers();

    VkDevice         m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;

    VkPipelineLayout      m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline            m_pipeline = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;

    VkBuffer       m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexBufferMemory = VK_NULL_HANDLE;
    VkBuffer       m_instanceBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_instanceBufferMemory = VK_NULL_HANDLE;

    std::chrono::high_resolution_clock::time_point m_startTime;

    static constexpr size_t MAX_INSTANCES = 100000;
};

} // namespace Graphics
} // namespace Phyxel
