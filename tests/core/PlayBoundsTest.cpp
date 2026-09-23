// PlayBoundsTest.cpp -- the edge of the world is a wall (G-169).
//
// User, 2026-09-23: "characters can walk off the edge of the map". Ravenmere's scenes are baked
// 3x3-chunk worlds; past the last chunk there was no ground and nothing blocked the step, so the
// player fell for ever (measured on the shipped exe: y = -577 twelve seconds after holding W at
// the farm's south edge - docs/evidence/ravenmere/rv_edge_l4_red.json).
//
// The rule: every kinematic character is clamped to the play bounds each frame. The bounds are
// authored (`playBounds` in the scene definition) or, failing that, derived from the rectangle of
// chunks the world has; a streaming world has none (its extent grows ahead of the player).
// These tests pin the value type, the derivation, the precedence and the loader's refusal of a
// malformed box. Whether the CHARACTER honours it is the L4's job (tools/rv_edge_l4.py) - the
// clamp call sits in resolveKinematicMovement, which needs a physics world.

#include <gtest/gtest.h>

#include "core/ChunkManager.h"
#include "core/GameDefinitionLoader.h"
#include "core/PlayBounds.h"

#include <vulkan/vulkan.h>

using Phyxel::Core::GameDefinitionLoader;
using Phyxel::Core::GameSubsystems;
using Phyxel::Core::PlayBounds;
using json = nlohmann::json;

// ---------------------------------------------------------------- the value type

TEST(PlayBounds, ABodyInsideIsLeftAlone) {
    const PlayBounds b = PlayBounds::make(-32.0f, -32.0f, 64.0f, 64.0f);
    const glm::vec3 p(6.0f, 17.0f, -29.7f);
    EXPECT_EQ(b.clamp(p, 0.26f), p);
    EXPECT_TRUE(b.contains(p));
}

TEST(PlayBounds, ABodyPastTheEdgeIsHeldAtTheEdgeMinusItsHalfWidth) {
    // The farm's south edge, where the red run went over: z = -32.
    const PlayBounds b = PlayBounds::make(-32.0f, -32.0f, 64.0f, 64.0f);
    const glm::vec3 over(6.0f, 17.0f, -36.2f);
    const glm::vec3 held = b.clamp(over, 0.26f);
    EXPECT_FLOAT_EQ(held.z, -32.0f + 0.26f);
    EXPECT_FLOAT_EQ(held.x, 6.0f) << "only the offending axis moves";
    EXPECT_FLOAT_EQ(held.y, 17.0f) << "y is never touched: the wall is horizontal";
}

TEST(PlayBounds, ACornerClampsBothAxesAndTheFarSideCountsTheHalfWidthToo) {
    const PlayBounds b = PlayBounds::make(0.0f, 0.0f, 10.0f, 10.0f);
    const glm::vec3 held = b.clamp(glm::vec3(12.0f, 1.0f, 11.0f), 0.5f);
    EXPECT_FLOAT_EQ(held.x, 9.5f);
    EXPECT_FLOAT_EQ(held.z, 9.5f);
}

TEST(PlayBounds, InvalidBoundsClampNothing) {
    const PlayBounds none;
    const glm::vec3 p(1e6f, 0.0f, -1e6f);
    EXPECT_EQ(none.clamp(p, 0.3f), p);
    EXPECT_FALSE(none.contains(p));
    EXPECT_TRUE(none.toJson().is_null());
}

TEST(PlayBounds, ABodyWiderThanTheStageSitsAtItsCentre) {
    const PlayBounds b = PlayBounds::make(0.0f, 0.0f, 1.0f, 1.0f);
    const glm::vec3 held = b.clamp(glm::vec3(5.0f, 0.0f, -5.0f), 2.0f);
    EXPECT_FLOAT_EQ(held.x, 0.5f);
    EXPECT_FLOAT_EQ(held.z, 0.5f);
}

TEST(PlayBounds, JsonRoundTripsAndMalformedBoxesAreRefusedWithAReason) {
    PlayBounds b;
    ASSERT_TRUE(PlayBounds::fromJson(json::parse(R"({"min":{"x":-32,"z":-32},"max":{"x":64,"z":64}})"), b));
    EXPECT_TRUE(b.valid);
    EXPECT_FLOAT_EQ(b.minX, -32.0f); EXPECT_FLOAT_EQ(b.maxZ, 64.0f);
    PlayBounds again;
    ASSERT_TRUE(PlayBounds::fromJson(b.toJson(), again));
    EXPECT_FLOAT_EQ(again.maxX, b.maxX);

    EXPECT_FALSE(PlayBounds::validate(json::parse(R"({"min":{"x":10,"z":0},"max":{"x":0,"z":10}})")).first) << "inverted x";
    EXPECT_FALSE(PlayBounds::validate(json::parse(R"({"min":{"x":0,"z":0},"max":{"x":10,"z":0}})")).first) << "zero-area z";
    EXPECT_FALSE(PlayBounds::validate(json::parse(R"({"min":{"x":0},"max":{"x":10,"z":10}})")).first) << "missing min.z";
    EXPECT_FALSE(PlayBounds::validate(json::parse(R"([0,0,10,10])")).first) << "not an object";
    EXPECT_FALSE(PlayBounds::validate(json::parse(R"({"min":{"x":"0","z":0},"max":{"x":10,"z":10}})")).first) << "string coordinate";
    EXPECT_NE(PlayBounds::validate(json::parse(R"({"min":{"x":10,"z":0},"max":{"x":0,"z":10}})")).second.find("exceed"), std::string::npos);
}

// ---------------------------------------------------------------- derived from the chunks

namespace {
struct HeadlessWorld : ::testing::Test {
    Phyxel::ChunkManager cm;   // no device: createVulkanBuffer no-ops
    void SetUp() override { cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE); }
    void chunkAt(int cx, int cz) { cm.addCubeWithMaterial(glm::ivec3(cx * 32 + 3, 5, cz * 32 + 3), "Stone"); }
};
}  // namespace

TEST_F(HeadlessWorld, NoChunksMeansNoDerivedBounds) {
    EXPECT_FALSE(cm.derivedWorldExtentXZ().valid);
    EXPECT_FALSE(cm.effectivePlayBounds().valid);
    EXPECT_STREQ(cm.playBoundsSource(), "none (no chunks)");
}

TEST_F(HeadlessWorld, TheDerivedExtentIsTheRectangleOfTheChunksTheWorldHas) {
    // Ravenmere farm shape: chunks x,z in -1..1 -> voxels -32..64 (half-open).
    for (int x = -1; x <= 1; ++x)
        for (int z = -1; z <= 1; ++z) chunkAt(x, z);
    const PlayBounds b = cm.derivedWorldExtentXZ();
    ASSERT_TRUE(b.valid);
    EXPECT_FLOAT_EQ(b.minX, -32.0f); EXPECT_FLOAT_EQ(b.maxX, 64.0f);
    EXPECT_FLOAT_EQ(b.minZ, -32.0f); EXPECT_FLOAT_EQ(b.maxZ, 64.0f);
    EXPECT_STREQ(cm.playBoundsSource(), "derived (chunk extent)");
    // the farm's authored entry is inside; the red run's resting place is not
    EXPECT_TRUE(b.contains(glm::vec3(6.0f, 17.0f, 16.0f)));
    EXPECT_FALSE(b.contains(glm::vec3(5.98f, -577.0f, -36.2f)));
}

TEST_F(HeadlessWorld, TheDerivedExtentFollowsNewChunksAndIsOrderIndependent) {
    chunkAt(0, 0);
    EXPECT_FLOAT_EQ(cm.derivedWorldExtentXZ().maxX, 32.0f);
    chunkAt(3, -2);                                   // a far chunk: the extent grows to include it
    const PlayBounds b = cm.derivedWorldExtentXZ();
    EXPECT_FLOAT_EQ(b.minX, 0.0f);   EXPECT_FLOAT_EQ(b.maxX, 128.0f);
    EXPECT_FLOAT_EQ(b.minZ, -64.0f); EXPECT_FLOAT_EQ(b.maxZ, 32.0f);
    // the same two chunks in the other order give the same rectangle (a min/max over a set)
    Phyxel::ChunkManager other; other.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
    other.addCubeWithMaterial(glm::ivec3(3 * 32 + 3, 5, -2 * 32 + 3), "Stone");
    other.addCubeWithMaterial(glm::ivec3(3, 5, 3), "Stone");
    const PlayBounds c = other.derivedWorldExtentXZ();
    EXPECT_FLOAT_EQ(c.minX, b.minX); EXPECT_FLOAT_EQ(c.maxX, b.maxX);
    EXPECT_FLOAT_EQ(c.minZ, b.minZ); EXPECT_FLOAT_EQ(c.maxZ, b.maxZ);
}

TEST_F(HeadlessWorld, AuthoredBoundsBeatTheDerivedExtent) {
    for (int x = -1; x <= 1; ++x)
        for (int z = -1; z <= 1; ++z) chunkAt(x, z);
    cm.setAuthoredPlayBounds(PlayBounds::make(0.0f, 0.0f, 20.0f, 20.0f));
    const PlayBounds e = cm.effectivePlayBounds();
    EXPECT_FLOAT_EQ(e.maxX, 20.0f) << "a designer's smaller stage wins";
    EXPECT_STREQ(cm.playBoundsSource(), "authored");
    cm.clearAuthoredPlayBounds();
    EXPECT_FLOAT_EQ(cm.effectivePlayBounds().maxX, 64.0f) << "cleared: back to the chunk extent";
}

TEST_F(HeadlessWorld, AStreamingWorldHasNoDerivedBounds) {
    // Its extent grows ahead of the player; a wall at the current rectangle would stop the
    // player at the streaming frontier. The residency hold covers unstreamed ground there.
    chunkAt(0, 0);
    cm.configureStreamingGeneration(true, Phyxel::WorldGenerator::GenerationType::Flat, 1);
    EXPECT_FALSE(cm.effectivePlayBounds().valid);
    EXPECT_STREQ(cm.playBoundsSource(), "none (streaming world)");
    cm.setAuthoredPlayBounds(PlayBounds::make(0.0f, 0.0f, 20.0f, 20.0f));
    EXPECT_TRUE(cm.effectivePlayBounds().valid) << "an authored stage still applies on a streaming world";
}

// ---------------------------------------------------------------- the loader

TEST_F(HeadlessWorld, TheLoaderAppliesAuthoredPlayBoundsAndClearsThemOnTheNextLoad) {
    GameSubsystems subs; subs.chunkManager = &cm;
    const json withBounds = json::parse(R"({"playBounds":{"min":{"x":-10,"z":-10},"max":{"x":10,"z":10}}})");
    const auto r = GameDefinitionLoader::load(withBounds, subs);
    ASSERT_TRUE(r.success) << r.error;
    EXPECT_TRUE(r.playBoundsAuthored);
    EXPECT_TRUE(r.toJson().value("play_bounds_authored", false));
    ASSERT_TRUE(cm.authoredPlayBounds().valid);
    EXPECT_FLOAT_EQ(cm.authoredPlayBounds().maxZ, 10.0f);

    const auto r2 = GameDefinitionLoader::load(json::object(), subs);   // the next scene: no key
    ASSERT_TRUE(r2.success) << r2.error;
    EXPECT_FALSE(r2.playBoundsAuthored);
    EXPECT_FALSE(cm.authoredPlayBounds().valid) << "the previous scene's stage must not leak into this one";
}

TEST_F(HeadlessWorld, TheLoaderRefusesAMalformedPlayBoundsBoxLoudly) {
    GameSubsystems subs; subs.chunkManager = &cm;
    const json bad = json::parse(R"({"playBounds":{"min":{"x":10,"z":0},"max":{"x":0,"z":10}}})");
    EXPECT_FALSE(GameDefinitionLoader::validate(bad).first);
    const auto r = GameDefinitionLoader::load(bad, subs);
    EXPECT_FALSE(r.success);
    EXPECT_NE(r.error.find("playBounds"), std::string::npos) << r.error;
    EXPECT_FALSE(cm.authoredPlayBounds().valid) << "a refused box must not half-apply";
}

// ---------------------------------------------------------------- the character honours it

#include "physics/PhysicsWorld.h"
#include "scene/AnimatedVoxelCharacter.h"

namespace {
// A humanoid on a headless physics world, driven south by an external move velocity (the NPC
// steering input) from 2 u inside the farm's south edge - the shape of the shipped red run.
struct WalkSouth {
    Phyxel::Physics::PhysicsWorld physics;
    Phyxel::Scene::AnimatedVoxelCharacter ch{&physics, glm::vec3(6.0f, 20.0f, -29.8f)};
    float minZ = 1e9f;
    bool run(Phyxel::ChunkManager* cm) {
        if (!ch.loadModel("resources/animated_characters/humanoid.anim")) return false;
        ch.setChunkManager(cm);
        for (int i = 0; i < 180; ++i) {               // three seconds at 60 Hz
            ch.setMoveVelocity(glm::vec3(0.0f, 0.0f, -3.0f));
            ch.update(1.0f / 60.0f);
            minZ = std::min(minZ, ch.getPosition().z);
        }
        return true;
    }
};
}  // namespace

TEST_F(HeadlessWorld, AControlCharacterWithoutBoundsWalksPastTheEdge) {
    // The measurement's control: the same drive with NO chunk manager must leave the rectangle,
    // or the test below would pass for a character that never moved.
    WalkSouth w;
    ASSERT_TRUE(w.run(nullptr));
    EXPECT_LT(w.minZ, -32.0f) << "without bounds the body walks off the edge (red run: z -36.2)";
}

TEST_F(HeadlessWorld, ACharacterWithBoundsIsHeldAtTheEdgeOnEveryFrame) {
    for (int x = -1; x <= 1; ++x)
        for (int z = -1; z <= 1; ++z) chunkAt(x, z);   // the farm's rectangle: z >= -32
    WalkSouth w;
    ASSERT_TRUE(w.run(&cm));
    EXPECT_GE(w.minZ, -32.0f) << "the wall: no frame puts the body centre past the edge";
    EXPECT_LT(w.minZ, -30.5f) << "...and the body did reach the wall (it is not simply stuck)";
    EXPECT_GE(w.ch.getPosition().z, -32.0f);
}
