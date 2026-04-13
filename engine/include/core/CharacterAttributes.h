#pragma once

#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

/// The six D&D ability types.
enum class AbilityType {
    Strength,
    Dexterity,
    Constitution,
    Intelligence,
    Wisdom,
    Charisma
};

/// Converts an AbilityType to its short name ("STR", "DEX", etc.).
const char* abilityShortName(AbilityType ability);

/// Converts an AbilityType to its full name ("Strength", "Dexterity", etc.).
const char* abilityFullName(AbilityType ability);

/// Parses "STR"/"Strength" etc. to AbilityType. Throws on unknown string.
AbilityType abilityFromString(const char* s);

/// A single ability score, tracking all the ways it can be modified.
///
/// D&D layering:
///   base      — the rolled/point-buy score
///   racial     — bonus from race (e.g. +2 STR for Mountain Dwarf)
///   equipment  — bonus from magic items currently equipped
///   temporary  — from spells / effects (Bull's Strength, Bane, etc.)
///
/// Total capped at 30 (absolute max in D&D, achievable only via Wish / divine intervention).
struct AbilityScore {
    int base      = 10;
    int racial    = 0;
    int equipment = 0;
    int temporary = 0;

    /// Sum of all layers, clamped [1, 30].
    int total() const;

    /// Standard D&D modifier: floor((total - 10) / 2).
    /// Score 10-11 → +0, 12-13 → +1, 8-9 → -1, etc.
    int modifier() const;

    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);
};

/// The six D&D ability scores for a character (player or NPC).
///
/// Derived stats (AC, initiative, carry capacity, etc.) are computed here
/// so that anything holding a CharacterAttributes can answer all stat queries
/// without knowing about class, equipment, or proficiency.
struct CharacterAttributes {
    AbilityScore strength;
    AbilityScore dexterity;
    AbilityScore constitution;
    AbilityScore intelligence;
    AbilityScore wisdom;
    AbilityScore charisma;

    // --- Accessors ---

    const AbilityScore& get(AbilityType type) const;
    AbilityScore& get(AbilityType type);

    int modifier(AbilityType type) const { return get(type).modifier(); }
    int score(AbilityType type) const    { return get(type).total(); }

    // --- Derived stats (no proficiency needed) ---

    /// Base initiative bonus = DEX modifier.
    int initiativeBonus() const { return dexterity.modifier(); }

    /// Unarmored AC = 10 + DEX modifier.
    int unarmoredAC() const { return 10 + dexterity.modifier(); }

    /// Carrying capacity in pounds = STR score × 15.
    int carryCapacity() const { return strength.total() * 15; }

    /// Push / drag / lift limit = STR score × 30.
    int pushDragLift() const { return strength.total() * 30; }

    // --- Point-buy helpers ---

    /// Standard array: [15, 14, 13, 12, 10, 8] assigned in order STR→CHA.
    void applyStandardArray(int str, int dex, int con, int intel, int wis, int cha);

    /// Roll 4d6 drop-lowest for each ability and assign scores.
    /// Requires DiceSystem — declared separately to avoid circular include;
    /// call CharacterAttributes::rollAbilityScores(attrs) from CharacterProgression.
    void setAll(int str, int dex, int con, int intel, int wis, int cha);

    // --- Serialization ---

    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);
};

} // namespace Core
} // namespace Phyxel
