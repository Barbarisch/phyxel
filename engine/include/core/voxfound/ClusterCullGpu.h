#pragma once
// docs/VoxelFoundationPlan.md F2.11 (§4.6 item 5 + item 3's per-block part) — the GPU draw-building pass.
//
// The 1/8 m faces of each chunk are grouped into clusters of blocks (Mesher.h clusterFaces); each cluster owns ONE
// VkDrawIndexedIndirectCommand {indexCount, instanceCount, 0, 0, firstInstance}. Per frame, shaders/voxfound_cull.comp
// sets instanceCount = the cluster's face count if its chunk is visible (the CPU's per-chunk decision) and its box is
// inside the frustum, else 0; the faces are then drawn with ONE vkCmdDrawIndexedIndirect over every cluster.
// No indirect-count: VulkanDevice does not enable the 1.2 drawIndirectCount feature, so culled clusters stay as
// zero-instance commands (their cost is measured).
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
        uint32_t chunk = 0;             // index into the chunk mask
    };

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
                uint32_t queueFamily, std::string* err);
    /// The per-chunk decision for frame `slot` (1 = visible), size = chunkCount.
    void setChunkVisible(uint32_t slot, const std::vector<uint32_t>& visible);
    /// Records the cull dispatch for frame `slot` and a barrier to the indirect + vertex stages.
    void record(VkCommandBuffer cmd, uint32_t slot, const glm::vec4 planes[6]);
    /// Runs record() on its own command buffer and waits (tests).
    bool cullNow(VkQueue queue, uint32_t queueFamily, uint32_t slot, const glm::vec4 planes[6], std::string* err);

    uint32_t clusterCount() const { return m_count; }
    VkBuffer commands(uint32_t slot) const { return slot < m_commands.size() ? m_commands[slot].buf : VK_NULL_HANDLE; }
    /// Copies slot's commands (5 words each) back to the host and waits (tests, stats).
    bool readCommands(VkQueue queue, uint32_t queueFamily, uint32_t slot, std::vector<uint32_t>& out, std::string* err);

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
    uint32_t m_slots = 0, m_count = 0, m_chunks = 0;
    Buffer m_clusters;
    std::vector<Buffer> m_commands, m_masks;   // one per frame slot
};

}  // namespace Phyxel::Core::VoxFound
