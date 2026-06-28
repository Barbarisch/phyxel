#pragma once

#include "vulkan/VulkanDevice.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <memory>

namespace Phyxel {
namespace Graphics {

class ShadowMap {
public:
    ShadowMap(Vulkan::VulkanDevice* device, uint32_t width = 2048, uint32_t height = 2048);
    ~ShadowMap();

    bool initialize();
    void cleanup();

    // Render pass management
    void beginRenderPass(VkCommandBuffer commandBuffer);
    void endRenderPass(VkCommandBuffer commandBuffer);

    // Getters
    VkRenderPass getRenderPass() const { return renderPass; }
    VkPipeline getPipeline() const { return pipeline; }
    VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }
    VkImageView getDepthImageView() const { return depthImageView; }
    VkSampler getSampler() const { return sampler; }
    uint32_t getWidth() const { return width; }
    uint32_t getHeight() const { return height; }

    // Extra shadow pipelines for non-static geometry
    VkPipeline getCharacterShadowPipeline() const { return characterPipeline; }
    VkPipelineLayout getCharacterShadowLayout() const { return characterPipelineLayout; }
    VkPipeline getKinematicShadowPipeline() const { return kinematicPipeline; }
    VkPipelineLayout getKinematicShadowLayout() const { return kinematicPipelineLayout; }
    VkPipeline getDynamicShadowPipeline() const { return dynamicPipeline; }
    VkPipelineLayout getDynamicShadowLayout() const { return dynamicPipelineLayout; }

    // Light matrix calculation
    glm::mat4 getLightSpaceMatrix(const glm::vec3& lightDir, const glm::vec3& center, float range);

    // Configurable shadow quality
    void setShadowRange(float range) { m_shadowRange = range; }
    float getShadowRange() const { return m_shadowRange; }

private:
    Vulkan::VulkanDevice* device;
    uint32_t width;
    uint32_t height;

    // Resources
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthImageMemory = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    // Shadow pipelines for non-static geometry
    VkPipelineLayout characterPipelineLayout = VK_NULL_HANDLE;
    VkPipeline characterPipeline = VK_NULL_HANDLE;
    VkPipelineLayout kinematicPipelineLayout = VK_NULL_HANDLE;
    VkPipeline kinematicPipeline = VK_NULL_HANDLE;
    VkPipelineLayout dynamicPipelineLayout = VK_NULL_HANDLE;
    VkPipeline dynamicPipeline = VK_NULL_HANDLE;

    // Shadow pass cost scales with caster area (~range²): the pass renders every
    // chunk within this range of the camera into the shadow map. 110 keeps good
    // coverage while cutting caster area ~46% vs 150, and sharpens near shadows
    // (smaller ortho frustum over the same 2048² map). Runtime-tunable via the
    // lighting UI slider (setShadowRange). (History: hard-coded 100 → 150 → 110.)
    float m_shadowRange = 120.0f; // covers the view; frustum is centred ahead of the camera (see RenderCoordinator)

    // Internal creation methods
    bool createDepthResources();
    bool createRenderPass();
    bool createFramebuffer();
    bool createPipeline();
    bool createSampler();
    bool createCharacterShadowPipeline();
    bool createKinematicShadowPipeline();
    bool createDynamicShadowPipeline();
};

} // namespace Graphics
} // namespace Phyxel
