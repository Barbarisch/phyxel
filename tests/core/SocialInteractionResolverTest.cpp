#include <gtest/gtest.h>
#include "core/SocialInteractionResolver.h"

using namespace Phyxel::Core;

// ---------------------------------------------------------------------------
// Name helpers
// ---------------------------------------------------------------------------

TEST(SocialInteractionResolverTest, SkillNameRoundTrip) {
    const SocialSkill skills[] = {
        SocialSkill::Persuasion, SocialSkill::Deception,
        SocialSkill::Intimidation, SocialSkill::Insight,
        SocialSkill::Performance
    };
    for (auto s : skills) {
        const char* n = socialSkillName(s);
        EXPECT_EQ(socialSkillFromString(n), s) << "Failed for: " << n;
    }
}

TEST(SocialInteractionResolverTest, SkillFromStringCaseInsensitive) {
    EXPECT_EQ(socialSkillFromString("persuasion"),   SocialSkill::Persuasion);
    EXPECT_EQ(socialSkillFromString("DECEPTION"),    SocialSkill::Deception);
    EXPECT_EQ(socialSkillFromString("Intimidation"), SocialSkill::Intimidation);
    EXPECT_EQ(socialSkillFromString("unknown"),      SocialSkill::Persuasion);  // default
}

// ---------------------------------------------------------------------------
// resolve
// ---------------------------------------------------------------------------

TEST(SocialInteractionResolverTest, ResolveSuccessLowDC) {
    DiceSystem dice;
    DiceSystem::setSeed(1);

    auto result = SocialInteractionResolver::resolve(
        SocialSkill::Persuasion, 10, 1, false, false, dice);

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.skill, SocialSkill::Persuasion);
    EXPECT_EQ(result.dc, 1);
    DiceSystem::setSeed(0);
}

TEST(SocialInteractionResolverTest, ResolveFailHighDC) {
    DiceSystem dice;
    DiceSystem::setSeed(1);

    auto result = SocialInteractionResolver::resolve(
        SocialSkill::Deception, 0, 100, false, false, dice);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.dc, 100);
    DiceSystem::setSeed(0);
}

TEST(SocialInteractionResolverTest, ResolveTotalEqualsRollPlusBonus) {
    DiceSystem dice;
    DiceSystem::setSeed(7);

    auto result = SocialInteractionResolver::resolve(
        SocialSkill::Persuasion, 5, 15, false, false, dice);

    EXPECT_EQ(result.total, result.roll + 5);
    DiceSystem::setSeed(0);
}

TEST(SocialInteractionResolverTest, ResolveDescriptionNonEmpty) {
    DiceSystem dice;
    DiceSystem::setSeed(1);
    auto result = SocialInteractionResolver::resolve(
        SocialSkill::Intimidation, 3, 12, false, false, dice);
    EXPECT_FALSE(result.description.empty());
    DiceSystem::setSeed(0);
}

TEST(SocialInteractionResolverTest, ResolveWithAdvantageHigherExpectedRoll) {
    DiceSystem dice;

    // Roll 100 times with advantage, average should be well above 10
    DiceSystem::setSeed(5);
    int total = 0;
    for (int i = 0; i < 100; ++i) {
        auto r = SocialInteractionResolver::resolve(SocialSkill::Persuasion, 0, 999, true, false, dice);
        total += r.roll;
    }
    float avg = static_cast<float>(total) / 100.0f;
    EXPECT_GT(avg, 10.0f);  // advantage should bias above average
    DiceSystem::setSeed(0);
}

// ---------------------------------------------------------------------------
// resolveInsight
// ---------------------------------------------------------------------------

TEST(SocialInteractionResolverTest, InsightResultHasAllFields) {
    DiceSystem dice;
    DiceSystem::setSeed(1);
    auto result = SocialInteractionResolver::resolveInsight(5, 3, dice);

    EXPECT_EQ(result.playerTotal, result.playerRoll + 5);
    EXPECT_EQ(result.npcTotal,    result.npcRoll    + 3);
    EXPECT_EQ(result.playerSucceeds, result.playerTotal > result.npcTotal);
    DiceSystem::setSeed(0);
}

TEST(SocialInteractionResolverTest, InsightWithHighBonusUsuallyWins) {
    DiceSystem dice;
    DiceSystem::setSeed(3);
    int wins = 0;
    for (int i = 0; i < 50; ++i) {
        auto r = SocialInteractionResolver::resolveInsight(20, 0, dice);  // huge bonus
        if (r.playerSucceeds) ++wins;
    }
    EXPECT_GT(wins, 40);  // should win most of the time
    DiceSystem::setSeed(0);
}

// ---------------------------------------------------------------------------
// persuasionDC / intimidationDC
// ---------------------------------------------------------------------------

TEST(SocialInteractionResolverTest, PersuasionDCScalesWithTier) {
    int hostile    = SocialInteractionResolver::persuasionDC(ReputationTier::Hostile);
    int unfriendly = SocialInteractionResolver::persuasionDC(ReputationTier::Unfriendly);
    int neutral    = SocialInteractionResolver::persuasionDC(ReputationTier::Neutral);
    int friendly   = SocialInteractionResolver::persuasionDC(ReputationTier::Friendly);
    int honored    = SocialInteractionResolver::persuasionDC(ReputationTier::Honored);
    int exalted    = SocialInteractionResolver::persuasionDC(ReputationTier::Exalted);

    // DC should decrease as reputation improves
    EXPECT_GT(hostile,    unfriendly);
    EXPECT_GT(unfriendly, neutral);
    EXPECT_GT(neutral,    friendly);
    EXPECT_GT(friendly,   honored);
    EXPECT_GT(honored,    exalted);
}

TEST(SocialInteractionResolverTest, IntimidationDCScalesWithTier) {
    int hostile  = SocialInteractionResolver::intimidationDC(ReputationTier::Hostile);
    int exalted  = SocialInteractionResolver::intimidationDC(ReputationTier::Exalted);
    EXPECT_GT(hostile, exalted);
}

// ---------------------------------------------------------------------------
// reputationDelta
// ---------------------------------------------------------------------------

TEST(SocialInteractionResolverTest, PersuasionSuccessGivesPositiveDelta) {
    EXPECT_GT(SocialInteractionResolver::reputationDelta(SocialSkill::Persuasion, true),  0);
}

TEST(SocialInteractionResolverTest, PersuasionFailGivesNegativeDelta) {
    EXPECT_LT(SocialInteractionResolver::reputationDelta(SocialSkill::Persuasion, false), 0);
}

TEST(SocialInteractionResolverTest, IntimidationAlwaysCostsGoodwill) {
    // Intimidation hurts reputation regardless of success
    EXPECT_LT(SocialInteractionResolver::reputationDelta(SocialSkill::Intimidation, true),  0);
    EXPECT_LT(SocialInteractionResolver::reputationDelta(SocialSkill::Intimidation, false), 0);
}

TEST(SocialInteractionResolverTest, FailureWorseThanSuccess) {
    // Failing intimidation should be worse than succeeding
    int successDelta = SocialInteractionResolver::reputationDelta(SocialSkill::Intimidation, true);
    int failDelta    = SocialInteractionResolver::reputationDelta(SocialSkill::Intimidation, false);
    EXPECT_GT(successDelta, failDelta);
}

TEST(SocialInteractionResolverTest, InsightNeutralOnReputation) {
    EXPECT_EQ(SocialInteractionResolver::reputationDelta(SocialSkill::Insight, true),  0);
    EXPECT_EQ(SocialInteractionResolver::reputationDelta(SocialSkill::Insight, false), 0);
}

TEST(SocialInteractionResolverTest, PerformanceSuccessPositive) {
    EXPECT_GT(SocialInteractionResolver::reputationDelta(SocialSkill::Performance, true), 0);
}
