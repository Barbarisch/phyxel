// WaterCore §42 (L4) - a paced wake's deadline: the time until a body's lowest point can fall to the box's highest recorded water
// (42.3), and the pinned defaults (42.4 item 1). Pure functions; the GPU half (paced == immediate) is in WaterRecordedPondTest.
#include <gtest/gtest.h>

#include "core/water/WaterCoreManager.h"

#include <cmath>

using namespace Phyxel::Core::Water;

namespace {
MovingSolid body(float y, float vy, float half = 1.0f / 6.0f) {
    MovingSolid b; b.centre = glm::vec3(0.0f, y, 0.0f); b.halfExtents = glm::vec3(half); b.velocity = glm::vec3(0.0f, vy, 0.0f);
    return b;
}
}  // namespace

TEST(WaterWakePace, TheDefaultsArePinned) {
    EXPECT_TRUE(WaterCoreManager::kWakePaceByDefault) << "42.6: on by default (the owner, 2026-10-29)";
    EXPECT_DOUBLE_EQ(WaterCoreManager::kWakeBudgetMsDefault, 2.0) << "the 120 fps share";
    EXPECT_EQ(WaterCoreManager::s_wakePace, WaterCoreManager::kWakePaceByDefault) << "a test left pacing changed";
    EXPECT_DOUBLE_EQ(WaterCoreManager::s_wakeBudgetMs, WaterCoreManager::kWakeBudgetMsDefault) << "a test left the budget changed";
    EXPECT_FALSE(WaterCoreManager::s_paceLateForTest) << "a test left the red on";
}

TEST(WaterWakePace, TheTimeToTheWaterIsAFreeFallFromTheLowestPoint) {
    const float half = 1.0f / 6.0f, reach = std::sqrt(3.0f) * half;   // |halfExtents|: the lowest point at any orientation
    // the L5 drop: released at rest with its lowest corner 1.0 m above the water (to the rotated bound): sqrt(2 h / g)
    const double t = WaterCoreManager::secondsToWater(body(16.5f + 1.0f + reach, 0.0f), 16.5f);
    EXPECT_NEAR(t, std::sqrt(2.0 * 1.0 / 9.81), 1e-4);
    // already at (or in) the water: 0 - the immediate wake
    EXPECT_DOUBLE_EQ(WaterCoreManager::secondsToWater(body(16.5f + reach - 1e-4f, 0.0f), 16.5f), 0.0);   // (exactly at it rounds either way)
    EXPECT_DOUBLE_EQ(WaterCoreManager::secondsToWater(body(15.0f, 0.0f), 16.5f), 0.0);
    // thrown down: sooner; thrown up: later (it rises first)
    const double down = WaterCoreManager::secondsToWater(body(16.5f + 1.0f + reach, -5.0f), 16.5f);
    const double up = WaterCoreManager::secondsToWater(body(16.5f + 1.0f + reach, 5.0f), 16.5f);
    EXPECT_LT(down, t);
    EXPECT_GT(up, t);
    EXPECT_NEAR(down, (-5.0 + std::sqrt(25.0 + 2.0 * 9.81 * 1.0)) / 9.81, 1e-4);   // 1 = 5 t + g t^2 / 2
    // a bigger body reaches it sooner from the same centre
    EXPECT_LT(WaterCoreManager::secondsToWater(body(18.0f, 0.0f, 0.5f), 16.5f), WaterCoreManager::secondsToWater(body(18.0f, 0.0f, 0.1f), 16.5f));
}
