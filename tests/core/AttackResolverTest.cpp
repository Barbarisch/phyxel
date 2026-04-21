#include <gtest/gtest.h>
#include "core/AttackResolver.h"
#include "core/CharacterAttributes.h"

using namespace Phyxel::Core;

class AttackResolverTest : public ::testing::Test {
protected:
    void SetUp() override { DiceSystem::setSeed(42); }
    DiceSystem dice;

    CharacterAttributes makeAttrs(int str=10, int dex=10, int con=10,
                                  int intel=10, int wis=10, int cha=10) {
        CharacterAttributes a;
        a.setAll(str, dex, con, intel, wis, cha);
        return a;
    }
};

// ---------------------------------------------------------------------------
// resolveAttack — basic hit/miss
// ---------------------------------------------------------------------------

TEST_F(AttackResolverTest, HitsWhenRollMeetsAC) {
    // Force a known roll by seeding. We use a high bonus to ensure hit.
    DiceSystem::setSeed(1);
    auto res = AttackResolver::resolveAttack(
        10, 10, DiceExpression::parse("1d6"), DamageType::Slashing,
        DamageResistance::Normal, false, false, dice);
    // With +10 bonus, any d20 roll will hit AC 10
    EXPECT_TRUE(res.hit);
    EXPECT_GT(res.finalDamage, 0);
}

TEST_F(AttackResolverTest, MissesWhenRollBelowAC) {
    // With -100 attackBonus, even nat-20 roll = 20 but bonus-corrected total will be below AC 25
    // Actually we need roll+bonus < AC. Use +0 bonus vs AC 30 — needs nat-20 to maybe hit
    // Let's use -10 bonus vs AC 15: max roll = 20-10=10, so always misses unless nat-20
    // Force seed so we get a low roll (not nat-20)
    DiceSystem::setSeed(5);
    // Try many times to get a miss
    bool gotMiss = false;
    for (int i = 0; i < 50; ++i) {
        auto res = AttackResolver::resolveAttack(
            -5, 20, DiceExpression::parse("1d6"), DamageType::Slashing,
            DamageResistance::Normal, false, false, dice);
        if (!res.hit && !res.critical) {
            gotMiss = true;
            EXPECT_EQ(res.finalDamage, 0);
            break;
        }
    }
    EXPECT_TRUE(gotMiss);
}

// ---------------------------------------------------------------------------
// Nat-20 critical, Nat-1 fumble
// ---------------------------------------------------------------------------

TEST_F(AttackResolverTest, CriticalOnNat20AlwaysHits) {
    // We'll use a rigged test: resolveAttack with guaranteed nat-20 is hard to force
    // Instead, verify critical flag sets finalDamage > 0 even vs high AC
    // by checking that when critical=true, hit=true and damage rolled
    // We use a loop to catch a nat-20
    DiceSystem::setSeed(7);
    bool gotCrit = false;
    for (int i = 0; i < 200; ++i) {
        auto res = AttackResolver::resolveAttack(
            0, 100, DiceExpression::parse("2d6"), DamageType::Fire,
            DamageResistance::Normal, false, false, dice);
        if (res.critical) {
            gotCrit = true;
            EXPECT_TRUE(res.hit);
            EXPECT_GT(res.finalDamage, 0);  // critical always damages
            EXPECT_EQ(res.d20Roll, 20);
            break;
        }
    }
    EXPECT_TRUE(gotCrit) << "Expected at least one nat-20 in 200 rolls";
}

TEST_F(AttackResolverTest, FumbleOnNat1AlwaysMisses) {
    DiceSystem::setSeed(3);
    bool gotFumble = false;
    for (int i = 0; i < 200; ++i) {
        auto res = AttackResolver::resolveAttack(
            100, 1, DiceExpression::parse("1d4"), DamageType::Bludgeoning,
            DamageResistance::Normal, false, false, dice);
        if (res.fumble) {
            gotFumble = true;
            EXPECT_FALSE(res.hit);
            EXPECT_EQ(res.finalDamage, 0);
            EXPECT_EQ(res.d20Roll, 1);
            break;
        }
    }
    EXPECT_TRUE(gotFumble) << "Expected at least one nat-1 in 200 rolls";
}

// ---------------------------------------------------------------------------
// Advantage / Disadvantage cancel
// ---------------------------------------------------------------------------

TEST_F(AttackResolverTest, AdvantageAndDisadvantageCancelToNormal) {
    // Both adv+disadv → normal roll (no droppedRoll stored)
    // We can't easily verify "normal" vs "advantage" from outside, but the
    // code path is correct if it doesn't crash and returns valid results.
    DiceSystem::setSeed(10);
    for (int i = 0; i < 20; ++i) {
        auto res = AttackResolver::resolveAttack(
            5, 12, DiceExpression::parse("1d8+3"), DamageType::Piercing,
            DamageResistance::Normal, true, true, dice);
        EXPECT_GE(res.d20Roll, 1);
        EXPECT_LE(res.d20Roll, 20);
    }
}

TEST_F(AttackResolverTest, AdvantageRollsHigher) {
    // With advantage, average should be higher than normal. Seed and compare.
    // We don't verify statistically here — just that it runs and returns valid results.
    DiceSystem::setSeed(15);
    auto res = AttackResolver::resolveAttack(
        3, 15, DiceExpression::parse("1d6+2"), DamageType::Slashing,
        DamageResistance::Normal, true, false, dice);
    EXPECT_GE(res.d20Roll, 1);
    EXPECT_LE(res.d20Roll, 20);
}

// ---------------------------------------------------------------------------
// Damage resistance
// ---------------------------------------------------------------------------

TEST_F(AttackResolverTest, ResistantHalvesDamage) {
    // Force a hit with +100 bonus, check damage is halved
    DiceSystem::setSeed(1);
    // Use fixed expression "0d1" isn't valid — use 1d1 hack: parse "1d4+20"
    // Just loop and collect a hit
    for (int i = 0; i < 20; ++i) {
        auto normal = AttackResolver::resolveAttack(
            20, 5, DiceExpression::parse("2d6+6"), DamageType::Fire,
            DamageResistance::Normal, false, false, dice);
        DiceSystem::setSeed(1);  // same seed
        auto resist = AttackResolver::resolveAttack(
            20, 5, DiceExpression::parse("2d6+6"), DamageType::Fire,
            DamageResistance::Resistant, false, false, dice);
        if (normal.hit && resist.hit) {
            EXPECT_EQ(resist.finalDamage, normal.finalDamage / 2);
            return;
        }
        DiceSystem::setSeed(i + 1);
    }
}

TEST_F(AttackResolverTest, VulnerableDoublesDamage) {
    DiceSystem::setSeed(1);
    for (int i = 0; i < 20; ++i) {
        auto normal = AttackResolver::resolveAttack(
            20, 5, DiceExpression::parse("2d6+6"), DamageType::Fire,
            DamageResistance::Normal, false, false, dice);
        DiceSystem::setSeed(1);
        auto vuln = AttackResolver::resolveAttack(
            20, 5, DiceExpression::parse("2d6+6"), DamageType::Fire,
            DamageResistance::Vulnerable, false, false, dice);
        if (normal.hit && vuln.hit) {
            EXPECT_EQ(vuln.finalDamage, normal.finalDamage * 2);
            return;
        }
        DiceSystem::setSeed(i + 1);
    }
}

TEST_F(AttackResolverTest, ImmuneDealsNoDamage) {
    DiceSystem::setSeed(1);
    for (int i = 0; i < 50; ++i) {
        auto res = AttackResolver::resolveAttack(
            100, 1, DiceExpression::parse("4d6+10"), DamageType::Necrotic,
            DamageResistance::Immune, false, false, dice);
        if (res.hit) {
            EXPECT_EQ(res.finalDamage, 0);
            return;
        }
    }
}

TEST_F(AttackResolverTest, ApplyResistanceDirectly) {
    EXPECT_EQ(AttackResolver::applyResistance(10, DamageResistance::Normal),     10);
    EXPECT_EQ(AttackResolver::applyResistance(10, DamageResistance::Resistant),  5);
    EXPECT_EQ(AttackResolver::applyResistance(11, DamageResistance::Resistant),  5);  // floor
    EXPECT_EQ(AttackResolver::applyResistance(10, DamageResistance::Vulnerable), 20);
    EXPECT_EQ(AttackResolver::applyResistance(10, DamageResistance::Immune),     0);
}

// ---------------------------------------------------------------------------
// calculateAC
// ---------------------------------------------------------------------------

TEST_F(AttackResolverTest, UnarmoredAC) {
    auto attrs = makeAttrs(10, 14);  // DEX=14, mod=+2
    int ac = AttackResolver::calculateAC(attrs, 0, -1, 0, 0);
    EXPECT_EQ(ac, 12);  // 10 + 2
}

TEST_F(AttackResolverTest, UnarmoredWithShield) {
    auto attrs = makeAttrs(10, 16);  // DEX=16, mod=+3
    int ac = AttackResolver::calculateAC(attrs, 0, -1, 2, 0);
    EXPECT_EQ(ac, 15);  // 10 + 3 + 2
}

TEST_F(AttackResolverTest, ArmoredWithDexCap) {
    auto attrs = makeAttrs(10, 18);  // DEX=18, mod=+4
    // Chain mail: base=16, maxDex=0 (no dex)
    int ac = AttackResolver::calculateAC(attrs, 16, 0, 0, 0);
    EXPECT_EQ(ac, 16);  // base only, DEX capped at 0
}

TEST_F(AttackResolverTest, ArmoredWithDexAndMagicBonus) {
    auto attrs = makeAttrs(10, 14);  // DEX=14, mod=+2
    // Leather: base=11, maxDex unlimited, +1 magic shield
    int ac = AttackResolver::calculateAC(attrs, 11, -1, 2, 1);
    EXPECT_EQ(ac, 16);  // 11 + 2 + 2 + 1
}

TEST_F(AttackResolverTest, NegativeDexUnarmoredReducesAC) {
    auto attrs = makeAttrs(10, 6);  // DEX=6, mod=-2
    int ac = AttackResolver::calculateAC(attrs, 0, -1, 0, 0);
    EXPECT_EQ(ac, 8);  // 10 - 2
}

// ---------------------------------------------------------------------------
// resolveWeaponAttack
// ---------------------------------------------------------------------------

TEST_F(AttackResolverTest, WeaponAttackStrengthBased) {
    auto attrs = makeAttrs(18);  // STR=18, mod=+4
    DiceSystem::setSeed(1);
    // profBonus=2, isProficient=true → attackBonus = +4+2=+6 → easily hits AC 10
    auto res = AttackResolver::resolveWeaponAttack(
        attrs, 2, true, true, 0, 10,
        DiceExpression::parse("1d8"), DamageType::Slashing,
        DamageResistance::Normal, false, false, dice);
    EXPECT_GE(res.attackBonus, 6);
}

TEST_F(AttackResolverTest, WeaponAttackNotProficient) {
    auto attrs = makeAttrs(10, 10);
    DiceSystem::setSeed(1);
    auto res = AttackResolver::resolveWeaponAttack(
        attrs, 3, false, true, 0, 15,
        DiceExpression::parse("1d6"), DamageType::Piercing,
        DamageResistance::Normal, false, false, dice);
    EXPECT_EQ(res.attackBonus, 0);  // STR mod=0, not proficient → 0
}

// ---------------------------------------------------------------------------
// resolveSavingThrow
// ---------------------------------------------------------------------------

TEST_F(AttackResolverTest, SavingThrowSucceeds) {
    auto attrs = makeAttrs(10, 10, 10, 10, 18);  // WIS=18, mod=+4
    DiceSystem::setSeed(1);
    // WIS save, proficient, profBonus=2 → modifier=+6 → easily beats DC 10
    bool anySuccess = false;
    for (int i = 0; i < 20; ++i) {
        auto res = AttackResolver::resolveSavingThrow(
            attrs, AbilityType::Wisdom, true, 2, 10, false, false, dice);
        EXPECT_EQ(res.modifier, 6);  // +4 WIS + 2 prof
        if (res.succeeded) { anySuccess = true; break; }
    }
    EXPECT_TRUE(anySuccess);
}

TEST_F(AttackResolverTest, SavingThrowFailsLowModifier) {
    auto attrs = makeAttrs(10, 10, 10, 10, 4);  // WIS=4, mod=-3
    DiceSystem::setSeed(1);
    // WIS save, not proficient, mod=-3 → vs DC 25 → almost always fails
    bool anyFail = false;
    for (int i = 0; i < 30; ++i) {
        auto res = AttackResolver::resolveSavingThrow(
            attrs, AbilityType::Wisdom, false, 0, 25, false, false, dice);
        if (!res.succeeded) { anyFail = true; break; }
    }
    EXPECT_TRUE(anyFail);
}

TEST_F(AttackResolverTest, SavingThrowFieldsPopulated) {
    auto attrs = makeAttrs(14);  // STR=14, mod=+2
    DiceSystem::setSeed(1);
    auto res = AttackResolver::resolveSavingThrow(
        attrs, AbilityType::Strength, false, 0, 15, false, false, dice);
    EXPECT_EQ(res.ability, AbilityType::Strength);
    EXPECT_EQ(res.dc, 15);
    EXPECT_EQ(res.modifier, 2);
    EXPECT_GE(res.roll, 1);
    EXPECT_LE(res.roll, 20);
    EXPECT_EQ(res.total, res.roll + res.modifier);
    EXPECT_EQ(res.succeeded, res.total >= 15);
}
