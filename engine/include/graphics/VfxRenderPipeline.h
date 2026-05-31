#pragma once

#include "core/Types.h"
#include "core/VfxSystem.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <glm/glm.hpp>

namespace Phyxel {
namespace Graphics {

class Camera;

// GPU instance layout for one VFX cube (matches vfx.vert attributes).
struct VfxInstanceData {
    glm::vec3 position;
    float padding1;
    glm::vec3 scale;
    float padding2;
    glm::vec4 color;
};

// Instanced-cube renderer for lightweight VFX particles. Mirrors
// DebrisRenderPipeline but uses ADDITIVE blending and disables depth
// writes so glowing particles accumulate without occluding each other.
// See [[project-spell-vfx]].
class VfxRenderPipeline {
public:
    VfxRenderPipeline();
    ~VfxRenderPipeline();

    void initialize(VkDevice device, VkPhysicalDevice physicalDevice, VkRenderPass renderPass, VkExtent2D swapChainExtent);
    void cleanup();

    void render(VkCommandBuffer commandBuffer, const Camera& camera, const glm::mat4& projectionMatrix, const std::vector<VfxParticle>& particles, size_t activeCount);

    void recreatePipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent);

private:
    void createDescriptorSetLayout();
    void createPipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent);
    void createBuffers();
    void updateInstanceBuffer(const std::vector<VfxParticle>& particles, size_t activeCount);

    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;

    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;

    VkBuffer m_instanceBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_instanceBufferMemory = VK_NULL_HANDLE;
    size_t m_instanceBufferSize = 0;

    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexBufferMemory = VK_NULL_HANDLE;

    static const size_t MAX_INSTANCES = 10000;
};

} // namespace Graphics
} // namespace Phyxel
