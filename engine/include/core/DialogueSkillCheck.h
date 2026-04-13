#pragma once

#include "core/ProficiencySystem.h"
#include "core/ReputationSystem.h"
#include "core/DiceSystem.h"
#include <string>
#include <nlohmann/json.hpp>

namespace Phyxel::Core {

// ---------------------------------------------------------------------------
// DialogueCheckType
// ---------------------------------------------------------------------------

enum class DialogueCheckType {
    SkillCheck,     ///< D&D skill roll vs DC (Persuasion, Deception, etc.)
    AbilityCheck,   ///< Raw ability score roll vs DC (STR, DEX, etc.)
    ReputationGate  ///< Requires minimum reputation tier (no roll — pass/fail)
};

const char*         dialogueCheckTypeName(DialogueCheckType type);
DialogueCheckType   dialogueCheckTypeFromString(const char* name);

// ---------------------------------------------------------------------------
// DialogueSkillCheck
// ---------------------------------------------------------------------------

/// A skill-check gate that can be attached to a dialogue choice.
/// Three variants:
///   SkillCheck — roll 1d20 + skillBonus vs DC
///   AbilityCheck — roll 1d20 + abilityModifier vs DC
///   ReputationGate — pass if reputation score meets minimum tier (no dice roll)
struct DialogueSkillCheck {
    DialogueCheckType type = DialogueCheckType::SkillCheck;

    // SkillCheck fields
    Skill skill = Skill::Persuasion;

    // AbilityCheck fields
    AbilityType ability = AbilityType::Charisma;

    // ReputationGate fields
    std::string    factionId;
    ReputationTier minimumTier = ReputationTier::Friendly;

    // Common
    int  dc          = 15;
    bool showOnFail  = true;   ///< Show the option even when player cannot satisfy it
    std::string description;   ///< Override label (e.g. "[Persuasion DC 15]"); auto-generated if empty

    // -----------------------------------------------------------------------
    // Result of a roll-based check
    // -----------------------------------------------------------------------
    struct RollResult {
        bool passed;
        int  roll;   ///< d20 result
        int  bonus;  ///< modifier applied
        int  total;  ///< roll + bonus
        int  dc;
    };

    /// Resolve a SkillCheck or AbilityCheck.
    /// @param bonus  Pre-computed skill/ability bonus (caller handles proficiency).
    /// @param dice   DiceSystem to use for the roll.
    RollResult resolve(int bonus, DiceSystem& dice) const;

    /// Resolve a ReputationGate (no dice — just compare score to tier threshold).
    bool resolveReputation(int reputationScore) const;

    /// Human-readable label: "[Persuasion DC 15]" / "[INT DC 12]" / "[Friendly: Merchants Guild]"
    std::string label() const;

    nlohmann::json toJson() const;
    static DialogueSkillCheck fromJson(const nlohmann::json& j);
};

} // namespace Phyxel::Core
