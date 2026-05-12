#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
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

    /// Run the SSAO + blur passes. Call after the scene pass, before post-process.
    void renderSSAO(VkCommandBuffer commandBuffer, const glm::mat4& proj);

    /// OIT pass management — begin/end the transparent render pass.
    void beginOITRenderPass(VkCommandBuffer commandBuffer);
    void endOITRenderPass(VkCommandBuffer commandBuffer);
    VkRenderPass getOITRenderPass() const { return oitRenderPass; }

    /// Reflection image — rendered into during the reflection pass, sampled by mirror surfaces.
    void beginReflectionRenderPass(VkCommandBuffer commandBuffer);
    void endReflectionRenderPass(VkCommandBuffer commandBuffer);
    VkImageView getReflectionImageView() const { return reflectionImageView; }
    VkSampler   getReflectionSampler()   const { return reflectionSampler; }

    // Getters
    VkRenderPass getSceneRenderPass() const { return sceneRenderPass; }
    VkRenderPass getPostProcessRenderPass() const { return postProcessRenderPass; }
    VkImageView getOffscreenImageView() const { return offscreenImageView; }
    VkSampler getOffscreenSampler() const { return offscreenSampler; }

    bool ssaoEnabled = true;

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
    VkSampler depthSampler = VK_NULL_HANDLE;

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

    // SSAO Resources
    VkImage ssaoImage = VK_NULL_HANDLE;
    VkDeviceMemory ssaoImageMemory = VK_NULL_HANDLE;
    VkImageView ssaoImageView = VK_NULL_HANDLE;
    VkFramebuffer ssaoFramebuffer = VK_NULL_HANDLE;
    VkRenderPass ssaoRenderPass = VK_NULL_HANDLE;
    VkPipeline ssaoPipeline = VK_NULL_HANDLE;
    VkPipelineLayout ssaoPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout ssaoDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet ssaoDescriptorSet = VK_NULL_HANDLE;

    // SSAO Blur Resources
    VkImage ssaoBlurImage = VK_NULL_HANDLE;
    VkDeviceMemory ssaoBlurImageMemory = VK_NULL_HANDLE;
    VkImageView ssaoBlurImageView = VK_NULL_HANDLE;
    VkFramebuffer ssaoBlurFramebuffer = VK_NULL_HANDLE;
    VkPipeline ssaoBlurPipeline = VK_NULL_HANDLE;
    VkPipelineLayout ssaoBlurPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout ssaoBlurDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet ssaoBlurDescriptorSet = VK_NULL_HANDLE;
    VkSampler ssaoSampler = VK_NULL_HANDLE;

    // OIT (Weighted Blended Order-Independent Transparency)
    VkImage oitAccumImage = VK_NULL_HANDLE;
    VkDeviceMemory oitAccumImageMemory = VK_NULL_HANDLE;
    VkImageView oitAccumImageView = VK_NULL_HANDLE;
    VkImage oitRevealImage = VK_NULL_HANDLE;
    VkDeviceMemory oitRevealImageMemory = VK_NULL_HANDLE;
    VkImageView oitRevealImageView = VK_NULL_HANDLE;
    VkSampler oitSampler = VK_NULL_HANDLE;
    VkRenderPass oitRenderPass = VK_NULL_HANDLE;
    VkFramebuffer oitFramebuffer = VK_NULL_HANDLE;

    bool createOITResources();
    bool createOITRenderPass();
    void cleanupOITResources();

    // Reflection image (rendered from reflected camera, sampled by mirror surfaces)
    VkImage reflectionImage = VK_NULL_HANDLE;
    VkDeviceMemory reflectionImageMemory = VK_NULL_HANDLE;
    VkImageView reflectionImageView = VK_NULL_HANDLE;
    VkImage reflectionDepthImage = VK_NULL_HANDLE;
    VkDeviceMemory reflectionDepthMemory = VK_NULL_HANDLE;
    VkImageView reflectionDepthView = VK_NULL_HANDLE;
    VkSampler reflectionSampler = VK_NULL_HANDLE;
    VkFramebuffer reflectionFramebuffer = VK_NULL_HANDLE;

    bool createReflectionResources();
    void cleanupReflectionResources();

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

    bool createSSAOResources();
    bool createSSAORenderPass();
    bool createSSAOPipeline();
    bool createSSAODescriptors();
    bool createSSAOBlurPipeline();
    bool createSSAOBlurDescriptors();
    void updateSSAODescriptors();
    void cleanupSSAOResources();
};

} // namespace Graphics
} // namespace Phyxel
