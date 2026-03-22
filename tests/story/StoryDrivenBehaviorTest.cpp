#include <gtest/gtest.h>
#include "scene/behaviors/StoryDrivenBehavior.h"
#include "scene/Entity.h"
#include "story/RuleBasedCharacterAgent.h"

using namespace Phyxel;
using namespace Phyxel::Scene;
using namespace Phyxel::Story;

// ============================================================================
// Minimal Entity stub for testing
// ============================================================================

namespace Phyxel { namespace Scene {

class TestEntity : public Entity {
public:
    TestEntity(const glm::vec3& pos) { position = pos; }
    void update(float) override {}
    void render(Graphics::RenderCoordinator*) override {}
};

}} // namespace Phyxel::Scene

namespace {

// Helper setup
struct TestFixture {
    CharacterProfile profile;
    CharacterMemory memory;
    RuleBasedCharacterAgent agent;
    Scene::TestEntity entity{glm::vec3(10, 20, 30)};
    NPCContext npcCtx;

    TestFixture() {
        profile.id = "npc_01";
        profile.name = "Test NPC";
        profile.description = "A test NPC.";
        profile.agencyLevel = AgencyLevel::Guided;

        memory.addInnateKnowledge("fact_01", "The village is peaceful.");

        npcCtx.self = &entity;
        npcCtx.selfId = "npc_01";
    }
};

} // anonymous namespace

// ============================================================================
// Construction and basic behavior
// ============================================================================

TEST(StoryDrivenBehaviorTest, BehaviorName) {
    TestFixture f;
    StoryDrivenBehavior behavior(&f.agent, &f.profile, &f.memory);
    EXPECT_EQ(behavior.getBehaviorName(), "StoryDriven");
}

TEST(StoryDrivenBehaviorTest, DefaultDecisionInterval) {
    TestFixture f;
    StoryDrivenBehavior behavior(&f.agent, &f.profile, &f.memory);
    EXPECT_FLOAT_EQ(behavior.getDecisionInterval(), 1.0f);
}

TEST(StoryDrivenBehaviorTest, SetDecisionInterval) {
    TestFixture f;
    StoryDrivenBehavior behavior(&f.agent, &f.profile, &f.memory);
    behavior.setDecisionInterval(0.5f);
    EXPECT_FLOAT_EQ(behavior.getDecisionInterval(), 0.5f);
}

TEST(StoryDrivenBehaviorTest, GetAgent) {
    TestFixture f;
    StoryDrivenBehavior behavior(&f.agent, &f.profile, &f.memory);
    EXPECT_EQ(behavior.getAgent(), &f.agent);
}

// ============================================================================
// Update & decision making
// ============================================================================

TEST(StoryDrivenBehaviorTest, NoDecisionBeforeInterval) {
    TestFixture f;
    StoryDrivenBehavior behavior(&f.agent, &f.profile, &f.memory);
    behavior.setDecisionInterval(2.0f);

    bool callbackFired = false;
    behavior.setDecisionCallback([&](const std::string&, const CharacterDecision&) {
        callbackFired = true;
    });

    // Update with dt < interval
    behavior.update(0.5f, f.npcCtx);
    EXPECT_FALSE(callbackFired);
}

TEST(StoryDrivenBehaviorTest, DecisionFiredAfterInterval) {
    TestFixture f;
    StoryDrivenBehavior behavior(&f.agent, &f.profile, &f.memory);
    behavior.setDecisionInterval(1.0f);

    bool callbackFired = false;
    CharacterDecision capturedDecision;
    behavior.setDecisionCallback([&](const std::string& id, const CharacterDecision& d) {
        callbackFired = true;
        capturedDecision = d;
        EXPECT_EQ(id, "npc_01");
    });

    // Update past the interval
    behavior.update(1.5f, f.npcCtx);
    EXPECT_TRUE(callbackFired);
    EXPECT_FALSE(capturedDecision.action.empty());
}

TEST(StoryDrivenBehaviorTest, LastDecisionUpdated) {
    TestFixture f;
    StoryDrivenBehavior behavior(&f.agent, &f.profile, &f.memory);
    behavior.setDecisionInterval(0.1f);

    behavior.update(0.2f, f.npcCtx);
    EXPECT_FALSE(behavior.getLastDecision().action.empty());
}

TEST(StoryDrivenBehaviorTest, NullAgentSafeUpdate) {
    TestFixture f;
    StoryDrivenBehavior behavior(nullptr, &f.profile, &f.memory);
    // Should not crash
    behavior.update(2.0f, f.npcCtx);
    EXPECT_TRUE(behavior.getLastDecision().action.empty());
}

TEST(StoryDrivenBehaviorTest, NullProfileSafeUpdate) {
    TestFixture f;
    StoryDrivenBehavior behavior(&f.agent, nullptr, &f.memory);
    // Should not crash
    behavior.update(2.0f, f.npcCtx);
    EXPECT_TRUE(behavior.getLastDecision().action.empty());
}

// ============================================================================
// Situation builder
// ============================================================================

TEST(StoryDrivenBehaviorTest, CustomSituationBuilder) {
    TestFixture f;
    StoryDrivenBehavior behavior(&f.agent, &f.profile, &f.memory);
    behavior.setDecisionInterval(0.1f);

    std::string capturedSituation;
    // Use a custom rule that captures the situation from context
    f.agent.addRule({
        "capture",
        [&](const CharacterDecisionContext& ctx) {
            capturedSituation = ctx.currentSituation;
            return 100.0f;
        },
        [](const CharacterDecisionContext&) {
            return CharacterDecision{"idle", {}, "captured", "", ""};
        }
    });

    behavior.setSituationBuilder([](const NPCContext&) {
        return "Custom: Standing near the forge.";
    });

    behavior.update(0.2f, f.npcCtx);
    EXPECT_EQ(capturedSituation, "Custom: Standing near the forge.");
}

TEST(StoryDrivenBehaviorTest, DefaultSituationIncludesPosition) {
    TestFixture f;
    StoryDrivenBehavior behavior(&f.agent, &f.profile, &f.memory);
    behavior.setDecisionInterval(0.1f);

    std::string capturedSituation;
    f.agent.addRule({
        "capture",
        [&](const CharacterDecisionContext& ctx) {
            capturedSituation = ctx.currentSituation;
            return 100.0f;
        },
        [](const CharacterDecisionContext&) {
            return CharacterDecision{"idle", {}, "", "", ""};
        }
    });

    behavior.update(0.2f, f.npcCtx);
    EXPECT_TRUE(capturedSituation.find("10") != std::string::npos);
    EXPECT_TRUE(capturedSituation.find("20") != std::string::npos);
    EXPECT_TRUE(capturedSituation.find("30") != std::string::npos);
}

// ============================================================================
// Knowledge summary in context
// ============================================================================

TEST(StoryDrivenBehaviorTest, KnowledgeSummaryInContext) {
    TestFixture f;
    StoryDrivenBehavior behavior(&f.agent, &f.profile, &f.memory);
    behavior.setDecisionInterval(0.1f);

    std::string capturedKnowledge;
    f.agent.addRule({
        "capture",
        [&](const CharacterDecisionContext& ctx) {
            capturedKnowledge = ctx.knowledgeSummary;
            return 100.0f;
        },
        [](const CharacterDecisionContext&) {
            return CharacterDecision{"idle", {}, "", "", ""};
        }
    });

    behavior.update(0.2f, f.npcCtx);
    EXPECT_TRUE(capturedKnowledge.find("peaceful") != std::string::npos);
}

// ============================================================================
// Interaction / dialogue
// ============================================================================

TEST(StoryDrivenBehaviorTest, OnInteractTriggersDecision) {
    TestFixture f;
    f.profile.agencyLevel = AgencyLevel::Scripted;  // Uses decide(), not generateDialogue()
    StoryDrivenBehavior behavior(&f.agent, &f.profile, &f.memory);

    bool callbackFired = false;
    behavior.setDecisionCallback([&](const std::string&, const CharacterDecision&) {
        callbackFired = true;
    });

    Scene::TestEntity interactor(glm::vec3(0, 0, 0));
    behavior.onInteract(&interactor);
    EXPECT_TRUE(callbackFired);
}

TEST(StoryDrivenBehaviorTest, OnInteractGuidedGeneratesDialogue) {
    TestFixture f;
    f.profile.agencyLevel = AgencyLevel::Guided;
    f.profile.traits.extraversion = 0.9f;
    StoryDrivenBehavior behavior(&f.agent, &f.profile, &f.memory);

    CharacterDecision capturedDecision;
    behavior.setDecisionCallback([&](const std::string&, const CharacterDecision& d) {
        capturedDecision = d;
    });

    Scene::TestEntity interactor(glm::vec3(0, 0, 0));
    behavior.onInteract(&interactor);
    EXPECT_EQ(capturedDecision.action, "speak");
    EXPECT_FALSE(capturedDecision.dialogueText.empty());
}

TEST(StoryDrivenBehaviorTest, OnEventDoesNotCrash) {
    TestFixture f;
    StoryDrivenBehavior behavior(&f.agent, &f.profile, &f.memory);
    // Should not crash
    behavior.onEvent("explosion", {{"x", 10}});
}

// ============================================================================
// Available actions from profile
// ============================================================================

TEST(StoryDrivenBehaviorTest, AllowedActionsPassedToContext) {
    TestFixture f;
    f.profile.allowedActions = {"speak", "flee"};
    StoryDrivenBehavior behavior(&f.agent, &f.profile, &f.memory);
    behavior.setDecisionInterval(0.1f);

    std::vector<std::string> capturedActions;
    f.agent.addRule({
        "capture",
        [&](const CharacterDecisionContext& ctx) {
            capturedActions = ctx.availableActions;
            return 100.0f;
        },
        [](const CharacterDecisionContext&) {
            return CharacterDecision{"idle", {}, "", "", ""};
        }
    });

    behavior.update(0.2f, f.npcCtx);
    ASSERT_EQ(capturedActions.size(), 2u);
    EXPECT_EQ(capturedActions[0], "speak");
    EXPECT_EQ(capturedActions[1], "flee");
}
