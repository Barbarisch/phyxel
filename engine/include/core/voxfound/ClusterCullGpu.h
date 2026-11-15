#pragma once
// docs/VoxelFoundationPlan.md F2.11 (§4.6 item 5 + item 3's per-block part) — the GPU draw-building pass.
//
// The 1/8 m faces of each chunk are grouped into clusters of blocks (Mesher.h clusterFaces); each cluster owns ONE
// VkDrawIndexedIndirectCommand {indexCount, instanceCount, 0, 0, firstInstance}. Per frame, shaders/voxfound_cull.comp
// sets instanceCount = the cluster's face count if its chunk is visible (the CPU's per-chunk decision) and its box is
// inside the frustum, else 0; the faces are then drawn with ONE vkCmdDrawIndexedIndirect over every cluster.
// Without compaction culled clusters stay as zero-instance commands (their cost is measured: F2.15).
// F2.15f: with a Compact table at upload, record(..., compact = true) instead APPENDS only the visible clusters, merging
// consecutive mergeable ones (Mesher.h mergeVisibleRuns), into [0, solid) for the solid multidraw and [solid, n) for
// the leaf-mass one, with each command's chunk origin at the same index in drawOrigins(slot) and the two command
// counts in counts(slot) (uint at byte 0 and 4) -- drawn with vkCmdDrawIndexedIndirectCount.
// Per frame slot: its own command buffer and chunk mask (a later frame's cull never overwrites commands an earlier
// frame is still drawing from). Boxes and planes are relative to an anchor the caller chooses (small floats).
// Clusters and commands live in DEVICE-LOCAL memory (filled by a staging copy at upload; the first version kept them
// host-visible and the 26k-cluster pass cost ~1 ms); only the small per-chunk mask is host-visible.

#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

namespace Phyxel::Vulkan { class ComputePipeline; }

namespace Phyxel::Core::VoxFound {

class ClusterCullGpu {
public:
    struct Cluster {
        glm::vec3 lo{0.0f}, hi{0.0f};   // metres, relative to the caller's anchor
        uint32_t firstInstance = 0;     // index of its first face in the face buffer
        uint32_t count = 0;             // faces
        uint32_t chunk = 0;             // index into the chunk mask (< 2^24)
        uint32_t level = 0;             // F2.14: pyramid level of its faces (< 16)
        int dir = -1;                   // F2.15: its faces' direction (Mesher.h faceDirIndex), -1 = all
    };
    /// F2.14: draw a cluster only when its level may be kept by a fragment in its box (Mesher.h lodLevelsForBox).
    /// Without it, only level-0 clusters are drawn.
    struct LodCull {
        glm::vec3 cam{0.0f};   // camera, same frame as the boxes
        float c = 0.0f;        // lodScaleFromCode(code): the same quantized scale voxel.frag uses
        int maxLevel = 0;
    };

    /// F2.15f: per cluster, the origin word its command carries (voxfound_face_mdi.vert drawOrigin), and the split
    /// between the solid multidraw [0, solid) and the leaf-mass one.
    struct Compact {
        std::vector<glm::ivec4> origins;
        uint32_t solid = 0;
    };
    /// F2.15f: merging stops at every multiple of this many clusters (bounds a head's forward scan, and the size of a
    /// merged draw: at 32 the merged draws cost +3.4 ms at forest_floor_open (in the leaf-card pass after them) while
    /// winning 0.85 ms at canopy_close; 8 was better at both, voxfound_f215f_ab_diag_*). Must divide 64.
    static constexpr uint32_t kMergeBlock = 8;

    ClusterCullGpu();
    ~ClusterCullGpu();
    ClusterCullGpu(const ClusterCullGpu&) = delete;
    ClusterCullGpu& operator=(const ClusterCullGpu&) = delete;

    bool init(VkDevice device, VkPhysicalDevice physical, const std::string& shaderDir, uint32_t frameSlots,
              std::string* err);
    void shutdown();
    bool initialized() const { return m_device != VK_NULL_HANDLE; }

    /// (Re)creates the buffers for these clusters: writes every command's static words (indexCount per instance =
    /// `indexCount`, firstInstance) and sets every instanceCount to the full count. Waits for the device.
    bool upload(const std::vector<Cluster>& clusters, uint32_t indexCount, uint32_t chunkCount, VkQueue queue,
                uint32_t queueFamily, std::string* err, const Compact* compact = nullptr);
    bool canCompact() const { return m_compact; }
    /// The per-chunk decision for frame `slot` (1 = visible), size = chunkCount.
    void setChunkVisible(uint32_t slot, const std::vector<uint32_t>& visible);
    /// Records the cull dispatch for frame `slot` and a barrier to the indirect + vertex stages.
    /// F2.15: `dirCam` (camera, same frame as the boxes) turns on the direction test (Mesher.h
    /// faceGroupMayFaceCamera) for clusters that have a direction.
    /// F2.15b: with `dirIsLight`, `dirCam` is the unit vector TOWARDS a directional light instead, and a direction
    /// group is kept iff its faces face the light (Mesher.h faceDirFacesLight).
    /// F2.15f: `compact` (needs a Compact table at upload) appends merged commands instead (see the top).
    void record(VkCommandBuffer cmd, uint32_t slot, const glm::vec4 planes[6], const LodCull* lod = nullptr,
                const glm::vec3* dirCam = nullptr, bool dirIsLight = false, bool compact = false);
    /// Runs record() on its own command buffer and waits (tests).
    bool cullNow(VkQueue queue, uint32_t queueFamily, uint32_t slot, const glm::vec4 planes[6], std::string* err,
                 const LodCull* lod = nullptr, const glm::vec3* dirCam = nullptr, bool dirIsLight = false,
                 bool compact = false);

    uint32_t clusterCount() const { return m_count; }
    VkBuffer commands(uint32_t slot) const { return slot < m_commands.size() ? m_commands[slot].buf : VK_NULL_HANDLE; }
    /// Copies slot's commands (5 words each) back to the host and waits (tests, stats).
    bool readCommands(VkQueue queue, uint32_t queueFamily, uint32_t slot, std::vector<uint32_t>& out, std::string* err);
    /// F2.15f: slot's compacted commands (5 words each, the whole buffer), their origins and the two counts.
    bool readCompact(VkQueue queue, uint32_t queueFamily, uint32_t slot, std::vector<uint32_t>& cmds,
                     std::vector<glm::ivec4>& origins, uint32_t counts[2], std::string* err);
    /// F2.15f: the appended commands (own buffer: the per-cluster commands keep their static words).
    VkBuffer compactCommands(uint32_t slot) const { return slot < m_ccmds.size() ? m_ccmds[slot].buf : VK_NULL_HANDLE; }
    VkBuffer drawOrigins(uint32_t slot) const { return slot < m_origOut.size() ? m_origOut[slot].buf : VK_NULL_HANDLE; }
    VkBuffer counts(uint32_t slot) const { return slot < m_counts.size() ? m_counts[slot].buf : VK_NULL_HANDLE; }
    uint32_t solidCount() const { return m_solid; }
    /// F2.15f: the leaf-mass commands' origins start at this index of drawOrigins(slot) (solid rounded up to 16 entries =
    /// 256 bytes, the largest minStorageBufferOffsetAlignment Vulkan allows, so it can be bound at that offset).
    uint32_t leafOriginBase() const { return (m_solid + 15u) / 16u * 16u; }

private:
    struct Buffer {
        VkBuffer buf = VK_NULL_HANDLE;
        VkDeviceMemory mem = VK_NULL_HANDLE;
        VkDeviceSize bytes = 0;
        void* mapped = nullptr;
    };
    bool createBuffer(Buffer& b, VkDeviceSize bytes, VkBufferUsageFlags usage, bool hostVisible, std::string* err);
    bool copy(VkQueue queue, uint32_t family, VkBuffer src, VkBuffer dst, VkDeviceSize bytes, std::string* err);
    void destroyBuffer(Buffer& b);
    uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags props) const;

    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical = VK_NULL_HANDLE;
    std::unique_ptr<Vulkan::ComputePipeline> m_pipe;
    uint32_t m_slots = 0, m_count = 0, m_chunks = 0, m_indexCount = 36, m_solid = 0;
    bool m_compact = false;
    Buffer m_clusters;
    Buffer m_firsts, m_origIn;   // F2.15f: per cluster, its first face and origin word (placeholders without Compact)
    std::vector<Buffer> m_commands, m_masks;   // one per frame slot
    std::vector<Buffer> m_origOut, m_counts, m_ccmds, m_groups;   // F2.15f: one per frame slot
};

}  // namespace Phyxel::Core::VoxFound
