#pragma once

#include "core/DiceSystem.h"
#include "core/DamageTypes.h"
#include "core/CharacterAttributes.h"

#include <string>
#include <vector>
#include <set>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

// ============================================================================
// Enums
// ============================================================================

enum class SpellSchool {
    Abjuration, Conjuration, Divination, Enchantment,
    Evocation, Illusion, Necromancy, Transmutation
};

enum class SpellComponent { Verbal, Somatic, Material };

enum class CastingTime {
    Action, BonusAction, Reaction, OneMinute, TenMinutes, OneHour
};

enum class SpellResolutionType {
    AttackRoll,   // d20 + spellAttackBonus vs target AC
    SavingThrow,  // target makes a saving throw vs DC
    AutoHit,      // always hits (magic missile, cure wounds)
    Utility       // no attack/damage (misty step, fly, etc.)
};

// Name helpers
const char*         spellSchoolName(SpellSchool s);
SpellSchool         spellSchoolFromString(const char* s);
const char*         castingTimeName(CastingTime ct);
CastingTime         castingTimeFromString(const char* s);
const char*         spellResolutionTypeName(SpellResolutionType r);
SpellResolutionType spellResolutionTypeFromString(const char* s);

// ============================================================================
// SpellDefinition
// ============================================================================

struct SpellDefinition {
    std::string id;
    std::string name;
    int         level  = 0;   // 0 = cantrip, 1–9 = leveled spell
    SpellSchool school = SpellSchool::Evocation;

    std::set<SpellComponent> components;
    std::string              materialDescription;

    CastingTime castingTime  = CastingTime::Action;
    int         rangeInFeet  = 0;
    bool        isSelf  = false;
    bool        isTouch = false;

    bool        requiresConcentration = false;
    float       durationSeconds       = 0.0f;  // 0 = instantaneous
    std::string durationDescription;

    SpellResolutionType resolutionType     = SpellResolutionType::AttackRoll;
    AbilityType         savingThrowAbility = AbilityType::Dexterity;
    bool                halfDamageOnSave   = false;

    DiceExpression baseDamage       = {0, DieType::D6, 0};  // count=0 = no damage
    DamageType     damageType       = DamageType::Force;
    DiceExpression upcastExtraPerSlot = {0, DieType::D6, 0};

    int healBase                    = 0;
    DiceExpression healDice         = {0, DieType::D8, 0};  // count=0 = no heal
    DiceExpression upcastHealPerSlot = {0, DieType::D8, 0};

    std::string              description;
    std::vector<std::string> classes;   // class IDs that can learn/prepare this spell

    // -----------------------------------------------------------------------
    // Queries
    // -----------------------------------------------------------------------
    bool isCantrip() const { return level == 0; }
    bool hasDamage()  const { return baseDamage.count > 0 || baseDamage.modifier > 0; }
    bool hasHeal()    const { return healDice.count > 0 || healBase > 0; }

    /// Cantrip damage multiplier by caster's total character level (5e breakpoints).
    static int cantripDiceMultiplier(int characterLevel);

    /// Damage expression for a cantrip at a given total character level.
    DiceExpression cantripDiceAt(int characterLevel) const;

    /// Damage expression when cast at slotLevel (ignored for cantrips, use cantripDiceAt).
    DiceExpression damageAt(int slotLevel) const;

    /// Heal dice expression when cast at slotLevel.
    DiceExpression healDiceAt(int slotLevel) const;

    // -----------------------------------------------------------------------
    // Serialization
    // -----------------------------------------------------------------------
    nlohmann::json toJson() const;
    static SpellDefinition fromJson(const nlohmann::json& j);
};

// ============================================================================
// SpellRegistry — singleton; load from resources/spells/*.json
// ============================================================================
class SpellRegistry {
public:
    static SpellRegistry& instance();

    void registerSpell(SpellDefinition def);
    const SpellDefinition* getSpell(const std::string& id) const;

    std::vector<const SpellDefinition*> getSpellsForClass(const std::string& classId) const;
    std::vector<const SpellDefinition*> getSpellsOfLevel(int level) const;
    std::vector<const SpellDefinition*> getAllSpells() const;

    /// Load all .json files in a directory (returns files loaded successfully).
    int  loadFromDirectory(const std::string& dirPath);
    bool loadFromFile(const std::string& filePath);

    size_t count() const { return m_spells.size(); }
    void   clear()       { m_spells.clear(); }

private:
    SpellRegistry() = default;
    std::unordered_map<std::string, SpellDefinition> m_spells;
};

} // namespace Core
} // namespace Phyxel
