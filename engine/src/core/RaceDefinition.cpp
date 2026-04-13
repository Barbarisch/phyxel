#include "core/RaceDefinition.h"
#include "utils/Logger.h"

#include <fstream>
#include <filesystem>

namespace Phyxel {
namespace Core {

// ---------------------------------------------------------------------------
// RaceDefinition
// ---------------------------------------------------------------------------

void RaceDefinition::applyTo(CharacterAttributes& attrs) const {
    for (const auto& [ability, bonus] : abilityBonuses) {
        attrs.get(ability).racial += bonus;
    }
}

nlohmann::json RaceDefinition::toJson() const {
    nlohmann::json j;
    j["id"]             = id;
    j["name"]           = name;
    j["speed"]          = speed;
    j["darkvisionRange"]= darkvisionRange;
    j["traits"]         = traits;
    j["languages"]      = languages;
    j["proficiencies"]  = proficiencies;

    nlohmann::json bonuses = nlohmann::json::object();
    for (const auto& [ability, bonus] : abilityBonuses) {
        bonuses[abilityShortName(ability)] = bonus;
    }
    j["abilityBonuses"] = bonuses;
    return j;
}

RaceDefinition RaceDefinition::fromJson(const nlohmann::json& j) {
    RaceDefinition r;
    r.id              = j.value("id", "");
    r.name            = j.value("name", r.id);
    r.speed           = j.value("speed", 30);
    r.darkvisionRange = j.value("darkvisionRange", 0);

    if (j.contains("traits"))        r.traits       = j["traits"].get<std::vector<std::string>>();
    if (j.contains("languages"))     r.languages    = j["languages"].get<std::vector<std::string>>();
    if (j.contains("proficiencies")) r.proficiencies= j["proficiencies"].get<std::vector<std::string>>();

    if (j.contains("abilityBonuses")) {
        for (const auto& [key, val] : j["abilityBonuses"].items()) {
            try {
                r.abilityBonuses[abilityFromString(key.c_str())] = val.get<int>();
            } catch (...) {
                LOG_WARN("RaceDefinition", "Unknown ability key '{}' in race '{}'", key, r.id);
            }
        }
    }
    return r;
}

// ---------------------------------------------------------------------------
// RaceRegistry
// ---------------------------------------------------------------------------

RaceRegistry& RaceRegistry::instance() {
    static RaceRegistry s_instance;
    return s_instance;
}

bool RaceRegistry::registerRace(const RaceDefinition& def) {
    if (def.id.empty()) {
        LOG_WARN("RaceRegistry", "Cannot register race with empty ID");
        return false;
    }
    auto [it, inserted] = m_races.emplace(def.id, def);
    if (!inserted) {
        LOG_WARN("RaceRegistry", "Race '{}' already registered — skipping", def.id);
    }
    return inserted;
}

const RaceDefinition* RaceRegistry::getRace(const std::string& id) const {
    auto it = m_races.find(id);
    return (it != m_races.end()) ? &it->second : nullptr;
}

bool RaceRegistry::hasRace(const std::string& id) const {
    return m_races.find(id) != m_races.end();
}

std::vector<std::string> RaceRegistry::getAllRaceIds() const {
    std::vector<std::string> ids;
    ids.reserve(m_races.size());
    for (const auto& [id, _] : m_races) ids.push_back(id);
    return ids;
}

int RaceRegistry::loadFromDirectory(const std::string& dirPath) {
    int count = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dirPath, ec)) {
        if (entry.path().extension() == ".json") {
            if (loadFromFile(entry.path().string())) ++count;
        }
    }
    if (ec) LOG_WARN("RaceRegistry", "Error iterating directory '{}': {}", dirPath, ec.message());
    return count;
}

bool RaceRegistry::loadFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        LOG_WARN("RaceRegistry", "Could not open race file: {}", filepath);
        return false;
    }
    try {
        nlohmann::json j = nlohmann::json::parse(file);
        loadFromJson(j);
        return true;
    } catch (const std::exception& e) {
        LOG_WARN("RaceRegistry", "Failed to parse race file '{}': {}", filepath, e.what());
        return false;
    }
}

int RaceRegistry::loadFromJson(const nlohmann::json& j) {
    int count = 0;
    if (j.is_array()) {
        for (const auto& item : j) {
            try {
                if (registerRace(RaceDefinition::fromJson(item))) ++count;
            } catch (const std::exception& e) {
                LOG_WARN("RaceRegistry", "Skipping race entry: {}", e.what());
            }
        }
    } else if (j.is_object()) {
        try {
            if (registerRace(RaceDefinition::fromJson(j))) ++count;
        } catch (const std::exception& e) {
            LOG_WARN("RaceRegistry", "Skipping race object: {}", e.what());
        }
    }
    return count;
}

void RaceRegistry::clear() {
    m_races.clear();
}

} // namespace Core
} // namespace Phyxel
