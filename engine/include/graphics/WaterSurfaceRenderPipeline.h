#pragma once

#include "core/water/RippleLayer.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <chrono>
#include <map>
#include <vector>

#include "core/water/WaterSurfaceMesh.h"
#include "core/water/WaterGpuSurfaceDraw.h"

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
    // 26.30 the tile store: two device-local arenas, a first-fit free list each (offset -> bytes), frees held back until no frame
    // in flight can still read them. Every offset is a multiple of `align`: a tile is drawn from vertexOffset = offset / the
    // vertex size, so the VERTEX arena's alignment must be a whole number of vertices (kVertexArenaAlign) - at 256 a wavy tile
    // (any vertex count) landed mid-vertex and drew garbage: holes in the pond after a stone (found in the owner's video,
    // 2026-10-18; a resting tile, 5,184 vertices, kept every offset a multiple of 48 by chance - the still-pond A/B passed).
    struct Arena {
        VkBuffer buffer = VK_NULL_HANDLE; VkDeviceMemory memory = VK_NULL_HANDLE; VkDeviceSize size = 0, used = 0;
        VkDeviceSize align = 256;
        std::map<VkDeviceSize, VkDeviceSize> freeList;
        bool alloc(VkDeviceSize bytes, VkDeviceSize& off);
        void release(VkDeviceSize off, VkDeviceSize bytes);
        /// 27.27: the arena is now `newSize` bytes - every offset in use stays where it is, [size, newSize) joins the free list
        void grow(VkDeviceSize newSize);
    };
    static constexpr VkDeviceSize kVertexArenaAlign = 768;   // lcm(256, sizeof(WaterSurfaceVertex) = 48)

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
    // 29 S1 (docs/WaterCore.md 29): the volumes whose surface the device made, drawn by vertex pulling (water_surface_pull.vert): each
    // column's first-run top (vkCmdDraw) and the extras list (vkCmdDrawIndirect). gpuSurfaceBarrier goes OUTSIDE the render pass,
    // before it: the water's call (same queue, submitted earlier) wrote the buffers.
    void renderGpuSurfaces(VkCommandBuffer commandBuffer, VkDescriptorSet uboSet, const Camera& camera, const glm::mat4& projectionMatrix,
                           const std::vector<Core::Water::GpuSurfaceDraw>& draws, VkExtent2D screenExtent, uint32_t frame);
    static void gpuSurfaceBarrier(VkCommandBuffer commandBuffer);
    bool gpuSurfaceReady() const { return m_pullPipeline != VK_NULL_HANDLE; }
    long lastGpuSurfaceDraws() const { return m_lastGpuSurfaceDraws; }
    void setCull(bool on) { m_cull = on; }
    bool cull() const { return m_cull; }
    struct TileStats { long tiles = 0, drawn = 0, culled = 0, uploaded = 0, deferred = 0, overArena = 0, grows = 0, growsTotal = 0, holesDeferred = 0, oldestWaitFrames = 0; size_t uploadBytes = 0, arenaVertexBytes = 0, arenaIndexBytes = 0, arenaVertexSize = 0, arenaIndexSize = 0; uint32_t drawnVertices = 0; };
    const TileStats& tileStats() const { return m_tileStats; }
    static constexpr VkDeviceSize kArenaVertexBytes = 96ull << 20;   ///< 2 M vertices (48 B) on the device
    static constexpr VkDeviceSize kArenaIndexBytes  = 16ull << 20;   ///< 4 M indices
    /// 27.27 THE ARENAS GROW. At their first size a 60 x 60 m lake (240 tiles resident, ~64 awake tiles re-uploaded a frame, each
    /// old copy held kFrames frames) filled the vertex arena, and a tile that did not fit was SKIPPED - not drawn: whole squares of
    /// the lake showed dry on screen while the water was all there (the owner's fixed-tick clip, 2026-10-24). A full arena now
    /// doubles (a new buffer, the old copied in on the device at the same offsets, the old freed once no frame in flight reads it)
    /// up to these caps; past them a tile is still skipped - counted (overArena) and logged.
    static constexpr VkDeviceSize kArenaVertexMaxBytes = 1024ull << 20;
    static constexpr VkDeviceSize kArenaIndexMaxBytes  = 192ull << 20;
    /// TEST ONLY (the live red control, water_render_core {arena_no_grow}): 27.27 undone - a full arena skips the tile
    static inline bool s_arenaNoGrowForTest = false;
    /// 27.27 THE UPLOAD ORDER. A frame uploads at most kStagingBytes; the rest waits. The tiles were visited in key order, so with
    /// more changing tiles than fit (a 60 x 60 m lake awake: ~135 deferred a frame) the same front tiles won every frame and the
    /// back ones NEVER uploaded - a tile empty while its square was in the record kept drawing empty after the water woke: a dry L
    /// on screen for 15 s+. Now: tiles drawing nothing where the new mesh has water first, then the longest-waiting. TEST ONLY
    /// (the live red control, water_render_core {upload_key_order}): the key order.
    static inline bool s_uploadKeyOrderForTest = false;
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
    /// L2 (43.2): the ripples' fade by distance - (viewer x, viewer z, near, far); far < 0 = none (weight 1)
    void setRippleFade(const glm::vec4& f) { m_rippleFade = f; }
    static constexpr int    kRippleIndices = 8;                      ///< header entries (field rippleLayer 0..7)
    static constexpr size_t kRippleHeaderBytes = 16 + kRippleIndices * 32;   ///< vec4 fade, vec4 geo[8], ivec4 meta[8]
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
    VkDescriptorSetLayout m_uboLayout = VK_NULL_HANDLE;   // 29 S1: the scene's (the pull layout's set 0)
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
    glm::vec4 m_rippleFade{0.0f, 0.0f, 0.0f, -1.0f};
    uint32_t m_lastVertices = 0;
    std::chrono::high_resolution_clock::time_point m_startTime;
    void pushLook(VkCommandBuffer commandBuffer, const Camera& camera, const glm::mat4& projectionMatrix, VkExtent2D screenExtent, const Core::Water::WaterLookPacked* look);
    void writeRipples(uint32_t slot);

    struct GpuTile { VkDeviceSize vOff = 0, vBytes = 0, iOff = 0, iBytes = 0; uint32_t indexCount = 0; uint64_t generation = 0; };
    struct PendingFree { uint64_t frame; VkDeviceSize vOff, vBytes, iOff, iBytes; };
    Arena m_vArena, m_iArena;
    VkBuffer m_staging[kFrames] = {}; VkDeviceMemory m_stagingMemory[kFrames] = {}; void* m_stagingMapped[kFrames] = {};
    std::map<Core::Water::SurfaceTileKey, GpuTile> m_gpuTiles;
    std::vector<PendingFree> m_pendingFree;
    struct RetiredBuffer { uint64_t frame; VkBuffer buffer; VkDeviceMemory memory; };
    std::map<Core::Water::SurfaceTileKey, uint64_t> m_waitingSince;   ///< 27.27: tiles whose rebuilt mesh is not uploaded yet - the frame they began waiting
    std::vector<RetiredBuffer> m_retiredBuffers;   ///< 27.27: arenas outgrown - freed once no frame in flight can read them
    long m_arenaGrows = 0;
    bool m_overArenaLogged = false;
    void makeDeviceBuffer(VkBuffer& buf, VkDeviceMemory& mem, VkDeviceSize bytes, VkBufferUsageFlags usage, VkMemoryPropertyFlags props);
    bool growArena(VkCommandBuffer cmd, Arena& a, VkDeviceSize need, VkDeviceSize maxBytes, VkBufferUsageFlags usage);
    uint64_t m_frameCounter = 0;
    bool m_cull = true;
    TileStats m_tileStats;
    void createTileStore();
    void destroyTileStore();
    // 29 S1: the pull pipeline (no vertex input; set 2 = the volume's field / corners / meta / list; push 128 B shared + 32 B vertex)
    VkPipeline m_pullPipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pullLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_pullSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_pullPool = VK_NULL_HANDLE;
    struct PullSet { uint64_t serial = 0; VkDescriptorSet set = VK_NULL_HANDLE; uint64_t lastFrame = 0; };
    std::vector<PullSet> m_pullSets;
    uint64_t m_pullFrame = 0;
    long m_lastGpuSurfaceDraws = 0;
    void createPullPipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent);
    void destroyPullPipeline();
};

} // namespace Graphics
} // namespace Phyxel
