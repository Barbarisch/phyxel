#include "core/SettlementProgram.h"

#include <fstream>

#include "utils/Logger.h"

namespace Phyxel {
namespace Core {

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
        }
    }
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
