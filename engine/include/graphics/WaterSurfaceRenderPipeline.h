#pragma once

#include "core/water/RippleLayer.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <chrono>
#include <map>
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

    // 26.30: the surface in TILES kept on the device (docs/WaterCore.md 26.30). uploadTiles records the copies of the tiles that
    // were rebuilt since they were last uploaded - OUTSIDE a render pass, before the water pass; renderTiles draws the tiles whose
    // box is inside the view (all of them with culling off) and then `extra` (the droplets) through the per-frame ring.
    void uploadTiles(VkCommandBuffer commandBuffer, const Core::Water::WaterSurfaceTiler& tiler, uint32_t frame);
    void renderTiles(VkCommandBuffer commandBuffer, VkDescriptorSet uboSet, const Camera& camera, const glm::mat4& projectionMatrix,
                     const Core::Water::WaterSurfaceTiler& tiler, const Core::Water::WaterSurfaceMesh& extra, VkExtent2D screenExtent, uint32_t frame);
    void setCull(bool on) { m_cull = on; }
    bool cull() const { return m_cull; }
    struct TileStats { long tiles = 0, drawn = 0, culled = 0, uploaded = 0, deferred = 0, overArena = 0; size_t uploadBytes = 0, arenaVertexBytes = 0, arenaIndexBytes = 0; uint32_t drawnVertices = 0; };
    const TileStats& tileStats() const { return m_tileStats; }
    static constexpr VkDeviceSize kArenaVertexBytes = 96ull << 20;   ///< 2 M vertices (48 B) on the device
    static constexpr VkDeviceSize kArenaIndexBytes  = 16ull << 20;   ///< 4 M indices
    static constexpr VkDeviceSize kStagingBytes     = 24ull << 20;   ///< per frame slot: what one frame may upload (the rest waits a frame)

    bool lastTruncated() const { return m_lastTruncated; }
    void setDebugMode(int m) { m_debugMode = m; }
    int  debugMode() const { return m_debugMode; }
    /// WaterCore 21.3 A/B: true draws the pre-21.3 constant (unlit) in-scatter. Default false (lit).
    void setScatterLegacy(bool on) { m_scatterLegacy = on; }
    void setHorizonGuard(bool on) { m_horizonGuard = on; }   ///< WaterCore 22.12 B2 (default on; off = the A/B control)
    /// WaterCore 25.26 M: the simulated water's default Secchi depth (m) where a body sets none; 0 = the derived clear water.
    void setDefaultClarity(float m) { m_defaultClarity = m < 0.0f ? 0.0f : m; }
    float defaultClarity() const { return m_defaultClarity; }
    bool horizonGuard() const { return m_horizonGuard; }
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
    bool     m_horizonGuard = true;
    float    m_defaultClarity = 5.0f;   // 25.26 M (owner: "a slight murkiness... not much"; 5 m picked from the A/B sheets, 3 m read too strong on the creek)
    // 22: ripple heights, one host-coherent buffer of kFrames slots, bound as a dynamic storage buffer (set 1 binding 2)
    VkBuffer m_rippleBuffer = VK_NULL_HANDLE; VkDeviceMemory m_rippleMemory = VK_NULL_HANDLE; void* m_rippleMapped = nullptr;
    VkDeviceSize m_rippleSlotBytes = 0;
    const std::vector<const Core::Water::RippleLayer*>* m_ripples = nullptr;
    bool m_rippleSmooth = false;
    uint32_t m_lastVertices = 0;
    std::chrono::high_resolution_clock::time_point m_startTime;
    void pushLook(VkCommandBuffer commandBuffer, const Camera& camera, const glm::mat4& projectionMatrix, VkExtent2D screenExtent, const Core::Water::WaterLookPacked* look);
    void writeRipples(uint32_t slot);

    // 26.30 the tile store: two device-local arenas, a first-fit free list each (offset -> bytes), frees held back until no frame
    // in flight can still read them
    struct Arena {
        VkBuffer buffer = VK_NULL_HANDLE; VkDeviceMemory memory = VK_NULL_HANDLE; VkDeviceSize size = 0, used = 0;
        std::map<VkDeviceSize, VkDeviceSize> freeList;
        bool alloc(VkDeviceSize bytes, VkDeviceSize& off);
        void release(VkDeviceSize off, VkDeviceSize bytes);
    };
    struct GpuTile { VkDeviceSize vOff = 0, vBytes = 0, iOff = 0, iBytes = 0; uint32_t indexCount = 0; uint64_t generation = 0; };
    struct PendingFree { uint64_t frame; VkDeviceSize vOff, vBytes, iOff, iBytes; };
    Arena m_vArena, m_iArena;
    VkBuffer m_staging[kFrames] = {}; VkDeviceMemory m_stagingMemory[kFrames] = {}; void* m_stagingMapped[kFrames] = {};
    std::map<Core::Water::SurfaceTileKey, GpuTile> m_gpuTiles;
    std::vector<PendingFree> m_pendingFree;
    uint64_t m_frameCounter = 0;
    bool m_cull = true;
    TileStats m_tileStats;
    void createTileStore();
    void destroyTileStore();
};

} // namespace Graphics
} // namespace Phyxel
