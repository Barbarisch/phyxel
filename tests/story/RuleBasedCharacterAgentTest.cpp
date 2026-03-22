#include <gtest/gtest.h>
#include "story/RuleBasedCharacterAgent.h"

using namespace Phyxel::Story;

// ============================================================================
// Helper: build a basic profile
// ============================================================================

static CharacterProfile makeProfile(const std::string& id, const std::string& name) {
    CharacterProfile p;
    p.id = id;
    p.name = name;
    p.description = "A test character.";
    p.agencyLevel = AgencyLevel::Guided;
    return p;
}

static CharacterDecisionContext makeContext(CharacterProfile* profile,
                                            const std::vector<std::string>& actions = {}) {
    CharacterDecisionContext ctx;
    ctx.profile = profile;
    ctx.currentSituation = "Standing in a field.";
    ctx.availableActions = actions;
    return ctx;
}

// ============================================================================
// Basic decision tests
// ============================================================================

TEST(RuleBasedAgentTest, DecideWithNoProfile) {
    RuleBasedCharacterAgent agent;
    CharacterDecisionContext ctx;
    auto decision = agent.decide(ctx);
    EXPECT_EQ(decision.action, "idle");
}

TEST(RuleBasedAgentTest, DecideDefaultIdleAction) {
    auto profile = makeProfile("npc_01", "Villager");
    auto ctx = makeContext(&profile);
    RuleBasedCharacterAgent agent;
    auto decision = agent.decide(ctx);
    // With no strong emotions or goals, should pick idle or wait
    EXPECT_FALSE(decision.action.empty());
}

TEST(RuleBasedAgentTest, HighFearTriggersFlee) {
    auto profile = makeProfile("npc_01", "Coward");
    profile.emotion.fear = 0.9f;
    profile.traits.neuroticism = 0.8f;
    auto ctx = makeContext(&profile, {"flee", "idle"});

    RuleBasedCharacterAgent agent;
    auto decision = agent.decide(ctx);
    EXPECT_EQ(decision.action, "flee");
}

TEST(RuleBasedAgentTest, HighAngerTriggersAttack) {
    auto profile = makeProfile("npc_01", "Warrior");
    profile.emotion.anger = 0.9f;
    profile.traits.agreeableness = 0.1f;  // Very disagreeable
    auto ctx = makeContext(&profile, {"attack", "idle"});

    RuleBasedCharacterAgent agent;
    auto decision = agent.decide(ctx);
    EXPECT_EQ(decision.action, "attack");
}

TEST(RuleBasedAgentTest, BraveryReducesFleeScore) {
    auto profile = makeProfile("npc_01", "Brave Guard");
    profile.emotion.fear = 0.6f;
    profile.traits.neuroticism = 0.7f;
    profile.traits.customTraits["bravery"] = 0.9f;
    auto ctx = makeContext(&profile, {"flee", "idle"});

    RuleBasedCharacterAgent agent;
    auto decision = agent.decide(ctx);
    // Bravery should counteract fear → should prefer idle
    EXPECT_EQ(decision.action, "idle");
}

TEST(RuleBasedAgentTest, TradeWithTrustedPartner) {
    auto profile = makeProfile("npc_01", "Merchant");
    profile.traits.agreeableness = 0.9f;
    Relationship rel;
    rel.targetCharacterId = "player";
    rel.trust = 0.8f;
    profile.relationships.push_back(rel);

    CharacterProfile partner;
    partner.id = "player";
    partner.name = "Player";

    auto ctx = makeContext(&profile, {"trade", "speak", "idle"});
    ctx.conversationPartner = &partner;

    RuleBasedCharacterAgent agent;
    auto decision = agent.decide(ctx);
    // High trust + agreeableness + trade available + conversation partner = trade
    EXPECT_EQ(decision.action, "trade");
}

TEST(RuleBasedAgentTest, SpeakInConversation) {
    auto profile = makeProfile("npc_01", "Chatty");
    profile.traits.extraversion = 0.9f;

    CharacterProfile partner;
    partner.id = "player";
    partner.name = "Player";

    auto ctx = makeContext(&profile, {"speak", "idle"});
    ctx.conversationPartner = &partner;

    RuleBasedCharacterAgent agent;
    auto decision = agent.decide(ctx);
    EXPECT_EQ(decision.action, "speak");
}

TEST(RuleBasedAgentTest, PursueHighPriorityGoal) {
    auto profile = makeProfile("npc_01", "Quester");
    CharacterGoal goal;
    goal.id = "find_artifact";
    goal.description = "Find the ancient artifact";
    goal.priority = 0.9f;
    goal.isActive = true;
    profile.goals.push_back(goal);

    auto ctx = makeContext(&profile, {"move_to", "idle"});

    RuleBasedCharacterAgent agent;
    auto decision = agent.decide(ctx);
    EXPECT_EQ(decision.action, "move_to");
    EXPECT_TRUE(decision.reasoning.find("goal") != std::string::npos);
}

// ============================================================================
// Custom rule tests
// ============================================================================

TEST(RuleBasedAgentTest, CustomRuleOverridesDefault) {
    auto profile = makeProfile("npc_01", "Special");
    auto ctx = makeContext(&profile);

    RuleBasedCharacterAgent agent;
    agent.addRule({
        "always_dance",
        [](const CharacterDecisionContext&) { return 100.0f; },
        [](const CharacterDecisionContext&) {
            return CharacterDecision{"dance", {}, "custom rule fired", "", "joy"};
        }
    });

    auto decision = agent.decide(ctx);
    EXPECT_EQ(decision.action, "dance");
    EXPECT_EQ(decision.reasoning, "custom rule fired");
}

TEST(RuleBasedAgentTest, MultipleCustomRulesHighestWins) {
    auto profile = makeProfile("npc_01", "Special");
    auto ctx = makeContext(&profile);

    RuleBasedCharacterAgent agent;
    agent.addRule({
        "low_priority",
        [](const CharacterDecisionContext&) { return 5.0f; },
        [](const CharacterDecisionContext&) {
            return CharacterDecision{"wave", {}, "low rule", "", ""};
        }
    });
    agent.addRule({
        "high_priority",
        [](const CharacterDecisionContext&) { return 10.0f; },
        [](const CharacterDecisionContext&) {
            return CharacterDecision{"salute", {}, "high rule", "", ""};
        }
    });

    auto decision = agent.decide(ctx);
    EXPECT_EQ(decision.action, "salute");
}

TEST(RuleBasedAgentTest, ClearRulesRemovesCustomRules) {
    auto profile = makeProfile("npc_01", "Special");
    auto ctx = makeContext(&profile);

    RuleBasedCharacterAgent agent;
    agent.addRule({
        "custom",
        [](const CharacterDecisionContext&) { return 100.0f; },
        [](const CharacterDecisionContext&) {
            return CharacterDecision{"dance", {}, "", "", ""};
        }
    });

    agent.clearRules();
    auto decision = agent.decide(ctx);
    EXPECT_NE(decision.action, "dance");  // Custom rule cleared, should fall back
}

TEST(RuleBasedAgentTest, ZeroScoreRuleDoesNotFire) {
    auto profile = makeProfile("npc_01", "Special");
    auto ctx = makeContext(&profile);

    RuleBasedCharacterAgent agent;
    agent.addRule({
        "never_fires",
        [](const CharacterDecisionContext&) { return 0.0f; },
        [](const CharacterDecisionContext&) {
            return CharacterDecision{"dance", {}, "", "", ""};
        }
    });

    auto decision = agent.decide(ctx);
    EXPECT_NE(decision.action, "dance");
}

// ============================================================================
// Dialogue tests
// ============================================================================

TEST(RuleBasedAgentTest, GenerateDialogueExtrovert) {
    auto profile = makeProfile("npc_01", "Chatty");
    profile.traits.extraversion = 0.9f;

    CharacterDecisionContext ctx;
    ctx.profile = &profile;

    RuleBasedCharacterAgent agent;
    std::string dialogue = agent.generateDialogue(ctx);
    EXPECT_FALSE(dialogue.empty());
    EXPECT_TRUE(dialogue.find("Hello") != std::string::npos ||
                dialogue.find("friendly") != std::string::npos);
}

TEST(RuleBasedAgentTest, GenerateDialogueIntrovert) {
    auto profile = makeProfile("npc_01", "Quiet");
    profile.traits.extraversion = 0.1f;

    CharacterDecisionContext ctx;
    ctx.profile = &profile;

    RuleBasedCharacterAgent agent;
    std::string dialogue = agent.generateDialogue(ctx);
    EXPECT_EQ(dialogue, "...");
}

TEST(RuleBasedAgentTest, DialogueTemplatesByEmotion) {
    auto profile = makeProfile("npc_01", "Emotional");
    profile.emotion.anger = 0.9f;

    CharacterDecisionContext ctx;
    ctx.profile = &profile;

    RuleBasedCharacterAgent agent;
    agent.setDialogueTemplates({
        {"angry", {"Get out of my way!", "I'm furious!"}},
        {"neutral", {"Hello."}}
    });

    std::string dialogue = agent.generateDialogue(ctx);
    EXPECT_TRUE(dialogue == "Get out of my way!" || dialogue == "I'm furious!");
}

TEST(RuleBasedAgentTest, DialogueNoProfile) {
    CharacterDecisionContext ctx;
    RuleBasedCharacterAgent agent;
    std::string dialogue = agent.generateDialogue(ctx);
    EXPECT_TRUE(dialogue.empty());
}

// ============================================================================
// Agent name
// ============================================================================

TEST(RuleBasedAgentTest, AgentName) {
    RuleBasedCharacterAgent agent;
    EXPECT_EQ(agent.getAgentName(), "RuleBased");
}
