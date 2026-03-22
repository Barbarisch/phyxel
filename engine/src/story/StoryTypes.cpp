#include "story/StoryTypes.h"
#include <algorithm>

namespace Phyxel {
namespace Story {

// ============================================================================
// WorldVariable JSON
// ============================================================================

void to_json(nlohmann::json& j, const WorldVariable& v) {
    j["key"] = v.key;
    std::visit([&](auto&& val) { j["value"] = val; }, v.value);
    // Tag the type for round-trip
    if (std::holds_alternative<bool>(v.value))        j["type"] = "bool";
    else if (std::holds_alternative<int>(v.value))     j["type"] = "int";
    else if (std::holds_alternative<float>(v.value))   j["type"] = "float";
    else if (std::holds_alternative<std::string>(v.value)) j["type"] = "string";
}

void from_json(const nlohmann::json& j, WorldVariable& v) {
    v.key = j.at("key").get<std::string>();
    std::string type = j.value("type", "string");
    if (type == "bool")        v.value = j.at("value").get<bool>();
    else if (type == "int")    v.value = j.at("value").get<int>();
    else if (type == "float")  v.value = j.at("value").get<float>();
    else                       v.value = j.at("value").get<std::string>();
}

// ============================================================================
// Location JSON
// ============================================================================

void to_json(nlohmann::json& j, const Location& loc) {
    j = nlohmann::json{
        {"id", loc.id},
        {"name", loc.name},
        {"worldPosition", {loc.worldPosition.x, loc.worldPosition.y, loc.worldPosition.z}},
        {"radius", loc.radius},
        {"controllingFaction", loc.controllingFaction},
        {"tags", loc.tags}
    };
}

void from_json(const nlohmann::json& j, Location& loc) {
    loc.id = j.at("id").get<std::string>();
    loc.name = j.at("name").get<std::string>();
    auto pos = j.at("worldPosition");
    loc.worldPosition = glm::vec3(pos[0].get<float>(), pos[1].get<float>(), pos[2].get<float>());
    loc.radius = j.value("radius", 32.0f);
    loc.controllingFaction = j.value("controllingFaction", "");
    loc.tags = j.value("tags", std::vector<std::string>{});
}

// ============================================================================
// Faction JSON
// ============================================================================

void to_json(nlohmann::json& j, const Faction& f) {
    j = nlohmann::json{
        {"id", f.id},
        {"name", f.name},
        {"relations", f.relations},
        {"memberCharacterIds", f.memberCharacterIds},
        {"controlledLocationIds", f.controlledLocationIds}
    };
}

void from_json(const nlohmann::json& j, Faction& f) {
    f.id = j.at("id").get<std::string>();
    f.name = j.at("name").get<std::string>();
    f.relations = j.value("relations", std::unordered_map<std::string, float>{});
    f.memberCharacterIds = j.value("memberCharacterIds", std::vector<std::string>{});
    f.controlledLocationIds = j.value("controlledLocationIds", std::vector<std::string>{});
}

// ============================================================================
// WorldEvent JSON
// ============================================================================

void to_json(nlohmann::json& j, const WorldEvent& e) {
    j = nlohmann::json{
        {"id", e.id},
        {"type", e.type},
        {"timestamp", e.timestamp},
        {"location", {e.location.x, e.location.y, e.location.z}},
        {"audibleRadius", e.audibleRadius},
        {"visibleRadius", e.visibleRadius},
        {"participants", e.participants},
        {"affectedFactions", e.affectedFactions},
        {"details", e.details},
        {"importance", e.importance}
    };
}

void from_json(const nlohmann::json& j, WorldEvent& e) {
    e.id = j.at("id").get<std::string>();
    e.type = j.at("type").get<std::string>();
    e.timestamp = j.value("timestamp", 0.0f);
    if (j.contains("location")) {
        auto loc = j["location"];
        e.location = glm::vec3(loc[0].get<float>(), loc[1].get<float>(), loc[2].get<float>());
    }
    e.audibleRadius = j.value("audibleRadius", 0.0f);
    e.visibleRadius = j.value("visibleRadius", 0.0f);
    e.participants = j.value("participants", std::vector<std::string>{});
    e.affectedFactions = j.value("affectedFactions", std::vector<std::string>{});
    e.details = j.value("details", nlohmann::json::object());
    e.importance = j.value("importance", 0.5f);
}

// ============================================================================
// WorldState JSON
// ============================================================================

void to_json(nlohmann::json& j, const WorldState& ws) {
    j["worldTime"] = ws.worldTime;
    j["dramaTension"] = ws.dramaTension;

    j["factions"] = nlohmann::json::object();
    for (auto& [id, faction] : ws.factions)
        j["factions"][id] = faction;

    j["locations"] = nlohmann::json::object();
    for (auto& [id, loc] : ws.locations)
        j["locations"][id] = loc;

    j["variables"] = nlohmann::json::object();
    for (auto& [key, var] : ws.variables)
        j["variables"][key] = var;

    j["eventHistory"] = ws.eventHistory;
}

void from_json(const nlohmann::json& j, WorldState& ws) {
    ws.worldTime = j.value("worldTime", 0.0f);
    ws.dramaTension = j.value("dramaTension", 0.0f);

    if (j.contains("factions")) {
        for (auto& [id, val] : j["factions"].items()) {
            ws.factions[id] = val.get<Faction>();
        }
    }
    if (j.contains("locations")) {
        for (auto& [id, val] : j["locations"].items()) {
            ws.locations[id] = val.get<Location>();
        }
    }
    if (j.contains("variables")) {
        for (auto& [key, val] : j["variables"].items()) {
            ws.variables[key] = val.get<WorldVariable>();
        }
    }
    ws.eventHistory = j.value("eventHistory", std::vector<WorldEvent>{});
}

// ============================================================================
// WorldState — mutation helpers
// ============================================================================

void WorldState::addFaction(Faction faction) {
    factions[faction.id] = std::move(faction);
}

void WorldState::addLocation(Location location) {
    locations[location.id] = std::move(location);
}

void WorldState::setVariable(const std::string& key, VariantValue value) {
    variables[key] = WorldVariable{key, std::move(value)};
}

void WorldState::recordEvent(WorldEvent event) {
    event.timestamp = worldTime;
    eventHistory.push_back(std::move(event));
}

// ============================================================================
// WorldState — query helpers
// ============================================================================

const Faction* WorldState::getFaction(const std::string& id) const {
    auto it = factions.find(id);
    return (it != factions.end()) ? &it->second : nullptr;
}

const Location* WorldState::getLocation(const std::string& id) const {
    auto it = locations.find(id);
    return (it != locations.end()) ? &it->second : nullptr;
}

const WorldVariable* WorldState::getVariable(const std::string& key) const {
    auto it = variables.find(key);
    return (it != variables.end()) ? &it->second : nullptr;
}

float WorldState::getFactionRelation(const std::string& factionA, const std::string& factionB) const {
    auto itA = factions.find(factionA);
    if (itA != factions.end()) {
        auto rel = itA->second.relations.find(factionB);
        if (rel != itA->second.relations.end())
            return rel->second;
    }
    // Check reverse direction
    auto itB = factions.find(factionB);
    if (itB != factions.end()) {
        auto rel = itB->second.relations.find(factionA);
        if (rel != itB->second.relations.end())
            return rel->second;
    }
    return 0.0f; // Neutral by default
}

std::vector<const WorldEvent*> WorldState::getEventsOfType(const std::string& type) const {
    std::vector<const WorldEvent*> result;
    for (auto& e : eventHistory) {
        if (e.type == type)
            result.push_back(&e);
    }
    return result;
}

std::vector<const WorldEvent*> WorldState::getEventsSince(float sinceTime) const {
    std::vector<const WorldEvent*> result;
    for (auto& e : eventHistory) {
        if (e.timestamp >= sinceTime)
            result.push_back(&e);
    }
    return result;
}

} // namespace Story
} // namespace Phyxel
