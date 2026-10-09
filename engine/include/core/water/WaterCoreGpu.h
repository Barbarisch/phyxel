// WaterCoreGpu — the WaterCore fill solver as Vulkan compute (docs/WaterCore.md §15.11, Phase C).
//
// The CPU reference (WaterSolver, WaterCore.h) stays the oracle; this class runs the SAME rules as
// kernels (shaders/wc_*.comp) on a grid uploaded from a WaterGrid and is accepted on parity rows,
// never on looks. Slice 1 (fills only): host-visible, persistently mapped buffers; one command
// buffer per step() call (ticks x substeps of dispatches), submitted and fenced; the answer is
// read back into the WaterGrid by download(). Raw Vulkan handles so the same class runs under the
// integration test fixture and under the engine's VulkanDevice.
#pragma once

#include "core/water/WaterCore.h"
#include "vulkan/ComputePipeline.h"
#include <vulkan/vulkan.h>
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace Phyxel { namespace Core { namespace Water {

/// Push block shared by every kernel (shaders/water_core.glsl WcPush).
struct WcPush {
    int32_t nx = 0, ny = 0, nz = 0, mode = 0;
    float dt = 0.0f, h = 1.0f, thr = 0.5f, gravity = 9.81f;
    float filmHold = 0.01f, param0 = 0.0f, param1 = 0.0f;
    int32_t iparam = 0;
};
static_assert(sizeof(WcPush) == 48, "WcPush must match the GLSL push block");

struct GpuStepStats {
    int    ticks = 0;
    int    substepsLast = 0;
    int    sweeps = 0;
    float  rbgsResidualMax = 0.0f;   ///< max |A p - b| over liquid cells after the last substep's sweeps
    double kineticEnergy = 0.0;
    double maxDeltaF = 0.0;
    double totalMass = 0.0;          ///< m^3 (from the fill reduction)
    double maxSpeed = 0.0;
    double residueDropped = 0.0;     ///< m^3 (sweep; downward merge only on the GPU, see wc_column_ops)
    int    quietTicks = 0;
    bool   asleep = false;
};

class WaterCoreGpu {
public:
    struct Buffer {
        VkBuffer       buf = VK_NULL_HANDLE;
        VkDeviceMemory mem = VK_NULL_HANDLE;
        VkDeviceSize   bytes = 0;
        void*          mapped = nullptr;
    };
    struct Volume {
        GridSpec spec;
        size_t cells = 0, nu = 0, nv = 0, nw = 0, latticeMax = 0, columns = 0, groups = 0;
        Buffer f, fOrig, fPrev, u, v, w, uOld, vOld, wOld, occ, src, liq, diag, rhs, p, res;
        Buffer valA, valB, knownA, knownB, first, keep, dropped, part, part2, out;
        std::array<std::unique_ptr<Vulkan::ComputePipeline>, 16> pipes;   // see Kernel
        int quietTicks = 0;
        bool asleep = false;
        bool quietBefore = false;
    };
    enum Kernel : int { FillAdvect = 0, VelAdvectU, VelAdvectV, VelAdvectW, FaceOpsU, FaceOpsV, FaceOpsW,
                        ColumnOps, Classify, Rbgs, ExtrapU, ExtrapV, ExtrapW, Reduce, KernelCount };

    WaterCoreGpu() = default;
    ~WaterCoreGpu();
    WaterCoreGpu(const WaterCoreGpu&) = delete;
    WaterCoreGpu& operator=(const WaterCoreGpu&) = delete;

    /// `shaderDir` holds the wc_*.comp.spv files. Returns false with `err` when the device or a
    /// pipeline cannot be created (the caller falls back to the CPU solver, loudly).
    bool init(VkDevice device, VkPhysicalDevice physical, VkQueue queue, uint32_t queueFamily,
              const std::string& shaderDir, std::string* err);
    void shutdown();
    bool ready() const { return m_device != VK_NULL_HANDLE && m_pool != VK_NULL_HANDLE; }
    /// Red-black SOR relaxation factor (1 = Gauss-Seidel). Measured, not assumed: see the parity rows.
    void setOmega(float w) { m_omega = w; }
    float omega() const { return m_omega; }

    /// Allocates the volume's buffers (refusing with the byte count when the allocation fails) and
    /// uploads f, u, v, w and the occupancy from the grid.
    Volume* createVolume(const WaterGrid& g, std::string* err);
    void destroyVolume(Volume* vol);
    void upload(Volume& vol, const WaterGrid& g);          ///< f, u, v, w, occ -> GPU
    void uploadSources(Volume& vol, const std::vector<float>& ratePerCell);   ///< m^3/s per cell (0 = none)
    void download(Volume& vol, WaterGrid& g) const;        ///< f, u, v, w -> grid (occ is the grid's own)

    /// Runs `ticks` ticks of `dt`, each with the CFL substep count the CPU would pick (max speed is
    /// reduced on the GPU before each tick), `sweeps` red-black Gauss-Seidel sweeps per projection.
    /// Returns the last tick's reductions; mass and the residual are read from the fenced copy.
    GpuStepStats step(Volume& vol, const SolverParams& params, float dt, int ticks, int sweeps);

private:
    bool createBuffer(Buffer& b, VkDeviceSize bytes, std::string* err);
    void destroyBuffer(Buffer& b);
    bool createPipelines(Volume& vol, std::string* err);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) const;
    void barrier(VkCommandBuffer cmd) const;
    void dispatch(VkCommandBuffer cmd, Vulkan::ComputePipeline& pipe, const WcPush& push, size_t threads, uint32_t local = 64) const;
    void copy(VkCommandBuffer cmd, const Buffer& from, const Buffer& to) const;
    void recordSubstep(VkCommandBuffer cmd, Volume& vol, WcPush push, const SolverParams& params, int sweeps, bool quietBefore);
    void recordExtrapolate(VkCommandBuffer cmd, Volume& vol, WcPush push, bool particles);
    void recordReductions(VkCommandBuffer cmd, Volume& vol, WcPush push, bool speedOnly);
    void submitAndWait(VkCommandBuffer cmd);

    VkDevice         m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical = VK_NULL_HANDLE;
    VkQueue          m_queue = VK_NULL_HANDLE;
    uint32_t         m_queueFamily = 0;
    VkCommandPool    m_pool = VK_NULL_HANDLE;
    VkFence          m_fence = VK_NULL_HANDLE;
    std::string      m_shaderDir;
    float            m_omega = 1.85f;   // measured: residual 0.14 at 40 sweeps on the 10 m column (1.6 at 1.7, 8.2 at 1.0); see GpuRbgsConvergenceScan
    std::vector<Volume*> m_volumes;
};

}}} // namespace Phyxel::Core::Water
