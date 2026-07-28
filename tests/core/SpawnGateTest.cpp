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

// ===========================================================================
// resolveSpawnWithClimb — the escalation, now in core so it HAS tests.
//
// While this logic sat inline in editor/src/Application.cpp it had zero automated
// coverage at any layer (that file is linked by no unit-test target), and in that
// state it shipped two defects that only live probing found: an inert branch, and a
// 27-SECOND main-loop stall. Both are pinned below.
// ===========================================================================

namespace {
// Wraps a solidity query and COUNTS the calls. The cost regression is only catchable
// if the cost is measurable, so it is measured rather than eyeballed.
struct CountingSolid {
    SolidAABBFn inner;
    mutable long calls = 0;
    SolidAABBFn fn() const {
        auto* self = const_cast<CountingSolid*>(this);
        return [self](const glm::vec3& lo, const glm::vec3& hi) {
            ++self->calls;
            return self->inner(lo, hi);
        };
    }
};

// A tall solid column with a 1-cube air pocket at `pocketY`: feet clear, body in rock,
// nothing clear laterally. The shape that defeated the old feet-cube escalation.
BoxWorld encasedPocketWorld(float pocketY, float columnTop) {
    BoxWorld w;
    w.ground(16.0f);
    w.add({20.0f, 16.0f, 20.0f}, {31.0f, columnTop, 31.0f});   // the column
    // carve the pocket (a hole in the middle of the column) by rebuilding around it
    BoxWorld out;
    out.ground(16.0f);
    out.add({20.0f, 16.0f, 20.0f}, {31.0f, pocketY, 31.0f});           // below pocket
    out.add({20.0f, pocketY + 1.0f, 20.0f}, {31.0f, columnTop, 31.0f}); // above pocket
    out.add({20.0f, pocketY, 20.0f}, {25.0f, pocketY + 1.0f, 31.0f});   // pocket ring -x
    out.add({26.0f, pocketY, 20.0f}, {31.0f, pocketY + 1.0f, 31.0f});   // pocket ring +x
    out.add({25.0f, pocketY, 20.0f}, {26.0f, pocketY + 1.0f, 25.0f});   // pocket ring -z
    out.add({25.0f, pocketY, 26.0f}, {26.0f, pocketY + 1.0f, 31.0f});   // pocket ring +z
    return out;   // the 1-cube void is (25..26, pocketY..pocketY+1, 25..26)
}
}  // namespace

// The defect that made the escalation inert: feet cube EMPTY, body embedded. A
// feets-only check reports "nothing to do" and the spawn is refused.
TEST(SpawnGateTest, ClimbEscapesAPocketWhereTheFeetAreClearButTheBodyIsNot) {
    const auto w = encasedPocketWorld(/*pocketY=*/30.0f, /*columnTop=*/50.0f);
    const glm::vec3 pocket(25.5f, 30.0f, 25.5f);

    ASSERT_TRUE(spawnIsEmbedded(w.fn(), pocket))
        << "fixture is wrong: the body should be embedded in the pocket";
    ASSERT_FALSE(spawnIsSupported(w.fn(), pocket) && !spawnIsEmbedded(w.fn(), pocket));

    // Plain resolveSpawn cannot escape -- everything within its lateral radius is rock.
    EXPECT_EQ(resolveSpawn(w.fn(), pocket).outcome, SpawnOutcome::Refused)
        << "the lateral search escaped a fully encased pocket - fixture is not encased";

    const SpawnResult r = resolveSpawnWithClimb(w.fn(), pocket);
    ASSERT_TRUE(r.ok()) << r.reason;
    EXPECT_EQ(r.outcome, SpawnOutcome::Relocated);
    EXPECT_FALSE(spawnIsEmbedded(w.fn(), r.position)) << "climbed to a position still in rock";
    EXPECT_GE(r.position.y, 50.0f) << "did not climb clear of the column top (y=50)";
    EXPECT_TRUE(r.supported) << "should land ON the column, not in the air above it";
}

// THE COST REGRESSION. The old climb re-ran the full resolveSpawn per step (~1536 x a
// 9-height x 12-ring search) and stalled the engine 27 seconds. Budget the solidity
// queries so that can never silently return.
TEST(SpawnGateTest, ClimbingAnUnresolvableColumnStaysWithinAQueryBudget) {
    // A column taller than the climb range, wide enough to block the lateral search at
    // every height: the genuinely unresolvable case, i.e. the worst path through the loop.
    BoxWorld w;
    w.ground(16.0f);
    w.add({0.0f, 16.0f, 0.0f}, {40.0f, 4000.0f, 40.0f});
    CountingSolid cs{w.fn()};

    const SpawnResult r = resolveSpawnWithClimb(cs.fn(), glm::vec3(20.0f, 20.0f, 20.0f), {},
                                                /*searchRadius=*/4.0f, /*maxClimb=*/512.0f);
    EXPECT_EQ(r.outcome, SpawnOutcome::Refused) << "a 4000-tall column was escapable?";

    // Per climb step the contract is a couple of AABB tests, not a nested ring search.
    // 512 m / (1/3 m) = 1536 steps; allow the lateral resolveSpawn attempt plus generous
    // slack, but far below the millions the per-step-resolveSpawn version cost.
    const long kBudget = 200000;
    EXPECT_LT(cs.calls, kBudget)
        << "the climb issued " << cs.calls << " solidity queries (budget " << kBudget
        << "). The per-step full-resolveSpawn version cost millions and stalled the main "
           "loop 27 seconds on exactly this input.";
}

// Teeth for the budget test: prove the counter and fixture register a LARGE cost when
// the work really is large, so the budget assertion is not passing on a no-op.
TEST(SpawnGateTest, TheQueryCounterActuallyRegistersWork) {
    BoxWorld w;
    w.ground(16.0f);
    w.add({0.0f, 16.0f, 0.0f}, {40.0f, 4000.0f, 40.0f});
    CountingSolid cs{w.fn()};
    resolveSpawnWithClimb(cs.fn(), glm::vec3(20.0f, 20.0f, 20.0f), {}, 4.0f, 512.0f);
    EXPECT_GT(cs.calls, 1000)
        << "only " << cs.calls << " queries - the fixture is not exercising the climb, so "
           "the budget test above would pass trivially";
}

// A clear spawn must not trigger any climb at all.
TEST(SpawnGateTest, AClearSpawnDoesNotClimb) {
    const auto w = thinWallWorld();
    const glm::vec3 open(8.0f, 16.0f, 5.0f);
    const SpawnResult r = resolveSpawnWithClimb(w.fn(), open);
    EXPECT_EQ(r.outcome, SpawnOutcome::Clear);
    EXPECT_EQ(r.position, open);
}

// ===========================================================================
// SPECIES SIZE — the gate must check the body that will actually exist.
//
// solution-auditor round 6: applySpawnGate calls resolveSpawn WITHOUT a
// CharacterBounds, so it silently uses the default 0.25 m / 1.75 m HUMANOID box
// for every species. Fauna is not humanoid: BodyPlan clamps capsule half-width to
// [0.12, 0.60] m (BodyPlan.h:52-53), and the real value is only resolved AFTER
// NPCEntity construction, when resizeController measures the loaded skeleton --
// i.e. strictly after the gate has already run.
//
// The merged FaunaSpawner spawns wolves, horses, stags and dragons through that
// funnel. Routing through a gate that measures the wrong volume is not protection,
// and the "a character is never created inside static geometry" claim is only true
// for humanoids until this is threaded through.
// ===========================================================================

// The gap, stated as an executable fact: a corridor that comfortably fits the
// humanoid box does NOT fit a wide quadruped, and the default-bounds check cannot
// tell the difference.
TEST(SpawnGateTest, TheDefaultHumanoidBoxDeclaresAGapClearThatAWideBodyCannotFit) {
    // Two walls 0.9 m apart on flat ground: wider than a humanoid (0.5 m), narrower
    // than a large quadruped (up to 1.2 m at BodyPlan's maxHalfWidth).
    BoxWorld w;
    w.ground(16.0f);
    // Inner faces at x=9.55 and x=10.45 -> a 0.90 m gap centred on x=10.0.
    w.add({8.00f, 16.0f, 0.0f}, {9.55f, 20.0f, 20.0f});
    w.add({10.45f, 16.0f, 0.0f}, {12.00f, 20.0f, 20.0f});
    const glm::vec3 inGap(10.0f, 16.0f, 10.0f);

    const CharacterBounds humanoid{};                       // 0.25 m half-width
    const CharacterBounds quadruped{0.55f, 1.30f};          // a large imported animal

    // What the gate checks today:
    EXPECT_FALSE(spawnIsEmbedded(w.fn(), inGap, humanoid))
        << "fixture is wrong: the humanoid box should FIT this 0.9 m gap";
    EXPECT_EQ(resolveSpawn(w.fn(), inGap, humanoid).outcome, SpawnOutcome::Clear);

    // What actually gets created there:
    EXPECT_TRUE(spawnIsEmbedded(w.fn(), inGap, quadruped))
        << "a 1.1 m-wide body should NOT fit a 0.9 m gap - if this passes, the fixture "
           "no longer models the species-size gap and the test below proves nothing";

    // Therefore: the gate, given the real body, does the right thing. The defect is
    // purely that nothing PASSES the real body -- which is what the API change fixes.
    const SpawnResult r = resolveSpawn(w.fn(), inGap, quadruped);
    EXPECT_NE(r.outcome, SpawnOutcome::Clear)
        << "given the REAL body the gate must not report Clear in a gap too narrow for it";
    if (r.ok()) EXPECT_FALSE(spawnIsEmbedded(w.fn(), r.position, quadruped));
}

// The climb must honour species size too, or a large creature gets 'rescued' onto a
// ledge that only a humanoid fits.
TEST(SpawnGateTest, TheClimbHonoursTheSuppliedBodySize) {
    BoxWorld w;
    w.ground(16.0f);
    // A solid slab with a shallow shelf above it: 1.0 m of headroom, fine for nothing tall.
    w.add({0.0f, 16.0f, 0.0f}, {20.0f, 24.0f, 20.0f});
    w.add({0.0f, 25.0f, 0.0f}, {20.0f, 30.0f, 20.0f});   // ceiling 1.0 m above the slab top

    const CharacterBounds tall{0.25f, 2.60f};   // a big creature
    const SpawnResult r = resolveSpawnWithClimb(w.fn(), glm::vec3(10.0f, 18.0f, 10.0f), tall);
    if (r.ok())
        EXPECT_FALSE(spawnIsEmbedded(w.fn(), r.position, tall))
            << "the climb placed a 2.6 m body somewhere it does not fit";
}
