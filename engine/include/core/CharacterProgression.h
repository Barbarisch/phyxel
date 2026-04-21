#pragma once

#include "core/CharacterSheet.h"
#include "core/DiceSystem.h"

#include <optional>
#include <string>

namespace Phyxel {
namespace Core {

/// D&D character progression: XP, leveling, ASI, rests, hit dice.
///
/// All methods are static — CharacterProgression is a stateless rules engine
/// that mutates CharacterSheet objects. It reads ClassDefinition from
/// ClassRegistry internally.
class CharacterProgression {
public:
    // -----------------------------------------------------------------------
    // XP thresholds (PHB table, index = level 0–20; [0] unused)
    // -----------------------------------------------------------------------
    static const int XP_THRESHOLDS[21];

    // Encounter XP budgets per character (for encounter difficulty calculation)
    static const int ENCOUNTER_XP_EASY[20];
    static const int ENCOUNTER_XP_MEDIUM[20];
    static const int ENCOUNTER_XP_HARD[20];
    static const int ENCOUNTER_XP_DEADLY[20];

    // -----------------------------------------------------------------------
    // XP / level queries
    // -----------------------------------------------------------------------

    /// Character level that corresponds to this XP total (1–20).
    static int levelForXP(int xp);

    /// XP required to reach this level.
    static int xpForLevel(int level);

    /// XP gap between current total and next level threshold.
    static int xpToNextLevel(int currentXP);

    // -----------------------------------------------------------------------
    // Leveling
    // -----------------------------------------------------------------------

    struct LevelUpResult {
        bool success = false;
        std::string classId;
        int newClassLevel = 0;
        int newTotalLevel = 0;
        int hpGained     = 0;   // HP added to maxHP
        std::vector<std::string> featuresGained;  // feature ids granted this level
        bool grantsASI   = false;  // true → player must spend an ASI or feat
    };

    /// Level up a character in the given class.
    /// @param useAverageHP  true = take class average (no dice), false = roll hit die
    /// Requires ClassRegistry to be populated.
    static LevelUpResult levelUp(CharacterSheet& sheet,
                                  const std::string& classId,
                                  DiceSystem& dice,
                                  bool useAverageHP = false);

    // -----------------------------------------------------------------------
    // XP awarding
    // -----------------------------------------------------------------------

    /// Add XP to the sheet. If autoLevel=true and XP crosses a threshold,
    /// automatically calls levelUp() for the first class in the sheet.
    /// Returns true if a level-up occurred.
    static bool awardXP(CharacterSheet& sheet,
                         int xp,
                         DiceSystem& dice,
                         bool autoLevel = false,
                         bool useAverageHP = false);

    // -----------------------------------------------------------------------
    // Ability Score Improvements
    // -----------------------------------------------------------------------

    /// Spend one availableASI to increase ability scores.
    /// - If secondary is nullopt: +2 to primary (capped at 20).
    /// - Otherwise: +1 to primary and +1 to secondary (each capped at 20).
    /// Returns false if no ASI available or both scores already at 20.
    static bool applyASI(CharacterSheet& sheet,
                          AbilityType primary,
                          std::optional<AbilityType> secondary = std::nullopt);

    // -----------------------------------------------------------------------
    // Rests
    // -----------------------------------------------------------------------

    struct ShortRestResult {
        bool success     = false;
        int hpRecovered  = 0;
        int hitDiceSpent = 0;
    };

    struct LongRestResult {
        int hpRestored      = 0;
        int hitDiceRestored = 0;
    };

    /// Short rest: spend hit dice to recover HP.
    /// Tries to spend from each HitDicePool in order until hitDiceToSpend is exhausted.
    static ShortRestResult shortRest(CharacterSheet& sheet,
                                      int hitDiceToSpend,
                                      DiceSystem& dice);

    /// Long rest: restore full HP and recover up to half of total hit dice (min 1).
    static LongRestResult longRest(CharacterSheet& sheet);

    // -----------------------------------------------------------------------
    // Hit dice helpers
    // -----------------------------------------------------------------------

    static int totalHitDiceRemaining(const CharacterSheet& sheet);
    static int totalHitDiceMax(const CharacterSheet& sheet);
};

} // namespace Core
} // namespace Phyxel
