#include <gtest/gtest.h>

#include <cmath>
#include <fstream>

#include <nlohmann/json.hpp>

#include "core/BuildingProgram.h"
#include "core/FurniturePlacer.h"
#include "core/HearthForge.h"
#include "core/StructureGrid.h"

using namespace Phyxel::Core;

// ============================================================================
// HearthForge — the vented built-in, as a forge (docs/ForgePattern.md):
//   * GROUNDED: every body dimension traces to object_dimensions.json, the same
//     canon FurnitureConformanceTest holds the furniture templates to. Moving
//     the hearth out of the asset library and into the shell must not quietly
//     resize it.
//   * DETERMINISTIC: same program -> same pose, every time.
//   * ONE POSE: what the floorplan SITES must be exactly what the furnish pass
//     later reserves — otherwise the shell builds a hearth in one place and the
//     furniture is arranged around another.
// ============================================================================

namespace {

/// The grounded extents of an archetype from resources/object_dimensions.json,
/// in metres, or false when the canon (or the file) is unavailable.
bool canonExtents(const std::string& archetype, double& w, double& h, double& d, double& tol) {
    std::ifstream in("resources/object_dimensions.json");
    if (!in.is_open()) return false;
    nlohmann::json j;
    try { in >> j; } catch (...) { return false; }
    const nlohmann::json* obj = nullptr;
    if (j.contains("archetypes") && j["archetypes"].contains(archetype))
        obj = &j["archetypes"][archetype];
    else if (j.contains(archetype)) obj = &j[archetype];
    if (!obj || !obj->is_object()) return false;
    if (!obj->contains("width") || !obj->contains("depth")) return false;
    w = (*obj)["width"].get<double>();
    d = (*obj)["depth"].get<double>();
    h = obj->contains("height") ? (*obj)["height"].get<double>() : 0.0;
    tol = obj->value("tol", 0.2);
    return true;
}

ProgStory hallStory(int W, int D) {
    ProgStory st;
    st.height = 3;
    ProgRoom rm;
    rm.id = "hall"; rm.purpose = "hall"; rm.rect = Rect{0, 0, W, D};
    st.rooms.push_back(rm);
    return st;
}

}  // namespace

// GROUNDED — the painted hearth is the canon size, not a number invented here.
// (Skipped, loudly, when the test binary cannot see resources/ — a silent pass
// would let the grounding rot.)
TEST(HearthForgeTest, BodyDimensionsMatchTheGroundedCanon) {
    struct Case { const char* type; const char* archetype; };
    for (const Case& c : {Case{"fireplace", "hearth"},
                          Case{"forge_hearth", "forge_hearth"},
                          Case{"oven_bread", "oven_bread"}}) {
        double cw = 0, ch = 0, cd = 0, tol = 0.2;
        if (!canonExtents(c.archetype, cw, ch, cd, tol)) {
            GTEST_SKIP() << "resources/object_dimensions.json not reachable from the test "
                            "working directory — grounding NOT checked";
        }
        const auto b = HearthForge::bodyOf(c.type);
        ASSERT_TRUE(b.known) << c.type << " has no body preset";
        EXPECT_NEAR(b.w / 9.0, cw, tol) << c.type << " width is off canon";
        EXPECT_NEAR(b.d / 9.0, cd, tol) << c.type << " depth is off canon";
    }
}

// The flue fits INSIDE the body it rises from — a stack wider than its hearth
// would hang off the masonry it is supposed to rest on.
TEST(HearthForgeTest, StackFitsWithinTheBodyItRisesFrom) {
    for (const char* type : {"fireplace", "forge_hearth", "oven_bread"}) {
        const auto b = HearthForge::bodyOf(type);
        ASSERT_TRUE(b.known) << type;
        EXPECT_GE(b.flueCx - HearthForge::kStackHalfMicro, 0) << type << " stack overhangs -x";
        EXPECT_LT(b.flueCx + HearthForge::kStackHalfMicro, b.w) << type << " stack overhangs +x";
        EXPECT_GE(b.flueCz - HearthForge::kStackHalfMicro, 0) << type << " stack overhangs -z";
        EXPECT_LT(b.flueCz + HearthForge::kStackHalfMicro, b.d) << type << " stack overhangs +z";
    }
}

// The footprint the placer reserves must cover the body that gets PAINTED.
TEST(HearthForgeTest, FootprintCoversThePaintedBody) {
    for (const char* type : {"fireplace", "forge_hearth", "oven_bread"}) {
        const auto b = HearthForge::bodyOf(type);
        const auto fp = HearthForge::footprintOf(type);
        EXPECT_GE(fp.width * 9, b.w) << type << " footprint narrower than the body";
        EXPECT_GE(fp.depth * 9, b.d) << type << " footprint shallower than the body";
        EXPECT_EQ(fp.microW, b.w - 1) << type;
        EXPECT_EQ(fp.microD, b.d - 1) << type;
    }
}

// ONE POSE. The floorplan sites the hearth with the same algorithm the furnish
// pass runs, so the two must agree field-for-field. If they ever diverge, the
// shell builds masonry in one place and the room is furnished around another —
// exactly the class of silent mismatch this plan set out to remove.
TEST(HearthForgeTest, SitedPoseMatchesTheFurnishPass) {
    FurniturePlacer::clearRecipes();
    const ProgStory story = hallStory(12, 8);
    std::map<std::string, Footprint> fps;
    for (const char* t : {"fireplace", "forge_hearth", "oven_bread"})
        fps[t] = HearthForge::footprintOf(t);

    const auto sited = HearthForge::siteHearths(story, fps, /*extT=*/3, /*intT=*/2, {}, "");
    ASSERT_EQ(sited.size(), 1u) << "the hall recipe must site exactly one hearth";

    const auto furnished = FurniturePlacer::furnish(story, glm::ivec3(0), 0, fps, nullptr,
                                                    /*extTMicro=*/3, "", {}, /*intTMicro=*/2);
    const FurniturePlacement* mirror = nullptr;
    for (const auto& p : furnished) if (HearthForge::isVented(p.type)) mirror = &p;
    ASSERT_NE(mirror, nullptr) << "the furnish pass placed no hearth to compare against";

    EXPECT_EQ(sited[0].type, mirror->type);
    EXPECT_EQ(sited[0].room, mirror->room);
    EXPECT_EQ(sited[0].rotation, mirror->rotation);
    EXPECT_EQ(sited[0].worldPos, mirror->worldPos);
    EXPECT_EQ(sited[0].backDir, mirror->backDir);
    EXPECT_EQ(sited[0].insetMicroX, mirror->insetMicroX);
    EXPECT_EQ(sited[0].insetMicroZ, mirror->insetMicroZ);
}

// DETERMINISM — same story in, same program fixture out (a forge contract).
TEST(HearthForgeTest, SitingIsDeterministic) {
    FurniturePlacer::clearRecipes();
    ProgStory a = hallStory(12, 8), b = hallStory(12, 8);
    EXPECT_EQ(HearthForge::siteIntoProgram(a, {}, 3, 2, {}, ""), 1);
    EXPECT_EQ(HearthForge::siteIntoProgram(b, {}, 3, 2, {}, ""), 1);
    ASSERT_EQ(a.fixtures.size(), b.fixtures.size());
    for (size_t i = 0; i < a.fixtures.size(); ++i)
        EXPECT_EQ(a.fixtures[i].toJson(), b.fixtures[i].toJson());
    // ...and IDEMPOTENT: re-siting the same story does not accumulate hearths.
    EXPECT_EQ(HearthForge::siteIntoProgram(a, {}, 3, 2, {}, ""), 1);
    EXPECT_EQ(a.fixtures.size(), b.fixtures.size());
}

// A stack rising from a lower story RESERVES its column, so nothing upstairs is
// sited inside the chimney breast.
TEST(HearthForgeTest, StackColumnIsReservedForTheStoryAbove) {
    FurniturePlacer::clearRecipes();
    ProgStory ground = hallStory(12, 8);
    ASSERT_EQ(HearthForge::siteIntoProgram(ground, {}, 3, 2, {}, ""), 1);

    ProgRoom rm;
    rm.id = "hall"; rm.purpose = "hall"; rm.rect = Rect{0, 0, 12, 8};
    const Rect footprint{0, 0, 12, 8};
    const auto pose = HearthForge::poseOf(ground.fixtures[0], rm.rect, footprint, 3, 2);
    ASSERT_GT(pose.stackCubes.w, 0);

    ProgStory upper = hallStory(12, 8);
    ASSERT_EQ(HearthForge::siteIntoProgram(upper, {}, 3, 2, {pose.stackCubes}, ""), 1);
    const Rect& up = upper.fixtures[0].rect;
    const bool overlaps = up.x < pose.stackCubes.x1() && pose.stackCubes.x < up.x1() &&
                          up.z < pose.stackCubes.z1() && pose.stackCubes.z < up.z1();
    EXPECT_FALSE(overlaps) << "the upstairs hearth was sited inside the stack from below";
}

// ============================================================================
// docs/VoxelFoundationPlan.md §7a.3 step 4 — the hearth bodies on the 1/8 m
// foundation grid, re-picked from the canon METRES (not a rescale of ninths):
//   * width/depth within the canon tolerance AND the nearest 1/8 m value;
//   * the same cube footprint as the legacy body, so siting is unchanged;
//   * the stack fits on the body; the ridge clearance is the smallest whole
//     number of cells that still clears 2 ft (IRC R1003.9) on each grid.
// ============================================================================
TEST(HearthForgeTest, FoundationBodiesArePickedFromTheCanonMetres) {
    const StructureGrid g8 = StructureGrid::foundation();
    struct Case { const char* type; const char* archetype; };
    for (const Case& c : {Case{"fireplace", "hearth"},
                          Case{"forge_hearth", "forge_hearth"},
                          Case{"oven_bread", "oven_bread"}}) {
        double cw = 0, ch = 0, cd = 0, tol = 0.2;
        if (!canonExtents(c.archetype, cw, ch, cd, tol))
            GTEST_SKIP() << "resources/object_dimensions.json not reachable - grounding NOT checked";
        const auto b = HearthForge::bodyOf(c.type, g8);
        ASSERT_TRUE(b.known) << c.type << " has no 1/8 m body";
        EXPECT_NEAR(b.w / 8.0, cw, tol) << c.type;
        EXPECT_NEAR(b.d / 8.0, cd, tol) << c.type;
        EXPECT_LE(std::abs(b.w - cw * 8), 0.5) << c.type << " width not the nearest eighth of " << cw;
        EXPECT_LE(std::abs(b.d - cd * 8), 0.5) << c.type << " depth not the nearest eighth of " << cd;
        EXPECT_GE(b.flueCx - HearthForge::kStackHalfMicro, 0) << c.type;
        EXPECT_LT(b.flueCx + HearthForge::kStackHalfMicro, b.w) << c.type;
        EXPECT_GE(b.flueCz - HearthForge::kStackHalfMicro, 0) << c.type;
        EXPECT_LT(b.flueCz + HearthForge::kStackHalfMicro, b.d) << c.type;
        const auto fp8 = HearthForge::footprintOf(c.type, g8);
        const auto fp9 = HearthForge::footprintOf(c.type);
        EXPECT_EQ(fp8.width, fp9.width) << c.type << " cube footprint changed with the grid";
        EXPECT_EQ(fp8.depth, fp9.depth) << c.type << " cube footprint changed with the grid";
        EXPECT_EQ(fp8.microW, b.w - 1) << c.type;
        EXPECT_EQ(fp8.microD, b.d - 1) << c.type;
    }
    // The fire sits at the canon working height: the forge block's top SURFACE (deckY is its top
    // course) and the oven's hearth floor (deckY is the coal row lying on it), both 0.8 m.
    EXPECT_NEAR((HearthForge::bodyOf("forge_hearth", g8).deckY + 1) / 8.0, 0.8, 1.0 / 16 + 1e-9);
    EXPECT_NEAR(HearthForge::bodyOf("oven_bread", g8).deckY / 8.0, 0.8, 1.0 / 16 + 1e-9);
    // Legacy bodies unchanged by the parameter.
    EXPECT_EQ(HearthForge::bodyOf("fireplace").w, 14);
    EXPECT_EQ(HearthForge::bodyOf("fireplace", StructureGrid::legacy()).h, 11);
}

TEST(HearthForgeTest, RidgeClearanceIsTheSmallestCountClearingTwoFeet) {
    const double twoFeet = 0.6096;
    for (const StructureGrid g : {StructureGrid::legacy(), StructureGrid::foundation()}) {
        const int n = HearthForge::ridgeClearance(g);
        EXPECT_GE(g.toM(n), twoFeet) << g.perM;
        EXPECT_LT(g.toM(n - 1), twoFeet) << g.perM;
    }
    EXPECT_EQ(HearthForge::ridgeClearance(StructureGrid::legacy()), HearthForge::kRidgeClearanceMicro);
}

// Flue area against the fireplace opening on both grids (IRC R1003.15.1: >= 1/10 of the opening),
// and the firebox keeps at least its legacy clear depth (0.444 m at 1/9; 0.5 m at 1/8).
TEST(HearthForgeTest, FlueAreaServesTheFireboxOpeningOnBothGrids) {
    for (const StructureGrid g : {StructureGrid::legacy(), StructureGrid::foundation()}) {
        const auto b = HearthForge::bodyOf("fireplace", g);
        const double openW = g.toM(b.w - 2 * b.sideW);
        const double openH = g.toM(b.h - b.baseT - b.topT);
        const double flue = g.toM(2 * HearthForge::kFlueHalfMicro + 1);
        EXPECT_GE(flue * flue, 0.1 * openW * openH) << g.perM;
        EXPECT_GE(g.toM(b.d - 1), g.isLegacy() ? 0.44 : 0.5) << g.perM << " firebox depth";
    }
}
