#include <gtest/gtest.h>

#include "scene/AnimatedVoxelCharacter.h"
#include "scene/motion/MotionOracle.h"
#include "core/ChunkManager.h"
#include "core/Chunk.h"
#include "physics/PhysicsWorld.h"
#include "physics/VoxelOccupancyGrid.h"
#include "physics/VoxelDynamicsWorld.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Phyxel;
using Phyxel::Scene::AnimatedVoxelCharacter;
using Phyxel::Scene::AnimatedCharacterState;
using namespace Phyxel::Scene::Motion;

// ============================================================================
// A2 — the behaviour-driven (external-velocity) path must plant its feet as well as the
// input-driven path does, at ANY speed the playback-rate scale (A0 #5) can produce.
//
// Live 2026-09-29 (Release, CharacterTestbed, 180–270 fps, the A1 oracle route):
//   input-driven humanoid @ 1.68 u/s (rate 1.00)  world skate  2 %   PASS
//   NPC patrol           @ 1.68 u/s (rate 1.00)  world skate 10 %   PASS
//   NPC patrol           @ 1.50 u/s (rate 0.89)  world skate 31 %   WARN
//   NPC patrol           @ 2.00 u/s (rate 1.19)  world skate 80 %   FAIL
// while the clip progress advanced at the right rate and the state stayed Walk. This test is
// the deterministic twin of those windows: same rig, same oracle options as the live route
// (`animation_validate`), a flat floor, 60 Hz. RED first on the speeds that failed live.
// ============================================================================

namespace {

constexpr const char* kHumanoid = "resources/animated_characters/humanoid.anim";
constexpr float kDt = 1.0f / 60.0f;
constexpr float kWalkClipSpeed = 1.6786038f;   // humanoid.anim `walk` Speed line

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

    std::unique_ptr<AnimatedVoxelCharacter> character(glm::vec3 pos) {
        auto ch = std::make_unique<AnimatedVoxelCharacter>(physics.get(), pos);
        EXPECT_TRUE(ch->loadModel(kHumanoid));
        ch->setChunkManager(&cm);
        return ch;
    }
};

struct SkateResult {
    float skate = -1.0f;        // stance body speed / mean capsule speed (live-route formula)
    float capsuleSpeed = 0.0f;
    float plantedPeak = 0.0f;
    int   stanceSamples = 0;
    std::string clip, state;
};

// Same evaluation as Application.cpp `animation_validate`, minus the JSON.
SkateResult evaluateLive(AnimatedVoxelCharacter& ch, std::vector<OracleFrame> frames, float dt) {
    SkateResult r;
    OracleOptions opt;
    opt.footJoints   = ch.oracleFootJoints();
    opt.kneeChains   = ch.oracleLegChains();
    opt.boxAdjacency = ch.oracleBoxAdjacency();
    opt.ground = [&ch](float x, float z) { return ch.groundYUnder(x, z); };
    std::vector<std::pair<std::size_t, std::size_t>> chainEdges;
    const auto& bones = ch.getSkeleton().bones;
    for (std::size_t i = 0; i < bones.size(); ++i)
        if (bones[i].parentId >= 0) chainEdges.emplace_back(static_cast<std::size_t>(bones[i].parentId), i);
    const auto m = evaluateMotion(frames, dt, chainEdges, opt);
    float meanCapsule = 0.0f;
    for (const auto& f : frames) meanCapsule += glm::length(glm::vec2(f.capsuleVelocity.x, f.capsuleVelocity.z));
    meanCapsule /= static_cast<float>(frames.size());
    r.capsuleSpeed  = meanCapsule;
    r.skate         = m.stanceBodySpeed / std::max(meanCapsule, 0.1f);
    r.plantedPeak   = m.maxPlantedJointSpeed;
    r.stanceSamples = m.stanceSamples;
    r.clip  = ch.getCurrentClipName();
    r.state = ch.stateToString(ch.getAnimationState());
    // Numbers on PASS too — the plan's §4b ledger quotes them.
    ::testing::Test::RecordProperty("world_skate", std::to_string(r.skate));
    ::testing::Test::RecordProperty("capsule_speed", std::to_string(r.capsuleSpeed));
    std::printf("[NpcPathSkate] capsule %.3f u/s  world_skate %.4f  planted_peak %.2f  stance %d  %s/%s\n",
                r.capsuleSpeed, r.skate, r.plantedPeak, r.stanceSamples, r.state.c_str(), r.clip.c_str());
    return r;
}

// Drive along +Z from the west side of the floor so 4 s of travel stays on the slab.
SkateResult driveExternal(float speed, int warmup = 120, int record = 120) {
    FloorWorld world;
    auto ch = world.character({16.0f, 16.05f, 4.0f});
    for (int i = 0; i < warmup; ++i) { ch->setMoveVelocity(glm::vec3(0.0f, 0.0f, speed)); ch->update(kDt); }
    ch->startOracleRecording(static_cast<std::size_t>(record));
    for (int i = 0; i < record; ++i) { ch->setMoveVelocity(glm::vec3(0.0f, 0.0f, speed)); ch->update(kDt); }
    return evaluateLive(*ch, ch->takeOracleFrames(), kDt);
}

SkateResult driveInput(int warmup = 120, int record = 120) {
    FloorWorld world;
    auto ch = world.character({16.0f, 16.05f, 4.0f});
    // W is NEGATIVE forward; -0.5 walks (|fwd| <= 0.6). At yaw 0 the input path integrates
    // -1 * (-sin, 0, -cos) = +Z, the same heading as the external drive above.
    for (int i = 0; i < warmup; ++i) { ch->setControlInput(-0.5f, 0.0f, 0.0f); ch->update(kDt); }
    ch->startOracleRecording(static_cast<std::size_t>(record));
    for (int i = 0; i < record; ++i) { ch->setControlInput(-0.5f, 0.0f, 0.0f); ch->update(kDt); }
    return evaluateLive(*ch, ch->takeOracleFrames(), kDt);
}

} // namespace

TEST(NpcPathSkate, InputDrivenWalkPlantsItsFeet_Control) {
    const auto r = driveInput();
    ASSERT_GE(r.stanceSamples, 6) << "no stance samples — the control did not walk";
    EXPECT_EQ(r.state, "Walk");
    EXPECT_LT(r.skate, 0.10f) << "capsule " << r.capsuleSpeed << " clip " << r.clip << " planted peak " << r.plantedPeak;
}

TEST(NpcPathSkate, ExternalVelocityAtClipSpeedPlantsItsFeet) {
    const auto r = driveExternal(kWalkClipSpeed);
    ASSERT_GE(r.stanceSamples, 6);
    EXPECT_EQ(r.state, "Walk");
    EXPECT_NEAR(r.capsuleSpeed, kWalkClipSpeed, 0.02f);
    EXPECT_LT(r.skate, 0.10f) << "clip " << r.clip << " planted peak " << r.plantedPeak;
}

TEST(NpcPathSkate, ExternalVelocitySlowerThanClipPlantsItsFeet) {
    const auto r = driveExternal(1.5f);        // rate 0.89 — live read 31 %
    ASSERT_GE(r.stanceSamples, 6);
    EXPECT_EQ(r.state, "Walk");
    EXPECT_LT(r.skate, 0.10f) << "capsule " << r.capsuleSpeed << " clip " << r.clip << " planted peak " << r.plantedPeak;
}

TEST(NpcPathSkate, ExternalVelocityFasterThanClipPlantsItsFeet) {
    const auto r = driveExternal(2.0f);        // rate 1.19 — live read 80 %
    ASSERT_GE(r.stanceSamples, 6);
    EXPECT_EQ(r.state, "Walk");
    EXPECT_LT(r.skate, 0.10f) << "capsule " << r.capsuleSpeed << " clip " << r.clip << " planted peak " << r.plantedPeak;
}
