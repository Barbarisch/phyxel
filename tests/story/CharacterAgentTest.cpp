#include <gtest/gtest.h>
#include "story/CharacterAgent.h"

using namespace Phyxel::Story;

// ============================================================================
// CharacterDecision JSON Tests
// ============================================================================

TEST(CharacterDecisionTest, JsonRoundTrip) {
    CharacterDecision d;
    d.action = "speak";
    d.parameters = {{"target", "npc_02"}};
    d.reasoning = "want to trade";
    d.dialogueText = "Got any wares?";
    d.emotion = "joy";

    nlohmann::json j = d;
    CharacterDecision d2 = j.get<CharacterDecision>();

    EXPECT_EQ(d2.action, "speak");
    EXPECT_EQ(d2.parameters["target"], "npc_02");
    EXPECT_EQ(d2.reasoning, "want to trade");
    EXPECT_EQ(d2.dialogueText, "Got any wares?");
    EXPECT_EQ(d2.emotion, "joy");
}

TEST(CharacterDecisionTest, JsonMinimalFields) {
    nlohmann::json j = {{"action", "idle"}};
    CharacterDecision d = j.get<CharacterDecision>();

    EXPECT_EQ(d.action, "idle");
    EXPECT_TRUE(d.reasoning.empty());
    EXPECT_TRUE(d.dialogueText.empty());
    EXPECT_TRUE(d.emotion.empty());
}

TEST(CharacterDecisionTest, DecisionDefaultValues) {
    CharacterDecision d;
    EXPECT_TRUE(d.action.empty());
    EXPECT_TRUE(d.parameters.is_null());
    EXPECT_TRUE(d.reasoning.empty());
    EXPECT_TRUE(d.dialogueText.empty());
    EXPECT_TRUE(d.emotion.empty());
}

// ============================================================================
// CharacterDecisionContext Tests
// ============================================================================

TEST(CharacterDecisionContextTest, DefaultConstruction) {
    CharacterDecisionContext ctx;
    EXPECT_EQ(ctx.profile, nullptr);
    EXPECT_TRUE(ctx.knowledgeSummary.empty());
    EXPECT_TRUE(ctx.currentSituation.empty());
    EXPECT_TRUE(ctx.availableActions.empty());
    EXPECT_EQ(ctx.conversationPartner, nullptr);
    EXPECT_TRUE(ctx.conversationHistory.empty());
}

TEST(CharacterDecisionContextTest, WithProfile) {
    CharacterProfile profile;
    profile.id = "guard_01";
    profile.name = "Guard";

    CharacterDecisionContext ctx;
    ctx.profile = &profile;
    ctx.knowledgeSummary = "The village is under attack.";
    ctx.currentSituation = "Standing at the gate.";
    ctx.availableActions = {"attack", "flee", "speak"};

    EXPECT_EQ(ctx.profile->id, "guard_01");
    EXPECT_EQ(ctx.knowledgeSummary, "The village is under attack.");
    EXPECT_EQ(ctx.availableActions.size(), 3u);
}
