#pragma once

#include "core/DiceSystem.h"
#include "core/SpellcasterComponent.h"
#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace Phyxel::Core {

// ---------------------------------------------------------------------------
// ShortRestResult
// ---------------------------------------------------------------------------

struct ShortRestResult {
    bool        completed   = false;
    std::string failReason;
    int         hpRecovered  = 0;
    int         hitDiceSpent = 0;
    int         hitDiceRemaining = 0;
};

// ---------------------------------------------------------------------------
// LongRestResult
// ---------------------------------------------------------------------------

struct LongRestResult {
    bool        completed       = false;
    std::string failReason;
    int         hpRecovered     = 0;  ///< HP gained (0 if already full)
    int         hitDiceRestored = 0;  ///< Hit dice returned (up to half of max)
    bool        spellSlotsRestored = false;
};

// ---------------------------------------------------------------------------
// RestSystem
// ---------------------------------------------------------------------------

/// Manages short and long rest mechanics for registered characters.
///
/// Short rest (1 hour in-world):
///   Spend N hit dice. Each die rolls hitDieType + CON modifier → add to HP.
///   Pact magic slots (warlocks) restore on short rest.
///
/// Long rest (8 hours in-world):
///   Restore HP to maximum. Restore half of max hit dice (min 1).
///   All spell slots restored via SpellcasterComponent::onLongRest().
///
/// Call registerCharacter() once per entity on creation.
class RestSystem {
public:
    // -----------------------------------------------------------------------
    // Character registration
    // -----------------------------------------------------------------------

    struct CharacterRestState {
        int     currentHp     = 0;
        int     maxHp         = 0;
        int     currentHitDice = 0;
        int     maxHitDice    = 0;
        DieType hitDieType    = DieType::D8;
        int     characterLevel = 1;
        std::string castingType;   ///< "full"|"half"|"third"|"pact"|"" (empty = no magic)
    };

    void registerCharacter(
        const std::string& entityId,
        int maxHp,
        int hitDiceCount,
        DieType hitDieType,
        int characterLevel,
        const std::string& castingType = "");

    bool isRegistered(const std::string& entityId) const;

    // HP management
    void setCurrentHp(const std::string& entityId, int hp);
    int  getCurrentHp(const std::string& entityId) const;     ///< 0 if not registered
    int  getMaxHp(const std::string& entityId) const;

    // Hit dice management
    void setCurrentHitDice(const std::string& entityId, int count);
    int  getCurrentHitDice(const std::string& entityId) const;
    int  getMaxHitDice(const std::string& entityId) const;

    // -----------------------------------------------------------------------
    // Rest actions
    // -----------------------------------------------------------------------

    /// Short rest: spend N hit dice, rolling each for HP recovery.
    /// @param entityId          Character taking the rest.
    /// @param hitDiceToSpend    Number of hit dice to spend (>= 0).
    /// @param constitutionMod   CON modifier added to each die roll.
    /// @param caster            Optional — warlock pact slots restored.
    /// @param dice              Dice system for rolls.
    ShortRestResult shortRest(
        const std::string& entityId,
        int hitDiceToSpend,
        int constitutionMod,
        DiceSystem& dice,
        SpellcasterComponent* caster = nullptr);

    /// Long rest: full HP, half hit dice back, all spell slots restored.
    /// @param caster  Optional — calls caster->onLongRest() if provided.
    LongRestResult longRest(
        const std::string& entityId,
        SpellcasterComponent* caster = nullptr);

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    void removeCharacter(const std::string& entityId);
    void clear();

    // -----------------------------------------------------------------------
    // Serialization (HP and hit dice state only — not spell slots)
    // -----------------------------------------------------------------------

    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);

private:
    std::unordered_map<std::string, CharacterRestState> m_characters;
};

} // namespace Phyxel::Core
