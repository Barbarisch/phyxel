#include "core/ConditionSystem.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace Phyxel {
namespace Core {

// ---------------------------------------------------------------------------
// Name helpers
// ---------------------------------------------------------------------------

const char* conditionName(Condition c) {
    switch (c) {
        case Condition::Blinded:       return "Blinded";
        case Condition::Charmed:       return "Charmed";
        case Condition::Deafened:      return "Deafened";
        case Condition::Frightened:    return "Frightened";
        case Condition::Grappled:      return "Grappled";
        case Condition::Incapacitated: return "Incapacitated";
        case Condition::Invisible:     return "Invisible";
        case Condition::Paralyzed:     return "Paralyzed";
        case Condition::Petrified:     return "Petrified";
        case Condition::Poisoned:      return "Poisoned";
        case Condition::Prone:         return "Prone";
        case Condition::Restrained:    return "Restrained";
        case Condition::Stunned:       return "Stunned";
        case Condition::Unconscious:   return "Unconscious";
        case Condition::Exhausted:     return "Exhausted";
        default:                       return "Unknown";
    }
}

Condition conditionFromString(const char* s) {
    if (!s) throw std::invalid_argument("null condition string");
    auto eq = [&](const char* c) { return _stricmp(s, c) == 0; };
    if (eq("Blinded"))       return Condition::Blinded;
    if (eq("Charmed"))       return Condition::Charmed;
    if (eq("Deafened"))      return Condition::Deafened;
    if (eq("Frightened"))    return Condition::Frightened;
    if (eq("Grappled"))      return Condition::Grappled;
    if (eq("Incapacitated")) return Condition::Incapacitated;
    if (eq("Invisible"))     return Condition::Invisible;
    if (eq("Paralyzed"))     return Condition::Paralyzed;
    if (eq("Petrified"))     return Condition::Petrified;
    if (eq("Poisoned"))      return Condition::Poisoned;
    if (eq("Prone"))         return Condition::Prone;
    if (eq("Restrained"))    return Condition::Restrained;
    if (eq("Stunned"))       return Condition::Stunned;
    if (eq("Unconscious"))   return Condition::Unconscious;
    if (eq("Exhausted"))     return Condition::Exhausted;
    throw std::invalid_argument(std::string("Unknown condition: ") + s);
}

// ---------------------------------------------------------------------------
// ConditionInstance serialization
// ---------------------------------------------------------------------------

nlohmann::json ConditionInstance::toJson() const {
    return {
        {"type",              conditionName(type)},
        {"durationRemaining", durationRemaining},
        {"sourceEntityId",    sourceEntityId},
        {"sourceSpellId",     sourceSpellId},
        {"description",       description}
    };
}

ConditionInstance ConditionInstance::fromJson(const nlohmann::json& j) {
    ConditionInstance ci;
    ci.type              = conditionFromString(j.value("type","Blinded").c_str());
    ci.durationRemaining = j.value("durationRemaining", -1.0f);
    ci.sourceEntityId    = j.value("sourceEntityId", "");
    ci.sourceSpellId     = j.value("sourceSpellId", "");
    ci.description       = j.value("description", "");
    return ci;
}

// ---------------------------------------------------------------------------
// Apply / Remove
// ---------------------------------------------------------------------------

void ConditionSystem::applyCondition(const std::string& entityId, ConditionInstance instance) {
    m_conditions[entityId].push_back(std::move(instance));
}

void ConditionSystem::removeCondition(const std::string& entityId, Condition type) {
    auto it = m_conditions.find(entityId);
    if (it == m_conditions.end()) return;
    auto& vec = it->second;
    vec.erase(std::remove_if(vec.begin(), vec.end(),
        [type](const ConditionInstance& ci) { return ci.type == type; }), vec.end());
    if (vec.empty()) m_conditions.erase(it);
}

void ConditionSystem::removeAllFromSource(const std::string& entityId,
                                           const std::string& sourceEntityId) {
    auto it = m_conditions.find(entityId);
    if (it == m_conditions.end()) return;
    auto& vec = it->second;
    vec.erase(std::remove_if(vec.begin(), vec.end(),
        [&](const ConditionInstance& ci) { return ci.sourceEntityId == sourceEntityId; }), vec.end());
    if (vec.empty()) m_conditions.erase(it);
}

void ConditionSystem::clearAllConditions(const std::string& entityId) {
    m_conditions.erase(entityId);
}

// ---------------------------------------------------------------------------
// Query — presence
// ---------------------------------------------------------------------------

bool ConditionSystem::hasCondition(const std::string& entityId, Condition type) const {
    auto it = m_conditions.find(entityId);
    if (it == m_conditions.end()) return false;
    for (const auto& ci : it->second)
        if (ci.type == type) return true;
    return false;
}

std::vector<Condition> ConditionSystem::getConditions(const std::string& entityId) const {
    std::vector<Condition> result;
    auto it = m_conditions.find(entityId);
    if (it == m_conditions.end()) return result;
    for (const auto& ci : it->second) {
        if (std::find(result.begin(), result.end(), ci.type) == result.end())
            result.push_back(ci.type);
    }
    return result;
}

const std::vector<ConditionInstance>* ConditionSystem::getInstances(
    const std::string& entityId) const {
    auto it = m_conditions.find(entityId);
    return (it != m_conditions.end()) ? &it->second : nullptr;
}

// ---------------------------------------------------------------------------
// Query — derived mechanical states
// ---------------------------------------------------------------------------

bool ConditionSystem::isIncapacitated(const std::string& entityId) const {
    return hasCondition(entityId, Condition::Incapacitated)
        || hasCondition(entityId, Condition::Paralyzed)
        || hasCondition(entityId, Condition::Petrified)
        || hasCondition(entityId, Condition::Stunned)
        || hasCondition(entityId, Condition::Unconscious);
}

bool ConditionSystem::autoFailsSave(const std::string& entityId, AbilityType ability) const {
    if (ability == AbilityType::Strength || ability == AbilityType::Dexterity) {
        return hasCondition(entityId, Condition::Paralyzed)
            || hasCondition(entityId, Condition::Petrified)
            || hasCondition(entityId, Condition::Stunned)
            || hasCondition(entityId, Condition::Unconscious);
    }
    return false;
}

int ConditionSystem::effectiveSpeed(const std::string& entityId, int baseSpeed) const {
    if (exhaustionZeroSpeed(entityId)) return 0;
    if (hasCondition(entityId, Condition::Grappled))    return 0;
    if (hasCondition(entityId, Condition::Paralyzed))   return 0;
    if (hasCondition(entityId, Condition::Petrified))   return 0;
    if (hasCondition(entityId, Condition::Restrained))  return 0;
    if (hasCondition(entityId, Condition::Stunned))     return 0;
    if (hasCondition(entityId, Condition::Unconscious)) return 0;
    // Exhaustion level 2: speed halved
    int exLevel = exhaustionLevel(entityId);
    if (exLevel >= 2) return baseSpeed / 2;
    return baseSpeed;
}

// ---------------------------------------------------------------------------
// Query — attack advantage / disadvantage
// ---------------------------------------------------------------------------

bool ConditionSystem::attackerHasAdvantageOn(const std::string& attackerId,
                                              const std::string& targetId,
                                              AttackContext ctx) const {
    // Attacker is invisible → advantage on all attacks
    if (hasCondition(attackerId, Condition::Invisible)) return true;

    // Target conditions that grant attacker advantage:
    if (hasCondition(targetId, Condition::Blinded))     return true;
    if (hasCondition(targetId, Condition::Paralyzed))   return true;
    if (hasCondition(targetId, Condition::Petrified))   return true;
    if (hasCondition(targetId, Condition::Restrained))  return true;
    if (hasCondition(targetId, Condition::Stunned))     return true;
    if (hasCondition(targetId, Condition::Unconscious)) return true;
    // Prone: melee attacks against prone targets have advantage
    if (ctx == AttackContext::Melee && hasCondition(targetId, Condition::Prone)) return true;

    return false;
}

bool ConditionSystem::attackerHasDisadvantageOn(const std::string& attackerId,
                                                 const std::string& targetId,
                                                 AttackContext ctx) const {
    // Attacker conditions that impose disadvantage:
    if (hasCondition(attackerId, Condition::Blinded))    return true;
    if (hasCondition(attackerId, Condition::Frightened)) return true; // approximate
    if (hasCondition(attackerId, Condition::Poisoned))   return true;
    if (hasCondition(attackerId, Condition::Restrained)) return true;
    // Prone attacker: disadvantage on their own melee attacks
    if (ctx == AttackContext::Melee && hasCondition(attackerId, Condition::Prone)) return true;
    // Exhaustion level 3+: disadvantage on attacks
    if (exhaustionDisadvantageOnAttacks(attackerId)) return true;

    // Target is invisible → disadvantage
    if (hasCondition(targetId, Condition::Invisible)) return true;
    // Prone target and ranged attack
    if (ctx == AttackContext::Ranged && hasCondition(targetId, Condition::Prone)) return true;

    return false;
}

bool ConditionSystem::meleeAutocritsAgainst(const std::string& targetId) const {
    return hasCondition(targetId, Condition::Paralyzed)
        || hasCondition(targetId, Condition::Unconscious);
}

// ---------------------------------------------------------------------------
// Exhaustion
// ---------------------------------------------------------------------------

void ConditionSystem::addExhaustionLevel(const std::string& entityId) {
    m_exhaustion[entityId] = std::min(6, m_exhaustion[entityId] + 1);
}

void ConditionSystem::removeExhaustionLevel(const std::string& entityId) {
    auto it = m_exhaustion.find(entityId);
    if (it == m_exhaustion.end()) return;
    it->second--;
    if (it->second <= 0) m_exhaustion.erase(it);
}

int ConditionSystem::exhaustionLevel(const std::string& entityId) const {
    auto it = m_exhaustion.find(entityId);
    return (it != m_exhaustion.end()) ? it->second : 0;
}

bool ConditionSystem::exhaustionDisadvantageOnChecks(const std::string& e) const  { return exhaustionLevel(e) >= 1; }
bool ConditionSystem::exhaustionDisadvantageOnAttacks(const std::string& e) const { return exhaustionLevel(e) >= 3; }
bool ConditionSystem::exhaustionDisadvantageOnSaves(const std::string& e) const   { return exhaustionLevel(e) >= 3; }
bool ConditionSystem::exhaustionReducesMaxHP(const std::string& e) const          { return exhaustionLevel(e) >= 4; }
bool ConditionSystem::exhaustionZeroSpeed(const std::string& e) const             { return exhaustionLevel(e) >= 5; }
bool ConditionSystem::exhaustionDead(const std::string& e) const                  { return exhaustionLevel(e) >= 6; }

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------

void ConditionSystem::update(float deltaTime) {
    for (auto& [entityId, instances] : m_conditions) {
        instances.erase(std::remove_if(instances.begin(), instances.end(),
            [deltaTime](ConditionInstance& ci) {
                if (ci.isPermanent()) return false;
                ci.durationRemaining -= deltaTime;
                return ci.durationRemaining <= 0.0f;
            }), instances.end());
    }
    // Remove entries with no conditions left
    for (auto it = m_conditions.begin(); it != m_conditions.end(); ) {
        if (it->second.empty()) it = m_conditions.erase(it);
        else ++it;
    }
}

// ---------------------------------------------------------------------------
// Cleanup
// ---------------------------------------------------------------------------

void ConditionSystem::removeEntity(const std::string& entityId) {
    m_conditions.erase(entityId);
    m_exhaustion.erase(entityId);
}

void ConditionSystem::clear() {
    m_conditions.clear();
    m_exhaustion.clear();
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

nlohmann::json ConditionSystem::toJson() const {
    nlohmann::json j;

    nlohmann::json conds = nlohmann::json::object();
    for (const auto& [id, instances] : m_conditions) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& ci : instances) arr.push_back(ci.toJson());
        conds[id] = arr;
    }
    j["conditions"] = conds;

    nlohmann::json exh = nlohmann::json::object();
    for (const auto& [id, level] : m_exhaustion) exh[id] = level;
    j["exhaustion"] = exh;

    return j;
}

void ConditionSystem::fromJson(const nlohmann::json& j) {
    clear();

    if (j.contains("conditions")) {
        for (const auto& [entityId, arr] : j["conditions"].items()) {
            for (const auto& cj : arr) {
                try { m_conditions[entityId].push_back(ConditionInstance::fromJson(cj)); }
                catch (...) {}
            }
        }
    }

    if (j.contains("exhaustion")) {
        for (const auto& [entityId, level] : j["exhaustion"].items()) {
            m_exhaustion[entityId] = std::clamp(level.get<int>(), 0, 6);
        }
    }
}

} // namespace Core
} // namespace Phyxel
