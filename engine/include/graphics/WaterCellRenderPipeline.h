#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <chrono>

#include "core/WaterManager.h" // Core::WaterSurfaceCell (the instance layout)

namespace Phyxel {
namespace Graphics {

class Camera;

// Per-cell water surface renderer (Phase 2, see docs/Water.md). Draws one
// instanced translucent 1x1 quad per simulated surface cell at its fill height,
// visualizing the actual water field (flow, pools, bodies at any height) rather than
// a single flat sea plane. Modeled on VfxRenderPipeline: own quad vertex buffer +
// dynamic instance buffer, alpha blend, depth-test/no-write, no culling.
//
// Instances are Core::WaterSurfaceCell (sloped per-corner top + column depth) — exactly
// what WaterManager::surfaceCells() produces.
class WaterCellRenderPipeline {
public:
    WaterCellRenderPipeline();
    ~WaterCellRenderPipeline();

    // `uboLayout` is the shared scene descriptor-set layout (VulkanDevice::getDescriptorSetLayout),
    // bound at SET 0 — water reads sun direction/colour, ambient and the view/projection matrices
    // from it, so it tracks the day/night cycle and can linearize the depth buffer
    // (WaterSystemV3 Phase 1). SET 1 is this pipeline's own scene taps (refraction + depth).
    void initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                    VkRenderPass renderPass, VkExtent2D swapChainExtent,
                    VkDescriptorSetLayout uboLayout);
    void cleanup();

    // Point set 1 at the post-scene taps: the half-res scene-colour copy (refraction) and the
    // scene depth buffer (water thickness → absorption + soft shorelines). Call after initialize()
    // and again after every swapchain resize (both images are recreated). Not mid-frame.
    void setSceneTextures(VkImageView refractionView, VkSampler refractionSampler,
                          VkImageView sceneDepthView, VkSampler sceneDepthSampler);

    // Ripple heightfield upload (small-scale plan Phase 3). Call once per frame BEFORE the water
    // render pass begins (the copy is recorded into `cmd`, which must be outside any render pass).
    // Creates the R32F image + staging lazily on first call, binds set 1 binding 2 itself, and
    // re-records a staging→image copy only when the field's version changed; the window/amplitude
    // push params update every call (the window travels with the player even when heights don't).
    // The image is not swapchain-sized — resizes don't touch it. No draw until this has run once.
    void updateRipple(VkCommandBuffer cmd, uint32_t frameIndex, const Core::RippleField& field);

    void render(VkCommandBuffer commandBuffer, VkDescriptorSet uboSet, const Camera& camera,
                const glm::mat4& projectionMatrix, const std::vector<Core::WaterSurfaceCell>& cells,
                VkExtent2D screenExtent);

    void recreatePipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent);

private:
    void createDescriptorSetLayout(VkDescriptorSetLayout uboLayout);
    void createDescriptorPool();
    void createPipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent);
    void createBuffers();

    VkDevice         m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;

    VkPipelineLayout      m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline            m_pipeline = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE; // set 1 (scene taps)
    VkDescriptorPool      m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet       m_descriptorSet = VK_NULL_HANDLE;
    bool                  m_texturesBound = false; // no draw until set 1 has real images
    bool                  m_rippleBound   = false; // …including the ripple heightfield (binding 2)
    glm::vec4             m_rippleWindow{0.0f, 0.0f, 1.0f / 64.0f, 0.0f}; // amplitude 0 until set

    // Ripple heightfield GPU state (created lazily by updateRipple; destroyed in cleanup()).
    // Staging is double-buffered by frame-in-flight: the fence wait at frame start makes writing
    // staging[currentFrame] safe while frame N-1's copy may still read the other buffer.
    void createRippleResources(int cells);
    VkImage        m_rippleImage = VK_NULL_HANDLE;
    VkDeviceMemory m_rippleImageMemory = VK_NULL_HANDLE;
    VkImageView    m_rippleView = VK_NULL_HANDLE;
    VkSampler      m_rippleSampler = VK_NULL_HANDLE;
    VkBuffer       m_rippleStaging[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkDeviceMemory m_rippleStagingMemory[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    void*          m_rippleStagingMapped[2] = { nullptr, nullptr };
    int            m_rippleCells = 0;
    unsigned long long m_rippleVersion = ~0ULL; // sentinel: force the first upload

    VkBuffer       m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexBufferMemory = VK_NULL_HANDLE;
    VkBuffer       m_instanceBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_instanceBufferMemory = VK_NULL_HANDLE;

    std::chrono::high_resolution_clock::time_point m_startTime;

    static constexpr size_t MAX_INSTANCES = 100000;
};

} // namespace Graphics
} // namespace Phyxel
