// Every path that drops a CPU dynamic object must also remove its VoxelRigidBody.
//
// Found 2026-10-04 while verifying DebrisInteractionPlan D6: /api/debug/clear_dynamics
// reported cpu_cleared 10, yet VoxelDynamicsWorld still held those 10 bodies. The three
// clearAllGlobalDynamic*() functions and the three lifetime-expiry branches erased the
// Cube/Subcube/Microcube but left its body in the world — an invisible collider that keeps
// simulating, blocks characters and debris, and is never freed. Only the isDead branch
// called removeBody(). Contract: after any drop, the world's body count equals the number
// of objects still held.

#include <gtest/gtest.h>

#include "core/Cube.h"
#include "core/DynamicObjectManager.h"
#include "core/Microcube.h"
#include "core/Subcube.h"
#include "physics/PhysicsWorld.h"
#include "physics/VoxelDynamicsWorld.h"
#include "physics/VoxelRigidBody.h"

#include <cfloat>
#include <memory>
#include <vector>

using namespace Phyxel;

namespace {

struct Rig {
    Physics::PhysicsWorld physics;
    std::vector<std::unique_ptr<Cube>> cubes;
    std::vector<std::unique_ptr<Subcube>> subcubes;
    std::vector<std::unique_ptr<Microcube>> microcubes;
    DynamicObjectManager dom;

    Rig() {
        EXPECT_TRUE(physics.initialize());
        dom.setCallbacks([this]() { return &physics; },
                         [this]() -> auto& { return subcubes; },
                         [this]() -> auto& { return cubes; },
                         [this]() -> auto& { return microcubes; },
                         []() {});
    }

    Physics::VoxelDynamicsWorld& world() { return *physics.getVoxelWorld(); }

    // Bodies get FLT_MAX lifetime so only the OBJECT's lifetime can expire them.
    Physics::VoxelRigidBody* body(float y) {
        auto* b = world().createVoxelBody(glm::vec3(0.0f, y, 0.0f), glm::vec3(0.5f), 1.0f, 0.2f, 0.6f);
        b->lifetime = FLT_MAX;
        return b;
    }

    void addCube(float lifetime = 30.0f) {
        auto c = std::make_unique<Cube>(glm::ivec3(0, 50, 0));
        c->setVoxelBody(body(50.0f + 2.0f * cubes.size()));
        c->setLifetime(lifetime);
        dom.addGlobalDynamicCube(std::move(c));
    }
    void addSubcube(float lifetime = 30.0f) {
        auto s = std::make_unique<Subcube>(glm::ivec3(10, 50, 0));
        s->setVoxelBody(body(80.0f + 2.0f * subcubes.size()));
        s->setLifetime(lifetime);
        dom.addGlobalDynamicSubcube(std::move(s));
    }
    void addMicrocube(float lifetime = 30.0f) {
        auto m = std::make_unique<Microcube>(glm::ivec3(20, 50, 0));
        m->setVoxelBody(body(110.0f + 2.0f * microcubes.size()));
        m->setLifetime(lifetime);
        dom.addGlobalDynamicMicrocube(std::move(m));
    }
};

}  // namespace

TEST(DynamicObjectBodyRelease, ClearCubesRemovesTheirBodies) {
    Rig r;
    for (int i = 0; i < 5; ++i) r.addCube();
    ASSERT_EQ(r.world().getBodyCount(), 5u);
    r.dom.clearAllGlobalDynamicCubes();
    EXPECT_EQ(r.cubes.size(), 0u);
    EXPECT_EQ(r.world().getBodyCount(), 0u) << "cleared cubes left ghost bodies in the world";
}

TEST(DynamicObjectBodyRelease, ClearSubcubesRemovesTheirBodies) {
    Rig r;
    for (int i = 0; i < 4; ++i) r.addSubcube();
    ASSERT_EQ(r.world().getBodyCount(), 4u);
    r.dom.clearAllGlobalDynamicSubcubes();
    EXPECT_EQ(r.world().getBodyCount(), 0u) << "cleared subcubes left ghost bodies in the world";
}

TEST(DynamicObjectBodyRelease, ClearMicrocubesRemovesTheirBodies) {
    Rig r;
    for (int i = 0; i < 3; ++i) r.addMicrocube();
    ASSERT_EQ(r.world().getBodyCount(), 3u);
    r.dom.clearAllGlobalDynamicMicrocubes();
    EXPECT_EQ(r.world().getBodyCount(), 0u) << "cleared microcubes left ghost bodies in the world";
}

// Lifetime expiry, one object of each kind expiring while a long-lived one survives:
// the survivor's body must stay, the expired ones' bodies must go.
TEST(DynamicObjectBodyRelease, LifetimeExpiryRemovesOnlyExpiredBodies) {
    Rig r;
    r.addCube(0.05f);      r.addCube(30.0f);
    r.addSubcube(0.05f);   r.addSubcube(30.0f);
    r.addMicrocube(0.05f); r.addMicrocube(30.0f);
    ASSERT_EQ(r.world().getBodyCount(), 6u);

    r.dom.updateAllDynamicObjects(0.1f);

    EXPECT_EQ(r.cubes.size(), 1u);
    EXPECT_EQ(r.subcubes.size(), 1u);
    EXPECT_EQ(r.microcubes.size(), 1u);
    EXPECT_EQ(r.world().getBodyCount(), 3u) << "expired objects left ghost bodies in the world";
    for (auto& c : r.cubes)      EXPECT_FALSE(c->getVoxelBody()->isDead);
    for (auto& s : r.subcubes)   EXPECT_FALSE(s->getVoxelBody()->isDead);
    for (auto& m : r.microcubes) EXPECT_FALSE(m->getVoxelBody()->isDead);
}

// Churn: spawn + clear many cycles; bodies must never accumulate.
TEST(DynamicObjectBodyRelease, SpawnClearChurnNeverAccumulatesBodies) {
    Rig r;
    for (int cycle = 0; cycle < 50; ++cycle) {
        for (int i = 0; i < 6; ++i) { r.addCube(); r.addSubcube(); r.addMicrocube(); }
        r.dom.clearAllGlobalDynamicCubes();
        r.dom.clearAllGlobalDynamicSubcubes();
        r.dom.clearAllGlobalDynamicMicrocubes();
        ASSERT_EQ(r.world().getBodyCount(), 0u) << "after cycle " << cycle;
    }
}
