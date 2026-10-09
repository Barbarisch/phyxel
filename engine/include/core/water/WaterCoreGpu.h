// WaterCoreGpu — the WaterCore fill solver as Vulkan compute (docs/WaterCore.md §15.11, Phase C).
//
// The CPU reference (WaterSolver, WaterCore.h) stays the oracle; this class runs the SAME rules as
// kernels (shaders/wc_*.comp) on a grid uploaded from a WaterGrid and is accepted on parity rows,
// never on looks. Slice 3: device-local buffers behind one host-visible staging buffer, ONE fenced
// submission per step() call (all ticks, all substeps), reductions (rest, residual, residue) folded
// on the device, and the grid downloaded only when the caller asks (download()). Raw Vulkan handles
// so the same class runs under the integration test fixture and under the engine's VulkanDevice.
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

/// A pump or sink on the device (shaders/wc_sources.comp GpuSource, 32 bytes).
struct GpuSource {
    int32_t cell = 0;          ///< grid-local cell index
    float rate = 0.0f;         ///< m^3/s, negative = sink
    float pending = 0.0f;      ///< m^3 owed
    float placedTotal = 0.0f;  ///< m^3 placed (negative = removed)
    float unplaced = 0.0f;
    float pad0 = 0.0f, pad1 = 0.0f, pad2 = 0.0f;
};
static_assert(sizeof(GpuSource) == 32, "GpuSource must match the GLSL struct");

struct GpuStepStats {
    int    ticks = 0;
    int    substepsLast = 0;         ///< the substep count used for every tick of the call (CFL from the last known max speed, see step())
    int    sweeps = 0;
    float  rbgsResidualMax = 0.0f;   ///< max |A p - b| over liquid cells after the last substep's sweeps
    double kineticEnergy = 0.0;
    double maxDeltaF = 0.0;          ///< over the LAST tick of the call
    double totalMass = 0.0;          ///< m^3 (from the fill reduction)
    double maxSpeed = 0.0;
    double residueDropped = 0.0;     ///< m^3 this call (sweep; downward merge only on the GPU, see wc_column_ops)
    int    quietTicks = 0;
    bool   asleep = false;
    double gpuMs = 0.0;              ///< wall time of the fenced submission (GPU time + submit/fence overhead)
};

class WaterCoreGpu {
public:
    struct Buffer {
        VkBuffer       buf = VK_NULL_HANDLE;
        VkDeviceMemory mem = VK_NULL_HANDLE;
        VkDeviceSize   bytes = 0;
        void*          mapped = nullptr;   // staging only
    };
    struct Volume {
        GridSpec spec;
        size_t cells = 0, nu = 0, nv = 0, nw = 0, latticeMax = 0, columns = 0, groups = 0;
        Buffer f, fOrig, fPrev, u, v, w, uOld, vOld, wOld, occ, src, liq, diag, rhs, p, res;
        Buffer valA, valB, knownA, knownB, first, keep, dropped, part, part2, out;
        Buffer origLat;                  ///< extrapolation scratch: the lattice before the halo pass (NOT uOld: that is the FLIP base)
        // Phase B2 on the GPU: the particle transport (FlipTransport mirrored on the device)
        bool particles = false;          ///< this volume's mass is carried by particles
        size_t particleCount = 0, particleCapacity = 0;
        bool haveOldGrid = false;        ///< a p2g base exists (else the first g2p is PIC)
        Buffer posA, velA, posB, velB, pcount, pstart, pcursor;
        VkDeviceSize offParticles = 0;   ///< staging region for pos+vel (capacity x 32 B)
        Buffer sources;                  ///< GpuSource array (device-local; kMaxSources)
        int sourceCount = 0;
        Buffer staging;                  ///< host-visible: [f | u | v | w | occ | src | out | p | sources]
        VkDeviceSize offU = 0, offV = 0, offW = 0, offOcc = 0, offSrc = 0, offOut = 0, offP = 0, offSources = 0;
        std::array<std::unique_ptr<Vulkan::ComputePipeline>, 32> pipes;   // >= KernelCount (static_assert below): 16 overflowed into quietTicks/asleep when the FLIP kernels arrived, 2026-10-08
        int quietTicks = 0;
        bool asleep = false;
        bool quietBefore = false;
        double lastMaxSpeed = 0.0;       ///< from the last call's reduction (or the upload), drives the CFL count
    };
    enum Kernel : int { FillAdvect = 0, VelAdvectU, VelAdvectV, VelAdvectW, FaceOpsU, FaceOpsV, FaceOpsW,
                        ColumnOps, Classify, Rbgs, ExtrapU, ExtrapV, ExtrapW, Reduce, Sources, FlipSort, FlipP2g, FlipG2p, KernelCount };
    static constexpr int kMaxSources = 64;
    static_assert(KernelCount <= 32, "Volume::pipes must hold every kernel");

    WaterCoreGpu() = default;
    ~WaterCoreGpu();
    WaterCoreGpu(const WaterCoreGpu&) = delete;
    WaterCoreGpu& operator=(const WaterCoreGpu&) = delete;

    bool init(VkDevice device, VkPhysicalDevice physical, VkQueue queue, uint32_t queueFamily,
              const std::string& shaderDir, std::string* err);
    void shutdown();
    bool ready() const { return m_device != VK_NULL_HANDLE && m_pool != VK_NULL_HANDLE; }
    void setOmega(float w) { m_omega = w; }
    float omega() const { return m_omega; }

    Volume* createVolume(const WaterGrid& g, std::string* err);
    /// Allocation only (no upload): refuses with the byte count when the device cannot hold it.
    Volume* createVolume(const GridSpec& spec, std::string* err, bool particles = false, size_t particleCapacity = 0);
    void destroyVolume(Volume* vol);
    void upload(Volume& vol, const WaterGrid& g);                              ///< f, u, v, w, occ -> GPU (fenced)
    /// Replace the volume's pumps/sinks (fenced); the projection's per-cell rate buffer is reset.
    bool setSources(Volume& vol, const std::vector<GpuSource>& sources, std::string* err);
    void readSources(Volume& vol, std::vector<GpuSource>& out) const;         ///< placedTotal/pending after a step (fenced)
    /// Phase B2: replace the volume's particles (fenced) and rebuild the grid's f and faces from them.
    bool setParticles(Volume& vol, const std::vector<FlipParticle>& ps, std::string* err);
    void readParticles(Volume& vol, std::vector<FlipParticle>& out) const;     ///< the sorted list (fenced)
    void setFlipBlend(float b) { m_flipBlend = b; }
    void download(Volume& vol, WaterGrid& g) const;                            ///< f, u, v, w -> grid (fenced)
    void readPressure(Volume& vol, std::vector<float>& p) const;               ///< the last projection's pressure (fenced; tests)
    /// Diagnostics: a per-cell buffer by name ("f", "p", "liq", "rhs", "diag", "res", "occ") as floats (fenced).
    void readCells(Volume& vol, const char* which, std::vector<float>& out) const;

    /// Runs `ticks` ticks of `dt` in ONE submission. The CFL substep count is taken from the last
    /// known max speed plus the gravity this call can add (ticks x g x dt), so it is conservative
    /// rather than per-tick exact; rest bookkeeping advances by `ticks` when the final state is
    /// quiet (exact for realtime's one tick per call).
    GpuStepStats step(Volume& vol, const SolverParams& params, float dt, int ticks, int sweeps);

private:
    bool createBuffer(Buffer& b, VkDeviceSize bytes, bool hostVisible, std::string* err);
    void destroyBuffer(Buffer& b);
    bool createPipelines(Volume& vol, std::string* err);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) const;
    void barrier(VkCommandBuffer cmd) const;
    void dispatchNoBarrier(VkCommandBuffer cmd, Vulkan::ComputePipeline& pipe, const WcPush& push, size_t threads, uint32_t local = 64) const;
    void dispatch(VkCommandBuffer cmd, Vulkan::ComputePipeline& pipe, const WcPush& push, size_t threads, uint32_t local = 64) const;
    void copy(VkCommandBuffer cmd, const Buffer& from, const Buffer& to, VkDeviceSize bytes, VkDeviceSize srcOff = 0, VkDeviceSize dstOff = 0) const;
    void recordSubstep(VkCommandBuffer cmd, Volume& vol, WcPush push, const SolverParams& params, int sweeps, bool quietBefore);
    void recordExtrapolate(VkCommandBuffer cmd, Volume& vol, WcPush push, bool particles);
    void recordReductions(VkCommandBuffer cmd, Volume& vol, WcPush push);
    VkCommandBuffer beginCommands() const;
    void submitAndWait(VkCommandBuffer cmd) const;

    VkDevice         m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical = VK_NULL_HANDLE;
    VkQueue          m_queue = VK_NULL_HANDLE;
    uint32_t         m_queueFamily = 0;
    VkCommandPool    m_pool = VK_NULL_HANDLE;
    VkFence          m_fence = VK_NULL_HANDLE;
    std::string      m_shaderDir;
    float            m_flipBlend = 0.95f;
    void recordParticleTransport(VkCommandBuffer cmd, Volume& vol, WcPush push);
    float            m_omega = 1.85f;   // measured: residual 0.14 at 40 sweeps on the 10 m column (1.6 at 1.7, 8.2 at 1.0); see GpuRbgsConvergenceScan
    std::vector<Volume*> m_volumes;
};

}}} // namespace Phyxel::Core::Water
