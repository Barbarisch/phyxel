#include <gtest/gtest.h>
#include "core/DiceSystem.h"

using namespace Phyxel::Core;

// ---------------------------------------------------------------------------
// DiceExpression parsing
// ---------------------------------------------------------------------------

TEST(DiceExpressionTest, ParseSimple) {
    auto e = DiceExpression::parse("2d6");
    EXPECT_EQ(e.count, 2);
    EXPECT_EQ(e.die, DieType::D6);
    EXPECT_EQ(e.modifier, 0);
}

TEST(DiceExpressionTest, ParseWithPositiveModifier) {
    auto e = DiceExpression::parse("1d8+3");
    EXPECT_EQ(e.count, 1);
    EXPECT_EQ(e.die, DieType::D8);
    EXPECT_EQ(e.modifier, 3);
}

TEST(DiceExpressionTest, ParseWithNegativeModifier) {
    auto e = DiceExpression::parse("3d4-2");
    EXPECT_EQ(e.count, 3);
    EXPECT_EQ(e.die, DieType::D4);
    EXPECT_EQ(e.modifier, -2);
}

TEST(DiceExpressionTest, ParseImpliedOneDie) {
    auto e = DiceExpression::parse("d20");
    EXPECT_EQ(e.count, 1);
    EXPECT_EQ(e.die, DieType::D20);
    EXPECT_EQ(e.modifier, 0);
}

TEST(DiceExpressionTest, ParseAllDieTypes) {
    for (auto [expr, expected] : std::vector<std::pair<std::string, DieType>>{
        {"1d4", DieType::D4}, {"1d6", DieType::D6}, {"1d8", DieType::D8},
        {"1d10", DieType::D10}, {"1d12", DieType::D12},
        {"1d20", DieType::D20}, {"1d100", DieType::D100}
    }) {
        EXPECT_EQ(DiceExpression::parse(expr).die, expected) << "Failed for " << expr;
    }
}

TEST(DiceExpressionTest, ToString) {
    auto e = DiceExpression::parse("2d6+3");
    EXPECT_EQ(e.toString(), "2d6+3");
}

TEST(DiceExpressionTest, ToStringNoModifier) {
    auto e = DiceExpression::parse("1d8");
    EXPECT_EQ(e.toString(), "1d8");
}

TEST(DiceExpressionTest, ToStringNegativeModifier) {
    auto e = DiceExpression::parse("1d6-1");
    EXPECT_EQ(e.toString(), "1d6-1");
}

// ---------------------------------------------------------------------------
// RollResult::describe
// ---------------------------------------------------------------------------

TEST(RollResultTest, DescribeBasic) {
    RollResult r;
    r.dice = {14};
    r.modifier = 3;
    r.total = 17;
    std::string d = r.describe();
    EXPECT_NE(d.find("17"), std::string::npos);
}

// ---------------------------------------------------------------------------
// DiceSystem — deterministic seeded rolls
// ---------------------------------------------------------------------------

class DiceSystemTest : public ::testing::Test {
protected:
    void SetUp() override {
        DiceSystem::setSeed(42);  // deterministic
    }
    void TearDown() override {
        DiceSystem::setSeed(0);   // restore random
    }
};

TEST_F(DiceSystemTest, RollD20InRange) {
    for (int i = 0; i < 100; ++i) {
        auto r = DiceSystem::roll(DieType::D20);
        EXPECT_GE(r.total, 1);
        EXPECT_LE(r.total, 20);
        EXPECT_EQ(r.dice.size(), 1u);
        EXPECT_EQ(r.modifier, 0);
    }
}

TEST_F(DiceSystemTest, RollD6InRange) {
    for (int i = 0; i < 100; ++i) {
        auto r = DiceSystem::roll(DieType::D6);
        EXPECT_GE(r.total, 1);
        EXPECT_LE(r.total, 6);
    }
}

TEST_F(DiceSystemTest, RollWithModifier) {
    // With modifier=5, d20 range becomes [6,25]
    for (int i = 0; i < 100; ++i) {
        auto r = DiceSystem::roll(DieType::D20, 5);
        EXPECT_GE(r.total, 6);
        EXPECT_LE(r.total, 25);
        EXPECT_EQ(r.modifier, 5);
    }
}

TEST_F(DiceSystemTest, CriticalSuccessDetected) {
    // Seed loop until we get a nat-20 — with seed=42 this is deterministic
    bool foundCrit = false;
    for (int i = 0; i < 1000; ++i) {
        auto r = DiceSystem::roll(DieType::D20);
        if (r.dice[0] == 20) {
            EXPECT_TRUE(r.isCriticalSuccess);
            foundCrit = true;
            break;
        }
    }
    EXPECT_TRUE(foundCrit) << "Never rolled a 20 in 1000 attempts (unexpected)";
}

TEST_F(DiceSystemTest, CriticalFailureDetected) {
    bool foundFumble = false;
    for (int i = 0; i < 1000; ++i) {
        auto r = DiceSystem::roll(DieType::D20);
        if (r.dice[0] == 1) {
            EXPECT_TRUE(r.isCriticalFailure);
            foundFumble = true;
            break;
        }
    }
    EXPECT_TRUE(foundFumble) << "Never rolled a 1 in 1000 attempts (unexpected)";
}

TEST_F(DiceSystemTest, NoCritFlagsOnNonD20) {
    for (int i = 0; i < 100; ++i) {
        auto r = DiceSystem::roll(DieType::D6);
        EXPECT_FALSE(r.isCriticalSuccess);
        EXPECT_FALSE(r.isCriticalFailure);
    }
}

TEST_F(DiceSystemTest, RollAdvantageAlwaysHigherOrEqual) {
    for (int i = 0; i < 200; ++i) {
        // Reseed so each pair is fresh but still deterministic overall
        auto adv = DiceSystem::rollAdvantage(DieType::D20);
        EXPECT_TRUE(adv.hadAdvantage);
        EXPECT_FALSE(adv.hadDisadvantage);
        // The kept value must be >= the dropped value
        EXPECT_GE(adv.dice[0], adv.droppedRoll);
        EXPECT_GE(adv.total, 1);
        EXPECT_LE(adv.total, 20);
    }
}

TEST_F(DiceSystemTest, RollDisadvantageAlwaysLowerOrEqual) {
    for (int i = 0; i < 200; ++i) {
        auto dis = DiceSystem::rollDisadvantage(DieType::D20);
        EXPECT_TRUE(dis.hadDisadvantage);
        EXPECT_FALSE(dis.hadAdvantage);
        EXPECT_LE(dis.dice[0], dis.droppedRoll);
    }
}

TEST_F(DiceSystemTest, ExpressionRollResultInRange) {
    // 3d6: min=3, max=18
    for (int i = 0; i < 200; ++i) {
        auto r = DiceSystem::rollExpression("3d6");
        EXPECT_GE(r.total, 3);
        EXPECT_LE(r.total, 18);
        EXPECT_EQ(r.dice.size(), 3u);
    }
}

TEST_F(DiceSystemTest, ExpressionRollWithModifier) {
    // 2d6+3: min=5, max=15
    for (int i = 0; i < 200; ++i) {
        auto r = DiceSystem::rollExpression("2d6+3");
        EXPECT_GE(r.total, 5);
        EXPECT_LE(r.total, 15);
    }
}

TEST_F(DiceSystemTest, CriticalDoublesDice) {
    auto expr = DiceExpression::parse("2d6+3");
    auto r = DiceSystem::rollCritical(expr);
    // 4 dice rolled (doubled), modifier +3 once
    EXPECT_EQ(r.dice.size(), 4u);
    EXPECT_GE(r.total, 7);   // min: 4×1 + 3
    EXPECT_LE(r.total, 27);  // max: 4×6 + 3
}

TEST_F(DiceSystemTest, CheckDC) {
    EXPECT_TRUE(DiceSystem::checkDC(15, 15));   // equal = success
    EXPECT_TRUE(DiceSystem::checkDC(20, 10));   // over = success
    EXPECT_FALSE(DiceSystem::checkDC(9, 10));   // under = fail
    EXPECT_FALSE(DiceSystem::checkDC(1, 2));
}

TEST_F(DiceSystemTest, AverageValueD6) {
    // avg of 1d6 = 3.5
    EXPECT_FLOAT_EQ(DiceSystem::averageValue("1d6"), 3.5f);
}

TEST_F(DiceSystemTest, AverageValueExpression) {
    // 2d8+3: avg = 2×4.5 + 3 = 12.0
    EXPECT_FLOAT_EQ(DiceSystem::averageValue("2d8+3"), 12.0f);
}

TEST_F(DiceSystemTest, AverageValueD20) {
    // avg of 1d20 = 10.5
    EXPECT_FLOAT_EQ(DiceSystem::averageValue(DiceExpression{1, DieType::D20, 0}), 10.5f);
}

TEST_F(DiceSystemTest, SeededReproducibility) {
    DiceSystem::setSeed(999);
    std::vector<int> run1;
    for (int i = 0; i < 20; ++i) run1.push_back(DiceSystem::roll(DieType::D20).total);

    DiceSystem::setSeed(999);
    std::vector<int> run2;
    for (int i = 0; i < 20; ++i) run2.push_back(DiceSystem::roll(DieType::D20).total);

    EXPECT_EQ(run1, run2);
}

TEST(DiceSystemUnseededTest, IsSeededFlag) {
    DiceSystem::setSeed(0);
    EXPECT_FALSE(DiceSystem::isSeeded());
    DiceSystem::setSeed(123);
    EXPECT_TRUE(DiceSystem::isSeeded());
    DiceSystem::setSeed(0);
    EXPECT_FALSE(DiceSystem::isSeeded());
}
