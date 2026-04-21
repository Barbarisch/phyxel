#include "core/AttackResolver.h"

#include <algorithm>
#include <cmath>

namespace Phyxel {
namespace Core {

// ---------------------------------------------------------------------------
// AttackRollResult / SavingThrowResult serialization
// ---------------------------------------------------------------------------

nlohmann::json AttackRollResult::toJson() const {
    return {
        {"hit",          hit},
        {"critical",     critical},
        {"fumble",       fumble},
        {"d20Roll",      d20Roll},
        {"attackBonus",  attackBonus},
        {"attackTotal",  attackTotal},
        {"targetAC",     targetAC},
        {"damageType",   damageTypeToString(damageType)},
        {"finalDamage",  finalDamage},
        {"resistance",   static_cast<int>(resistance)}
    };
}

nlohmann::json SavingThrowResult::toJson() const {
    return {
        {"succeeded", succeeded},
        {"roll",      roll},
        {"modifier",  modifier},
        {"total",     total},
        {"dc",        dc},
        {"ability",   abilityShortName(ability)}
    };
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

RollResult AttackResolver::rollAttackD20(bool hasAdvantage, bool hasDisadvantage,
                                          int attackBonus, DiceSystem& dice) {
    // Advantage and disadvantage cancel out
    bool effectiveAdv  = hasAdvantage  && !hasDisadvantage;
    bool effectiveDisadv = hasDisadvantage && !hasAdvantage;

    if (effectiveAdv)   return dice.rollAdvantage(DieType::D20, attackBonus);
    if (effectiveDisadv) return dice.rollDisadvantage(DieType::D20, attackBonus);
    return dice.roll(DieType::D20, attackBonus);
}

RollResult AttackResolver::rollDamage(const DiceExpression& expr, bool critical, DiceSystem& rng) {
    if (critical) return rng.rollCritical(expr);
    return rng.rollExpression(expr);
}

// ---------------------------------------------------------------------------
// Attack resolution — primitive API
// ---------------------------------------------------------------------------

AttackRollResult AttackResolver::resolveAttack(
    int attackBonus, int targetAC,
    const DiceExpression& damageDice, DamageType damageType,
    DamageResistance resistance,
    bool hasAdvantage, bool hasDisadvantage,
    DiceSystem& dice)
{
    AttackRollResult result;
    result.attackBonus = attackBonus;
    result.targetAC    = targetAC;
    result.damageType  = damageType;
    result.resistance  = resistance;

    // Roll d20
    auto d20 = rollAttackD20(hasAdvantage, hasDisadvantage, attackBonus, dice);
    result.d20Roll    = d20.dice[0];
    result.attackTotal = d20.total;
    result.critical   = d20.isCriticalSuccess;  // nat 20 always hits + crits
    result.fumble     = d20.isCriticalFailure;  // nat 1 always misses

    // Hit determination: nat-20 always hits, nat-1 always misses, else compare to AC
    result.hit = result.critical || (!result.fumble && d20.total >= targetAC);

    if (!result.hit) {
        result.finalDamage = 0;
        return result;
    }

    // Roll damage (double dice on crit)
    result.damageRoll  = rollDamage(damageDice, result.critical, dice);
    int rawDamage      = std::max(0, result.damageRoll.total);
    result.finalDamage = applyResistance(rawDamage, resistance);
    return result;
}

// ---------------------------------------------------------------------------
// Attack resolution — sheet-integrated API
// ---------------------------------------------------------------------------

AttackRollResult AttackResolver::resolveWeaponAttack(
    const CharacterAttributes& attackerAttrs,
    int proficiencyBonus, bool isProficient,
    bool useStrength, int magicBonus,
    int targetAC,
    const DiceExpression& damageDice, DamageType damageType,
    DamageResistance resistance,
    bool hasAdvantage, bool hasDisadvantage,
    DiceSystem& dice)
{
    int abilityMod  = useStrength
        ? attackerAttrs.strength.modifier()
        : attackerAttrs.dexterity.modifier();
    int profBonus   = isProficient ? proficiencyBonus : 0;
    int attackBonus = abilityMod + profBonus + magicBonus;

    // The damage dice modifier also includes the ability score mod
    DiceExpression scaledDice = damageDice;
    scaledDice.modifier += abilityMod + magicBonus;

    return resolveAttack(attackBonus, targetAC, scaledDice, damageType,
                         resistance, hasAdvantage, hasDisadvantage, dice);
}

// ---------------------------------------------------------------------------
// Saving throw
// ---------------------------------------------------------------------------

SavingThrowResult AttackResolver::resolveSavingThrow(
    const CharacterAttributes& targetAttrs,
    AbilityType ability, bool proficient, int proficiencyBonus,
    int dc, bool hasAdvantage, bool hasDisadvantage,
    DiceSystem& dice)
{
    SavingThrowResult result;
    result.dc      = dc;
    result.ability = ability;
    result.modifier = targetAttrs.modifier(ability) + (proficient ? proficiencyBonus : 0);

    auto d20 = rollAttackD20(hasAdvantage, hasDisadvantage, result.modifier, dice);
    result.roll  = d20.dice[0];
    result.total = d20.total;
    result.succeeded = DiceSystem::checkDC(result.total, dc);
    return result;
}

// ---------------------------------------------------------------------------
// AC calculation
// ---------------------------------------------------------------------------

int AttackResolver::calculateAC(
    const CharacterAttributes& attrs,
    int armorBaseAC, int maxDexBonus, int shieldBonus, int magicBonus)
{
    int dexMod = attrs.dexterity.modifier();

    if (armorBaseAC == 0) {
        // Unarmored: 10 + DEX mod
        return 10 + dexMod + shieldBonus + magicBonus;
    }

    // Apply DEX cap
    int effectiveDex = (maxDexBonus < 0) ? dexMod : std::min(dexMod, maxDexBonus);
    return armorBaseAC + effectiveDex + shieldBonus + magicBonus;
}

// ---------------------------------------------------------------------------
// Damage resistance
// ---------------------------------------------------------------------------

int AttackResolver::applyResistance(int damage, DamageResistance resistance) {
    switch (resistance) {
        case DamageResistance::Immune:     return 0;
        case DamageResistance::Resistant:  return damage / 2;   // floor division (5e rule)
        case DamageResistance::Vulnerable: return damage * 2;
        default:                           return damage;
    }
}

} // namespace Core
} // namespace Phyxel
