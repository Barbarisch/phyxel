#pragma once
// docs/VoxelFoundationPlan.md §4.1, §4.6 items 1-2, F2.1 — the GPU face builder.
//
// Builds the faces of one chunk-sized voxel grid (each side 1..256 voxels) on the device:
//   * hidden-face culling: a face exists where a solid voxel's neighbour across it is air
//     (outside the grid: Outside::Air = air, Outside::Skip = no face);
//   * run merging across the whole chunk (meshRuns, Mesher.h): one thread per slice row, no ordering
//     between threads, so the result is a pure function of the voxels.
// Output is the 8-byte PackedFace record. Accepted only on EXACT equality with the CPU reference
// meshRuns (as a set) — pinned by VoxFoundFaceBuilderGpu (tests/integration). Kernel:
// shaders/voxfound_faces.comp.
//
// Not yet: neighbour chunks at the grid border (§4.6 item 1), the block solidity masks / uniform-block
// skip, partial rebuild of changed rows (§4.6 item 9). This is the exact core those build on.

#include <memory>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "core/voxfound/Mesher.h"
#include "core/voxfound/VoxelGrid.h"

namespace Phyxel { namespace Vulkan { class ComputePipeline; } }

namespace Phyxel::Core::VoxFound {

class FaceBuilderGpu {
public:
    FaceBuilderGpu();
    ~FaceBuilderGpu();
    FaceBuilderGpu(const FaceBuilderGpu&) = delete;
    FaceBuilderGpu& operator=(const FaceBuilderGpu&) = delete;

    bool init(VkDevice device, VkPhysicalDevice physical, VkQueue queue, uint32_t queueFamily,
              const std::string& shaderDir, std::string* err);
    void shutdown();
    bool initialized() const { return m_device != VK_NULL_HANDLE; }

    /// Build g's faces on the device into `out` (order unspecified). Refuses (false + reason) a grid with
    /// a side of 0 or more than kMaxChunkVoxels. Grows its face buffer and rebuilds when the faces
    /// overflow it, so the result is always complete.
    /// `sprigs` (F2.3, optional): its card materials emit no faces (meshRuns with cards), and a fourth pass
    /// appends their leaf sprigs (foliageSprigs) to `out` as axis-3 records (isSprig). Exact vs the CPU pair.
    bool build(const VoxelGrid& g, Outside outside, std::vector<PackedFace>& out, std::string* err,
               const SprigParams* sprigs = nullptr);

    /// Device time of the last build's face pass (ms, from timestamps; < 0 when unavailable).
    double lastGpuMs() const { return m_lastGpuMs; }
    /// Face-buffer capacity in faces (it grows on overflow).
    size_t capacity() const { return m_capacity; }
    void setInitialCapacity(size_t faces) { m_initialCapacity = faces; }

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
    bool ensureBuffers(size_t voxelWords, size_t faces, std::string* err);

    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    VkCommandPool m_pool = VK_NULL_HANDLE;
    VkFence m_fence = VK_NULL_HANDLE;
    VkQueryPool m_queries = VK_NULL_HANDLE;
    double m_timestampMs = 0.0;
    std::unique_ptr<Vulkan::ComputePipeline> m_faces;

    Buffer m_voxels, m_out, m_counter, m_stagingIn, m_stagingOut;
    size_t m_voxelWords = 0, m_capacity = 0, m_initialCapacity = 1u << 16;
    double m_lastGpuMs = -1.0;
};

}  // namespace Phyxel::Core::VoxFound
