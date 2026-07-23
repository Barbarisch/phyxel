#include <gtest/gtest.h>
#include "scene/AnimatedVoxelCharacter.h"
#include "graphics/AnimationSystem.h"

#include <algorithm>

using namespace Phyxel;
using Phyxel::Scene::AnimatedVoxelCharacter;

// Pins the character-facing convention (docs/CoordinateSystem.md "Character
// Facing"): model-space forward — the face — is +Z. Facing mistakes on
// variant rigs (features sculpted onto the back of the head) have recurred;
// these tests make the convention executable instead of tribal knowledge.

TEST(CharacterFacing, ForwardAtYawZeroIsPositiveZ) {
    AnimatedVoxelCharacter ch(nullptr, glm::vec3(0.0f));
    glm::vec3 fwd = ch.getForwardDirection();
    EXPECT_NEAR(fwd.x, 0.0f, 1e-5f);
    EXPECT_NEAR(fwd.y, 0.0f, 1e-5f);
    EXPECT_NEAR(fwd.z, 1.0f, 1e-5f);
}

TEST(CharacterFacing, OgreFaceFeaturesAreOnPositiveZ) {
    // The ogre variant rig's jaw/tusks (the boxes with explicit colors, plus
    // the jaw slab beyond the head shell) must sit on the +Z side of the
    // head bone. Fails loudly if a re-derive ships a backward face.
    Skeleton skeleton;
    std::vector<AnimationClip> clips;
    VoxelModel model;
    AnimationSystem anim;
    ASSERT_TRUE(anim.loadFromFile("resources/animated_characters/ogre.anim",
                                  skeleton, clips, model));

    int headId = -1;
    for (const auto& b : skeleton.bones) {
        std::string low = b.name;
        std::transform(low.begin(), low.end(), low.begin(), ::tolower);
        if (low.size() >= 4 && low.compare(low.size() - 4, 4, "head") == 0) headId = b.id;
    }
    ASSERT_GE(headId, 0);

    int featureBoxes = 0;
    for (const auto& s : model.shapes) {
        if (s.boneId != headId) continue;
        const bool hasExplicitColor = s.color.a > 0.0f;
        const bool protrudesFront = s.offset.z > 0.17f;   // beyond the head shell (max ~0.164)
        if (hasExplicitColor || protrudesFront) {
            ++featureBoxes;
            EXPECT_GT(s.offset.z, 0.0f)
                << "face feature on the BACK of the head (z=" << s.offset.z
                << ") — model-space face is +Z, see docs/CoordinateSystem.md";
        }
    }
    EXPECT_GE(featureBoxes, 3) << "expected jaw + 2 tusks on the ogre rig";
}
