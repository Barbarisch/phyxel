#pragma once

#include "vulkan/VulkanDevice.h"
#include <vulkan/vulkan.h>
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
    void setPipelineStatsActive(bool on) { pipelineStatsActive = on; }
    bool getPipelineStatsActive() const { return pipelineStatsActive; }

    const std::vector<GpuScopeResult>& getResults() const { return lastFrameResults; }

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
    std::vector<VkQueryPool> statsPools;
    std::vector<bool> statsPending;   // per (frame,slot): a query was recorded, read it back next cycle
    GpuPipelineStats lastPipelineStats[2];
    static const uint32_t NUM_PIPELINE_STATS = 4;  // input prims, VS inv, clip inv, frag inv

    struct ScopeData {
        std::string name;
        uint32_t startIndex;
        uint32_t endIndex;
        uint32_t depth;
    };

    struct FrameData {
        std::vector<ScopeData> completedScopes;
        std::vector<ScopeData> activeScopes; // Stack
        uint32_t queryCount = 0;
        bool queryReset = false;
    };

    std::vector<FrameData> frames;
    std::vector<GpuScopeResult> lastFrameResults;
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

#define GPU_PROFILE_SCOPE(profiler, cmd, name) ScopedGpuTimer _gpu_timer_##__LINE__(profiler, cmd, name)

} // namespace Phyxel
