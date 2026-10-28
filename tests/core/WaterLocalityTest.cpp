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
