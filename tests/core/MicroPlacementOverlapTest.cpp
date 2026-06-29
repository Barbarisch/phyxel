#include <gtest/gtest.h>

#include <fstream>

#include "core/RoomProgram.h"
#include "core/FurniturePlacer.h"
#include "core/RoomLayout.h"
#include "core/BuildingProgram.h"
#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"

using namespace Phyxel::Core;

// ============================================================================
// L2 VOXEL-OVERLAP — the falsifiable measurement the solution-auditor demanded for the furniture
// wall-clip / floor-sink fix. The earlier FurniturePlacerTest cases only check the inset ARITHMETIC;
// these scan the REAL realized shell voxels (StructureRealizer canvas.occupiedMicro) and assert that a
// wall-backed piece's micro footprint, placed at FurniturePlacer::microWorldPos, does NOT intersect any
// wall voxel — with a RED baseline proving the NAIVE (un-inset, cube-Y) placement DOES intersect.
// ============================================================================

namespace {
bool loadShippedCanon(RoomProgramRegistry& reg) {
    for (const char* p : {"resources/room_program.json", "../resources/room_program.json",
                          "../../resources/room_program.json", "../../../resources/room_program.json"}) {
        std::ifstream f(p);
        if (f.good()) return reg.loadFromFile(p);
    }
    return false;
}
StyleProfile cottageStyle() {
    StyleProfileRegistry reg;
    reg.loadFromJson(nlohmann::json::parse(R"({
        "timber_cottage": { "roof_style":"gable", "foundation":"crawlspace",
            "thickness": { "exterior_wall":0.333, "interior_wall":0.222, "foundation_wall":0.667,
                           "floor":0.333, "ceiling":0.222 },
            "materials": { "structure":"Wood", "floor":"Wood", "roof":"Wood", "foundation":"Stone" },
            "roof": { "pitch":0.8 } } })"));
    return *reg.get("timber_cottage");
}
BuildingProgram houseProgram(const RoomProgram* rp) {
    BuildingProgram p;
    p.name = "house"; p.style = "timber_cottage"; p.footprintW = 14; p.footprintD = 8;
    p.substructure = "slab"; p.typology = "hall_house";
    ProgStory s; s.height = 3; p.stories.push_back(s);
    autofillRoomLayout(p, 7u, rp);
    return p;
}
// Count occupied (wall) micro cells under a 1x1 piece's micro footprint at a wall-height row.
int wallOverlapAt(const StructureRealizer::ShellResult& sh, const glm::ivec3& microPos, int probeY) {
    int hits = 0;
    for (int mx = microPos.x; mx < microPos.x + 9; ++mx)
        for (int mz = microPos.z; mz < microPos.z + 9; ++mz)
            if (sh.canvas.occupiedMicro(mx, probeY, mz)) ++hits;
    return hits;
}
} // namespace

// GREEN: every wall-backed piece, placed at microWorldPos (inset by extT), clears the wall at a
// wall-height row. RED baseline (teeth): the NAIVE cube placement (no inset) embeds in the wall for at
// least one piece — so the green check measures a real property, not a vacuous pass.
TEST(MicroPlacementOverlapTest, InsetClearsWallsNaiveDoesNot) {
    RoomProgramRegistry reg;
    if (!loadShippedCanon(reg)) GTEST_SKIP() << "room_program.json not reachable from CWD";
    const RoomProgram* rp = reg.get("hall_house");
    ASSERT_NE(rp, nullptr);

    BuildingProgram p = houseProgram(rp);
    auto sh = StructureRealizer::realizeShell(p, cottageStyle());
    ASSERT_TRUE(sh.ok) << sh.error;
    ASSERT_FALSE(sh.floorTopByStory.empty());

    const int extT   = 3;                              // exterior_wall 0.333 m -> 3 micro
    const int wBase  = sh.floorTopByStory[0];          // walkable surface micro-Y
    const int probeY = wBase + 5;                      // a row up inside the wall band (walls span wBase..wTop)

    // Furnish the ground story at origin (0,0,0); 1x1 footprints (self-consistent with the check).
    const auto placements = FurniturePlacer::furnish(p.stories[0], glm::ivec3(0, 0, 0), wBase / 9);

    int wallBackedPieces = 0, insetOverlaps = 0, naiveOverlaps = 0;
    for (const auto& pl : placements) {
        if (pl.backDir == glm::ivec3(0)) continue;     // only wall-backed pieces touch a wall
        ++wallBackedPieces;
        const glm::ivec3 inset = FurniturePlacer::microWorldPos(pl, extT, wBase);
        insetOverlaps += wallOverlapAt(sh, inset, probeY);
        // NAIVE = the OLD cube placement: cube origin in micro, no inset.
        const glm::ivec3 naive(pl.worldPos.x * 9, wBase, pl.worldPos.z * 9);
        naiveOverlaps += wallOverlapAt(sh, naive, probeY);
    }

    ASSERT_GT(wallBackedPieces, 0) << "no wall-backed furniture to measure";
    EXPECT_EQ(insetOverlaps, 0)
        << "FIXED placement: furniture micro-footprints still intersect wall voxels (" << insetOverlaps
        << " cells) — furniture is INSIDE a wall";
    EXPECT_GT(naiveOverlaps, 0)
        << "RED baseline has no teeth: the old cube placement did NOT embed in any wall, so the inset "
        << "check proves nothing";
}
