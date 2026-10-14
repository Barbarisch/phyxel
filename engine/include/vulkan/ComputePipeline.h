#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>

namespace Phyxel {
namespace Vulkan {

/**
 * ComputePipeline — thin wrapper around a Vulkan compute pipeline.
 *
 * Usage:
 *   ComputePipeline pipe;
 *   pipe.create(device, physDevice, "shaders/solver_integrate.comp.spv",
 *               { {0, STORAGE_BUFFER, COMPUTE}, {1, STORAGE_BUFFER, COMPUTE} },
 *               sizeof(MyPushConstants));
 *
 *   // Per frame:
 *   pipe.bindBuffer(0, particleBuffer, bufferSize);
 *   pipe.updateDescriptors();          // call once after all bindBuffer calls
 *   pipe.bind(cmd);
 *   pipe.pushConstants(cmd, &pc, sizeof(pc));
 *   pipe.dispatch(cmd, groupsX);
 *   pipe.cleanup();
 *
 * Each ComputePipeline owns exactly one VkDescriptorSet backed by a fixed pool.
 * Re-create the pipeline if the shader or binding layout changes.
 */
class ComputePipeline {
public:
    ComputePipeline() = default;
    ~ComputePipeline() = default;

    // Non-copyable
    ComputePipeline(const ComputePipeline&)            = delete;
    ComputePipeline& operator=(const ComputePipeline&) = delete;

    struct BufferBinding {
        uint32_t binding;
        VkBuffer buffer     = VK_NULL_HANDLE;
        VkDeviceSize size   = 0;
        VkDeviceSize offset = 0;
    };

    /**
     * Create the pipeline.
     * @param device          Logical device
     * @param spvPath         Path to compiled .spv file
     * @param bindingCount    Number of storage buffer bindings (all STORAGE_BUFFER at COMPUTE stage)
     * @param pushConstSize   Size in bytes of push constants (0 = none)
     * @param setCount        Descriptor sets to allocate — one per frame in flight when a binding
     *                        is a per-frame-slot buffer (the shared occupancy pool, which the CPU
     *                        rewrites for the OTHER frame while this one reads). Default 1.
     */
    bool create(VkDevice device, const std::string& spvPath,
                uint32_t bindingCount, uint32_t pushConstSize, uint32_t setCount = 1);

    void cleanup();

    /**
     * Set the buffer for a given binding in EVERY descriptor set.
     * Call updateDescriptors() after all slots are set.
     */
    void bindBuffer(uint32_t binding, VkBuffer buffer, VkDeviceSize size, VkDeviceSize offset = 0);

    /** Set the buffer for a binding in ONE descriptor set (a per-frame-slot buffer). */
    void bindBufferInSet(uint32_t set, uint32_t binding, VkBuffer buffer, VkDeviceSize size,
                         VkDeviceSize offset = 0);

    /**
     * Write all bound buffers to every descriptor set.
     * Must be called before the first dispatch and after any bindBuffer() change.
     */
    void updateDescriptors();

    /** Record vkCmdBindPipeline + vkCmdBindDescriptorSets with descriptor set `set` (0 by default). */
    void bind(VkCommandBuffer cmd, uint32_t set = 0) const;

    uint32_t setCount() const { return static_cast<uint32_t>(m_sets.size()); }

    /** Record vkCmdPushConstants */
    void pushConstants(VkCommandBuffer cmd, const void* data, uint32_t size) const;

    /** Record vkCmdDispatch */
    void dispatch(VkCommandBuffer cmd, uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1) const;

    bool isValid() const { return m_pipeline != VK_NULL_HANDLE; }

private:
    static std::vector<char> loadSpv(const std::string& path);

    VkDevice              m_device         = VK_NULL_HANDLE;
    VkPipeline            m_pipeline       = VK_NULL_HANDLE;
    VkPipelineLayout      m_pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_dsLayout       = VK_NULL_HANDLE;
    VkDescriptorPool      m_dsPool         = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_sets;   ///< one per frame slot (size 1 for ordinary passes)

    uint32_t              m_bindingCount   = 0;
    uint32_t              m_pushConstSize  = 0;

    std::vector<std::vector<BufferBinding>> m_bindings;   ///< [set][binding]
};

} // namespace Vulkan
} // namespace Phyxel
