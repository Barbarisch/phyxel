#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "core/SpawnGate.h"
#include "core/StructureBuildService.h"

using namespace Phyxel::Core;

// ============================================================================
// SpawnGate — "it should be impossible (by default) to generate a character
// inside a wall/object/static voxel" (user directive, 2026-07-28).
//
// The teeth that matter here are about RESOLUTION. The failure this gate exists
// to prevent is a character embedded in an INTERIOR PARTITION, which is ~2 micro
// (0.22 m) thick and sits inside a cube that a cube-granular check reports as
// empty. So every test below uses a thin wall, and one test pins the cube-only
// approach FAILING on it — otherwise a gate could pass its own tests while being
// blind to the exact geometry people get stuck in.
// ============================================================================

namespace {

// A world of axis-aligned solid boxes, queried as an AABB overlap — the same shape
// as Physics::VoxelDynamicsWorld::anyStaticSolidInAABB, which is what the engine
// wires in.
struct BoxWorld {
    struct Box { glm::vec3 lo, hi; };
    std::vector<Box> boxes;

    void add(glm::vec3 lo, glm::vec3 hi) { boxes.push_back({lo, hi}); }

    /// Flat ground: solid slab below y = top.
    void ground(float top, float extent = 40.0f) {
        add({-extent, top - 4.0f, -extent}, {extent, top, extent});
    }

    SolidAABBFn fn() const {
        auto copy = boxes;
        return [copy](const glm::vec3& lo, const glm::vec3& hi) {
            for (const auto& b : copy)
                if (lo.x < b.hi.x && hi.x > b.lo.x && lo.y < b.hi.y && hi.y > b.lo.y &&
                    lo.z < b.hi.z && hi.z > b.lo.z)
                    return true;
            return false;
        };
    }
};

// A 2-micro (0.222 m) interior partition running along Z at x = 5.0, on ground y = 16.
BoxWorld thinWallWorld() {
    BoxWorld w;
    w.ground(16.0f);
    w.add({5.0f, 16.0f, 0.0f}, {5.0f + 2.0f / 9.0f, 19.0f, 10.0f});
    return w;
}

}  // namespace

// ---------------------------------------------------------------------------
// The core requirement: a character requested INSIDE a wall is not spawned there.
// ---------------------------------------------------------------------------
TEST(SpawnGateTest, ACharacterRequestedInsideAThinWallIsNotSpawnedThere) {
    const auto w = thinWallWorld();
    const glm::vec3 inWall(5.1f, 16.0f, 5.0f);   // dead inside the 0.222 m partition

    EXPECT_TRUE(spawnIsEmbedded(w.fn(), inWall))
        << "a body standing inside a 2-micro partition must read as embedded";

    const SpawnResult r = resolveSpawn(w.fn(), inWall);
    EXPECT_NE(r.outcome, SpawnOutcome::Clear) << "the gate did not notice the wall";
    ASSERT_TRUE(r.ok()) << r.reason;   // open ground either side: resolvable
    EXPECT_EQ(r.outcome, SpawnOutcome::Relocated);
    EXPECT_FALSE(spawnIsEmbedded(w.fn(), r.position))
        << "the RESOLVED position is still inside geometry - the gate moved it nowhere useful";
    EXPECT_TRUE(r.supported) << "relocated onto unsupported air when solid ground was available";
}

// ---------------------------------------------------------------------------
// TEETH — the resolution point. The pre-existing cube-granular helper CANNOT see
// this wall. If this ever starts passing, the thin-wall case has become trivial
// and the test above stops proving anything.
// ---------------------------------------------------------------------------
TEST(SpawnGateTest, ACubeGranularCheckIsBlindToTheThinWallThatTrapsCharacters) {
    const auto w = thinWallWorld();
    const glm::vec3 inWall(5.1f, 16.0f, 5.0f);

    // The cube-level view: "is the containing CUBE solid?" — cube (5,16,5) is only
    // 2/9 full, so a cube-granular world model reports empty.
    auto cubeSolid = [&w](const glm::ivec3& c) {
        // A cube counts as solid only if it is (near) fully occupied, which is what a
        // cube-resolution voxel query means.
        const glm::vec3 lo(static_cast<float>(c.x) + 0.4f, static_cast<float>(c.y) + 0.4f,
                           static_cast<float>(c.z) + 0.4f);
        const glm::vec3 hi(lo.x + 0.2f, lo.y + 0.2f, lo.z + 0.2f);   // cube centre probe
        return w.fn()(lo, hi);
    };
    EXPECT_FALSE(cubeSolid(glm::ivec3(5, 16, 5)))
        << "the thin wall filled the cube centre - this fixture no longer models a THIN wall";

    // snapToStandable over that same cube-granular view therefore sees nothing wrong
    // and hands the caller the embedded cell straight back.
    const glm::ivec3 cell(5, 16, 5);
    const glm::ivec3 snapped = StructureBuildService::snapToStandable(cubeSolid, cell, 4);
    EXPECT_EQ(snapped, cell)
        << "cube-granular snapping unexpectedly moved the character - if it now detects thin "
           "walls, SpawnGate's reason for existing has changed";

    // And the resolution-complete gate does catch it.
    EXPECT_TRUE(spawnIsEmbedded(w.fn(), glm::vec3(5.1f, 16.0f, 5.0f)));
}

// ---------------------------------------------------------------------------
// TEETH — a clear spawn must be left ALONE. A gate that relocates everybody is
// as broken as one that relocates nobody, and would silently scatter every NPC.
// ---------------------------------------------------------------------------
TEST(SpawnGateTest, AClearSpawnIsLeftExactlyWhereItWasAsked) {
    const auto w = thinWallWorld();
    const glm::vec3 open(8.0f, 16.0f, 5.0f);

    const SpawnResult r = resolveSpawn(w.fn(), open);
    EXPECT_EQ(r.outcome, SpawnOutcome::Clear);
    EXPECT_EQ(r.position, open);
    EXPECT_FLOAT_EQ(r.movedDistance, 0.0f);
    EXPECT_TRUE(r.supported);
}

// A character standing FLUSH against a wall face is fine and must not be moved —
// this is the case the skin inset exists for.
TEST(SpawnGateTest, StandingFlushAgainstAWallIsNotEmbedded) {
    const auto w = thinWallWorld();
    // Feet centred so the body's +x face exactly touches the wall's -x face at x=5.0.
    const glm::vec3 flush(5.0f - 0.25f, 16.0f, 5.0f);
    EXPECT_FALSE(spawnIsEmbedded(w.fn(), flush))
        << "a body touching a wall face reads as inside it - the gate would relocate "
           "every character who stands against a wall";
    EXPECT_EQ(resolveSpawn(w.fn(), flush).outcome, SpawnOutcome::Clear);
}

// ---------------------------------------------------------------------------
// TEETH — REFUSAL is real. Fully encased in solid with nothing clear in range,
// the gate must refuse rather than hand back the embedded position.
// ---------------------------------------------------------------------------
TEST(SpawnGateTest, FullyEncasedSpawnIsRefusedNotSilentlyAccepted) {
    BoxWorld w;
    w.ground(16.0f);
    w.add({-10.0f, 16.0f, -10.0f}, {10.0f, 24.0f, 10.0f});   // a solid block of stone

    const glm::vec3 buried(0.0f, 17.0f, 0.0f);
    const SpawnResult r = resolveSpawn(w.fn(), buried, {}, /*searchRadius=*/3.0f);

    EXPECT_EQ(r.outcome, SpawnOutcome::Refused);
    EXPECT_FALSE(r.ok()) << "a fully buried spawn reported ok() - callers will spawn it";
    EXPECT_EQ(r.position, buried) << "a refused spawn must not hand back a moved position";
    EXPECT_FALSE(r.reason.empty()) << "a refusal must say why";
}

// A spawn deep inside a big block IS resolvable when the search radius reaches open
// air — proving refusal is about reachability, not a blanket give-up.
TEST(SpawnGateTest, TheSameBuriedSpawnResolvesWhenTheSearchReachesOpenAir) {
    BoxWorld w;
    w.ground(16.0f);
    w.add({-1.0f, 16.0f, -1.0f}, {1.0f, 24.0f, 1.0f});   // a narrow 2 m pillar

    const glm::vec3 buried(0.0f, 17.0f, 0.0f);
    const SpawnResult r = resolveSpawn(w.fn(), buried, {}, /*searchRadius=*/4.0f);

    ASSERT_TRUE(r.ok()) << r.reason;
    EXPECT_EQ(r.outcome, SpawnOutcome::Relocated);
    EXPECT_FALSE(spawnIsEmbedded(w.fn(), r.position));
}

// ---------------------------------------------------------------------------
// Resolution granularity: escaping a thin wall must be a SMALL step, not a leap
// through it into the next room. A cube-sized search step would overshoot.
// ---------------------------------------------------------------------------
TEST(SpawnGateTest, EscapingAThinWallIsAShortStepNotALeapThroughIt) {
    const auto w = thinWallWorld();
    const glm::vec3 inWall(5.1f, 16.0f, 5.0f);

    const SpawnResult r = resolveSpawn(w.fn(), inWall);
    ASSERT_EQ(r.outcome, SpawnOutcome::Relocated);
    EXPECT_LT(r.movedDistance, 1.0f)
        << "moved " << r.movedDistance << " m to clear a 0.22 m wall - the search step is "
           "coarse enough to teleport characters through walls";
}

// No solidity query = no claim. A gate that cannot see the world must not start
// refusing spawns out of ignorance (that would break every headless/test caller).
TEST(SpawnGateTest, WithoutASolidityQueryTheGateStandsDownInsteadOfRefusing) {
    const SpawnResult r = resolveSpawn(SolidAABBFn{}, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(r.outcome, SpawnOutcome::Clear);
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(r.position, glm::vec3(1.0f, 2.0f, 3.0f));
}
