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
class Camera;
class CameraManager;
}
namespace Core {
class PerfCapture;
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

// POST /api/debug/gi_probe {skip_buried: bool, two_level_trace: bool} (GI-1, GI-2). Each field is
// optional (omitted = unchanged); a non-boolean refuses the whole request and nothing is applied.
// Echoes both options and gi_enabled (with the probe field off there is no pass to act on). Both
// default ON.
nlohmann::json setGiProbeOptions(Graphics::RenderCoordinator* rc, const nlohmann::json& params);

// GET /api/debug/cpu_timing?frames=N (I7). CPU scopes of drawFrame, same statistics as gpu_timing:
// drawFrame > LOD Update, Light Occupancy, Dirty Chunk Flush (includes meshing), Fence Wait, Acquire,
// Frame Setup (> Light Select+Upload), Record (> Shadow Pass, Scene Pass > Static Geometry > Occlusion
// BFS), Submit, Present.
nlohmann::json cpuTiming(Graphics::RenderCoordinator* rc, const nlohmann::json& params);

// GET /api/debug/mesh_timing?reset=0|1 (I7). rebuildAllFaces split by phase, plus whole-rebuild time
// bucketed by the chunk's microcube count. reset=1 returns the stats and then zeroes them, so a
// caller can measure exactly one window.
nlohmann::json meshTiming(const nlohmann::json& params);

// ---- City benchmark tooling (docs/PerfProgram2026-09.md section 16) ----

// GET /api/debug/frame_pacing?frames=N (I11). The main-loop scopes of PerformanceProfiler (Frame >
// API Drain, Update > Water/Scripting/AI/NPCs/Entities/Camera Sync/..., Streaming Pump, render) plus
// "Frame Interval" (wall time between consecutive frame ends), with the cpu_timing statistics
// (median/p90/p99/max/mean/last per scope) AND the raw per-frame series (`series`: frames_used entries
// of {serial, values{key: ms}}). frames clamped to [1, 240], omitted = all held; frames_used echoes it.
nlohmann::json framePacing(const PerfCapture* pc, const nlohmann::json& params);

// POST /api/debug/record (I15): {start:true, max_frames:N} begins a route recording (N clamped to
// [1, 65536], echoed as capacity; refused while already recording); {stop:true} ends it. Echoes
// {recording, frames, capacity, truncated}.
nlohmann::json recordControl(PerfCapture* pc, const nlohmann::json& params);

// GET /api/debug/record?from=K&count=M (I15): the recording, one row per frame from row K (default 0),
// at most M rows (default and max 2048 per response, so a 6,000-frame route is read in pages).
// `phase_keys` / `gpu_keys` name the columns of each row's `phases` / `gpu` arrays; null = not run
// this frame or not resolved. Rows whose GPU timings never resolved have `gpu_resolved: false`.
nlohmann::json recordDump(const PerfCapture* pc, const nlohmann::json& params);

// POST /api/camera/path (I12): {waypoints:[{x,y,z,yaw,pitch}] (world units, degrees), speed_u_per_s,
// loop (default false), stream_follow (default true)} starts a constant-speed path; {stop:true} stops
// it and releases the streaming focus. Refused, with nothing applied: < 2 waypoints, a non-finite
// value, speed <= 0 or > kMaxPathSpeedUnitsPerSec, stream_follow while another holder has the focus.
// GET (params empty) reports {playing, finished, progress, arc_length_u, speed_u_per_s, camera
// {x,y,z,yaw,pitch}, stream_follow, focus_holder}.
constexpr float kMaxPathSpeedUnitsPerSec = 64.0f;
nlohmann::json cameraPath(PerfCapture* pc, Graphics::CameraManager* cameras, ChunkManager* chunks,
                          const Graphics::Camera* camera, const nlohmann::json& params);

}  // namespace Phyxel::Core::PerfApi
