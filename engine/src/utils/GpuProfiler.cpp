#include "utils/GpuProfiler.h"
#include "utils/Logger.h"
#include <algorithm>
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

    // Timestamps only carry timestampValidBits meaningful bits; a delta must be masked to them or a
    // wrap reads as a huge duration.
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device->getPhysicalDevice(), &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device->getPhysicalDevice(), &familyCount, families.data());
    const uint32_t gfx = device->getGraphicsQueueFamily();
    if (gfx < familyCount) timestampValidBits = families[gfx].timestampValidBits;
    if (timestampValidBits == 0)
        LOG_WARN("GpuProfiler", "Graphics queue reports timestampValidBits = 0; GPU timings are meaningless");

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
    // Apply a requested stats toggle HERE, at the frame boundary, so `active` cannot change
    // between a begin/end pair - that leaves an unterminated query and loses the device.
    pipelineStatsActive = pipelineStatsRequested;
    currentFrame = frameIndex;
    auto& frame = frames[currentFrame];
    
    // Fetch results from the PREVIOUS usage of this frame index (which is now finished)
    // But wait, if we are just starting frame N, frame N from previous cycle might still be in flight?
    // No, standard double buffering means we wait for fence N before starting frame N again.
    // So it is safe to read results now.
    
    if (frame.queryCount > 0 && frame.serial != 0) {
        // (value, availability) pairs. Without the availability word a NOT_READY readback left the
        // previous frame's results in place and a poller counted that frame again; now a frame is
        // used only when every query it wrote is available, and the history accepts each serial once.
        std::vector<uint64_t> data(size_t(frame.queryCount) * 2u, 0);
        const VkResult result = vkGetQueryPoolResults(
            device->getDevice(), queryPools[currentFrame], 0, frame.queryCount,
            data.size() * sizeof(uint64_t), data.data(), 2 * sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);

        const uint64_t mask = timestampValidBits >= 64 ? ~uint64_t(0)
                                                        : ((uint64_t(1) << timestampValidBits) - 1);
        auto available = [&](uint32_t i) { return i < frame.queryCount && data[size_t(i) * 2 + 1] != 0; };
        auto msBetween = [&](uint32_t a, uint32_t b) {
            const uint64_t ticks = (data[size_t(b) * 2] - data[size_t(a) * 2]) & mask;
            return double(ticks) * double(timestampPeriod) / 1000000.0;
        };

        bool allAvailable = (result == VK_SUCCESS || result == VK_NOT_READY);
        for (const auto& scope : frame.completedScopes)
            allAvailable = allAvailable && available(scope.startIndex) && available(scope.endIndex);
        const bool haveFrame = frame.frameStartIndex != UINT32_MAX && frame.frameEndIndex != UINT32_MAX;
        if (haveFrame)
            allAvailable = allAvailable && available(frame.frameStartIndex) && available(frame.frameEndIndex);

        if (!allAvailable) {
            ++notReadyFrames;   // keep the previous results; nothing enters the history
        } else {
            // completedScopes is in END order (children first). Key and report in START order.
            std::vector<ScopeData> ordered = frame.completedScopes;
            std::sort(ordered.begin(), ordered.end(),
                      [](const ScopeData& a, const ScopeData& b) { return a.order < b.order; });
            std::vector<std::string> paths;
            paths.reserve(ordered.size());
            for (const auto& s : ordered) paths.push_back(s.path);
            const auto keys = GpuTimingHistory::occurrenceKeys(paths);

            lastFrameResults.clear();
            std::vector<GpuTimingSample> samples;
            samples.reserve(ordered.size() + 1);
            if (haveFrame) {
                const double frameMs = msBetween(frame.frameStartIndex, frame.frameEndIndex);
                samples.push_back({kFrameScopeName, kFrameScopeName, 0, frameMs});
                lastGpuFrameMs.store(frameMs, std::memory_order_relaxed);
            }
            for (size_t i = 0; i < ordered.size(); ++i) {
                const double ms = msBetween(ordered[i].startIndex, ordered[i].endIndex);
                // gpu_scopes keeps its existing shape: name, ms, depth, in the order scopes ENDED.
                samples.push_back({keys[i], ordered[i].name, ordered[i].depth, ms});
            }
            for (const auto& scope : frame.completedScopes)
                lastFrameResults.push_back({scope.name, msBetween(scope.startIndex, scope.endIndex), scope.depth});
            history.addFrame(frame.serial, samples);
            lastResultSerial = frame.serial;
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
    frame.scopesStarted = 0;
    frame.frameEndIndex = UINT32_MAX;
    frame.serial = ++frameSerial;

    vkCmdResetQueryPool(cmd, queryPools[currentFrame], 0, MAX_QUERIES_PER_FRAME);
    if (pipelineStatsEnabled) {
        for (uint32_t slot = 0; slot < NUM_STATS_SLOTS; ++slot) {
            vkCmdResetQueryPool(cmd, statsPools[currentFrame * NUM_STATS_SLOTS + slot], 0, 1);
        }
    }

    // Whole-frame bracket: this is the first timestamp in the command buffer after the resets, and
    // finishFrame writes the last one. Their difference is the real GPU frame time.
    frame.frameStartIndex = frame.queryCount++;
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queryPools[currentFrame], frame.frameStartIndex);
}

void GpuProfiler::finishFrame(VkCommandBuffer cmd) {
    auto& frame = frames[currentFrame];
    if (frame.frameStartIndex == UINT32_MAX || frame.queryCount >= MAX_QUERIES_PER_FRAME) return;
    frame.frameEndIndex = frame.queryCount++;
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPools[currentFrame], frame.frameEndIndex);
}

void GpuProfiler::endFrame() {
    // Nothing to do here, results are read at start of next cycle
}

void GpuProfiler::beginPipelineStats(VkCommandBuffer cmd, uint32_t slot) {
    if (!pipelineStatsEnabled || !pipelineStatsActive || slot >= NUM_STATS_SLOTS) return;
    vkCmdBeginQuery(cmd, statsPools[currentFrame * NUM_STATS_SLOTS + slot], 0, 0);
}

void GpuProfiler::endPipelineStats(VkCommandBuffer cmd, uint32_t slot) {
    if (!pipelineStatsEnabled || !pipelineStatsActive || slot >= NUM_STATS_SLOTS) return;
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
    scope.path = frame.activeScopes.empty() ? name : frame.activeScopes.back().path + "/" + name;
    scope.startIndex = startIndex;
    scope.depth = static_cast<uint32_t>(frame.activeScopes.size());
    scope.order = frame.scopesStarted++;

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
