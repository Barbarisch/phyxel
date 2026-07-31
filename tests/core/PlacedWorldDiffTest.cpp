#include <gtest/gtest.h>

#include <functional>
#include <set>
#include <vector>

#include "core/BuildingProgram.h"
#include "core/MicroCanvas.h"
#include "core/PlacedWorldDiff.h"
#include "core/RoomLayout.h"
#include "core/RoomProgram.h"
#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"

using namespace Phyxel::Core;

// ============================================================================
// PlacedWorldDiff — the canvas<->world seam.
//
// Everything in the structure pipeline validates the PLAN (the MicroCanvas). This
// is the first check that the ARTIFACT matches it. A test suite for it therefore
// has to be adversarial about one thing above all: a diff that always reports
// "perfect" is worse than no diff, because it launders the seam it was built to
// expose. So every positive case below is paired with a world deliberately
// mutated to drop cells, and the diff must find exactly those.
// ============================================================================

namespace {

// A world backed by an explicit set of solid micro cells — the artifact side.
struct MicroWorld {
    std::set<std::tuple<int, int, int>> solid;

    void stampCanvas(const MicroCanvas& c, const glm::ivec3& originCubes) {
        const glm::ivec3 o = originCubes * 9;
        for (const auto& lc : c.occupiedCells())
            solid.insert({o.x + lc.x, o.y + lc.y, o.z + lc.z});
    }
    /// Drop every Nth stamped cell — a silent partial placement.
    size_t dropEveryNth(int n) {
        size_t dropped = 0;
        std::vector<std::tuple<int, int, int>> keep;
        int i = 0;
        for (const auto& c : solid) {
            if (i++ % n == 0) { ++dropped; continue; }
            keep.push_back(c);
        }
        solid.clear();
        for (const auto& c : keep) solid.insert(c);
        return dropped;
    }
    SolidMicroFn fn() const {
        auto copy = solid;
        return [copy](int x, int y, int z) { return copy.count({x, y, z}) > 0; };
    }
};

// A realized cottage — a REAL canvas from the real realizer, not a hand-built blob,
// so the diff is exercised against the shape the pipeline actually produces
// (sub-cube walls, carved openings, a pitched roof).
StructureRealizer::ShellResult realizeCottage(int w = 8, int d = 6) {
    StyleProfileRegistry reg;
    reg.loadFromJson(nlohmann::json::parse(R"({
        "timber_cottage": { "roof_style":"gable", "foundation":"slab",
            "thickness": { "exterior_wall":0.333, "interior_wall":0.222, "foundation_wall":0.667,
                           "floor":0.333, "ceiling":0.222 },
            "materials": { "structure":"Wood", "floor":"Wood", "roof":"Wood", "foundation":"Stone" },
            "roof": { "pitch":0.8 } } })"));
    BuildingProgram p;
    p.name = "c"; p.style = "timber_cottage"; p.typology = "croft";
    p.footprintW = w; p.footprintD = d; p.substructure = "slab";
    ProgStory s; s.height = 3; p.stories.push_back(s);
    autofillRoomLayout(p, 1u, nullptr);
    return StructureRealizer::realizeShell(p, *reg.get("timber_cottage"));
}

}  // namespace

// ---------------------------------------------------------------------------
// A faithfully stamped world diffs clean. If this failed, every other result
// would be noise.
// ---------------------------------------------------------------------------
TEST(PlacedWorldDiffTest, AFaithfullyStampedStructureDiffsClean) {
    const auto shell = realizeCottage();
    ASSERT_TRUE(shell.ok) << shell.error;
    const glm::ivec3 origin(20, 16, 30);

    MicroWorld w;
    w.stampCanvas(shell.canvas, origin);

    const PlacedDiff d = diffCanvasAgainstWorld(shell.canvas, origin, w.fn());
    EXPECT_TRUE(d.ok()) << d.summary();
    EXPECT_TRUE(d.missing.empty());
    EXPECT_GT(d.plannedCells, 0) << "the realizer produced an EMPTY canvas - this test is vacuous";
    EXPECT_EQ(d.matchedCells, d.plannedCells);
    EXPECT_DOUBLE_EQ(d.fidelity(), 1.0);
}

// ---------------------------------------------------------------------------
// TEETH — the defect this exists to catch. A partial stamp (the silent-drop
// family: voxel caps, ungenerated chunks, mixed-resolution overwrite refusals)
// must be found, counted, and LOCATED.
// ---------------------------------------------------------------------------
TEST(PlacedWorldDiffTest, ASilentlyPartialStampIsCaughtCountedAndLocated) {
    const auto shell = realizeCottage();
    ASSERT_TRUE(shell.ok) << shell.error;
    const glm::ivec3 origin(20, 16, 30);

    MicroWorld w;
    w.stampCanvas(shell.canvas, origin);
    const size_t dropped = w.dropEveryNth(7);
    ASSERT_GT(dropped, 0u);

    const PlacedDiff d = diffCanvasAgainstWorld(shell.canvas, origin, w.fn(),
                                                /*maxReported=*/100000);
    EXPECT_FALSE(d.ok()) << "a partial stamp diffed CLEAN - the seam is not being checked";
    EXPECT_EQ(d.missing.size(), dropped)
        << "expected exactly the dropped cells, got " << d.missing.size() << " of " << dropped;
    EXPECT_LT(d.fidelity(), 1.0);

    // Every reported cell must actually be planned-but-absent, and its world/local
    // coords must correspond -- otherwise the report points somewhere useless.
    const auto solid = w.fn();
    for (const auto& m : d.missing) {
        EXPECT_TRUE(shell.canvas.occupiedMicro(m.local.x, m.local.y, m.local.z))
            << "reported a MISSING cell that was never planned";
        EXPECT_FALSE(solid(m.world.x, m.world.y, m.world.z))
            << "reported a MISSING cell that IS solid in the world";
        EXPECT_EQ(m.world, origin * 9 + m.local) << "world/local coords disagree";
    }
}

// A structure stamped at the WRONG ORIGIN is the pathological case: the plan is
// perfect, the world is full of voxels, and nothing matches. Fidelity must
// collapse rather than the diff quietly succeeding.
TEST(PlacedWorldDiffTest, AStructureStampedAtTheWrongOriginCollapsesFidelity) {
    const auto shell = realizeCottage();
    ASSERT_TRUE(shell.ok) << shell.error;

    MicroWorld w;
    w.stampCanvas(shell.canvas, glm::ivec3(20, 16, 30));       // stamped here
    const PlacedDiff d = diffCanvasAgainstWorld(shell.canvas,
                                                glm::ivec3(60, 16, 90),  // looked for here
                                                w.fn(), /*maxReported=*/100000);
    EXPECT_FALSE(d.ok());
    EXPECT_EQ(d.matchedCells, 0) << "cells matched at an origin 40 cubes away";
    EXPECT_DOUBLE_EQ(d.fidelity(), 0.0);
}

// ---------------------------------------------------------------------------
// Truncation must be LOUD. A catastrophically wrong build produces a huge diff;
// capping the report is fine, hiding that it was capped is the silent-failure
// pattern this whole module exists to end.
// ---------------------------------------------------------------------------
TEST(PlacedWorldDiffTest, HittingTheReportCapIsSurfacedNotSilent) {
    const auto shell = realizeCottage();
    ASSERT_TRUE(shell.ok) << shell.error;

    MicroWorld empty;   // nothing was stamped at all
    const PlacedDiff d = diffCanvasAgainstWorld(shell.canvas, glm::ivec3(0, 0, 0),
                                                empty.fn(), /*maxReported=*/10);
    EXPECT_TRUE(d.truncated) << "the cap was hit but not reported";
    EXPECT_FALSE(d.ok()) << "a truncated diff must never report ok()";
    EXPECT_EQ(d.missing.size(), 10u);
    EXPECT_GT(d.plannedCells, 10) << "counts must reflect the WHOLE plan, not the capped list";
    EXPECT_EQ(d.matchedCells, 0);
    EXPECT_NE(d.summary().find("TRUNCATED"), std::string::npos)
        << "summary hides the truncation: " << d.summary();
}

// No world query = no claim. A diff that cannot see the world must not invent
// thousands of drops (which would make it useless in headless contexts).
TEST(PlacedWorldDiffTest, WithoutAWorldQueryTheDiffMakesNoClaim) {
    const auto shell = realizeCottage();
    ASSERT_TRUE(shell.ok);
    const PlacedDiff d = diffCanvasAgainstWorld(shell.canvas, glm::ivec3(0, 0, 0), SolidMicroFn{});
    EXPECT_TRUE(d.ok());
    EXPECT_EQ(d.plannedCells, 0);
    EXPECT_TRUE(d.missing.empty());
}

// ---------------------------------------------------------------------------
// The AABB adapter is what connects this to the live engine
// (VoxelDynamicsWorld::anyStaticSolidInAABB). Its inset must be tight enough that
// a neighbouring cell's face does not read as filling this one -- otherwise every
// cell adjacent to geometry reads solid and the diff reports perfect fidelity for
// any world at all.
// ---------------------------------------------------------------------------
TEST(PlacedWorldDiffTest, TheAABBAdapterDoesNotLeakAcrossCellBoundaries) {
    // One solid micro cell at micro (9, 9, 9) == world [1.0, 1.111) on each axis.
    std::function<bool(const glm::vec3&, const glm::vec3&)> solidAABB =
        [](const glm::vec3& lo, const glm::vec3& hi) {
            const float cell = 1.0f / 9.0f;
            const glm::vec3 blo(9 * cell, 9 * cell, 9 * cell);
            const glm::vec3 bhi(10 * cell, 10 * cell, 10 * cell);
            return lo.x < bhi.x && hi.x > blo.x && lo.y < bhi.y && hi.y > blo.y &&
                   lo.z < bhi.z && hi.z > blo.z;
        };
    const SolidMicroFn f = microSolidityFromAABB(solidAABB);
    ASSERT_TRUE(f) << "adapter returned an empty function";

    EXPECT_TRUE(f(9, 9, 9)) << "the occupied cell does not read as solid";
    for (const auto& n : {glm::ivec3(8, 9, 9), glm::ivec3(10, 9, 9), glm::ivec3(9, 8, 9),
                          glm::ivec3(9, 10, 9), glm::ivec3(9, 9, 8), glm::ivec3(9, 9, 10)})
        EXPECT_FALSE(f(n.x, n.y, n.z))
            << "neighbour (" << n.x << "," << n.y << "," << n.z
            << ") reads solid - the adapter leaks across the shared face, so EVERY cell "
               "touching geometry would count as stamped and the diff would always pass";
}
