#pragma once

#include "core/water/RippleLayer.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <chrono>
#include <vector>

#include "core/water/WaterSurfaceMesh.h"

namespace Phyxel {
namespace Graphics {

class Camera;

// WaterCore Phase F (docs/WaterCore.md 17): draws the simulated water's own surface mesh
// (Core::Water::WaterSurfaceMesh, rebuilt on the CPU every frame from the volumes' surface fields).
// Same render pass, scene taps (refraction + depth) and blend state as the sea sheet and the old
// cell renderer; no ripple heightfield (the mesh IS the disturbance). Vertex + index buffers are
// host-visible rings indexed by frame-in-flight, so a frame's upload never overwrites what the
// previous frame's command buffer is still reading.
class WaterSurfaceRenderPipeline {
public:
    WaterSurfaceRenderPipeline();
    ~WaterSurfaceRenderPipeline();

    void initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                    VkRenderPass renderPass, VkExtent2D swapChainExtent,
                    VkDescriptorSetLayout uboLayout);
    void cleanup();
    void setSceneTextures(VkImageView refractionView, VkSampler refractionSampler,
                          VkImageView sceneDepthView, VkSampler sceneDepthSampler);
    /// Upload + draw. `frame` selects the ring slot (0..kFrames-1). A mesh larger than the ring slot is
    /// drawn truncated and reported through `lastTruncated()` - never silently.
    void render(VkCommandBuffer commandBuffer, VkDescriptorSet uboSet, const Camera& camera,
                const glm::mat4& projectionMatrix, const Core::Water::WaterSurfaceMesh& mesh,
                VkExtent2D screenExtent, uint32_t frame);
    void recreatePipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent);

    bool lastTruncated() const { return m_lastTruncated; }
    void setDebugMode(int m) { m_debugMode = m; }
    int  debugMode() const { return m_debugMode; }
    /// WaterCore 21.3 A/B: true draws the pre-21.3 constant (unlit) in-scatter. Default false (lit).
    void setScatterLegacy(bool on) { m_scatterLegacy = on; }
    /// 22: the ripple layers, indexed as the fields' `rippleLayer` (null entries = none); `smooth` = the bilinear A/B.
    void setRipples(const std::vector<const Core::Water::RippleLayer*>* layers, bool smooth) { m_ripples = layers; m_rippleSmooth = smooth; }
    static constexpr int    kRippleIndices = 8;                      ///< header entries (field rippleLayer 0..7)
    static constexpr size_t kRippleFloats = 4u * 512u * 512u;        ///< heights per frame slot (4 layers of 512 x 512, the manager's budget)
    bool scatterLegacy() const { return m_scatterLegacy; }
    uint32_t lastVertices() const { return m_lastVertices; }
    static constexpr uint32_t kFrames = 2;
    static constexpr size_t   kMaxVertices = 1u << 19;   ///< 512 k vertices x 48 B = 24 MB per slot (a 96 x 96 band is ~17 k)
    static constexpr size_t   kMaxIndices  = kMaxVertices * 3 / 2;

private:
    void createDescriptorSetLayout(VkDescriptorSetLayout uboLayout);
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
    bool                  m_texturesBound = false;

    VkBuffer       m_vertexBuffer[kFrames] = {};
    VkDeviceMemory m_vertexMemory[kFrames] = {};
    void*          m_vertexMapped[kFrames] = {};
    VkBuffer       m_indexBuffer[kFrames] = {};
    VkDeviceMemory m_indexMemory[kFrames] = {};
    void*          m_indexMapped[kFrames] = {};
    bool     m_lastTruncated = false;
    int      m_debugMode = 0;
    bool     m_scatterLegacy = false;
    // 22: ripple heights, one host-coherent buffer of kFrames slots, bound as a dynamic storage buffer (set 1 binding 2)
    VkBuffer m_rippleBuffer = VK_NULL_HANDLE; VkDeviceMemory m_rippleMemory = VK_NULL_HANDLE; void* m_rippleMapped = nullptr;
    VkDeviceSize m_rippleSlotBytes = 0;
    const std::vector<const Core::Water::RippleLayer*>* m_ripples = nullptr;
    bool m_rippleSmooth = false;
    uint32_t m_lastVertices = 0;
    std::chrono::high_resolution_clock::time_point m_startTime;
};

} // namespace Graphics
} // namespace Phyxel
