#include <gtest/gtest.h>

#include "core/RealizedWorldValidator.h"

using namespace Phyxel::Core;

// ============================================================================
// Geometric world-placement detectors — proven to FIRE on real defects (the current world's
// bed/chest coincidence and bench/fireplace overlap) with a TEETH pair so each measures real geometry.
// ============================================================================

namespace {
PlacedBox box(const std::string& id, const std::string& type,
              glm::ivec3 mn, glm::ivec3 mx, const std::string& parent = "s") {
    PlacedBox b; b.id = id; b.type = type; b.parent = parent; b.min = mn; b.max = mx; return b;
}
} // namespace

// ---- V5 furniture overlap --------------------------------------------------
// TEETH: two separated fixtures pass; two that share a cell fire.
TEST(RealizedWorldValidatorTest, OverlapDetectorHasTeeth) {
    // separated bed and chest — no overlap
    std::vector<PlacedBox> clear = {
        box("bed_1", "bed",   {0, 17, 0}, {1, 17, 1}),
        box("chest_1", "chest", {4, 17, 0}, {4, 17, 1}),
    };
    EXPECT_TRUE(RealizedWorldValidator::checkFurnitureOverlaps(clear).ok())
        << "separated fixtures wrongly flagged";

    // bed and chest at the identical bbox (the real house_3/4/5 defect) — must fire
    std::vector<PlacedBox> coincident = {
        box("bed_2", "bed",   {-13, 17, 38}, {-12, 17, 38}),
        box("chest_2", "chest", {-13, 17, 38}, {-12, 17, 38}),
    };
    EXPECT_FALSE(RealizedWorldValidator::checkFurnitureOverlaps(coincident).ok())
        << "coincident bed+chest NOT detected";
}

// Per-building gate: coincident fixtures in DIFFERENT structures are not a real overlap (adjacent
// buildings can share a wall cell); the SAME two in one structure fire.
TEST(RealizedWorldValidatorTest, CrossBuildingNotFlaggedSameBuildingIs) {
    std::vector<PlacedBox> crossBuilding = {
        box("bed_a", "bed",   {0, 17, 0}, {1, 17, 1}, "house_A"),
        box("chest_a", "chest", {0, 17, 0}, {1, 17, 1}, "house_B"),   // coincident, but other building
    };
    EXPECT_TRUE(RealizedWorldValidator::checkFurnitureOverlaps(crossBuilding).ok())
        << "coincident fixtures in different buildings wrongly flagged";

    std::vector<PlacedBox> sameBuilding = {
        box("bed_b", "bed",   {0, 17, 0}, {1, 17, 1}, "house_A"),
        box("chest_b", "chest", {0, 17, 0}, {1, 17, 1}, "house_A"),   // same building -> real overlap
    };
    EXPECT_FALSE(RealizedWorldValidator::checkFurnitureOverlaps(sameBuilding).ok())
        << "coincident fixtures in the SAME building NOT flagged";
}

// A hearth overlap is reported distinctly (the user's "furniture overlaps fireplaces").
TEST(RealizedWorldValidatorTest, FireplaceOverlapReportedDistinctly) {
    // the real house_4 case: bench_wood_4 (48,15,1)-(48,15,2) ∩ fireplace_4 (47,15,1)-(48,16,1)
    std::vector<PlacedBox> items = {
        box("fireplace_4", "fireplace", {47, 15, 1}, {48, 16, 1}),
        box("bench_wood_4", "bench",     {48, 15, 1}, {48, 15, 2}),
    };
    auto rep = RealizedWorldValidator::checkFurnitureOverlaps(items);
    EXPECT_FALSE(rep.ok()) << "bench overlapping the hearth NOT detected";
    ASSERT_FALSE(rep.issues().empty());
    EXPECT_EQ(rep.issues().front().code, "furniture_on_fireplace")
        << "hearth overlap not classified as furniture_on_fireplace";
}

// ---- V10 grass under house -------------------------------------------------
// TEETH: a footprint with a Grass cube below the floor fires; one over cleared Dirt passes.
TEST(RealizedWorldValidatorTest, GrassUnderHouseDetectorHasTeeth) {
    // floor at y=17; the real house_2 case has Grass at y=16 across the footprint, Dirt below.
    std::vector<FootprintScan> structs = { {"house_2", {1, 17, 19}, {8, 20, 24}} };
    auto grassy = [](int, int y, int) -> std::string {
        return y == 16 ? std::string("Grass") : std::string("Dirt");
    };
    EXPECT_FALSE(RealizedWorldValidator::checkGrassUnderFootprint(structs, grassy).ok())
        << "grass under the floor NOT detected";

    // cleared: all Dirt below the floor -> passes
    auto cleared = [](int, int, int) -> std::string { return "Dirt"; };
    EXPECT_TRUE(RealizedWorldValidator::checkGrassUnderFootprint(structs, cleared).ok())
        << "a cleared footprint wrongly flagged";
}

// ---- V8 chimney over hearth ------------------------------------------------
// TEETH, calibrated on the real defect: hearth at x=1 (bbox), chimney Stone one cube over at x=2.
TEST(RealizedWorldValidatorTest, ChimneyOverHearthDetectorHasTeeth) {
    // fireplace_2's real bbox: (1,17,19)-(1,18,20); hearth top y=18.
    std::vector<PlacedBox> fp = { box("fireplace_2", "fireplace", {1, 17, 19}, {1, 18, 20}) };

    // OFFSET (the real defect): Stone rises at x=2 (outside footprint x=1), none above x=1 -> fires.
    auto offset = [](int x, int y, int z, const std::string& m) {
        return m == "Stone" && x == 2 && z == 19 && y >= 19 && y <= 21;
    };
    auto rOff = RealizedWorldValidator::checkChimneyOverHearth(fp, offset);
    EXPECT_FALSE(rOff.ok()) << "offset chimney NOT detected";
    ASSERT_FALSE(rOff.issues().empty());
    EXPECT_EQ(rOff.issues().front().code, "chimney_offset_from_hearth");

    // SEATED: Stone rises directly above the hearth footprint (x=1) -> passes.
    auto seated = [](int x, int y, int z, const std::string& m) {
        return m == "Stone" && x == 1 && z == 19 && y >= 19 && y <= 21;
    };
    EXPECT_TRUE(RealizedWorldValidator::checkChimneyOverHearth(fp, seated).ok())
        << "a correctly seated chimney was wrongly flagged";

    // MISSING: no Stone rises above the hearth at all -> fires (distinctly).
    auto none = [](int, int, int, const std::string&) { return false; };
    auto rMiss = RealizedWorldValidator::checkChimneyOverHearth(fp, none);
    EXPECT_FALSE(rMiss.ok()) << "missing chimney NOT detected";
    EXPECT_EQ(rMiss.issues().front().code, "chimney_missing");

    // BRICKS-OFFSET: chimneys are now Bricks; an offset Bricks stack must still fire as offset (guards
    // against the Bricks-broadening being applied to the seated branch only).
    auto bricksOffset = [](int x, int y, int z, const std::string& m) {
        return m == "Bricks" && x == 2 && z == 19 && y >= 19 && y <= 21;
    };
    auto rBrOff = RealizedWorldValidator::checkChimneyOverHearth(fp, bricksOffset);
    EXPECT_FALSE(rBrOff.ok()) << "Bricks-offset chimney NOT detected";
    EXPECT_EQ(rBrOff.issues().front().code, "chimney_offset_from_hearth");

    // BRICKS-SEATED: a Bricks stack directly over the hearth footprint passes.
    auto bricksSeated = [](int x, int y, int z, const std::string& m) {
        return m == "Bricks" && x == 1 && z == 19 && y >= 19 && y <= 21;
    };
    EXPECT_TRUE(RealizedWorldValidator::checkChimneyOverHearth(fp, bricksSeated).ok())
        << "a Bricks chimney seated on the hearth was wrongly flagged";
}

// ---- V7 path under house ---------------------------------------------------
// TEETH: a Cobblestone cell in the footprint INTERIOR fires; cobblestone only at the perimeter
// (a path meeting the door) passes; no cobblestone passes.
TEST(RealizedWorldValidatorTest, PathUnderHouseDetectorHasTeeth) {
    std::vector<FootprintScan> structs = { {"house_2", {1, 17, 19}, {8, 20, 24}} };

    // INTERIOR path (the real defect): Cobblestone at (4,17,21) — inside the inset region -> fires.
    auto through = [](int x, int y, int z, const std::string& m) {
        return m == "Cobblestone" && x == 4 && z == 21 && y == 17;
    };
    EXPECT_FALSE(RealizedWorldValidator::checkPathUnderFootprint(structs, through).ok())
        << "interior through-path NOT detected";

    // PERIMETER only: Cobblestone at the wall line x=1 (a doorstep) -> not flagged.
    auto doorstep = [](int x, int y, int z, const std::string& m) {
        return m == "Cobblestone" && x == 1 && y == 17;   // x=1 is the footprint edge (not interior)
    };
    EXPECT_TRUE(RealizedWorldValidator::checkPathUnderFootprint(structs, doorstep).ok())
        << "a perimeter doorstep path wrongly flagged as under-house";

    auto none = [](int, int, int, const std::string&) { return false; };
    EXPECT_TRUE(RealizedWorldValidator::checkPathUnderFootprint(structs, none).ok());
}

// ---- V3 yard flatness ------------------------------------------------------
// TEETH: a sloped yard (height varies across the ring) fires; a flat yard passes.
TEST(RealizedWorldValidatorTest, YardFlatnessDetectorHasTeeth) {
    std::vector<FootprintScan> structs = { {"house_5", {33, 16, 18}, {42, 19, 25}} };

    // SLOPED (the real house_5 case): surface 14 on the low side, 18 on the high side -> span 4 -> fires.
    auto sloped = [](int x, int) -> int { return x < 38 ? 14 : 18; };
    EXPECT_FALSE(RealizedWorldValidator::checkYardFlatness(structs, sloped).ok())
        << "a sloped yard was NOT detected";

    // FLAT: constant surface height -> span 0 -> passes.
    auto flat = [](int, int) -> int { return 16; };
    EXPECT_TRUE(RealizedWorldValidator::checkYardFlatness(structs, flat).ok())
        << "a flat yard wrongly flagged";
}

TEST(RealizedWorldValidatorTest, FloorFlushDetectorHasTeeth) {
    // floor at y=17 (bbox min.y)
    std::vector<FootprintScan> structs = { {"house_x", {4, 17, -2}, {11, 20, 3}} };

    // STEP: yard sits at y=15, floor at 17 -> step 2 (>flushTol 1) -> fires (you step up to enter).
    auto stepped = [](int, int) -> int { return 15; };
    EXPECT_FALSE(RealizedWorldValidator::checkFloorFlush(structs, stepped).ok())
        << "a floor sitting above the yard was NOT detected";

    // FLUSH: yard level with the floor (y=17) -> step 0 -> passes.
    auto flush = [](int, int) -> int { return 17; };
    EXPECT_TRUE(RealizedWorldValidator::checkFloorFlush(structs, flush).ok())
        << "a flush floor was wrongly flagged";

    // 1-cube threshold is tolerated (not a real step).
    auto oneStep = [](int, int) -> int { return 16; };
    EXPECT_TRUE(RealizedWorldValidator::checkFloorFlush(structs, oneStep).ok())
        << "a 1-cube threshold should be within tolerance";
}

// ---- V6 fence corner overlap -----------------------------------------------
// TEETH, calibrated on the real (11,28) corner: straights ~48 Log micros, doubled corner 94.
TEST(RealizedWorldValidatorTest, FenceCornerOverlapDetectorHasTeeth) {
    // OVERLAP: an L corner whose post is ~2x the straight runs -> fires.
    std::vector<FencePost> overlap = {
        {{9, 17, 28}, 48}, {{10, 17, 28}, 48},     // south run
        {{11, 17, 26}, 48}, {{11, 17, 27}, 48},    // east run
        {{11, 17, 28}, 94},                        // CORNER (doubled)
    };
    auto rOv = RealizedWorldValidator::checkFenceCornerOverlaps(overlap);
    EXPECT_FALSE(rOv.ok()) << "doubled fence corner NOT detected";
    ASSERT_FALSE(rOv.issues().empty());
    EXPECT_EQ(rOv.issues().front().code, "fence_corner_overlap");

    // CLEAN: the corner shares one post (~same as straights) -> passes.
    std::vector<FencePost> clean = {
        {{9, 17, 28}, 48}, {{10, 17, 28}, 48},
        {{11, 17, 26}, 48}, {{11, 17, 27}, 48},
        {{11, 17, 28}, 48},
    };
    EXPECT_TRUE(RealizedWorldValidator::checkFenceCornerOverlaps(clean).ok())
        << "a clean shared-post corner wrongly flagged";

    // STRAIGHT run: even a heavy cell with no both-axis neighbour is not a corner -> passes.
    std::vector<FencePost> straight = {
        {{9, 17, 28}, 48}, {{10, 17, 28}, 99}, {{11, 17, 28}, 48},
    };
    EXPECT_TRUE(RealizedWorldValidator::checkFenceCornerOverlaps(straight).ok())
        << "a straight run wrongly flagged as a corner";
}

// ---- V1 fence over path ----------------------------------------------------
// TEETH, calibrated on the real (0,28) case: fence Log at y20 with a Cobblestone path at y19 below.
TEST(RealizedWorldValidatorTest, FenceOverPathDetectorHasTeeth) {
    std::vector<FencePost> posts = { {{0, 20, 28}, 48}, {{2, 18, 28}, 48} };
    // path Cobblestone only directly under the (0,28) post -> that one fires, the other doesn't.
    auto probe = [](int x, int y, int z, const std::string& m) {
        return m == "Cobblestone" && x == 0 && z == 28 && y == 19;
    };
    auto rep = RealizedWorldValidator::checkFenceOverPath(posts, probe);
    EXPECT_FALSE(rep.ok()) << "fence over path NOT detected";
    ASSERT_FALSE(rep.issues().empty());
    EXPECT_EQ(rep.issues().front().code, "fence_over_path");
    EXPECT_EQ(rep.issues().size(), 1u) << "the on-grass fence post was wrongly flagged";

    // no path below any post -> passes.
    auto none = [](int, int, int, const std::string&) { return false; };
    EXPECT_TRUE(RealizedWorldValidator::checkFenceOverPath(posts, none).ok());
}

// ---- V11 chest facing ------------------------------------------------------
// TEETH, calibrated on the real chest_closed_2 (center (4,17,22), rot 90) with the south wall at z>=24.
TEST(RealizedWorldValidatorTest, ChestFacingDetectorHasTeeth) {
    auto wallSouth = [](int, int, int z) { return z >= 24; };   // a wall band to the south (+Z)

    // rot 90 -> clasp faces -X, back faces +X (open); nearest wall is +Z -> mis-facing -> fires.
    std::vector<ChestPlacement> wrong = { {"chest_a", {4, 17, 22}, 90} };
    EXPECT_FALSE(RealizedWorldValidator::checkChestFacing(wrong, wallSouth).ok())
        << "mis-facing chest NOT detected";

    // rot 180 -> clasp faces -Z (into room), back faces +Z (onto the south wall) -> correct -> passes.
    std::vector<ChestPlacement> right = { {"chest_b", {4, 17, 22}, 180} };
    EXPECT_TRUE(RealizedWorldValidator::checkChestFacing(right, wallSouth).ok())
        << "a correctly wall-backed chest wrongly flagged";

    // no wall within reach -> open placement, cannot judge -> passes.
    auto noWall = [](int, int, int) { return false; };
    EXPECT_TRUE(RealizedWorldValidator::checkChestFacing(wrong, noWall).ok())
        << "an open-placed chest wrongly flagged";
}

// ---- V2 fence along a terrain cliff ----------------------------------------
// TEETH, calibrated on house_5's west fence (x=31 on ground ~16, x=32 cut to ~14 = a >=2 cliff).
TEST(RealizedWorldValidatorTest, FenceAgainstRiseDetectorHasTeeth) {
    std::vector<FencePost> posts = { {{31, 17, 20}, 48} };

    // CLIFF: terrain drops 2+ cubes just inside (x>=32 -> 14, else 16) -> fires.
    auto cliff = [](int x, int) -> int { return x >= 32 ? 14 : 16; };
    EXPECT_FALSE(RealizedWorldValidator::checkFenceAgainstRise(posts, cliff).ok())
        << "fence along a >=2 cube cliff NOT detected";

    // GENTLE 1-cube slope -> not a cliff -> passes.
    auto slope = [](int x, int) -> int { return x >= 32 ? 15 : 16; };
    EXPECT_TRUE(RealizedWorldValidator::checkFenceAgainstRise(posts, slope).ok())
        << "a gentle 1-cube slope wrongly flagged as a cliff";

    // FLAT -> passes.
    auto flat = [](int, int) -> int { return 16; };
    EXPECT_TRUE(RealizedWorldValidator::checkFenceAgainstRise(posts, flat).ok());
}

// Surface clutter (a mug on a table) is NOT flagged even if boxes touch — it belongs on the surface.
TEST(RealizedWorldValidatorTest, ClutterExcluded) {
    std::vector<PlacedBox> items = {
        box("table_1", "table", {7, 17, 1}, {8, 17, 1}),
        box("mug_1", "mug",     {7, 18, 1}, {7, 18, 1}),   // a cube up, but even if it overlapped:
        box("mug_2", "mug",     {7, 17, 1}, {7, 17, 1}),   // coincident with the table — still ignored
    };
    EXPECT_TRUE(RealizedWorldValidator::checkFurnitureOverlaps(items).ok())
        << "clutter wrongly flagged as an overlap";
}
