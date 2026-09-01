#include <gtest/gtest.h>

#include "ai/TacticalSpace.h"
#include "core/ChunkManager.h"
#include "physics/PhysicsWorld.h"

using namespace Phyxel;

// ============================================================================
// TacticalSpace — line of sight and cover search against real voxel geometry.
//
// Rig: ONE chunk (32^3), a flat floor at y=16, one variable per test. Every
// case states its prediction and has a control (the same query with the
// obstacle absent / from a position that should NOT be covered).
// ============================================================================

namespace {

class TacticalSpaceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Headless init (ChunkSealedTest pattern) — without it the first
        // addCube throws "bad function call" on an unset device callback.
        cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
        // Floor: y=16 across the chunk, so feet at y=17 are standable.
        for (int x = 0; x < 32; ++x)
            for (int z = 0; z < 32; ++z)
                cm.addCubeWithMaterial(glm::ivec3(x, 16, z), "Stone");
    }

    /// A wall of `h` cubes at (x,z), rising from the floor.
    void pillar(int x, int z, int h) {
        for (int i = 1; i <= h; ++i)
            cm.addCubeWithMaterial(glm::ivec3(x, 16 + i, z), "Stone");
    }

    /// Feet position on the floor.
    static glm::vec3 feet(float x, float z) { return glm::vec3(x, 17.0f, z); }

    // physics BEFORE cm: members destruct in reverse order, and ~Chunk
    // unregisters its occupancy grid from the physics world.
    Phyxel::Physics::PhysicsWorld physics;
    ChunkManager cm;
};

} // namespace

TEST_F(TacticalSpaceTest, ClearGroundHasLineOfSight) {
    // Control for every occlusion test below: nothing between them.
    EXPECT_TRUE(AI::TacticalSpace::canSee(cm, feet(4, 16), feet(28, 16)));
}

TEST_F(TacticalSpaceTest, AWallBreaksLineOfSight) {
    // A wall tall enough to cover a 1.6 u eye line, spanning the sight line.
    for (int z = 12; z <= 20; ++z) pillar(16, z, 4);

    EXPECT_FALSE(AI::TacticalSpace::canSee(cm, feet(4, 16), feet(28, 16)))
        << "the wall is directly between them";
    // ...and the same wall must NOT block a sight line that goes around it.
    EXPECT_TRUE(AI::TacticalSpace::canSee(cm, feet(4, 28), feet(28, 28)))
        << "sight line is clear of the wall's span";
}

TEST_F(TacticalSpaceTest, ALowWallDoesNotBlockAStandingEyeLine) {
    // One cube high: it exists, but a 1.6 u eye looks straight over it. Cover
    // has to be measured, not assumed from "there is geometry here".
    for (int z = 12; z <= 20; ++z) pillar(16, z, 1);
    EXPECT_TRUE(AI::TacticalSpace::canSee(cm, feet(4, 16), feet(28, 16)));
}

TEST_F(TacticalSpaceTest, LineOfSightIsSymmetric) {
    for (int z = 12; z <= 20; ++z) pillar(16, z, 4);
    const glm::vec3 a = feet(4, 16), b = feet(28, 16);
    EXPECT_EQ(AI::TacticalSpace::canSee(cm, a, b),
              AI::TacticalSpace::canSee(cm, b, a));
}

TEST_F(TacticalSpaceTest, StandableRequiresGroundAndHeadroom) {
    EXPECT_TRUE(AI::TacticalSpace::isStandable(cm, feet(10, 10)));

    // Roofed in: ground is there, but the body does not fit.
    cm.addCubeWithMaterial(glm::ivec3(10, 17, 10), "Stone");
    EXPECT_FALSE(AI::TacticalSpace::isStandable(cm, feet(10, 10)));

    // No ground at all (above the floor, nothing underfoot).
    EXPECT_FALSE(AI::TacticalSpace::isStandable(cm, glm::vec3(20.0f, 24.0f, 20.0f)));
}

TEST_F(TacticalSpaceTest, OpenGroundOffersNoCover) {
    // THE control for the cover search: with nothing to hide behind, findCover
    // must report failure rather than inventing a spot. A behavior that trusted
    // a bogus position would walk its NPC into the open.
    auto spot = AI::TacticalSpace::findCover(cm, feet(8, 16), feet(28, 16), 10.0f);
    EXPECT_FALSE(spot.found);
}

TEST_F(TacticalSpaceTest, FindsCoverBehindAWallAndItActuallyCovers) {
    // A wall between the seeker and the threat.
    for (int z = 10; z <= 22; ++z) pillar(16, z, 4);

    const glm::vec3 threat = feet(28, 16);
    const glm::vec3 seeker = feet(12, 16);      // already on the far side
    auto spot = AI::TacticalSpace::findCover(cm, seeker, threat, 10.0f);

    ASSERT_TRUE(spot.found);
    // The contract is not "a position was returned" — it is that the threat
    // cannot see the position. Verify the property, not the call.
    EXPECT_FALSE(AI::TacticalSpace::canSee(cm, spot.position, threat))
        << "returned spot at (" << spot.position.x << "," << spot.position.z
        << ") is still visible to the threat";
    EXPECT_TRUE(AI::TacticalSpace::isStandable(cm, spot.position));
}

TEST_F(TacticalSpaceTest, CoverIsSoughtOnTheFarSideOfTheObstacle) {
    // Wall at x=16; threat to the east. Cover should be found WEST of the wall
    // (away from the threat), not by running past the enemy.
    for (int z = 10; z <= 22; ++z) pillar(16, z, 4);

    const glm::vec3 threat = feet(28, 16);
    auto spot = AI::TacticalSpace::findCover(cm, feet(14, 16), threat, 10.0f);

    ASSERT_TRUE(spot.found);
    EXPECT_LT(spot.position.x, 16.0f)
        << "cover was chosen on the threat's side of the wall";
}

TEST_F(TacticalSpaceTest, CoverStaysWithinTheSearchRadius) {
    for (int z = 10; z <= 22; ++z) pillar(16, z, 4);
    const glm::vec3 seeker = feet(14, 16);
    auto spot = AI::TacticalSpace::findCover(cm, seeker, feet(28, 16), 6.0f);

    ASSERT_TRUE(spot.found);
    glm::vec3 d = spot.position - seeker; d.y = 0.0f;
    EXPECT_LE(glm::length(d), 6.0f + 0.01f);
}

// ── directRouteWalkable ──────────────────────────────────────────────────
// The Redoubt failure in one test: the horde could SEE defenders over the
// parapet, "charged" on that basis, and walked into the outside of the wall.
// Walking and seeing are different questions and must not share an answer.

TEST_F(TacticalSpaceTest, OpenGroundIsWalkable) {
    EXPECT_TRUE(AI::TacticalSpace::directRouteWalkable(cm, feet(4, 16), feet(28, 16)));
}

TEST_F(TacticalSpaceTest, AWallStopsAWalkerWhileTheDefenderOnItStaysVisible) {
    // THE Redoubt case. A rampart with defenders standing ON it: the attacker
    // can see them perfectly well, and cannot walk to them. Charging on a sight
    // check piled 120 bodies against the outside of a fort wall.
    //
    // (An earlier version of this test claimed a 2-voxel wall blocks walking
    // but not sight. It does not - its top sits at y=19, above a 1.6u eye at
    // y=18.6 - and with whole voxels no wall occupies that band at all. The
    // real geometry is an ELEVATED defender, which is what the fort had.)
    for (int z = 12; z <= 20; ++z) pillar(16, z, 2);

    const glm::vec3 attacker = feet(4, 16);
    const glm::vec3 onRampart(16.0f, 19.0f, 16.0f);   // standing on the wall top

    EXPECT_FALSE(AI::TacticalSpace::directRouteWalkable(cm, attacker, onRampart))
        << "the wall face must stop a walker";
    EXPECT_TRUE(AI::TacticalSpace::canSee(cm, attacker, onRampart))
        << "control: the defender on the parapet IS visible - which is exactly "
           "why an LOS check is the wrong gate for movement";
}

TEST_F(TacticalSpaceTest, ASingleStepUpIsWalkable) {
    // One voxel is a step, not a wall. Refusing it would make AI treat every
    // kerb as impassable and path around the world.
    for (int z = 12; z <= 20; ++z) pillar(16, z, 1);
    EXPECT_TRUE(AI::TacticalSpace::directRouteWalkable(cm, feet(4, 16), feet(28, 16)));
}

TEST_F(TacticalSpaceTest, AGapInTheWallIsWalkable) {
    // Wall with a gate. The route THROUGH the gap must read as walkable, or a
    // pathfinder would never be told the direct line is fine and every fighter
    // would re-path forever.
    for (int z = 10; z <= 22; ++z) {
        if (z >= 15 && z <= 17) continue;      // the gate
        pillar(16, z, 3);
    }
    EXPECT_TRUE(AI::TacticalSpace::directRouteWalkable(cm, feet(4, 16), feet(28, 16)))
        << "the line runs straight through the gap";
    EXPECT_FALSE(AI::TacticalSpace::directRouteWalkable(cm, feet(4, 20), feet(28, 20)))
        << "control: the same wall off to the side still blocks";
}

TEST_F(TacticalSpaceTest, GroundHeightLandsOnTopOfTheSurface) {
    // Dropping from above the floor puts the feet at y=17, on top of y=16.
    EXPECT_FLOAT_EQ(AI::TacticalSpace::groundHeight(cm, 5.0f, 5.0f, 24.0f), 17.0f);

    // Stack two more cubes: the standing surface rises with them.
    pillar(5, 5, 2);
    EXPECT_FLOAT_EQ(AI::TacticalSpace::groundHeight(cm, 5.0f, 5.0f, 24.0f), 19.0f);
}
