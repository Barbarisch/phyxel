#include "core/PerfApi.h"

#include <algorithm>

#include "graphics/LightManager.h"
#include "graphics/RenderCoordinator.h"
#include "utils/GpuProfiler.h"

namespace Phyxel::Core::PerfApi {

namespace {

nlohmann::json statsJson(const GpuTimingStats& s) {
    return nlohmann::json{{"n", s.n},
                          {"median_ms", s.median},
                          {"p90_ms", s.p90},
                          {"p99_ms", s.p99},
                          {"mean_ms", s.mean},
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

const char* presentModeName(VkPresentModeKHR mode) {
    switch (mode) {
        case VK_PRESENT_MODE_IMMEDIATE_KHR: return "IMMEDIATE";
        case VK_PRESENT_MODE_MAILBOX_KHR: return "MAILBOX";
        case VK_PRESENT_MODE_FIFO_KHR: return "FIFO";
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "FIFO_RELAXED";
        default: return "OTHER";
    }
}

}  // namespace Phyxel::Core::PerfApi
