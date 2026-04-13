#pragma once

#include "core/CharacterAttributes.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

// ============================================================================
// The 15 D&D 5e conditions + Exhaustion (tracked separately by level)
// ============================================================================
enum class Condition {
    Blinded,        // auto-fail sight checks; attacks: disadv out, adv in
    Charmed,        // can't attack charmer; charmer has adv on social checks vs you
    Deafened,       // auto-fail hearing checks
    Frightened,     // disadv on checks/attacks while source in sight; can't move closer
    Grappled,       // speed = 0; ends if grappler incapacitated or forced apart
    Incapacitated,  // no actions or reactions
    Invisible,      // attacks: adv out, disadv in; can't be seen without special sense
    Paralyzed,      // incapacitated; auto-fail STR/DEX saves; adv in; melee = auto-crit
    Petrified,      // transformed to stone; paralyzed + resistance to all damage
    Poisoned,       // disadv on attack rolls and ability checks
    Prone,          // disadv on attacks; melee adv in; ranged disadv in; 0-cost stand = half speed
    Restrained,     // speed = 0; disadv attacks; adv in; disadv on DEX saves
    Stunned,        // incapacitated; auto-fail STR/DEX saves; adv in
    Unconscious,    // incapacitated + prone; auto-fail STR/DEX; adv in; melee = auto-crit
    Exhausted,      // special: 6 cumulative levels (tracked separately)
};

const char* conditionName(Condition c);
Condition   conditionFromString(const char* s);

/// One active condition instance on an entity.
struct ConditionInstance {
    Condition   type;
    float       durationRemaining = -1.0f;  // seconds; -1 = indefinite
    std::string sourceEntityId;             // who applied it (empty = environmental)
    std::string sourceSpellId;              // which spell caused it (for dispel)
    std::string description;               // e.g. "Web spell, concentration"

    bool isPermanent() const { return durationRemaining < 0.0f; }
    nlohmann::json toJson() const;
    static ConditionInstance fromJson(const nlohmann::json& j);
};

/// Attack context: affects which advantage/disadvantage rules apply.
enum class AttackContext { Melee, Ranged, SpellAttack };

// ============================================================================
// ConditionSystem — manages active conditions for all entities in a scene
// ============================================================================
class ConditionSystem {
public:
    // -----------------------------------------------------------------------
    // Apply / Remove
    // -----------------------------------------------------------------------

    /// Apply a condition to an entity. Multiple instances of the same condition
    /// from different sources stack (each must be removed individually).
    void applyCondition(const std::string& entityId, ConditionInstance instance);

    /// Remove all instances of a condition from an entity.
    void removeCondition(const std::string& entityId, Condition type);

    /// Remove a specific condition instance by source entity (e.g. caster loses
    /// concentration → their conditions on targets drop).
    void removeAllFromSource(const std::string& entityId,
                              const std::string& sourceEntityId);

    /// Remove all conditions from an entity (e.g. Greater Restoration).
    void clearAllConditions(const std::string& entityId);

    // -----------------------------------------------------------------------
    // Query — presence
    // -----------------------------------------------------------------------

    bool hasCondition(const std::string& entityId, Condition type) const;

    /// All active conditions on an entity.
    std::vector<Condition> getConditions(const std::string& entityId) const;

    /// All instances (with duration info) on an entity.
    const std::vector<ConditionInstance>* getInstances(const std::string& entityId) const;

    // -----------------------------------------------------------------------
    // Query — derived mechanical states
    // -----------------------------------------------------------------------

    /// True if the entity has Paralyzed, Stunned, Unconscious, Petrified,
    /// or Incapacitated — all of which strip actions/reactions.
    bool isIncapacitated(const std::string& entityId) const;

    /// True if this entity auto-fails saving throws of the given ability
    /// (Paralyzed/Stunned/Unconscious → auto-fail STR and DEX).
    bool autoFailsSave(const std::string& entityId, AbilityType ability) const;

    /// Speed of this entity (in feet) after applying condition-based reductions.
    /// Caller passes the entity's base speed.
    int effectiveSpeed(const std::string& entityId, int baseSpeed) const;

    // -----------------------------------------------------------------------
    // Query — attack advantage/disadvantage
    // -----------------------------------------------------------------------

    /// Should the attacker roll with advantage against this target?
    /// Depends on attack context (melee vs ranged vs spell).
    bool attackerHasAdvantageOn(const std::string& attackerId,
                                 const std::string& targetId,
                                 AttackContext ctx) const;

    /// Should the attacker roll with disadvantage against this target?
    bool attackerHasDisadvantageOn(const std::string& attackerId,
                                    const std::string& targetId,
                                    AttackContext ctx) const;

    /// Melee hits on this target are critical hits regardless of roll
    /// (Paralyzed / Unconscious within 5 ft — in-engine we approximate as always).
    bool meleeAutocritsAgainst(const std::string& targetId) const;

    // -----------------------------------------------------------------------
    // Exhaustion (special: 6 cumulative levels)
    // -----------------------------------------------------------------------

    void addExhaustionLevel(const std::string& entityId);
    void removeExhaustionLevel(const std::string& entityId);
    int  exhaustionLevel(const std::string& entityId) const;

    // Exhaustion effects (call these when checking rolls):
    bool exhaustionDisadvantageOnChecks(const std::string& entityId) const;  // level 1+
    bool exhaustionDisadvantageOnAttacks(const std::string& entityId) const; // level 3+
    bool exhaustionDisadvantageOnSaves(const std::string& entityId) const;   // level 3+
    bool exhaustionReducesMaxHP(const std::string& entityId) const;          // level 4+
    bool exhaustionZeroSpeed(const std::string& entityId) const;             // level 5+
    bool exhaustionDead(const std::string& entityId) const;                  // level 6

    // -----------------------------------------------------------------------
    // Update (tick durations)
    // -----------------------------------------------------------------------

    /// Call every frame with deltaTime. Removes expired timed conditions.
    void update(float deltaTime);

    // -----------------------------------------------------------------------
    // Cleanup
    // -----------------------------------------------------------------------

    /// Remove a dead/removed entity and all their conditions.
    void removeEntity(const std::string& entityId);

    void clear();

    // -----------------------------------------------------------------------
    // Serialization
    // -----------------------------------------------------------------------
    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);

private:
    // entityId → active condition instances
    std::unordered_map<std::string, std::vector<ConditionInstance>> m_conditions;
    // entityId → exhaustion level (0–6)
    std::unordered_map<std::string, int> m_exhaustion;
};

} // namespace Core
} // namespace Phyxel
