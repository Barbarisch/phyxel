#include <gtest/gtest.h>
#include "scene/AnimatedVoxelCharacter.h"
#include "scene/AppearancePresetRegistry.h"

using namespace Phyxel::Scene;

// Phase B (docs/CharacterLibraryPlan.md): collision must match the body.
// Capsule half-width was hardcoded 0.25 and step height fixed at 4/9 for every
// character regardless of proportions — a goliath collided like a halfling.
// These tests pin the scaled behavior. Characters are constructed without a
// physics world (loadModel/resizeController don't need one); tests run from
// the repo root so humanoid.anim and the preset JSON load.

namespace {

AnimatedVoxelCharacter* makeCharacter(const char* presetId) {
    auto& presets = AppearancePresetRegistry::instance();
    presets.ensureLoaded();
    auto* ch = new AnimatedVoxelCharacter(nullptr, glm::vec3(0.0f));
    if (presetId) {
        const auto* preset = presets.getPreset(presetId);
        EXPECT_NE(preset, nullptr) << presetId;
        if (preset) ch->setAppearance(*preset);
    }
    EXPECT_TRUE(ch->loadModel("resources/animated_characters/humanoid.anim"));
    return ch;
}

} // namespace

TEST(CharacterCapsuleScaling, StandardKeepsLegacyCapsuleAndStep) {
    // Golden: an unproportioned character must keep the exact legacy values —
    // no behavior change for every existing standard human in shipped content.
    std::unique_ptr<AnimatedVoxelCharacter> ch(makeCharacter(nullptr));
    EXPECT_NEAR(ch->getControllerHalfWidth(), 0.25f, 0.01f);
    EXPECT_NEAR(ch->getMaxStepHeight(), 4.0f / 9.0f, 0.01f);
}

TEST(CharacterCapsuleScaling, GoliathCapsuleWiderThanStandard) {
    std::unique_ptr<AnimatedVoxelCharacter> ch(makeCharacter("goliath"));
    EXPECT_GT(ch->getControllerHalfWidth(), 0.26f)
        << "goliath (1.30 bulk / 1.25 shoulders) must not collide like a standard human";
}

TEST(CharacterCapsuleScaling, HalflingCapsuleNarrowerThanStandard) {
    std::unique_ptr<AnimatedVoxelCharacter> ch(makeCharacter("halfling"));
    EXPECT_LT(ch->getControllerHalfWidth(), 0.24f);
}

TEST(CharacterCapsuleScaling, StepHeightScalesWithLegs) {
    std::unique_ptr<AnimatedVoxelCharacter> goliath(makeCharacter("goliath"));
    std::unique_ptr<AnimatedVoxelCharacter> halfling(makeCharacter("halfling"));
    EXPECT_GT(goliath->getMaxStepHeight(), 4.0f / 9.0f + 0.02f);
    EXPECT_LT(halfling->getMaxStepHeight(), 4.0f / 9.0f - 0.02f);
}

TEST(CharacterCapsuleScaling, StepHeightFlooredAtSubcubeStair) {
    // Structures are built on the 1/3-voxel subcube-step grid — every race,
    // however small, must still climb a 1/3 riser (L3 navigability contract).
    std::unique_ptr<AnimatedVoxelCharacter> halfling(makeCharacter("halfling"));
    EXPECT_GE(halfling->getMaxStepHeight(), 1.0f / 3.0f + 0.005f);
}

TEST(CharacterCapsuleScaling, CapsuleWidthClamped) {
    // Whatever the preset does, width stays inside sane bounds.
    for (const char* id : {"halfling", "gnome", "dwarf", "elf", "tiefling",
                           "half_orc", "dragonborn", "goliath", "giant",
                           "goblin", "ogre"}) {
        std::unique_ptr<AnimatedVoxelCharacter> ch(makeCharacter(id));
        EXPECT_GE(ch->getControllerHalfWidth(), 0.12f) << id;
        EXPECT_LE(ch->getControllerHalfWidth(), 0.60f) << id;
    }
}

TEST(CharacterCapsuleScaling, OgreIsTheWidestAndStepsHighest) {
    // The Large-monster extreme must out-collide the biggest playable race.
    std::unique_ptr<AnimatedVoxelCharacter> ogre(makeCharacter("ogre"));
    std::unique_ptr<AnimatedVoxelCharacter> goliath(makeCharacter("goliath"));
    EXPECT_GT(ogre->getControllerHalfWidth(), goliath->getControllerHalfWidth());
    EXPECT_GE(ogre->getMaxStepHeight(), goliath->getMaxStepHeight());
    EXPECT_LE(ogre->getMaxStepHeight(), 0.70f);   // cap: no gliding over half-walls
}
