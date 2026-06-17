#include <gtest/gtest.h>
#include "core/CombatDirector.h"
#include "core/DiceSystem.h"

using namespace Phyxel::Core;

namespace {

std::vector<CombatDirector::Combatant> heroVsGoblin() {
    return {
        {"hero",   /*playerSide*/ true,  /*initBonus*/ 2, /*speed*/ 30},
        {"goblin", /*playerSide*/ false, /*initBonus*/ 1, /*speed*/ 30},
    };
}

} // namespace

class CombatDirectorTest : public ::testing::Test {
protected:
    void SetUp() override { DiceSystem::setSeed(7); }
    void TearDown() override { DiceSystem::setSeed(0); }
    DiceSystem dice;
};

// --- Mode parsing -----------------------------------------------------------

TEST_F(CombatDirectorTest, ModeStringRoundTrip) {
    EXPECT_EQ(combatModeFromString("turn_based"), CombatMode::TurnBased);
    EXPECT_EQ(combatModeFromString("turnbased"),  CombatMode::TurnBased);
    EXPECT_EQ(combatModeFromString("real_time"),  CombatMode::RealTime);
    EXPECT_EQ(combatModeFromString("anything"),   CombatMode::RealTime);  // default
    EXPECT_STREQ(combatModeToString(CombatMode::TurnBased), "turn_based");
    EXPECT_STREQ(combatModeToString(CombatMode::RealTime),  "real_time");
}

TEST_F(CombatDirectorTest, DefaultsToRealTimeAndNotInCombat) {
    CombatDirector d;
    EXPECT_EQ(d.mode(), CombatMode::RealTime);
    EXPECT_FALSE(d.isTurnBased());
    EXPECT_FALSE(d.inCombat());
}

// --- Real-time mode: in-combat flag + sides, but no turn structure ----------

TEST_F(CombatDirectorTest, RealTimeTracksSidesButNoTurns) {
    CombatDirector d;
    d.setMode(CombatMode::RealTime);
    d.beginEncounter(heroVsGoblin(), dice);

    EXPECT_TRUE(d.inCombat());
    EXPECT_TRUE(d.isPlayerSide("hero"));
    EXPECT_FALSE(d.isPlayerSide("goblin"));

    // Turn queries are inert in real-time.
    EXPECT_TRUE(d.currentEntityId().empty());
    EXPECT_EQ(d.currentRound(), 0);
    EXPECT_TRUE(d.advanceTurn().empty());
    EXPECT_FALSE(d.isPlayerTurn());
    EXPECT_FALSE(d.isEntityTurn("hero"));
}

// --- Turn-based mode: initiative + rounds + turn flow -----------------------

TEST_F(CombatDirectorTest, TurnBasedRollsInitiativeAndStartsRoundOne) {
    CombatDirector d;
    d.setMode(CombatMode::TurnBased);
    d.beginEncounter(heroVsGoblin(), dice);

    EXPECT_TRUE(d.inCombat());
    EXPECT_EQ(d.currentRound(), 1);
    EXPECT_FALSE(d.currentEntityId().empty());
    EXPECT_TRUE(d.isEntityTurn(d.currentEntityId()));

    // Whoever's turn it is, isPlayerTurn must agree with their side.
    EXPECT_EQ(d.isPlayerTurn(), d.isPlayerSide(d.currentEntityId()));
}

TEST_F(CombatDirectorTest, AdvanceTurnCyclesAndCountsRounds) {
    CombatDirector d;
    d.setMode(CombatMode::TurnBased);
    d.beginEncounter(heroVsGoblin(), dice);

    std::string first  = d.currentEntityId();
    std::string second = d.advanceTurn();
    EXPECT_NE(second, first);
    EXPECT_EQ(d.currentRound(), 1);

    // Wrapping back to the first participant advances the round.
    std::string wrapped = d.advanceTurn();
    EXPECT_EQ(wrapped, first);
    EXPECT_EQ(d.currentRound(), 2);
}

// --- Side queries -----------------------------------------------------------

TEST_F(CombatDirectorTest, SideCounts) {
    CombatDirector d;
    d.setMode(CombatMode::TurnBased);
    d.beginEncounter(heroVsGoblin(), dice);
    EXPECT_EQ(d.playerSideCount(), 1);
    EXPECT_EQ(d.enemySideCount(), 1);
    EXPECT_FALSE(d.playerSideWiped());
    EXPECT_FALSE(d.enemySideWiped());
}

// --- Removing a combatant auto-ends when a side is wiped --------------------

TEST_F(CombatDirectorTest, RemovingLastEnemyEndsEncounter) {
    CombatDirector d;
    d.setMode(CombatMode::TurnBased);
    d.beginEncounter(heroVsGoblin(), dice);

    d.removeCombatant("goblin");
    // Auto-end clears all combat state (including the side map), so the
    // observable result is simply that we're no longer in combat.
    EXPECT_FALSE(d.inCombat());
    EXPECT_TRUE(d.currentEntityId().empty());
}

TEST_F(CombatDirectorTest, RemovingNonLastDoesNotEnd) {
    CombatDirector d;
    d.setMode(CombatMode::TurnBased);
    d.beginEncounter({
        {"hero",  true,  2, 30},
        {"gob_a", false, 1, 30},
        {"gob_b", false, 1, 30},
    }, dice);

    d.removeCombatant("gob_a");
    EXPECT_TRUE(d.inCombat());
    EXPECT_EQ(d.enemySideCount(), 1);
}

// --- Begin is a no-op while already in combat -------------------------------

TEST_F(CombatDirectorTest, BeginWhileInCombatIsNoOp) {
    CombatDirector d;
    d.setMode(CombatMode::TurnBased);
    d.beginEncounter(heroVsGoblin(), dice);
    int round = d.currentRound();
    d.advanceTurn();

    // Second begin must not reset the encounter.
    d.beginEncounter({{"someone", true, 0, 30}}, dice);
    EXPECT_TRUE(d.inCombat());
    EXPECT_FALSE(d.isPlayerSide("someone"));  // never added
    EXPECT_GE(d.currentRound(), round);
}

// --- Serialization round-trip ----------------------------------------------

TEST_F(CombatDirectorTest, JsonRoundTrip) {
    CombatDirector d;
    d.setMode(CombatMode::TurnBased);
    d.beginEncounter(heroVsGoblin(), dice);
    d.advanceTurn();

    auto j = d.toJson();
    CombatDirector restored;
    restored.fromJson(j);

    EXPECT_EQ(restored.mode(), CombatMode::TurnBased);
    EXPECT_TRUE(restored.inCombat());
    EXPECT_EQ(restored.currentRound(), d.currentRound());
    EXPECT_EQ(restored.currentEntityId(), d.currentEntityId());
    EXPECT_TRUE(restored.isPlayerSide("hero"));
    EXPECT_FALSE(restored.isPlayerSide("goblin"));
}
