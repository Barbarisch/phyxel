#include <gtest/gtest.h>

#include "scene/AnimatedVoxelCharacter.h"
#include "graphics/ClipMetaSchema.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace Phyxel;
using Phyxel::Scene::AnimatedVoxelCharacter;
using Phyxel::Scene::AnimatedCharacterState;

// ============================================================================
// A3 — bone masks + additive layers (docs/AnimationSystemV3Plan.md §3b, §4 A3). The single
// mechanism behind grip / load / condition / mood: a `role=layer` clip whose factor
// coordinates match the character's is composed over the base pose on the bones of its
// `mask` — additively (delta from the layer's own frame 0) or as an override. Legs stay the
// base's. RED 2026-09-30: no layer is ever applied; masks resolve to nothing.
// ============================================================================

namespace {

constexpr float kDt = 1.0f / 60.0f;

std::string q(float deg, float x, float y, float z) {
    const glm::quat r = glm::angleAxis(glm::radians(deg), glm::normalize(glm::vec3(x, y, z)));
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%.6f %.6f %.6f %.6f", r.x, r.y, r.z, r.w);
    return buf;
}

// Four bones: Hips(root) -> Spine -> LeftArm ; Hips -> LeftUpLeg. Mixamo names so the humanoid
// plan resolves the arm segment and the left leg's upper bone (that is all the masks need).
std::string writeLayerRig() {
    const auto dir = std::filesystem::temp_directory_path() / "phyxel_layer_composition";
    std::filesystem::create_directories(dir);
    const auto path = (dir / "layers.anim").string();
    std::ofstream f(path, std::ios::trunc);
    f << "# archetype: humanoid_normal\n";
    f << "# clip_meta: walk type=locomotion role=base gait=biped state=walk\n";
    f << "# clip_meta: carry_2h role=layer grip=2h_heavy mask=upper additive=1\n";
    f << "# clip_meta: guard_shield role=layer grip=1h_shield mask=arms additive=0\n";
    f << "SKELETON\nBoneCount 4\n";
    f << "Bone 0 mixamorig:Hips -1 0 1 0 0 0 0 1 1 1 1\n";
    f << "Bone 1 mixamorig:Spine 0 0 0.2 0 0 0 0 1 1 1 1\n";
    f << "Bone 2 mixamorig:LeftArm 1 0.2 0.3 0 0 0 0 1 1 1 1\n";
    f << "Bone 3 mixamorig:LeftUpLeg 0 0.1 -0.1 0 0 0 0 1 1 1 1\n";
    f << "MODEL\nBoxCount 1\nBox 0 0.3 0.3 0.3 0 0 0\n";
    // base: leg swings 0->30 deg about X over 1 s; arm holds 10 deg about X
    f << "ANIMATION walk\nDuration 1\nBoneChannelCount 2\n";
    f << "Channel 3 0 2 0\nR 0 " << q(0, 1, 0, 0) << "\nR 1 " << q(30, 1, 0, 0) << "\n";
    f << "Channel 2 0 1 0\nR 0 " << q(10, 1, 0, 0) << "\n";
    // additive carry: frame 0 is the neutral reference; by t=1 the arm is +45 deg about Z, spine +5 deg
    f << "ANIMATION carry_2h\nDuration 1\nBoneChannelCount 2\n";
    f << "Channel 2 0 2 0\nR 0 " << q(0, 0, 0, 1) << "\nR 1 " << q(45, 0, 0, 1) << "\n";
    f << "Channel 1 0 2 0\nR 0 " << q(0, 1, 0, 0) << "\nR 1 " << q(5, 1, 0, 0) << "\n";
    // override guard: arm held at 90 deg about Y
    f << "ANIMATION guard_shield\nDuration 1\nBoneChannelCount 1\n";
    f << "Channel 2 0 1 0\nR 0 " << q(90, 0, 1, 0) << "\n";
    return path;
}

float angleBetweenDeg(const glm::quat& a, const glm::quat& b) {
    const glm::quat d = glm::inverse(a) * b;
    return glm::degrees(glm::angle(glm::normalize(d)));
}

std::unique_ptr<AnimatedVoxelCharacter> rigInPreview() {
    auto ch = std::make_unique<AnimatedVoxelCharacter>(nullptr, glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_TRUE(ch->loadModel(writeLayerRig()));
    ch->forceState(AnimatedCharacterState::Preview);     // keep the FSM out; playAnimation is sticky here
    ch->playAnimation("walk");
    return ch;
}

void run(AnimatedVoxelCharacter& ch, int frames) { for (int i = 0; i < frames; ++i) ch.update(kDt); }

const Bone& bone(const AnimatedVoxelCharacter& ch, const char* name) {
    const auto& sk = ch.getSkeleton();
    return sk.bones[sk.boneMap.at(name)];
}

} // namespace

TEST(LayerComposition, MasksResolveFromThePlanRoles) {
    auto ch = rigInPreview();
    EXPECT_EQ(ch->maskBones("legs"),  (std::vector<int>{3})) << "leg upper bones and their subtrees";
    EXPECT_EQ(ch->maskBones("arms"),  (std::vector<int>{2})) << "isArm segment roots and their subtrees";
    EXPECT_EQ(ch->maskBones("upper"), (std::vector<int>{1, 2})) << "everything but the root and the legs";
    EXPECT_TRUE(ch->maskBones("none").empty());
}

TEST(LayerComposition, NoMatchingLayerLeavesTheBasePoseUntouched) {
    auto ch = rigInPreview();
    run(*ch, 61);
    EXPECT_TRUE(ch->activeLayers().empty());
    EXPECT_NEAR(angleBetweenDeg(bone(*ch, "mixamorig:LeftArm").currentRotation, glm::angleAxis(glm::radians(10.0f), glm::vec3(1, 0, 0))), 0.0f, 0.5f);
}

TEST(LayerComposition, AdditiveUpperLayerComposesOverTheBaseAndLeavesLegsAlone) {
    auto ch = rigInPreview();
    run(*ch, 61);
    const glm::quat legBefore = bone(*ch, "mixamorig:LeftUpLeg").currentRotation;
    const glm::quat armBase   = bone(*ch, "mixamorig:LeftArm").currentRotation;

    ClipMeta::Factors f; f.state = "walk"; f.grip = "2h_heavy";
    ch->setCompositionFactors(f);
    run(*ch, 90);                                          // layer reaches t>=1 and full weight
    ASSERT_EQ(ch->activeLayers().size(), 1u);
    EXPECT_EQ(ch->getAnimationClips()[ch->activeLayers()[0].clipIndex].name, "carry_2h");
    EXPECT_NEAR(ch->activeLayers()[0].weight, 1.0f, 1e-3f);

    // The arm is the base's 10 deg X composed with the layer's +45 deg Z delta: 45 deg away from base.
    EXPECT_NEAR(angleBetweenDeg(armBase, bone(*ch, "mixamorig:LeftArm").currentRotation), 45.0f, 1.0f);
    // The leg is outside the upper mask: identical to the un-layered base at the same phase.
    // (walk loops every 1 s and 61+90 frames = 151 frames ≡ 31 frames into the cycle; compare
    //  against the base evaluated at that phase.)
    const float phase = std::fmod(151 * kDt, 1.0f);
    const glm::quat legExpected = glm::angleAxis(glm::radians(30.0f * phase), glm::vec3(1, 0, 0));
    EXPECT_NEAR(angleBetweenDeg(bone(*ch, "mixamorig:LeftUpLeg").currentRotation, legExpected), 0.0f, 1.0f);
    (void)legBefore;
    // The root is never in a mask.
    EXPECT_NEAR(angleBetweenDeg(bone(*ch, "mixamorig:Hips").currentRotation, glm::quat(1, 0, 0, 0)), 0.0f, 1e-3f);
}

TEST(LayerComposition, OverrideArmsLayerReplacesTheArmPose) {
    auto ch = rigInPreview();
    ClipMeta::Factors f; f.state = "walk"; f.grip = "1h_shield";
    ch->setCompositionFactors(f);
    run(*ch, 90);
    ASSERT_EQ(ch->activeLayers().size(), 1u);
    EXPECT_FALSE(ch->activeLayers()[0].additive);
    EXPECT_NEAR(angleBetweenDeg(bone(*ch, "mixamorig:LeftArm").currentRotation, glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0))), 0.0f, 1.0f);
    // Spine is outside the arms mask.
    EXPECT_NEAR(angleBetweenDeg(bone(*ch, "mixamorig:Spine").currentRotation, glm::quat(1, 0, 0, 0)), 0.0f, 1e-3f);
}

TEST(LayerComposition, ChangingFactorsFadesTheOldLayerOutAndTheNewOneIn) {
    auto ch = rigInPreview();
    ClipMeta::Factors f; f.state = "walk"; f.grip = "2h_heavy";
    ch->setCompositionFactors(f);
    run(*ch, 90);
    f.grip = "empty";
    ch->setCompositionFactors(f);
    run(*ch, 3);                                           // mid fade-out: still present, weight < 1
    ASSERT_EQ(ch->activeLayers().size(), 1u);
    EXPECT_LT(ch->activeLayers()[0].weight, 1.0f);
    run(*ch, 30);
    EXPECT_TRUE(ch->activeLayers().empty()) << "a faded-out layer is dropped";
}
