#include <gtest/gtest.h>

#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"
#include "core/BuildingProgram.h"

using namespace Phyxel::Core;

// ============================================================================
// finish_forge P2.5 Ã¢â‚¬â€ ROOF SLOPE RESOLUTION (docs/structure-generation/FinishDetailPlan.md)
//
// The gable pass honors the grounded pitch_deg but rasterizes it horizontally
// CUBE-quantized: the slope advances one full cube (1 m) of run per step, rising
// `pitch` subcubes each time. At thatch 50 deg that is a 1.33 m rise per 1 m
// tread Ã¢â‚¬â€ metre-wide stair-steps on the most visible plane of every building.
//
// P2.5 invariant, measured on the REAL canvas: the roof's top surface must be
// micro-stepped Ã¢â‚¬â€ walking the slope axis one micro column at a time, adjacent
// columns' surface height may differ by at most 2 micro. (A 50 deg plane rises
// ~1.2 micro per micro of run; the cube-stepped roof jumps pitch*3 = 12 micro
// at every cube boundary.)
//
// RED-BEFORE-GREEN: written against the cube-stepped rasterizer, where the step
// metric fails (max adjacent delta = 12) and only coverage holds.
// ============================================================================

namespace {

// 7x9 cottage: slope runs along X (span 7 cubes), ridge along Z, gables at the
// Z ends. Thatch roof at the grounded 50 deg pitch so the surface metric can
// filter roof cells by material.
const char* kThatchCottage = R"({
    "name": "cottage", "style": "thatch_test", "footprint": [7, 9],
    "substructure": "crawlspace", "roof_style": "gable",
    "stories": [{
        "height": 3,
        "rooms": [ { "id": "hall", "rect": [0,0,7,9], "purpose": "living" } ],
        "portals": [
            { "between": ["exterior","hall"], "pos": [0,4], "width": 1, "height": 2, "kind": "door" }
        ]
    }]
})";

StyleProfile thatchStyle() {
    StyleProfileRegistry reg;
    reg.loadFromJson(nlohmann::json::parse(R"({
        "thatch_test": {
            "roof_style": "gable", "foundation": "crawlspace",
            "thickness": { "exterior_wall": 0.222, "interior_wall": 0.111,
                           "foundation_wall": 0.444, "floor": 0.333, "ceiling": 0.111 },
            "materials": { "structure": "Wood", "floor": "Wood", "roof": "Thatch", "foundation": "Stone" },
            "roof": { "pitch_deg": 50.0 }
        }
    })"));
    return *reg.get("thatch_test");
}

StructureRealizer::ShellResult build() {
    return StructureRealizer::realizeShell(
        BuildingProgram::fromJson(nlohmann::json::parse(kThatchCottage)),
        thatchStyle());
}

// Highest micro y whose cell at (x, z) holds the roof material, or -1 if none.
int roofTopAt(const MicroCanvas& c, int x, int z, int yHi, const char* mat = "Thatch") {
    for (int y = yHi; y >= 0; --y)
        if (c.materialAt(x, y, z) == mat) return y;
    return -1;
}

// 9x6 stone manor wing with a HIP roof: the surface must slope up from ALL FOUR
// eaves (height ~ distance to the nearest footprint edge), forming a ridge
// segment along the long (X) axis. ClayTile at the grounded 40 deg pitch.
const char* kHipManor = R"({
    "name": "manor_wing", "style": "hip_test", "footprint": [9, 6],
    "substructure": "slab", "roof_style": "hip",
    "stories": [{
        "height": 3,
        "rooms": [ { "id": "hall", "rect": [0,0,9,6], "purpose": "living" } ],
        "portals": [
            { "between": ["exterior","hall"], "pos": [0,3], "width": 1, "height": 2, "kind": "door" }
        ]
    }]
})";

StyleProfile hipStyle() {
    StyleProfileRegistry reg;
    reg.loadFromJson(nlohmann::json::parse(R"({
        "hip_test": {
            "roof_style": "hip", "foundation": "slab",
            "thickness": { "exterior_wall": 0.667, "interior_wall": 0.333,
                           "foundation_wall": 0.667, "floor": 0.333, "ceiling": 0.333 },
            "materials": { "structure": "StoneBricks", "floor": "Wood", "roof": "ClayTile", "foundation": "Stone" },
            "roof": { "pitch_deg": 40.0 }
        }
    })"));
    return *reg.get("hip_test");
}

StructureRealizer::ShellResult buildHip() {
    return StructureRealizer::realizeShell(
        BuildingProgram::fromJson(nlohmann::json::parse(kHipManor)),
        hipStyle());
}

} // namespace

// The slope surface must be micro-stepped, not cube-stepped: along the slope
// axis (X here), adjacent micro columns' roof-surface heights differ by <= 2
// micro. RED today: the cube-quantized rasterizer jumps pitch*3 = 12 micro at
// every cube boundary (50 deg -> pitch 4 subcubes per cube of run).
TEST(RoofForgeTest, SlopeSurfaceIsMicroStepped) {
    auto r = build();
    ASSERT_TRUE(r.ok) << r.error;
    const auto& c = r.canvas;
    glm::ivec3 lo, hi;
    ASSERT_TRUE(c.microBounds(lo, hi));

    // Mid-ridge slice: z at the centre of the 9-cube depth, away from the gable
    // ends. Footprint is 7 cubes = 63 micro columns in X.
    const int z = 4 * 9 + 4;
    int maxStep = 0, atX = -1;
    int prev = roofTopAt(c, 0, z, hi.y);
    ASSERT_GE(prev, 0) << "no roof surface at x=0";
    for (int x = 1; x < 7 * 9; ++x) {
        const int top = roofTopAt(c, x, z, hi.y);
        ASSERT_GE(top, 0) << "roof surface has a hole at x=" << x;
        const int step = std::abs(top - prev);
        if (step > maxStep) { maxStep = step; atX = x; }
        prev = top;
    }
    EXPECT_LE(maxStep, 2)
        << "roof slope is cube-stepped: adjacent surface columns jump " << maxStep
        << " micro at x=" << atX << " (a 50 deg plane should rise <=2 micro per micro of run)";
}

// Coverage guard (GREEN before and after): every micro column of the footprint
// carries roof material at the mid-ridge slice Ã¢â‚¬â€ smoothing the slope must not
// open holes in the surface.
TEST(RoofForgeTest, SlopeSurfaceCoversTheFootprint) {
    auto r = build();
    ASSERT_TRUE(r.ok) << r.error;
    const auto& c = r.canvas;
    glm::ivec3 lo, hi;
    ASSERT_TRUE(c.microBounds(lo, hi));

    const int z = 4 * 9 + 4;
    for (int x = 0; x < 7 * 9; ++x)
        EXPECT_GE(roofTopAt(c, x, z, hi.y), 0) << "no roof cell in column x=" << x;
}

// Eave-flush guard (GREEN before and after): the roof's lowest course still
// rests directly ON the wall top at the eave Ã¢â‚¬â€ micro-stepping must not
// reintroduce the 1-micro hover (V1 checkRoofEaveFlush).
TEST(RoofForgeTest, EaveStaysFlushOnTheWallTop) {
    auto r = build();
    ASSERT_TRUE(r.ok) << r.error;
    const auto& c = r.canvas;
    glm::ivec3 lo, hi;
    ASSERT_TRUE(c.microBounds(lo, hi));

    // Find the eave course: the lowest Thatch cell over the westmost column.
    const int z = 4 * 9 + 4;
    int eaveY = -1;
    for (int y = 0; y <= hi.y && eaveY < 0; ++y)
        if (c.materialAt(0, y, z) == "Thatch") eaveY = y;
    ASSERT_GE(eaveY, 0);

    // The cell directly below the eave must be occupied (wall top / ceiling
    // edge), not air: no hover gap.
    EXPECT_TRUE(c.occupiedMicro(0, eaveY - 1, z) || c.occupiedMicro(1, eaveY - 1, z))
        << "eave hovers: air directly under the roof's lowest course";
}

// A HIP roof slopes up from ALL FOUR eaves toward the ridge Ã¢â‚¬â€ no vertical gable
// triangle, no flat cap. RED today: "hip" is unimplemented and silently falls
// back to a FLAT roof cap, so the surface height at the centre equals the height
// at every edge.
TEST(RoofForgeTest, HipRoofSlopesUpFromAllFourEaves) {
    auto r = buildHip();
    ASSERT_TRUE(r.ok) << r.error;
    const auto& c = r.canvas;
    glm::ivec3 lo, hi;
    ASSERT_TRUE(c.microBounds(lo, hi));

    // Footprint 9x6 cubes = 81x54 micro. Sample the roof surface at the centre
    // and just inside each of the four eaves (2 micro in from the edge).
    const int cx = 40, cz = 27;
    const int centre = roofTopAt(c, cx, cz, hi.y, "ClayTile");
    ASSERT_GE(centre, 0) << "no roof surface at the footprint centre";
    const int west  = roofTopAt(c, 2,  cz, hi.y, "ClayTile");
    const int east  = roofTopAt(c, 78, cz, hi.y, "ClayTile");
    const int south = roofTopAt(c, cx, 2,  hi.y, "ClayTile");
    const int north = roofTopAt(c, cx, 51, hi.y, "ClayTile");
    ASSERT_GE(west, 0);  ASSERT_GE(east, 0);
    ASSERT_GE(south, 0); ASSERT_GE(north, 0);

    // 40 deg (pitch 3 subcubes/cube = 1 micro per micro of run): 2 micro in from
    // the shorter half-span (27 micro) the centre must sit well above every eave.
    EXPECT_GT(centre, west)  << "hip roof is flat along +x: west eave as high as the centre";
    EXPECT_GT(centre, east)  << "hip roof is flat along -x: east eave as high as the centre";
    EXPECT_GT(centre, south) << "hip roof is flat along +z: south eave as high as the centre";
    EXPECT_GT(centre, north) << "hip roof is flat along -z: north eave as high as the centre";
}

// The hip surface must be micro-stepped along BOTH axes (same invariant as the
// gable slope). GREEN on the flat fallback (steps of 0) and GREEN after Ã¢â‚¬â€ the
// slope-exists test above carries the red; this pins smoothness once hip lands.
TEST(RoofForgeTest, HipSlopeSurfaceIsMicroStepped) {
    auto r = buildHip();
    ASSERT_TRUE(r.ok) << r.error;
    const auto& c = r.canvas;
    glm::ivec3 lo, hi;
    ASSERT_TRUE(c.microBounds(lo, hi));

    int maxStep = 0;
    int prev = roofTopAt(c, 0, 27, hi.y, "ClayTile");
    ASSERT_GE(prev, 0);
    for (int x = 1; x < 81; ++x) {
        const int top = roofTopAt(c, x, 27, hi.y, "ClayTile");
        ASSERT_GE(top, 0) << "roof hole at x=" << x;
        maxStep = std::max(maxStep, std::abs(top - prev));
        prev = top;
    }
    prev = roofTopAt(c, 40, 0, hi.y, "ClayTile");
    ASSERT_GE(prev, 0);
    for (int z = 1; z < 54; ++z) {
        const int top = roofTopAt(c, 40, z, hi.y, "ClayTile");
        ASSERT_GE(top, 0) << "roof hole at z=" << z;
        maxStep = std::max(maxStep, std::abs(top - prev));
        prev = top;
    }
    EXPECT_LE(maxStep, 2) << "hip slope is coarser than micro-stepped";
}

// Eave-flush guard for the hip: the lowest roof course rests directly on the
// wall top on every side (GREEN on the flat fallback and after).
TEST(RoofForgeTest, HipEaveStaysFlushOnTheWallTop) {
    auto r = buildHip();
    ASSERT_TRUE(r.ok) << r.error;
    const auto& c = r.canvas;
    glm::ivec3 lo, hi;
    ASSERT_TRUE(c.microBounds(lo, hi));

    int eaveY = -1;
    for (int y = 0; y <= hi.y && eaveY < 0; ++y)
        if (c.materialAt(0, y, 27) == "ClayTile") eaveY = y;
    ASSERT_GE(eaveY, 0);
    EXPECT_TRUE(c.occupiedMicro(0, eaveY - 1, 27) || c.occupiedMicro(1, eaveY - 1, 27))
        << "hip eave hovers: air directly under the roof's lowest course";
}

// ---------------------------------------------------------------------------
// EAVE OVERHANG (P2.5 remainder): styles carry a grounded-with-caveat
// roof.overhang (0.4 m; modern-analog 300-450 mm range per TrimGrounding Ã¢â‚¬â€
// the vernacular figure is NEEDS-RESEARCH, so the value ships FLAGGED in the
// style sources). The rasterizer must extend the slope plane past the eave
// walls so the roof visibly shelters them. RED today: the roof stops dead at
// the footprint edge Ã¢â‚¬â€ nothing exists outside it at eave level.
// ---------------------------------------------------------------------------

namespace {
StyleProfile overhangStyle() {
    StyleProfileRegistry reg;
    reg.loadFromJson(nlohmann::json::parse(R"({
        "overhang_test": {
            "roof_style": "gable", "foundation": "crawlspace",
            "thickness": { "exterior_wall": 0.222, "interior_wall": 0.111,
                           "foundation_wall": 0.444, "floor": 0.333, "ceiling": 0.111 },
            "materials": { "structure": "Wood", "floor": "Wood", "roof": "Thatch", "foundation": "Stone" },
            "roof": { "pitch_deg": 50.0, "overhang": 0.4 }
        }
    })"));
    return *reg.get("overhang_test");
}
} // namespace

// Roof cells exist OUTSIDE the footprint at both eave sides (x < 0 and x >= 63
// for the 7-cube span), continuing the slope plane downward past the wall.
TEST(RoofForgeTest, EavesOverhangTheWalls) {
    auto r = StructureRealizer::realizeShell(
        BuildingProgram::fromJson(nlohmann::json::parse(kThatchCottage)), overhangStyle());
    ASSERT_TRUE(r.ok) << r.error;
    const auto& c = r.canvas;
    glm::ivec3 lo, hi;
    ASSERT_TRUE(c.microBounds(lo, hi));

    const int z = 4 * 9 + 4;
    bool west = false, east = false;
    for (int y = 0; y <= hi.y; ++y) {
        if (roofTopAt(c, -1, z, hi.y) >= 0) west = true;
        if (roofTopAt(c, 63, z, hi.y) >= 0) east = true;
        break;   // roofTopAt already scans y; one call per side suffices
    }
    EXPECT_TRUE(west) << "no roof overhang past the west eave wall";
    EXPECT_TRUE(east) << "no roof overhang past the east eave wall";
}

// The overhang keeps micro-stepping (the slope metric holds across the
// extended span) and the eave-flush guard still holds AT the wall plane.
TEST(RoofForgeTest, OverhangContinuesTheSlopePlane) {
    auto r = StructureRealizer::realizeShell(
        BuildingProgram::fromJson(nlohmann::json::parse(kThatchCottage)), overhangStyle());
    ASSERT_TRUE(r.ok) << r.error;
    const auto& c = r.canvas;
    glm::ivec3 lo, hi;
    ASSERT_TRUE(c.microBounds(lo, hi));

    const int z = 4 * 9 + 4;
    // Walk from the westmost overhang cell across the wall line: adjacent
    // surface steps stay <= 2 micro (one continuous plane, no cliff at x=0).
    int prev = -1, maxStep = 0;
    for (int x = -4; x < 8; ++x) {
        const int top = roofTopAt(c, x, z, hi.y);
        if (top < 0) continue;                 // outside the actual overhang reach
        if (prev >= 0) maxStep = std::max(maxStep, std::abs(top - prev));
        prev = top;
    }
    ASSERT_GE(prev, 0);
    EXPECT_LE(maxStep, 2) << "overhang breaks the slope plane at the wall line";
}

// ---------------------------------------------------------------------------
// P2.6 Ã¢â‚¬â€ L-PLAN ROOFS (FinishDetailPlan): a non-rectangular footprint currently
// falls back to a FLAT CAP. The authentic mechanism (grounded direction: hall +
// cross-wing composition) is TWO intersecting gabled ranges Ã¢â‚¬â€ the main range and
// the wing each carry their own ridge, meeting in a valley. RED: flat cap = no
// slope anywhere.
// ---------------------------------------------------------------------------

namespace {
// L-plan: main range 9x4 (rooms hall+service) + wing 4x4 under the hall ->
// footprint = 9x8 minus the notch x[4,9) z[4,8).
const char* kLPlanHouse = R"({
    "name": "lplan", "style": "thatch_test", "footprint": [9, 8],
    "substructure": "slab", "roof_style": "gable",
    "stories": [{
        "height": 3,
        "rooms": [
            { "id": "hall",    "rect": [0,0,4,4], "purpose": "hall" },
            { "id": "service", "rect": [4,0,5,4], "purpose": "service" },
            { "id": "solar",   "rect": [0,4,4,4], "purpose": "solar" }
        ],
        "portals": [
            { "between": ["exterior","service"], "pos": [6,0], "width": 1, "height": 2, "kind": "door" },
            { "between": ["hall","service"], "pos": [4,2], "width": 1, "height": 2, "kind": "door" },
            { "between": ["hall","solar"],   "pos": [2,4], "width": 1, "height": 2, "kind": "door" }
        ]
    }]
})";

StructureRealizer::ShellResult buildL() {
    return StructureRealizer::realizeShell(
        BuildingProgram::fromJson(nlohmann::json::parse(kLPlanHouse)), thatchStyle());
}
} // namespace

// The MAIN range carries a real ridge: its surface at mid-depth stands well
// above its eave. RED today: the flat cap is dead level.
TEST(RoofForgeTest, LPlanMainRangeHasARidge) {
    auto r = buildL();
    ASSERT_TRUE(r.ok) << r.error;
    const auto& c = r.canvas;
    glm::ivec3 lo, hi;
    ASSERT_TRUE(c.microBounds(lo, hi));
    // x=58: inside the main range only (past the wing's x span). Slope runs
    // along Z (span 4 cubes): eave at z~2, ridge at z~18.
    const int eave  = roofTopAt(c, 58, 2, hi.y);
    const int ridge = roofTopAt(c, 58, 18, hi.y);
    ASSERT_GE(eave, 0);  ASSERT_GE(ridge, 0);
    EXPECT_GE(ridge - eave, 12)
        << "main range is flat (ridge " << ridge << " vs eave " << eave << ") Ã¢â‚¬â€ the L-plan flat cap";
}

// The WING carries its own perpendicular ridge. RED today (flat).
TEST(RoofForgeTest, LPlanWingHasAPerpendicularRidge) {
    auto r = buildL();
    ASSERT_TRUE(r.ok) << r.error;
    const auto& c = r.canvas;
    glm::ivec3 lo, hi;
    ASSERT_TRUE(c.microBounds(lo, hi));
    // z=54: inside the wing only (past the main range's depth). Wing slope runs
    // along X (span 4 cubes): eave at x~2, ridge at x~18.
    const int eave  = roofTopAt(c, 2, 54, hi.y);
    const int ridge = roofTopAt(c, 18, 54, hi.y);
    ASSERT_GE(eave, 0);  ASSERT_GE(ridge, 0);
    EXPECT_GE(ridge - eave, 12)
        << "wing is flat (ridge " << ridge << " vs eave " << eave << ")";
}

// Coverage + notch guards (GREEN before and after): every footprint column
// carries roof; the notch centre Ã¢â‚¬â€ far from any wall Ã¢â‚¬â€ stays open sky.
TEST(RoofForgeTest, LPlanRoofCoversTheFootprintNotTheNotch) {
    auto r = buildL();
    ASSERT_TRUE(r.ok) << r.error;
    const auto& c = r.canvas;
    glm::ivec3 lo, hi;
    ASSERT_TRUE(c.microBounds(lo, hi));
    // Sample interior columns of both ranges + the junction line (watertight).
    for (int x : {2, 18, 34, 40, 58, 76})
        EXPECT_GE(roofTopAt(c, x, 18, hi.y), 0) << "roof hole in the main range at x=" << x;
    for (int z : {2, 18, 34, 40, 58, 68})
        EXPECT_GE(roofTopAt(c, 18, z, hi.y), 0) << "roof hole down the wing at z=" << z;
    // Notch centre stays clear.
    EXPECT_LT(roofTopAt(c, 58, 54, hi.y), 0) << "roof spills over the notch centre";
    // The plan records a real roof style for the composed L, not the flat fallback
    // (auditor gap: this field was untested).
    ASSERT_FALSE(r.plan.roof.empty());
    EXPECT_EQ(r.plan.roof[0].style, "gable") << "L-plan roof reported as flat in the plan";
}
