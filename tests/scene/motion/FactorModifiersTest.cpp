#include <gtest/gtest.h>

#include "scene/AnimatedVoxelCharacter.h"
#include "scene/motion/MotionOracle.h"
#include "core/ChunkManager.h"
#include "core/Chunk.h"
#include "physics/PhysicsWorld.h"
#include "physics/VoxelOccupancyGrid.h"
#include "physics/VoxelDynamicsWorld.h"

#include <glm/gtx/quaternion.hpp>

#include <cstdio>
#include <memory>
#include <vector>

using namespace Phyxel;
using Phyxel::Scene::AnimatedVoxelCharacter;
using Phyxel::Scene::AnimatedCharacterState;
using namespace Phyxel::Scene::Motion;

// ============================================================================
// A3 — procedural modifiers from the factor table (docs/AnimationSystemV3Plan.md §3b):
// forward lean ∝ load/condition over the PLAN's spine chain, and cadence ∝ load/condition
// scaling playback and ground travel together. RED 2026-09-30: no spine chain on the plan,
// lean ignores factors, cadence is always 1.
// ============================================================================

namespace {

constexpr const char* kHumanoid = "resources/animated_characters/humanoid.anim";
constexpr float kDt = 1.0f / 60.0f;
constexpr float kWalkClipSpeed = 1.6786038f;

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
    std::unique_ptr<AnimatedVoxelCharacter> character(glm::vec3 pos = {16, 16.05f, 4}) {
        auto ch = std::make_unique<AnimatedVoxelCharacter>(physics.get(), pos);
        EXPECT_TRUE(ch->loadModel(kHumanoid));
        ch->setChunkManager(&cm);
        return ch;
    }
};

struct WalkResult { float capsuleSpeed = 0.0f, skate = -1.0f; int stance = 0; };

WalkResult driveInputWalk(AnimatedVoxelCharacter& ch, int warmup = 120, int record = 120) {
    for (int i = 0; i < warmup; ++i) { ch.setControlInput(-0.5f, 0.0f, 0.0f); ch.update(kDt); }
    ch.startOracleRecording(static_cast<std::size_t>(record));
    for (int i = 0; i < record; ++i) { ch.setControlInput(-0.5f, 0.0f, 0.0f); ch.update(kDt); }
    auto frames = ch.takeOracleFrames();
    OracleOptions opt;
    opt.footJoints = ch.oracleFootJoints(); opt.kneeChains = ch.oracleLegChains();
    opt.boxAdjacency = ch.oracleBoxAdjacency();
    opt.ground = [&ch](float x, float z) { return ch.groundYUnder(x, z); };
    std::vector<std::pair<std::size_t, std::size_t>> edges;
    const auto& bones = ch.getSkeleton().bones;
    for (std::size_t i = 0; i < bones.size(); ++i) if (bones[i].parentId >= 0) edges.emplace_back((std::size_t)bones[i].parentId, i);
    const auto m = evaluateMotion(frames, kDt, edges, opt);
    WalkResult r;
    for (const auto& f : frames) r.capsuleSpeed += glm::length(glm::vec2(f.capsuleVelocity.x, f.capsuleVelocity.z));
    r.capsuleSpeed /= static_cast<float>(frames.size());
    r.skate = m.stanceBodySpeed / std::max(r.capsuleSpeed, 0.1f);
    r.stance = m.stanceSamples;
    return r;
}

float pitchDeg(const glm::quat& q) {
    // signed rotation about the local X axis, from the quaternion's twist about X
    const glm::quat n = glm::normalize(q);
    return glm::degrees(2.0f * std::atan2(n.x, n.w));
}

} // namespace

TEST(FactorModifiers, HumanoidPlanSpineChainResolvesToTheThreeSpineBones) {
    FloorWorld w;
    auto ch = w.character();
    const auto& chain = ch->spineChain();
    ASSERT_EQ(chain.size(), 3u);
    const auto& sk = ch->getSkeleton();
    EXPECT_EQ(sk.bones[chain[0]].name, "mixamorig:Spine");
    EXPECT_EQ(sk.bones[chain[1]].name, "mixamorig:Spine1");
    EXPECT_EQ(sk.bones[chain[2]].name, "mixamorig:Spine2");
}

TEST(FactorModifiers, BulkyLoadLeansTheSpineChainForwardTenDegreesAndLeavesLegsAlone) {
    FloorWorld w;
    auto a = w.character(), b = w.character();
    a->setPhaseJitterSeed(0.0f); b->setPhaseJitterSeed(0.0f);   // same idle phase: the lean is the only variable
    a->forceState(AnimatedCharacterState::Preview); b->forceState(AnimatedCharacterState::Preview);
    a->playAnimation("idle"); b->playAnimation("idle");
    ClipMeta::Factors f; f.state = "idle"; f.load = "bulky";
    b->setCompositionFactors(f);
    EXPECT_FLOAT_EQ(a->factorLeanDeg(), 0.0f);
    EXPECT_FLOAT_EQ(b->factorLeanDeg(), 10.0f);
    for (int i = 0; i < 30; ++i) { a->update(kDt); b->update(kDt); }
    float totalDelta = 0.0f;
    for (int id : a->spineChain()) {
        const glm::quat qa = a->getSkeleton().bones[id].currentRotation;
        const glm::quat qb = b->getSkeleton().bones[id].currentRotation;
        totalDelta += glm::degrees(glm::angle(glm::normalize(glm::inverse(qa) * qb)));
    }
    EXPECT_NEAR(totalDelta, 10.0f, 0.5f) << "sum of per-bone lean deltas over the chain";
    const auto& la = a->getSkeleton().bones[a->maskBones("legs").front()].currentRotation;
    const auto& lb = b->getSkeleton().bones[b->maskBones("legs").front()].currentRotation;
    EXPECT_NEAR(glm::degrees(glm::angle(glm::normalize(glm::inverse(la) * lb))), 0.0f, 1e-3f);
}

TEST(FactorModifiers, BulkyLoadSlowsTheWalkCadenceAndTheFeetStayPlanted) {
    FloorWorld w;
    auto ch = w.character();
    ClipMeta::Factors f; f.load = "bulky";
    ch->setCompositionFactors(f);
    EXPECT_NEAR(ch->cadenceFactor(), 0.85f, 1e-4f);
    const auto r = driveInputWalk(*ch);
    std::printf("[FactorModifiers] bulky walk: capsule %.3f u/s (clip %.3f x %.2f) skate %.4f stance %d\n",
                r.capsuleSpeed, kWalkClipSpeed, ch->cadenceFactor(), r.skate, r.stance);
    ASSERT_GE(r.stance, 6);
    EXPECT_NEAR(r.capsuleSpeed, kWalkClipSpeed * 0.85f, 0.03f) << "ground travel follows the cadence";
    EXPECT_LT(r.skate, 0.10f) << "playback slowed with the travel — no skate";
}

TEST(FactorModifiers, FreshUnloadedCharacterIsUnchanged_Control) {
    FloorWorld w;
    auto ch = w.character();
    EXPECT_FLOAT_EQ(ch->cadenceFactor(), 1.0f);
    EXPECT_FLOAT_EQ(ch->factorLeanDeg(), 0.0f);
    const auto r = driveInputWalk(*ch);
    ASSERT_GE(r.stance, 6);
    EXPECT_NEAR(r.capsuleSpeed, kWalkClipSpeed, 0.03f);
    EXPECT_LT(r.skate, 0.10f);
}
