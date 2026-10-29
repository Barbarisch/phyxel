// WaterCore §40 (L1) - the viewer distance every water system reads. A pure function of the viewer and a world position: the
// distance to a box is 0 inside and exact outside (negative coordinates too), the detail fade never rises with distance and never
// jumps, and the defaults - the two distances L1 took over unchanged - are pinned (40.1 items 1, 3, 4).
#include <gtest/gtest.h>

#include "core/water/WaterCoreManager.h"
#include "core/water/WaterLocality.h"

#include <cmath>

using namespace Phyxel::Core::Water;

TEST(WaterLocality, TheDefaultsAreTheTwoDistancesItTookOver) {
    // 27.10's record draw radius and 25.29's ripple window: L1 changes nothing when it lands
    EXPECT_FLOAT_EQ(WaterLocality::kDrawMDefault, 48.0f);
    EXPECT_EQ(WaterLocality::kRippleWindowCellsDefault, 384);
    EXPECT_EQ(WaterCoreManager::kColumnRippleWindow, WaterLocality::kRippleWindowCellsDefault);
    const WaterLocality fresh;
    EXPECT_FLOAT_EQ(fresh.drawM, WaterLocality::kDrawMDefault);
    EXPECT_EQ(fresh.rippleWindowCells, WaterLocality::kRippleWindowCellsDefault);
    EXPECT_FALSE(fresh.testViewerOn);
    EXPECT_FLOAT_EQ(WaterCoreManager::s_locality.drawM, WaterLocality::kDrawMDefault) << "a test left the draw radius changed";
    EXPECT_EQ(WaterCoreManager::s_locality.rippleWindowCells, WaterLocality::kRippleWindowCellsDefault) << "a test left the window changed";
    EXPECT_FALSE(WaterCoreManager::s_locality.testViewerOn) << "a test left the test viewer on";
}

// L3 (41.4 item 8): the kept-memory reach and cap
TEST(WaterLocality, TheMemoryDefaultsArePinned) {
    EXPECT_FLOAT_EQ(WaterLocality::kMemoryMDefault, 48.0f);
    EXPECT_FLOAT_EQ(WaterLocality::kMemoryMDefault, WaterLocality::kDrawMDefault) << "41.2: a pond you can see keeps its memory";
    EXPECT_FLOAT_EQ(WaterLocality::kMemoryCapMbDefault, 256.0f);
    const WaterLocality fresh;
    EXPECT_FLOAT_EQ(fresh.memoryM, WaterLocality::kMemoryMDefault);
    EXPECT_FLOAT_EQ(fresh.memoryCapMb, WaterLocality::kMemoryCapMbDefault);
    EXPECT_FALSE(fresh.testKeepMemoryOff);
    EXPECT_FLOAT_EQ(WaterCoreManager::s_locality.memoryM, WaterLocality::kMemoryMDefault) << "a test left the memory reach changed";
    EXPECT_FLOAT_EQ(WaterCoreManager::s_locality.memoryCapMb, WaterLocality::kMemoryCapMbDefault) << "a test left the cap changed";
    EXPECT_FALSE(WaterCoreManager::s_locality.testKeepMemoryOff) << "a test left kept memory off";
}

// L3 (41.3): the release rule - beyond the reach released; over the cap the nearest are kept first; ties by position, never by order
TEST(WaterLocality, KeptMemoryIsReleasedByDistanceThenByTheCap) {
    using KB = WaterLocality::KeptBox;
    const unsigned long long MB = 1024ull * 1024ull;
    const glm::vec3 viewer(0.0f, 10.0f, 0.0f);
    // boxes at 0 (the viewer inside), ~10, ~30 and ~60 m east, negative z included
    const std::vector<KB> kept = {{{-4, 0, -4}, {3, 2, 3}, 40 * MB}, {{10, 0, -8}, {17, 2, -1}, 40 * MB},
                                  {{30, 0, 0}, {37, 2, 7}, 40 * MB}, {{60, 0, -20}, {67, 2, -13}, 40 * MB}};
    EXPECT_TRUE(WaterLocality::keptToRelease(kept, viewer, 1024.0f, 4096.0f).empty()) << "all within the reach and the cap";
    EXPECT_EQ(WaterLocality::keptToRelease(kept, viewer, 48.0f, 4096.0f), (std::vector<size_t>{3})) << "beyond 48 m";
    EXPECT_EQ(WaterLocality::keptToRelease(kept, viewer, 1024.0f, 100.0f), (std::vector<size_t>{2, 3})) << "the cap holds two: the farthest go";
    EXPECT_EQ(WaterLocality::keptToRelease(kept, viewer, 0.0f, 4096.0f).size(), 4u) << "a reach of 0 keeps nothing";
    EXPECT_EQ(WaterLocality::keptToRelease(kept, viewer, 1024.0f, 0.0f).size(), 4u) << "a cap of 0 keeps nothing";
    // two at the same distance, room for one: the same one wins in either list order (the lower x)
    const std::vector<KB> tie = {{{20, 0, 0}, {27, 2, 7}, 40 * MB}, {{-28, 0, 0}, {-21, 2, 7}, 40 * MB}};
    const std::vector<KB> tieSwapped = {tie[1], tie[0]};
    const glm::vec3 mid(0.0f, 0.0f, 4.0f);
    EXPECT_EQ(WaterLocality::keptToRelease(tie, mid, 1024.0f, 50.0f), (std::vector<size_t>{0})) << "the box at x 20 goes (x -28 wins the tie)";
    EXPECT_EQ(WaterLocality::keptToRelease(tieSwapped, mid, 1024.0f, 50.0f), (std::vector<size_t>{1})) << "the list order picked the winner";
}

TEST(WaterLocality, TheDistanceToABoxIsZeroInsideAndExactOutside) {
    const glm::vec2 lo(-40.0f, -8.0f), hi(-32.0f, 0.0f);   // an 8 m tile at negative coordinates
    EXPECT_FLOAT_EQ(WaterLocality::horizontalDistanceToBox(glm::vec3(-36.0f, 99.0f, -4.0f), lo, hi), 0.0f) << "inside (height ignored)";
    EXPECT_FLOAT_EQ(WaterLocality::horizontalDistanceToBox(glm::vec3(-29.0f, 0.0f, -4.0f), lo, hi), 3.0f) << "east of it";
    EXPECT_FLOAT_EQ(WaterLocality::horizontalDistanceToBox(glm::vec3(-43.0f, 0.0f, 4.0f), lo, hi), 5.0f) << "a corner: 3-4-5";
    EXPECT_FLOAT_EQ(WaterLocality::horizontalDistanceSqToBox(glm::vec3(-43.0f, 0.0f, 4.0f), lo, hi), 25.0f);
    EXPECT_FLOAT_EQ(WaterLocality::horizontalDistance(glm::vec3(1.0f, 7.0f, 1.0f), glm::vec2(4.0f, 5.0f)), 5.0f);
}

TEST(WaterLocality, TheDetailFadeNeverRisesAndNeverJumps) {
    const float nearM = 20.0f, farM = 40.0f;
    EXPECT_FLOAT_EQ(WaterLocality::detail(0.0f, nearM, farM), 1.0f);
    EXPECT_FLOAT_EQ(WaterLocality::detail(nearM, nearM, farM), 1.0f);
    EXPECT_FLOAT_EQ(WaterLocality::detail(farM, nearM, farM), 0.0f);
    EXPECT_FLOAT_EQ(WaterLocality::detail(1000.0f, nearM, farM), 0.0f);
    EXPECT_NEAR(WaterLocality::detail(30.0f, nearM, farM), 0.5f, 1e-6f);
    float prev = 1.0f, worstStep = 0.0f;
    for (int mm = 0; mm <= 50000; ++mm) {   // 0..50 m in 1 mm steps
        const float v = WaterLocality::detail(static_cast<float>(mm) * 1e-3f, nearM, farM);
        EXPECT_LE(v, prev + 1e-7f) << "the fade rose at " << mm << " mm";
        worstStep = std::max(worstStep, std::abs(v - prev));
        prev = v;
    }
    EXPECT_LT(worstStep, 1e-3f) << "the fade jumped (a band edge, not a fade)";
    EXPECT_FLOAT_EQ(WaterLocality::detail(25.0f, 30.0f, 30.0f), 1.0f) << "far <= near: a hard reach at near";
    EXPECT_FLOAT_EQ(WaterLocality::detail(31.0f, 30.0f, 30.0f), 0.0f);
}
