#include <gtest/gtest.h>

#include "scene/AnimatedVoxelCharacter.h"

using namespace Phyxel;
using namespace Phyxel::Scene;
using namespace Phyxel::Scene::Motion;

namespace {
void makeRig(Skeleton& skeleton, VoxelModel& model, std::vector<AnimationClip>& clips) {
    skeleton.addBone("Hips", -1, {0, 1, 0}, glm::quat(1, 0, 0, 0), {1, 1, 1});
    skeleton.addBone("Finger", 0, {0, 0.5f, 0}, glm::quat(1, 0, 0, 0), {1, 1, 1});
    model.shapes.push_back({0, {0.4f, 0.4f, 0.4f}, {0, 0, 0}});
    AnimationClip idle;
    idle.name = "idle";
    idle.duration = 1.0f;
    AnimationChannel hips;
    hips.boneId = 0;
    hips.rotationKeys = {{0.0f, glm::quat(1, 0, 0, 0)},
                         {1.0f, glm::quat(1, 0, 0, 0)}};
    idle.channels.push_back(hips);
    clips.push_back(idle);
}

std::unique_ptr<AnimatedVoxelCharacter> makeCharacter() {
    Skeleton skeleton;
    VoxelModel model;
    std::vector<AnimationClip> clips;
    makeRig(skeleton, model, clips);
    auto character = std::make_unique<AnimatedVoxelCharacter>(nullptr, glm::vec3(0));
    if (!character->loadFromSkeleton(skeleton, model, clips)) return nullptr;
    character->forceState(AnimatedCharacterState::Idle);
    character->seekToClip(0, 0.0f);
    return character;
}
}

TEST(CharacterMotionProvider, OverridesLocomotionAndPreservesUnmappedBone) {
    auto character = makeCharacter();
    ASSERT_NE(character, nullptr);
    const glm::quat generated = glm::angleAxis(glm::radians(35.0f), glm::vec3(0, 1, 0));
    auto source = std::make_shared<DeterministicMotionSource>(
        std::vector<std::string>{"pelvis"}, std::vector<glm::quat>{generated});
    ASSERT_TRUE(character->setMotionSource(source, {{"pelvis", "Hips", glm::quat(1, 0, 0, 0)}}));

    for (int i = 0; i < 10; ++i) character->update(1.0f / 60.0f);
    EXPECT_TRUE(character->usedMotionSourceLastFrame());
    const Skeleton& result = character->getSkeleton();
    EXPECT_NEAR(std::abs(glm::dot(result.bones[0].currentRotation, generated)), 1.0f, 1.0e-5f);
    EXPECT_NEAR(std::abs(glm::dot(result.bones[1].currentRotation, glm::quat(1, 0, 0, 0))),
                1.0f, 1.0e-5f);
}

TEST(CharacterMotionProvider, AuthoredAttackNeverUsesProvider) {
    auto character = makeCharacter();
    ASSERT_NE(character, nullptr);
    auto source = std::make_shared<DeterministicMotionSource>(
        std::vector<std::string>{"pelvis"},
        std::vector<glm::quat>{glm::angleAxis(glm::radians(35.0f), glm::vec3(0, 1, 0))});
    ASSERT_TRUE(character->setMotionSource(source, {{"pelvis", "Hips", glm::quat(1, 0, 0, 0)}}));
    character->forceState(AnimatedCharacterState::Attack);

    character->update(1.0f / 60.0f);
    EXPECT_FALSE(character->usedMotionSourceLastFrame());
}

TEST(CharacterMotionProvider, ProviderFailureLeavesClipPoseUntouched) {
    auto character = makeCharacter();
    ASSERT_NE(character, nullptr);
    auto source = std::make_shared<DeterministicMotionSource>(
        std::vector<std::string>{"pelvis"}, std::vector<glm::quat>{glm::quat(1, 0, 0, 0)});
    source->setAvailable(false);
    ASSERT_TRUE(character->setMotionSource(source, {{"pelvis", "Hips", glm::quat(1, 0, 0, 0)}}));

    character->update(1.0f / 60.0f);
    EXPECT_FALSE(character->usedMotionSourceLastFrame());
    EXPECT_EQ(character->getMotionSourceStatus().effectiveProvider, "clips");
    EXPECT_NEAR(std::abs(glm::dot(character->getSkeleton().bones[0].currentRotation,
                                  glm::quat(1, 0, 0, 0))), 1.0f, 1.0e-5f);
}
