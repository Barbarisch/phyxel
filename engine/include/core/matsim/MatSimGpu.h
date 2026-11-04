#pragma once
// docs/MaterialSimPlan.md §6c — MatSim P1b: the GPU port of MatSimCore::step.
//
// The CPU core stays the owner of the state and of every edit (fill, resolidify, cell-size switch,
// trapped contents, the expected ledger). This class steps a DEVICE COPY of the core's stepping
// state (cells, tick, the two sleep mark sets): upload(core) -> step(n) ... -> download(core).
// The kernels (shaders/matsim_block.comp, matsim_dilate.comp) are a line-for-line port and must
// stay bit-identical to the CPU — pinned by MatSimGpuParity (tests/integration).
//
// Per tick: dilate (the brick process mask, and clear this tick's marks) then block (the rules).
// Cells ping-pong between two buffers and marks rotate over three, so each tick binds one of six
// descriptor sets (tick mod 6). One submission and one fence per chunk of up to 256 ticks.

#include "core/matsim/MatSimCore.h"
#include <vulkan/vulkan.h>
#include <memory>
#include <string>

namespace Phyxel { namespace Vulkan { class ComputePipeline; } }

namespace Phyxel::Core::MatSim {

class MatSimGpu {
public:
    MatSimGpu();
    ~MatSimGpu();
    MatSimGpu(const MatSimGpu&) = delete;
    MatSimGpu& operator=(const MatSimGpu&) = delete;

    bool init(VkDevice device, VkPhysicalDevice physical, VkQueue queue, uint32_t queueFamily,
              const std::string& shaderDir, std::string* err);
    void shutdown();
    bool initialized() const { return m_device != VK_NULL_HANDLE; }

    /// Copy the core's stepping state to the device, (re)allocating for its box. Refuses (false +
    /// reason) for an uncreated core or a box whose buffers cannot be allocated.
    bool upload(const MatSimCore& core, std::string* err);
    bool loaded() const { return m_cells > 0; }
    /// Free the box's buffers (the pipelines stay).
    void release();

    /// Step n ticks on the device. Stats as MatSimCore::step reports them.
    StepStats step(int n);
    /// Bring the stepped state back into the core (cells, tick, marks). The core must hold the
    /// box that was uploaded.
    bool download(MatSimCore& core, std::string* err);

    uint64_t tick() const { return m_tick; }
    /// Device time of the last step() call per tick, from timestamps (ms); < 0 when unavailable.
    double lastGpuMsPerTick() const { return m_lastMsPerTick; }

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
    VkCommandBuffer beginCommands() const;
    void submitAndWait(VkCommandBuffer cmd) const;
    void barrier(VkCommandBuffer cmd, VkPipelineStageFlags src, VkAccessFlags srcAccess,
                 VkPipelineStageFlags dst, VkAccessFlags dstAccess) const;
    void bindSets();

    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    VkCommandPool m_pool = VK_NULL_HANDLE;
    VkFence m_fence = VK_NULL_HANDLE;
    VkQueryPool m_queries = VK_NULL_HANDLE;
    double m_timestampMs = 0.0;   ///< ms per timestamp tick; 0 = no timestamps on this queue
    std::unique_ptr<Vulkan::ComputePipeline> m_block, m_dilate;

    // The box (from the core at upload).
    IVec3 m_origin{}, m_size{}, m_brickLo{}, m_brickDim{};
    uint32_t m_full = 0, m_seed = 0;
    bool m_sleep = true;
    size_t m_cells = 0, m_bricks = 0;
    uint64_t m_tick = 0;   ///< the simulation tick (the core's clock)
    uint64_t m_g = 0;      ///< ticks stepped since upload: selects the buffer roles
    Buffer m_cellBuf[2], m_marks[3], m_process, m_mat, m_stats, m_staging;
    double m_lastMsPerTick = -1.0;
};

} // namespace Phyxel::Core::MatSim
