#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <array>

namespace Phyxel {
    namespace Vulkan { class VulkanDevice; }
}

namespace Phyxel {
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
    VkSampler getOffscreenSampler() const { return offscreenSampler; }

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

    // Bloom Resources
    std::array<VkImage, 2> blurImages = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    std::array<VkDeviceMemory, 2> blurImageMemory = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    std::array<VkImageView, 2> blurImageViews = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    std::array<VkFramebuffer, 2> blurFramebuffers = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkRenderPass blurRenderPass = VK_NULL_HANDLE;
    VkPipeline blurPipeline = VK_NULL_HANDLE;
    VkPipelineLayout blurPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout blurDescriptorSetLayout = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, 2> blurDescriptorSets = {VK_NULL_HANDLE, VK_NULL_HANDLE};

    // Post Process Resources
    VkRenderPass postProcessRenderPass = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    // Helpers
    bool createOffscreenResources();
    bool createBloomResources(uint32_t width, uint32_t height);
    bool createSceneRenderPass();
    bool createBlurRenderPass();
    bool createSceneFramebuffer();
    bool createBlurFramebuffers(uint32_t width, uint32_t height);
    bool createPostProcessRenderPass();
    bool createDescriptorSetLayout();
    bool createBlurDescriptorSetLayout();
    bool createPipeline();
    bool createBlurPipeline();
    bool createDescriptorPool();
    bool createDescriptorSet();
    bool createBlurDescriptorSets();
    void updateDescriptorSet();
    void updateBlurDescriptorSets();
    
    void renderBloom(VkCommandBuffer commandBuffer);
};

} // namespace Graphics
} // namespace Phyxel
