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
    bool createGraphicsPipelineForDynamicSubcubes(); // For dynamic subcube rendering
    void cleanup();

    // Shader management
    bool loadShaders(const std::string& vertPath, const std::string& fragPath);

    // Pipeline state
    void bindGraphicsPipeline(VkCommandBuffer commandBuffer);

    // Getters
    VkPipeline getGraphicsPipeline() const { return graphicsPipeline; }
    VkPipelineLayout getGraphicsLayout() const { return pipelineLayout; }
    VkRenderPass getRenderPass() const { return renderPass; }

    // Render pass management
    bool createRenderPass();

private:
    const VulkanDevice& vulkanDevice;
    
    // Pipeline objects
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;

    // Descriptor set layouts
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;

    // Shader modules
    VkShaderModule vertShaderModule = VK_NULL_HANDLE;
    VkShaderModule fragShaderModule = VK_NULL_HANDLE;

    // Helper functions
    VkShaderModule createShaderModule(const std::vector<char>& code);
    bool createDescriptorSetLayout();
    bool createDynamicDescriptorSetLayout();  // UBO-only layout for dynamic subcubes
    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
    VkFormat findDepthFormat();
};

} // namespace Vulkan
} // namespace VulkanCube
