#pragma once

#include "core/CharacterAttributes.h"

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

/// Static data describing a playable race (Human, Elf, Dwarf, etc.).
/// Loaded from resources/races/<id>.json at startup.
struct RaceDefinition {
    std::string id;    // "human", "elf_high", "dwarf_mountain"
    std::string name;  // "Human", "High Elf", "Mountain Dwarf"

    /// Flat bonuses added to ability scores (racial layer in AbilityScore).
    std::map<AbilityType, int> abilityBonuses;

    /// Base movement speed in feet (25 or 30 typically).
    int speed = 30;

    /// Darkvision range in feet (0 = none, 60 = standard darkvision).
    int darkvisionRange = 0;

    /// Named racial traits, e.g. "Fey Ancestry", "Lucky", "Relentless Endurance".
    /// Game code can branch on these strings; engine just stores them.
    std::vector<std::string> traits;

    /// Languages known (e.g. "Common", "Elvish", "Dwarvish").
    std::vector<std::string> languages;

    /// Proficiencies granted by race (skill, weapon, tool, or armor strings).
    std::vector<std::string> proficiencies;

    /// Apply racial ability score bonuses to an attribute set.
    void applyTo(CharacterAttributes& attrs) const;

    nlohmann::json toJson() const;
    static RaceDefinition fromJson(const nlohmann::json& j);
};

// ---------------------------------------------------------------------------
// RaceRegistry — singleton, loaded from resources/races/
// ---------------------------------------------------------------------------
class RaceRegistry {
public:
    static RaceRegistry& instance();

    bool registerRace(const RaceDefinition& def);
    const RaceDefinition* getRace(const std::string& id) const;
    bool hasRace(const std::string& id) const;
    std::vector<std::string> getAllRaceIds() const;
    size_t count() const { return m_races.size(); }

    /// Load all races from a directory (reads every *.json file).
    int loadFromDirectory(const std::string& dirPath);

    /// Load a single race from a JSON file.
    bool loadFromFile(const std::string& filepath);

    /// Load from a JSON object (single race) or array of races.
    int loadFromJson(const nlohmann::json& j);

    void clear();

private:
    RaceRegistry() = default;
    RaceRegistry(const RaceRegistry&) = delete;
    RaceRegistry& operator=(const RaceRegistry&) = delete;

    std::unordered_map<std::string, RaceDefinition> m_races;
};

} // namespace Core
} // namespace Phyxel
