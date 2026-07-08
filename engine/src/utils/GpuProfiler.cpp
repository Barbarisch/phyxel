#include "utils/GpuProfiler.h"
#include "utils/Logger.h"
#include <iostream>

namespace Phyxel {

GpuProfiler::GpuProfiler() {}

GpuProfiler::~GpuProfiler() {
    cleanup();
}

void GpuProfiler::init(Vulkan::VulkanDevice* device, uint32_t maxFramesInFlight) {
    this->device = device;
    this->maxFrames = maxFramesInFlight;

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device->getPhysicalDevice(), &props);
    timestampPeriod = props.limits.timestampPeriod;

    queryPools.resize(maxFrames);
    frames.resize(maxFrames);

    VkQueryPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    poolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    poolInfo.queryCount = MAX_QUERIES_PER_FRAME;

    for (uint32_t i = 0; i < maxFrames; i++) {
        if (vkCreateQueryPool(device->getDevice(), &poolInfo, nullptr, &queryPools[i]) != VK_SUCCESS) {
            LOG_ERROR("GpuProfiler", "Failed to create query pool!");
        }
    }

    // D0 pipeline-statistics pools (docs/RenderDensityPlan.md) — only if the feature is enabled.
    pipelineStatsEnabled = device->pipelineStatsSupported();
    if (pipelineStatsEnabled) {
        statsPools.resize(maxFrames * NUM_STATS_SLOTS);
        statsPending.assign(maxFrames * NUM_STATS_SLOTS, false);
        VkQueryPoolCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        si.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
        si.queryCount = 1;
        si.pipelineStatistics =
            VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT |
            VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT |
            VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT |
            VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT;
        for (uint32_t i = 0; i < statsPools.size(); i++) {
            if (vkCreateQueryPool(device->getDevice(), &si, nullptr, &statsPools[i]) != VK_SUCCESS) {
                LOG_ERROR("GpuProfiler", "Failed to create pipeline-statistics pool!");
                pipelineStatsEnabled = false;
            }
        }
    }
}

void GpuProfiler::cleanup() {
    if (device) {
        for (auto pool : queryPools) {
            if (pool != VK_NULL_HANDLE) {
                vkDestroyQueryPool(device->getDevice(), pool, nullptr);
            }
        }
        queryPools.clear();
        for (auto pool : statsPools) {
            if (pool != VK_NULL_HANDLE) {
                vkDestroyQueryPool(device->getDevice(), pool, nullptr);
            }
        }
        statsPools.clear();
    }
}

void GpuProfiler::startFrame(uint32_t frameIndex, VkCommandBuffer cmd) {
    currentFrame = frameIndex;
    auto& frame = frames[currentFrame];
    
    // Fetch results from the PREVIOUS usage of this frame index (which is now finished)
    // But wait, if we are just starting frame N, frame N from previous cycle might still be in flight?
    // No, standard double buffering means we wait for fence N before starting frame N again.
    // So it is safe to read results now.
    
    if (frame.queryCount > 0) {
        std::vector<uint64_t> timestamps(frame.queryCount);
        VkResult result = vkGetQueryPoolResults(
            device->getDevice(), 
            queryPools[currentFrame], 
            0, 
            frame.queryCount, 
            timestamps.size() * sizeof(uint64_t), 
            timestamps.data(), 
            sizeof(uint64_t), 
            VK_QUERY_RESULT_64_BIT
        );

        if (result == VK_SUCCESS) {
            lastFrameResults.clear();
            for (const auto& scope : frame.completedScopes) {
                if (scope.endIndex < timestamps.size()) {
                    uint64_t start = timestamps[scope.startIndex];
                    uint64_t end = timestamps[scope.endIndex];
                    double durationNs = (end - start) * timestampPeriod;
                    
                    lastFrameResults.push_back({
                        scope.name,
                        durationNs / 1000000.0, // Convert to ms
                        scope.depth
                    });
                }
            }
        }
    }

    // D0/D1: read back this frame-slot's pipeline-statistics from its previous (now-finished) use.
    if (pipelineStatsEnabled) {
        for (uint32_t slot = 0; slot < NUM_STATS_SLOTS; ++slot) {
            uint32_t idx = currentFrame * NUM_STATS_SLOTS + slot;
            if (!statsPending[idx]) continue;
            uint64_t s[NUM_PIPELINE_STATS] = {0};
            VkResult sr = vkGetQueryPoolResults(
                device->getDevice(), statsPools[idx], 0, 1,
                sizeof(s), s, sizeof(s), VK_QUERY_RESULT_64_BIT);
            if (sr == VK_SUCCESS) {
                lastPipelineStats[slot].valid = true;
                lastPipelineStats[slot].inputPrimitives = s[0];
                lastPipelineStats[slot].vsInvocations   = s[1];
                lastPipelineStats[slot].clipInvocations = s[2];
                lastPipelineStats[slot].fragInvocations = s[3];
            }
            statsPending[idx] = false;
        }
    }

    // Reset for new frame
    frame.completedScopes.clear();
    frame.activeScopes.clear();
    frame.queryCount = 0;

    vkCmdResetQueryPool(cmd, queryPools[currentFrame], 0, MAX_QUERIES_PER_FRAME);
    if (pipelineStatsEnabled) {
        for (uint32_t slot = 0; slot < NUM_STATS_SLOTS; ++slot) {
            vkCmdResetQueryPool(cmd, statsPools[currentFrame * NUM_STATS_SLOTS + slot], 0, 1);
        }
    }
}

void GpuProfiler::endFrame() {
    // Nothing to do here, results are read at start of next cycle
}

void GpuProfiler::beginPipelineStats(VkCommandBuffer cmd, uint32_t slot) {
    if (!pipelineStatsEnabled || slot >= NUM_STATS_SLOTS) return;
    vkCmdBeginQuery(cmd, statsPools[currentFrame * NUM_STATS_SLOTS + slot], 0, 0);
}

void GpuProfiler::endPipelineStats(VkCommandBuffer cmd, uint32_t slot) {
    if (!pipelineStatsEnabled || slot >= NUM_STATS_SLOTS) return;
    uint32_t idx = currentFrame * NUM_STATS_SLOTS + slot;
    vkCmdEndQuery(cmd, statsPools[idx], 0);
    statsPending[idx] = true;
}

void GpuProfiler::startScope(VkCommandBuffer cmd, const std::string& name) {
    auto& frame = frames[currentFrame];
    if (frame.queryCount + 2 > MAX_QUERIES_PER_FRAME) return;

    uint32_t startIndex = frame.queryCount++;
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queryPools[currentFrame], startIndex);

    ScopeData scope;
    scope.name = name;
    scope.startIndex = startIndex;
    scope.depth = static_cast<uint32_t>(frame.activeScopes.size());
    
    frame.activeScopes.push_back(scope);
}

void GpuProfiler::endScope(VkCommandBuffer cmd) {
    auto& frame = frames[currentFrame];
    if (frame.activeScopes.empty()) return;
    if (frame.queryCount >= MAX_QUERIES_PER_FRAME) return;

    ScopeData scope = frame.activeScopes.back();
    frame.activeScopes.pop_back();

    uint32_t endIndex = frame.queryCount++;
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPools[currentFrame], endIndex);

    scope.endIndex = endIndex;
    frame.completedScopes.push_back(scope);
}

} // namespace Phyxel
