#include "core/PerfApi.h"

#include <algorithm>

#include "core/Chunk.h"
#include "core/ChunkManager.h"
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

namespace {

bool paramFlag(const nlohmann::json& params, const char* key) {
    if (!params.contains(key)) return false;
    const auto& v = params[key];
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_number()) return v.get<double>() != 0.0;
    if (v.is_string()) return v.get<std::string>() == "1" || v.get<std::string>() == "true";
    return false;
}

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

}  // namespace Phyxel::Core::PerfApi
