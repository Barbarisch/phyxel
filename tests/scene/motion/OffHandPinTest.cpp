#include <gtest/gtest.h>

#include "scene/AnimatedVoxelCharacter.h"
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

// ============================================================================
// A3 — two-handed carry, L3: the off-hand is IK-pinned to the held item's second grip with the
// plan's arm chain (docs/AnimationSystemV3Plan.md §4 A3 item 3). The grip-distance check from
// the gate: hand-to-target under one microcube (0.111 u) when reachable. RED 2026-09-30: no
// arm chain, no pin — the hand stays wherever the layer put it.
// ============================================================================

namespace {
constexpr const char* kHumanoid = "resources/animated_characters/humanoid.anim";
constexpr float kDt = 1.0f / 60.0f;

// A floor so the capsule grounds and the visual body spring settles — the pin works in the
// render frame (visual Y), which a physics-less character never updates.
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
    std::unique_ptr<AnimatedVoxelCharacter> idleHumanoid() {
        auto ch = std::make_unique<AnimatedVoxelCharacter>(physics.get(), glm::vec3(16.0f, 16.05f, 16.0f));
        EXPECT_TRUE(ch->loadModel(kHumanoid));
        ch->setChunkManager(&cm);
        ch->setPhaseJitterSeed(0.0f);
        ch->forceState(AnimatedCharacterState::Preview);
        ch->playAnimation("idle");
        for (int i = 0; i < 60; ++i) ch->update(kDt);   // ground + settle the visual spring
        return ch;
    }
};

glm::vec3 handModel(const AnimatedVoxelCharacter& ch, int boneId) {
    return glm::vec3(ch.getSkeleton().bones[boneId].globalTransform[3]);   // model space: fine for deltas
}
} // namespace

TEST(OffHandPin, HumanoidPlanResolvesTheLeftArmAsTheOffHandChain) {
    FloorWorld w;
    auto ch = w.idleHumanoid();
    const auto chain = ch->offHandChain();
    ASSERT_GE(chain[0], 0); ASSERT_GE(chain[1], 0); ASSERT_GE(chain[2], 0);
    const auto& sk = ch->getSkeleton();
    EXPECT_EQ(sk.bones[chain[0]].name, "mixamorig:LeftArm");
    EXPECT_EQ(sk.bones[chain[1]].name, "mixamorig:LeftForeArm");
    EXPECT_EQ(sk.bones[chain[2]].name, "mixamorig:LeftHand");
}

TEST(OffHandPin, ReachableTargetIsHitWithinOneMicrocube) {
    FloorWorld w;
    auto ch = w.idleHumanoid();
    ASSERT_GE(ch->offHandChain()[2], 0);
    // ~35 cm in front of the chest, a little to the left: inside a Mixamo arm's reach (~0.55 u)
    const glm::vec3 target = ch->getPosition() + glm::vec3(-0.15f, 1.15f, 0.35f);
    for (int i = 0; i < 30; ++i) { ch->setOffHandTarget(target); ch->update(kDt); }   // ease-in complete
    std::printf("[OffHandPin] reachable: error %.4f u (active %d)\n", ch->offHandError(), (int)ch->offHandPinActive());
    EXPECT_TRUE(ch->offHandPinActive());
    EXPECT_LT(ch->offHandError(), 0.111f) << "grip-distance check: one microcube";
}

TEST(OffHandPin, UnreachableTargetReportsItsErrorHonestly) {
    FloorWorld w;
    auto ch = w.idleHumanoid();
    const glm::vec3 target = ch->getPosition() + glm::vec3(0.0f, 1.2f, 2.5f);   // far beyond arm reach
    for (int i = 0; i < 30; ++i) { ch->setOffHandTarget(target); ch->update(kDt); }
    std::printf("[OffHandPin] unreachable: error %.3f u\n", ch->offHandError());
    EXPECT_GT(ch->offHandError(), 1.0f) << "the solver clamps at full reach and the readback says so";
}

TEST(OffHandPin, ClearingTheTargetReturnsTheHandToTheBasePose_Control) {
    FloorWorld w;
    auto a = w.idleHumanoid();
    auto b = w.idleHumanoid();
    const int hand = a->offHandChain()[2];
    const glm::vec3 target = b->getPosition() + glm::vec3(-0.15f, 1.15f, 0.35f);
    for (int i = 0; i < 30; ++i) { b->setOffHandTarget(target); b->update(kDt); a->update(kDt); }
    const float pinned = glm::length(handModel(*a, hand) - handModel(*b, hand));
    EXPECT_GT(pinned, 0.05f) << "the pinned hand differs from the base pose";
    b->clearOffHandTarget();
    for (int i = 0; i < 30; ++i) { b->update(kDt); a->update(kDt); }
    EXPECT_FALSE(b->offHandPinActive());
    const float released = glm::length(handModel(*a, hand) - handModel(*b, hand));
    EXPECT_LT(released, 1e-3f) << "released, both idles are at the same phase and pose again";
}
