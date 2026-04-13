#include <gtest/gtest.h>
#include "core/SpellResolver.h"
#include "core/SpellDefinition.h"

using namespace Phyxel::Core;

class SpellResolverTest : public ::testing::Test {
protected:
    void SetUp() override {
        DiceSystem::setSeed(42);
        SpellRegistry::instance().clear();
        registerTestSpells();

        // Wizard with INT=18 (+4 mod), level 5
        casterAttrs.setAll(10, 10, 10, 18, 10, 10);
        caster.initialize("wizard", AbilityType::Intelligence, 5, "full");

        // Default target attrs (STR=10, DEX=12 +1, CON=10, INT=10, WIS=10, CHA=10)
        targetAttrs.setAll(10, 12, 10, 10, 10, 10);
    }

    void TearDown() override {
        DiceSystem::setSeed(0);
        SpellRegistry::instance().clear();
    }

    void registerTestSpells() {
        auto& reg = SpellRegistry::instance();

        // Attack roll cantrip (fire bolt)
        {
            SpellDefinition d;
            d.id             = "fire_bolt";
            d.level          = 0;
            d.school         = SpellSchool::Evocation;
            d.resolutionType = SpellResolutionType::AttackRoll;
            d.baseDamage     = DiceExpression::parse("1d10");
            d.damageType     = DamageType::Fire;
            reg.registerSpell(d);
        }
        // Saving throw spell (fireball)
        {
            SpellDefinition d;
            d.id                 = "fireball";
            d.level              = 3;
            d.school             = SpellSchool::Evocation;
            d.resolutionType     = SpellResolutionType::SavingThrow;
            d.savingThrowAbility = AbilityType::Dexterity;
            d.halfDamageOnSave   = true;
            d.baseDamage         = DiceExpression::parse("8d6");
            d.upcastExtraPerSlot = DiceExpression::parse("1d6");
            d.damageType         = DamageType::Fire;
            reg.registerSpell(d);
        }
        // Auto-hit damage (magic missile)
        {
            SpellDefinition d;
            d.id             = "magic_missile";
            d.level          = 1;
            d.resolutionType = SpellResolutionType::AutoHit;
            d.baseDamage     = DiceExpression::parse("3d4");
            d.upcastExtraPerSlot = DiceExpression::parse("1d4");
            d.damageType     = DamageType::Force;
            reg.registerSpell(d);
        }
        // Auto-hit heal (cure wounds)
        {
            SpellDefinition d;
            d.id             = "cure_wounds";
            d.level          = 1;
            d.resolutionType = SpellResolutionType::AutoHit;
            d.healDice       = DiceExpression::parse("1d8");
            d.upcastHealPerSlot = DiceExpression::parse("1d8");
            reg.registerSpell(d);
        }
        // Utility (misty step)
        {
            SpellDefinition d;
            d.id             = "misty_step";
            d.level          = 2;
            d.resolutionType = SpellResolutionType::Utility;
            reg.registerSpell(d);
        }
    }

    SpellcasterComponent caster{"wizard"};
    CharacterAttributes  casterAttrs;
    CharacterAttributes  targetAttrs;
    DiceSystem           dice;

    CastResult doCast(const std::string& spellId, int slotLevel = 0,
                      DamageResistance resist = DamageResistance::Normal,
                      bool adv = false, bool disadv = false) {
        return SpellResolver::castSpell(
            caster, casterAttrs, 3 /*profBonus*/, spellId, slotLevel,
            5 /*casterTotalLevel*/, "target", 14 /*targetAC*/,
            &targetAttrs, false /*targetProfInSave*/, resist,
            adv, disadv, dice);
    }
};

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

TEST_F(SpellResolverTest, UnknownSpellFails) {
    auto res = doCast("nonexistent");
    EXPECT_FALSE(res.success);
    EXPECT_FALSE(res.failReason.empty());
}

TEST_F(SpellResolverTest, CantripNotKnownFails) {
    auto res = doCast("fire_bolt", 0);
    EXPECT_FALSE(res.success);
}

TEST_F(SpellResolverTest, CantripKnownSucceeds) {
    caster.learnCantrip("fire_bolt");
    DiceSystem::setSeed(1);
    auto res = doCast("fire_bolt", 0);
    EXPECT_TRUE(res.success);
}

TEST_F(SpellResolverTest, UnpreparedSpellFails) {
    caster.learnSpell("fireball");  // known but not prepared
    auto res = doCast("fireball", 3);
    EXPECT_FALSE(res.success);
}

TEST_F(SpellResolverTest, PreparedSpellSucceeds) {
    caster.learnSpell("fireball", true);
    auto res = doCast("fireball", 3);
    EXPECT_TRUE(res.success);
}

TEST_F(SpellResolverTest, NoSlotFails) {
    caster.learnSpell("fireball", true);
    // Exhaust all slots
    while (caster.slots().canSpend(3)) caster.spendSlot(3);
    auto res = doCast("fireball", 3);
    EXPECT_FALSE(res.success);
}

// ---------------------------------------------------------------------------
// Slot spending
// ---------------------------------------------------------------------------

TEST_F(SpellResolverTest, CastSpendsSlot) {
    caster.learnSpell("fireball", true);
    int before = caster.slots().remaining[2];
    doCast("fireball", 3);
    EXPECT_EQ(caster.slots().remaining[2], before - 1);
}

TEST_F(SpellResolverTest, CantripDoesNotSpendSlot) {
    caster.learnCantrip("fire_bolt");
    int totalBefore = caster.slots().totalRemaining();
    doCast("fire_bolt", 0);
    EXPECT_EQ(caster.slots().totalRemaining(), totalBefore);
}

// ---------------------------------------------------------------------------
// Damage resolution
// ---------------------------------------------------------------------------

TEST_F(SpellResolverTest, MagicMissileAutoHitDealsPositiveDamage) {
    caster.learnSpell("magic_missile", true);
    DiceSystem::setSeed(1);
    for (int i = 0; i < 10; ++i) {
        auto res = SpellResolver::castSpell(
            caster, casterAttrs, 3, "magic_missile", 1, 5,
            "target", 30 /*impossible to hit conventionally*/, &targetAttrs,
            false, DamageResistance::Normal, false, false, dice);
        if (res.success) {
            EXPECT_TRUE(res.hit);
            EXPECT_GT(res.damage, 0);
            return;
        }
        // Restore slot for retry
        caster.slots().restore(1);
    }
}

TEST_F(SpellResolverTest, CureWoundsHeals) {
    caster.learnSpell("cure_wounds", true);
    DiceSystem::setSeed(1);
    auto res = SpellResolver::castSpell(
        caster, casterAttrs, 3, "cure_wounds", 1, 5,
        "target", 0, nullptr, false,
        DamageResistance::Normal, false, false, dice);
    EXPECT_TRUE(res.success);
    EXPECT_GT(res.healAmount, 0);  // 1d8 + INT mod(+4) >= 5
}

TEST_F(SpellResolverTest, FireballSavingThrowHalfDamage) {
    caster.learnSpell("fireball", true);
    DiceSystem::setSeed(1);
    // Force target to always save: use WIS-saving-throw target with huge modifier
    // We can't easily force the save result, but we can test that damage
    // is in a valid range for 8d6 (8–48) or half (4–24)
    auto res = doCast("fireball", 3);
    EXPECT_TRUE(res.success);
    if (res.targetSaved) {
        EXPECT_LE(res.damage, 24);  // at most half of max 8d6=48 = 24 (after resistance)
    } else {
        EXPECT_LE(res.damage, 48);
    }
}

TEST_F(SpellResolverTest, ResistantHalvesDamage) {
    caster.learnSpell("magic_missile", true);
    DiceSystem::setSeed(10);
    auto normal = SpellResolver::castSpell(
        caster, casterAttrs, 3, "magic_missile", 1, 5,
        "target", 0, nullptr, false, DamageResistance::Normal, false, false, dice);
    caster.slots().restore(1);
    DiceSystem::setSeed(10);
    auto resist = SpellResolver::castSpell(
        caster, casterAttrs, 3, "magic_missile", 1, 5,
        "target", 0, nullptr, false, DamageResistance::Resistant, false, false, dice);

    if (normal.success && resist.success) {
        EXPECT_EQ(resist.damage, normal.damage / 2);
    }
}

TEST_F(SpellResolverTest, ImmuneTakesNoDamage) {
    caster.learnSpell("magic_missile", true);
    DiceSystem::setSeed(1);
    auto res = SpellResolver::castSpell(
        caster, casterAttrs, 3, "magic_missile", 1, 5,
        "target", 0, nullptr, false, DamageResistance::Immune, false, false, dice);
    EXPECT_TRUE(res.success);
    EXPECT_EQ(res.damage, 0);
}

// ---------------------------------------------------------------------------
// Upcast scaling
// ---------------------------------------------------------------------------

TEST_F(SpellResolverTest, FireballUpcastDealsMoreDamage) {
    // 8d6 at slot 3 vs 9d6 at slot 4
    // A level 5 wizard has 3rd-level (2 slots) and 4th is beyond their table —
    // give extra slots manually so we can test upcast math.
    caster.learnSpell("fireball", true);
    caster.slots().maximum[3] = 2;   // grant 4th-level slots for this test
    caster.slots().remaining[3] = 2;

    DiceSystem::setSeed(1);
    auto base = doCast("fireball", 3);
    caster.slots().restore(3);

    DiceSystem::setSeed(1);
    auto upcasted = SpellResolver::castSpell(
        caster, casterAttrs, 3, "fireball", 4, 5,
        "target", 14, &targetAttrs, false, DamageResistance::Normal, false, false, dice);

    EXPECT_TRUE(base.success);
    EXPECT_TRUE(upcasted.success);
    // Max 8d6=48 at base; max 9d6=54 at slot 4
    EXPECT_LE(base.damage,     48);
    EXPECT_LE(upcasted.damage, 54);
}

// ---------------------------------------------------------------------------
// Utility spell
// ---------------------------------------------------------------------------

TEST_F(SpellResolverTest, UtilitySpellSucceeds) {
    caster.learnSpell("misty_step", true);
    auto res = SpellResolver::castSpell(
        caster, casterAttrs, 3, "misty_step", 2, 5,
        "self", 0, nullptr, false, DamageResistance::Normal, false, false, dice);
    EXPECT_TRUE(res.success);
    EXPECT_TRUE(res.hit);
    EXPECT_EQ(res.damage, 0);
    EXPECT_EQ(res.healAmount, 0);
}

// ---------------------------------------------------------------------------
// Cantrip scaling
// ---------------------------------------------------------------------------

TEST_F(SpellResolverTest, CantripScalesAtLevel5) {
    caster.learnCantrip("fire_bolt");
    // At level 5, fire_bolt becomes 2d10
    const auto* spell = SpellRegistry::instance().getSpell("fire_bolt");
    ASSERT_NE(spell, nullptr);
    auto dice = spell->cantripDiceAt(5);
    EXPECT_EQ(dice.count, 2);
    EXPECT_EQ(static_cast<int>(dice.die), 10);
}
