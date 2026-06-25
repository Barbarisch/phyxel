#include <gtest/gtest.h>

#include "core/StairPlanner.h"

using namespace Phyxel::Core;

namespace {
// maxStep on the grid = the character's m_maxStepHeight 4/9 m -> 4 micro.
constexpr int kMaxStep = 4;
// Floor-to-floor for a 3-cube story: floor slab 3 micro + 27 micro walls = 30 micro.
constexpr int kRise = 30;

bool hasLanding(const StairPlan& p, int wmMicro, int h1) {
    for (const auto& s : p.solids)
        if (s.w == wmMicro && (s.y + s.h) == h1) return true;   // full-width platform whose TOP is at mid
    return false;
}
int topReached(const StairPlan& p) {
    int t = 0;
    for (const auto& s : p.solids) t = std::max(t, s.y + s.h);  // surface height = base + thickness
    return t;
}
}  // namespace

TEST(StairPlannerTest, SwitchbackFitsCompliantAndReachesTop) {
    StairPlan p = planStair(2, 6, kRise, StairForm::Switchback, kMaxStep);
    ASSERT_TRUE(p.ok) << p.error;
    EXPECT_LE(p.maxRiserMicro, kMaxStep) << "switchback riser exceeds the character step-up";
    EXPECT_EQ(p.topMicro, kRise);
    EXPECT_EQ(topReached(p), kRise) << "no tread reaches the upper floor";
    EXPECT_TRUE(hasLanding(p, 2 * 9, kRise / 2)) << "switchback has no full-width mid-landing";
}

TEST(StairPlannerTest, SwitchbackUsesTwoLanes) {
    StairPlan p = planStair(2, 6, kRise, StairForm::Switchback, kMaxStep);
    ASSERT_TRUE(p.ok) << p.error;
    bool laneA = false, laneB = false;   // lanes split the 18-micro width at x=9
    for (const auto& s : p.solids) {
        if (s.w < 2 * 9) {               // a flight lane (not the full-width landing)
            if (s.x == 0) laneA = true;
            if (s.x == 9) laneB = true;
        }
    }
    EXPECT_TRUE(laneA && laneB) << "switchback should run two opposite lanes (the 180 turn)";
}

TEST(StairPlannerTest, SwitchbackNeedsWidthTwo) {
    StairPlan p = planStair(1, 8, kRise, StairForm::Switchback, kMaxStep);
    EXPECT_FALSE(p.ok);
    EXPECT_FALSE(p.error.empty());
}

TEST(StairPlannerTest, StraightWithEnoughRunIsWalkable) {
    StairPlan p = planStair(2, 9, kRise, StairForm::Straight, kMaxStep);
    ASSERT_TRUE(p.ok) << p.error;
    EXPECT_LE(p.maxRiserMicro, kMaxStep);
    EXPECT_EQ(topReached(p), kRise);
}

TEST(StairPlannerTest, TooShallowWellDoesNotFit) {
    StairPlan p = planStair(1, 1, kRise, StairForm::Straight, kMaxStep);
    EXPECT_FALSE(p.ok) << "a 1x1 well cannot hold a walkable flight";
}

// Taller story (height 5 -> rise 48 micro) still folds to a compliant switchback if the
// well is deep enough — proves the planner adapts riser/tread to fit, not a fixed shape.
TEST(StairPlannerTest, TallerStoryStillCompliantInDeeperWell) {
    StairPlan p = planStair(2, 8, 3 + 5 * 9, StairForm::Switchback, kMaxStep);
    ASSERT_TRUE(p.ok) << p.error;
    EXPECT_LE(p.maxRiserMicro, kMaxStep);
    EXPECT_EQ(p.topMicro, 3 + 5 * 9);
}
