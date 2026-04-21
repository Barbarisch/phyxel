#pragma once

#include "core/CharacterAttributes.h"
#include "core/ProficiencySystem.h"

#include <string>
#include <vector>
#include <set>
#include <optional>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

/// One entry in a character's multiclass list.
struct ClassLevel {
    std::string classId;    // "fighter", "wizard"
    int level = 1;
    std::string subclassId; // "champion", "evocation" (empty until subclass chosen)
};

/// Per-class pool of hit dice (spent on short rests to recover HP).
struct HitDicePool {
    std::string classId;
    int faces    = 8;    // die faces: 6, 8, 10, 12
    int total    = 0;    // total dice for this class (= class level)
    int remaining = 0;   // dice available to spend on short rest

    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);
};

/// Death save state — tracked while a creature is at 0 HP.
/// Three successes → stable. Three failures → dead. Nat-1 → two failures. Nat-20 → 1 HP.
struct DeathSaveState {
    int successes = 0;
    int failures  = 0;
    bool stable   = false;

    bool isDead()   const { return failures >= 3; }
    bool isStable() const { return successes >= 3 || stable; }
    void reset() { successes = failures = 0; stable = false; }

    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);
};

/// The complete D&D character stat block for a player or NPC.
///
/// CharacterSheet is a pure data object — it holds no update() loop, no rendering,
/// and no entity references. CharacterProgression mutates it; NPCEntity/player
/// embed it as an optional component.
struct CharacterSheet {
    // --- Identity ---
    std::string characterId;
    std::string name;
    std::string raceId;             // key into RaceRegistry
    std::vector<ClassLevel> classes;

    // --- Core stats ---
    CharacterAttributes attributes;
    int maxHP        = 0;
    int currentHP    = 0;
    int temporaryHP  = 0;   // absorbed before currentHP
    int armorClass   = 10;  // recalculated via recalculate()
    int speed        = 30;  // feet per turn (from race, modified by effects)
    int initiative   = 0;   // DEX mod + bonuses, updated by recalculate()

    // --- Skills & saves ---
    std::vector<SkillEntry>     skills;
    std::set<AbilityType>       savingThrowProficiencies;

    // --- Proficiency strings ---
    std::set<std::string> armorProficiencies;   // "light", "medium", "heavy", "shields"
    std::set<std::string> weaponProficiencies;  // "simple", "martial", or specific ids
    std::set<std::string> toolProficiencies;
    std::vector<std::string>    languages;

    // --- Progression ---
    int  experiencePoints = 0;
    std::vector<HitDicePool> hitDicePools;  // one entry per class
    int  availableASIs    = 0;              // unspent ASI/feat choices
    std::vector<std::string> featIds;
    std::vector<std::string> earnedFeatureIds;  // all class features gained so far

    // --- Death saves ---
    DeathSaveState deathSaves;

    // -----------------------------------------------------------------------
    // Computed queries (read-only, call recalculate() after mutating attrs)
    // -----------------------------------------------------------------------

    int totalLevel() const;
    int proficiencyBonus() const;

    /// Bonus for a skill check (ability mod + proficiency component).
    int skillBonus(Skill skill) const;

    /// Bonus for a saving throw.
    int savingThrowBonus(AbilityType ability) const;

    /// Passive Perception = 10 + Perception bonus.
    int passivePerception() const;

    ProficiencyLevel skillProficiencyLevel(Skill skill) const;
    bool isProficientWith(Skill skill) const;
    bool hasSavingThrowProficiency(AbilityType ability) const;

    bool isAlive() const { return currentHP > 0; }

    // -----------------------------------------------------------------------
    // HP helpers
    // -----------------------------------------------------------------------

    /// Apply damage: burns temporaryHP first, then currentHP.
    /// Returns actual damage dealt to currentHP (after temp absorption).
    int takeDamage(int amount);

    /// Recover HP up to maxHP. Returns amount actually healed.
    int heal(int amount);

    /// Grant temporary HP (replaces existing if higher, never stacks).
    void grantTemporaryHP(int amount);

    // -----------------------------------------------------------------------
    // Recalculate derived fields
    // -----------------------------------------------------------------------

    /// Recompute initiative and unarmored AC from current attributes.
    /// Call this after changing ability scores or equipment.
    void recalculate();

    // -----------------------------------------------------------------------
    // Serialization
    // -----------------------------------------------------------------------
    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);

    // -----------------------------------------------------------------------
    // Factory helpers
    // -----------------------------------------------------------------------

    /// Build a fresh level-1 character. Applies race bonuses and class defaults.
    /// Requires ClassRegistry and RaceRegistry to be loaded.
    static CharacterSheet create(const std::string& name,
                                  const std::string& raceId,
                                  const std::string& classId);
};

} // namespace Core
} // namespace Phyxel
