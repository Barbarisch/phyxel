#include <gtest/gtest.h>
#include "scene/AnimatedVoxelCharacter.h"

#include <memory>

using namespace Phyxel;
using Phyxel::Scene::AnimatedVoxelCharacter;
using Phyxel::Scene::AnimatedCharacterState;

// Phase D step 5: clip selection is extracted into clipForState(state,
// sprinting) layered mapping -> plan clipDefaults -> legacy switch. This
// table pins the HUMANOID resolution for every state x sprint x mapping
// combination to the literal pre-refactor switch (transcribed by hand from
// the old code, NOT derived from the code under test). The humanoid plan's
// clipDefaults are empty, so layers 1+3 must reproduce the old behavior
// exactly; any drift here is a real regression, not noise.

namespace {

struct Expect {
    AnimatedCharacterState state;
    const char* normal;   // expected clip, not sprinting
    const char* sprint;   // expected clip, sprinting (same unless variant)
};

// Transcribed verbatim from the pre-refactor FSM switch
// (AnimatedVoxelCharacter.cpp, input-driven fork). Member-variable-driven
// states (Attack/Block/Cast/Dodge/HitReact/Death/Celebrate) are asserted
// separately for their EMPTY-member fallbacks.
const Expect kLegacyTable[] = {
    { AnimatedCharacterState::Idle,          "idle",                 "idle" },
    { AnimatedCharacterState::StartWalk,     "start_walking",        "start_walking" },
    { AnimatedCharacterState::Walk,          "walk",                 "walk" },
    { AnimatedCharacterState::Run,           "run",                  "fast_run" },
    { AnimatedCharacterState::Jump,          "jump",                 "jump" },
    { AnimatedCharacterState::Fall,          "jump_down",            "jump_down" },
    { AnimatedCharacterState::Land,          "landing",              "landing" },
    { AnimatedCharacterState::Crouch,        "standing_to_crouched", "standing_to_crouched" },
    { AnimatedCharacterState::CrouchIdle,    "crouch_idle",          "crouch_idle" },
    { AnimatedCharacterState::CrouchWalk,    "crouched_walking",     "crouched_walking" },
    { AnimatedCharacterState::StandUp,       "crouch_to_stand",      "crouch_to_stand" },
    { AnimatedCharacterState::KnockedOut,    "ko_lay",               "ko_lay" },
    { AnimatedCharacterState::GetUp,         "get_up",               "get_up" },
    { AnimatedCharacterState::TurnLeft,      "left_turn",            "left_turn" },
    { AnimatedCharacterState::TurnRight,     "right_turn",           "right_turn" },
    { AnimatedCharacterState::StrafeLeft,    "left_strafe_walk",     "left_strafe" },
    { AnimatedCharacterState::StrafeRight,   "right_strafe_walk",    "right_strafe" },
    { AnimatedCharacterState::WalkStrafeLeft,  "left_strafe_walk",   "left_strafe" },
    { AnimatedCharacterState::WalkStrafeRight, "right_strafe_walk",  "right_strafe" },
    { AnimatedCharacterState::BackwardWalk,  "walking_backward",     "walking_backward" },
    { AnimatedCharacterState::StopWalk,      "female_stop_walking",  "female_stop_walking" },
    { AnimatedCharacterState::StopRun,       "run_to_stop",          "run_to_stop" },
    { AnimatedCharacterState::ClimbStairs,   "stair_up",             "stair_up" },
    { AnimatedCharacterState::DescendStairs, "stair_down",           "stair_down" },
    { AnimatedCharacterState::SitDown,       "stand_to_sit",         "stand_to_sit" },
    { AnimatedCharacterState::SittingIdle,   "sitting_idle",         "sitting_idle" },
    { AnimatedCharacterState::SitStandUp,    "sit_to_stand",         "sit_to_stand" },
    { AnimatedCharacterState::Preview,       "",                     "" },
};

std::unique_ptr<AnimatedVoxelCharacter> makeHumanoid() {
    auto ch = std::make_unique<AnimatedVoxelCharacter>(nullptr, glm::vec3(0.0f));
    EXPECT_TRUE(ch->loadModel("resources/animated_characters/humanoid.anim"));
    return ch;
}

} // namespace

TEST(ClipSelection, HumanoidMatchesLegacySwitchExactly) {
    auto ch = makeHumanoid();
    for (const auto& e : kLegacyTable) {
        EXPECT_EQ(ch->clipForState(e.state, false), e.normal)
            << "state " << (int)e.state << " (normal)";
        EXPECT_EQ(ch->clipForState(e.state, true), e.sprint)
            << "state " << (int)e.state << " (sprint)";
    }
}

TEST(ClipSelection, EmptyMemberFallbacksMatchLegacy) {
    // Member-variable states with empty members must resolve to the legacy
    // fallbacks (Attack's empty m_currentAttackClip resolved "" pre-refactor
    // — the FSM handled that downstream; preserve it verbatim).
    auto ch = makeHumanoid();
    EXPECT_EQ(ch->clipForState(AnimatedCharacterState::Block, false), "body_block");
    EXPECT_EQ(ch->clipForState(AnimatedCharacterState::Dodge, false), "roll_forward");
    EXPECT_EQ(ch->clipForState(AnimatedCharacterState::HitReact, false), "idle");
    EXPECT_EQ(ch->clipForState(AnimatedCharacterState::Death, false), "idle");
    EXPECT_EQ(ch->clipForState(AnimatedCharacterState::Celebrate, false), "taunt");
    EXPECT_EQ(ch->clipForState(AnimatedCharacterState::Cast, false), "idle");
    // m_currentAttackClip is initialized to "attack" (not empty) — the
    // legacy default swing before any moveset is equipped.
    EXPECT_EQ(ch->clipForState(AnimatedCharacterState::Attack, false), "attack");
}

TEST(ClipSelection, MappingOverridesEverything) {
    auto ch = makeHumanoid();
    ch->setAnimationMapping("Walk", "scamper_walk");
    ch->setAnimationMapping("SitDown", "hop_onto_seat");
    EXPECT_EQ(ch->clipForState(AnimatedCharacterState::Walk, false), "scamper_walk");
    EXPECT_EQ(ch->clipForState(AnimatedCharacterState::Walk, true), "scamper_walk");
    // The sitAt defect fix: SitDown mapping must reach clip selection.
    EXPECT_EQ(ch->clipForState(AnimatedCharacterState::SitDown, false), "hop_onto_seat");
    ch->removeAnimationMapping("Walk");
    EXPECT_EQ(ch->clipForState(AnimatedCharacterState::Walk, false), "walk");
}

TEST(ClipSelection, WolfPlanDefaultsFillTheVocabulary) {
    // Creature plans provide their clip vocabulary through layer 2 — no
    // per-character mapping needed for a wolf to walk/idle/sit.
    auto ch = std::make_unique<AnimatedVoxelCharacter>(nullptr, glm::vec3(0.0f));
    ASSERT_TRUE(ch->loadModel("resources/animated_characters/character_wolf.anim"));
    EXPECT_EQ(ch->clipForState(AnimatedCharacterState::Idle, false),
              "Wolf_Skeleton|Wolf_Idle_");
    EXPECT_EQ(ch->clipForState(AnimatedCharacterState::Walk, false),
              "Wolf_Skeleton|Wolf_Walk_cycle_");
    EXPECT_EQ(ch->clipForState(AnimatedCharacterState::Run, false),
              "Wolf_Skeleton|Wolf_Run_Cycle_");
    EXPECT_EQ(ch->clipForState(AnimatedCharacterState::SittingIdle, false),
              "Wolf_Skeleton|Wolf_seat_");
    // Mapping still outranks the plan.
    ch->setAnimationMapping("Walk", "Wolf_Skeleton|Wolf_creep_cycle");
    EXPECT_EQ(ch->clipForState(AnimatedCharacterState::Walk, false),
              "Wolf_Skeleton|Wolf_creep_cycle");
    // States with no plan default fall through to the legacy switch names
    // (which the wolf rig simply doesn't have — the FSM's missing-clip
    // handling covers that, same as pre-refactor).
    EXPECT_EQ(ch->clipForState(AnimatedCharacterState::Jump, false), "jump");
}
