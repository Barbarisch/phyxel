#pragma once

#include "vulkan/VulkanDevice.h"
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <memory>

namespace VulkanCube {

struct GpuScopeResult {
    std::string name;
    double durationMs;
    uint32_t depth;
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

    const std::vector<GpuScopeResult>& getResults() const { return lastFrameResults; }

private:
    Vulkan::VulkanDevice* device = nullptr;
    float timestampPeriod = 1.0f;
    uint32_t maxFrames = 2;
    uint32_t currentFrame = 0;

    static const uint32_t MAX_QUERIES_PER_FRAME = 128; 

    std::vector<VkQueryPool> queryPools;
    
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

} // namespace VulkanCube
