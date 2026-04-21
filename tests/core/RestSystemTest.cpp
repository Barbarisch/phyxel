#include <gtest/gtest.h>
#include "core/RestSystem.h"
#include "core/SpellcasterComponent.h"

using namespace Phyxel::Core;

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class RestSystemTest : public ::testing::Test {
protected:
    void SetUp() override {
        DiceSystem::setSeed(42);
        // Fighter: 10d10 HD, no spellcasting
        rest.registerCharacter("fighter", 80, 10, DieType::D10, 10);
        // Wizard: 5d6 HD, full caster
        rest.registerCharacter("wizard", 30, 5, DieType::D6, 5, "full");
    }
    void TearDown() override { DiceSystem::setSeed(0); }

    RestSystem rest;
    DiceSystem dice;
};

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

TEST_F(RestSystemTest, RegisteredCharacterStartsAtMaxHp) {
    EXPECT_EQ(rest.getCurrentHp("fighter"), 80);
    EXPECT_EQ(rest.getMaxHp("fighter"),     80);
}

TEST_F(RestSystemTest, RegisteredCharacterStartsWithFullHitDice) {
    EXPECT_EQ(rest.getCurrentHitDice("fighter"), 10);
    EXPECT_EQ(rest.getMaxHitDice("fighter"),     10);
}

TEST_F(RestSystemTest, IsRegistered) {
    EXPECT_TRUE(rest.isRegistered("fighter"));
    EXPECT_FALSE(rest.isRegistered("nobody"));
}

TEST_F(RestSystemTest, UnregisteredCharacterReturnsZero) {
    EXPECT_EQ(rest.getCurrentHp("unknown"),      0);
    EXPECT_EQ(rest.getCurrentHitDice("unknown"), 0);
}

// ---------------------------------------------------------------------------
// HP / hit dice setters
// ---------------------------------------------------------------------------

TEST_F(RestSystemTest, SetCurrentHpClampsToMax) {
    rest.setCurrentHp("fighter", 9999);
    EXPECT_EQ(rest.getCurrentHp("fighter"), 80);
}

TEST_F(RestSystemTest, SetCurrentHpClampsToZero) {
    rest.setCurrentHp("fighter", -10);
    EXPECT_EQ(rest.getCurrentHp("fighter"), 0);
}

TEST_F(RestSystemTest, SetCurrentHitDiceClampsToMax) {
    rest.setCurrentHitDice("fighter", 999);
    EXPECT_EQ(rest.getCurrentHitDice("fighter"), 10);
}

TEST_F(RestSystemTest, SetCurrentHitDiceClampsToZero) {
    rest.setCurrentHitDice("fighter", -1);
    EXPECT_EQ(rest.getCurrentHitDice("fighter"), 0);
}

// ---------------------------------------------------------------------------
// Short rest — basic mechanics
// ---------------------------------------------------------------------------

TEST_F(RestSystemTest, ShortRestFailsForUnregistered) {
    auto result = rest.shortRest("nobody", 1, 0, dice);
    EXPECT_FALSE(result.completed);
    EXPECT_FALSE(result.failReason.empty());
}

TEST_F(RestSystemTest, ShortRestSpendZeroDice) {
    rest.setCurrentHp("fighter", 50);
    auto result = rest.shortRest("fighter", 0, 0, dice);
    EXPECT_TRUE(result.completed);
    EXPECT_EQ(result.hitDiceSpent,    0);
    EXPECT_EQ(result.hpRecovered,     0);
    EXPECT_EQ(result.hitDiceRemaining, 10);
    EXPECT_EQ(rest.getCurrentHp("fighter"), 50);
}

TEST_F(RestSystemTest, ShortRestSpendsDiceAndReducesCount) {
    rest.setCurrentHp("fighter", 40);
    auto result = rest.shortRest("fighter", 2, 0, dice);
    EXPECT_TRUE(result.completed);
    EXPECT_EQ(result.hitDiceSpent, 2);
    EXPECT_EQ(result.hitDiceRemaining, 8);
    EXPECT_EQ(rest.getCurrentHitDice("fighter"), 8);
}

TEST_F(RestSystemTest, ShortRestRecoversHp) {
    rest.setCurrentHp("fighter", 40);
    auto result = rest.shortRest("fighter", 2, 3, dice);  // +3 CON
    EXPECT_TRUE(result.completed);
    EXPECT_GE(result.hpRecovered, 0);
    EXPECT_EQ(rest.getCurrentHp("fighter"), 40 + result.hpRecovered);
}

TEST_F(RestSystemTest, ShortRestDoesNotExceedMaxHp) {
    rest.setCurrentHp("fighter", 79);  // 1 below max
    // Even with huge bonus, HP can't exceed 80
    auto result = rest.shortRest("fighter", 5, 10, dice);
    EXPECT_LE(rest.getCurrentHp("fighter"), 80);
}

TEST_F(RestSystemTest, ShortRestFailsWhenNotEnoughHitDice) {
    rest.setCurrentHitDice("fighter", 1);
    auto result = rest.shortRest("fighter", 2, 0, dice);
    EXPECT_FALSE(result.completed);
    EXPECT_FALSE(result.failReason.empty());
    EXPECT_EQ(rest.getCurrentHitDice("fighter"), 1);  // unchanged
}

TEST_F(RestSystemTest, ShortRestNegativeDiceFailsGracefully) {
    auto result = rest.shortRest("fighter", -1, 0, dice);
    EXPECT_FALSE(result.completed);
}

TEST_F(RestSystemTest, ShortRestWithNegativeConModDoesNotHealNegative) {
    rest.setCurrentHp("fighter", 40);
    // With a very negative CON mod, total HP gain should be clamped to 0
    auto result = rest.shortRest("fighter", 1, -100, dice);
    EXPECT_TRUE(result.completed);
    EXPECT_GE(result.hpRecovered, 0);
}

// ---------------------------------------------------------------------------
// Short rest — warlock pact slots
// ---------------------------------------------------------------------------

TEST_F(RestSystemTest, ShortRestRestoresWarlockPactSlots) {
    SpellcasterComponent warlock;
    warlock.initialize("warlock", AbilityType::Charisma, 5, "pact");
    // Level-5 warlock: 2 × 3rd-level pact slots
    warlock.spendSlot(3);  // spend one
    EXPECT_EQ(warlock.slots().remaining[2], 1);

    rest.registerCharacter("warlock", 40, 5, DieType::D8, 5, "pact");
    auto result = rest.shortRest("warlock", 0, 0, dice, &warlock);
    EXPECT_TRUE(result.completed);
    EXPECT_EQ(warlock.slots().remaining[2], 2);  // restored
}

TEST_F(RestSystemTest, ShortRestDoesNotRestoreFullCasterSlots) {
    SpellcasterComponent wizard;
    wizard.initialize("mage", AbilityType::Intelligence, 5, "full");
    wizard.spendSlot(1);
    int before = wizard.slots().remaining[0];

    rest.shortRest("wizard", 0, 0, dice, &wizard);
    // Full casters don't get slots back on short rest
    EXPECT_EQ(wizard.slots().remaining[0], before);
}

// ---------------------------------------------------------------------------
// Long rest
// ---------------------------------------------------------------------------

TEST_F(RestSystemTest, LongRestFailsForUnregistered) {
    auto result = rest.longRest("nobody");
    EXPECT_FALSE(result.completed);
    EXPECT_FALSE(result.failReason.empty());
}

TEST_F(RestSystemTest, LongRestRestoresFullHp) {
    rest.setCurrentHp("fighter", 20);
    auto result = rest.longRest("fighter");
    EXPECT_TRUE(result.completed);
    EXPECT_EQ(rest.getCurrentHp("fighter"), 80);
    EXPECT_EQ(result.hpRecovered, 60);
}

TEST_F(RestSystemTest, LongRestAtFullHpRecoveredZero) {
    auto result = rest.longRest("fighter");
    EXPECT_TRUE(result.completed);
    EXPECT_EQ(result.hpRecovered, 0);
    EXPECT_EQ(rest.getCurrentHp("fighter"), 80);
}

TEST_F(RestSystemTest, LongRestRestoresHalfMaxHitDice) {
    rest.setCurrentHitDice("fighter", 2);  // missing 8 dice
    auto result = rest.longRest("fighter");
    EXPECT_TRUE(result.completed);
    // Restores min(5, 8) = 5 dice (half of 10)
    EXPECT_EQ(result.hitDiceRestored, 5);
    EXPECT_EQ(rest.getCurrentHitDice("fighter"), 7);
}

TEST_F(RestSystemTest, LongRestRestoresMinimumOneDie) {
    // Character with only 1 hit die total — half = 0, but min is 1
    rest.registerCharacter("weakling", 10, 1, DieType::D6, 1);
    rest.setCurrentHitDice("weakling", 0);
    auto result = rest.longRest("weakling");
    EXPECT_EQ(result.hitDiceRestored, 1);
    EXPECT_EQ(rest.getCurrentHitDice("weakling"), 1);
}

TEST_F(RestSystemTest, LongRestDoesNotExceedMaxHitDice) {
    // Already at full hit dice — nothing to restore
    auto result = rest.longRest("fighter");
    EXPECT_EQ(result.hitDiceRestored, 0);
    EXPECT_EQ(rest.getCurrentHitDice("fighter"), 10);
}

TEST_F(RestSystemTest, LongRestCappedByMissing) {
    rest.setCurrentHitDice("fighter", 8);  // missing 2 of 10
    auto result = rest.longRest("fighter");
    // Half of max = 5, but only 2 are missing → restore 2
    EXPECT_EQ(result.hitDiceRestored, 2);
    EXPECT_EQ(rest.getCurrentHitDice("fighter"), 10);
}

TEST_F(RestSystemTest, LongRestRestoresSpellSlots) {
    SpellcasterComponent wizard;
    wizard.initialize("mage", AbilityType::Intelligence, 5, "full");
    wizard.spendSlot(1);
    wizard.spendSlot(1);
    EXPECT_LT(wizard.slots().remaining[0], wizard.slots().maximum[0]);

    auto result = rest.longRest("wizard", &wizard);
    EXPECT_TRUE(result.completed);
    EXPECT_TRUE(result.spellSlotsRestored);
    EXPECT_EQ(wizard.slots().remaining[0], wizard.slots().maximum[0]);
}

TEST_F(RestSystemTest, LongRestWithoutCasterNoSlotsFlag) {
    auto result = rest.longRest("fighter");
    EXPECT_TRUE(result.completed);
    EXPECT_FALSE(result.spellSlotsRestored);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

TEST_F(RestSystemTest, RemoveCharacter) {
    rest.removeCharacter("fighter");
    EXPECT_FALSE(rest.isRegistered("fighter"));
    EXPECT_EQ(rest.getCurrentHp("fighter"), 0);
}

TEST_F(RestSystemTest, Clear) {
    rest.clear();
    EXPECT_FALSE(rest.isRegistered("fighter"));
    EXPECT_FALSE(rest.isRegistered("wizard"));
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

TEST_F(RestSystemTest, JsonRoundTrip) {
    rest.setCurrentHp("fighter",       55);
    rest.setCurrentHitDice("fighter",   7);
    rest.setCurrentHp("wizard",        18);
    rest.setCurrentHitDice("wizard",    3);

    auto j = rest.toJson();
    RestSystem rest2;
    rest2.fromJson(j);

    EXPECT_EQ(rest2.getCurrentHp("fighter"),       55);
    EXPECT_EQ(rest2.getCurrentHitDice("fighter"),   7);
    EXPECT_EQ(rest2.getMaxHp("fighter"),           80);
    EXPECT_EQ(rest2.getMaxHitDice("fighter"),      10);

    EXPECT_EQ(rest2.getCurrentHp("wizard"),        18);
    EXPECT_EQ(rest2.getCurrentHitDice("wizard"),    3);
}
