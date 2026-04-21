#include <gtest/gtest.h>
#include "core/ActionEconomy.h"

using namespace Phyxel::Core;

TEST(ActionEconomyTest, DefaultBudgetAllAvailable) {
    ActionBudget b;
    EXPECT_TRUE(b.canAct());
    EXPECT_TRUE(b.canBonusAct());
    EXPECT_TRUE(b.canReact());
    EXPECT_TRUE(b.canMove());
    EXPECT_TRUE(b.canFreeInteract());
    EXPECT_EQ(b.movementRemaining, 30);
    EXPECT_FALSE(b.isExhausted());
}

TEST(ActionEconomyTest, SpendAction) {
    ActionBudget b;
    EXPECT_TRUE(b.spendAction());
    EXPECT_FALSE(b.canAct());
    EXPECT_FALSE(b.spendAction());  // already spent
}

TEST(ActionEconomyTest, SpendBonusAction) {
    ActionBudget b;
    EXPECT_TRUE(b.spendBonusAction());
    EXPECT_FALSE(b.canBonusAct());
    EXPECT_FALSE(b.spendBonusAction());
}

TEST(ActionEconomyTest, SpendReaction) {
    ActionBudget b;
    EXPECT_TRUE(b.spendReaction());
    EXPECT_FALSE(b.canReact());
    EXPECT_FALSE(b.spendReaction());
}

TEST(ActionEconomyTest, SpendFreeObject) {
    ActionBudget b;
    EXPECT_TRUE(b.spendFreeObject());
    EXPECT_FALSE(b.canFreeInteract());
    EXPECT_FALSE(b.spendFreeObject());
}

TEST(ActionEconomyTest, SpendMovementPartial) {
    ActionBudget b;
    b.movementRemaining = 30;
    int spent = b.spendMovement(10);
    EXPECT_EQ(spent, 10);
    EXPECT_EQ(b.movementRemaining, 20);
    EXPECT_TRUE(b.canMove());
}

TEST(ActionEconomyTest, SpendMovementAll) {
    ActionBudget b;
    b.movementRemaining = 30;
    int spent = b.spendMovement(30);
    EXPECT_EQ(spent, 30);
    EXPECT_EQ(b.movementRemaining, 0);
    EXPECT_FALSE(b.canMove());
}

TEST(ActionEconomyTest, SpendMovementClampsAtZero) {
    ActionBudget b;
    b.movementRemaining = 10;
    int spent = b.spendMovement(50);
    EXPECT_EQ(spent, 10);
    EXPECT_EQ(b.movementRemaining, 0);
}

TEST(ActionEconomyTest, IsExhaustedWhenAllSpent) {
    ActionBudget b;
    b.spendAction();
    b.spendBonusAction();
    b.spendMovement(30);
    EXPECT_TRUE(b.isExhausted());
}

TEST(ActionEconomyTest, ResetRestoresAll) {
    ActionBudget b;
    b.spendAction();
    b.spendBonusAction();
    b.spendReaction();
    b.spendMovement(30);
    b.reset(35);
    EXPECT_TRUE(b.canAct());
    EXPECT_TRUE(b.canBonusAct());
    EXPECT_TRUE(b.canReact());
    EXPECT_EQ(b.movementRemaining, 35);
}

TEST(ActionEconomyTest, DashDoublesMovement) {
    ActionBudget b;
    b.movementRemaining = 30;
    b.applyDash(30);
    EXPECT_EQ(b.movementRemaining, 60);
}

TEST(ActionEconomyTest, JsonRoundTrip) {
    ActionBudget b;
    b.spendAction();
    b.spendMovement(15);
    auto j = b.toJson();
    ActionBudget b2;
    b2.fromJson(j);
    EXPECT_FALSE(b2.canAct());
    EXPECT_TRUE(b2.canBonusAct());
    EXPECT_EQ(b2.movementRemaining, 15);
}
