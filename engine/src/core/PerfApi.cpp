#include "core/PerfApi.h"
#include <cmath>
#include "graphics/CameraManager.h"
#include "graphics/Camera.h"
#include "core/PerfCapture.h"

#include <algorithm>

#include "core/Chunk.h"
#include "core/ChunkManager.h"
#include "graphics/GiProbeField.h"
#include "graphics/LightManager.h"
#include "graphics/RenderCoordinator.h"
#include "vulkan/RenderPipeline.h"
#include "utils/GpuProfiler.h"

namespace Phyxel::Core::PerfApi {

namespace {

// Query-string flags arrive as strings ("1"/"true"), JSON bodies as booleans or numbers.
bool paramFlag(const nlohmann::json& params, const char* key) {
    if (!params.contains(key)) return false;
    const auto& v = params[key];
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_number()) return v.get<double>() != 0.0;
    if (v.is_string()) return v.get<std::string>() == "1" || v.get<std::string>() == "true";
    return false;
}

nlohmann::json statsJson(const GpuTimingStats& s) {
    return nlohmann::json{{"n", s.n},
                          {"median_ms", s.median},
                          {"p90_ms", s.p90},
                          {"p99_ms", s.p99},
                          {"mean_ms", s.mean},
                          {"max_ms", s.max},
                          {"last_ms", s.last}};
}

// Reads frames=N from a query-string-derived number or a JSON body. Omitted means "all held".
size_t requestedFrames(const nlohmann::json& params, size_t capacity) {
    if (!params.contains("frames")) return capacity;
    const auto& v = params["frames"];
    long long n = 0;
    if (v.is_number_integer()) n = v.get<long long>();
    else if (v.is_number()) n = static_cast<long long>(v.get<double>());
    else if (v.is_string()) {
        try { n = std::stoll(v.get<std::string>()); } catch (...) { n = static_cast<long long>(capacity); }
    }
    // Clamp to [1, capacity]. The ring never holds more than `capacity` frames, and a window larger
    // than what is held would silently report fewer samples than the caller asked for while looking
    // like a full window; frames_used echoes what was actually covered.
    if (n < 1) n = 1;
    if (n > static_cast<long long>(capacity)) n = static_cast<long long>(capacity);
    return static_cast<size_t>(n);
}

}  // namespace

nlohmann::json gpuTiming(const GpuProfiler* prof, const nlohmann::json& params) {
    if (!prof) return {{"success", false}, {"error", "GpuProfiler not available"}};
    const GpuTimingHistory& h = prof->getHistory();
    const size_t frames = requestedFrames(params, h.capacity());

    nlohmann::json scopes = nlohmann::json::array();
    nlohmann::json frame = nullptr;
    for (const auto& s : h.stats(frames)) {
        if (s.key == GpuProfiler::kFrameScopeName) {
            frame = statsJson(s);
            continue;
        }
        nlohmann::json j = statsJson(s);
        j["key"] = s.key;
        j["name"] = s.name;
        j["depth"] = s.depth;
        scopes.push_back(std::move(j));
    }
    return {{"success", true},
            {"frames_requested", frames},
            {"frames_used", std::min(frames, h.framesHeld())},
            {"frames_held", h.framesHeld()},
            {"capacity", h.capacity()},
            {"frames_accepted", h.framesAccepted()},
            {"stale_skipped", h.staleSkipped()},
            {"not_ready_frames", prof->getNotReadyFrames()},
            {"last_serial", h.lastSerial()},
            {"timestamp_valid_bits", prof->getTimestampValidBits()},
            {"gpu_frame_ms", frame},
            {"scopes", scopes}};
}

nlohmann::json setDepthPrepass(Graphics::RenderCoordinator* rc, const nlohmann::json& params) {
    if (!rc) return {{"success", false}, {"error", "RenderCoordinator not available"}};
    const bool available = rc->getRenderPipeline() && rc->getRenderPipeline()->hasDepthPrepass();
    if (params.contains("enabled")) {
        if (!params["enabled"].is_boolean())
            return {{"success", false}, {"error", "'enabled' must be a boolean"}, {"applied", false}};
        // Refused when the pipelines do not exist: a flag that claims ON while nothing runs is
        // exactly the silent no-op that misleads an A/B.
        if (params["enabled"].get<bool>() && !available)
            return {{"success", false}, {"error", "depth prepass pipelines unavailable (voxel_depth.frag.spv?)"},
                    {"available", false}, {"applied", false}};
        Graphics::RenderCoordinator::s_depthPrepass = params["enabled"].get<bool>();
    }
    return {{"success", true},
            {"enabled", Graphics::RenderCoordinator::s_depthPrepass},
            {"available", available},
            {"ran_last_frame", rc->depthPrepassRanLastFrame()},
            {"note", "applies from the next frame; skipped while a debug visualization pipeline is active"}};
}

nlohmann::json setGiProbeOptions(Graphics::RenderCoordinator* rc, const nlohmann::json& params) {
    if (!rc) return {{"success", false}, {"error", "RenderCoordinator not available"}};
    // Validate everything before applying anything, so a half-valid request changes nothing.
    for (const char* k : {"skip_buried", "two_level_trace"})
        if (params.contains(k) && !params[k].is_boolean())
            return {{"success", false}, {"error", std::string("'") + k + "' must be a boolean"}, {"applied", false}};
    if (params.contains("skip_buried")) Graphics::GiProbeField::s_skipBuried = params["skip_buried"].get<bool>();
    if (params.contains("two_level_trace")) Graphics::GiProbeField::s_twoLevelTrace = params["two_level_trace"].get<bool>();
    // gi_enabled: with the probe field off there is no probe pass, so the options act on nothing.
    return {{"success", true},
            {"skip_buried", Graphics::GiProbeField::s_skipBuried},
            {"two_level_trace", Graphics::GiProbeField::s_twoLevelTrace},
            {"gi_enabled", rc->getGiEnabled()},
            {"note", "applies from the next probe dispatch"}};
}

nlohmann::json cpuTiming(Graphics::RenderCoordinator* rc, const nlohmann::json& params) {
    if (!rc) return {{"success", false}, {"error", "RenderCoordinator not available"}};
    const GpuTimingHistory& h = rc->getCpuTiming().history();
    const size_t frames = requestedFrames(params, h.capacity());
    nlohmann::json scopes = nlohmann::json::array();
    for (const auto& s : h.stats(frames)) {
        nlohmann::json j = statsJson(s);
        j["key"] = s.key;
        j["name"] = s.name;
        j["depth"] = s.depth;
        scopes.push_back(std::move(j));
    }
    return {{"success", true},
            {"clock", "steady_clock (CPU wall time on the main thread)"},
            {"frames_requested", frames},
            {"frames_used", std::min(frames, h.framesHeld())},
            {"frames_held", h.framesHeld()},
            {"frames_accepted", h.framesAccepted()},
            {"scopes", scopes}};
}

nlohmann::json meshTiming(const nlohmann::json& params) {
    using CRM = Graphics::ChunkRenderManager;
    const auto s = CRM::getMeshPhaseStats();
    nlohmann::json phases = nlohmann::json::array();
    for (int i = 0; i < CRM::kMeshPhases; ++i)
        phases.push_back({{"phase", CRM::meshPhaseName(i)},
                          {"total_ms", s.phaseTotalMs[i]},
                          {"mean_ms", s.calls ? s.phaseTotalMs[i] / double(s.calls) : 0.0},
                          {"max_ms", s.phaseMaxMs[i]}});
    nlohmann::json buckets = nlohmann::json::array();
    for (int i = 0; i < CRM::kMicroBuckets; ++i)
        buckets.push_back({{"microcubes", CRM::microBucketName(i)},
                           {"rebuilds", s.bucketCalls[i]},
                           {"mean_ms", s.bucketCalls[i] ? s.bucketTotalMs[i] / double(s.bucketCalls[i]) : 0.0},
                           {"max_ms", s.bucketMaxMs[i]},
                           {"mean_microcubes", s.bucketCalls[i] ? double(s.bucketMicrocubes[i]) / double(s.bucketCalls[i]) : 0.0}});
    const bool reset = paramFlag(params, "reset");
    if (reset) CRM::resetMeshPhaseStats();
    return {{"success", true},
            {"rebuilds", s.calls},
            {"phases", phases},
            {"by_microcube_count", buckets},
            {"reset_after_read", reset}};
}

nlohmann::json gpuFrameSummary(const GpuProfiler* prof, size_t frames) {
    if (!prof) return nullptr;
    for (const auto& s : prof->getHistory().stats(frames))
        if (s.key == GpuProfiler::kFrameScopeName) return statsJson(s);
    return nullptr;
}

nlohmann::json setPipelineStats(GpuProfiler* prof, const nlohmann::json& params) {
    if (!prof) return {{"success", false}, {"error", "GpuProfiler not available"}};
    // Both names are accepted: the editor used "enabled", the standalone "on". Omitted means
    // unchanged. The editor used to default an omitted field to TRUE, so a bare POST switched the
    // counters on.
    const char* field = params.contains("enabled") ? "enabled" : (params.contains("on") ? "on" : nullptr);
    if (field) {
        if (!params[field].is_boolean())
            return {{"success", false}, {"error", std::string("'") + field + "' must be a boolean"}};
        prof->setPipelineStatsActive(params[field].get<bool>());
    }
    // The toggle applies at the next frame boundary (never mid-frame), so both states are echoed.
    return {{"success", true},
            {"enabled", prof->getPipelineStatsRequested()},
            {"pipeline_stats_active", prof->getPipelineStatsActive()},
            {"applies_at", "next_frame"}};
}

nlohmann::json lightStats(Graphics::RenderCoordinator* rc) {
    if (!rc) return {{"success", false}, {"error", "RenderCoordinator not available"}};
    using Graphics::LightManager;
    using Graphics::LightSource;
    const LightManager::Census c = rc->getLightManager().census();

    nlohmann::json bySource = nlohmann::json::object();
    nlohmann::json bySourceUploaded = nlohmann::json::object();
    for (size_t i = 0; i < static_cast<size_t>(LightSource::Count); ++i) {
        const char* name = Graphics::lightSourceName(static_cast<LightSource>(i));
        bySource[name] = c.bySource[i];
        bySourceUploaded[name] = c.bySourceUploaded[i];
    }
    static const char* kBinLabels[LightManager::Census::kRadiusBins] = {
        "[0,2)", "[2,4)", "[4,6)", "[6,8)", "[8,10)", "[10,15)", "[15,inf)"};
    nlohmann::json hist = nlohmann::json::array();
    for (size_t b = 0; b < LightManager::Census::kRadiusBins; ++b)
        hist.push_back({{"radius_u", kBinLabels[b]}, {"count", c.radiusHist[b]}});

    const auto& er = rc->getEmitterReconcileStats();
    const size_t uploaded = c.uploadedPoint + c.uploadedSpot;
    return {{"success", true},
            {"registered", c.registeredPoint + c.registeredSpot},
            {"registered_point", c.registeredPoint},
            {"registered_spot", c.registeredSpot},
            {"enabled_point", c.enabledPoint},
            {"enabled_spot", c.enabledSpot},
            {"uploaded_point", c.uploadedPoint},
            {"uploaded_spot", c.uploadedSpot},
            {"dropped_point", c.droppedPoint},
            {"dropped_spot", c.droppedSpot},
            {"max_point", Graphics::MAX_POINT_LIGHTS},
            {"max_spot", Graphics::MAX_SPOT_LIGHTS},
            // Duplicates: lights sharing a position (within 1e-3 u). Each still runs its own
            // per-fragment march, so uploaded - unique_positions_uploaded is pure waste.
            {"unique_positions_registered", c.uniquePositionsRegistered},
            {"unique_positions_uploaded", c.uniquePositionsUploaded},
            {"duplicate_uploads", uploaded - std::min(uploaded, c.uniquePositionsUploaded)},
            {"by_source", bySource},
            {"by_source_uploaded", bySourceUploaded},
            {"point_radius_hist", hist},
            {"cpu_ms", {{"select_sort", c.lastSelectMs},
                        {"selections", c.selections},
                        {"emitter_hash", er.hashMs},
                        {"emitter_rebuild_last", er.lastRebuildMs},
                        {"emitter_rebuilds", er.rebuilds},
                        {"emitters", er.emitters}}}};
}

namespace {

nlohmann::json maskJson(uint32_t mask) {
    return nlohmann::json::array({(mask & 1u) != 0, (mask & 2u) != 0, (mask & 4u) != 0});
}

}  // namespace

nlohmann::json voxelTiers(Graphics::RenderCoordinator* rc, ChunkManager* cm, const nlohmann::json& params) {
    if (!rc || !cm) return {{"success", false}, {"error", "RenderCoordinator/ChunkManager not available"}};
    namespace TR = Graphics::TierRanges;
    const bool perChunk = paramFlag(params, "per_chunk");
    const bool wantCovered = paramFlag(params, "covered");
    // Capped so a per-chunk dump of a large world cannot stall the 5 s game-loop wait.
    constexpr size_t kMaxChunksListed = 512;

    uint64_t merged[TR::kTiers] = {}, units[TR::kTiers] = {};
    size_t storedCubes = 0, storedSubs = 0, storedMicros = 0, subBytes = 0, microBytes = 0;
    uint64_t covered = 0, coveredUnknown = 0;
    size_t chunksMeshed = 0;
    nlohmann::json chunkList = nlohmann::json::array();
    bool truncated = false;

    for (const auto& ch : cm->chunks) {
        if (!ch) continue;
        const auto fs = ch->fineStorageStats();
        storedCubes += fs.cubes;
        storedSubs += fs.subcubes;
        storedMicros += fs.microcubes;
        subBytes += fs.subcubeBytes;
        microBytes += fs.microcubeBytes;
        const auto& tf = ch->getTierFaces();
        const auto& tu = ch->getTierUnitFaces();
        for (uint32_t t = 0; t < TR::kTiers; ++t) {
            merged[t] += tf[t];
            units[t] += tu[t];
        }
        if (ch->getNumInstances() > 0) ++chunksMeshed;
        uint64_t cc = 0, cu = 0;
        if (wantCovered) {
            ch->countCoveredCubeFaces(cc, cu);
            covered += cc;
            coveredUnknown += cu;
        }
        if (perChunk) {
            if (chunkList.size() >= kMaxChunksListed) { truncated = true; continue; }
            const glm::ivec3 o = ch->getWorldOrigin();
            nlohmann::json j = {{"origin", {o.x, o.y, o.z}},
                                {"stored", {{"cube", fs.cubes}, {"sub", fs.subcubes}, {"micro", fs.microcubes}}},
                                {"faces_merged", {tf[0], tf[1], tf[2], tf[3]}},
                                {"unit_faces", {tu[0], tu[1], tu[2], tu[3]}}};
            if (wantCovered) j["covered_cube_faces"] = {{"covered", cc}, {"unknown", cu}};
            chunkList.push_back(std::move(j));
        }
    }

    const auto& ds = rc->getTierDrawStats();
    const size_t instanceBytes = sizeof(Phyxel::InstanceData);
    static const char* kNames[TR::kTiers] = {"cube", "sub", "micro", "lod_cell"};
    const size_t stored[TR::kTiers] = {storedCubes, storedSubs, storedMicros, 0};
    nlohmann::json tiers = nlohmann::json::object();
    for (uint32_t t = 0; t < TR::kTiers; ++t) {
        nlohmann::json j = {
            {"faces_merged", merged[t]},
            {"unit_faces_premerge", units[t]},
            {"merge_ratio", merged[t] ? double(units[t]) / double(merged[t]) : 0.0},
            {"faces_view_main", ds.mainFaces[t]},
            {"shadow_faces_view", {{"mid", ds.shadowFaces[0][t]}, {"near", ds.shadowFaces[1][t]},
                                   {"far", ds.shadowFaces[2][t]}}},
            {"gpu_bytes", merged[t] * instanceBytes}};
        if (t != TR::kLodCell) j["stored"] = stored[t];
        // Sub/micro objects are heap objects (floor: excludes allocator overhead + lookup maps).
        // Cubes live in the palette voxel store, whose bytes are not attributed per voxel here.
        if (t == TR::kSub) j["cpu_bytes_floor"] = subBytes;
        if (t == TR::kMicro) j["cpu_bytes_floor"] = microBytes;
        tiers[kNames[t]] = std::move(j);
    }

    nlohmann::json out = {
        {"success", true},
        {"tiers", tiers},
        {"chunks_total", cm->chunks.size()},
        {"chunks_meshed", chunksMeshed},
        {"main_chunks_unattributed", ds.mainChunksUnattributed},
        {"masked_chunks_skipped", ds.maskedChunksSkipped},
        {"shadow_cmd_overflow", ds.shadowCmdOverflow},
        {"tier_mask", {{"main", maskJson(Graphics::RenderCoordinator::s_tierMaskMain)},
                       {"shadow", maskJson(Graphics::RenderCoordinator::s_tierMaskShadow)}}},
        {"per_chunk", perChunk},
        {"covered", wantCovered}};
    if (wantCovered)
        out["covered_cube_faces"] = {{"covered", covered}, {"unknown_cross_chunk", coveredUnknown},
                                     {"cube_unit_faces", units[TR::kCube]}};
    if (perChunk) {
        out["chunks"] = std::move(chunkList);
        out["truncated"] = truncated;
    }
    return out;
}

nlohmann::json setTierMask(const nlohmann::json& params) {
    // Validate everything before applying anything: a half-applied request would leave a mask the
    // caller did not ask for, and every later measurement would be attributed wrongly.
    auto parse = [&](const char* key, uint32_t& out, std::string& err) -> bool {
        if (!params.contains(key)) return true;   // omitted = unchanged
        const auto& a = params[key];
        if (!a.is_array() || a.size() != 3) { err = std::string("'") + key + "' must be [cube, sub, micro] booleans"; return false; }
        uint32_t m = 0;
        for (size_t i = 0; i < 3; ++i) {
            if (!a[i].is_boolean()) { err = std::string("'") + key + "' must be [cube, sub, micro] booleans"; return false; }
            if (a[i].get<bool>()) m |= 1u << i;
        }
        out = m;
        return true;
    };
    uint32_t mainMask = Graphics::RenderCoordinator::s_tierMaskMain;
    uint32_t shadowMask = Graphics::RenderCoordinator::s_tierMaskShadow;
    std::string err;
    if (!parse("main", mainMask, err) || !parse("shadow", shadowMask, err))
        return {{"success", false}, {"error", err}, {"applied", false}};
    Graphics::RenderCoordinator::s_tierMaskMain = mainMask;
    Graphics::RenderCoordinator::s_tierMaskShadow = shadowMask;
    return {{"success", true},
            {"main", maskJson(mainMask)},
            {"shadow", maskJson(shadowMask)},
            {"note", "LOD cells always draw; the OIT and mirror passes are not masked"}};
}

const char* presentModeName(VkPresentModeKHR mode) {
    switch (mode) {
        case VK_PRESENT_MODE_IMMEDIATE_KHR: return "IMMEDIATE";
        case VK_PRESENT_MODE_MAILBOX_KHR: return "MAILBOX";
        case VK_PRESENT_MODE_FIFO_KHR: return "FIFO";
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "FIFO_RELAXED";
        default: return "OTHER";
    }
}

// ---- City benchmark tooling (docs/PerfProgram2026-09.md section 16) ----

namespace {

nlohmann::json numOrNull(float v) { return std::isnan(v) ? nlohmann::json(nullptr) : nlohmann::json(v); }

bool finiteNumber(const nlohmann::json& v) {
    return v.is_number() && std::isfinite(v.get<double>());
}

}  // namespace

nlohmann::json framePacing(const PerfCapture* pc, const nlohmann::json& params) {
    if (!pc) return {{"success", false}, {"error", "frame pacing not available in this host"}};
    const GpuTimingHistory& h = pc->framePacing().history();
    const size_t frames = requestedFrames(params, h.capacity());
    nlohmann::json scopes = nlohmann::json::array();
    for (const auto& s : h.stats(frames)) {
        nlohmann::json j = statsJson(s);
        j["key"] = s.key;
        j["name"] = s.name;
        j["depth"] = s.depth;
        scopes.push_back(std::move(j));
    }
    nlohmann::json series = nlohmann::json::array();
    for (const auto& f : h.series(frames)) {
        nlohmann::json values = nlohmann::json::object();
        for (const auto& [key, ms] : f.values) values[key] = ms;
        series.push_back({{"serial", f.serial}, {"values", std::move(values)}});
    }
    return {{"success", true},
            {"clock", "steady_clock (CPU wall time on the main thread)"},
            {"frames_requested", frames},
            {"frames_used", std::min(frames, h.framesHeld())},
            {"frames_held", h.framesHeld()},
            {"frames_accepted", h.framesAccepted()},
            {"scopes", scopes},
            {"series", series}};
}

nlohmann::json recordControl(PerfCapture* pc, const nlohmann::json& params) {
    if (!pc) return {{"success", false}, {"error", "recorder not available in this host"}};
    RouteRecorder& r = pc->recorder();
    auto state = [&r]() {
        return nlohmann::json{{"recording", r.recording()}, {"frames", r.frames()},
                              {"capacity", r.framesCapacity()}, {"truncated", r.truncated()}};
    };
    const bool start = params.value("start", false);
    const bool stop = params.value("stop", false);
    if (start == stop) {
        nlohmann::json out = state();
        out["success"] = false;
        out["error"] = "send exactly one of {start:true, max_frames:N} or {stop:true}";
        return out;
    }
    if (stop) {
        r.stop();
        nlohmann::json out = state();
        out["success"] = true;
        return out;
    }
    long long n = 6000;
    if (params.contains("max_frames")) {
        if (!finiteNumber(params["max_frames"]))
            return {{"success", false}, {"error", "'max_frames' must be a number"}};
        n = static_cast<long long>(params["max_frames"].get<double>());
    }
    // Clamped to [1, kMaxFrames]: the buffer is allocated once, here, so its size is bounded.
    const size_t cap = static_cast<size_t>(std::clamp<long long>(n, 1, static_cast<long long>(RouteRecorder::kMaxFrames)));
    if (!r.start(cap)) {
        nlohmann::json out = state();
        out["success"] = false;
        out["error"] = "already recording: stop the current recording first";
        return out;
    }
    nlohmann::json out = state();
    out["success"] = true;
    return out;
}

nlohmann::json recordDump(const PerfCapture* pc, const nlohmann::json& params) {
    if (!pc) return {{"success", false}, {"error", "recorder not available in this host"}};
    const RouteRecorder& r = pc->recorder();
    auto readIndex = [&params](const char* key, size_t dflt) -> size_t {
        if (!params.contains(key)) return dflt;
        const auto& v = params[key];
        long long n = static_cast<long long>(dflt);
        if (v.is_number()) n = static_cast<long long>(v.get<double>());
        else if (v.is_string()) { try { n = std::stoll(v.get<std::string>()); } catch (...) {} }
        return static_cast<size_t>(std::max<long long>(0, n));
    };
    constexpr size_t kPage = 2048;   // bounds one response (a route is read in pages)
    const size_t from = std::min(readIndex("from", 0), r.frames());
    const size_t count = std::min({readIndex("count", kPage), kPage, r.frames() - from});
    nlohmann::json rows = nlohmann::json::array();
    const size_t nPhase = r.phaseKeys().size(), nGpu = r.gpuKeys().size();
    for (size_t i = from; i < from + count; ++i) {
        const auto& row = r.row(i);
        nlohmann::json phases = nlohmann::json::array(), gpu = nlohmann::json::array();
        for (size_t k = 0; k < nPhase; ++k) phases.push_back(numOrNull(row.phaseMs[k]));
        for (size_t k = 0; k < nGpu; ++k) gpu.push_back(numOrNull(row.gpuMs[k]));
        rows.push_back({{"frame", row.frame},
                        {"gpu_serial", row.gpuSerial},
                        {"frame_ms", row.frameMs},
                        {"phases", std::move(phases)},
                        {"gpu", std::move(gpu)},
                        {"gpu_resolved", row.gpuResolved},
                        {"camera", {row.cameraPos.x, row.cameraPos.y, row.cameraPos.z, row.cameraYaw, row.cameraPitch}},
                        {"path_progress", row.pathProgress},
                        {"streaming", {{"resident_chunks", row.residentChunks},
                                       {"pending_generation", row.pendingGeneration},
                                       {"pending_remesh", row.pendingRemesh},
                                       {"camera_chunk_resident",
                                        row.cameraChunkResident < 0 ? nlohmann::json(nullptr)
                                                                    : nlohmann::json(row.cameraChunkResident == 1)}}}});
    }
    return {{"success", true},
            {"recording", r.recording()},
            {"frames", r.frames()},
            {"capacity", r.framesCapacity()},
            {"truncated", r.truncated()},
            {"dropped_keys", r.droppedKeys()},
            {"from", from},
            {"count", count},
            {"phase_keys", r.phaseKeys()},
            {"gpu_keys", r.gpuKeys()},
            {"rows", rows}};
}

nlohmann::json cameraPath(PerfCapture* pc, Graphics::CameraManager* cameras, ChunkManager* chunks,
                          const Graphics::Camera* camera, const nlohmann::json& params) {
    if (!pc || !cameras) return {{"success", false}, {"error", "camera path not available in this host"}};
    Graphics::CameraPath& path = cameras->getPath();
    auto status = [&]() {
        nlohmann::json j = {{"playing", path.isPlaying()},
                            {"finished", path.isFinished()},
                            {"progress", path.progress()},
                            {"arc_length_u", path.arcLength()},
                            {"speed_u_per_s", path.constantSpeed()},
                            {"stream_follow", pc->streamFollow()},
                            {"focus_holder", chunks ? chunks->streamingFocusHolder() : std::string()}};
        if (camera) {
            const glm::vec3 p = camera->getPosition();
            j["camera"] = {{"x", p.x}, {"y", p.y}, {"z", p.z}, {"yaw", camera->getYaw()}, {"pitch", camera->getPitch()}};
        }
        return j;
    };
    auto refuse = [&](const std::string& why) {
        nlohmann::json j = status();
        j["success"] = false;
        j["applied"] = false;
        j["error"] = why;
        return j;
    };

    if (params.empty()) {   // GET
        nlohmann::json j = status();
        j["success"] = true;
        return j;
    }
    if (params.value("stop", false)) {
        path.stop();
        pc->setStreamFollow(false);
        const bool released = chunks && chunks->clearStreamingFocusOverride(PerfCapture::kFocusHolder);
        nlohmann::json j = status();
        j["success"] = true;
        j["stopped"] = true;
        j["focus_released"] = released;
        return j;
    }

    // ---- start: validate EVERYTHING before applying anything ----
    if (!params.contains("waypoints") || !params["waypoints"].is_array() || params["waypoints"].size() < 2)
        return refuse("'waypoints' must be an array of at least 2 {x,y,z,yaw,pitch}");
    std::vector<Graphics::CameraWaypoint> wps;
    for (const auto& w : params["waypoints"]) {
        for (const char* k : {"x", "y", "z", "yaw", "pitch"})
            if (!w.contains(k) || !finiteNumber(w[k]))
                return refuse(std::string("every waypoint needs a finite '") + k + "'");
        wps.push_back({glm::vec3(w["x"].get<float>(), w["y"].get<float>(), w["z"].get<float>()),
                       w["yaw"].get<float>(), w["pitch"].get<float>(), 0.0f});
    }
    if (!params.contains("speed_u_per_s") || !finiteNumber(params["speed_u_per_s"]))
        return refuse("'speed_u_per_s' (world units per second) is required");
    const float speed = params["speed_u_per_s"].get<float>();
    // (0, 64] u/s. Above that the path is a flythrough no player performs, and the streaming focus
    // (which follows it, at most StreamingFocus::kMaxStepPerFrame per frame) falls behind the camera:
    // the benchmark would measure streaming FAILURE instead of streaming cost.
    if (!(speed > 0.0f) || speed > kMaxPathSpeedUnitsPerSec)
        return refuse("'speed_u_per_s' must be in (0, 64]");
    const bool loop = params.value("loop", false);
    const bool follow = params.value("stream_follow", true);
    if (follow && chunks && chunks->hasStreamingFocusOverride() &&
        chunks->streamingFocusHolder() != PerfCapture::kFocusHolder)
        return refuse("stream_follow refused: the streaming focus is held by '" + chunks->streamingFocusHolder() + "'");

    path.clearWaypoints();
    for (const auto& wp : wps) path.addWaypoint(wp);
    path.setConstantSpeed(speed);
    path.setLooping(loop);
    path.play();
    pc->setStreamFollow(follow);
    nlohmann::json j = status();
    j["success"] = true;
    j["applied"] = true;
    j["waypoints"] = wps.size();
    j["duration_s"] = loop ? -1.0f : path.arcLength() / speed;
    return j;
}

}  // namespace Phyxel::Core::PerfApi
