#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Story {

// ============================================================================
// WorldVariable — key/value store for global flags, counters, etc.
// ============================================================================

using VariantValue = std::variant<bool, int, float, std::string>;

struct WorldVariable {
    std::string key;
    VariantValue value;
};

void to_json(nlohmann::json& j, const WorldVariable& v);
void from_json(const nlohmann::json& j, WorldVariable& v);

// ============================================================================
// Location — a named area in the world with spatial bounds
// ============================================================================

struct Location {
    std::string id;
    std::string name;
    glm::vec3 worldPosition{0.0f};
    float radius = 32.0f;
    std::string controllingFaction;
    std::vector<std::string> tags;
};

void to_json(nlohmann::json& j, const Location& loc);
void from_json(const nlohmann::json& j, Location& loc);

// ============================================================================
// Faction — a group of characters with inter-faction standings
// ============================================================================

struct Faction {
    std::string id;
    std::string name;
    std::unordered_map<std::string, float> relations; // factionId → standing (-1 to 1)
    std::vector<std::string> memberCharacterIds;
    std::vector<std::string> controlledLocationIds;
};

void to_json(nlohmann::json& j, const Faction& f);
void from_json(const nlohmann::json& j, Faction& f);

// ============================================================================
// WorldEvent — immutable record of something that happened
// ============================================================================

struct WorldEvent {
    std::string id;
    std::string type;           // "combat", "dialogue", "trade", "death", etc.
    float timestamp = 0.0f;     // World time
    glm::vec3 location{0.0f};
    float audibleRadius = 0.0f;
    float visibleRadius = 0.0f;

    std::vector<std::string> participants;
    std::vector<std::string> affectedFactions;

    nlohmann::json details;
    float importance = 0.5f;    // 0.0-1.0, affects propagation speed/range
};

void to_json(nlohmann::json& j, const WorldEvent& e);
void from_json(const nlohmann::json& j, WorldEvent& e);

// ============================================================================
// WorldState — objective ground truth about the world
// ============================================================================

struct WorldState {
    std::unordered_map<std::string, Faction> factions;
    std::unordered_map<std::string, Location> locations;
    std::unordered_map<std::string, WorldVariable> variables;
    std::vector<WorldEvent> eventHistory;

    float worldTime = 0.0f;
    float dramaTension = 0.0f; // 0.0 = peaceful, 1.0 = crisis

    // --- Mutation helpers ---
    void addFaction(Faction faction);
    void addLocation(Location location);
    void setVariable(const std::string& key, VariantValue value);
    void recordEvent(WorldEvent event);

    // --- Query helpers ---
    const Faction* getFaction(const std::string& id) const;
    const Location* getLocation(const std::string& id) const;
    const WorldVariable* getVariable(const std::string& key) const;
    float getFactionRelation(const std::string& factionA, const std::string& factionB) const;
    std::vector<const WorldEvent*> getEventsOfType(const std::string& type) const;
    std::vector<const WorldEvent*> getEventsSince(float sinceTime) const;
};

void to_json(nlohmann::json& j, const WorldState& ws);
void from_json(const nlohmann::json& j, WorldState& ws);

} // namespace Story
} // namespace Phyxel
