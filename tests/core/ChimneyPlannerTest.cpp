#include <gtest/gtest.h>

#include <climits>
#include <cmath>
#include <set>

#include "core/StructureGenerator.h"
#include "core/StructureRealizer.h"
#include "core/BuildingProgram.h"
#include "core/StyleProfile.h"

using namespace Phyxel::Core;

// ============================================================================
// place_chimney (#14) — the masonry stack must run a CONTINUOUS flue from the hearth UP THROUGH the
// roof, clearing the ridge for draught (3-2-10 rule). These pin StructureGenerator::planChimneyStack
// (the real output stamped into the world): F1 continuity (no flue gap = no smoke trap), F2 ridge
// clearance (with a teeth case proving the check measures), + the flue void / solid cap.
// ============================================================================

namespace {
// Global micro-Y of a microcube placement.
int gy(const VoxelPlacement& v) { return v.position.y * 9 + v.subcubePos.y * 3 + v.microcubePos.y; }
int gx(const VoxelPlacement& v) { return v.position.x * 9 + v.subcubePos.x * 3 + v.microcubePos.x; }
int gz(const VoxelPlacement& v) { return v.position.z * 9 + v.subcubePos.z * 3 + v.microcubePos.z; }
} // namespace

// F1 + F2: the stack is continuous from base to top and clears the roof apex.
TEST(ChimneyPlannerTest, StackIsContinuousAndClearsTheRidge) {
    const int base = 12, apex = 60, top = apex + 5;   // 5 micro (~0.56 m) above the ridge
    const auto r = StructureGenerator::planChimneyStack(20, 20, base, top, "Stone");
    ASSERT_FALSE(r.voxels.empty());

    std::set<int> ys;
    int maxY = INT_MIN, minY = INT_MAX;
    for (const auto& v : r.voxels) {
        const int y = gy(v);
        ys.insert(y); maxY = std::max(maxY, y); minY = std::min(minY, y);
        EXPECT_EQ(v.material, "Stone") << "chimney must be masonry";
        EXPECT_EQ(v.level, VoxelLevel::Microcube) << "chimney is microcube-thin, not full cubes";
    }
    EXPECT_EQ(minY, base) << "stack must start at the hearth base";
    EXPECT_EQ(maxY, top)  << "stack must reach the planned top";
    // F1 — CONTINUOUS: every micro-Y in [base, top] carries the stack (no flue gap).
    for (int y = base; y <= top; ++y)
        EXPECT_TRUE(ys.count(y)) << "flue/stack GAP at micro-Y " << y << " (smoke would be trapped)";
    // F2 — clears the ridge.
    EXPECT_GT(maxY, apex) << "stack does not clear the roof apex (downdraught)";
}

// F2 on REAL output: take the apex of an ACTUALLY-realized roof (the production path:
// MicroCanvas::microBounds), apply the SAME clearance constant the build handler uses, and assert the
// planned stack rises above that real apex. Teeth: the shared `kChimneyRidgeClearanceMicro` must meet
// the grounded 2 ft (0.610 m -> ceil(0.610*9)=6 micro) floor — if it regresses to 5, this FAILS.
TEST(ChimneyPlannerTest, ClearsTheRealRoofApexWithGroundedClearance) {
    // Grounded floor: 2 ft = 0.610 m -> 6 micro (1 micro = 1/9 m). The production constant must meet it.
    const int minClearanceMicro = (int)std::ceil(0.610 * 9.0);   // = 6
    EXPECT_GE(StructureGenerator::kChimneyRidgeClearanceMicro, minClearanceMicro)
        << "chimney ridge clearance " << StructureGenerator::kChimneyRidgeClearanceMicro
        << " micro is BELOW the 2 ft (0.610 m / " << minClearanceMicro << " micro) IRC R1003.9 floor";

    // Realize a real gable-roofed shell and read its apex the way the handler does.
    StyleProfileRegistry sreg;
    sreg.loadFromJson(nlohmann::json::parse(R"({
        "timber_cottage": { "roof_style":"gable", "foundation":"crawlspace",
            "thickness": { "exterior_wall":0.333, "interior_wall":0.222, "foundation_wall":0.667,
                           "floor":0.333, "ceiling":0.222 },
            "materials": { "structure":"Wood", "floor":"Wood", "roof":"Wood", "foundation":"Stone" },
            "roof": { "pitch":0.8 } } })"));
    BuildingProgram p;
    p.name = "house"; p.style = "timber_cottage"; p.footprintW = 12; p.footprintD = 8;
    p.substructure = "slab";
    { ProgStory s; s.height = 3; s.rooms.push_back(ProgRoom::fromJson(nlohmann::json::parse(
        R"({"id":"hall","purpose":"hall","rect":[0,0,12,8]})"))); p.stories.push_back(s); }

    auto shell = StructureRealizer::realizeShell(p, *sreg.get("timber_cottage"));
    ASSERT_TRUE(shell.ok) << shell.error;
    glm::ivec3 lo, hi;
    ASSERT_TRUE(shell.canvas.microBounds(lo, hi)) << "empty canvas — no apex to clear";
    const int apex = hi.y;   // real roof apex (local micro), as the handler reads it

    const int top = apex + StructureGenerator::kChimneyRidgeClearanceMicro;
    const auto r = StructureGenerator::planChimneyStack(50, 40, /*base=*/0, top, "Stone");
    int maxY = INT_MIN;
    for (const auto& v : r.voxels) maxY = std::max(maxY, gy(v));
    EXPECT_GT(maxY, apex) << "stack top " << maxY << " does not clear the real roof apex " << apex;
    EXPECT_GE(maxY - apex, minClearanceMicro) << "stack does not clear the apex by the grounded 2 ft";
}

// The stack has a FLUE VOID below the cap (open for smoke) and a SOLID cap on top (the pot).
TEST(ChimneyPlannerTest, HasFlueVoidBelowCapSolidTop) {
    const int base = 12, top = 40, cx = 20, cz = 20;
    const auto r = StructureGenerator::planChimneyStack(cx, cz, base, top, "Stone", /*capRows=*/2);
    const int midY = (base + top) / 2;   // a level well below the cap
    bool centreVoidBelowCap = true, capCentreSolid = false;
    for (const auto& v : r.voxels) {
        if (gx(v) == cx && gz(v) == cz) {
            if (gy(v) == midY) centreVoidBelowCap = false;   // found stone at the flue centre -> NOT void
            if (gy(v) == top)  capCentreSolid = true;        // cap centre is sealed
        }
    }
    EXPECT_TRUE(centreVoidBelowCap) << "the flue centre must be VOID below the cap (open for smoke)";
    EXPECT_TRUE(capCentreSolid)     << "the cap must SEAL the top (the pot hides the flue)";
}
