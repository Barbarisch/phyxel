#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <chrono>

namespace Phyxel {
namespace Graphics {

class Camera;

// Phase 0 water surface (see docs/WaterSystem.md).
//
// Draws a single large translucent quad locked to a world sea level and centered on
// the camera in XZ — an "infinite ocean" plane. The scene depth buffer occludes it,
// so the surface only appears where open space meets sea level (the implicit-ocean
// model). Procedural Fresnel + ripples in the shader; no simulation, reflection, or
// refraction yet (those are later phases). Self-contained, modeled on
// VfxRenderPipeline: its own quad vertex buffer, pipeline, and push constants.
class WaterRenderPipeline {
public:
    WaterRenderPipeline();
    ~WaterRenderPipeline();

    void initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                    VkRenderPass renderPass, VkExtent2D swapChainExtent);
    void cleanup();

    // Point the reflection sampler at the planar-reflection texture. Call after
    // initialize() and again after any swapchain resize (the texture is recreated).
    // Must not be called mid-frame (updates a descriptor set).
    void setReflectionTexture(VkImageView reflectionView, VkSampler reflectionSampler);

    // Draw the sea-level surface. `size` is the side length of the camera-following
    // quad (world units). When `reflectionEnabled`, the fragment shader samples the
    // reflection texture (set via setReflectionTexture). Call inside the scene render
    // pass after opaque geometry.
    void render(VkCommandBuffer commandBuffer, const Camera& camera,
                const glm::mat4& projectionMatrix, float seaLevel, float size,
                VkExtent2D screenExtent, bool reflectionEnabled);

    void recreatePipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent);

private:
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createPipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent);
    void createBuffers();

    VkDevice         m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;

    VkPipelineLayout      m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline            m_pipeline = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool      m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet       m_descriptorSet = VK_NULL_HANDLE;

    VkBuffer       m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexBufferMemory = VK_NULL_HANDLE;

    std::chrono::high_resolution_clock::time_point m_startTime;
};

} // namespace Graphics
} // namespace Phyxel
