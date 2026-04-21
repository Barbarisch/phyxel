#pragma once

#include "core/CharacterAttributes.h"
#include <nlohmann/json.hpp>
#include <string>

namespace Phyxel {
namespace Core {

/// The 18 D&D skills. Each maps to a governing ability score.
enum class Skill {
    // Strength
    Athletics,
    // Dexterity
    Acrobatics,
    SleightOfHand,
    Stealth,
    // Intelligence
    Arcana,
    History,
    Investigation,
    Nature,
    Religion,
    // Wisdom
    AnimalHandling,
    Insight,
    Medicine,
    Perception,
    Survival,
    // Charisma
    Deception,
    Intimidation,
    Performance,
    Persuasion,

    COUNT  // sentinel — do not use as a skill
};

/// How proficient a character is with a skill.
/// Expert doubles the proficiency bonus (Rogue, Bard).
enum class ProficiencyLevel {
    None       = 0,   // just the raw ability modifier
    HalfProf   = 1,   // +floor(profBonus/2) — Jack of All Trades (Bard)
    Proficient = 2,   // +profBonus
    Expert     = 4    // +profBonus×2
};

/// One entry in a character's skill list.
struct SkillEntry {
    Skill skill;
    ProficiencyLevel level = ProficiencyLevel::None;
};

/// D&D proficiency helpers.
///
/// Proficiency bonus scales with total character level (not per-class):
///   Levels  1-4  → +2
///   Levels  5-8  → +3
///   Levels  9-12 → +4
///   Levels 13-16 → +5
///   Levels 17+   → +6
class ProficiencySystem {
public:
    // --- Proficiency bonus ---

    /// Returns the proficiency bonus for a given total character level (1-20).
    static int proficiencyBonus(int totalLevel);

    // --- Skill checks ---

    /// Total bonus for a skill check: abilityMod + proficiency component.
    static int skillBonus(
        const CharacterAttributes& attrs,
        Skill skill,
        ProficiencyLevel prof,
        int totalLevel);

    /// Passive check value = 10 + activeBonus.
    /// Used for passive Perception, Insight, Investigation.
    static int passiveCheck(int activeBonus);

    // --- Saving throws ---

    /// Total bonus for a saving throw.
    /// If proficient, adds the full proficiency bonus.
    static int savingThrowBonus(
        const CharacterAttributes& attrs,
        AbilityType ability,
        bool proficient,
        int totalLevel);

    // --- Attribute mapping ---

    /// Which ability governs a given skill (e.g. Perception → Wisdom).
    static AbilityType abilityForSkill(Skill skill);

    // --- Name helpers ---

    static const char* skillName(Skill skill);
    static Skill skillFromString(const char* s);  // throws on unknown

    // --- Lookup helpers (for UI / MCP) ---

    /// All 18 skills in order.
    static const Skill ALL_SKILLS[18];

    /// Returns the bonus as a display string, e.g. "+5" or "-1".
    static std::string bonusString(int bonus);
};

} // namespace Core
} // namespace Phyxel
