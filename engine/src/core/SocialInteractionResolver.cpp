#include "core/SocialInteractionResolver.h"
#include <sstream>

namespace Phyxel::Core {

// ---------------------------------------------------------------------------
// Name helpers
// ---------------------------------------------------------------------------

const char* socialSkillName(SocialSkill skill) {
    switch (skill) {
        case SocialSkill::Persuasion:   return "Persuasion";
        case SocialSkill::Deception:    return "Deception";
        case SocialSkill::Intimidation: return "Intimidation";
        case SocialSkill::Insight:      return "Insight";
        case SocialSkill::Performance:  return "Performance";
    }
    return "Persuasion";
}

SocialSkill socialSkillFromString(const char* name) {
    if (!name) return SocialSkill::Persuasion;
    auto eq = [&](const char* s) { return _stricmp(name, s) == 0; };
    if (eq("Persuasion"))   return SocialSkill::Persuasion;
    if (eq("Deception"))    return SocialSkill::Deception;
    if (eq("Intimidation")) return SocialSkill::Intimidation;
    if (eq("Insight"))      return SocialSkill::Insight;
    if (eq("Performance"))  return SocialSkill::Performance;
    return SocialSkill::Persuasion;
}

// ---------------------------------------------------------------------------
// SocialInteractionResolver::resolve
// ---------------------------------------------------------------------------

SocialResult SocialInteractionResolver::resolve(
    SocialSkill skill,
    int         skillBonus,
    int         dc,
    bool        hasAdvantage,
    bool        hasDisadvantage,
    DiceSystem& dice)
{
    RollResult rollResult;
    if (hasAdvantage && !hasDisadvantage)
        rollResult = dice.rollAdvantage(DieType::D20);
    else if (hasDisadvantage && !hasAdvantage)
        rollResult = dice.rollDisadvantage(DieType::D20);
    else
        rollResult = dice.roll(DieType::D20);

    int total   = rollResult.total + skillBonus;
    bool success = total >= dc;

    std::ostringstream oss;
    oss << socialSkillName(skill) << ": " << total << " vs DC " << dc
        << " — " << (success ? "Success!" : "Failure.");

    return { success, rollResult.total, total, dc, skill, oss.str() };
}

// ---------------------------------------------------------------------------
// SocialInteractionResolver::resolveInsight
// ---------------------------------------------------------------------------

SocialInteractionResolver::InsightResult SocialInteractionResolver::resolveInsight(
    int insightBonus, int deceptionBonus, DiceSystem& dice)
{
    auto pr = dice.roll(DieType::D20);
    auto nr = dice.roll(DieType::D20);
    int playerTotal = pr.total + insightBonus;
    int npcTotal    = nr.total + deceptionBonus;
    return { playerTotal > npcTotal, pr.total, playerTotal, nr.total, npcTotal };
}

// ---------------------------------------------------------------------------
// DC tables
// ---------------------------------------------------------------------------

int SocialInteractionResolver::persuasionDC(ReputationTier npcTier) {
    switch (npcTier) {
        case ReputationTier::Exalted:    return 5;
        case ReputationTier::Honored:    return 10;
        case ReputationTier::Friendly:   return 12;
        case ReputationTier::Neutral:    return 15;
        case ReputationTier::Unfriendly: return 20;
        case ReputationTier::Hostile:    return 25;
    }
    return 15;
}

int SocialInteractionResolver::intimidationDC(ReputationTier npcTier) {
    switch (npcTier) {
        case ReputationTier::Exalted:    return 5;
        case ReputationTier::Honored:    return 10;
        case ReputationTier::Friendly:   return 15;
        case ReputationTier::Neutral:    return 15;
        case ReputationTier::Unfriendly: return 18;
        case ReputationTier::Hostile:    return 22;
    }
    return 15;
}

// ---------------------------------------------------------------------------
// Reputation deltas
// ---------------------------------------------------------------------------

int SocialInteractionResolver::reputationDelta(SocialSkill skill, bool succeeded) {
    switch (skill) {
        case SocialSkill::Persuasion:
            return succeeded ? 25 : -10;
        case SocialSkill::Deception:
            // Success grants short-term cooperation but long-term distrust
            return succeeded ? 0 : -15;
        case SocialSkill::Intimidation:
            // Intimidation always costs goodwill; success costs less
            return succeeded ? -15 : -30;
        case SocialSkill::Performance:
            return succeeded ? 20 : 5;
        case SocialSkill::Insight:
            return 0;  // Insight doesn't change reputation
    }
    return 0;
}

} // namespace Phyxel::Core
