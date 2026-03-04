#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <vector>

namespace VulkanCube {

// Lightweight hover overlay system that doesn't require chunk rebuilds
class HoverRenderer {
public:
    struct HoverInstance {
        glm::vec3 position;
        glm::vec3 originalColor;
        glm::vec3 hoverColor;
    };

    HoverRenderer() = default;
    ~HoverRenderer() = default;

    void initialize(VkDevice device, VkPhysicalDevice physicalDevice);
    void cleanup();

    void setHoveredCube(const glm::vec3& worldPos, const glm::vec3& originalColor);
    void clearHover();
    
    void updateUniformBuffer(uint32_t frameIndex, const glm::mat4& view, const glm::mat4& proj);
    void render(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout);

    bool hasHoveredCube() const { return hoveredCube.has_value(); }

private:
    VkDevice device = VK_NULL_HANDLE;
    
    std::optional<HoverInstance> hoveredCube;
    
    // Small dedicated buffer for hover overlay
    VkBuffer hoverBuffer = VK_NULL_HANDLE;
    VkDeviceMemory hoverBufferMemory = VK_NULL_HANDLE;
    void* hoverMappedMemory = nullptr;
    
    void createHoverBuffer();
    void updateHoverBuffer();
};

} // namespace VulkanCube
