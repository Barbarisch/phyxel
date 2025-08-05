#pragma once

#include "core/Types.h"
#include "vulkan/VulkanDevice.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <string>

namespace VulkanCube {
namespace Vulkan {

class RenderPipeline {
public:
    RenderPipeline(const VulkanDevice& device);
    ~RenderPipeline();

    // Pipeline creation
    bool createGraphicsPipeline();
    bool createComputePipeline();
    void cleanup();

    // Shader management
    bool loadShaders(const std::string& vertPath, const std::string& fragPath);
    bool loadComputeShader(const std::string& compPath);

    // Pipeline state
    void bindGraphicsPipeline(VkCommandBuffer commandBuffer);
    void bindComputePipeline(VkCommandBuffer commandBuffer);

    // Getters
    VkPipeline getGraphicsPipeline() const { return graphicsPipeline; }
    VkPipeline getComputePipeline() const { return computePipeline; }
    VkPipelineLayout getGraphicsLayout() const { return pipelineLayout; }
    VkPipelineLayout getComputeLayout() const { return computePipelineLayout; }
    VkDescriptorSetLayout getComputeDescriptorSetLayout() const { return computeDescriptorSetLayout; }
    VkRenderPass getRenderPass() const { return renderPass; }

    // Render pass management
    bool createRenderPass();

private:
    const VulkanDevice& vulkanDevice;
    
    // Pipeline objects
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout computePipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;
    VkPipeline computePipeline = VK_NULL_HANDLE;

    // Descriptor set layouts
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout computeDescriptorSetLayout = VK_NULL_HANDLE;

    // Shader modules
    VkShaderModule vertShaderModule = VK_NULL_HANDLE;
    VkShaderModule fragShaderModule = VK_NULL_HANDLE;
    VkShaderModule computeShaderModule = VK_NULL_HANDLE;

    // Helper functions
    VkShaderModule createShaderModule(const std::vector<char>& code);
    bool createDescriptorSetLayout();
    bool createComputeDescriptorSetLayout();
    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
    VkFormat findDepthFormat();
};

} // namespace Vulkan
} // namespace VulkanCube
