#pragma once

// Shared JSON for the perf-instrumentation routes (docs/PerfProgram2026-09.md, §3.2), so the editor
// host (Application) and the standalone --test host (GameApiService) answer identically.

#include <vulkan/vulkan.h>

#include <nlohmann/json.hpp>

namespace Phyxel {
class GpuProfiler;
class ChunkManager;
namespace Graphics {
class RenderCoordinator;
}
}

namespace Phyxel::Core::PerfApi {

// GET /api/debug/gpu_timing?frames=N (I1). Per-scope median / p90 / p99 / mean / last over the last
// N accepted GPU frames, plus the whole-frame time. Echoes frames_used, stale/not-ready counters and
// timestamp_valid_bits so a caller can judge the numbers.
nlohmann::json gpuTiming(const GpuProfiler* prof, const nlohmann::json& params);

// The whole-frame GPU time over the last `frames` frames, as {n, median_ms, p90_ms, p99_ms, mean_ms,
// last_ms}, or null when there is no history yet. Feeds engine_timing.gpu_frame_ms.
nlohmann::json gpuFrameSummary(const GpuProfiler* prof, size_t frames);

// POST /api/debug/pipeline_stats. Accepts "enabled" (editor) or "on" (standalone); omitted means
// unchanged. Echoes the requested state and the state in effect this frame.
nlohmann::json setPipelineStats(GpuProfiler* prof, const nlohmann::json& params);

// "IMMEDIATE" / "MAILBOX" / "FIFO" / "FIFO_RELAXED" / "OTHER".
const char* presentModeName(VkPresentModeKHR mode);

// GET /api/debug/light_stats (I3). Read-only census: registered / enabled / uploaded / dropped,
// unique positions (duplicates), by_source, point-radius histogram, and the CPU cost of light
// selection + the emissive-voxel reconcile. `rc` supplies both the LightManager and the reconcile
// timing; null gives an error.
nlohmann::json lightStats(Graphics::RenderCoordinator* rc);

// GET /api/debug/voxel_tiers?per_chunk=0|1&covered=0|1 (I5). Per tier (cube / sub / micro / LOD cell):
// stored objects, instances after merging, unit faces before merging, instances submitted this view
// in the main pass and per shadow cascade, CPU and GPU bytes. covered=1 adds the (costlier) count of
// cube faces fully hidden behind opaque sub/micro detail. per_chunk=1 lists chunks (capped at 512).
nlohmann::json voxelTiers(Graphics::RenderCoordinator* rc, ChunkManager* cm, const nlohmann::json& params);

// POST /api/debug/tier_mask {main:[cube,sub,micro], shadow:[cube,sub,micro]} (I6). Each array is
// optional (omitted = unchanged) but must be exactly three booleans. Echoes both masks.
nlohmann::json setTierMask(const nlohmann::json& params);

// POST /api/debug/depth_prepass {enabled: bool} (P-DP). Omitted = unchanged; a non-boolean is refused
// and nothing is applied. Echoes {enabled, available (pipelines built), ran_last_frame}. Default OFF.
nlohmann::json setDepthPrepass(Graphics::RenderCoordinator* rc, const nlohmann::json& params);

// GET /api/debug/cpu_timing?frames=N (I7). CPU scopes of drawFrame, same statistics as gpu_timing:
// drawFrame > LOD Update, Light Occupancy, Dirty Chunk Flush (includes meshing), Fence Wait, Acquire,
// Frame Setup (> Light Select+Upload), Record (> Shadow Pass, Scene Pass > Static Geometry > Occlusion
// BFS), Submit, Present.
nlohmann::json cpuTiming(Graphics::RenderCoordinator* rc, const nlohmann::json& params);

// GET /api/debug/mesh_timing?reset=0|1 (I7). rebuildAllFaces split by phase, plus whole-rebuild time
// bucketed by the chunk's microcube count. reset=1 returns the stats and then zeroes them, so a
// caller can measure exactly one window.
nlohmann::json meshTiming(const nlohmann::json& params);

}  // namespace Phyxel::Core::PerfApi
