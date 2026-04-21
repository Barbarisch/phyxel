#pragma once

#include "core/DiceSystem.h"
#include "core/ReputationSystem.h"
#include <string>

namespace Phyxel::Core {

// ---------------------------------------------------------------------------
// SocialSkill
// ---------------------------------------------------------------------------

enum class SocialSkill {
    Persuasion,
    Deception,
    Intimidation,
    Insight,
    Performance
};

const char* socialSkillName(SocialSkill skill);
SocialSkill socialSkillFromString(const char* name);

// ---------------------------------------------------------------------------
// SocialResult
// ---------------------------------------------------------------------------

struct SocialResult {
    bool        success;
    int         roll;    ///< d20 result (0 for opposed checks — see resolveInsight)
    int         total;   ///< roll + bonus
    int         dc;      ///< DC that was targeted
    SocialSkill skill;
    std::string description;  ///< e.g. "Persuasion: 18 vs DC 15 — Success!"
};

// ---------------------------------------------------------------------------
// SocialInteractionResolver
// ---------------------------------------------------------------------------

class SocialInteractionResolver {
public:
    /// Standard social skill check against a fixed DC.
    static SocialResult resolve(
        SocialSkill skill,
        int         skillBonus,
        int         dc,
        bool        hasAdvantage,
        bool        hasDisadvantage,
        DiceSystem& dice
    );

    /// Insight (Wisdom) vs Deception (Charisma) opposed check.
    /// Returns true if the player's Insight beats the NPC's Deception.
    /// @param insightBonus   Player's Insight skill bonus.
    /// @param deceptionBonus NPC's Deception skill bonus.
    struct InsightResult {
        bool playerSucceeds;
        int  playerRoll, playerTotal;
        int  npcRoll,    npcTotal;
    };
    static InsightResult resolveInsight(int insightBonus, int deceptionBonus, DiceSystem& dice);

    /// Suggested DC for Persuasion based on NPC's current reputation tier.
    static int persuasionDC(ReputationTier npcTier);

    /// Suggested DC for Intimidation based on NPC's current reputation tier.
    static int intimidationDC(ReputationTier npcTier);

    /// Reputation gain/loss for a successful/failed social interaction.
    /// @param skill     Social skill used.
    /// @param succeeded Whether the check passed.
    /// @return Reputation delta to apply to the relevant faction.
    static int reputationDelta(SocialSkill skill, bool succeeded);
};

} // namespace Phyxel::Core
