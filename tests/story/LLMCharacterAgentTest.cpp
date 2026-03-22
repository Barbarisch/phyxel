#include <gtest/gtest.h>
#include "story/LLMCharacterAgent.h"
#include "story/RuleBasedCharacterAgent.h"

using namespace Phyxel::Story;

// ============================================================================
// Helper: build test profiles and contexts
// ============================================================================

static CharacterProfile makeProfile(const std::string& id, const std::string& name) {
    CharacterProfile p;
    p.id = id;
    p.name = name;
    p.description = "A test character in the game world.";
    p.agencyLevel = AgencyLevel::Autonomous;
    p.traits.openness = 0.7f;
    p.traits.extraversion = 0.6f;
    p.traits.agreeableness = 0.5f;
    return p;
}

static CharacterDecisionContext makeContext(CharacterProfile* profile) {
    CharacterDecisionContext ctx;
    ctx.profile = profile;
    ctx.knowledgeSummary = "The dragon was spotted near the mountain.";
    ctx.currentSituation = "Standing in the village square.";
    ctx.availableActions = {"speak", "move_to", "flee"};
    return ctx;
}

// ============================================================================
// LLM unavailable fallback tests
// ============================================================================

TEST(LLMCharacterAgentTest, NoCallbackFallsBackToAgent) {
    RuleBasedCharacterAgent fallback;
    LLMCharacterAgent agent(nullptr, &fallback);

    auto profile = makeProfile("guard", "Guard");
    auto ctx = makeContext(&profile);

    auto decision = agent.decide(ctx);
    // Should fall back to rule-based
    EXPECT_FALSE(decision.action.empty());
}

TEST(LLMCharacterAgentTest, NoCallbackNoFallbackReturnsIdle) {
    LLMCharacterAgent agent(nullptr);

    auto profile = makeProfile("guard", "Guard");
    auto ctx = makeContext(&profile);

    auto decision = agent.decide(ctx);
    EXPECT_EQ(decision.action, "idle");
    EXPECT_TRUE(decision.reasoning.find("unavailable") != std::string::npos);
}

TEST(LLMCharacterAgentTest, EmptyResponseFallsBack) {
    RuleBasedCharacterAgent fallback;
    LLMCharacterAgent agent(
        [](const std::string&) { return ""; },
        &fallback
    );

    auto profile = makeProfile("guard", "Guard");
    auto ctx = makeContext(&profile);

    auto decision = agent.decide(ctx);
    // Empty response → fallback
    EXPECT_FALSE(decision.action.empty());
}

TEST(LLMCharacterAgentTest, EmptyResponseNoFallbackReturnsIdle) {
    LLMCharacterAgent agent([](const std::string&) { return ""; });

    auto profile = makeProfile("guard", "Guard");
    auto ctx = makeContext(&profile);

    auto decision = agent.decide(ctx);
    EXPECT_EQ(decision.action, "idle");
}

// ============================================================================
// Successful LLM response tests
// ============================================================================

TEST(LLMCharacterAgentTest, ValidJsonResponseParsed) {
    LLMCharacterAgent agent([](const std::string&) {
        return R"({"action":"speak","parameters":{},"reasoning":"greeting","dialogueText":"Hello traveler!","emotion":"joy"})";
    });

    auto profile = makeProfile("merchant", "Merchant");
    auto ctx = makeContext(&profile);

    auto decision = agent.decide(ctx);
    EXPECT_EQ(decision.action, "speak");
    EXPECT_EQ(decision.dialogueText, "Hello traveler!");
    EXPECT_EQ(decision.emotion, "joy");
    EXPECT_EQ(decision.reasoning, "greeting");
}

TEST(LLMCharacterAgentTest, MinimalJsonResponseParsed) {
    LLMCharacterAgent agent([](const std::string&) {
        return R"({"action":"flee"})";
    });

    auto profile = makeProfile("coward", "Coward");
    auto ctx = makeContext(&profile);

    auto decision = agent.decide(ctx);
    EXPECT_EQ(decision.action, "flee");
    EXPECT_TRUE(decision.reasoning.empty());
}

TEST(LLMCharacterAgentTest, InvalidJsonFallsBack) {
    RuleBasedCharacterAgent fallback;
    LLMCharacterAgent agent(
        [](const std::string&) { return "this is not json at all"; },
        &fallback
    );

    auto profile = makeProfile("guard", "Guard");
    auto ctx = makeContext(&profile);

    auto decision = agent.decide(ctx);
    // Invalid parse → fallback to rule-based
    EXPECT_FALSE(decision.action.empty());
    EXPECT_NE(decision.action, "this is not json at all");
}

TEST(LLMCharacterAgentTest, InvalidJsonNoFallbackReturnsIdle) {
    LLMCharacterAgent agent([](const std::string&) { return "garbage"; });

    auto profile = makeProfile("guard", "Guard");
    auto ctx = makeContext(&profile);

    auto decision = agent.decide(ctx);
    EXPECT_EQ(decision.action, "idle");
    EXPECT_TRUE(decision.reasoning.find("parse failed") != std::string::npos);
}

// ============================================================================
// Dialogue generation tests
// ============================================================================

TEST(LLMCharacterAgentTest, GenerateDialogueJsonResponse) {
    LLMCharacterAgent agent([](const std::string&) {
        return R"({"dialogue":"Beware the dragon, traveler."})";
    });

    auto profile = makeProfile("sage", "Sage");
    CharacterDecisionContext ctx;
    ctx.profile = &profile;

    std::string dialogue = agent.generateDialogue(ctx);
    EXPECT_EQ(dialogue, "Beware the dragon, traveler.");
}

TEST(LLMCharacterAgentTest, GenerateDialogueTextKeyResponse) {
    LLMCharacterAgent agent([](const std::string&) {
        return R"({"text":"Welcome to my shop."})";
    });

    auto profile = makeProfile("shop", "Shopkeeper");
    CharacterDecisionContext ctx;
    ctx.profile = &profile;

    std::string dialogue = agent.generateDialogue(ctx);
    EXPECT_EQ(dialogue, "Welcome to my shop.");
}

TEST(LLMCharacterAgentTest, GenerateDialogueRawText) {
    LLMCharacterAgent agent([](const std::string&) {
        return "Stay away from the caves.";
    });

    auto profile = makeProfile("elder", "Elder");
    CharacterDecisionContext ctx;
    ctx.profile = &profile;

    std::string dialogue = agent.generateDialogue(ctx);
    EXPECT_EQ(dialogue, "Stay away from the caves.");
}

TEST(LLMCharacterAgentTest, GenerateDialogueEmptyFallback) {
    RuleBasedCharacterAgent fallback;
    LLMCharacterAgent agent(
        [](const std::string&) { return ""; },
        &fallback
    );

    auto profile = makeProfile("npc", "NPC");
    profile.traits.extraversion = 0.9f;  // Ensure fallback generates something
    CharacterDecisionContext ctx;
    ctx.profile = &profile;

    std::string dialogue = agent.generateDialogue(ctx);
    EXPECT_FALSE(dialogue.empty());
}

TEST(LLMCharacterAgentTest, GenerateDialogueNoCallbackNoFallback) {
    LLMCharacterAgent agent(nullptr);

    auto profile = makeProfile("npc", "NPC");
    CharacterDecisionContext ctx;
    ctx.profile = &profile;

    std::string dialogue = agent.generateDialogue(ctx);
    EXPECT_TRUE(dialogue.empty());
}

// ============================================================================
// Prompt building tests
// ============================================================================

TEST(LLMCharacterAgentTest, DecisionPromptContainsCharacterInfo) {
    LLMCharacterAgent agent(nullptr);
    auto profile = makeProfile("guard", "Gate Guard");
    profile.roles = {"guard", "questgiver"};

    CharacterGoal goal;
    goal.id = "protect_gate";
    goal.description = "Protect the village gate";
    goal.priority = 0.9f;
    goal.isActive = true;
    profile.goals.push_back(goal);

    auto ctx = makeContext(&profile);

    std::string prompt = agent.buildDecisionPrompt(ctx);
    EXPECT_TRUE(prompt.find("Gate Guard") != std::string::npos);
    EXPECT_TRUE(prompt.find("dragon") != std::string::npos);
    EXPECT_TRUE(prompt.find("village square") != std::string::npos);
    EXPECT_TRUE(prompt.find("speak") != std::string::npos);
    EXPECT_TRUE(prompt.find("move_to") != std::string::npos);
    EXPECT_TRUE(prompt.find("flee") != std::string::npos);
    EXPECT_TRUE(prompt.find("Protect the village gate") != std::string::npos);
    EXPECT_TRUE(prompt.find("guard") != std::string::npos);
    EXPECT_TRUE(prompt.find("questgiver") != std::string::npos);
}

TEST(LLMCharacterAgentTest, DecisionPromptWithConversationPartner) {
    LLMCharacterAgent agent(nullptr);
    auto profile = makeProfile("merchant", "Merchant");
    auto partner = makeProfile("player", "Hero");

    auto ctx = makeContext(&profile);
    ctx.conversationPartner = &partner;
    ctx.conversationHistory = "Merchant: Welcome!\nHero: Do you have potions?";

    std::string prompt = agent.buildDecisionPrompt(ctx);
    EXPECT_TRUE(prompt.find("Hero") != std::string::npos);
    EXPECT_TRUE(prompt.find("Do you have potions?") != std::string::npos);
}

TEST(LLMCharacterAgentTest, DialoguePromptContainsCharacterInfo) {
    LLMCharacterAgent agent(nullptr);
    auto profile = makeProfile("sage", "Wise Sage");

    CharacterDecisionContext ctx;
    ctx.profile = &profile;
    ctx.knowledgeSummary = "Ancient artifact is hidden in the northern caves.";

    std::string prompt = agent.buildDialoguePrompt(ctx);
    EXPECT_TRUE(prompt.find("Wise Sage") != std::string::npos);
    EXPECT_TRUE(prompt.find("Ancient artifact") != std::string::npos);
}

TEST(LLMCharacterAgentTest, DecisionPromptDefaultActions) {
    LLMCharacterAgent agent(nullptr);
    auto profile = makeProfile("npc", "NPC");

    CharacterDecisionContext ctx;
    ctx.profile = &profile;
    // Empty availableActions → should show default set

    std::string prompt = agent.buildDecisionPrompt(ctx);
    EXPECT_TRUE(prompt.find("speak") != std::string::npos);
    EXPECT_TRUE(prompt.find("attack") != std::string::npos);
    EXPECT_TRUE(prompt.find("idle") != std::string::npos);
}

// ============================================================================
// Parse response tests
// ============================================================================

TEST(LLMCharacterAgentTest, ParseDecisionValidJson) {
    std::string json = R"({"action":"attack","parameters":{"target":"dragon"},"reasoning":"it threatens the village"})";
    CharacterDecision d;
    EXPECT_TRUE(LLMCharacterAgent::parseDecisionResponse(json, d));
    EXPECT_EQ(d.action, "attack");
    EXPECT_EQ(d.parameters["target"], "dragon");
    EXPECT_EQ(d.reasoning, "it threatens the village");
}

TEST(LLMCharacterAgentTest, ParseDecisionMissingAction) {
    std::string json = R"({"reasoning":"no action field"})";
    CharacterDecision d;
    EXPECT_FALSE(LLMCharacterAgent::parseDecisionResponse(json, d));
}

TEST(LLMCharacterAgentTest, ParseDecisionInvalidJson) {
    CharacterDecision d;
    EXPECT_FALSE(LLMCharacterAgent::parseDecisionResponse("not json", d));
}

// ============================================================================
// Configuration tests
// ============================================================================

TEST(LLMCharacterAgentTest, CustomSystemPrompt) {
    LLMCharacterAgent agent(nullptr);
    agent.setSystemPrompt("You are a medieval knight.");
    EXPECT_EQ(agent.getSystemPrompt(), "You are a medieval knight.");

    auto profile = makeProfile("knight", "Knight");
    CharacterDecisionContext ctx;
    ctx.profile = &profile;

    std::string prompt = agent.buildDecisionPrompt(ctx);
    EXPECT_TRUE(prompt.find("medieval knight") != std::string::npos);
}

TEST(LLMCharacterAgentTest, MaxResponseTokens) {
    LLMCharacterAgent agent(nullptr);
    EXPECT_EQ(agent.getMaxResponseTokens(), 200);
    agent.setMaxResponseTokens(500);
    EXPECT_EQ(agent.getMaxResponseTokens(), 500);
}

TEST(LLMCharacterAgentTest, IsLLMAvailable) {
    LLMCharacterAgent noCallback(nullptr);
    EXPECT_FALSE(noCallback.isLLMAvailable());

    LLMCharacterAgent withCallback([](const std::string&) { return ""; });
    EXPECT_TRUE(withCallback.isLLMAvailable());
}

TEST(LLMCharacterAgentTest, SetFallback) {
    LLMCharacterAgent agent(nullptr);
    RuleBasedCharacterAgent fallback;
    agent.setFallback(&fallback);

    auto profile = makeProfile("npc", "NPC");
    auto ctx = makeContext(&profile);

    auto decision = agent.decide(ctx);
    // Should use the fallback now
    EXPECT_FALSE(decision.action.empty());
}

TEST(LLMCharacterAgentTest, AgentName) {
    LLMCharacterAgent agent(nullptr);
    EXPECT_EQ(agent.getAgentName(), "LLM");
}

// ============================================================================
// Prompt captures callback input
// ============================================================================

TEST(LLMCharacterAgentTest, CallbackReceivesPrompt) {
    std::string capturedPrompt;
    LLMCharacterAgent agent([&](const std::string& prompt) {
        capturedPrompt = prompt;
        return R"({"action":"idle"})";
    });

    auto profile = makeProfile("npc", "Test NPC");
    auto ctx = makeContext(&profile);
    agent.decide(ctx);

    EXPECT_FALSE(capturedPrompt.empty());
    EXPECT_TRUE(capturedPrompt.find("Test NPC") != std::string::npos);
}
