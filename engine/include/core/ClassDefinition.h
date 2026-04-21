#pragma once

#include "core/CharacterAttributes.h"
#include "core/ProficiencySystem.h"

#include <string>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

/// A single class feature (ability, trait, or mechanic) gained at a specific level.
struct ClassFeature {
    std::string id;           // "second_wind", "action_surge", "sneak_attack"
    std::string name;         // "Second Wind"
    std::string description;  // Short mechanical description
    int level = 1;            // Level at which it's gained

    nlohmann::json toJson() const;
    static ClassFeature fromJson(const nlohmann::json& j);
};

/// Static data describing a character class (Fighter, Wizard, Rogue, etc.).
/// Loaded from resources/classes/<id>.json at startup.
struct ClassDefinition {
    std::string id;    // "fighter", "wizard", "rogue"
    std::string name;  // "Fighter", "Wizard", "Rogue"

    /// Number of faces on the hit die (6, 8, 10, 12).
    int hitDieFaces = 8;

    /// Governing ability for the class's main role (e.g. STR for Fighter, INT for Wizard).
    AbilityType primaryAbility = AbilityType::Strength;

    /// Saving throw proficiencies granted by this class.
    std::vector<AbilityType> savingThrowProficiencies;

    /// Armor proficiency strings: "light", "medium", "heavy", "shields".
    std::vector<std::string> armorProficiencies;

    /// Weapon proficiency strings: "simple", "martial", or specific weapon ids.
    std::vector<std::string> weaponProficiencies;

    /// Tool proficiency strings (e.g. "thieves_tools", "herbalism_kit").
    std::vector<std::string> toolProficiencies;

    /// How many skills the player may choose at character creation.
    int skillChoices = 2;

    /// Pool of skills the player may pick from at character creation.
    std::vector<Skill> skillPool;

    /// Spellcasting ability ("INT", "WIS", "CHA", or "" for non-casters).
    std::string spellcastingAbility;

    /// Spellcasting type ("full", "half", "third", "pact", or "").
    std::string spellcastingType;

    /// All features the class grants, keyed by level (1–20).
    std::map<int, std::vector<ClassFeature>> features;

    /// Levels where an ASI (Ability Score Improvement) or feat is available.
    std::vector<int> asiLevels;

    // --- Queries ---

    /// All features gained at exactly this class level.
    std::vector<ClassFeature> getFeaturesAtLevel(int level) const;

    /// All features gained up to and including this level.
    std::vector<ClassFeature> getAllFeaturesUpToLevel(int level) const;

    /// Whether this class grants an ASI/feat at the given level.
    bool hasASIAtLevel(int level) const;

    nlohmann::json toJson() const;
    static ClassDefinition fromJson(const nlohmann::json& j);
};

// ---------------------------------------------------------------------------
// ClassRegistry — singleton, loaded from resources/classes/
// ---------------------------------------------------------------------------
class ClassRegistry {
public:
    static ClassRegistry& instance();

    bool registerClass(const ClassDefinition& def);
    const ClassDefinition* getClass(const std::string& id) const;
    bool hasClass(const std::string& id) const;
    std::vector<std::string> getAllClassIds() const;
    size_t count() const { return m_classes.size(); }

    /// Load all classes from a directory (reads every *.json file).
    int loadFromDirectory(const std::string& dirPath);

    /// Load a single class from a JSON file.
    bool loadFromFile(const std::string& filepath);

    /// Load from a JSON object (single class) or array of classes.
    int loadFromJson(const nlohmann::json& j);

    void clear();

private:
    ClassRegistry() = default;
    ClassRegistry(const ClassRegistry&) = delete;
    ClassRegistry& operator=(const ClassRegistry&) = delete;

    std::unordered_map<std::string, ClassDefinition> m_classes;
};

} // namespace Core
} // namespace Phyxel
