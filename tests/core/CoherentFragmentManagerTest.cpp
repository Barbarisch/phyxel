/**
 * Phase 1b (docs/DestructionSystemV2.md §5.G, H12) — CoherentFragmentManager.
 *
 * The persistent owner that ticks a physicalized world fragment: each frame it syncs
 * the rigid body's transform into the renderer, and reaps fragments whose body the
 * physics world removed. Proves: spawn creates a live body + render object; update()
 * makes the render transform FOLLOW the falling body; a removed body is reaped.
 */

#include <gtest/gtest.h>
#include "core/CoherentFragmentManager.h"
#include "core/KinematicVoxelManager.h"
#include "physics/VoxelDynamicsWorld.h"
#include "physics/VoxelRigidBody.h"
#include <glm/gtc/matrix_transform.hpp>

using namespace Phyxel::Core;

namespace {

KinematicVoxel ucube(float x, float y, float z) {
    KinematicVoxel v;
    v.localPos = glm::vec3(x, y, z);
    v.scale    = glm::vec3(1.0f);
    v.materialName = "Stone";
    return v;
}
std::vector<KinematicVoxel> block() {
    std::vector<KinematicVoxel> v;
    for (int x = 0; x < 2; ++x)
        for (int y = 0; y < 2; ++y)
            for (int z = 0; z < 2; ++z)
                v.push_back(ucube((float)x, (float)y, (float)z));
    return v;
}
float unitMass(const KinematicVoxel&) { return 1.0f; }

// Current render transform of the single kinematic object.
glm::mat4 soleRenderTransform(const KinematicVoxelManager& kin) {
    return kin.getObjects().begin()->second.currentTransform;
}

} // namespace

TEST(CoherentFragmentManagerTest, NotReadyWithoutDeps) {
    CoherentFragmentManager mgr;
    EXPECT_FALSE(mgr.ready());
    EXPECT_EQ(mgr.spawn("f", block(), glm::mat4(1.0f), glm::vec3(0), glm::vec3(0), unitMass), 0u);
    EXPECT_EQ(mgr.count(), 0u);
}

TEST(CoherentFragmentManagerTest, SpawnCreatesLiveBodyAndRender) {
    Phyxel::Physics::VoxelDynamicsWorld world;
    KinematicVoxelManager kin;
    CoherentFragmentManager mgr;
    mgr.setDeps(&world, &kin);
    ASSERT_TRUE(mgr.ready());

    uint32_t id = mgr.spawn("f", block(), glm::mat4(1.0f), glm::vec3(0), glm::vec3(0), unitMass);
    EXPECT_NE(id, 0u);
    EXPECT_EQ(mgr.count(), 1u);
    EXPECT_EQ(world.getBodyCount(), 1u);
    EXPECT_EQ(kin.count(), 1u);
    // Persistent: pinned to never time out.
    ASSERT_NE(world.getBodyById(id), nullptr);
    EXPECT_GT(world.getBodyById(id)->lifetime, 1e30f);
}

TEST(CoherentFragmentManagerTest, RenderTransformFollowsFallingBody) {
    Phyxel::Physics::VoxelDynamicsWorld world;
    world.setGravity(glm::vec3(0.0f, -9.81f, 0.0f));
    KinematicVoxelManager kin;
    CoherentFragmentManager mgr;
    mgr.setDeps(&world, &kin);

    glm::mat4 high = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 100.0f, 0.0f));
    uint32_t id = mgr.spawn("f", block(), high, glm::vec3(0), glm::vec3(0), unitMass);
    ASSERT_NE(id, 0u);

    mgr.update(0.0f);
    float startRenderY = soleRenderTransform(kin)[3].y;

    for (int i = 0; i < 60; ++i) {          // ~1s of gravity
        world.stepSimulation(1.0f / 60.0f);
        mgr.update(1.0f / 60.0f);
    }

    auto* body = world.getBodyById(id);
    ASSERT_NE(body, nullptr);
    float endRenderY = soleRenderTransform(kin)[3].y;
    EXPECT_LT(endRenderY, startRenderY - 1.0f) << "render did not follow the falling body";
    // The render transform tracks the body position (sync correctness).
    EXPECT_NEAR(endRenderY, body->position.y, 1e-3f);
}

TEST(CoherentFragmentManagerTest, ReapsRemovedBody) {
    Phyxel::Physics::VoxelDynamicsWorld world;
    KinematicVoxelManager kin;
    CoherentFragmentManager mgr;
    mgr.setDeps(&world, &kin);

    uint32_t id = mgr.spawn("f", block(), glm::mat4(1.0f), glm::vec3(0), glm::vec3(0), unitMass);
    ASSERT_NE(id, 0u);
    ASSERT_EQ(mgr.count(), 1u);

    world.removeBody(world.getBodyById(id));   // physics world drops the body
    mgr.update(0.0f);                          // manager should notice and reap
    EXPECT_EQ(mgr.count(), 0u);
    EXPECT_EQ(kin.count(), 0u) << "render object leaked after body removal";
}
