#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>

namespace VulkanCube {
    namespace Vulkan { class VulkanDevice; }
}

namespace VulkanCube {
namespace Graphics {

class PostProcessor {
public:
    PostProcessor(Vulkan::VulkanDevice* device, uint32_t width, uint32_t height);
    ~PostProcessor();

    bool initialize();
    void cleanup();
    void resize(uint32_t width, uint32_t height);

    // Render Pass Management
    void beginSceneRenderPass(VkCommandBuffer commandBuffer);
    void endSceneRenderPass(VkCommandBuffer commandBuffer);
    
    void beginPostProcessRenderPass(VkCommandBuffer commandBuffer, VkFramebuffer swapchainFramebuffer);
    void drawQuad(VkCommandBuffer commandBuffer);
    void endPostProcessRenderPass(VkCommandBuffer commandBuffer);
    
    void draw(VkCommandBuffer commandBuffer, VkFramebuffer swapchainFramebuffer);

    // Getters
    VkRenderPass getSceneRenderPass() const { return sceneRenderPass; }
    VkRenderPass getPostProcessRenderPass() const { return postProcessRenderPass; }
    VkImageView getOffscreenImageView() const { return offscreenImageView; }

private:
    Vulkan::VulkanDevice* device;
    uint32_t width;
    uint32_t height;

    // Offscreen Resources
    VkImage offscreenImage = VK_NULL_HANDLE;
    VkDeviceMemory offscreenImageMemory = VK_NULL_HANDLE;
    VkImageView offscreenImageView = VK_NULL_HANDLE;
    VkSampler offscreenSampler = VK_NULL_HANDLE;
    
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthImageMemory = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;

    VkFramebuffer sceneFramebuffer = VK_NULL_HANDLE;
    VkRenderPass sceneRenderPass = VK_NULL_HANDLE;

    // Post Process Resources
    VkRenderPass postProcessRenderPass = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    // Helpers
    bool createOffscreenResources();
    bool createSceneRenderPass();
    bool createSceneFramebuffer();
    bool createPostProcessRenderPass();
    bool createDescriptorSetLayout();
    bool createPipeline();
    bool createDescriptorPool();
    bool createDescriptorSet();
    void updateDescriptorSet();
};

} // namespace Graphics
} // namespace VulkanCube
