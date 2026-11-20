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
    /// F5.5c: the incremental edit after a bake, instead of load(): the changed world cells (index, material) and the
    /// survivors (`removed` = the baked bodies' indices before removal, ascending; `survivors` = the bodies after).
    /// Only those cells and the survivors' records are written; params, state, warm start and the tick counter are
    /// set exactly as a fresh load() of the edited world with the survivors (bit-equal after it).
    bool edit(const std::vector<std::pair<uint32_t, uint8_t>>& cells, const std::vector<size_t>& removed,
              const std::vector<SimBody>& survivors, std::string* err = nullptr);
    double lastEditMs() const { return m_lastEditMs; }
    double lastEditPrepMs() const { return m_lastEditPrepMs; }   // F5.5c: of which the CPU side (before the submit)
    bool editPending() const { return m_editPending; }   // F5.5c: staged, recorded by the next step / stepBegin / read
    bool load(const VoxelGrid& world, const std::vector<FractureMaterial>& mats, const std::vector<SimBody>& bodies,
              const BodySimParams& p, std::string* err);
    /// Runs `ticks` ticks (one submit per `ticksPerSubmit`).
    bool step(int ticks, std::string* err, int ticksPerSubmit = 30);
    /// Copies pose, velocities and sleep state (asleep, slowTicks, asleepTicks) back into `bodies` (same order as load()).
    bool read(std::vector<SimBody>& bodies, std::string* err);

    /// F5.5b, the live path: `ticks` ticks plus the read-back of the bodies and the counters, recorded in ONE submit that
    /// is not waited on. Bit-equal to step(ticks, err, ticks) + read() (the same recording). One step in flight at most.
    bool stepBegin(int ticks, std::string* err);
    /// 1: the step in flight finished -- stats()/lastGpuMs() updated, `bodies` (if not null; the loaded count) receive
    /// pose, velocities and sleep state; 0: still running (wait = false); -1: nothing in flight or an error.
    int stepPoll(std::vector<SimBody>* bodies, bool wait, std::string* err);
    bool stepPending() const { return m_pendingCmd != VK_NULL_HANDLE; }
    int pendingTicks() const { return m_pendingTicks; }
    uint32_t bodyCount() const { return m_nb; }

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
    /// F5.5a3: the body-world test (K_WORLD, HARD_APPLY) one workgroup per awake body; false = one thread per body
    /// (the control). Default on.
    void setWorldParallel(bool on) { m_worldPar = on; }
    /// F5.5a4: each body's primal on a 32-thread group (a fixed shared-memory tree), from per-colour lists: indirect per
    /// colour when many bodies are awake, one workgroup for every colour when few are (the fused choice above; the two
    /// forms are bit-equal). Deterministic but NOT bit-equal to the serial primal (the sum is reassociated).
    /// false = the serial primal (the control). Default on.
    void setPrimalParallel(bool on) { m_primalPar = on; }
    bool fusedThisStep() const { return m_fused; }
    /// F5.5a: a per-pass device-time profile (timestamps between the pass groups; forces one tick per submit, so it
    /// measures each tick on its own). Reset by setProfile(true).
    static constexpr int kProfGroups = 13;
    static constexpr int kProfMaxStamps = 64;
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
    void recordCounters(VkCommandBuffer cmd);           // the summed counters -> m_counters (host visible)
    void recordBodiesToStage(VkCommandBuffer cmd);      // the body records -> m_stage (host visible)
    void unpackCounters();
    void unpackBodies(std::vector<SimBody>& bodies) const;
    void finishPending();                               // waits for a step in flight and drops its read-back
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
    std::unique_ptr<Vulkan::ComputePipeline> m_pipePrimal;  // F5.5a4
    bool m_fused = true;              // the path of the current step() call
    uint32_t m_fusedMaxAwake = 128;
    uint32_t m_lastAwake = 0;
    bool m_narrowPar = true;
    bool m_worldPar = true;
    bool m_primalPar = true;
    bool m_profile = false;
    VkCommandBuffer m_pendingCmd = VK_NULL_HANDLE;    // F5.5b: the step in flight (stepBegin)
    int m_pendingTicks = 0;
    bool m_pendingTimed = false;
    double m_lastEditMs = -1.0;                    // F5.5c: the last edit()'s wall time
    double m_lastEditPrepMs = -1.0;
    std::vector<uint32_t> m_voxOff, m_corOff;      // F5.5c: each current body's voxel / corner offsets (edit keeps them)
    std::vector<double> m_radius;                  // F5.5c: each current body's bounding radius (from its corners)
    std::vector<uint32_t> m_prm;                   // F5.5c: the params words as last uploaded
    // F5.5c run 2: edit() stages its data in the stage buffer past the read-back region and does not submit; the next
    // command buffer (step / stepBegin / read) records these copies first (same queue, a barrier after): the frame
    // never waits on it (live, a waited submit cost ~14 ms on a GPU busy with rendering)
    bool m_editPending = false;
    std::vector<VkBufferCopy> m_editParams, m_editBodies, m_editWorld;
    VkDeviceSize m_editOff = 0;                    // where edit data starts in the stage (past nb_loaded x GBody)
    void recordEdit(VkCommandBuffer cmd);
    Profile m_prof;
    VkQueryPool m_profQueries = VK_NULL_HANDLE;
    uint32_t m_profStamp = 0;
    std::vector<int> m_profLabels;   // the group each timestamp closes (-1: the first)

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
