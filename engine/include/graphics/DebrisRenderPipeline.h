#pragma once

#include "core/Types.h"
#include "core/DebrisSystem.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <functional>
#include <glm/glm.hpp>

namespace Phyxel {
namespace Graphics {

class Camera;

struct DebrisInstanceData {
    glm::vec3 position;
    float padding1;
    glm::vec3 scale;
    float padding2;
    glm::vec4 color;
};

class DebrisRenderPipeline {
public:
    DebrisRenderPipeline();
    ~DebrisRenderPipeline();

    void initialize(VkDevice device, VkPhysicalDevice physicalDevice, VkRenderPass renderPass, VkExtent2D swapChainExtent);
    void cleanup();
    
    void render(VkCommandBuffer commandBuffer, const Camera& camera, const glm::mat4& projectionMatrix, const std::vector<DebrisParticle>& particles, size_t activeCount);
    
    // Update pipeline if window resizes
    void recreatePipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent);

    // Phase 4c: sample the baked light field at a world position so break-debris darkens
    // in unlit interiors and picks up glow/spell light (folded into per-particle color on
    // upload; debris.vert/.frag unchanged). Returns vec4(sky, blockR, blockG, blockB) 0..1.
    using LightSampler = std::function<glm::vec4(const glm::vec3& worldPos)>;
    void setLightSampler(LightSampler fn) { m_lightSampler = std::move(fn); }

private:
    void createDescriptorSetLayout();
    void createPipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent);
    void createBuffers();
    void updateInstanceBuffer(const std::vector<DebrisParticle>& particles, size_t activeCount);

    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    
    // Instance Buffer
    VkBuffer m_instanceBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_instanceBufferMemory = VK_NULL_HANDLE;
    size_t m_instanceBufferSize = 0;
    
    // Vertex Buffer (Shared Cube Mesh)
    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexBufferMemory = VK_NULL_HANDLE;
    
    LightSampler m_lightSampler; // Phase 4c baked-light sampler (null = unlit color passthrough)

    // Constants
    static const size_t MAX_INSTANCES = 10000;
};

} // namespace Graphics
} // namespace Phyxel
