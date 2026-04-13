#include <gtest/gtest.h>
#include "core/SpellDefinition.h"

using namespace Phyxel::Core;

// ---------------------------------------------------------------------------
// Name helpers
// ---------------------------------------------------------------------------

TEST(SpellDefinitionTest, SchoolNameRoundTrip) {
    const SpellSchool schools[] = {
        SpellSchool::Abjuration, SpellSchool::Conjuration, SpellSchool::Divination,
        SpellSchool::Enchantment, SpellSchool::Evocation, SpellSchool::Illusion,
        SpellSchool::Necromancy,  SpellSchool::Transmutation
    };
    for (auto s : schools) {
        const char* name = spellSchoolName(s);
        EXPECT_EQ(spellSchoolFromString(name), s) << "Round-trip failed for: " << name;
    }
}

TEST(SpellDefinitionTest, CastingTimeRoundTrip) {
    const CastingTime times[] = {
        CastingTime::Action, CastingTime::BonusAction, CastingTime::Reaction,
        CastingTime::OneMinute, CastingTime::TenMinutes, CastingTime::OneHour
    };
    for (auto ct : times) {
        const char* name = castingTimeName(ct);
        EXPECT_EQ(castingTimeFromString(name), ct) << "Round-trip failed for: " << name;
    }
}

TEST(SpellDefinitionTest, ResolutionTypeRoundTrip) {
    const SpellResolutionType types[] = {
        SpellResolutionType::AttackRoll, SpellResolutionType::SavingThrow,
        SpellResolutionType::AutoHit,    SpellResolutionType::Utility
    };
    for (auto rt : types) {
        const char* name = spellResolutionTypeName(rt);
        EXPECT_EQ(spellResolutionTypeFromString(name), rt) << "Round-trip failed for: " << name;
    }
}

// ---------------------------------------------------------------------------
// SpellDefinition — cantrip
// ---------------------------------------------------------------------------

TEST(SpellDefinitionTest, CantripDiceMultiplier) {
    EXPECT_EQ(SpellDefinition::cantripDiceMultiplier(1),  1);
    EXPECT_EQ(SpellDefinition::cantripDiceMultiplier(4),  1);
    EXPECT_EQ(SpellDefinition::cantripDiceMultiplier(5),  2);
    EXPECT_EQ(SpellDefinition::cantripDiceMultiplier(10), 2);
    EXPECT_EQ(SpellDefinition::cantripDiceMultiplier(11), 3);
    EXPECT_EQ(SpellDefinition::cantripDiceMultiplier(16), 3);
    EXPECT_EQ(SpellDefinition::cantripDiceMultiplier(17), 4);
    EXPECT_EQ(SpellDefinition::cantripDiceMultiplier(20), 4);
}

TEST(SpellDefinitionTest, CantripDiceAtLevel) {
    SpellDefinition spell;
    spell.level       = 0;
    spell.baseDamage  = DiceExpression::parse("1d10");

    auto dice1 = spell.cantripDiceAt(1);
    EXPECT_EQ(dice1.count, 1);
    EXPECT_EQ(static_cast<int>(dice1.die), 10);

    auto dice5 = spell.cantripDiceAt(5);
    EXPECT_EQ(dice5.count, 2);
    EXPECT_EQ(static_cast<int>(dice5.die), 10);

    auto dice17 = spell.cantripDiceAt(17);
    EXPECT_EQ(dice17.count, 4);
    EXPECT_EQ(static_cast<int>(dice17.die), 10);
}

TEST(SpellDefinitionTest, IsCantrip) {
    SpellDefinition cantrip;
    cantrip.level = 0;
    EXPECT_TRUE(cantrip.isCantrip());

    SpellDefinition leveled;
    leveled.level = 3;
    EXPECT_FALSE(leveled.isCantrip());
}

// ---------------------------------------------------------------------------
// SpellDefinition — upcast scaling
// ---------------------------------------------------------------------------

TEST(SpellDefinitionTest, DamageAtBaseLevel) {
    SpellDefinition spell;
    spell.level      = 3;
    spell.baseDamage = DiceExpression::parse("8d6");
    // No upcast scaling
    auto dice = spell.damageAt(3);
    EXPECT_EQ(dice.count, 8);
    EXPECT_EQ(static_cast<int>(dice.die), 6);
}

TEST(SpellDefinitionTest, DamageAtHigherLevel) {
    SpellDefinition spell;
    spell.level             = 3;
    spell.baseDamage        = DiceExpression::parse("8d6");
    spell.upcastExtraPerSlot = DiceExpression::parse("1d6");

    auto dice4 = spell.damageAt(4);  // 1 level above → +1d6
    EXPECT_EQ(dice4.count, 9);
    EXPECT_EQ(static_cast<int>(dice4.die), 6);

    auto dice6 = spell.damageAt(6);  // 3 levels above → +3d6
    EXPECT_EQ(dice6.count, 11);
    EXPECT_EQ(static_cast<int>(dice6.die), 6);
}

TEST(SpellDefinitionTest, DamageAtLowerLevelClampsToBase) {
    SpellDefinition spell;
    spell.level             = 3;
    spell.baseDamage        = DiceExpression::parse("8d6");
    spell.upcastExtraPerSlot = DiceExpression::parse("1d6");

    // Passing slotLevel < spell level should not reduce damage
    auto dice = spell.damageAt(1);
    EXPECT_EQ(dice.count, 8);  // no negative scaling
}

// ---------------------------------------------------------------------------
// SpellDefinition — healing scaling
// ---------------------------------------------------------------------------

TEST(SpellDefinitionTest, HealDiceAtBaseLevel) {
    SpellDefinition spell;
    spell.level    = 1;
    spell.healDice = DiceExpression::parse("1d8");

    auto dice = spell.healDiceAt(1);
    EXPECT_EQ(dice.count, 1);
    EXPECT_EQ(static_cast<int>(dice.die), 8);
}

TEST(SpellDefinitionTest, HealDiceUpcast) {
    SpellDefinition spell;
    spell.level           = 1;
    spell.healDice        = DiceExpression::parse("1d8");
    spell.upcastHealPerSlot = DiceExpression::parse("1d8");

    auto dice3 = spell.healDiceAt(3);  // 2 levels up → +2d8
    EXPECT_EQ(dice3.count, 3);
    EXPECT_EQ(static_cast<int>(dice3.die), 8);
}

// ---------------------------------------------------------------------------
// SpellDefinition — JSON round-trip
// ---------------------------------------------------------------------------

TEST(SpellDefinitionTest, JsonRoundTrip) {
    SpellDefinition spell;
    spell.id              = "fireball";
    spell.name            = "Fireball";
    spell.level           = 3;
    spell.school          = SpellSchool::Evocation;
    spell.components      = {SpellComponent::Verbal, SpellComponent::Somatic, SpellComponent::Material};
    spell.materialDescription = "Bat guano and sulfur";
    spell.castingTime     = CastingTime::Action;
    spell.rangeInFeet     = 150;
    spell.requiresConcentration = false;
    spell.durationDescription = "Instantaneous";
    spell.resolutionType  = SpellResolutionType::SavingThrow;
    spell.savingThrowAbility = AbilityType::Dexterity;
    spell.halfDamageOnSave = true;
    spell.baseDamage      = DiceExpression::parse("8d6");
    spell.upcastExtraPerSlot = DiceExpression::parse("1d6");
    spell.damageType      = DamageType::Fire;
    spell.description     = "A fireball!";
    spell.classes         = {"sorcerer", "wizard"};

    auto j = spell.toJson();
    auto spell2 = SpellDefinition::fromJson(j);

    EXPECT_EQ(spell2.id,    "fireball");
    EXPECT_EQ(spell2.name,  "Fireball");
    EXPECT_EQ(spell2.level, 3);
    EXPECT_EQ(spell2.school, SpellSchool::Evocation);
    EXPECT_TRUE(spell2.components.count(SpellComponent::Verbal));
    EXPECT_TRUE(spell2.components.count(SpellComponent::Material));
    EXPECT_EQ(spell2.rangeInFeet, 150);
    EXPECT_EQ(spell2.resolutionType, SpellResolutionType::SavingThrow);
    EXPECT_EQ(spell2.savingThrowAbility, AbilityType::Dexterity);
    EXPECT_TRUE(spell2.halfDamageOnSave);
    EXPECT_EQ(spell2.baseDamage.count, 8);
    EXPECT_EQ(static_cast<int>(spell2.baseDamage.die), 6);
    EXPECT_EQ(spell2.upcastExtraPerSlot.count, 1);
    EXPECT_EQ(spell2.damageType, DamageType::Fire);
    EXPECT_EQ(spell2.classes.size(), 2u);
}

// ---------------------------------------------------------------------------
// SpellRegistry
// ---------------------------------------------------------------------------

class SpellRegistryTest : public ::testing::Test {
protected:
    void SetUp() override { SpellRegistry::instance().clear(); }
    void TearDown() override { SpellRegistry::instance().clear(); }

    SpellDefinition makeSpell(const std::string& id, int level,
                               const std::string& cls) {
        SpellDefinition d;
        d.id    = id;
        d.name  = id;
        d.level = level;
        d.classes = {cls};
        return d;
    }
};

TEST_F(SpellRegistryTest, RegisterAndRetrieve) {
    auto& reg = SpellRegistry::instance();
    reg.registerSpell(makeSpell("fire_bolt", 0, "wizard"));
    EXPECT_NE(reg.getSpell("fire_bolt"), nullptr);
    EXPECT_EQ(reg.getSpell("nonexistent"), nullptr);
}

TEST_F(SpellRegistryTest, CountSpells) {
    auto& reg = SpellRegistry::instance();
    reg.registerSpell(makeSpell("a", 0, "wizard"));
    reg.registerSpell(makeSpell("b", 1, "wizard"));
    reg.registerSpell(makeSpell("c", 3, "sorcerer"));
    EXPECT_EQ(reg.count(), 3u);
}

TEST_F(SpellRegistryTest, GetSpellsForClass) {
    auto& reg = SpellRegistry::instance();
    reg.registerSpell(makeSpell("fire_bolt",  0, "wizard"));
    reg.registerSpell(makeSpell("fireball",   3, "wizard"));
    reg.registerSpell(makeSpell("hex",        1, "warlock"));

    auto wizardSpells = reg.getSpellsForClass("wizard");
    EXPECT_EQ(wizardSpells.size(), 2u);

    auto warlockSpells = reg.getSpellsForClass("warlock");
    EXPECT_EQ(warlockSpells.size(), 1u);
}

TEST_F(SpellRegistryTest, GetSpellsOfLevel) {
    auto& reg = SpellRegistry::instance();
    reg.registerSpell(makeSpell("fire_bolt",    0, "wizard"));
    reg.registerSpell(makeSpell("eldritch",     0, "warlock"));
    reg.registerSpell(makeSpell("magic_missile",1, "wizard"));
    reg.registerSpell(makeSpell("fireball",     3, "wizard"));

    EXPECT_EQ(reg.getSpellsOfLevel(0).size(), 2u);
    EXPECT_EQ(reg.getSpellsOfLevel(1).size(), 1u);
    EXPECT_EQ(reg.getSpellsOfLevel(3).size(), 1u);
    EXPECT_EQ(reg.getSpellsOfLevel(9).size(), 0u);
}

TEST_F(SpellRegistryTest, LoadFromFileArray) {
    auto& reg = SpellRegistry::instance();
    int loaded = reg.loadFromFile("resources/spells/cantrips.json");
    EXPECT_TRUE(loaded);
    EXPECT_GE(reg.count(), 6u);
    EXPECT_NE(reg.getSpell("fire_bolt"),    nullptr);
    EXPECT_NE(reg.getSpell("sacred_flame"), nullptr);
}

TEST_F(SpellRegistryTest, LoadFromDirectory) {
    auto& reg = SpellRegistry::instance();
    int count = reg.loadFromDirectory("resources/spells");
    EXPECT_GE(count, 3);  // cantrips, level1, level2, level3
    EXPECT_NE(reg.getSpell("fire_bolt"),    nullptr);
    EXPECT_NE(reg.getSpell("fireball"),     nullptr);
    EXPECT_NE(reg.getSpell("magic_missile"),nullptr);
    EXPECT_NE(reg.getSpell("scorching_ray"),nullptr);
    EXPECT_GE(reg.count(), 20u);
}

TEST_F(SpellRegistryTest, SpellDataFireball) {
    auto& reg = SpellRegistry::instance();
    reg.loadFromFile("resources/spells/level3.json");
    const auto* fb = reg.getSpell("fireball");
    ASSERT_NE(fb, nullptr);
    EXPECT_EQ(fb->level,         3);
    EXPECT_EQ(fb->school,        SpellSchool::Evocation);
    EXPECT_EQ(fb->resolutionType,SpellResolutionType::SavingThrow);
    EXPECT_EQ(fb->savingThrowAbility, AbilityType::Dexterity);
    EXPECT_TRUE(fb->halfDamageOnSave);
    EXPECT_EQ(fb->baseDamage.count, 8);
    EXPECT_EQ(static_cast<int>(fb->baseDamage.die), 6);
    EXPECT_EQ(fb->upcastExtraPerSlot.count, 1);
    EXPECT_EQ(fb->damageType, DamageType::Fire);
}

TEST_F(SpellRegistryTest, SpellDataCureWounds) {
    auto& reg = SpellRegistry::instance();
    reg.loadFromFile("resources/spells/level1.json");
    const auto* cw = reg.getSpell("cure_wounds");
    ASSERT_NE(cw, nullptr);
    EXPECT_EQ(cw->level, 1);
    EXPECT_EQ(cw->resolutionType, SpellResolutionType::AutoHit);
    EXPECT_TRUE(cw->isTouch);
    EXPECT_TRUE(cw->hasHeal());
    EXPECT_FALSE(cw->hasDamage());
    EXPECT_EQ(cw->healDice.count, 1);
    EXPECT_EQ(static_cast<int>(cw->healDice.die), 8);
}
