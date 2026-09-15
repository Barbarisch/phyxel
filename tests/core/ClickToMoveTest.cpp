// ClickToMove (Ravenmere G-75 / CombatUiBg3 increment 5): click the ground, the
// character walks there along the NavGraph; honest outcomes (arrived / no path /
// stalled / cancelled); standoff + onArrive for NPC clicks; the ground pick.
#include <gtest/gtest.h>
#include "core/ClickToMove.h"
#include "core/NavGraph.h"
#include "graphics/Camera.h"

#include <cmath>
#include <memory>

using namespace Phyxel::Core;

namespace {

class MockBody : public ITurnActorBody {
public:
    glm::vec3 pos{0, 1, 0};
    float speed = 3.0f;
    bool frozen = false;      // a wedge: stepToward reports no movement
    int stops = 0;
    glm::vec3 position() const override { return pos; }
    float stepToward(const glm::vec3& target, float dt) override {
        if (frozen) return 0.0f;
        glm::vec3 to = target - pos; to.y = 0.0f;
        const float d = std::sqrt(to.x * to.x + to.z * to.z);
        if (d < 1e-5f) return 0.0f;
        const float step = std::min(d, speed * dt);
        pos += (to / d) * step;
        return step;
    }
    void stop() override { ++stops; }
    void beginAttack(const glm::vec3&) override {}
    bool isAttacking() const override { return false; }
};

// Ground at y=0 over x,z 0..8; a wall (y 1..3) across z at x=4, open only at z=0..1.
VoxelQueryFunc wallWorld(bool gap) {
    return [gap](const glm::ivec3& p) -> bool {
        if (p.y == 0) return true;
        if (p.x == 4 && p.y >= 1 && p.y <= 3) return !(gap && p.z <= 1);
        return false;
    };
}

NavAgentProfile humanoid() { NavAgentProfile a; a.height = 2; a.stepHeight = 1; a.maxFallY = 4; return a; }

float hdist(const glm::vec3& a, const glm::vec3& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z));
}

// Tick until the walker stops (bounded).
int run(ClickToMove& c, int maxTicks = 2000, float dt = 0.05f) {
    int n = 0;
    while (c.active() && n < maxTicks) { c.tick(dt); ++n; }
    return n;
}

} // namespace

TEST(ClickToMoveTest, WalksAStraightLineWhenTheSceneHasNoGraph) {
    MockBody body; body.pos = {0.5f, 1.0f, 0.5f};
    ClickToMove c;
    c.setBodyProvider([&] { return &body; });
    ASSERT_TRUE(c.requestWalkTo({6.5f, 1.0f, 0.5f}));
    EXPECT_TRUE(c.active());
    run(c);
    EXPECT_EQ(c.lastResult(), ClickToMove::Result::Arrived);
    EXPECT_LE(hdist(body.pos, {6.5f, 1.0f, 0.5f}), ClickToMove::kGoalRadius + 1e-3f);
    EXPECT_GE(c.walked(), 5.5f);
    EXPECT_EQ(body.stops, 1) << "the body is stopped exactly once on arrival";
}

TEST(ClickToMoveTest, RoutesAroundTheWallThroughTheGap) {
    NavGraph g(wallWorld(true));
    g.buildRegion({0, 0}, {8, 8}, humanoid());
    MockBody body; body.pos = {1.5f, 1.0f, 6.5f};
    ClickToMove c;
    c.setBodyProvider([&] { return &body; });
    c.setGraphProvider([&]() -> const NavGraph* { return &g; });
    c.setAgent(humanoid());
    ASSERT_TRUE(c.requestWalkTo({7.5f, 1.0f, 6.5f}));
    ASSERT_GE(c.waypoints().size(), 2u);
    float minZ = 99.0f;
    int n = 0;
    while (c.active() && n < 4000) { c.tick(0.05f); minZ = std::min(minZ, body.pos.z); ++n; }
    EXPECT_EQ(c.lastResult(), ClickToMove::Result::Arrived);
    EXPECT_LT(minZ, 2.0f) << "the walk must pass through the z<=1 gap, not through the wall";
    EXPECT_LE(hdist(body.pos, {7.5f, 1.0f, 6.5f}), ClickToMove::kGoalRadius + 1e-3f);
    EXPECT_GT(c.walked(), 9.0f) << "a detour, not the 6 m straight line";
}

TEST(ClickToMoveTest, NoPathIsReportedAndTheBodyNeverMoves) {
    NavGraph g(wallWorld(false));
    g.buildRegion({0, 0}, {8, 8}, humanoid());
    MockBody body; body.pos = {1.5f, 1.0f, 6.5f};
    ClickToMove c;
    c.setBodyProvider([&] { return &body; });
    c.setGraphProvider([&]() -> const NavGraph* { return &g; });
    c.setAgent(humanoid());
    EXPECT_FALSE(c.requestWalkTo({7.5f, 1.0f, 6.5f}));
    EXPECT_EQ(c.lastResult(), ClickToMove::Result::NoPath);
    EXPECT_FALSE(c.active());
    run(c);
    EXPECT_EQ(body.pos, glm::vec3(1.5f, 1.0f, 6.5f));
}

TEST(ClickToMoveTest, StandoffStopsShortAndFiresOnArriveOnce) {
    MockBody body; body.pos = {0.5f, 1.0f, 0.5f};
    ClickToMove c;
    c.setBodyProvider([&] { return &body; });
    int arrived = 0;
    const glm::vec3 npc{5.5f, 1.0f, 0.5f};
    ASSERT_TRUE(c.requestWalkTo(npc, 1.2f, [&] { ++arrived; }));
    run(c);
    EXPECT_EQ(c.lastResult(), ClickToMove::Result::Arrived);
    EXPECT_EQ(arrived, 1);
    const float d = hdist(body.pos, npc);
    EXPECT_LE(d, 1.2f + 1e-3f);
    EXPECT_GE(d, 1.2f - 0.2f) << "stopped at the standoff, not on top of the NPC";
    run(c);                     // extra ticks after arrival are no-ops
    EXPECT_EQ(arrived, 1);
}

TEST(ClickToMoveTest, CancelStopsTheBodyAndDoesNotFireOnArrive) {
    MockBody body; body.pos = {0.5f, 1.0f, 0.5f};
    ClickToMove c;
    c.setBodyProvider([&] { return &body; });
    int arrived = 0;
    ASSERT_TRUE(c.requestWalkTo({9.5f, 1.0f, 0.5f}, 0.0f, [&] { ++arrived; }));
    for (int i = 0; i < 5; ++i) c.tick(0.05f);
    EXPECT_TRUE(c.active());
    c.cancel();                  // WASD pressed / combat began
    EXPECT_FALSE(c.active());
    EXPECT_EQ(c.lastResult(), ClickToMove::Result::Cancelled);
    EXPECT_EQ(body.stops, 1);
    const glm::vec3 where = body.pos;
    run(c);
    EXPECT_EQ(body.pos, where) << "a cancelled walk never resumes";
    EXPECT_EQ(arrived, 0);
}

TEST(ClickToMoveTest, AWalkThatMakesNoProgressEndsAsStalled) {
    MockBody body; body.pos = {0.5f, 1.0f, 0.5f};
    ClickToMove c;
    c.setBodyProvider([&] { return &body; });
    ASSERT_TRUE(c.requestWalkTo({9.5f, 1.0f, 0.5f}));
    for (int i = 0; i < 10; ++i) c.tick(0.05f);   // 0.5 s of progress
    body.frozen = true;                             // wedged on a jamb
    float elapsed = 0.0f;
    while (c.active() && elapsed < 10.0f) { c.tick(0.05f); elapsed += 0.05f; }
    EXPECT_EQ(c.lastResult(), ClickToMove::Result::Stalled);
    EXPECT_LE(elapsed, ClickToMove::kStallSeconds + 0.1f) << "reported within the stall window";
    EXPECT_EQ(body.stops, 1);
}

TEST(ClickToMoveTest, GroundPickHitsTheCubeUnderTheCursorAndStandsOnItsTop) {
    // Flat floor of cubes at y=0 (top at y=1); a 2-high block at x=3, z=3.
    auto solid = [](const glm::ivec3& p) { return p.y == 0 || (p.x == 3 && p.z == 3 && p.y <= 2); };
    // Camera 10 m up looking straight down at (5.5, 5.5).
    Phyxel::Graphics::Camera cam({5.5f, 11.0f, 5.5f});
    cam.setPitch(-89.9f);
    const glm::vec2 vp{1280.0f, 720.0f};
    glm::vec3 hit; glm::ivec3 cube;
    ASSERT_TRUE(ClickToMove::pickGround(cam, {640.0f, 360.0f}, vp, solid, 100.0f, hit, &cube));
    EXPECT_EQ(cube.y, 0);
    EXPECT_NEAR(hit.y, 1.0f, 1e-4f) << "standing point is the cube's top";
    EXPECT_NEAR(hit.x, 5.5f, 0.15f);
    EXPECT_NEAR(hit.z, 5.5f, 0.15f);
    // The block: project its top centre to the screen, pick there, land on top of it.
    glm::vec2 px;
    ASSERT_TRUE(ClickToMove::projectToScreen(cam, {3.5f, 3.0f, 3.5f}, vp, px));
    ASSERT_TRUE(ClickToMove::pickGround(cam, px, vp, solid, 100.0f, hit, &cube));
    EXPECT_EQ(cube, glm::ivec3(3, 2, 3));
    EXPECT_NEAR(hit.y, 3.0f, 1e-4f);
    // Nothing solid: an empty world returns false instead of a bogus point.
    EXPECT_FALSE(ClickToMove::pickGround(cam, {640.0f, 360.0f}, vp,
                                         [](const glm::ivec3&) { return false; }, 100.0f, hit));
}
