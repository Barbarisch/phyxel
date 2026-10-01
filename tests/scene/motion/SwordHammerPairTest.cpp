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
#include <string>
#include <vector>

using namespace Phyxel;
using Phyxel::Scene::AnimatedVoxelCharacter;
using Phyxel::Scene::AnimatedCharacterState;
using namespace Phyxel::Scene::Motion;

// ============================================================================
// A3's first deliverable pair (docs/AnimationSystemV3Plan.md §4 A3, owner's example): walk
// while holding a 1H sword vs a 2H hammer — SAME base walk, DIFFERENT carry layer, identical
// leg trajectories, different upper body. The layers `carry_1h` / `carry_2h_heavy` are additive
// upper-body clips authored by tools/anim_pipeline/make_carry_layer.py from the sword-ready
// and two-handed-ready poses. L3: the stance-feet metric on both, legs compared joint by joint.
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
    std::unique_ptr<AnimatedVoxelCharacter> character(glm::vec3 pos = {16, 16.05f, 4}) {
        auto ch = std::make_unique<AnimatedVoxelCharacter>(physics.get(), pos);
        EXPECT_TRUE(ch->loadModel(kHumanoid));
        ch->setChunkManager(&cm);
        ch->setPhaseJitterSeed(0.0f);          // both walkers start at the same phase
        return ch;
    }
};

struct Walked {
    std::vector<OracleFrame> frames;
    float capsuleSpeed = 0.0f, skate = -1.0f;
    int stance = 0;
    std::vector<std::string> layers;
};

Walked walkWith(FloorWorld& w, const char* grip, int warmup = 120, int record = 120) {
    auto ch = w.character();
    ClipMeta::Factors f; f.grip = grip;
    ch->setCompositionFactors(f);
    for (int i = 0; i < warmup; ++i) { ch->setControlInput(-0.5f, 0.0f, 0.0f); ch->update(kDt); }
    ch->startOracleRecording(static_cast<std::size_t>(record));
    for (int i = 0; i < record; ++i) { ch->setControlInput(-0.5f, 0.0f, 0.0f); ch->update(kDt); }
    Walked out;
    out.frames = ch->takeOracleFrames();
    OracleOptions opt;
    opt.footJoints = ch->oracleFootJoints(); opt.kneeChains = ch->oracleLegChains();
    opt.boxAdjacency = ch->oracleBoxAdjacency();
    opt.ground = [c = ch.get()](float x, float z) { return c->groundYUnder(x, z); };
    std::vector<std::pair<std::size_t, std::size_t>> edges;
    const auto& bones = ch->getSkeleton().bones;
    for (std::size_t i = 0; i < bones.size(); ++i) if (bones[i].parentId >= 0) edges.emplace_back((std::size_t)bones[i].parentId, i);
    const auto m = evaluateMotion(out.frames, kDt, edges, opt);
    for (const auto& fr : out.frames) out.capsuleSpeed += glm::length(glm::vec2(fr.capsuleVelocity.x, fr.capsuleVelocity.z));
    out.capsuleSpeed /= static_cast<float>(out.frames.size());
    out.skate = m.stanceBodySpeed / std::max(out.capsuleSpeed, 0.1f);
    out.stance = m.stanceSamples;
    for (const auto& L : ch->activeLayers()) out.layers.push_back(ch->getAnimationClips()[L.clipIndex].name);
    return out;
}

float maxLocalRotDeltaDeg(const Walked& a, const Walked& b, const std::vector<int>& bones) {
    float worst = 0.0f;
    const size_t n = std::min(a.frames.size(), b.frames.size());
    for (size_t f = 0; f < n; ++f)
        for (int id : bones) {
            const glm::quat qa = a.frames[f].localRotations[id], qb = b.frames[f].localRotations[id];
            worst = std::max(worst, glm::degrees(glm::angle(glm::normalize(glm::inverse(qa) * qb))));
        }
    return worst;
}

float meanLocalRotDeltaDeg(const Walked& a, const Walked& b, const std::vector<int>& bones) {
    double sum = 0.0; size_t count = 0;
    const size_t n = std::min(a.frames.size(), b.frames.size());
    for (size_t f = 0; f < n; ++f)
        for (int id : bones) {
            const glm::quat qa = a.frames[f].localRotations[id], qb = b.frames[f].localRotations[id];
            sum += glm::degrees(glm::angle(glm::normalize(glm::inverse(qa) * qb))); ++count;
        }
    return count ? static_cast<float>(sum / count) : 0.0f;
}

} // namespace

TEST(SwordHammerPair, SameLegsDifferentUpperBodyAndNobodySkates) {
    FloorWorld w;
    auto probe = w.character();
    const std::vector<int> legs = probe->maskBones("legs");
    const std::vector<int> arms = probe->maskBones("arms");
    ASSERT_FALSE(legs.empty()); ASSERT_FALSE(arms.empty());
    probe.reset();

    const Walked sword  = walkWith(w, "1h");
    const Walked hammer = walkWith(w, "2h_heavy");
    const Walked bare   = walkWith(w, "empty");

    ASSERT_EQ(sword.layers,  (std::vector<std::string>{"carry_1h"}));
    ASSERT_EQ(hammer.layers, (std::vector<std::string>{"carry_2h_heavy"}));
    EXPECT_TRUE(bare.layers.empty());

    const float legDelta = maxLocalRotDeltaDeg(sword, hammer, legs);
    const float armDelta = meanLocalRotDeltaDeg(sword, hammer, arms);
    const float bareVsHammerArms = meanLocalRotDeltaDeg(bare, hammer, arms);
    std::printf("[SwordHammerPair] legs max delta %.4f deg | arms mean delta %.1f deg (bare vs hammer %.1f) | "
                "skate sword %.3f hammer %.3f bare %.3f | speed %.3f/%.3f\n",
                legDelta, armDelta, bareVsHammerArms, sword.skate, hammer.skate, bare.skate,
                sword.capsuleSpeed, hammer.capsuleSpeed);

    EXPECT_LT(legDelta, 0.01f) << "the carry layers are upper-body only: leg trajectories must be identical";
    EXPECT_GT(armDelta, 15.0f) << "sword and hammer carries must differ in the arms";
    EXPECT_GT(bareVsHammerArms, 15.0f) << "the hammer carry must differ from the bare walk";
    ASSERT_GE(sword.stance, 6); ASSERT_GE(hammer.stance, 6);
    EXPECT_LT(sword.skate, 0.10f);
    EXPECT_LT(hammer.skate, 0.10f);
    EXPECT_NEAR(sword.capsuleSpeed, hammer.capsuleSpeed, 1e-3f) << "grip alone does not change speed (load does)";
}
