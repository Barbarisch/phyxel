#include <gtest/gtest.h>
#include "core/InitiativeTracker.h"
#include "core/DiceSystem.h"

using namespace Phyxel::Core;

class InitiativeTrackerTest : public ::testing::Test {
protected:
    void SetUp() override { DiceSystem::setSeed(7); }
    void TearDown() override { DiceSystem::setSeed(0); }
    DiceSystem dice;
};

TEST_F(InitiativeTrackerTest, StartCombatRegistersEntities) {
    InitiativeTracker t;
    t.startCombat({"alice","bob","charlie"});
    EXPECT_TRUE(t.isCombatActive());
    EXPECT_EQ(t.currentRound(), 1);
    EXPECT_EQ(t.turnOrder().size(), 3u);
}

TEST_F(InitiativeTrackerTest, NotActiveBeforeStart) {
    InitiativeTracker t;
    EXPECT_FALSE(t.isCombatActive());
}

TEST_F(InitiativeTrackerTest, RollInitiativeStoresResult) {
    InitiativeTracker t;
    t.startCombat({"alice"});
    t.rollInitiative("alice", 3, dice);
    const auto* p = t.find("alice");
    ASSERT_NE(p, nullptr);
    EXPECT_GE(p->initiativeRoll, 4);   // d20 ≥ 1, +3 modifier ≥ 4
    EXPECT_LE(p->initiativeRoll, 23);  // d20 ≤ 20, +3 ≤ 23
    EXPECT_EQ(p->initiativeModifier, 3);
}

TEST_F(InitiativeTrackerTest, SetInitiativeManual) {
    InitiativeTracker t;
    t.startCombat({"alice","bob"});
    t.setInitiative("alice", 18, 3);
    t.setInitiative("bob",   12, 1);
    t.sortOrder();

    EXPECT_EQ(t.currentEntityId(), "alice");  // higher wins
}

TEST_F(InitiativeTrackerTest, SortOrderHighestFirst) {
    InitiativeTracker t;
    t.startCombat({"a","b","c","d"});
    t.setInitiative("a", 10);
    t.setInitiative("b", 20);
    t.setInitiative("c",  5);
    t.setInitiative("d", 15);
    t.sortOrder();

    EXPECT_EQ(t.turnOrder()[0].entityId, "b");
    EXPECT_EQ(t.turnOrder()[1].entityId, "d");
    EXPECT_EQ(t.turnOrder()[2].entityId, "a");
    EXPECT_EQ(t.turnOrder()[3].entityId, "c");
}

TEST_F(InitiativeTrackerTest, TiebreakByModifier) {
    InitiativeTracker t;
    t.startCombat({"alice","bob"});
    t.setInitiative("alice", 15, 2);  // same roll, higher mod
    t.setInitiative("bob",   15, 0);
    t.sortOrder();
    EXPECT_EQ(t.currentEntityId(), "alice");
}

TEST_F(InitiativeTrackerTest, EndTurnAdvancesToNext) {
    InitiativeTracker t;
    t.startCombat({"a","b","c"});
    t.setInitiative("a", 20); t.setInitiative("b", 15); t.setInitiative("c", 10);
    t.sortOrder();

    EXPECT_EQ(t.currentEntityId(), "a");
    t.endTurn();
    EXPECT_EQ(t.currentEntityId(), "b");
    t.endTurn();
    EXPECT_EQ(t.currentEntityId(), "c");
}

TEST_F(InitiativeTrackerTest, EndTurnWrapsAndIncrementsRound) {
    InitiativeTracker t;
    t.startCombat({"a","b"});
    t.setInitiative("a", 20); t.setInitiative("b", 10);
    t.sortOrder();

    t.endTurn();  // a → b
    EXPECT_EQ(t.currentRound(), 1);
    t.endTurn();  // b → a (new round)
    EXPECT_EQ(t.currentRound(), 2);
    EXPECT_EQ(t.currentEntityId(), "a");
}

TEST_F(InitiativeTrackerTest, EndTurnResetsBudget) {
    InitiativeTracker t;
    t.startCombat({"a","b"}, 30);
    t.setInitiative("a", 20); t.setInitiative("b", 10);
    t.sortOrder();

    // Spend all of a's budget
    t.currentParticipant().budget.spendAction();
    t.currentParticipant().budget.spendMovement(30);

    t.endTurn();  // now b's turn — b's budget should be fresh
    EXPECT_TRUE(t.currentParticipant().budget.canAct());
    EXPECT_EQ(t.currentParticipant().budget.movementRemaining, 30);
}

TEST_F(InitiativeTrackerTest, IsEntityTurn) {
    InitiativeTracker t;
    t.startCombat({"a","b"});
    t.setInitiative("a", 20); t.setInitiative("b", 10);
    t.sortOrder();

    EXPECT_TRUE(t.isEntityTurn("a"));
    EXPECT_FALSE(t.isEntityTurn("b"));
}

TEST_F(InitiativeTrackerTest, SurprisedEntitySkipsFirstTurn) {
    InitiativeTracker t;
    t.startCombat({"a","b"});
    t.setInitiative("a", 20); t.setInitiative("b", 10);
    t.setSurprised("b", true);
    t.sortOrder();

    // a acts
    EXPECT_EQ(t.currentEntityId(), "a");
    t.endTurn();
    // b is surprised — skip to round 2 start which is back to a
    EXPECT_EQ(t.currentEntityId(), "a");
    EXPECT_EQ(t.currentRound(), 2);
}

TEST_F(InitiativeTrackerTest, ReactionUsage) {
    InitiativeTracker t;
    t.startCombat({"a"});

    EXPECT_TRUE(t.canReact("a"));
    EXPECT_TRUE(t.useReaction("a"));
    EXPECT_FALSE(t.canReact("a"));
    EXPECT_FALSE(t.useReaction("a"));  // already used
}

TEST_F(InitiativeTrackerTest, RemoveParticipantWhileActive) {
    InitiativeTracker t;
    t.startCombat({"a","b","c"});
    t.setInitiative("a", 20); t.setInitiative("b", 15); t.setInitiative("c", 10);
    t.sortOrder();

    EXPECT_EQ(t.turnOrder().size(), 3u);
    t.removeParticipant("b");
    EXPECT_EQ(t.turnOrder().size(), 2u);
    EXPECT_EQ(t.find("b"), nullptr);
    EXPECT_NE(t.find("a"), nullptr);
    EXPECT_NE(t.find("c"), nullptr);
}

TEST_F(InitiativeTrackerTest, EndCombatClearsState) {
    InitiativeTracker t;
    t.startCombat({"a","b"});
    t.endCombat();
    EXPECT_FALSE(t.isCombatActive());
    EXPECT_EQ(t.turnOrder().size(), 0u);
    EXPECT_EQ(t.currentRound(), 0);
}

TEST_F(InitiativeTrackerTest, JsonRoundTrip) {
    InitiativeTracker t;
    t.startCombat({"a","b"});
    t.setInitiative("a", 18, 2);
    t.setInitiative("b", 12, 0);
    t.sortOrder();
    t.currentParticipant().budget.spendAction();

    auto j = t.toJson();
    InitiativeTracker t2;
    t2.fromJson(j);

    EXPECT_TRUE(t2.isCombatActive());
    EXPECT_EQ(t2.currentRound(), 1);
    EXPECT_EQ(t2.currentEntityId(), "a");
    EXPECT_FALSE(t2.currentParticipant().budget.canAct());
    EXPECT_EQ(t2.turnOrder().size(), 2u);
}
