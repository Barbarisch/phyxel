#include "core/DialogueSkillCheck.h"
#include <string>
#include <sstream>

namespace Phyxel::Core {

// ---------------------------------------------------------------------------
// Name helpers
// ---------------------------------------------------------------------------

const char* dialogueCheckTypeName(DialogueCheckType type) {
    switch (type) {
        case DialogueCheckType::SkillCheck:    return "SkillCheck";
        case DialogueCheckType::AbilityCheck:  return "AbilityCheck";
        case DialogueCheckType::ReputationGate: return "ReputationGate";
    }
    return "SkillCheck";
}

DialogueCheckType dialogueCheckTypeFromString(const char* name) {
    if (!name) return DialogueCheckType::SkillCheck;
    auto eq = [&](const char* s) { return _stricmp(name, s) == 0; };
    if (eq("SkillCheck"))     return DialogueCheckType::SkillCheck;
    if (eq("AbilityCheck"))   return DialogueCheckType::AbilityCheck;
    if (eq("ReputationGate")) return DialogueCheckType::ReputationGate;
    return DialogueCheckType::SkillCheck;
}

// ---------------------------------------------------------------------------
// DialogueSkillCheck::resolve
// ---------------------------------------------------------------------------

DialogueSkillCheck::RollResult DialogueSkillCheck::resolve(int bonus, DiceSystem& dice) const {
    auto rollResult = dice.roll(DieType::D20);
    int total = rollResult.total + bonus;
    return { total >= dc, rollResult.total, bonus, total, dc };
}

bool DialogueSkillCheck::resolveReputation(int reputationScore) const {
    return ReputationSystem::tierForScore(reputationScore) >= minimumTier;
}

// ---------------------------------------------------------------------------
// label
// ---------------------------------------------------------------------------

std::string DialogueSkillCheck::label() const {
    if (!description.empty()) return description;

    std::ostringstream oss;
    switch (type) {
        case DialogueCheckType::SkillCheck:
            oss << "[" << ProficiencySystem::skillName(skill) << " DC " << dc << "]";
            break;
        case DialogueCheckType::AbilityCheck:
            oss << "[" << abilityShortName(ability) << " DC " << dc << "]";
            break;
        case DialogueCheckType::ReputationGate:
            oss << "[" << reputationTierName(minimumTier) << ": " << factionId << "]";
            break;
    }
    return oss.str();
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

nlohmann::json DialogueSkillCheck::toJson() const {
    nlohmann::json j;
    j["type"]        = dialogueCheckTypeName(type);
    j["dc"]          = dc;
    j["showOnFail"]  = showOnFail;
    j["description"] = description;

    switch (type) {
        case DialogueCheckType::SkillCheck:
            j["skill"] = ProficiencySystem::skillName(skill);
            break;
        case DialogueCheckType::AbilityCheck:
            j["ability"] = abilityShortName(ability);
            break;
        case DialogueCheckType::ReputationGate:
            j["factionId"]   = factionId;
            j["minimumTier"] = reputationTierName(minimumTier);
            break;
    }
    return j;
}

DialogueSkillCheck DialogueSkillCheck::fromJson(const nlohmann::json& j) {
    DialogueSkillCheck c;
    c.type        = dialogueCheckTypeFromString(j.value("type", "SkillCheck").c_str());
    c.dc          = j.value("dc", 15);
    c.showOnFail  = j.value("showOnFail", true);
    c.description = j.value("description", "");

    switch (c.type) {
        case DialogueCheckType::SkillCheck:
            c.skill = ProficiencySystem::skillFromString(j.value("skill", "Persuasion").c_str());
            break;
        case DialogueCheckType::AbilityCheck:
            c.ability = abilityFromString(j.value("ability", "CHA").c_str());
            break;
        case DialogueCheckType::ReputationGate:
            c.factionId   = j.value("factionId", "");
            c.minimumTier = reputationTierFromString(j.value("minimumTier", "Friendly").c_str());
            break;
    }
    return c;
}

} // namespace Phyxel::Core
