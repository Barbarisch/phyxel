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
    bool createDebugGraphicsPipeline(); // For debug voxel visualization
    bool createDebugLinePipeline(); // For debug line/raycast visualization
    void cleanup();

    // Shader management
    bool loadShaders(const std::string& vertPath, const std::string& fragPath);
    bool loadDebugShaders(const std::string& vertPath, const std::string& fragPath);
    bool loadDebugLineShaders(const std::string& vertPath, const std::string& fragPath);

    // Pipeline state
    void bindGraphicsPipeline(VkCommandBuffer commandBuffer);
    void bindDebugGraphicsPipeline(VkCommandBuffer commandBuffer);
    void bindDebugLinePipeline(VkCommandBuffer commandBuffer);

    // Getters
    VkPipeline getGraphicsPipeline() const { return graphicsPipeline; }
    VkPipeline getDebugGraphicsPipeline() const { return debugGraphicsPipeline; }
    VkPipeline getDebugLinePipeline() const { return debugLinePipeline; }
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
    VkPipeline debugGraphicsPipeline = VK_NULL_HANDLE;  // Debug voxel visualization pipeline
    VkPipeline debugLinePipeline = VK_NULL_HANDLE;      // Debug line/raycast visualization pipeline

    // Descriptor set layouts
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;

    // Shader modules
    VkShaderModule vertShaderModule = VK_NULL_HANDLE;
    VkShaderModule fragShaderModule = VK_NULL_HANDLE;
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
} // namespace VulkanCube
