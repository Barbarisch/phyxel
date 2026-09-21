#include <gtest/gtest.h>

#include "scene/motion/HumanoidRetargeter.h"

#include <glm/gtx/quaternion.hpp>

using namespace Phyxel;
using namespace Phyxel::Scene::Motion;

namespace {
constexpr float kEpsilon = 1.0e-5f;

void expectSameRotation(const glm::quat& a, const glm::quat& b) {
    EXPECT_NEAR(std::abs(glm::dot(glm::normalize(a), glm::normalize(b))), 1.0f, kEpsilon);
}

Skeleton targetSkeleton() {
    Skeleton skeleton;
    skeleton.addBone("Hips", -1, {0, 1, 0},
                     glm::angleAxis(glm::radians(30.0f), glm::vec3(0, 1, 0)), {1, 1, 1});
    skeleton.addBone("Spine", 0, {0, 1, 0}, glm::quat(1, 0, 0, 0), {1, 1, 1});
    skeleton.addBone("Finger", 1, {0, 1, 0}, glm::quat(1, 0, 0, 0), {1, 1, 1});
    return skeleton;
}
}

TEST(HumanoidRetargeter, NeutralSourceMapsToTargetBindPose) {
    Skeleton target = targetSkeleton();
    const glm::quat sourceBind = glm::angleAxis(glm::radians(-20.0f), glm::vec3(1, 0, 0));
    HumanoidRetargeter retargeter(target, {{"pelvis", "Hips", sourceBind}});
    ASSERT_TRUE(retargeter.valid()) << retargeter.error();

    LocalPoseFrame source;
    source.jointNames = {"pelvis"};
    source.localRotations = {sourceBind};
    std::vector<glm::quat> base = {
        target.bones[0].localRotation, target.bones[1].localRotation,
        target.bones[2].localRotation};
    std::vector<glm::quat> result;
    ASSERT_TRUE(retargeter.retarget(source, base, result));
    expectSameRotation(result[0], target.bones[0].localRotation);
}

TEST(HumanoidRetargeter, TransfersBindRelativeDeltaAndPreservesUnmappedBones) {
    Skeleton target = targetSkeleton();
    const glm::quat sourceBind = glm::angleAxis(glm::radians(15.0f), glm::vec3(1, 0, 0));
    const glm::quat delta = glm::angleAxis(glm::radians(40.0f), glm::vec3(0, 0, 1));
    HumanoidRetargeter retargeter(target, {{"pelvis", "Hips", sourceBind}});

    LocalPoseFrame source;
    source.jointNames = {"pelvis"};
    source.localRotations = {sourceBind * delta};
    std::vector<glm::quat> base = {
        target.bones[0].localRotation,
        glm::angleAxis(glm::radians(11.0f), glm::vec3(0, 1, 0)),
        glm::angleAxis(glm::radians(27.0f), glm::vec3(1, 0, 0))};
    std::vector<glm::quat> result;
    ASSERT_TRUE(retargeter.retarget(source, base, result));
    expectSameRotation(result[0], target.bones[0].localRotation * delta);
    expectSameRotation(result[1], base[1]);
    expectSameRotation(result[2], base[2]);
}

TEST(HumanoidRetargeter, CorrectsQuaternionHemisphereWithoutAngularSpike) {
    const glm::quat previous = glm::angleAxis(glm::radians(10.0f), glm::vec3(0, 1, 0));
    glm::quat sameRotationOppositeSign = -previous;
    ASSERT_TRUE(normalizeAndMatchHemisphere(sameRotationOppositeSign, previous));
    EXPECT_GT(glm::dot(sameRotationOppositeSign, previous), 0.99999f);
}

TEST(HumanoidRetargeter, XYZWFixtureCatchesWXYZInterpretation) {
    // Upstream ABI order for a +90 degree X rotation is x,y,z,w. X is
    // deliberately asymmetric here: the earlier Y-axis fixture happened to
    // rotate the sampled forward vector identically under both permutations.
    const float xyzw[4] = {0.70710678f, 0.0f, 0.0f, 0.70710678f};
    const glm::quat correct(xyzw[3], xyzw[0], xyzw[1], xyzw[2]);
    const glm::quat wrong(xyzw[0], xyzw[1], xyzw[2], xyzw[3]);
    const glm::vec3 forward(0, 0, 1);
    const glm::vec3 correctDirection = correct * forward;
    const glm::vec3 wrongDirection = wrong * forward;
    EXPECT_NEAR(correctDirection.y, -1.0f, kEpsilon);
    EXPECT_GT(glm::length(correctDirection - wrongDirection), 1.0f);
}

TEST(HumanoidRetargeter, RejectsMissingTargetBoneAndDuplicateSourceNames) {
    Skeleton target = targetSkeleton();
    HumanoidRetargeter bad(target, {{"pelvis", "Missing", glm::quat(1, 0, 0, 0)}});
    EXPECT_FALSE(bad.valid());

    HumanoidRetargeter good(target, {{"pelvis", "Hips", glm::quat(1, 0, 0, 0)}});
    LocalPoseFrame source;
    source.jointNames = {"pelvis", "pelvis"};
    source.localRotations = {glm::quat(1, 0, 0, 0), glm::quat(1, 0, 0, 0)};
    std::vector<glm::quat> base(target.bones.size(), glm::quat(1, 0, 0, 0));
    std::vector<glm::quat> result;
    EXPECT_FALSE(good.retarget(source, base, result));
}

TEST(HumanoidRetargeter, CollapsesOrderedHingeChainOntoTargetJoint) {
    Skeleton target = targetSkeleton();
    RetargetJoint mapping;
    mapping.targetBone = "Hips";
    mapping.sourceChain = {"pitch", "roll", "yaw"};
    HumanoidRetargeter retargeter(target, {mapping});
    ASSERT_TRUE(retargeter.valid()) << retargeter.error();

    const glm::quat pitch = glm::angleAxis(glm::radians(10.0f), glm::vec3(1, 0, 0));
    const glm::quat roll = glm::angleAxis(glm::radians(15.0f), glm::vec3(0, 0, 1));
    const glm::quat yaw = glm::angleAxis(glm::radians(20.0f), glm::vec3(0, 1, 0));
    LocalPoseFrame source;
    source.jointNames = {"pitch", "roll", "yaw"};
    source.localRotations = {pitch, roll, yaw};
    std::vector<glm::quat> base = {target.bones[0].localRotation,
                                   target.bones[1].localRotation,
                                   target.bones[2].localRotation};
    std::vector<glm::quat> result;
    ASSERT_TRUE(retargeter.retarget(source, base, result));
    expectSameRotation(result[0], target.bones[0].localRotation * pitch * roll * yaw);
}
