#pragma once
// docs/VoxelFoundationPlan.md F2.14e (§4.6 item 7 continued) — the GPU level build.
//
// Builds pyramid levels 1..n of one voxel grid (a chunk's 1 m margin area, sides even down to level n, each side up
// to kMaxSide) on the device: shaders/voxfound_lod.comp, one thread per coarse cell, no ordering between threads.
//   pass 0: level 1 straight from the voxel bytes (lodBase fused in: a voxel's surface = it touches air INSIDE the
//           grid; outside the grid is unknown, as lodBase);
//   pass 1: level k+1 from level k (lodDown's cell step: count sum, surface and volume votes, half-solid rule);
//   pass 2: lodDown's neighbour step (a solid cell covering no surface takes its air face neighbours' surface vote).
// Accepted only on EXACT equality with the CPU reference lodBase + lodDown, every channel (VoxFoundLodBuilderGpu,
// tests/integration). Output: out[k - 1] = level k as a LodGrid (all channels; `part`/`fixed` stay 0, as lodDown's).

#include <memory>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "core/voxfound/Mesher.h"
#include "core/voxfound/VoxelGrid.h"

namespace Phyxel { namespace Vulkan { class ComputePipeline; } }

namespace Phyxel::Core::VoxFound {

class LodBuilderGpu {
public:
    static constexpr int kMaxSide = 512;
    static constexpr int kMaxLevels = 7;

    LodBuilderGpu();
    ~LodBuilderGpu();
    LodBuilderGpu(const LodBuilderGpu&) = delete;
    LodBuilderGpu& operator=(const LodBuilderGpu&) = delete;

    bool init(VkDevice device, VkPhysicalDevice physical, VkQueue queue, uint32_t queueFamily,
              const std::string& shaderDir, std::string* err);
    void shutdown();
    bool initialized() const { return m_device != VK_NULL_HANDLE; }

    /// Levels 1..levels of g into out (out[k - 1] = level k). Refuses (false + reason) levels outside 1..kMaxLevels,
    /// a side over kMaxSide, or a side not divisible by 2^levels.
    bool build(const VoxelGrid& g, int levels, std::vector<LodGrid>& out, std::string* err);
    /// Device time of the last build's passes (ms, from timestamps; < 0 when unavailable).
    double lastGpuMs() const { return m_lastGpuMs; }

private:
    struct Buffer {
        VkBuffer buf = VK_NULL_HANDLE;
        VkDeviceMemory mem = VK_NULL_HANDLE;
        VkDeviceSize bytes = 0;
        void* mapped = nullptr;
    };
    bool createBuffer(Buffer& b, VkDeviceSize bytes, bool hostVisible, std::string* err);
    void destroyBuffer(Buffer& b);
    uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags props) const;
    bool ensureBuffers(size_t voxelWords, size_t cells, std::string* err);

    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    VkCommandPool m_pool = VK_NULL_HANDLE;
    VkFence m_fence = VK_NULL_HANDLE;
    VkQueryPool m_queries = VK_NULL_HANDLE;
    double m_timestampMs = 0.0;
    double m_lastGpuMs = -1.0;
    std::unique_ptr<Vulkan::ComputePipeline> m_pipe;
    Buffer m_voxels, m_stagingIn;              // level-0 bytes, 4 per word
    Buffer m_mat, m_votes, m_count, m_surf;    // every level >= 1, concatenated (one uint per cell each)
    Buffer m_stagingOut;                       // the four channels read back
    size_t m_voxelWords = 0, m_cells = 0;
};

}  // namespace Phyxel::Core::VoxFound
