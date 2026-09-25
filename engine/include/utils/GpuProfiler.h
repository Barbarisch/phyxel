#pragma once

#include "vulkan/VulkanDevice.h"
#include "utils/GpuTimingHistory.h"
#include <vulkan/vulkan.h>
#include <atomic>
#include <string>
#include <vector>
#include <memory>

namespace Phyxel {

struct GpuScopeResult {
    std::string name;
    double durationMs;
    uint32_t depth;
};

// D0 overdraw counter (docs/RenderDensityPlan.md). Pipeline statistics for one wrapped pass
// (the Static Geometry pass). fragInvocations is the fill/overdraw cost; inputPrimitives ÷ face
// count exposes the 36-index geometry amplification (should be ~12 tris/face today).
struct GpuPipelineStats {
    bool     valid = false;
    uint64_t inputPrimitives = 0;   // triangles submitted (INPUT_ASSEMBLY_PRIMITIVES)
    uint64_t vsInvocations   = 0;   // vertex-shader invocations
    uint64_t clipInvocations = 0;   // primitives entering clipping
    uint64_t fragInvocations = 0;   // FRAGMENT_SHADER_INVOCATIONS — the fill cost
};

class GpuProfiler {
public:
    GpuProfiler();
    ~GpuProfiler();

    void init(Vulkan::VulkanDevice* device, uint32_t maxFramesInFlight = 2);
    void cleanup();

    void startFrame(uint32_t frameIndex, VkCommandBuffer cmd);
    void endFrame();

    void startScope(VkCommandBuffer cmd, const std::string& name);
    void endScope(VkCommandBuffer cmd);

    // D0/D1: wrap a pass to count fragment invocations + primitives. Slot 0 = Static Geometry,
    // slot 1 = Shadow pass. No-op if the pipelineStatisticsQuery feature is unavailable. Begin/end
    // must be inside a render pass.
    static const uint32_t STATS_SLOT_STATIC    = 0;
    static const uint32_t STATS_SLOT_SHADOW    = 1;
    // Slot 2 = instanced characters (player + NPCs). Added because character GPU cost was
    // previously unattributable: every part draws a full 36-vertex cube with no face culling,
    // so this pass is suspected to dominate in crowded scenes and needed its own counter
    // before optimizing it (docs/CharacterPipelineScaling.md F10/P0.1).
    static const uint32_t STATS_SLOT_CHARACTER = 2;
    static const uint32_t NUM_STATS_SLOTS      = 3;
    void beginPipelineStats(VkCommandBuffer cmd, uint32_t slot);
    void endPipelineStats(VkCommandBuffer cmd, uint32_t slot);
    const GpuPipelineStats& getPipelineStats(uint32_t slot) const { return lastPipelineStats[slot < NUM_STATS_SLOTS ? slot : 0]; }
    // The pipeline-statistics queries add GPU-sync overhead, so they are OFF by default and must be
    // switched on only for a counting session (never during a perf A/B). See docs/RenderDensityPlan.md.
    /// Toggling this takes effect at the NEXT frame boundary, never mid-frame. A flip
    /// between beginPipelineStats and endPipelineStats leaves an unterminated Vulkan
    /// query in the command buffer and the device is lost on submit - measured 2026-09-22
    /// by toggling it over the API while the town was rendering: the next command timed
    /// out and the process was gone.
    void setPipelineStatsActive(bool on) { pipelineStatsRequested = on; }
    bool getPipelineStatsActive() const { return pipelineStatsActive; }
    bool getPipelineStatsRequested() const { return pipelineStatsRequested; }

    const std::vector<GpuScopeResult>& getResults() const { return lastFrameResults; }

    // I1 (docs/PerfProgram2026-09.md): per-scope timing history over the last 240 frames, keyed by
    // scope path + occurrence. "GPU Frame" is the whole command buffer, bracketed by startFrame and
    // finishFrame. Only frames whose queries were all AVAILABLE are added, each once.
    void finishFrame(VkCommandBuffer cmd);
    const GpuTimingHistory& getHistory() const { return history; }
    uint32_t getTimestampValidBits() const { return timestampValidBits; }
    uint64_t getNotReadyFrames() const { return notReadyFrames; }
    uint64_t getLastResultSerial() const { return lastResultSerial; }
    // The most recent whole-frame GPU time, safe to read from ANY thread (engine_timing is served on
    // the HTTP thread, which must not touch the history). -1 until the first frame resolves.
    double getLastGpuFrameMsAtomic() const { return lastGpuFrameMs.load(std::memory_order_relaxed); }
    static constexpr const char* kFrameScopeName = "GPU Frame";

private:
    Vulkan::VulkanDevice* device = nullptr;
    float timestampPeriod = 1.0f;
    uint32_t maxFrames = 2;
    uint32_t currentFrame = 0;

    static const uint32_t MAX_QUERIES_PER_FRAME = 128; 

    std::vector<VkQueryPool> queryPools;

    // D0/D1 pipeline-statistics pools. Layout: statsPools[frame*NUM_STATS_SLOTS + slot], one
    // multi-counter query each.
    bool pipelineStatsEnabled = false;   // feature available + pools created
    bool pipelineStatsActive  = false;   // runtime gate (OFF by default — avoids sync overhead)
    bool pipelineStatsRequested = false;  // applied at the next startFrame (see setter)
    std::vector<VkQueryPool> statsPools;
    std::vector<bool> statsPending;   // per (frame,slot): a query was recorded, read it back next cycle
    GpuPipelineStats lastPipelineStats[NUM_STATS_SLOTS];
    // One stats record per slot. When this array was sized 2 while NUM_STATS_SLOTS was 3, the
    // CHARACTER slot's readback wrote 40 bytes past it into `frames` and `lastFrameResults`, and the
    // next timestamp readback handed the driver a garbage query count: G-155, the "NVIDIA driver
    // crash" (0xC0000005 in nvoglv64.dll), reproduced 2026-09-24 on the 4090 as soon as stats were
    // switched on with a character in view. Adding a slot must grow this array with it.
    static_assert(sizeof(lastPipelineStats) / sizeof(lastPipelineStats[0]) == NUM_STATS_SLOTS,
                  "lastPipelineStats must hold exactly one record per pipeline-statistics slot");
    static const uint32_t NUM_PIPELINE_STATS = 4;  // input prims, VS inv, clip inv, frag inv

    struct ScopeData {
        std::string name;
        std::string path;        // parent names joined with '/', ending in this scope's name
        uint32_t startIndex;
        uint32_t endIndex;
        uint32_t depth;
        uint32_t order = 0;      // position in START order, so results can be keyed in recording order
    };

    struct FrameData {
        std::vector<ScopeData> completedScopes;
        std::vector<ScopeData> activeScopes; // Stack
        uint32_t queryCount = 0;
        bool queryReset = false;
        uint64_t serial = 0;                 // frame serial when these queries were recorded (0 = none)
        uint32_t frameStartIndex = UINT32_MAX;
        uint32_t frameEndIndex = UINT32_MAX;
        uint32_t scopesStarted = 0;
    };

    std::vector<FrameData> frames;
    std::vector<GpuScopeResult> lastFrameResults;

    GpuTimingHistory history{240};
    uint64_t frameSerial = 0;          // incremented once per startFrame
    uint64_t notReadyFrames = 0;       // readbacks where some query was not yet available
    uint64_t lastResultSerial = 0;     // serial of the frame lastFrameResults came from
    uint32_t timestampValidBits = 64;
    std::atomic<double> lastGpuFrameMs{-1.0};
};

class ScopedGpuTimer {
public:
    ScopedGpuTimer(GpuProfiler* profiler, VkCommandBuffer cmd, const std::string& name)
        : profiler(profiler), cmd(cmd) {
        if (profiler) profiler->startScope(cmd, name);
    }
    ~ScopedGpuTimer() {
        if (profiler) profiler->endScope(cmd);
    }
private:
    GpuProfiler* profiler;
    VkCommandBuffer cmd;
};

// Two-step concat so __LINE__ expands: a single ## pasted the literal "_gpu_timer___LINE__", so every
// use declared the same variable and two scopes in one block did not compile. The type is fully
// qualified so the macro also works outside namespace Phyxel.
#define PHX_GPU_CONCAT_INNER(a, b) a##b
#define PHX_GPU_CONCAT(a, b) PHX_GPU_CONCAT_INNER(a, b)
#define GPU_PROFILE_SCOPE(profiler, cmd, name) \
    ::Phyxel::ScopedGpuTimer PHX_GPU_CONCAT(_gpu_timer_, __LINE__)(profiler, cmd, name)

} // namespace Phyxel
