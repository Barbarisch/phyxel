#include "core/ReputationSystem.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace Phyxel::Core {

// ---------------------------------------------------------------------------
// Name helpers
// ---------------------------------------------------------------------------

const char* reputationTierName(ReputationTier tier) {
    switch (tier) {
        case ReputationTier::Hostile:    return "Hostile";
        case ReputationTier::Unfriendly: return "Unfriendly";
        case ReputationTier::Neutral:    return "Neutral";
        case ReputationTier::Friendly:   return "Friendly";
        case ReputationTier::Honored:    return "Honored";
        case ReputationTier::Exalted:    return "Exalted";
    }
    return "Neutral";
}

ReputationTier reputationTierFromString(const char* name) {
    if (!name) return ReputationTier::Neutral;
    auto eq = [&](const char* s) { return _stricmp(name, s) == 0; };
    if (eq("Hostile"))    return ReputationTier::Hostile;
    if (eq("Unfriendly")) return ReputationTier::Unfriendly;
    if (eq("Neutral"))    return ReputationTier::Neutral;
    if (eq("Friendly"))   return ReputationTier::Friendly;
    if (eq("Honored"))    return ReputationTier::Honored;
    if (eq("Exalted"))    return ReputationTier::Exalted;
    return ReputationTier::Neutral;
}

// ---------------------------------------------------------------------------
// FactionDefinition JSON
// ---------------------------------------------------------------------------

nlohmann::json FactionDefinition::toJson() const {
    nlohmann::json j;
    j["id"]          = id;
    j["name"]        = name;
    j["description"] = description;
    j["startingReputation"] = startingReputation;
    j["enemyFactionIds"] = enemyFactionIds;
    j["allyFactionIds"]  = allyFactionIds;
    return j;
}

FactionDefinition FactionDefinition::fromJson(const nlohmann::json& j) {
    FactionDefinition def;
    def.id          = j.value("id", "");
    def.name        = j.value("name", "");
    def.description = j.value("description", "");
    def.startingReputation = j.value("startingReputation", 0);
    if (j.contains("enemyFactionIds") && j["enemyFactionIds"].is_array()) {
        for (const auto& e : j["enemyFactionIds"])
            def.enemyFactionIds.push_back(e.get<std::string>());
    }
    if (j.contains("allyFactionIds") && j["allyFactionIds"].is_array()) {
        for (const auto& a : j["allyFactionIds"])
            def.allyFactionIds.push_back(a.get<std::string>());
    }
    return def;
}

// ---------------------------------------------------------------------------
// FactionRegistry
// ---------------------------------------------------------------------------

FactionRegistry& FactionRegistry::instance() {
    static FactionRegistry s_instance;
    return s_instance;
}

void FactionRegistry::registerFaction(FactionDefinition def) {
    m_factions[def.id] = std::move(def);
}

const FactionDefinition* FactionRegistry::getFaction(const std::string& factionId) const {
    auto it = m_factions.find(factionId);
    return (it != m_factions.end()) ? &it->second : nullptr;
}

std::vector<std::string> FactionRegistry::getAllFactionIds() const {
    std::vector<std::string> ids;
    ids.reserve(m_factions.size());
    for (const auto& [id, _] : m_factions)
        ids.push_back(id);
    return ids;
}

void FactionRegistry::clear() {
    m_factions.clear();
}

bool FactionRegistry::loadFromFile(const std::string& filePath) {
    std::ifstream f(filePath);
    if (!f.is_open()) return false;
    try {
        auto j = nlohmann::json::parse(f);
        if (j.is_array()) {
            for (const auto& item : j)
                registerFaction(FactionDefinition::fromJson(item));
        } else if (j.is_object() && j.contains("factions") && j["factions"].is_array()) {
            for (const auto& item : j["factions"])
                registerFaction(FactionDefinition::fromJson(item));
        } else if (j.is_object()) {
            registerFaction(FactionDefinition::fromJson(j));
        }
        return true;
    } catch (...) {
        return false;
    }
}

int FactionRegistry::loadFromDirectory(const std::string& dirPath) {
    namespace fs = std::filesystem;
    int count = 0;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dirPath, ec)) {
        if (entry.path().extension() == ".json") {
            if (loadFromFile(entry.path().string()))
                ++count;
        }
    }
    return count;
}

// ---------------------------------------------------------------------------
// ReputationSystem
// ---------------------------------------------------------------------------

ReputationTier ReputationSystem::tierForScore(int score) {
    if (score >= REP_EXALTED_MIN)   return ReputationTier::Exalted;
    if (score >= REP_HONORED_MIN)   return ReputationTier::Honored;
    if (score >= REP_FRIENDLY_MIN)  return ReputationTier::Friendly;
    if (score >= REP_NEUTRAL_MIN)   return ReputationTier::Neutral;
    if (score >= REP_UNFRIENDLY_MIN) return ReputationTier::Unfriendly;
    return ReputationTier::Hostile;
}

int ReputationSystem::getReputation(const std::string& entityId,
                                     const std::string& factionId) const {
    auto eit = m_reputations.find(entityId);
    if (eit == m_reputations.end()) {
        // Return starting reputation from registry if available
        const auto* def = FactionRegistry::instance().getFaction(factionId);
        return def ? def->startingReputation : 0;
    }
    auto fit = eit->second.find(factionId);
    if (fit == eit->second.end()) {
        const auto* def = FactionRegistry::instance().getFaction(factionId);
        return def ? def->startingReputation : 0;
    }
    return fit->second;
}

void ReputationSystem::setReputation(const std::string& entityId,
                                      const std::string& factionId, int value) {
    int clamped = std::clamp(value, REP_MIN, REP_MAX);
    m_reputations[entityId][factionId] = clamped;
}

void ReputationSystem::adjustReputation(const std::string& entityId,
                                         const std::string& factionId, int delta) {
    int current = getReputation(entityId, factionId);
    setReputation(entityId, factionId, current + delta);
}

ReputationTier ReputationSystem::getTier(const std::string& entityId,
                                          const std::string& factionId) const {
    return tierForScore(getReputation(entityId, factionId));
}

bool ReputationSystem::isHostile(const std::string& entityId,
                                   const std::string& factionId) const {
    return getTier(entityId, factionId) == ReputationTier::Hostile;
}

bool ReputationSystem::isFriendly(const std::string& entityId,
                                    const std::string& factionId) const {
    return getTier(entityId, factionId) >= ReputationTier::Friendly;
}

bool ReputationSystem::isHonored(const std::string& entityId,
                                   const std::string& factionId) const {
    return getTier(entityId, factionId) >= ReputationTier::Honored;
}

void ReputationSystem::removeEntity(const std::string& entityId) {
    m_reputations.erase(entityId);
}

void ReputationSystem::clear() {
    m_reputations.clear();
}

nlohmann::json ReputationSystem::toJson() const {
    nlohmann::json j = nlohmann::json::object();
    for (const auto& [entityId, factions] : m_reputations) {
        nlohmann::json fj = nlohmann::json::object();
        for (const auto& [factionId, score] : factions)
            fj[factionId] = score;
        j[entityId] = fj;
    }
    return j;
}

void ReputationSystem::fromJson(const nlohmann::json& j) {
    m_reputations.clear();
    for (auto& [entityId, factions] : j.items()) {
        for (auto& [factionId, score] : factions.items()) {
            int val = std::clamp(score.get<int>(), REP_MIN, REP_MAX);
            m_reputations[entityId][factionId] = val;
        }
    }
}

} // namespace Phyxel::Core
