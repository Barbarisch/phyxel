// KinematicMoverFeedTest - DebrisInteractionPlan Phase 3b: doors, animated template parts and held
// items (KinematicVoxelManager objects flagged pushesDebris) feed GPU debris AND CPU bodies.
// Contract (KinematicVoxelManager::syncCollidersToPhysics):
//   * only objects flagged pushesDebris are fed (furniture/item-prop visuals mirror CPU bodies,
//     which Phase 3c already feeds - feeding them twice would double the push);
//   * an object is split into oriented sub-boxes no longer than kMoverCell per axis, so each box's
//     velocity (from this frame's transform delta at ITS centre) follows a swing: the free edge of a
//     door moves faster than the hinge edge;
//   * a teleport is clamped (20 m/s), the first frame has zero velocity;
//   * the same boxes become CPU kinematic obstacles (the empty stub doors used to have).

#include <gtest/gtest.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/KinematicVoxelManager.h"
#include "physics/PhysicsWorld.h"
#include "physics/VoxelDynamicsWorld.h"

using namespace Phyxel;
using Phyxel::Core::KinematicVoxel;
using Phyxel::Core::KinematicVoxelManager;

namespace {
// A 1 (x) by 2 (y) by 1/9 (z) slab of microcube-thick voxels: a door hinged at its x = 0 edge.
std::vector<KinematicVoxel> doorVoxels() {
    std::vector<KinematicVoxel> out;
    for (int x = 0; x < 3; ++x)
        for (int y = 0; y < 6; ++y) {
            KinematicVoxel v;
            v.localPos = glm::vec3((x + 0.5f) / 3.0f, (y + 0.5f) / 3.0f, 0.0f);
            v.scale = glm::vec3(1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 9.0f);
            v.materialName = "Wood";
            out.push_back(v);
        }
    return out;
}
constexpr float kDt = 1.0f / 60.0f;
}  // namespace

TEST(KinematicMoverFeed, OnlyObjectsThatPushDebrisAreFed) {
    KinematicVoxelManager kvm;
    const auto door = kvm.add("door", doorVoxels());
    kvm.add("dynfurn", doorVoxels());   // a CPU body's visual: fed by 3c, never here
    kvm.setPushesDebris(door, true);
    kvm.syncCollidersToPhysics(kDt);
    ASSERT_FALSE(kvm.lastMoverBoxes().empty());
    for (const auto& b : kvm.lastMoverBoxes()) EXPECT_LT(b.center.x, 1.01f);
    EXPECT_EQ(kvm.lastMoverBoxes().size(), kvm.moverBoxCountFor(door)) << "the furniture visual is not fed";
}

TEST(KinematicMoverFeed, SubBoxesCoverTheObjectAndFollowItsRotation) {
    KinematicVoxelManager kvm;
    const auto door = kvm.add("door", doorVoxels());
    kvm.setPushesDebris(door, true);
    const glm::mat4 open90 = glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(10, 0, 10)),
                                         glm::radians(90.0f), glm::vec3(0, 1, 0));
    kvm.setTransform(door, open90);
    kvm.syncCollidersToPhysics(kDt);
    const auto& boxes = kvm.lastMoverBoxes();
    ASSERT_GE(boxes.size(), 2u) << "a 1 x 2 m door is split (cell <= kMoverCell)";
    float volume = 0.0f;
    for (const auto& b : boxes) {
        volume += 8.0f * b.halfExtents.x * b.halfExtents.y * b.halfExtents.z;
        for (int a = 0; a < 3; ++a) EXPECT_LE(2.0f * b.halfExtents[a], KinematicVoxelManager::kMoverCell + 1e-4f);
        EXPECT_NEAR(std::abs(glm::dot(b.rotation, glm::quat_cast(glm::mat3(open90)))), 1.0f, 1e-4f);
        // yawed 90 degrees about +Y at (10, 0, 10): local +x maps to world -z
        EXPECT_NEAR(b.center.x, 10.0f, 0.06f);
        EXPECT_LT(b.center.z, 10.01f);
    }
    EXPECT_NEAR(volume, 1.0f * 2.0f * (1.0f / 9.0f), 1e-3f) << "the sub-boxes tile the object's box exactly";
}

TEST(KinematicMoverFeed, VelocityIsPerSubBoxFromTheTransformDelta) {
    KinematicVoxelManager kvm;
    const auto door = kvm.add("door", doorVoxels());
    kvm.setPushesDebris(door, true);
    kvm.syncCollidersToPhysics(kDt);
    for (const auto& b : kvm.lastMoverBoxes()) EXPECT_EQ(b.velocity, glm::vec3(0.0f)) << "first frame";

    // Swing 6 degrees about the hinge in one frame (6 rad/s): v = w x r, the free edge fastest.
    kvm.setTransform(door, glm::rotate(glm::mat4(1.0f), glm::radians(6.0f), glm::vec3(0, 1, 0)));
    kvm.syncCollidersToPhysics(kDt);
    float vNear = 1e9f, vFar = 0.0f;
    for (const auto& b : kvm.lastMoverBoxes()) {
        const float r = glm::length(glm::vec2(b.center.x, b.center.z));
        const float v = glm::length(b.velocity);
        EXPECT_NEAR(v, r * glm::radians(6.0f) / kDt, 0.05f * v + 0.05f) << "chord speed at the box centre";
        vNear = std::min(vNear, v); vFar = std::max(vFar, v);
    }
    EXPECT_GT(vFar, 1.5f * vNear) << "the free edge outruns the hinge edge";

    // A teleport: clamped, not passed on.
    kvm.setTransform(door, glm::translate(glm::mat4(1.0f), glm::vec3(500, 0, 0)));
    kvm.syncCollidersToPhysics(kDt);
    for (const auto& b : kvm.lastMoverBoxes()) EXPECT_LE(glm::length(b.velocity), 20.0f + 1e-3f);
}

TEST(KinematicMoverFeed, TheSameBoxesBlockCpuBodies) {
    Phyxel::Physics::PhysicsWorld physics;
    physics.initialize();
    KinematicVoxelManager kvm(&physics);
    const auto door = kvm.add("door", doorVoxels());
    kvm.setPushesDebris(door, true);
    kvm.setTransform(door, glm::translate(glm::mat4(1.0f), glm::vec3(0, 1, 0)));
    kvm.syncCollidersToPhysics(kDt);
    ASSERT_FALSE(kvm.lastMoverBoxes().empty());
    EXPECT_EQ(physics.getVoxelWorld()->kinematicObstacleCount(&kvm), kvm.lastMoverBoxes().size());
    kvm.remove(door);
    kvm.syncCollidersToPhysics(kDt);
    EXPECT_EQ(physics.getVoxelWorld()->kinematicObstacleCount(&kvm), 0u) << "a removed door leaves no ghost";
}
