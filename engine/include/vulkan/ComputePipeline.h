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
 *   pipe.create(device, physDevice, "shaders/particle_integrate.comp.spv",
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
     */
    bool create(VkDevice device, const std::string& spvPath,
                uint32_t bindingCount, uint32_t pushConstSize);

    void cleanup();

    /**
     * Set the buffer for a given binding slot.
     * Call updateDescriptors() after all slots are set.
     */
    void bindBuffer(uint32_t binding, VkBuffer buffer, VkDeviceSize size, VkDeviceSize offset = 0);

    /**
     * Write all bound buffers to the descriptor set.
     * Must be called before the first dispatch and after any bindBuffer() change.
     */
    void updateDescriptors();

    /** Record vkCmdBindPipeline + vkCmdBindDescriptorSets */
    void bind(VkCommandBuffer cmd) const;

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
    VkDescriptorSet       m_ds             = VK_NULL_HANDLE;

    uint32_t              m_bindingCount   = 0;
    uint32_t              m_pushConstSize  = 0;

    std::vector<BufferBinding> m_bindings;
};

} // namespace Vulkan
} // namespace Phyxel
