#include <gtest/gtest.h>
#include "story/StoryDirectorTypes.h"
#include <nlohmann/json.hpp>

using namespace Phyxel::Story;

// ============================================================================
// BeatType Tests
// ============================================================================

TEST(BeatTypeTest, ToStringRoundTrip) {
    EXPECT_EQ(beatTypeFromString(beatTypeToString(BeatType::Hard)), BeatType::Hard);
    EXPECT_EQ(beatTypeFromString(beatTypeToString(BeatType::Soft)), BeatType::Soft);
    EXPECT_EQ(beatTypeFromString(beatTypeToString(BeatType::Optional)), BeatType::Optional);
}

TEST(BeatTypeTest, UnknownDefaultsToSoft) {
    EXPECT_EQ(beatTypeFromString("Nonsense"), BeatType::Soft);
}

// ============================================================================
// ArcConstraintMode Tests
// ============================================================================

TEST(ArcConstraintModeTest, ToStringRoundTrip) {
    EXPECT_EQ(arcConstraintModeFromString(arcConstraintModeToString(ArcConstraintMode::Scripted)),
              ArcConstraintMode::Scripted);
    EXPECT_EQ(arcConstraintModeFromString(arcConstraintModeToString(ArcConstraintMode::Guided)),
              ArcConstraintMode::Guided);
    EXPECT_EQ(arcConstraintModeFromString(arcConstraintModeToString(ArcConstraintMode::Emergent)),
              ArcConstraintMode::Emergent);
    EXPECT_EQ(arcConstraintModeFromString(arcConstraintModeToString(ArcConstraintMode::Freeform)),
              ArcConstraintMode::Freeform);
}

TEST(ArcConstraintModeTest, UnknownDefaultsToGuided) {
    EXPECT_EQ(arcConstraintModeFromString("Unknown"), ArcConstraintMode::Guided);
}

// ============================================================================
// BeatStatus Tests
// ============================================================================

TEST(BeatStatusTest, ToStringRoundTrip) {
    EXPECT_EQ(beatStatusFromString(beatStatusToString(BeatStatus::Pending)), BeatStatus::Pending);
    EXPECT_EQ(beatStatusFromString(beatStatusToString(BeatStatus::Active)), BeatStatus::Active);
    EXPECT_EQ(beatStatusFromString(beatStatusToString(BeatStatus::Completed)), BeatStatus::Completed);
    EXPECT_EQ(beatStatusFromString(beatStatusToString(BeatStatus::Skipped)), BeatStatus::Skipped);
    EXPECT_EQ(beatStatusFromString(beatStatusToString(BeatStatus::Failed)), BeatStatus::Failed);
}

TEST(BeatStatusTest, UnknownDefaultsToPending) {
    EXPECT_EQ(beatStatusFromString("Nonsense"), BeatStatus::Pending);
}

// ============================================================================
// StoryBeat JSON Tests
// ============================================================================

TEST(StoryBeatTest, JsonRoundTrip) {
    StoryBeat beat;
    beat.id = "find_sword";
    beat.description = "Hero finds the legendary sword";
    beat.type = BeatType::Hard;
    beat.triggerCondition = "entered_cave";
    beat.directorActions = {"spawn_sword", "play_fanfare"};
    beat.requiredCharacters = {"hero", "guide"};
    beat.prerequisites = {"meet_guide"};
    beat.completionCondition = "has_sword";
    beat.failureCondition = "guide_died";
    beat.status = BeatStatus::Active;
    beat.activatedTime = 100.0f;

    nlohmann::json j = beat;
    StoryBeat loaded = j.get<StoryBeat>();

    EXPECT_EQ(loaded.id, "find_sword");
    EXPECT_EQ(loaded.description, "Hero finds the legendary sword");
    EXPECT_EQ(loaded.type, BeatType::Hard);
    EXPECT_EQ(loaded.triggerCondition, "entered_cave");
    EXPECT_EQ(loaded.directorActions.size(), 2u);
    EXPECT_EQ(loaded.requiredCharacters.size(), 2u);
    EXPECT_EQ(loaded.prerequisites.size(), 1u);
    EXPECT_EQ(loaded.completionCondition, "has_sword");
    EXPECT_EQ(loaded.failureCondition, "guide_died");
    EXPECT_EQ(loaded.status, BeatStatus::Active);
    EXPECT_FLOAT_EQ(loaded.activatedTime, 100.0f);
}

TEST(StoryBeatTest, JsonMinimalFields) {
    nlohmann::json j = {{"id", "test_beat"}};
    StoryBeat beat = j.get<StoryBeat>();
    EXPECT_EQ(beat.id, "test_beat");
    EXPECT_EQ(beat.type, BeatType::Soft); // default
    EXPECT_EQ(beat.status, BeatStatus::Pending); // default
}

// ============================================================================
// StoryArc Tests
// ============================================================================

class StoryArcTest : public ::testing::Test {
protected:
    StoryArc makeArc() {
        StoryArc arc;
        arc.id = "main_quest";
        arc.name = "The Dragon's Bane";
        arc.constraintMode = ArcConstraintMode::Guided;

        StoryBeat b1;
        b1.id = "meet_guide";
        b1.type = BeatType::Hard;
        arc.beats.push_back(std::move(b1));

        StoryBeat b2;
        b2.id = "find_sword";
        b2.type = BeatType::Soft;
        b2.prerequisites = {"meet_guide"};
        arc.beats.push_back(std::move(b2));

        StoryBeat b3;
        b3.id = "optional_lore";
        b3.type = BeatType::Optional;
        arc.beats.push_back(std::move(b3));

        return arc;
    }
};

TEST_F(StoryArcTest, CompletedBeatCount) {
    auto arc = makeArc();
    EXPECT_EQ(arc.completedBeatCount(), 0);

    arc.beats[0].status = BeatStatus::Completed;
    EXPECT_EQ(arc.completedBeatCount(), 1);
}

TEST_F(StoryArcTest, TotalBeatCount) {
    auto arc = makeArc();
    EXPECT_EQ(arc.totalBeatCount(), 3);
}

TEST_F(StoryArcTest, GetBeat) {
    auto arc = makeArc();
    EXPECT_NE(arc.getBeat("meet_guide"), nullptr);
    EXPECT_NE(arc.getBeat("find_sword"), nullptr);
    EXPECT_EQ(arc.getBeat("nonexistent"), nullptr);
}

TEST_F(StoryArcTest, RecalculateProgress) {
    auto arc = makeArc();
    arc.recalculateProgress();
    EXPECT_FLOAT_EQ(arc.progress, 0.0f);
    EXPECT_FALSE(arc.isCompleted);

    arc.beats[0].status = BeatStatus::Completed;
    arc.recalculateProgress();
    EXPECT_NEAR(arc.progress, 1.0f / 3.0f, 0.01f);
    EXPECT_FALSE(arc.isCompleted);

    arc.beats[1].status = BeatStatus::Completed;
    arc.beats[2].status = BeatStatus::Completed;
    arc.recalculateProgress();
    EXPECT_FLOAT_EQ(arc.progress, 1.0f);
    EXPECT_TRUE(arc.isCompleted);
}

TEST_F(StoryArcTest, JsonRoundTrip) {
    auto arc = makeArc();
    arc.tensionCurve = {0.2f, 0.5f, 0.9f, 0.3f};
    arc.isActive = true;

    nlohmann::json j = arc;
    StoryArc loaded = j.get<StoryArc>();

    EXPECT_EQ(loaded.id, "main_quest");
    EXPECT_EQ(loaded.name, "The Dragon's Bane");
    EXPECT_EQ(loaded.constraintMode, ArcConstraintMode::Guided);
    EXPECT_EQ(loaded.beats.size(), 3u);
    EXPECT_EQ(loaded.tensionCurve.size(), 4u);
    EXPECT_TRUE(loaded.isActive);
    EXPECT_EQ(loaded.beats[0].id, "meet_guide");
    EXPECT_EQ(loaded.beats[1].prerequisites.size(), 1u);
}

// ============================================================================
// DirectorAction Tests
// ============================================================================

TEST(DirectorActionTest, JsonRoundTrip) {
    DirectorAction action;
    action.type = "inject_event";
    action.params = {{"eventId", "battle_01"}, {"importance", 0.8}};

    nlohmann::json j = action;
    DirectorAction loaded = j.get<DirectorAction>();

    EXPECT_EQ(loaded.type, "inject_event");
    EXPECT_EQ(loaded.params["eventId"], "battle_01");
    EXPECT_FLOAT_EQ(loaded.params["importance"].get<float>(), 0.8f);
}
