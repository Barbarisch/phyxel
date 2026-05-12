#pragma once

#include "core/Types.h"
#include "vulkan/VulkanDevice.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <string>

namespace Phyxel {
namespace Vulkan {

class RenderPipeline {
public:
    RenderPipeline(const VulkanDevice& device);
    ~RenderPipeline();

    // Pipeline creation
    bool createGraphicsPipeline();
    bool createGraphicsPipelineForDynamicSubcubes(); // For dynamic subcube rendering
    bool createCharacterPipeline(); // For character rendering
    bool createInstancedCharacterPipeline(); // For instanced character rendering
    bool createDebugGraphicsPipeline(); // For debug voxel visualization
    bool createDebugLinePipeline(); // For debug line/raycast visualization
    bool createOITPipeline(VkRenderPass oitRenderPass); // Weighted Blended OIT transparent pass
    bool createMirrorPipeline(VkRenderPass sceneRenderPass); // Reflective mirror surface pass
    void updateMirrorReflectionDescriptor(VkImageView reflectionView, VkSampler reflectionSampler);
    void cleanup();

    // Shader management
    bool loadShaders(const std::string& vertPath, const std::string& fragPath);
    bool loadCharacterShaders(const std::string& vertPath, const std::string& fragPath);
    bool loadInstancedCharacterShaders(const std::string& vertPath, const std::string& fragPath);
    bool loadDebugShaders(const std::string& vertPath, const std::string& fragPath);
    bool loadDebugLineShaders(const std::string& vertPath, const std::string& fragPath);

    // Pipeline state
    void bindGraphicsPipeline(VkCommandBuffer commandBuffer);
    void bindCharacterPipeline(VkCommandBuffer commandBuffer);
    void bindInstancedCharacterPipeline(VkCommandBuffer commandBuffer);
    void bindDebugGraphicsPipeline(VkCommandBuffer commandBuffer);
    void bindDebugLinePipeline(VkCommandBuffer commandBuffer);
    void bindOITPipeline(VkCommandBuffer commandBuffer);
    void bindMirrorPipeline(VkCommandBuffer commandBuffer, uint32_t frameIndex, VkDescriptorSet mirrorReflDescSet);

    // Getters
    VkPipeline getGraphicsPipeline() const { return graphicsPipeline; }
    VkPipeline getCharacterPipeline() const { return characterPipeline; }
    VkPipeline getInstancedCharacterPipeline() const { return instancedCharacterPipeline; }
    VkPipeline getDebugGraphicsPipeline() const { return debugGraphicsPipeline; }
    VkPipeline getDebugLinePipeline() const { return debugLinePipeline; }
    VkPipeline getOITPipeline() const { return oitPipeline; }
    VkPipeline getMirrorPipeline() const { return mirrorPipeline; }
    VkPipelineLayout getMirrorPipelineLayout() const { return mirrorPipelineLayout; }
    VkDescriptorSet getMirrorReflectionDescriptorSet() const { return mirrorReflectionDescriptorSet; }
    VkPipelineLayout getGraphicsLayout() const { return pipelineLayout; }
    VkPipelineLayout getCharacterLayout() const { return characterPipelineLayout; }
    VkPipelineLayout getInstancedCharacterLayout() const { return instancedCharacterPipelineLayout; }
    VkDescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }
    VkRenderPass getRenderPass() const { return renderPass; }

    // Render pass management
    bool createRenderPass();
    void setRenderPass(VkRenderPass pass);

private:
    const VulkanDevice& vulkanDevice;
    
    // Pipeline objects
    VkRenderPass renderPass = VK_NULL_HANDLE;
    bool ownsRenderPass = true;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout characterPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout instancedCharacterPipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;
    VkPipeline characterPipeline = VK_NULL_HANDLE;
    VkPipeline instancedCharacterPipeline = VK_NULL_HANDLE;
    VkPipeline debugGraphicsPipeline = VK_NULL_HANDLE;  // Debug voxel visualization pipeline
    VkPipeline debugLinePipeline = VK_NULL_HANDLE;      // Debug line/raycast visualization pipeline
    VkPipeline oitPipeline = VK_NULL_HANDLE;            // Weighted Blended OIT transparent pass
    VkShaderModule oitFragShaderModule = VK_NULL_HANDLE;
    VkPipeline mirrorPipeline = VK_NULL_HANDLE;          // Reflective mirror surface pass
    VkPipelineLayout mirrorPipelineLayout = VK_NULL_HANDLE; // Layout with set 0 + set 1 (reflection)
    VkShaderModule mirrorFragShaderModule = VK_NULL_HANDLE;
    VkDescriptorSetLayout mirrorReflectionDescSetLayout = VK_NULL_HANDLE; // set 1: reflection sampler
    VkDescriptorPool mirrorDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet mirrorReflectionDescriptorSet = VK_NULL_HANDLE; // single, not per-frame

    // Descriptor set layouts
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;

    // Shader modules
    VkShaderModule vertShaderModule = VK_NULL_HANDLE;
    VkShaderModule fragShaderModule = VK_NULL_HANDLE;
    VkShaderModule characterVertShaderModule = VK_NULL_HANDLE;
    VkShaderModule characterFragShaderModule = VK_NULL_HANDLE;
    VkShaderModule instancedCharacterVertShaderModule = VK_NULL_HANDLE;
    VkShaderModule instancedCharacterFragShaderModule = VK_NULL_HANDLE;
    VkShaderModule debugVertShaderModule = VK_NULL_HANDLE;
    VkShaderModule debugFragShaderModule = VK_NULL_HANDLE;
    VkShaderModule debugLineVertShaderModule = VK_NULL_HANDLE;
    VkShaderModule debugLineFragShaderModule = VK_NULL_HANDLE;

    // Helper functions
    VkShaderModule createShaderModule(const std::vector<char>& code);
    bool createDescriptorSetLayout();
    bool createDynamicDescriptorSetLayout();  // UBO-only layout for dynamic subcubes
    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
    VkFormat findDepthFormat();
};

} // namespace Vulkan
} // namespace Phyxel
