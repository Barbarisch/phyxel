#include <gtest/gtest.h>

#include "scene/AnimatedVoxelCharacter.h"

#include <memory>

using namespace Phyxel;
using Phyxel::Scene::AnimatedVoxelCharacter;
using Phyxel::Scene::AnimatedCharacterState;

// ============================================================================
// A2 — ONE clip-selection function (docs/AnimationSystemV3Plan.md §4 A2).
// Today the external-velocity (behaviour-driven NPC) path has its own selector that only
// ever picks Walk or Idle and never runs the FSM, so a behaviour-driven NPC that is hit
// never flinches: hitReact() sets a request that only updateStateMachine() consumes.
// RED on 2026-09-29.
// ============================================================================

namespace {
constexpr const char* kHumanoid = "resources/animated_characters/humanoid.anim";
constexpr float kDt = 1.0f / 60.0f;
}

TEST(SelectClip, BehaviourDrivenNpcReactsToAHit) {
    AnimatedVoxelCharacter ch(nullptr, glm::vec3(0.0f, 16.05f, 0.0f));
    ASSERT_TRUE(ch.loadModel(kHumanoid));
    ch.playAnimation("idle");
    for (int i = 0; i < 10; ++i) { ch.setMoveVelocity(glm::vec3(0.0f, 0.0f, 1.0f)); ch.update(kDt); }
    ASSERT_EQ(ch.getCurrentClipName(), "walk") << "precondition: the velocity path is walking";

    ch.hitReact(false);
    bool reacted = false;
    for (int i = 0; i < 10 && !reacted; ++i) {
        ch.setMoveVelocity(glm::vec3(0.0f, 0.0f, 1.0f));
        ch.update(kDt);
        reacted = ch.getAnimationState() == AnimatedCharacterState::HitReact;
    }
    EXPECT_TRUE(reacted) << "hitReact() was never consumed on the external-velocity path (state="
                         << ch.stateToString(ch.getAnimationState()) << ", clip=" << ch.getCurrentClipName() << ")";
}

TEST(SelectClip, BehaviourDrivenNpcStillWalksAndIdlesThroughTheUnifiedSelector) {
    AnimatedVoxelCharacter ch(nullptr, glm::vec3(0.0f, 16.05f, 0.0f));
    ASSERT_TRUE(ch.loadModel(kHumanoid));
    ch.playAnimation("idle");
    for (int i = 0; i < 10; ++i) { ch.setMoveVelocity(glm::vec3(0.0f, 0.0f, 1.0f)); ch.update(kDt); }
    EXPECT_EQ(ch.getCurrentClipName(), "walk");
    for (int i = 0; i < 10; ++i) { ch.setMoveVelocity(glm::vec3(0.0f)); ch.update(kDt); }
    EXPECT_EQ(ch.getCurrentClipName(), "idle");
}
