#pragma once

#include "core/CharacterAttributes.h"
#include "core/ConditionSystem.h"
#include "core/ProficiencySystem.h"
#include "core/DiceSystem.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

// ============================================================================
// Monster size categories (D&D 5e)
// ============================================================================
enum class CreatureSize { Tiny, Small, Medium, Large, Huge, Gargantuan };

const char* creatureSizeName(CreatureSize s);
CreatureSize creatureSizeFromString(const char* s);

// ============================================================================
// MonsterAttack — one action available to a monster
// ============================================================================
struct MonsterAttack {
    std::string name;           // "Shortsword", "Bite", "Fire Breath"
    bool        isWeaponAttack  = true;  // false = special/breath/etc.
    int         toHitBonus      = 0;     // total attack modifier (for weapon attacks)
    std::string damageDice;              // "1d6+2" — parsed by DiceExpression
    std::string damageType;              // "piercing", "fire", "bludgeoning" …
    int         reach           = 5;     // feet (melee) or 0 (ranged)
    bool        isRanged        = false;
    int         rangeNormal     = 0;     // feet (ranged weapon attacks)
    int         rangeLong       = 0;
    // For saving-throw attacks (breath, web, etc.)
    bool        requiresSave    = false;
    std::string saveAbility;             // "DEX", "CON" …
    int         saveDC          = 0;
    std::string effectOnFail;            // "Restrained", "Poisoned", or free-form text
    float       effectDuration  = -1.0f; // seconds; -1 = indefinite
    std::string description;

    nlohmann::json toJson() const;
    static MonsterAttack fromJson(const nlohmann::json& j);
};

// ============================================================================
// MonsterDefinition — full D&D 5e stat block
// ============================================================================
struct MonsterDefinition {
    // --- Identity ---
    std::string   id;           // "goblin", "troll", "young_red_dragon"
    std::string   name;         // "Goblin", "Troll", "Young Red Dragon"
    std::string   type;         // "humanoid", "undead", "beast", "dragon" …
    std::string   subtype;      // "(goblinoid)", "(any race)", "" …
    CreatureSize  size = CreatureSize::Medium;
    std::string   alignment;    // "neutral evil", "chaotic evil", "unaligned" …

    // --- Combat stats ---
    int           armorClass    = 10;
    std::string   armorSource;  // "natural armor", "leather armor + shield", …
    std::string   hitPointDice; // "2d6" → average = 7 (Goblin)
    int           averageHP     = 0;
    int           speed         = 30;  // feet; overrides for fly/swim can be added later

    // --- Ability scores ---
    CharacterAttributes attributes; // STR/DEX/CON/INT/WIS/CHA

    // --- Saving throw proficiencies (add proficiency bonus on top of ability mod) ---
    std::vector<AbilityType> savingThrowProficiencies;

    // --- Skill proficiencies ---
    std::unordered_map<std::string, ProficiencyLevel> skillProficiencies;

    // --- Defenses ---
    std::vector<std::string> damageResistances;   // e.g. "bludgeoning", "fire"
    std::vector<std::string> damageImmunities;    // e.g. "poison"
    std::vector<std::string> conditionImmunities; // e.g. "poisoned", "exhaustion"

    // --- Senses ---
    int darkvisionRange    = 0;   // feet (0 = none)
    int blindsightRange    = 0;
    int truesightRange     = 0;
    int passivePerception  = 10;

    // --- Languages ---
    std::vector<std::string> languages;

    // --- Challenge ---
    float challengeRating  = 0.0f; // 0.125 = 1/8, 0.25 = 1/4, 0.5 = 1/2, 1..30
    int   xpValue          = 0;

    // --- Actions ---
    std::vector<MonsterAttack> attacks;

    // --- Special traits (free-form text for now; later can be structured) ---
    // e.g. {"Aggressive": "...", "Undead Fortitude": "..."}
    std::vector<std::pair<std::string, std::string>> traits;

    // --- Tags for filtering ---
    std::vector<std::string> tags; // "goblinoid", "undead", "fiend", etc.

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    /// Proficiency bonus derived from CR (D&D 5e table).
    int proficiencyBonus() const;

    /// Roll HP from hitPointDice (uses the global DiceSystem RNG).
    int rollHP() const;

    /// Shorthand: is this monster immune to the given condition string?
    bool isImmuneTo(const std::string& conditionStr) const;

    nlohmann::json toJson() const;
    static MonsterDefinition fromJson(const nlohmann::json& j);
};

// ============================================================================
// MonsterRegistry — singleton, loaded from resources/monsters/
// ============================================================================
class MonsterRegistry {
public:
    static MonsterRegistry& instance();

    bool   registerMonster(const MonsterDefinition& def);
    const  MonsterDefinition* getMonster(const std::string& id) const;
    bool   hasMonster(const std::string& id) const;
    size_t count() const { return m_monsters.size(); }

    std::vector<std::string>              getAllIds() const;
    std::vector<const MonsterDefinition*> getByTag(const std::string& tag) const;
    std::vector<const MonsterDefinition*> getByCR(float minCR, float maxCR) const;
    std::vector<const MonsterDefinition*> getByType(const std::string& type) const;

    /// Load all *.json files from a directory. Returns count loaded.
    int  loadFromDirectory(const std::string& dirPath);
    bool loadFromFile(const std::string& filepath);
    /// Load from a JSON object (single monster) or array.
    int  loadFromJson(const nlohmann::json& j);

    void clear();

private:
    MonsterRegistry() = default;
    MonsterRegistry(const MonsterRegistry&) = delete;
    MonsterRegistry& operator=(const MonsterRegistry&) = delete;

    std::unordered_map<std::string, MonsterDefinition> m_monsters;
};

} // namespace Core
} // namespace Phyxel
