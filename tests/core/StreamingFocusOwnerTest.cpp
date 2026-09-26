// StreamingFocusOwnerTest -- the streaming focus override has more than one user (WorldForge build job,
// /api/worldforge/focus, the camera-path benchmark), so it has an OWNER (docs/PerfProgram2026-09.md
// section 16, I14 / V6). Before it did, the second user overwrote the first and any release cleared
// everyone's focus: a WorldForge release would have cut a benchmark route's streaming mid-run.
#include <gtest/gtest.h>

#include "core/StreamingFocus.h"

using Phyxel::Core::StreamingFocus;

TEST(StreamingFocusOwnerTest, ADifferentHolderCannotTakeAHeldFocus) {
    StreamingFocus f;
    ASSERT_TRUE(f.set({100, 20, 100}, "worldforge_build"));
    EXPECT_FALSE(f.set({5, 20, 5}, "camera_path"));
    EXPECT_EQ(f.holder(), "worldforge_build");
    EXPECT_EQ(f.anchor({0, 0, 0}), glm::vec3(100, 20, 100));
}

TEST(StreamingFocusOwnerTest, OnlyTheHolderCanRelease) {
    StreamingFocus f;
    ASSERT_TRUE(f.set({100, 20, 100}, "worldforge_build"));
    EXPECT_FALSE(f.clear("camera_path"));
    EXPECT_TRUE(f.held());
    EXPECT_TRUE(f.clear("worldforge_build"));
    EXPECT_FALSE(f.held());
    EXPECT_EQ(f.anchor({7, 8, 9}), glm::vec3(7, 8, 9));   // released: the player again
}

TEST(StreamingFocusOwnerTest, AfterReleaseAnotherHolderCanTakeIt) {
    StreamingFocus f;
    ASSERT_TRUE(f.set({100, 20, 100}, "worldforge_focus"));
    ASSERT_TRUE(f.clear("worldforge_focus"));
    EXPECT_TRUE(f.set({5, 20, 5}, "camera_path"));
    EXPECT_EQ(f.holder(), "camera_path");
}

// Control: one user behaves exactly as the old single optional did.
TEST(StreamingFocusOwnerTest, ASingleHolderMovesAndReleasesAsBefore) {
    StreamingFocus f;
    EXPECT_EQ(f.anchor({1, 2, 3}), glm::vec3(1, 2, 3));
    EXPECT_TRUE(f.set({10, 0, 0}, "worldforge_build"));
    EXPECT_TRUE(f.set({20, 0, 0}, "worldforge_build"));   // the holder moves its own focus
    EXPECT_EQ(f.anchor({1, 2, 3}), glm::vec3(20, 0, 0));
    EXPECT_TRUE(f.clear("worldforge_build"));
    EXPECT_FALSE(f.set({1, 1, 1}, ""));                    // an anonymous holder is refused
}

TEST(StreamingFocusOwnerTest, StepTowardNeverMovesMoreThanTheShortStepPerFrame) {
    const glm::vec3 a{0, 0, 0}, far{1000, 0, 0}, near{10, 0, 0};
    EXPECT_FLOAT_EQ(glm::length(StreamingFocus::stepToward(a, far) - a), StreamingFocus::kMaxStepPerFrame);
    EXPECT_EQ(StreamingFocus::stepToward(a, near), near);
}
