// Unit tests for Phase C: KinematicAnimator drives KinematicVoxelObject
// transforms via rotation and translation tracks.

#include <gtest/gtest.h>

#include "core/KinematicAnimator.h"
#include "core/KinematicVoxelManager.h"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

using namespace Phyxel::Core;

namespace {

constexpr float kEps = 1e-3f;

// Spawn a single-voxel kinematic object so we have something to drive.
std::string spawnUnit(KinematicVoxelManager& kvm, const std::string& hint = "part") {
    std::vector<KinematicVoxel> voxels;
    KinematicVoxel v;
    v.localPos = glm::vec3(0.0f);
    v.materialName = "Default";
    voxels.push_back(v);
    return kvm.add(hint, std::move(voxels), glm::mat4(1.0f), "", true);
}

}  // namespace

TEST(KinematicAnimatorTest, RegisterPushesInitialTransform) {
    KinematicVoxelManager kvm;
    KinematicAnimator anim;
    anim.setKinematicVoxelManager(&kvm);

    auto id = spawnUnit(kvm);
    KinematicAnimator::PartConfig cfg;
    cfg.kinematicId = id;
    cfg.hingeWorld = {10.0f, 5.0f, 20.0f};
    cfg.rotationAxis = KinematicAnimator::Axis::Y;
    anim.registerPart(cfg);

    const auto& obj = kvm.getObjects().at(id);
    EXPECT_NEAR(obj.currentTransform[3].x, 10.0f, kEps);
    EXPECT_NEAR(obj.currentTransform[3].y, 5.0f,  kEps);
    EXPECT_NEAR(obj.currentTransform[3].z, 20.0f, kEps);
    EXPECT_TRUE(anim.isSettled(id));
}

TEST(KinematicAnimatorTest, SettlesOnTargetAfterEnoughTime) {
    KinematicVoxelManager kvm;
    KinematicAnimator anim;
    anim.setKinematicVoxelManager(&kvm);
    auto id = spawnUnit(kvm);
    anim.registerPart({id, glm::vec3(0.0f), KinematicAnimator::Axis::Y, 0.0f});

    // Open by 90° at 4 rad/s. Should settle in well under 1s.
    const float target = glm::radians(90.0f);
    anim.setTargetAngle(id, target, 4.0f);
    EXPECT_FALSE(anim.isSettled(id));

    for (int i = 0; i < 200 && !anim.isSettled(id); ++i) {
        anim.update(1.0f / 60.0f);
    }
    EXPECT_TRUE(anim.isSettled(id));
    EXPECT_NEAR(anim.currentAngle(id), target, kEps);
}

TEST(KinematicAnimatorTest, ZeroSpeedSnaps) {
    KinematicVoxelManager kvm;
    KinematicAnimator anim;
    anim.setKinematicVoxelManager(&kvm);
    auto id = spawnUnit(kvm);
    anim.registerPart({id, glm::vec3(0.0f), KinematicAnimator::Axis::Y, 0.0f});

    anim.setTargetAngle(id, glm::radians(45.0f), 0.0f);
    EXPECT_TRUE(anim.isSettled(id));
    EXPECT_NEAR(anim.currentAngle(id), glm::radians(45.0f), kEps);
}

TEST(KinematicAnimatorTest, SettleCallbackFiresOnceOnArrival) {
    KinematicVoxelManager kvm;
    KinematicAnimator anim;
    anim.setKinematicVoxelManager(&kvm);
    auto id = spawnUnit(kvm);
    anim.registerPart({id, glm::vec3(0.0f), KinematicAnimator::Axis::Y, 0.0f});

    int fired = 0;
    float lastAngle = 0.0f;
    anim.setSettleCallback([&](const std::string& /*id*/, float ang, float /*off*/) {
        ++fired;
        lastAngle = ang;
    });

    anim.setTargetAngle(id, glm::radians(60.0f), 4.0f);
    for (int i = 0; i < 200 && !anim.isSettled(id); ++i) {
        anim.update(1.0f / 60.0f);
    }
    // A few extra ticks after settle should not re-fire.
    for (int i = 0; i < 10; ++i) anim.update(1.0f / 60.0f);

    EXPECT_EQ(fired, 1);
    EXPECT_NEAR(lastAngle, glm::radians(60.0f), kEps);
}

TEST(KinematicAnimatorTest, TransformRotatesPointAroundHinge) {
    // A voxel at local (1,0,0) with hinge at world (10,5,20), rotated 90° around +Y.
    // GLM is right-handed: positive Y rotation maps +X → -Z. Expected world pos: (10, 5, 19).
    KinematicAnimator::PartConfig cfg;
    cfg.hingeWorld = {10, 5, 20};
    cfg.rotationAxis = KinematicAnimator::Axis::Y;
    glm::mat4 m = KinematicAnimator::composeTransform(cfg, glm::radians(90.0f), 0.0f);
    glm::vec4 worldPos = m * glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_NEAR(worldPos.x, 10.0f, kEps);
    EXPECT_NEAR(worldPos.y,  5.0f, kEps);
    EXPECT_NEAR(worldPos.z, 19.0f, kEps);
}

TEST(KinematicAnimatorTest, SlideOffsetTranslatesAlongLocalAxis) {
    KinematicAnimator::PartConfig cfg;
    cfg.hingeWorld   = {0, 0, 0};
    cfg.rotationAxis = KinematicAnimator::Axis::Y;
    cfg.slideDirLocal = {0, 0, 1};   // slide along local +Z

    glm::mat4 m = KinematicAnimator::composeTransform(cfg, 0.0f, 0.5f);
    glm::vec4 origin = m * glm::vec4(0, 0, 0, 1);
    EXPECT_NEAR(origin.z, 0.5f, kEps);
}

TEST(KinematicAnimatorTest, UnregisterStopsUpdates) {
    KinematicVoxelManager kvm;
    KinematicAnimator anim;
    anim.setKinematicVoxelManager(&kvm);
    auto id = spawnUnit(kvm);
    anim.registerPart({id, glm::vec3(0.0f), KinematicAnimator::Axis::Y, 0.0f});
    anim.setTargetAngle(id, glm::radians(90.0f), 1.0f);
    anim.unregisterPart(id);
    anim.update(10.0f);   // long tick, but the part is gone
    EXPECT_FALSE(anim.has(id));
    // The voxel object itself is still there with whatever transform was last set.
    EXPECT_TRUE(kvm.getObjects().count(id));
}
