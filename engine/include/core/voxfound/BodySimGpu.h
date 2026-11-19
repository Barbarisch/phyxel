#pragma once
// docs/VoxelFoundationPlan.md §6.7, F5.2b -- the body solver on the GPU (shaders/voxfound_bodies.comp).
//
// The same tick as BodySimRef (BodySim.h), in float, headless (raw Vulkan handles, as FractureGpu): integrate ->
// world contacts -> body-body contacts -> the same per-pair cap -> per-body constraint lists sorted by key ->
// Jones-Plassmann colours -> iterations of {primal per colour, dual} -> hard contact (body-body, then the world) ->
// velocities, sleep, wake -> warm start saved in a hashed table. Compared with the reference by the same invariants
// (not bit-equality; see docs/evidence/voxfound_f52_prediction.txt, F5.2b).

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "core/voxfound/BodySim.h"

namespace Phyxel { namespace Vulkan { class ComputePipeline; } }

namespace Phyxel::Core::VoxFound {

struct BodySimGpuStats {
    uint32_t contacts = 0;                    // constraints in the last tick
    uint32_t maxContacts = 0;                 // over the ticks of the last step() call
    uint64_t candidates = 0, capped = 0, deepPoints = 0, pairsTested = 0, hardFires = 0;   // summed over the call
    uint32_t scratchOverflow = 0;             // candidates that did not fit a worker's scratch (summed)
    uint32_t constraintOverflow = 0;          // constraints past the buffer (summed)
    uint32_t uncoloured = 0;                  // dynamic bodies left without a colour (summed)
    uint32_t pairOverflow = 0;                // F5.5a2: body pairs past the pair list (summed)
    uint32_t colours = 0;                     // the most colours any tick used
    uint32_t awake = 0;                       // after the last tick
};

class BodySimGpu {
public:
    static constexpr uint32_t kMaxColours = 32;
    static constexpr uint32_t kScratch = 4096;      // candidates per worker
    static constexpr uint32_t kWarmCap = 1u << 16;  // warm-start table entries per half

    BodySimGpu();
    ~BodySimGpu();
    BodySimGpu(const BodySimGpu&) = delete;
    BodySimGpu& operator=(const BodySimGpu&) = delete;

    bool init(VkDevice device, VkPhysicalDevice physical, VkQueue queue, uint32_t queueFamily,
              const std::string& shaderDir, std::string* err);
    void shutdown();
    bool initialized() const { return m_device != VK_NULL_HANDLE; }

    /// The world, the materials and the bodies as BodySimRef::add built them (grids, corners, mass properties, pose,
    /// velocities, sleep state).
    bool load(const VoxelGrid& world, const std::vector<FractureMaterial>& mats, const std::vector<SimBody>& bodies,
              const BodySimParams& p, std::string* err);
    /// Runs `ticks` ticks (one submit per `ticksPerSubmit`).
    bool step(int ticks, std::string* err, int ticksPerSubmit = 30);
    /// Copies pose, velocities and sleep state back into `bodies` (same order as load()).
    bool read(std::vector<SimBody>& bodies, std::string* err);

    /// F5.5a: the colour rounds in ONE dispatch and each iteration's primal over every colour in ONE dispatch
    /// (voxfound_bodies_fused.comp, one workgroup), 17 solve dispatches per tick instead of ~290 (the per-colour path,
    /// kept as the control; bit-equal). One workgroup wins while few bodies are awake and loses when many are (run 2:
    /// 1.2 vs 1.8 ms mean at ~100 bodies, 7.3 vs 3.4 at ~1,000), so the path is chosen per step() call from the awake
    /// count the previous call ended with: fused while it is <= fusedMaxAwake (default 128). The choice changes cost
    /// only: the two paths are bit-equal. setFused(true / false) forces one path.
    void setFused(bool on) { m_fusedMaxAwake = on ? UINT32_MAX : 0u; }
    void setFusedMaxAwake(uint32_t n) { m_fusedMaxAwake = n; }
    /// F5.5a2: the body-body narrowphase (pairs + hard contact) one workgroup per pair; false = the serial per-body
    /// passes (F5.2b, the control). Default on.
    void setNarrowParallel(bool on) { m_narrowPar = on; }
    bool fusedThisStep() const { return m_fused; }
    /// F5.5a: a per-pass device-time profile (timestamps between the pass groups; forces one tick per submit, so it
    /// measures each tick on its own). Reset by setProfile(true).
    static constexpr int kProfGroups = 11;
    static const char* profileName(int g);
    struct Profile {
        double sumMs[kProfGroups] = {};
        double minMs[kProfGroups] = {};
        int ticks = 0;
    };
    void setProfile(bool on) {
        m_profile = on;
        if (on) m_prof = Profile{};
    }
    const Profile& profile() const { return m_prof; }
    const BodySimGpuStats& stats() const { return m_stats; }
    double lastGpuMs() const { return m_lastGpuMs; }   // device time of the last step() call (< 0 unavailable)
    bool implemented() const;

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
    bool submit(const std::function<void(VkCommandBuffer)>& record, bool timed, double* ms, std::string* err);
    void recordTick(VkCommandBuffer cmd);

    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physical = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    VkCommandPool m_pool = VK_NULL_HANDLE;
    VkFence m_fence = VK_NULL_HANDLE;
    VkQueryPool m_queries = VK_NULL_HANDLE;
    double m_timestampMs = 0.0;
    double m_lastGpuMs = -1.0;
    std::unique_ptr<Vulkan::ComputePipeline> m_pipe;
    std::unique_ptr<Vulkan::ComputePipeline> m_pipeFused;   // F5.5a
    bool m_fused = true;              // the path of the current step() call
    uint32_t m_fusedMaxAwake = 128;
    uint32_t m_lastAwake = 0;
    bool m_narrowPar = true;
    bool m_profile = false;
    Profile m_prof;
    VkQueryPool m_profQueries = VK_NULL_HANDLE;
    uint32_t m_profStamp = 0;

    // device buffers (see shaders/voxfound_bodies.comp for the layouts)
    Buffer m_params, m_bodies, m_bodyVox, m_corners, m_world, m_cons, m_scratch, m_state, m_lists, m_warm, m_push;
    Buffer m_aux, m_hres;                             // F5.5a2: awake list, pair lists, hard-pair hash + results
    uint32_t m_maxPairs = 0;
    Buffer m_stage;                                   // host-visible: uploads and read-backs
    Buffer m_counters;                                // host-visible: per-tick counters
    uint32_t m_nb = 0, m_maxCons = 0, m_parity = 0, m_iterations = 8, m_tick = 0;
    BodySimGpuStats m_stats;
};

}  // namespace Phyxel::Core::VoxFound
