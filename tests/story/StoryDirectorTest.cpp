#include <gtest/gtest.h>
#include "story/StoryDirector.h"
#include "story/StoryEngine.h"
#include <nlohmann/json.hpp>

using namespace Phyxel::Story;

// ============================================================================
// StoryDirector Tests
// ============================================================================

class StoryDirectorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set up a basic world state
        ws.setVariable("entered_cave", true);
        ws.setVariable("has_sword", false);
        ws.worldTime = 0.0f;
    }

    StoryArc makeLinearArc() {
        StoryArc arc;
        arc.id = "main";
        arc.name = "Main Quest";
        arc.constraintMode = ArcConstraintMode::Scripted;
        arc.isActive = true;

        StoryBeat b1;
        b1.id = "beat_1";
        b1.type = BeatType::Hard;
        b1.triggerCondition = "entered_cave";
        b1.completionCondition = "found_key";
        arc.beats.push_back(std::move(b1));

        StoryBeat b2;
        b2.id = "beat_2";
        b2.type = BeatType::Hard;
        b2.triggerCondition = ""; // no trigger condition = triggers when prereqs met
        b2.completionCondition = "opened_door";
        b2.prerequisites = {"beat_1"};
        arc.beats.push_back(std::move(b2));

        StoryBeat b3;
        b3.id = "beat_3";
        b3.type = BeatType::Hard;
        b3.completionCondition = "dragon_slain";
        b3.prerequisites = {"beat_2"};
        arc.beats.push_back(std::move(b3));

        return arc;
    }

    StoryDirector director;
    WorldState ws;
};

// ===================== Arc Management =====================

TEST_F(StoryDirectorTest, AddAndGetArc) {
    auto arc = makeLinearArc();
    director.addArc(std::move(arc));
    ASSERT_NE(director.getArc("main"), nullptr);
    EXPECT_EQ(director.getArc("main")->name, "Main Quest");
}

TEST_F(StoryDirectorTest, AddArcReplacesExisting) {
    auto arc1 = makeLinearArc();
    director.addArc(std::move(arc1));

    StoryArc arc2;
    arc2.id = "main";
    arc2.name = "Replaced Quest";
    arc2.isActive = true;
    director.addArc(std::move(arc2));

    EXPECT_EQ(director.getArc("main")->name, "Replaced Quest");
    EXPECT_EQ(director.getArcs().size(), 1u);
}

TEST_F(StoryDirectorTest, RemoveArc) {
    director.addArc(makeLinearArc());
    EXPECT_TRUE(director.removeArc("main"));
    EXPECT_EQ(director.getArc("main"), nullptr);
    EXPECT_FALSE(director.removeArc("main")); // already removed
}

TEST_F(StoryDirectorTest, ActivateDeactivateArc) {
    auto arc = makeLinearArc();
    arc.isActive = false;
    director.addArc(std::move(arc));

    EXPECT_TRUE(director.getActiveArcIds().empty());

    director.activateArc("main");
    EXPECT_EQ(director.getActiveArcIds().size(), 1u);

    director.deactivateArc("main");
    EXPECT_TRUE(director.getActiveArcIds().empty());
}

TEST_F(StoryDirectorTest, GetActiveArcIdsExcludesCompleted) {
    auto arc = makeLinearArc();
    arc.isCompleted = true;
    director.addArc(std::move(arc));
    EXPECT_TRUE(director.getActiveArcIds().empty());
}

// ===================== Scripted Mode =====================

TEST_F(StoryDirectorTest, ScriptedModeActivatesBeatOnTrigger) {
    director.addArc(makeLinearArc());
    // entered_cave is true, so beat_1 should activate
    director.update(1.0f, ws);

    auto* arc = director.getArc("main");
    EXPECT_EQ(arc->beats[0].status, BeatStatus::Active);
    // beat_2 prerequisites not met (beat_1 not completed), so still pending
    EXPECT_EQ(arc->beats[1].status, BeatStatus::Pending);
}

TEST_F(StoryDirectorTest, ScriptedModeCompletesOnCondition) {
    director.addArc(makeLinearArc());
    director.update(1.0f, ws); // beat_1 activated

    ws.setVariable("found_key", true);
    ws.worldTime = 10.0f;
    director.update(1.0f, ws); // beat_1 completed

    auto* arc = director.getArc("main");
    EXPECT_EQ(arc->beats[0].status, BeatStatus::Completed);
    // beat_2 should now activate (no trigger condition, prereqs met)
    EXPECT_EQ(arc->beats[1].status, BeatStatus::Active);
}

TEST_F(StoryDirectorTest, ScriptedModeSequentialProgression) {
    director.addArc(makeLinearArc());

    // Activate beat_1
    director.update(1.0f, ws);
    EXPECT_EQ(director.getArc("main")->beats[0].status, BeatStatus::Active);

    // Complete beat_1
    ws.setVariable("found_key", true);
    ws.worldTime = 10.0f;
    director.update(1.0f, ws);

    // Complete beat_2
    ws.setVariable("opened_door", true);
    ws.worldTime = 20.0f;
    director.update(1.0f, ws);
    EXPECT_EQ(director.getArc("main")->beats[1].status, BeatStatus::Completed);

    // Complete beat_3
    ws.setVariable("dragon_slain", true);
    ws.worldTime = 30.0f;
    director.update(1.0f, ws);
    EXPECT_EQ(director.getArc("main")->beats[2].status, BeatStatus::Completed);

    // update() already calls recalculateProgress
    auto* arc = director.getArc("main");
    EXPECT_TRUE(arc->isCompleted);
    EXPECT_FLOAT_EQ(arc->progress, 1.0f);
}

TEST_F(StoryDirectorTest, ScriptedModeBlocksOnUntriggeredBeat) {
    auto arc = makeLinearArc();
    arc.beats[0].triggerCondition = "never_happens";
    director.addArc(std::move(arc));

    director.update(1.0f, ws);
    // beat_1 can't trigger, so nothing activates
    EXPECT_EQ(director.getArc("main")->beats[0].status, BeatStatus::Pending);
    EXPECT_EQ(director.getArc("main")->beats[1].status, BeatStatus::Pending);
}

TEST_F(StoryDirectorTest, ScriptedModeBeatFailure) {
    auto arc = makeLinearArc();
    arc.beats[0].failureCondition = "cave_collapsed";
    director.addArc(std::move(arc));

    // Activate beat_1
    director.update(1.0f, ws);
    EXPECT_EQ(director.getArc("main")->beats[0].status, BeatStatus::Active);

    // Trigger failure
    ws.setVariable("cave_collapsed", true);
    director.update(1.0f, ws);
    EXPECT_EQ(director.getArc("main")->beats[0].status, BeatStatus::Failed);
}

// ===================== Guided Mode =====================

TEST_F(StoryDirectorTest, GuidedModeActivatesTriggeredBeats) {
    auto arc = makeLinearArc();
    arc.constraintMode = ArcConstraintMode::Guided;
    director.addArc(std::move(arc));

    director.update(1.0f, ws);
    EXPECT_EQ(director.getArc("main")->beats[0].status, BeatStatus::Active);
}

TEST_F(StoryDirectorTest, GuidedModeRespectsMinTimeBetweenBeats) {
    auto arc = makeLinearArc();
    arc.constraintMode = ArcConstraintMode::Guided;
    arc.minTimeBetweenBeats = 30.0f;
    director.addArc(std::move(arc));

    // Activate and complete beat_1
    director.update(1.0f, ws);
    ws.setVariable("found_key", true);
    ws.worldTime = 5.0f;
    director.update(1.0f, ws);
    EXPECT_EQ(director.getArc("main")->beats[0].status, BeatStatus::Completed);

    // beat_2 should not activate yet (only 5s since arc start, need 30s)
    director.update(1.0f, ws);
    EXPECT_EQ(director.getArc("main")->beats[1].status, BeatStatus::Pending);

    // Advance time past minTimeBetweenBeats
    ws.worldTime = 40.0f;
    director.update(1.0f, ws);
    EXPECT_EQ(director.getArc("main")->beats[1].status, BeatStatus::Active);
}

TEST_F(StoryDirectorTest, GuidedModeEmitsNudgeWhenStalled) {
    auto arc = makeLinearArc();
    arc.constraintMode = ArcConstraintMode::Guided;
    arc.maxTimeWithoutProgress = 10.0f;
    arc.beats[0].triggerCondition = "not_set"; // Won't trigger naturally
    director.addArc(std::move(arc));

    std::vector<DirectorAction> actions;
    director.setActionCallback([&](const DirectorAction& a) { actions.push_back(a); });

    // First update — not stalled yet
    ws.worldTime = 5.0f;
    director.update(5.0f, ws);

    // Advance past maxTimeWithoutProgress
    ws.worldTime = 15.0f;
    director.update(10.0f, ws);

    bool hasNudge = false;
    for (auto& a : actions) {
        if (a.type == "nudge_beat") hasNudge = true;
    }
    EXPECT_TRUE(hasNudge);
}

// ===================== Emergent Mode =====================

TEST_F(StoryDirectorTest, EmergentModeSkipsHardBeats) {
    auto arc = makeLinearArc(); // All hard beats
    arc.constraintMode = ArcConstraintMode::Emergent;
    director.addArc(std::move(arc));

    director.update(1.0f, ws);
    auto* a = director.getArc("main");
    for (auto& b : a->beats) {
        EXPECT_EQ(b.status, BeatStatus::Skipped);
    }
}

TEST_F(StoryDirectorTest, EmergentModeActivatesSoftBeats) {
    StoryArc arc;
    arc.id = "side";
    arc.constraintMode = ArcConstraintMode::Emergent;
    arc.isActive = true;

    StoryBeat b;
    b.id = "discover";
    b.type = BeatType::Soft;
    b.triggerCondition = "entered_cave";
    b.completionCondition = "found_secret";
    arc.beats.push_back(std::move(b));

    director.addArc(std::move(arc));
    director.update(1.0f, ws);

    EXPECT_EQ(director.getArc("side")->beats[0].status, BeatStatus::Active);
}

// ===================== Freeform Mode =====================

TEST_F(StoryDirectorTest, FreeformModeSkipsAllBeats) {
    auto arc = makeLinearArc();
    arc.constraintMode = ArcConstraintMode::Freeform;
    director.addArc(std::move(arc));

    director.update(1.0f, ws);
    auto* a = director.getArc("main");
    for (auto& b : a->beats) {
        EXPECT_EQ(b.status, BeatStatus::Skipped);
    }
}

// ===================== Condition Evaluation =====================

TEST_F(StoryDirectorTest, DefaultConditionEvalBool) {
    ws.setVariable("flag_true", true);
    ws.setVariable("flag_false", false);

    director.addArc(makeLinearArc());

    // Use internal evaluation via beat trigger
    StoryArc arc;
    arc.id = "test_cond";
    arc.constraintMode = ArcConstraintMode::Scripted;
    arc.isActive = true;

    StoryBeat b;
    b.id = "b1";
    b.type = BeatType::Hard;
    b.triggerCondition = "flag_true";
    b.completionCondition = "flag_false"; // false variable won't complete
    arc.beats.push_back(std::move(b));

    director.addArc(std::move(arc));
    director.update(1.0f, ws);

    EXPECT_EQ(director.getArc("test_cond")->beats[0].status, BeatStatus::Active);
}

TEST_F(StoryDirectorTest, CustomConditionEvaluator) {
    director.setConditionEvaluator([](const std::string& cond, const WorldState& ws) -> bool {
        // Custom: check if condition starts with "!"
        if (!cond.empty() && cond[0] == '!') {
            return ws.getVariable(cond.substr(1)) == nullptr;
        }
        return ws.getVariable(cond) != nullptr;
    });

    StoryArc arc;
    arc.id = "custom";
    arc.constraintMode = ArcConstraintMode::Scripted;
    arc.isActive = true;

    StoryBeat b;
    b.id = "b1";
    b.type = BeatType::Hard;
    b.triggerCondition = "!nonexistent"; // Should evaluate to true
    b.completionCondition = "!also_nonexistent";
    arc.beats.push_back(std::move(b));

    director.addArc(std::move(arc));
    director.update(1.0f, ws);

    // Both trigger and completion should be true
    EXPECT_EQ(director.getArc("custom")->beats[0].status, BeatStatus::Completed);
}

// ===================== Director Actions =====================

TEST_F(StoryDirectorTest, EmitsDirectorActionsOnBeatActivation) {
    auto arc = makeLinearArc();
    arc.beats[0].directorActions = {"play_cutscene", "spawn_monster"};
    director.addArc(std::move(arc));

    std::vector<DirectorAction> receivedActions;
    director.setActionCallback([&](const DirectorAction& a) { receivedActions.push_back(a); });

    director.update(1.0f, ws);

    EXPECT_EQ(receivedActions.size(), 2u);
    EXPECT_EQ(receivedActions[0].type, "play_cutscene");
    EXPECT_EQ(receivedActions[1].type, "spawn_monster");
}

TEST_F(StoryDirectorTest, PendingActionsTracked) {
    auto arc = makeLinearArc();
    arc.beats[0].directorActions = {"action_1"};
    director.addArc(std::move(arc));

    director.update(1.0f, ws);
    EXPECT_EQ(director.getPendingActions().size(), 1u);

    director.clearPendingActions();
    EXPECT_TRUE(director.getPendingActions().empty());
}

TEST_F(StoryDirectorTest, InjectEventEmitsAction) {
    WorldEvent e;
    e.id = "storm";
    e.type = "weather";
    e.importance = 0.6f;

    std::vector<DirectorAction> actions;
    director.setActionCallback([&](const DirectorAction& a) { actions.push_back(a); });

    director.injectEvent(e);
    ASSERT_EQ(actions.size(), 1u);
    EXPECT_EQ(actions[0].type, "inject_event");
    EXPECT_EQ(actions[0].params["eventId"], "storm");
}

TEST_F(StoryDirectorTest, PromoteCharacterAgencyEmitsAction) {
    std::vector<DirectorAction> actions;
    director.setActionCallback([&](const DirectorAction& a) { actions.push_back(a); });

    director.promoteCharacterAgency("guard_01", AgencyLevel::Guided);
    ASSERT_EQ(actions.size(), 1u);
    EXPECT_EQ(actions[0].type, "promote_agency");
    EXPECT_EQ(actions[0].params["characterId"], "guard_01");
}

TEST_F(StoryDirectorTest, SuggestGoalEmitsAction) {
    std::vector<DirectorAction> actions;
    director.setActionCallback([&](const DirectorAction& a) { actions.push_back(a); });

    director.suggestGoal("hero", "investigate", "Investigate the noise", 0.8f);
    ASSERT_EQ(actions.size(), 1u);
    EXPECT_EQ(actions[0].type, "suggest_goal");
    EXPECT_FLOAT_EQ(actions[0].params["priority"].get<float>(), 0.8f);
}

// ===================== Manual Beat Control =====================

TEST_F(StoryDirectorTest, SkipBeat) {
    director.addArc(makeLinearArc());
    director.skipBeat("main", "beat_1");
    EXPECT_EQ(director.getArc("main")->beats[0].status, BeatStatus::Skipped);
}

TEST_F(StoryDirectorTest, CompleteBeat) {
    director.addArc(makeLinearArc());
    director.completeBeat("main", "beat_1", 42.0f);
    auto* arc = director.getArc("main");
    EXPECT_EQ(arc->beats[0].status, BeatStatus::Completed);
    EXPECT_FLOAT_EQ(arc->beats[0].completedTime, 42.0f);
    EXPECT_NEAR(arc->progress, 1.0f / 3.0f, 0.01f);
}

// ===================== Tension / Pacing =====================

TEST_F(StoryDirectorTest, TensionCurveInterpolation) {
    StoryArc arc;
    arc.id = "tension";
    arc.isActive = true;
    arc.constraintMode = ArcConstraintMode::Guided;
    arc.tensionCurve = {0.0f, 0.5f, 1.0f};
    arc.progress = 0.5f;
    director.addArc(std::move(arc));

    EXPECT_FLOAT_EQ(director.getTargetTension("tension"), 0.5f);
}

TEST_F(StoryDirectorTest, TensionCurveAtBounds) {
    StoryArc arc;
    arc.id = "t";
    arc.isActive = true;
    arc.tensionCurve = {0.2f, 0.8f};
    arc.progress = 0.0f;
    director.addArc(std::move(arc));

    EXPECT_FLOAT_EQ(director.getTargetTension("t"), 0.2f);

    director.getArcMut("t")->progress = 1.0f;
    EXPECT_FLOAT_EQ(director.getTargetTension("t"), 0.8f);
}

TEST_F(StoryDirectorTest, RecommendedTensionAveragesActiveArcs) {
    StoryArc a1;
    a1.id = "a1";
    a1.isActive = true;
    a1.tensionCurve = {0.6f};
    a1.progress = 0.0f;
    director.addArc(std::move(a1));

    StoryArc a2;
    a2.id = "a2";
    a2.isActive = true;
    a2.tensionCurve = {0.4f};
    a2.progress = 0.0f;
    director.addArc(std::move(a2));

    EXPECT_FLOAT_EQ(director.getRecommendedTension(), 0.5f);
}

TEST_F(StoryDirectorTest, PacingUpdatesDramaTension) {
    StoryArc arc;
    arc.id = "pacer";
    arc.isActive = true;
    arc.constraintMode = ArcConstraintMode::Freeform;
    arc.tensionCurve = {0.8f}; // constant high tension
    director.addArc(std::move(arc));

    ws.dramaTension = 0.0f;
    // Run several updates to let tension converge
    for (int i = 0; i < 100; ++i) {
        ws.worldTime += 1.0f;
        director.update(1.0f, ws);
    }
    EXPECT_GT(ws.dramaTension, 0.3f); // Should have moved toward 0.8
}

// ===================== EventBus Integration =====================

TEST_F(StoryDirectorTest, ListenToEventBus) {
    EventBus bus;
    director.listenTo(bus);
    EXPECT_EQ(bus.subscriberCount(), 1u);
}

// ===================== Serialization =====================

TEST_F(StoryDirectorTest, SaveLoadRoundTrip) {
    auto arc = makeLinearArc();
    arc.tensionCurve = {0.1f, 0.5f, 0.9f};
    director.addArc(std::move(arc));

    // Advance state
    director.update(1.0f, ws);
    ws.setVariable("found_key", true);
    ws.worldTime = 10.0f;
    director.update(5.0f, ws);

    auto saved = director.saveState();

    StoryDirector loaded;
    loaded.loadState(saved);

    ASSERT_NE(loaded.getArc("main"), nullptr);
    EXPECT_EQ(loaded.getArc("main")->beats.size(), 3u);
    EXPECT_EQ(loaded.getArc("main")->tensionCurve.size(), 3u);
    // Beat 1 should be completed
    EXPECT_EQ(loaded.getArc("main")->beats[0].status, BeatStatus::Completed);
}

// ===================== StoryEngine Integration =====================

TEST_F(StoryDirectorTest, StoryEngineIntegration) {
    StoryEngine engine;
    engine.defineWorld(ws);

    StoryArc arc;
    arc.id = "quest";
    arc.constraintMode = ArcConstraintMode::Scripted;

    StoryBeat b;
    b.id = "start";
    b.type = BeatType::Hard;
    b.triggerCondition = "entered_cave";
    b.completionCondition = "quest_done";
    arc.beats.push_back(std::move(b));

    engine.addStoryArc(std::move(arc));

    // The arc should be active
    EXPECT_EQ(engine.getDirector().getActiveArcIds().size(), 1u);

    // Update should evaluate the beat
    engine.update(1.0f);
    EXPECT_EQ(engine.getDirector().getArc("quest")->beats[0].status, BeatStatus::Active);

    // Complete it
    engine.getWorldState().setVariable("quest_done", true);
    engine.update(1.0f);
    EXPECT_EQ(engine.getDirector().getArc("quest")->beats[0].status, BeatStatus::Completed);
}

TEST_F(StoryDirectorTest, StoryEngineDirectorSaveLoad) {
    StoryEngine engine;
    engine.defineWorld(ws);

    StoryArc arc;
    arc.id = "save_test";
    arc.constraintMode = ArcConstraintMode::Scripted;
    StoryBeat b;
    b.id = "b1";
    b.type = BeatType::Hard;
    b.triggerCondition = "entered_cave";
    b.completionCondition = "done";
    arc.beats.push_back(std::move(b));
    engine.addStoryArc(std::move(arc));

    engine.update(1.0f); // Activate beat

    auto state = engine.saveState();

    StoryEngine engine2;
    engine2.loadState(state);

    ASSERT_NE(engine2.getDirector().getArc("save_test"), nullptr);
    EXPECT_EQ(engine2.getDirector().getArc("save_test")->beats[0].status, BeatStatus::Active);
}
