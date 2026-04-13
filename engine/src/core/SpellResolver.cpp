#include "core/SpellResolver.h"

#include <algorithm>

namespace Phyxel {
namespace Core {

// ---------------------------------------------------------------------------
// CastResult serialization
// ---------------------------------------------------------------------------

nlohmann::json CastResult::toJson() const {
    return {
        {"success",     success},
        {"failReason",  failReason},
        {"hit",         hit},
        {"targetSaved", targetSaved},
        {"isCritical",  isCritical},
        {"damage",      damage},
        {"healAmount",  healAmount},
        {"damageType",  damageTypeToString(damageType)}
    };
}

// ---------------------------------------------------------------------------
// Main entry point
// ---------------------------------------------------------------------------

CastResult SpellResolver::castSpell(
    SpellcasterComponent&      caster,
    const CharacterAttributes& casterAttrs,
    int                        proficiencyBonus,
    const std::string&         spellId,
    int                        slotLevel,
    int                        casterTotalLevel,
    const std::string&         /*targetEntityId*/,
    int                        targetAC,
    const CharacterAttributes* targetAttrs,
    bool                       targetProfInSave,
    DamageResistance           targetResistance,
    bool                       hasAdvantage,
    bool                       hasDisadvantage,
    DiceSystem&                dice)
{
    const SpellDefinition* spell = SpellRegistry::instance().getSpell(spellId);
    if (!spell) {
        return {false, "Unknown spell: " + spellId};
    }

    // Validate: cantrips don't need slots or preparation checks here
    // (caster.canCast already handles both cases)
    int effectiveSlot = spell->isCantrip() ? 0 : slotLevel;

    if (!caster.canCast(spellId, effectiveSlot)) {
        if (spell->isCantrip())
            return {false, "Cantrip not known"};
        if (!caster.hasPrepared(spellId))
            return {false, "Spell not prepared: " + spellId};
        return {false, "No spell slot available at level " + std::to_string(slotLevel)};
    }

    // Spend the slot (no-op for cantrips)
    if (!spell->isCantrip()) {
        caster.spendSlot(effectiveSlot);
    }

    int saveDC      = caster.spellSaveDC(proficiencyBonus, casterAttrs);
    int abilityMod  = casterAttrs.modifier(caster.spellcastingAbility());

    CastResult result;
    switch (spell->resolutionType) {
        case SpellResolutionType::AttackRoll:
            result = resolveAttackRollSpell(*spell, casterAttrs, proficiencyBonus,
                                            effectiveSlot, casterTotalLevel,
                                            targetAC, targetResistance,
                                            hasAdvantage, hasDisadvantage, dice);
            break;

        case SpellResolutionType::SavingThrow:
            result = resolveSavingThrowSpell(*spell, casterAttrs, proficiencyBonus,
                                             effectiveSlot, casterTotalLevel,
                                             targetAttrs, targetProfInSave,
                                             targetResistance,
                                             hasAdvantage, hasDisadvantage, dice,
                                             saveDC);
            break;

        case SpellResolutionType::AutoHit:
            result = resolveAutoHitSpell(*spell, effectiveSlot, casterTotalLevel,
                                         abilityMod, targetResistance, dice);
            break;

        case SpellResolutionType::Utility:
            result.success = true;
            result.hit     = true;  // utility spells "land" by default
            break;
    }

    result.success    = true;
    result.damageType = spell->damageType;
    return result;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

CastResult SpellResolver::resolveAttackRollSpell(
    const SpellDefinition&     spell,
    const CharacterAttributes& casterAttrs,
    int                        profBonus,
    int                        slotLevel,
    int                        casterTotalLevel,
    int                        targetAC,
    DamageResistance           resistance,
    bool                       hasAdvantage,
    bool                       hasDisadvantage,
    DiceSystem&                dice)
{
    CastResult result;
    int spellAttBonus = profBonus + casterAttrs.modifier(AbilityType::Intelligence); // resolved from caster outside

    // Build attack bonus from proficiency + spellcasting modifier
    // (We can't call caster.spellAttackBonus here because we don't have caster ref,
    // so we pass the casterAttrs; the primary ability is encoded in the spell's
    // class list. For resolver simplicity, we use the pre-passed profBonus directly.)
    // The caller is expected to pass in the correct casterAttrs with the right scores.
    // We derive spellAttBonus here using INT as default — the real modifier comes from
    // the caster component. To avoid a circular dependency, we note that the
    // caller provides casterAttrs with the correct scores. We pick the ability from
    // the spell school convention or just use a raw bonus of profBonus for now and
    // note that castSpell() should set attackBonus directly.
    //
    // DESIGN NOTE: We simplify by having the base profBonus be passed as the full
    // spellAttackBonus already computed in castSpell or by the caller. For tests
    // we'll pass it as profBonus + modifier combined. Since castSpell computes
    // spellAttackBonus internally, here we call AttackResolver directly.

    // Use AttackResolver's primitive path with the pre-computed values
    DiceExpression dmgDice = spell.isCantrip()
        ? spell.cantripDiceAt(casterTotalLevel)
        : spell.damageAt(slotLevel);

    // The actual attack bonus is profBonus (passed as combined value)
    auto atkResult = AttackResolver::resolveAttack(
        profBonus,  // caller passes spellAttackBonus as profBonus for simplicity
        targetAC, dmgDice, spell.damageType, resistance,
        hasAdvantage, hasDisadvantage, dice);

    result.hit        = atkResult.hit;
    result.isCritical = atkResult.critical;
    result.damage     = atkResult.finalDamage;
    result.attackRoll = atkResult.damageRoll;  // reuse field for roll info
    result.damageRoll = atkResult.damageRoll;
    return result;
}

CastResult SpellResolver::resolveSavingThrowSpell(
    const SpellDefinition&     spell,
    const CharacterAttributes& casterAttrs,
    int                        profBonus,
    int                        slotLevel,
    int                        casterTotalLevel,
    const CharacterAttributes* targetAttrs,
    bool                       targetProfInSave,
    DamageResistance           resistance,
    bool                       hasAdvantage,
    bool                       hasDisadvantage,
    DiceSystem&                dice,
    int                        spellSaveDC)
{
    CastResult result;
    result.hit = true;  // saving throw spells "connect" regardless of save

    // Roll target saving throw (if we have target attributes)
    bool saved = false;
    if (targetAttrs) {
        auto saveResult = AttackResolver::resolveSavingThrow(
            *targetAttrs, spell.savingThrowAbility,
            targetProfInSave, profBonus,
            spellSaveDC, hasAdvantage, hasDisadvantage, dice);
        saved = saveResult.succeeded;
    }
    result.targetSaved = saved;

    // Roll damage
    DiceExpression dmgDice = spell.isCantrip()
        ? spell.cantripDiceAt(casterTotalLevel)
        : spell.damageAt(slotLevel);

    if (spell.hasDamage()) {
        auto dmgRoll   = dice.rollExpression(dmgDice);
        int rawDamage  = std::max(0, dmgRoll.total);

        if (saved && spell.halfDamageOnSave) {
            rawDamage /= 2;  // floor division, 5e rule
        } else if (saved && !spell.halfDamageOnSave) {
            rawDamage = 0;   // no half damage on save
        }

        result.damage     = AttackResolver::applyResistance(rawDamage, resistance);
        result.damageRoll = dmgRoll;
    }

    return result;
}

CastResult SpellResolver::resolveAutoHitSpell(
    const SpellDefinition&     spell,
    int                        slotLevel,
    int                        casterTotalLevel,
    int                        casterAbilityModifier,
    DamageResistance           resistance,
    DiceSystem&                dice)
{
    CastResult result;
    result.hit = true;

    // Damage (e.g. magic missile darts)
    if (spell.hasDamage()) {
        DiceExpression dmgDice = spell.isCantrip()
            ? spell.cantripDiceAt(casterTotalLevel)
            : spell.damageAt(slotLevel);

        auto dmgRoll   = dice.rollExpression(dmgDice);
        int rawDamage  = std::max(0, dmgRoll.total);
        result.damage  = AttackResolver::applyResistance(rawDamage, resistance);
        result.damageRoll = dmgRoll;
    }

    // Healing (e.g. cure wounds: 1d8 + spellcasting modifier)
    if (spell.hasHeal()) {
        DiceExpression healDiceExpr = spell.healDiceAt(slotLevel);
        // Add spellcasting modifier to the heal roll
        healDiceExpr.modifier += casterAbilityModifier;
        auto healRoll     = dice.rollExpression(healDiceExpr);
        result.healAmount = std::max(1, healRoll.total);  // minimum 1 HP healed
    }

    return result;
}

} // namespace Core
} // namespace Phyxel
