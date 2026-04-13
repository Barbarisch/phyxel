#include "core/ProficiencySystem.h"

#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <sstream>

namespace Phyxel {
namespace Core {

// ---------------------------------------------------------------------------
// Static data
// ---------------------------------------------------------------------------

const Skill ProficiencySystem::ALL_SKILLS[18] = {
    Skill::Athletics,
    Skill::Acrobatics,
    Skill::SleightOfHand,
    Skill::Stealth,
    Skill::Arcana,
    Skill::History,
    Skill::Investigation,
    Skill::Nature,
    Skill::Religion,
    Skill::AnimalHandling,
    Skill::Insight,
    Skill::Medicine,
    Skill::Perception,
    Skill::Survival,
    Skill::Deception,
    Skill::Intimidation,
    Skill::Performance,
    Skill::Persuasion,
};

// ---------------------------------------------------------------------------
// Proficiency bonus
// ---------------------------------------------------------------------------

int ProficiencySystem::proficiencyBonus(int totalLevel) {
    // PHB table: ceil(level/4) + 1, clamped to range [2,6]
    totalLevel = std::clamp(totalLevel, 1, 20);
    return ((totalLevel - 1) / 4) + 2;
}

// ---------------------------------------------------------------------------
// Skill bonus
// ---------------------------------------------------------------------------

int ProficiencySystem::skillBonus(
    const CharacterAttributes& attrs,
    Skill skill,
    ProficiencyLevel prof,
    int totalLevel)
{
    int abilityMod = attrs.modifier(abilityForSkill(skill));
    int pb = proficiencyBonus(totalLevel);

    switch (prof) {
        case ProficiencyLevel::None:       return abilityMod;
        case ProficiencyLevel::HalfProf:   return abilityMod + pb / 2;
        case ProficiencyLevel::Proficient: return abilityMod + pb;
        case ProficiencyLevel::Expert:     return abilityMod + pb * 2;
        default:                           return abilityMod;
    }
}

int ProficiencySystem::passiveCheck(int activeBonus) {
    return 10 + activeBonus;
}

// ---------------------------------------------------------------------------
// Saving throw bonus
// ---------------------------------------------------------------------------

int ProficiencySystem::savingThrowBonus(
    const CharacterAttributes& attrs,
    AbilityType ability,
    bool proficient,
    int totalLevel)
{
    int abilityMod = attrs.modifier(ability);
    if (proficient) {
        return abilityMod + proficiencyBonus(totalLevel);
    }
    return abilityMod;
}

// ---------------------------------------------------------------------------
// Ability mapping
// ---------------------------------------------------------------------------

AbilityType ProficiencySystem::abilityForSkill(Skill skill) {
    switch (skill) {
        case Skill::Athletics:                         return AbilityType::Strength;
        case Skill::Acrobatics:
        case Skill::SleightOfHand:
        case Skill::Stealth:                           return AbilityType::Dexterity;
        case Skill::Arcana:
        case Skill::History:
        case Skill::Investigation:
        case Skill::Nature:
        case Skill::Religion:                          return AbilityType::Intelligence;
        case Skill::AnimalHandling:
        case Skill::Insight:
        case Skill::Medicine:
        case Skill::Perception:
        case Skill::Survival:                          return AbilityType::Wisdom;
        case Skill::Deception:
        case Skill::Intimidation:
        case Skill::Performance:
        case Skill::Persuasion:                        return AbilityType::Charisma;
        default:
            throw std::invalid_argument("Unknown skill in abilityForSkill");
    }
}

// ---------------------------------------------------------------------------
// Name helpers
// ---------------------------------------------------------------------------

const char* ProficiencySystem::skillName(Skill skill) {
    switch (skill) {
        case Skill::Athletics:     return "Athletics";
        case Skill::Acrobatics:    return "Acrobatics";
        case Skill::SleightOfHand: return "Sleight of Hand";
        case Skill::Stealth:       return "Stealth";
        case Skill::Arcana:        return "Arcana";
        case Skill::History:       return "History";
        case Skill::Investigation: return "Investigation";
        case Skill::Nature:        return "Nature";
        case Skill::Religion:      return "Religion";
        case Skill::AnimalHandling:return "Animal Handling";
        case Skill::Insight:       return "Insight";
        case Skill::Medicine:      return "Medicine";
        case Skill::Perception:    return "Perception";
        case Skill::Survival:      return "Survival";
        case Skill::Deception:     return "Deception";
        case Skill::Intimidation:  return "Intimidation";
        case Skill::Performance:   return "Performance";
        case Skill::Persuasion:    return "Persuasion";
        default:                   return "Unknown";
    }
}

Skill ProficiencySystem::skillFromString(const char* s) {
    if (!s) throw std::invalid_argument("null skill string");

    auto eq = [&](const char* cmp) {
        // Case-insensitive, ignores spaces
        size_t si = 0, ci = 0;
        size_t slen = strlen(s), clen = strlen(cmp);
        while (si < slen && ci < clen) {
            char sc = s[si];
            char cc = cmp[ci];
            if (sc == ' ') { ++si; continue; }
            if (cc == ' ') { ++ci; continue; }
            if (std::tolower(sc) != std::tolower(cc)) return false;
            ++si; ++ci;
        }
        // skip trailing spaces
        while (si < slen && s[si] == ' ') ++si;
        while (ci < clen && cmp[ci] == ' ') ++ci;
        return si == slen && ci == clen;
    };

    if (eq("Athletics"))      return Skill::Athletics;
    if (eq("Acrobatics"))     return Skill::Acrobatics;
    if (eq("SleightOfHand") || eq("Sleight of Hand")) return Skill::SleightOfHand;
    if (eq("Stealth"))        return Skill::Stealth;
    if (eq("Arcana"))         return Skill::Arcana;
    if (eq("History"))        return Skill::History;
    if (eq("Investigation"))  return Skill::Investigation;
    if (eq("Nature"))         return Skill::Nature;
    if (eq("Religion"))       return Skill::Religion;
    if (eq("AnimalHandling") || eq("Animal Handling")) return Skill::AnimalHandling;
    if (eq("Insight"))        return Skill::Insight;
    if (eq("Medicine"))       return Skill::Medicine;
    if (eq("Perception"))     return Skill::Perception;
    if (eq("Survival"))       return Skill::Survival;
    if (eq("Deception"))      return Skill::Deception;
    if (eq("Intimidation"))   return Skill::Intimidation;
    if (eq("Performance"))    return Skill::Performance;
    if (eq("Persuasion"))     return Skill::Persuasion;

    throw std::invalid_argument(std::string("Unknown skill: ") + s);
}

std::string ProficiencySystem::bonusString(int bonus) {
    if (bonus >= 0) return "+" + std::to_string(bonus);
    return std::to_string(bonus);
}

} // namespace Core
} // namespace Phyxel
