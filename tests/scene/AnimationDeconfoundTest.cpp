#include <gtest/gtest.h>

#include "scene/AnimatedVoxelCharacter.h"
#include "core/ChunkManager.h"
#include "core/Chunk.h"
#include "physics/PhysicsWorld.h"
#include "physics/VoxelOccupancyGrid.h"
#include "physics/VoxelDynamicsWorld.h"

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
// Animation System v3 — phase A0 "deconfound" (docs/AnimationSystemV3Plan.md §1.3, §4 A0).
//
// Ten defects were found by READING AnimatedVoxelCharacter.cpp on 2026-09-29. Several of
// them contaminate every animation measurement taken after them (a blend duration zeroed
// forever, seat offsets that were tuned for months while never being read, NPC feet that
// skate by construction). Each test here was written to FAIL on that code first.
// ============================================================================

namespace {

constexpr const char* kHumanoid = "resources/animated_characters/humanoid.anim";
constexpr float kDt = 1.0f / 60.0f;

// A flat floor at cube y=15 (stand at y=16) in chunk (0,0,0), same shape as
// CharacterDoorFunnelTest, so the capsule grounds and update() runs its full path.
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

    std::unique_ptr<AnimatedVoxelCharacter> character(const char* rig, glm::vec3 pos = {16, 16.05f, 16}) {
        auto ch = std::make_unique<AnimatedVoxelCharacter>(physics.get(), pos);
        EXPECT_TRUE(ch->loadModel(rig)) << rig;
        ch->setChunkManager(&cm);
        return ch;
    }
};

void settle(AnimatedVoxelCharacter& ch, int frames = 30) {
    for (int i = 0; i < frames; ++i) ch.update(kDt);
}

int clipIndex(const AnimatedVoxelCharacter& ch, const std::string& name) {
    const auto& clips = ch.getAnimationClips();
    for (size_t i = 0; i < clips.size(); ++i)
        if (clips[i].name == name) return static_cast<int>(i);
    return -1;
}

// --- a minimal rig file the parser accepts: 2 bones, 1 box, N clips --------
// Each clip: hips (bone 0) position key at t=0 and t=duration, identity rotation.
struct TinyClip {
    std::string name;
    float duration;
    glm::vec3 hipsStart;
    glm::vec3 hipsEnd;
    std::string extraLines;   // e.g. "Speed 1.5"
};

std::string writeTinyRig(const std::string& stem, const std::vector<std::string>& header,
                         const std::vector<TinyClip>& clips) {
    const auto dir = std::filesystem::temp_directory_path() / "phyxel_anim_deconfound";
    std::filesystem::create_directories(dir);
    const auto path = (dir / (stem + ".anim")).string();
    std::ofstream f(path, std::ios::trunc);
    for (const auto& h : header) f << h << "\n";
    f << "SKELETON\nBoneCount 2\n";
    f << "Bone 0 mixamorig:Hips -1 0 1 0 0 0 0 1 1 1 1\n";
    f << "Bone 1 mixamorig:Spine 0 0 0.2 0 0 0 0 1 1 1 1\n";
    f << "MODEL\nBoxCount 1\nBox 0 0.3 0.3 0.3 0 0 0\n";
    for (const auto& c : clips) {
        f << "ANIMATION " << c.name << "\nDuration " << c.duration << "\n";
        if (!c.extraLines.empty()) f << c.extraLines << "\n";
        f << "BoneChannelCount 1\n";
        f << "Channel 0 2 1 0\n";
        f << "P 0 " << c.hipsStart.x << " " << c.hipsStart.y << " " << c.hipsStart.z << "\n";
        f << "P " << c.duration << " " << c.hipsEnd.x << " " << c.hipsEnd.y << " " << c.hipsEnd.z << "\n";
        f << "R 0 0 0 0 1\n";
    }
    return path;
}

} // namespace

// ---------------------------------------------------------------------------
// #1  blendDuration must survive a sit cycle. The seated branch hard-cuts between the
//     three sit clips by writing blendDuration = 0 and never restores it, so EVERY later
//     crossfade on that character is a cut.
// ---------------------------------------------------------------------------
TEST(AnimationDeconfound, BlendDurationSurvivesSitCycle) {
    FloorWorld w;
    auto ch = w.character(kHumanoid);
    ch->playAnimation("idle");
    settle(*ch);
    const float before = ch->getBlendDuration();
    ASSERT_GT(before, 0.05f) << "precondition: a real crossfade is configured";

    ch->sitAt(glm::vec3(16.0f, 16.6f, 16.0f), 0.0f, glm::vec3(0.0f), glm::vec3(0.0f),
              glm::vec3(0.0f), 0.2f, 0.0f);
    for (int i = 0; i < 5; ++i) ch->update(kDt);                 // SitDown clip switch happens here
    EXPECT_FLOAT_EQ(ch->getBlendDuration(), before)
        << "the seated branch zeroed blendDuration permanently (AVC ~3407)";

    // Drive through the whole cycle and check again on the far side.
    ch->forceState(AnimatedCharacterState::SitStandUp);
    for (int i = 0; i < 300; ++i) ch->update(kDt);
    EXPECT_FALSE(ch->isSitting());
    EXPECT_FLOAT_EQ(ch->getBlendDuration(), before);
}

// ---------------------------------------------------------------------------
// #2  Per-state sit offsets must be LIVE. sitDownOffset / sittingIdleOffset were stored
//     and never read (every state used sitStandUpOffset), so months of per-state profile
//     tuning had no effect. Two characters differing ONLY in sitDownOffset must sit at
//     different positions during SitDown.
// ---------------------------------------------------------------------------
// A4 (2026-09-30) RETIRED `PerStateSitOffsetsChangeTheSeatedPosition` (A0 #2): the per-state sit
// offsets are gone with the single seated origin + per-frame pelvis constraint
// (docs/AnimationSystemV3Plan.md §4 A4 item 2). sitAt() still accepts them for API compatibility
// and ignores them; the seated pose is pinned by tests/scene/motion/SeatSolveTest.cpp instead.

// ---------------------------------------------------------------------------
// #4  Phase jitter must survive an FSM-driven clip change. Only playAnimation() honoured
//     m_phaseJitter; the FSM and NPC paths wrote animTime = 0, so an army entering Walk
//     via input/behaviour marched in lock-step again.
// ---------------------------------------------------------------------------
TEST(AnimationDeconfound, PhaseJitterSurvivesFsmDrivenClipChange) {
    FloorWorld w;
    auto a = w.character(kHumanoid);
    auto b = w.character(kHumanoid);
    a->setPhaseJitterSeed(0.20f);
    b->setPhaseJitterSeed(0.70f);
    a->playAnimation("idle"); b->playAnimation("idle");
    settle(*a); settle(*b);
    // Forward input (W = NEGATIVE forward, docs/AnimatedCharacter.md) → FSM Idle→Walk.
    for (int i = 0; i < 10; ++i) {
        a->setControlInput(-0.5f, 0.0f, 0.0f); b->setControlInput(-0.5f, 0.0f, 0.0f);
        a->update(kDt); b->update(kDt);
    }
    ASSERT_EQ(a->getCurrentClipName(), "walk");
    ASSERT_EQ(b->getCurrentClipName(), "walk");
    const float dur = a->getAnimationClips()[clipIndex(*a, "walk")].duration;
    float phaseGap = std::fabs(a->getAnimationTime() - b->getAnimationTime()) / dur;
    phaseGap = std::fmod(phaseGap, 1.0f);
    EXPECT_NEAR(phaseGap, 0.5f, 0.05f)
        << "two seeds 0.5 cycles apart ended up " << phaseGap << " cycles apart — the FSM path reset animTime to 0";
}

// ---------------------------------------------------------------------------
// #5  Behaviour-driven (external velocity) NPCs must not skate. The velocity path plays
//     `walk` at rate 1 whatever the behaviour's speed; the clip's Speed line says which
//     body speed its feet were authored for. Playback rate must follow speed/Speed.
// ---------------------------------------------------------------------------
TEST(AnimationDeconfound, ExternalVelocityScalesPlaybackToClipSpeed) {
    FloorWorld w;
    auto ch = w.character(kHumanoid);
    ch->playAnimation("idle");
    settle(*ch);
    const int walk = clipIndex(*ch, "walk");
    ASSERT_GE(walk, 0);
    const float clipSpeed = ch->getAnimationClips()[walk].speed;
    ASSERT_GT(clipSpeed, 1.0f) << "precondition: walk carries an authored Speed (~1.68)";

    const float behaviourSpeed = 0.8f;                  // half the authored speed
    // Enter walk and let the blend settle.
    for (int i = 0; i < 30; ++i) { ch->setMoveVelocity(glm::vec3(0.0f, 0.0f, behaviourSpeed)); ch->update(kDt); }
    ASSERT_EQ(ch->getCurrentClipName(), "walk");
    const float t0 = ch->getAnimationTime();
    const int frames = 30;
    for (int i = 0; i < frames; ++i) { ch->setMoveVelocity(glm::vec3(0.0f, 0.0f, behaviourSpeed)); ch->update(kDt); }
    const float advanced = ch->getAnimationTime() - t0;
    const float expected = frames * kDt * (behaviourSpeed / clipSpeed);
    EXPECT_NEAR(advanced, expected, 0.02f)
        << "walk advanced " << advanced << "s for " << frames << " frames at " << behaviourSpeed
        << " u/s (clip Speed " << clipSpeed << "): the feet are cycling for " << clipSpeed
        << " u/s while the body moves at " << behaviourSpeed << " → skate";
}

// ---------------------------------------------------------------------------
// #6  clip_meta: `releaseFrame` is authored on every cast clip but was parsed by nothing;
//     cast_1h_*/cast_2h_* have NO hitFrameFraction and silently got the 0.4 default.
//     releaseFrame must alias hitFrameFraction when the latter is absent, and an unknown
//     key must not abort the remaining keys on the line.
// ---------------------------------------------------------------------------
TEST(AnimationDeconfound, ClipMetaReleaseFrameAliasesHitFrameAndUnknownKeysAreSkipped) {
    const auto path = writeTinyRig("meta",
        {"# clip_meta: swing unknownKey=whatever releaseFrame=0.700000 interruptible=0"},
        {{"idle", 1.0f, {0, 1, 0}, {0, 1, 0}, ""}, {"swing", 1.0f, {0, 1, 0}, {0, 1, 0}, ""}});
    AnimatedVoxelCharacter ch(nullptr, glm::vec3(0.0f));
    ASSERT_TRUE(ch.loadModel(path));
    const int swing = clipIndex(ch, "swing");
    ASSERT_GE(swing, 0);
    EXPECT_NEAR(ch.getAnimationClips()[swing].hitFrameFraction, 0.7f, 1e-4f)
        << "releaseFrame was dropped (unknown to the parser) — cast clips fire at the 0.4 default";
    EXPECT_FALSE(ch.getAnimationClips()[swing].interruptible)
        << "a key AFTER the unknown one was lost";
}

// ---------------------------------------------------------------------------
// #7  Hot reload must return the file's NEW keyframes. The parse cache is path-keyed and
//     never invalidated, so reloadAnimations() re-read clip_meta but kept stale clips.
// ---------------------------------------------------------------------------
TEST(AnimationDeconfound, ReloadPicksUpAChangedFile) {
    const auto path = writeTinyRig("reload", {},
        {{"idle", 1.0f, {0, 1, 0}, {0, 1, 0}, ""}});
    AnimatedVoxelCharacter ch(nullptr, glm::vec3(0.0f));
    ASSERT_TRUE(ch.loadModel(path));
    ASSERT_NEAR(ch.getAnimationClips()[0].duration, 1.0f, 1e-5f);
    writeTinyRig("reload", {}, {{"idle", 2.5f, {0, 1, 0}, {0, 1, 0}, ""}});   // same path, new content
    ASSERT_TRUE(ch.reloadAnimations(path));
    EXPECT_NEAR(ch.getAnimationClips()[0].duration, 2.5f, 1e-5f)
        << "reloadAnimations served the cached parse — edits on disk never reach a running character";
}

// ---------------------------------------------------------------------------
// #8  standUp() must use the MAPPED Idle clip when transferring the hips delta into
//     worldPosition, not the literal "idle". A rig whose Idle is mapped to a clip with a
//     different t=0 hips position must land at a different spot than the unmapped one.
// ---------------------------------------------------------------------------
TEST(AnimationDeconfound, StandUpTransfersHipsDeltaAgainstTheMappedIdle) {
    const glm::vec3 hips(0, 1, 0);
    const auto path = writeTinyRig("standup", {}, {
        {"idle",         1.0f, hips, hips, ""},
        {"custom_idle",  1.0f, hips + glm::vec3(0, 0, 1.0f), hips + glm::vec3(0, 0, 1.0f), ""},
        {"stand_to_sit", 0.5f, hips, hips, ""},
        {"sitting_idle", 1.0f, hips, hips, ""},
        {"sit_to_stand", 0.5f, hips, hips, ""},
    });
    auto run = [&](bool mapped) -> glm::vec3 {
        AnimatedVoxelCharacter ch(nullptr, glm::vec3(0.0f));
        if (!ch.loadModel(path)) { ADD_FAILURE() << "loadModel failed: " << path; return glm::vec3(0.0f); }
        if (mapped) ch.setAnimationMapping("Idle", "custom_idle");
        ch.playAnimation("idle");
        ch.update(kDt);
        ch.sitAt(glm::vec3(0.0f, 0.6f, 0.0f), 0.0f, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.0f), 0.2f, 0.0f);
        ch.update(kDt);
        ch.forceState(AnimatedCharacterState::SitStandUp);
        for (int i = 0; i < 60; ++i) ch.update(kDt);            // past the 0.5 s clip → Idle
        EXPECT_FALSE(ch.isSitting());
        return ch.getPosition();
    };
    glm::vec3 pUnmapped, pMapped;
    { SCOPED_TRACE("unmapped"); pUnmapped = run(false); }
    { SCOPED_TRACE("mapped");   pMapped   = run(true);  }
    EXPECT_NEAR(pMapped.z - pUnmapped.z, -1.0f, 1e-3f)
        << "standUp() sampled the literal \"idle\" (dz=" << pMapped.z - pUnmapped.z
        << "); the mapped Idle's t=0 hips sit 1.0 further forward";
}

// ---------------------------------------------------------------------------
// #10 sitAt() must reset the run-strafe draw lean; otherwise the drawn body sits rotated
//     against the seat facing the snap uses. Draw yaw must equal the seat facing.
// ---------------------------------------------------------------------------
TEST(AnimationDeconfound, SitAtResetsStrafeLeanSoDrawYawMatchesSeat) {
    FloorWorld w;
    auto ch = w.character(kHumanoid);
    ch->playAnimation("idle");
    settle(*ch);
    // Run-strafe for a while to build up the lean (sprint + strafe input).
    ch->setSprint(true);
    for (int i = 0; i < 60; ++i) { ch->setControlInput(-1.0f, 0.0f, 1.0f); ch->update(kDt); }
    ch->setSprint(false);
    const float facing = 0.9f;
    ch->sitAt(glm::vec3(16.0f, 16.6f, 16.0f), facing, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.0f), 0.2f, 0.0f);
    ch->update(kDt);
    EXPECT_NEAR(ch->getDrawYaw(), facing, 1e-3f)
        << "a leftover strafe lean is drawn on top of the seat facing";
}
