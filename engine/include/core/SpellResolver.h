#pragma once

#include "core/SpellDefinition.h"
#include "core/SpellcasterComponent.h"
#include "core/AttackResolver.h"
#include "core/DiceSystem.h"

#include <string>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

// ============================================================================
// CastResult — outcome of a single spell cast attempt
// ============================================================================
struct CastResult {
    bool        success    = false;  // false = couldn't cast (no slot, not known, etc.)
    std::string failReason;

    bool        hit        = false;  // for AttackRoll spells
    bool        targetSaved = false; // for SavingThrow spells (true = save succeeded)
    bool        isCritical  = false;

    int         damage     = 0;      // final damage dealt (after resistance)
    int         healAmount = 0;      // final HP restored (AutoHit healing spells)
    DamageType  damageType = DamageType::Force;

    RollResult  attackRoll;   // populated for AttackRoll spells
    RollResult  damageRoll;   // populated when damage was rolled

    nlohmann::json toJson() const;
};

// ============================================================================
// SpellResolver
// ============================================================================
class SpellResolver {
public:
    /// Resolve a complete spell cast.
    ///
    /// - Validates the cast is legal (known/prepared, slot available).
    /// - Spends the slot.
    /// - Resolves attack roll, saving throw, or auto-hit as appropriate.
    /// - Applies resistance.
    /// - For concentration spells, call caster.startConcentration() separately
    ///   after a successful cast (SpellResolver does NOT manage concentration).
    ///
    /// @param caster               Caster's SpellcasterComponent (slot spent here).
    /// @param casterAttrs          Caster's ability scores.
    /// @param proficiencyBonus     Caster's proficiency bonus.
    /// @param spellId              ID of the spell to cast.
    /// @param slotLevel            Slot level used (must be >= spell's base level; 0 for cantrips).
    /// @param casterTotalLevel     Caster's total character level (cantrip scaling).
    /// @param targetEntityId       ID of the primary target (informational).
    /// @param targetAC             Target's armor class (for AttackRoll spells).
    /// @param targetAttrs          Target's ability scores (for SavingThrow spells; may be null).
    /// @param targetProfInSave     Whether target is proficient in the relevant save.
    /// @param targetResistance     Target's damage resistance for the spell's damage type.
    /// @param hasAdvantage         Attacker has advantage on spell attack roll.
    /// @param hasDisadvantage      Attacker has disadvantage on spell attack roll.
    /// @param dice                 RNG source.
    static CastResult castSpell(
        SpellcasterComponent&      caster,
        const CharacterAttributes& casterAttrs,
        int                        proficiencyBonus,
        const std::string&         spellId,
        int                        slotLevel,
        int                        casterTotalLevel,
        const std::string&         targetEntityId,
        int                        targetAC,
        const CharacterAttributes* targetAttrs,
        bool                       targetProfInSave,
        DamageResistance           targetResistance,
        bool                       hasAdvantage,
        bool                       hasDisadvantage,
        DiceSystem&                dice);

private:
    static CastResult resolveAttackRollSpell(
        const SpellDefinition&     spell,
        const CharacterAttributes& casterAttrs,
        int                        profBonus,
        int                        slotLevel,
        int                        casterTotalLevel,
        int                        targetAC,
        DamageResistance           resistance,
        bool                       hasAdvantage,
        bool                       hasDisadvantage,
        DiceSystem&                dice);

    static CastResult resolveSavingThrowSpell(
        const SpellDefinition&     spell,
        const CharacterAttributes& casterAttrs,
        int                        profBonus,
        int                        slotLevel,
        int                        casterTotalLevel,
        const CharacterAttributes* targetAttrs,
        bool                       targetProfInSave,
        DamageResistance           resistance,
        bool                       hasAdvantage,    // on save (rare — target adv)
        bool                       hasDisadvantage, // on save
        DiceSystem&                dice,
        int                        spellSaveDC);

    static CastResult resolveAutoHitSpell(
        const SpellDefinition&     spell,
        int                        slotLevel,
        int                        casterTotalLevel,
        int                        casterAbilityModifier,
        DamageResistance           resistance,
        DiceSystem&                dice);
};

} // namespace Core
} // namespace Phyxel
