#include <gtest/gtest.h>
#include "core/ConditionSystem.h"

using namespace Phyxel::Core;

class ConditionSystemTest : public ::testing::Test {
protected:
    ConditionSystem cs;

    ConditionInstance makeInst(Condition c, float dur = -1.0f,
                                const std::string& src = "") {
        ConditionInstance ci;
        ci.type              = c;
        ci.durationRemaining = dur;
        ci.sourceEntityId    = src;
        return ci;
    }
};

// ---------------------------------------------------------------------------
// Apply / hasCondition / getConditions
// ---------------------------------------------------------------------------

TEST_F(ConditionSystemTest, ApplyAndHasCondition) {
    cs.applyCondition("alice", makeInst(Condition::Blinded));
    EXPECT_TRUE(cs.hasCondition("alice", Condition::Blinded));
    EXPECT_FALSE(cs.hasCondition("alice", Condition::Charmed));
    EXPECT_FALSE(cs.hasCondition("bob", Condition::Blinded));
}

TEST_F(ConditionSystemTest, MultipleConditionsOnOneEntity) {
    cs.applyCondition("alice", makeInst(Condition::Blinded));
    cs.applyCondition("alice", makeInst(Condition::Poisoned));
    auto conds = cs.getConditions("alice");
    EXPECT_EQ(conds.size(), 2u);
    EXPECT_TRUE(cs.hasCondition("alice", Condition::Blinded));
    EXPECT_TRUE(cs.hasCondition("alice", Condition::Poisoned));
}

TEST_F(ConditionSystemTest, SameConditionMultipleInstances) {
    // Multiple sources of same condition stack as separate instances
    cs.applyCondition("alice", makeInst(Condition::Frightened, -1, "goblin"));
    cs.applyCondition("alice", makeInst(Condition::Frightened, -1, "dragon"));
    auto* instances = cs.getInstances("alice");
    ASSERT_NE(instances, nullptr);
    EXPECT_EQ(instances->size(), 2u);
    // getConditions deduplicates
    auto conds = cs.getConditions("alice");
    EXPECT_EQ(conds.size(), 1u);
    EXPECT_EQ(conds[0], Condition::Frightened);
}

// ---------------------------------------------------------------------------
// Remove
// ---------------------------------------------------------------------------

TEST_F(ConditionSystemTest, RemoveCondition) {
    cs.applyCondition("alice", makeInst(Condition::Blinded));
    cs.applyCondition("alice", makeInst(Condition::Poisoned));
    cs.removeCondition("alice", Condition::Blinded);
    EXPECT_FALSE(cs.hasCondition("alice", Condition::Blinded));
    EXPECT_TRUE(cs.hasCondition("alice", Condition::Poisoned));
}

TEST_F(ConditionSystemTest, RemoveAllFromSource) {
    cs.applyCondition("alice", makeInst(Condition::Frightened, -1, "wizard"));
    cs.applyCondition("alice", makeInst(Condition::Blinded,    -1, "wizard"));
    cs.applyCondition("alice", makeInst(Condition::Poisoned,   -1, "goblin"));
    cs.removeAllFromSource("alice", "wizard");
    EXPECT_FALSE(cs.hasCondition("alice", Condition::Frightened));
    EXPECT_FALSE(cs.hasCondition("alice", Condition::Blinded));
    EXPECT_TRUE(cs.hasCondition("alice",  Condition::Poisoned));
}

TEST_F(ConditionSystemTest, ClearAllConditions) {
    cs.applyCondition("alice", makeInst(Condition::Blinded));
    cs.applyCondition("alice", makeInst(Condition::Stunned));
    cs.clearAllConditions("alice");
    EXPECT_FALSE(cs.hasCondition("alice", Condition::Blinded));
    EXPECT_FALSE(cs.hasCondition("alice", Condition::Stunned));
    EXPECT_EQ(cs.getConditions("alice").size(), 0u);
}

TEST_F(ConditionSystemTest, RemoveNonExistentIsNoOp) {
    EXPECT_NO_THROW(cs.removeCondition("nobody", Condition::Blinded));
    EXPECT_NO_THROW(cs.removeAllFromSource("nobody", "src"));
}

// ---------------------------------------------------------------------------
// Duration expiry
// ---------------------------------------------------------------------------

TEST_F(ConditionSystemTest, TimedConditionExpires) {
    cs.applyCondition("alice", makeInst(Condition::Poisoned, 1.0f));
    EXPECT_TRUE(cs.hasCondition("alice", Condition::Poisoned));
    cs.update(0.5f);
    EXPECT_TRUE(cs.hasCondition("alice", Condition::Poisoned));
    cs.update(0.6f);  // total > 1.0
    EXPECT_FALSE(cs.hasCondition("alice", Condition::Poisoned));
}

TEST_F(ConditionSystemTest, PermanentConditionDoesNotExpire) {
    cs.applyCondition("alice", makeInst(Condition::Blinded, -1.0f));
    cs.update(9999.0f);
    EXPECT_TRUE(cs.hasCondition("alice", Condition::Blinded));
}

TEST_F(ConditionSystemTest, MixedDurationsExpireCorrectly) {
    cs.applyCondition("alice", makeInst(Condition::Blinded,  -1.0f));  // permanent
    cs.applyCondition("alice", makeInst(Condition::Poisoned, 0.5f));   // timed
    cs.update(1.0f);
    EXPECT_TRUE(cs.hasCondition("alice",  Condition::Blinded));
    EXPECT_FALSE(cs.hasCondition("alice", Condition::Poisoned));
}

// ---------------------------------------------------------------------------
// isIncapacitated
// ---------------------------------------------------------------------------

TEST_F(ConditionSystemTest, IsIncapacitatedFromParalyzed) {
    cs.applyCondition("alice", makeInst(Condition::Paralyzed));
    EXPECT_TRUE(cs.isIncapacitated("alice"));
}

TEST_F(ConditionSystemTest, IsIncapacitatedFromStunned) {
    cs.applyCondition("alice", makeInst(Condition::Stunned));
    EXPECT_TRUE(cs.isIncapacitated("alice"));
}

TEST_F(ConditionSystemTest, IsIncapacitatedFromUnconscious) {
    cs.applyCondition("alice", makeInst(Condition::Unconscious));
    EXPECT_TRUE(cs.isIncapacitated("alice"));
}

TEST_F(ConditionSystemTest, IsIncapacitatedFromPetrified) {
    cs.applyCondition("alice", makeInst(Condition::Petrified));
    EXPECT_TRUE(cs.isIncapacitated("alice"));
}

TEST_F(ConditionSystemTest, IsIncapacitatedFromIncapacitated) {
    cs.applyCondition("alice", makeInst(Condition::Incapacitated));
    EXPECT_TRUE(cs.isIncapacitated("alice"));
}

TEST_F(ConditionSystemTest, BlindedNotIncapacitated) {
    cs.applyCondition("alice", makeInst(Condition::Blinded));
    EXPECT_FALSE(cs.isIncapacitated("alice"));
}

// ---------------------------------------------------------------------------
// autoFailsSave
// ---------------------------------------------------------------------------

TEST_F(ConditionSystemTest, ParalyzedAutoFailsStrDex) {
    cs.applyCondition("alice", makeInst(Condition::Paralyzed));
    EXPECT_TRUE(cs.autoFailsSave("alice", AbilityType::Strength));
    EXPECT_TRUE(cs.autoFailsSave("alice", AbilityType::Dexterity));
    EXPECT_FALSE(cs.autoFailsSave("alice", AbilityType::Constitution));
    EXPECT_FALSE(cs.autoFailsSave("alice", AbilityType::Wisdom));
}

TEST_F(ConditionSystemTest, UnconsciousAutoFailsStrDex) {
    cs.applyCondition("alice", makeInst(Condition::Unconscious));
    EXPECT_TRUE(cs.autoFailsSave("alice", AbilityType::Strength));
    EXPECT_TRUE(cs.autoFailsSave("alice", AbilityType::Dexterity));
}

TEST_F(ConditionSystemTest, BlindedDoesNotAutoFail) {
    cs.applyCondition("alice", makeInst(Condition::Blinded));
    EXPECT_FALSE(cs.autoFailsSave("alice", AbilityType::Strength));
    EXPECT_FALSE(cs.autoFailsSave("alice", AbilityType::Dexterity));
}

// ---------------------------------------------------------------------------
// effectiveSpeed
// ---------------------------------------------------------------------------

TEST_F(ConditionSystemTest, GrappledZeroSpeed) {
    cs.applyCondition("alice", makeInst(Condition::Grappled));
    EXPECT_EQ(cs.effectiveSpeed("alice", 30), 0);
}

TEST_F(ConditionSystemTest, RestrainedZeroSpeed) {
    cs.applyCondition("alice", makeInst(Condition::Restrained));
    EXPECT_EQ(cs.effectiveSpeed("alice", 30), 0);
}

TEST_F(ConditionSystemTest, UnconsciousZeroSpeed) {
    cs.applyCondition("alice", makeInst(Condition::Unconscious));
    EXPECT_EQ(cs.effectiveSpeed("alice", 30), 0);
}

TEST_F(ConditionSystemTest, NoConditionsBaseSpeed) {
    EXPECT_EQ(cs.effectiveSpeed("alice", 30), 30);
}

TEST_F(ConditionSystemTest, ExhaustionLevel2HalvesSpeed) {
    cs.addExhaustionLevel("alice");
    cs.addExhaustionLevel("alice");
    EXPECT_EQ(cs.effectiveSpeed("alice", 30), 15);
}

TEST_F(ConditionSystemTest, ExhaustionLevel5ZeroSpeed) {
    for (int i = 0; i < 5; ++i) cs.addExhaustionLevel("alice");
    EXPECT_EQ(cs.effectiveSpeed("alice", 30), 0);
}

// ---------------------------------------------------------------------------
// Advantage / disadvantage on attacks
// ---------------------------------------------------------------------------

TEST_F(ConditionSystemTest, InvisibleAttackerHasAdvantage) {
    cs.applyCondition("alice", makeInst(Condition::Invisible));
    EXPECT_TRUE(cs.attackerHasAdvantageOn("alice", "bob", AttackContext::Melee));
}

TEST_F(ConditionSystemTest, BlindedTargetGrantsAdvantage) {
    cs.applyCondition("bob", makeInst(Condition::Blinded));
    EXPECT_TRUE(cs.attackerHasAdvantageOn("alice", "bob", AttackContext::Melee));
}

TEST_F(ConditionSystemTest, ParalyzedTargetGrantsAdvantage) {
    cs.applyCondition("bob", makeInst(Condition::Paralyzed));
    EXPECT_TRUE(cs.attackerHasAdvantageOn("alice", "bob", AttackContext::Ranged));
}

TEST_F(ConditionSystemTest, ProneMeleeGrantsAdvantage) {
    cs.applyCondition("bob", makeInst(Condition::Prone));
    EXPECT_TRUE(cs.attackerHasAdvantageOn("alice", "bob", AttackContext::Melee));
    EXPECT_FALSE(cs.attackerHasAdvantageOn("alice", "bob", AttackContext::Ranged));
}

TEST_F(ConditionSystemTest, BlindedAttackerHasDisadvantage) {
    cs.applyCondition("alice", makeInst(Condition::Blinded));
    EXPECT_TRUE(cs.attackerHasDisadvantageOn("alice", "bob", AttackContext::Melee));
}

TEST_F(ConditionSystemTest, PoisonedAttackerHasDisadvantage) {
    cs.applyCondition("alice", makeInst(Condition::Poisoned));
    EXPECT_TRUE(cs.attackerHasDisadvantageOn("alice", "bob", AttackContext::Melee));
}

TEST_F(ConditionSystemTest, InvisibleTargetGivesAttackerDisadvantage) {
    cs.applyCondition("bob", makeInst(Condition::Invisible));
    EXPECT_TRUE(cs.attackerHasDisadvantageOn("alice", "bob", AttackContext::Melee));
}

TEST_F(ConditionSystemTest, ProneAttackerMeleeDisadvantage) {
    cs.applyCondition("alice", makeInst(Condition::Prone));
    EXPECT_TRUE(cs.attackerHasDisadvantageOn("alice", "bob", AttackContext::Melee));
    EXPECT_FALSE(cs.attackerHasDisadvantageOn("alice", "bob", AttackContext::Ranged));
}

TEST_F(ConditionSystemTest, ProneTargetRangedDisadvantage) {
    cs.applyCondition("bob", makeInst(Condition::Prone));
    EXPECT_TRUE(cs.attackerHasDisadvantageOn("alice", "bob", AttackContext::Ranged));
    EXPECT_FALSE(cs.attackerHasDisadvantageOn("alice", "bob", AttackContext::Melee));
}

TEST_F(ConditionSystemTest, MeleeAutocritsParalyzed) {
    cs.applyCondition("bob", makeInst(Condition::Paralyzed));
    EXPECT_TRUE(cs.meleeAutocritsAgainst("bob"));
}

TEST_F(ConditionSystemTest, MeleeAutocritsUnconscious) {
    cs.applyCondition("bob", makeInst(Condition::Unconscious));
    EXPECT_TRUE(cs.meleeAutocritsAgainst("bob"));
}

TEST_F(ConditionSystemTest, BlindedDoesNotAutocrit) {
    cs.applyCondition("bob", makeInst(Condition::Blinded));
    EXPECT_FALSE(cs.meleeAutocritsAgainst("bob"));
}

// ---------------------------------------------------------------------------
// Exhaustion
// ---------------------------------------------------------------------------

TEST_F(ConditionSystemTest, ExhaustionStartsAtZero) {
    EXPECT_EQ(cs.exhaustionLevel("alice"), 0);
    EXPECT_FALSE(cs.exhaustionDisadvantageOnChecks("alice"));
}

TEST_F(ConditionSystemTest, AddExhaustionLevel) {
    cs.addExhaustionLevel("alice");
    EXPECT_EQ(cs.exhaustionLevel("alice"), 1);
    EXPECT_TRUE(cs.exhaustionDisadvantageOnChecks("alice"));
    EXPECT_FALSE(cs.exhaustionDisadvantageOnAttacks("alice"));
}

TEST_F(ConditionSystemTest, ExhaustionLevel3DisadvantageOnAttacks) {
    for (int i = 0; i < 3; ++i) cs.addExhaustionLevel("alice");
    EXPECT_TRUE(cs.exhaustionDisadvantageOnAttacks("alice"));
    EXPECT_TRUE(cs.exhaustionDisadvantageOnSaves("alice"));
}

TEST_F(ConditionSystemTest, ExhaustionCapAt6) {
    for (int i = 0; i < 10; ++i) cs.addExhaustionLevel("alice");
    EXPECT_EQ(cs.exhaustionLevel("alice"), 6);
    EXPECT_TRUE(cs.exhaustionDead("alice"));
}

TEST_F(ConditionSystemTest, RemoveExhaustionLevel) {
    cs.addExhaustionLevel("alice");
    cs.addExhaustionLevel("alice");
    cs.removeExhaustionLevel("alice");
    EXPECT_EQ(cs.exhaustionLevel("alice"), 1);
    cs.removeExhaustionLevel("alice");
    EXPECT_EQ(cs.exhaustionLevel("alice"), 0);
}

TEST_F(ConditionSystemTest, ExhaustionLevel4ReducesMaxHP) {
    for (int i = 0; i < 4; ++i) cs.addExhaustionLevel("alice");
    EXPECT_TRUE(cs.exhaustionReducesMaxHP("alice"));
}

// ---------------------------------------------------------------------------
// removeEntity / clear
// ---------------------------------------------------------------------------

TEST_F(ConditionSystemTest, RemoveEntityClearsConditionsAndExhaustion) {
    cs.applyCondition("alice", makeInst(Condition::Blinded));
    cs.addExhaustionLevel("alice");
    cs.removeEntity("alice");
    EXPECT_FALSE(cs.hasCondition("alice", Condition::Blinded));
    EXPECT_EQ(cs.exhaustionLevel("alice"), 0);
}

TEST_F(ConditionSystemTest, ClearAll) {
    cs.applyCondition("alice", makeInst(Condition::Stunned));
    cs.applyCondition("bob",   makeInst(Condition::Poisoned));
    cs.addExhaustionLevel("alice");
    cs.clear();
    EXPECT_FALSE(cs.hasCondition("alice", Condition::Stunned));
    EXPECT_FALSE(cs.hasCondition("bob",   Condition::Poisoned));
    EXPECT_EQ(cs.exhaustionLevel("alice"), 0);
}

// ---------------------------------------------------------------------------
// Name helpers
// ---------------------------------------------------------------------------

TEST_F(ConditionSystemTest, ConditionNameRoundTrip) {
    const Condition conditions[] = {
        Condition::Blinded, Condition::Charmed, Condition::Paralyzed,
        Condition::Exhausted, Condition::Unconscious
    };
    for (auto c : conditions) {
        const char* name = conditionName(c);
        EXPECT_EQ(conditionFromString(name), c) << "Round-trip failed for: " << name;
    }
}

TEST_F(ConditionSystemTest, ConditionFromStringCaseInsensitive) {
    EXPECT_EQ(conditionFromString("blinded"),   Condition::Blinded);
    EXPECT_EQ(conditionFromString("BLINDED"),   Condition::Blinded);
    EXPECT_EQ(conditionFromString("Poisoned"),  Condition::Poisoned);
}

// ---------------------------------------------------------------------------
// JSON serialization
// ---------------------------------------------------------------------------

TEST_F(ConditionSystemTest, JsonRoundTrip) {
    cs.applyCondition("alice", makeInst(Condition::Blinded, -1.0f, "wizard"));
    cs.applyCondition("alice", makeInst(Condition::Poisoned, 5.0f, ""));
    cs.applyCondition("bob",   makeInst(Condition::Stunned));
    cs.addExhaustionLevel("alice");
    cs.addExhaustionLevel("alice");

    auto j = cs.toJson();

    ConditionSystem cs2;
    cs2.fromJson(j);

    EXPECT_TRUE(cs2.hasCondition("alice", Condition::Blinded));
    EXPECT_TRUE(cs2.hasCondition("alice", Condition::Poisoned));
    EXPECT_TRUE(cs2.hasCondition("bob",   Condition::Stunned));
    EXPECT_EQ(cs2.exhaustionLevel("alice"), 2);
    EXPECT_EQ(cs2.exhaustionLevel("bob"),   0);

    auto* instances = cs2.getInstances("alice");
    ASSERT_NE(instances, nullptr);
    EXPECT_EQ(instances->size(), 2u);
}

TEST_F(ConditionSystemTest, ConditionInstanceJsonRoundTrip) {
    ConditionInstance ci;
    ci.type              = Condition::Frightened;
    ci.durationRemaining = 3.5f;
    ci.sourceEntityId    = "dragon";
    ci.sourceSpellId     = "fear_spell";
    ci.description       = "Terrified by dragon";

    auto j   = ci.toJson();
    auto ci2 = ConditionInstance::fromJson(j);

    EXPECT_EQ(ci2.type,              Condition::Frightened);
    EXPECT_FLOAT_EQ(ci2.durationRemaining, 3.5f);
    EXPECT_EQ(ci2.sourceEntityId,    "dragon");
    EXPECT_EQ(ci2.sourceSpellId,     "fear_spell");
    EXPECT_EQ(ci2.description,       "Terrified by dragon");
}
