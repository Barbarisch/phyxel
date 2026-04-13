#pragma once

#include "core/DamageTypes.h"
#include "core/DiceSystem.h"
#include "core/CharacterAttributes.h"
#include "core/ProficiencySystem.h"

#include <string>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

/// Full result of a d20 attack roll.
struct AttackRollResult {
    bool hit            = false;
    bool critical       = false;   // natural 20 — damage dice doubled
    bool fumble         = false;   // natural 1  — always misses
    int  d20Roll        = 0;       // the raw d20 result before modifier
    int  attackBonus    = 0;       // total modifier added
    int  attackTotal    = 0;       // d20Roll + attackBonus
    int  targetAC       = 0;
    RollResult damageRoll;         // raw damage roll (use finalDamage for actual)
    DamageType damageType = DamageType::Physical;
    int  finalDamage    = 0;       // after resistance/vulnerability/immunity
    DamageResistance resistance = DamageResistance::Normal;

    nlohmann::json toJson() const;
};

/// Result of a target's saving throw against a spell or effect.
struct SavingThrowResult {
    bool       succeeded    = false;
    int        roll         = 0;   // d20 result
    int        modifier     = 0;   // save bonus
    int        total        = 0;   // roll + modifier
    int        dc           = 0;
    AbilityType ability     = AbilityType::Constitution;

    nlohmann::json toJson() const;
};

/// Stateless rules engine for D&D 5e attack and saving throw resolution.
///
/// Design: AttackResolver takes only what it needs — no entity references,
/// no registry lookups. Callers compute attack bonuses, advantage/disadvantage
/// (from ConditionSystem), and resistances (from DamageResistances) before
/// passing them in. This keeps the resolver pure and fast.
class AttackResolver {
public:
    // -----------------------------------------------------------------------
    // Attack rolls
    // -----------------------------------------------------------------------

    /// Resolve a melee or ranged weapon/spell attack.
    /// @param attackBonus  STR/DEX mod + proficiency + magic bonus (already summed)
    /// @param targetAC     defender's total AC
    /// @param damageDice   e.g. DiceExpression::parse("1d8+3")
    /// @param resistance   caller queries DamageResistances for this type
    static AttackRollResult resolveAttack(
        int attackBonus,
        int targetAC,
        const DiceExpression& damageDice,
        DamageType damageType,
        DamageResistance resistance,
        bool hasAdvantage,
        bool hasDisadvantage,
        DiceSystem& dice);

    /// Convenience: takes a CharacterAttributes for the attacker to compute
    /// attack bonus automatically (STR/DEX + proficiency).
    static AttackRollResult resolveWeaponAttack(
        const CharacterAttributes& attackerAttrs,
        int proficiencyBonus,
        bool isProficient,
        bool useStrength,        // false = use DEX (finesse/ranged)
        int magicBonus,          // +0, +1, +2, or +3 from weapon enchantment
        int targetAC,
        const DiceExpression& damageDice,
        DamageType damageType,
        DamageResistance resistance,
        bool hasAdvantage,
        bool hasDisadvantage,
        DiceSystem& dice);

    // -----------------------------------------------------------------------
    // Saving throws
    // -----------------------------------------------------------------------

    /// Resolve a saving throw. Proficiency adds to the roll only if proficient.
    static SavingThrowResult resolveSavingThrow(
        const CharacterAttributes& targetAttrs,
        AbilityType ability,
        bool proficient,
        int proficiencyBonus,
        int dc,
        bool hasAdvantage,
        bool hasDisadvantage,
        DiceSystem& dice);

    // -----------------------------------------------------------------------
    // AC calculation
    // -----------------------------------------------------------------------

    /// Calculate a creature's Armor Class.
    /// @param armorBaseAC   0 = unarmored (use 10 + DEX mod)
    /// @param maxDexBonus   -1 = unlimited (light/no armor), 0 = none (heavy), N = cap (medium)
    /// @param shieldBonus   +2 if wielding a shield, else 0
    /// @param magicBonus    from magic armor or spells
    static int calculateAC(
        const CharacterAttributes& attrs,
        int armorBaseAC     = 0,
        int maxDexBonus     = -1,
        int shieldBonus     = 0,
        int magicBonus      = 0);

    // -----------------------------------------------------------------------
    // Damage resistance
    // -----------------------------------------------------------------------

    /// Apply a damage resistance to a raw damage value.
    /// Resistant: floor(damage / 2). Vulnerable: damage * 2. Immune: 0.
    static int applyResistance(int damage, DamageResistance resistance);

private:
    /// Roll damage for a hit, doubling dice on a critical.
    static RollResult rollDamage(const DiceExpression& dice, bool critical, DiceSystem& rng);

    /// Select d20 roll accounting for advantage/disadvantage (cancel if both).
    static RollResult rollAttackD20(bool hasAdvantage, bool hasDisadvantage,
                                     int attackBonus, DiceSystem& dice);
};

} // namespace Core
} // namespace Phyxel
