#include <gtest/gtest.h>

#include "scene/AnimatedVoxelCharacter.h"
#include "scene/BodyPlan.h"
#include "scene/motion/MotionOracle.h"
#include "core/ChunkManager.h"
#include "core/Chunk.h"
#include "physics/PhysicsWorld.h"
#include "physics/VoxelOccupancyGrid.h"
#include "physics/VoxelDynamicsWorld.h"

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
using Phyxel::Scene::BodyPlan;
using Phyxel::Scene::BodyPlanRegistry;

// ============================================================================
// A3 — transition graph (docs/AnimationSystemV3Plan.md §4 A3): per-edge blend seconds from
// the plan, foot-phase-synced locomotion switches (walk↔run enter at the SAME gait phase,
// read off `stanceL`/`stanceR` clip_meta markers), and root motion that keeps flowing through
// a blend. RED 2026-09-30: humanoid.json has no edges, no clip carries stance markers, every
// switch restarts at the phase seed, and root motion pauses for the whole crossfade.
// ============================================================================

namespace {

constexpr const char* kHumanoid = "resources/animated_characters/humanoid.anim";
constexpr float kDt = 1.0f / 60.0f;

struct FloorWorld {
    std::unique_ptr<Phyxel::Physics::PhysicsWorld> physics;
    ChunkManager cm;
    std::vector<std::unique_ptr<Phyxel::Physics::VoxelOccupancyGrid>> grids;
    FloorWorld() {
        physics = std::make_unique<Phyxel::Physics::PhysicsWorld>();
        physics->initialize();
        cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
        auto owned = std::make_unique<Chunk>(glm::ivec3(0, 0, 0));
        owned->initializeForLoading();
        cm.chunkMap[glm::ivec3(0, 0, 0)] = owned.get();
        cm.chunks.push_back(std::move(owned));
        auto g = std::make_unique<Phyxel::Physics::VoxelOccupancyGrid>();
        g->setChunkOrigin(glm::ivec3(0, 0, 0));
        for (int x = 0; x < 32; ++x)
            for (int z = 0; z < 32; ++z) g->setCube(glm::ivec3(x, 15, z), true);
        physics->getVoxelWorld()->registerGrid(g.get());
        grids.push_back(std::move(g));
    }
    std::unique_ptr<AnimatedVoxelCharacter> character(const char* rig, glm::vec3 pos = {16, 16.05f, 4}) {
        auto ch = std::make_unique<AnimatedVoxelCharacter>(physics.get(), pos);
        EXPECT_TRUE(ch->loadModel(rig)) << rig;
        ch->setChunkManager(&cm);
        return ch;
    }
};

int clipIndex(const AnimatedVoxelCharacter& ch, const std::string& name) {
    const auto& clips = ch.getAnimationClips();
    for (size_t i = 0; i < clips.size(); ++i) if (clips[i].name == name) return (int)i;
    return -1;
}

float frac(float x) { return x - std::floor(x); }
float phaseDistance(float a, float b) { float d = frac(a - b); return std::min(d, 1.0f - d); }

// Gait phase of a clip at a time: cycle fraction measured from the LEFT foot plant.
float gaitPhase(const AnimationClip& c, float t) {
    const float cycle = frac(t / c.duration);
    return (c.stanceL >= 0.0f) ? frac(cycle - c.stanceL) : cycle;
}

// Minimal rig with a root-motion clip (same writer shape as AnimationDeconfoundTest).
std::string writeStepRig() {
    const auto dir = std::filesystem::temp_directory_path() / "phyxel_transition_graph";
    std::filesystem::create_directories(dir);
    const auto path = (dir / "step.anim").string();
    std::ofstream f(path, std::ios::trunc);
    f << "# archetype: humanoid_normal\n";
    f << "SKELETON\nBoneCount 2\n";
    f << "Bone 0 mixamorig:Hips -1 0 1 0 0 0 0 1 1 1 1\n";
    f << "Bone 1 mixamorig:Spine 0 0 0.2 0 0 0 0 1 1 1 1\n";
    f << "MODEL\nBoxCount 1\nBox 0 0.3 0.3 0.3 0 0 0\n";
    f << "ANIMATION idle\nDuration 1\nBoneChannelCount 1\nChannel 0 2 1 0\nP 0 0 1 0\nP 1 0 1 0\nR 0 0 0 0 1\n";
    f << "ANIMATION step\nDuration 1\nRootMotion 0 0 1\nBoneChannelCount 1\nChannel 0 2 1 0\nP 0 0 1 0\nP 1 0 1 1\nR 0 0 0 0 1\n";
    return path;
}

} // namespace

TEST(TransitionGraph, HumanoidPlanDeclaresPhaseSyncedLocomotionEdgesAndAWildcard) {
    auto& reg = BodyPlanRegistry::instance();
    reg.clear(); reg.ensureLoaded();
    const BodyPlan& plan = reg.planFor(Scene::MorphologyType::Humanoid);
    const auto* walkRun = plan.findTransition("Walk", "Run");
    ASSERT_NE(walkRun, nullptr) << "humanoid.json must declare Walk->Run";
    EXPECT_TRUE(walkRun->phaseSync);
    EXPECT_GT(walkRun->blend, 0.0f);
    const auto* hit = plan.findTransition("Walk", "HitReact");
    ASSERT_NE(hit, nullptr) << "a '*' -> HitReact edge covers every source state";
    EXPECT_LT(hit->blend, plan.defaultBlend) << "a hit reaction cuts faster than the default";
    EXPECT_EQ(plan.findTransition("SitDown", "SittingIdle"), nullptr) << "undeclared edges use the default";
    EXPECT_FLOAT_EQ(plan.defaultBlend, 0.2f);
}

TEST(TransitionGraph, HumanoidLocomotionClipsCarryStanceMarkers) {
    FloorWorld w;
    auto ch = w.character(kHumanoid);
    for (const char* name : {"walk", "run", "fast_run", "left_strafe_walk", "right_strafe_walk", "left_strafe", "right_strafe", "walking_backward"}) {
        const int i = clipIndex(*ch, name);
        ASSERT_GE(i, 0) << name;
        const auto& c = ch->getAnimationClips()[i];
        EXPECT_GE(c.stanceL, 0.0f) << name << " has no stanceL marker (run anim_lint.py stance --write)";
        EXPECT_LT(c.stanceL, 1.0f) << name;
        EXPECT_GE(c.stanceR, 0.0f) << name;
        EXPECT_LT(c.stanceR, 1.0f) << name;
        // Alternating gaits plant half a cycle apart; strafes are side-step gaits and are not.
        const std::string n = name;
        if (c.stanceL >= 0.0f && c.stanceR >= 0.0f && n.find("strafe") == std::string::npos)
            EXPECT_NEAR(phaseDistance(c.stanceL, c.stanceR), 0.5f, 0.15f) << name << ": feet plant half a cycle apart";
    }
}

TEST(TransitionGraph, WalkToRunEntersAtTheSameGaitPhaseForTenSeededSwitchTimes) {
    // Ten different walk phases at the moment of the switch; every one must land on the
    // matching run phase. Control: the phase the OLD rule would have chosen (the character's
    // seed × duration) differs from the synced start on most seeds — the sync is doing work.
    int synced = 0, differsFromSeed = 0;
    for (int seed = 0; seed < 10; ++seed) {
        FloorWorld w;
        auto ch = w.character(kHumanoid);
        for (int i = 0; i < 60 + seed * 7; ++i) { ch->setControlInput(-0.5f, 0.0f, 0.0f); ch->update(kDt); }
        ASSERT_EQ(ch->getCurrentClipName(), "walk") << "seed " << seed;
        const int walkIdx = ch->getCurrentClipIndex();
        const float walkTimeBefore = ch->getAnimTime();
        ch->setControlInput(-1.0f, 0.0f, 0.0f);           // |fwd| > 0.6 → Run
        ch->update(kDt);
        ASSERT_EQ(ch->getCurrentClipName(), "run") << "seed " << seed;
        const auto& clips = ch->getAnimationClips();
        const float runStart = ch->getAnimTime() - kDt * ch->getPlaybackRateForTest();   // time the run clip was entered at
        const float pWalk = gaitPhase(clips[walkIdx], walkTimeBefore);
        const float pRun  = gaitPhase(clips[ch->getCurrentClipIndex()], runStart);
        const float d = phaseDistance(pWalk, pRun);
        std::printf("[TransitionGraph] seed %d walk phase %.3f -> run phase %.3f (dist %.3f) blend %.2fs\n",
                    seed, pWalk, pRun, d, ch->activeBlendDuration());
        if (d < 0.05f) ++synced;
        const float seedStart = ch->getPhaseJitter() * clips[ch->getCurrentClipIndex()].duration;
        if (std::fabs(seedStart - runStart) > 0.02f) ++differsFromSeed;
        EXPECT_FLOAT_EQ(ch->activeBlendDuration(), 0.25f) << "Walk->Run edge blend";
    }
    EXPECT_EQ(synced, 10) << "switches that kept the gait phase within 5% of a cycle";
    EXPECT_GE(differsFromSeed, 7) << "control: the synced start must not just equal the phase seed";
}

TEST(TransitionGraph, RootMotionKeepsFlowingThroughTheBlend) {
    // Tiny rig: `step` translates the root +1 u along +Z over 1 s with RootMotion 0 0 1.
    // Blending idle→step over 0.2 s must move the character ~0.2 u; today the blend branch
    // parks m_prevRootPos and applies nothing until the crossfade ends.
    FloorWorld w;
    auto ch = w.character(writeStepRig().c_str());
    ch->setBlendDuration(0.2f);
    // Preview keeps playAnimation() sticky; in any other state the FSM re-selects its own clip
    // on the next update (the review-panel footgun, A2), and `step` would never play.
    ch->forceState(AnimatedCharacterState::Preview);
    ch->playAnimation("idle");
    for (int i = 0; i < 30; ++i) ch->update(kDt);
    const glm::vec3 before = ch->getPosition();
    ch->playAnimation("step");
    for (int i = 0; i < 12; ++i) ch->update(kDt);                 // exactly the 0.2 s blend window
    const float moved = ch->getPosition().z - before.z;
    EXPECT_NEAR(moved, 0.2f, 0.06f) << "root motion during the crossfade (u); 0 = paused";
}
