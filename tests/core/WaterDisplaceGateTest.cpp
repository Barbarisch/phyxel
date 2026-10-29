// WaterCore §44 (A10) step 2 - who shoves the water (WaterDisplaceGate): the size floor, the budget by score, one decision per
// stroke (never switched mid-stroke), ties by id never by the list's order, and the pinned defaults.
#include <gtest/gtest.h>

#include "core/water/WaterCoreManager.h"
#include "core/water/WaterDisplaceGate.h"

#include <algorithm>
#include <random>

using namespace Phyxel::Core::Water;
using G = WaterDisplaceGate;

namespace {
constexpr float kOn = 0.4f, kOff = 0.2f;   // the editor's hysteresis shape (the values do not matter to the rule)
G::Body body(uint64_t id, float half, float speed) { return {id, glm::vec3(half), speed}; }
}  // namespace

TEST(WaterDisplaceGate, TheDefaultsArePinned) {
    EXPECT_FLOAT_EQ(G::kMinHalfMDefault, 1.0f / 6.0f) << "44.2: a 1/3 m subcube and larger shove";
    EXPECT_EQ(G::kBudgetDefault, 32);
    EXPECT_EQ(G::kBudgetMax, 1024) << "the push window cap (A2g)";
    const G g;
    EXPECT_FLOAT_EQ(g.minHalfM, G::kMinHalfMDefault);
    EXPECT_EQ(g.budget, G::kBudgetDefault);
    EXPECT_FLOAT_EQ(WaterCoreManager::s_displaceGate.minHalfM, G::kMinHalfMDefault) << "a test left the floor changed";
    EXPECT_EQ(WaterCoreManager::s_displaceGate.budget, G::kBudgetDefault) << "a test left the budget changed";
}

TEST(WaterDisplaceGate, ASmallBlockOnlyRipplesAndASubcubeShoves) {
    const G g;
    const auto r = g.select({body(1, 1.0f / 18.0f, 3.0f), body(2, 1.0f / 6.0f, 3.0f), body(3, 0.5f, 3.0f)}, {}, kOn, kOff);
    EXPECT_FALSE(r.shoving(1)) << "a 1/9 m block";
    EXPECT_EQ(r.state.at(1), G::RippleOnly) << "it still moves: it wakes a pond and ripples";
    EXPECT_TRUE(r.shoving(2)) << "a 1/3 m subcube (half 1/6 exactly, through float)";
    EXPECT_TRUE(r.shoving(3));
    EXPECT_EQ(r.refusedSize, 1);
    G every; every.minHalfM = 0.0f;
    EXPECT_TRUE(every.select({body(1, 1.0f / 18.0f, 3.0f)}, {}, kOn, kOff).shoving(1)) << "floor 0: every size (the moonshot)";
    G none; none.budget = 0;
    EXPECT_FALSE(none.select({body(2, 0.5f, 3.0f)}, {}, kOn, kOff).shoving(2)) << "budget 0: none shove";
}

TEST(WaterDisplaceGate, ABodyNeverStartsOrStopsShovingMidStroke) {
    G g; g.budget = 1;
    // frame 1: two strokes start, room for one - the larger shoves, the other is ripple-only for its stroke
    auto r1 = g.select({body(1, 0.2f, 3.0f), body(2, 0.5f, 3.0f)}, {}, kOn, kOff);
    EXPECT_TRUE(r1.shoving(2)); EXPECT_FALSE(r1.shoving(1)); EXPECT_EQ(r1.refusedBudget, 1);
    // frame 2: the shoving body has slowed to rest - its stroke ends; body 1 is still in its stroke: it must NOT start now
    auto r2 = g.select({body(1, 0.2f, 2.0f), body(2, 0.5f, 0.1f)}, r1.state, kOn, kOff);
    EXPECT_FALSE(r2.shoving(2)) << "its stroke ended";
    EXPECT_FALSE(r2.shoving(1)) << "a body refused at its stroke's start began shoving mid-stroke";
    EXPECT_EQ(r2.state.at(1), G::RippleOnly);
    // between the on and off speeds a stroke goes on as decided (hysteresis), both ways
    auto r3 = g.select({body(1, 0.2f, 0.3f), body(3, 0.5f, 3.0f)}, r2.state, kOn, kOff);
    EXPECT_EQ(r3.state.at(1), G::RippleOnly);
    EXPECT_TRUE(r3.shoving(3)) << "a new stroke takes the free slot";
    auto r4 = g.select({body(3, 0.5f, 0.3f)}, r3.state, kOn, kOff);
    EXPECT_TRUE(r4.shoving(3)) << "slowed below the on speed but above the off speed: still shoving";
    // body 1 slows below the off speed (its stroke ends), then speeds up again: a NEW stroke, decided afresh (room now)
    auto r5 = g.select({body(1, 0.2f, 0.1f)}, r4.state, kOn, kOff);
    EXPECT_EQ(r5.state.at(1), G::Idle);
    auto r6 = g.select({body(1, 0.2f, 3.0f)}, r5.state, kOn, kOff);
    EXPECT_TRUE(r6.shoving(1));
}

TEST(WaterDisplaceGate, TheBudgetGoesByScoreAndTiesByIdInAnyListOrder) {
    G g; g.budget = 5;
    std::vector<G::Body> bodies;
    for (uint64_t i = 1; i <= 12; ++i) bodies.push_back(body(i * 7919u, 0.2f + 0.02f * static_cast<float>(i % 4), 1.0f + 0.5f * static_cast<float>(i % 3)));
    bodies.push_back(body(5, 0.2f, 1.0f)); bodies.push_back(body(3, 0.2f, 1.0f));   // a tie with each other (and with i = 4, 8, 12 ...)
    const auto ref = g.select(bodies, {}, kOn, kOff);
    EXPECT_EQ(ref.admitted, 5); EXPECT_EQ(ref.refusedBudget, static_cast<int>(bodies.size()) - 5);
    // the admitted are the five largest scores
    std::vector<float> scores;
    for (const auto& b : bodies) scores.push_back(8.0f * b.halfExtents.x * b.halfExtents.y * b.halfExtents.z * b.relSpeed);
    std::vector<float> sorted = scores; std::sort(sorted.rbegin(), sorted.rend());
    for (size_t i = 0; i < bodies.size(); ++i)
        if (scores[i] > sorted[5]) EXPECT_TRUE(ref.shoving(bodies[i].id)) << "a body with one of the five largest scores was refused";
    std::mt19937 rng(3);
    for (int k = 0; k < 20; ++k) {
        std::vector<G::Body> sh = bodies; std::shuffle(sh.begin(), sh.end(), rng);
        const auto r = g.select(sh, {}, kOn, kOff);
        for (const auto& b : bodies) EXPECT_EQ(r.shoving(b.id), ref.shoving(b.id)) << "the list order changed who shoves (shuffle " << k << ")";
    }
    // the bodies already shoving count first: a stronger newcomer does not take a held slot
    G one; one.budget = 1;
    const auto a = one.select({body(1, 0.2f, 1.0f)}, {}, kOn, kOff);
    const auto b2 = one.select({body(1, 0.2f, 1.0f), body(2, 0.9f, 9.0f)}, a.state, kOn, kOff);
    EXPECT_TRUE(b2.shoving(1)); EXPECT_FALSE(b2.shoving(2));
}
