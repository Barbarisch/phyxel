#pragma once

#include <set>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

// ============================================================================
// Damage types — full D&D 5e set, backwards-compatible with CombatSystem
// ============================================================================
enum class DamageType {
    // Real-time action types (pre-existing, keep values stable)
    Physical  = 0,
    Fire      = 1,
    Ice       = 2,   // maps to Cold in D&D contexts
    Poison    = 3,
    // D&D 5e physical subtypes
    Bludgeoning = 10,
    Piercing    = 11,
    Slashing    = 12,
    // D&D 5e elemental
    Cold        = 13,  // alias of Ice for D&D code
    Lightning   = 14,
    Thunder     = 15,
    Acid        = 16,
    // D&D 5e magical
    Necrotic    = 20,
    Radiant     = 21,
    Psychic     = 22,
    Force       = 23,
};

const char* damageTypeToString(DamageType type);
DamageType  damageTypeFromString(const char* s);

/// How an entity interacts with a specific damage type.
enum class DamageResistance { Normal, Resistant, Vulnerable, Immune };

// ============================================================================
// DamageResistances — per-entity set of resistances, vulnerabilities, immunities
// ============================================================================
struct DamageResistances {
    std::set<DamageType> resistant;
    std::set<DamageType> vulnerable;
    std::set<DamageType> immune;

    DamageResistance getResistance(DamageType type) const;

    void addResistance(DamageType type)   { resistant.insert(type); }
    void addVulnerability(DamageType type){ vulnerable.insert(type); }
    void addImmunity(DamageType type)     { immune.insert(type); }
    void removeResistance(DamageType type){ resistant.erase(type); }
    void removeVulnerability(DamageType t){ vulnerable.erase(t); }
    void removeImmunity(DamageType type)  { immune.erase(type); }

    bool isEmpty() const { return resistant.empty() && vulnerable.empty() && immune.empty(); }

    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);
};

} // namespace Core
} // namespace Phyxel
