#include "core/SettlementProgram.h"

#include <algorithm>
#include <cmath>
#include <fstream>

#include "utils/Logger.h"

namespace Phyxel {
namespace Core {

SettlementTierPreset applyDensity(const SettlementTierPreset& t, double density) {
    const double d = std::clamp(density, 0.5, 2.0);
    if (d == 1.0) return t;                            // identity: legacy byte-compatible
    SettlementTierPreset out = t;
    // Blocks and plot depth scale by SQRT(density): both-sided lane infill needs a block to
    // hold two plot depths, so linear tightening starves the very rows density exists to add
    // (measured: linear at 1.5 dropped 42 -> 33 buildings; sqrt raises it instead).
    const double sq = std::sqrt(d);
    auto scaleDown = [&](int v, int floor_, double by) {   // tighter as density rises
        return std::max(floor_, static_cast<int>(std::lround(v / by)));
    };
    auto scaleUp = [&](int v, int cap) {                   // more as density rises
        return std::min(cap, std::max(1, static_cast<int>(std::lround(v * d))));
    };
    out.blocksMin = scaleDown(t.blocksMin, 8, sq);
    out.blocksMax = std::max(out.blocksMin, scaleDown(t.blocksMax, 10, sq));
    // Plots: shallower tofts (floor 6; the allocator still clamps to each typology's minDepth).
    out.plot.depthMin = scaleDown(t.plot.depthMin, 6, sq);
    out.plot.depthMax = std::max(out.plot.depthMin, scaleDown(t.plot.depthMax, 8, sq));
    out.plot.sideGap = std::max(0, static_cast<int>(std::lround(t.plot.sideGap / d)));
    // Setbacks: dense frontages build to the street line.
    out.setback.max = std::max(t.setback.min, static_cast<int>(std::lround(t.setback.max / d)));
    // More (or fewer) buildings, bounded well above any real tier.
    out.buildingsMin = scaleUp(t.buildingsMin, 400);
    out.buildingsMax = std::max(out.buildingsMin, scaleUp(t.buildingsMax, 400));
    // Dense quarters keep fewer enclosures.
    out.fenceFraction = std::clamp(t.fenceFraction / d, 0.0, 1.0);
    return out;
}

SettlementTierPreset SettlementProgramRegistry::parse(const std::string& era,
                                                      const std::string& tier,
                                                      const nlohmann::json& rec) {
    SettlementTierPreset t;
    t.era = era;
    t.name = tier;
    if (!rec.is_object()) return t;

    t.description = rec.value("description", "");
    t.morphology = rec.value("morphology", "");

    if (rec.contains("buildings") && rec["buildings"].is_object()) {
        t.buildingsMin = rec["buildings"].value("min", t.buildingsMin);
        t.buildingsMax = rec["buildings"].value("max", t.buildingsMax);
    }
    if (rec.contains("street") && rec["street"].is_object()) {
        const auto& s = rec["street"];
        t.street.mainWidth = s.value("main_width", 0);
        t.street.laneWidth = s.value("lane_width", 0);
        t.street.material = s.value("material", std::string("Dirt"));
        t.street.backLanes = s.value("back_lanes", false);
    }
    if (rec.contains("plot") && rec["plot"].is_object()) {
        const auto& p = rec["plot"];
        t.plot.depthMin = p.value("depth_min", t.plot.depthMin);
        t.plot.depthMax = p.value("depth_max", t.plot.depthMax);
        t.plot.sideGap = p.value("side_gap", t.plot.sideGap);
    }
    if (rec.contains("setback") && rec["setback"].is_object()) {
        t.setback.min = rec["setback"].value("min", t.setback.min);
        t.setback.max = rec["setback"].value("max", t.setback.max);
    }
    if (rec.contains("typology_weights") && rec["typology_weights"].is_object())
        for (auto it = rec["typology_weights"].begin(); it != rec["typology_weights"].end(); ++it)
            if (it.value().is_number()) t.typologyWeights[it.key()] = it.value().get<int>();
    if (rec.contains("core_typology_weights") && rec["core_typology_weights"].is_object())
        for (auto it = rec["core_typology_weights"].begin();
             it != rec["core_typology_weights"].end(); ++it)
            if (it.value().is_number()) t.coreTypologyWeights[it.key()] = it.value().get<int>();
    if (rec.contains("typology_caps") && rec["typology_caps"].is_object())
        for (auto it = rec["typology_caps"].begin(); it != rec["typology_caps"].end(); ++it)
            if (it.value().is_number()) t.typologyCaps[it.key()] = it.value().get<int>();
    t.coreRing = rec.value("core_ring", 0);
    if (rec.contains("blocks") && rec["blocks"].is_object()) {
        t.blocksMin = rec["blocks"].value("min", t.blocksMin);
        t.blocksMax = rec["blocks"].value("max", t.blocksMax);
    }
    if (rec.contains("public") && rec["public"].is_object()) {
        const auto& pb = rec["public"];
        t.pub.well = pb.value("well", false);
        if (pb.contains("market_square") && pb["market_square"].is_object()) {
            t.pub.marketW = pb["market_square"].value("w", 0);
            t.pub.marketD = pb["market_square"].value("d", 0);
            t.pub.stalls  = pb["market_square"].value("stalls", 0);
            t.pub.statue  = pb["market_square"].value("statue", false);
        }
    }
    if (rec.contains("walls") && rec["walls"].is_object()) {
        const auto& w = rec["walls"];
        t.walls.enabled          = w.value("enabled", false);
        t.walls.heightCubes      = w.value("height", t.walls.heightCubes);
        t.walls.thicknessCubes   = w.value("thickness", t.walls.thicknessCubes);
        t.walls.gateWidthCubes   = w.value("gate_width", t.walls.gateWidthCubes);
        t.walls.marginCubes      = w.value("margin", t.walls.marginCubes);
        t.walls.towers           = w.value("towers", t.walls.towers);
        t.walls.towerSize        = w.value("tower_size", t.walls.towerSize);
        t.walls.towerExtraHeight = w.value("tower_extra_height", t.walls.towerExtraHeight);
        t.walls.crenellations    = w.value("crenellations", t.walls.crenellations);
        t.walls.material         = w.value("material", t.walls.material);
    }
    if (rec.contains("fences") && rec["fences"].is_object())
        t.fenceFraction = std::clamp(rec["fences"].value("fraction", 1.0), 0.0, 1.0);
    if (rec.contains("sources") && rec["sources"].is_object())
        for (auto it = rec["sources"].begin(); it != rec["sources"].end(); ++it)
            if (it.value().is_string()) t.sources[it.key()] = it.value().get<std::string>();

    return t;
}

bool SettlementProgramRegistry::loadFromJson(const nlohmann::json& j) {
    if (!j.is_object() || !j.contains("eras") || !j["eras"].is_object()) return false;

    size_t loaded = 0;
    for (auto eraIt = j["eras"].begin(); eraIt != j["eras"].end(); ++eraIt) {
        const std::string& era = eraIt.key();
        if (era.empty() || era[0] == '_' || !eraIt.value().is_object()) continue;
        if (!eraIt.value().contains("tiers") || !eraIt.value()["tiers"].is_object()) continue;
        const auto& tiers = eraIt.value()["tiers"];
        for (auto tIt = tiers.begin(); tIt != tiers.end(); ++tIt) {
            const std::string& tier = tIt.key();
            if (tier.empty() || tier[0] == '_' || !tIt.value().is_object()) continue;
            m_tiers[era + "/" + tier] = parse(era, tier, tIt.value());
            ++loaded;
        }
    }
    return loaded > 0;
}

const SettlementTierPreset* SettlementProgramRegistry::get(const std::string& era,
                                                           const std::string& tier) const {
    auto it = m_tiers.find(era + "/" + tier);
    return it == m_tiers.end() ? nullptr : &it->second;
}

std::vector<std::string> SettlementProgramRegistry::eras() const {
    std::vector<std::string> out;
    for (const auto& [k, _] : m_tiers) {
        const std::string era = k.substr(0, k.find('/'));
        if (out.empty() || out.back() != era) out.push_back(era);  // m_tiers is sorted by key
    }
    return out;
}

std::vector<std::string> SettlementProgramRegistry::tiers(const std::string& era) const {
    std::vector<std::string> out;
    const std::string prefix = era + "/";
    for (const auto& [k, _] : m_tiers)
        if (k.rfind(prefix, 0) == 0) out.push_back(k.substr(prefix.size()));
    return out;
}

bool SettlementProgramRegistry::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_WARN_FMT("SettlementProgram", "settlement_program not found at " << path);
        return false;
    }
    nlohmann::json j;
    try {
        file >> j;
    } catch (const std::exception& e) {
        LOG_WARN_FMT("SettlementProgram", "parse error in " << path << ": " << e.what());
        return false;
    }
    const bool ok = loadFromJson(j);
    // Grounding rule (warn-but-allow): flag any tier without provenance.
    size_t unsourced = 0;
    for (const auto& [name, t] : m_tiers)
        if (t.sources.empty()) ++unsourced;
    if (unsourced > 0)
        LOG_WARN_FMT("SettlementProgram", unsourced << " of " << m_tiers.size()
                     << " settlement tiers are UNSOURCED — must be grounded");
    LOG_INFO_FMT("SettlementProgram", "Loaded " << m_tiers.size() << " settlement tiers from " << path);
    return ok;
}

} // namespace Core
} // namespace Phyxel
