#pragma once
// docs/MaterialSimPlan.md §6h — the GPU port of MatSimParticles::step (experiment E1/E2's particle
// fluid). The CPU MatSimParticles stays the owner of the particles and of everything that is not
// the per-substep solve (lift, wake, settle, deposit, the averaged velocity). One step() call =
// one frame: upload the particles and the box's cells, record every substep (predict, neighbour
// grid by counting sort, density iterations, velocity, divergence iterations, viscosity + commit)
// into one submission, wait, download positions and velocities.
//
// Not bit-exact with the CPU (float order is kept, but FMA contraction and the driver's division
// differ in the last bits; the fluid is chaotic, so differences grow). Accepted on the measured
// physics instead: MatSimParticlesGpu tests (tests/integration).

#include "core/matsim/MatSimCore.h"
#include <vulkan/vulkan.h>
#include <memory>
#include <string>

namespace Phyxel { namespace Vulkan { class ComputePipeline; } }

namespace Phyxel::Core::MatSim {

class MatSimParticles;

class MatSimParticlesGpu {
public:
    MatSimParticlesGpu();
    ~MatSimParticlesGpu();
    MatSimParticlesGpu(const MatSimParticlesGpu&) = delete;
    MatSimParticlesGpu& operator=(const MatSimParticlesGpu&) = delete;

    bool init(VkDevice device, VkPhysicalDevice physical, VkQueue queue, uint32_t queueFamily,
              const std::string& shaderDir, std::string* err);
    void shutdown();
    bool initialized() const { return m_device != VK_NULL_HANDLE; }

    /// One frame of ps.step(core, dt) on the device. Refuses (false + reason, nothing changed) for
    /// a config the kernels do not implement (tensileK > 0) or buffers that cannot be allocated.
    bool step(MatSimParticles& ps, const MatSimCore& core, double dt, std::string* err);

    /// Device time of the last step() (ms, timestamps); < 0 when unavailable.
    double lastGpuMs() const { return m_lastMs; }
    /// Wall time of the last step() including uploads and the wait (ms).
    double lastWallMs() const { return m_lastWallMs; }
    /// 6l: the last step()'s host split (ms): staging fill, record+submit+wait, read-back (+ endFrame).
    double lastPackMs() const { return m_packMs; }
    double lastWaitMs() const { return m_waitMs; }
    double lastUnpackMs() const { return m_unpackMs; }

private:
    struct Buffer {
        VkBuffer buf = VK_NULL_HANDLE;
        VkDeviceMemory mem = VK_NULL_HANDLE;
        VkDeviceSize bytes = 0;
        void* mapped = nullptr;
    };
    bool createBuffer(Buffer& b, VkDeviceSize bytes, bool hostVisible, std::string* err);
    void destroyBuffer(Buffer& b);
    bool ensure(Buffer& b, VkDeviceSize bytes, bool hostVisible, bool& changed, std::string* err);
    uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags props) const;
    void release();

    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    VkCommandPool m_pool = VK_NULL_HANDLE;
    VkFence m_fence = VK_NULL_HANDLE;
    VkQueryPool m_queries = VK_NULL_HANDLE;
    double m_timestampMs = 0.0;
    std::unique_ptr<Vulkan::ComputePipeline> m_pipe;

    Buffer m_parts, m_work, m_keys, m_sorted, m_count, m_start, m_blocks, m_cells, m_mat, m_staging;
    double m_lastMs = -1.0, m_lastWallMs = 0.0;
    double m_packMs = 0.0, m_waitMs = 0.0, m_unpackMs = 0.0;
};

} // namespace Phyxel::Core::MatSim
