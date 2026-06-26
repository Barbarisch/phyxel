#include <gtest/gtest.h>

#include <vector>

#include "core/SettlementLayout.h"
#include "core/RoomLayout.h"
#include "core/RoomProgram.h"
#include "core/BuildingProgram.h"
#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"
#include "core/TraversalProbe.h"

using namespace Phyxel::Core;

// ============================================================================
// Settlement L3 — a generated settlement must be NAVIGABLE, not just non-overlapping
// on paper. We realize multiple buildings into a composed occupancy (each building's
// voxels at its plot offset; everything else is flat street/yard ground), then a
// TraversalProbe walks from the street into EVERY building's interior — proving the
// street network connects to every door. Teeth: a sealed building is unreachable.
// ============================================================================

namespace {
RoomProgram hallHouse() {
    RoomProgram rp;
    rp.name = "hall_house"; rp.bays = 4; rp.bayLength = 4; rp.widthMin = 6; rp.widthMax = 8;
    rp.rooms = {{"service", "service", 1.0}, {"hall", "hall", 2.0}, {"solar", "solar", 1.0}};
    return rp;
}
StyleProfile cottageStyle() {
    StyleProfileRegistry reg;
    reg.loadFromJson(nlohmann::json::parse(R"({
        "timber_cottage": { "roof_style":"gable", "foundation":"slab",
            "thickness": { "exterior_wall":0.333, "interior_wall":0.222, "foundation_wall":0.667,
                           "floor":0.333, "ceiling":0.222 },
            "materials": { "structure":"Wood", "floor":"Wood", "roof":"Wood", "foundation":"Stone" },
            "roof": { "pitch":0.8 } } })"));
    return *reg.get("timber_cottage");
}

struct Realized {
    Rect footprint;                       // settlement-local plot footprint
    StructureRealizer::ShellResult shell; // realized at LOCAL origin (0,0,0)
    BuildingProgram program;              // autofilled (for room centres / portals)
};

// Build + autofill (typology) + realize a hall_house at `fp`. If sealDoor, strip the exterior entrance
// before realizing (no door carved) — the negative control.
Realized realizeBuilding(const Rect& fp, bool sealDoor) {
    BuildingProgram p;
    p.name = "b"; p.style = "timber_cottage";
    p.footprintW = fp.w; p.footprintD = fp.d;
    p.substructure = "slab"; p.typology = "hall_house";
    ProgStory s; s.height = 3; p.stories.push_back(s);
    const RoomProgram rp = hallHouse();
    autofillRoomLayout(p, 1u, &rp);
    if (sealDoor) {
        std::vector<ProgPortal> interiorOnly;
        for (const auto& po : p.stories[0].portals)
            if (po.a != "exterior" && po.b != "exterior") interiorOnly.push_back(po);
        p.stories[0].portals = interiorOnly;
    }
    Realized r;
    r.footprint = fp;
    r.program = p;
    r.shell = StructureRealizer::realizeShell(p, cottageStyle());
    return r;
}

// Composed settlement occupancy: a micro inside a building's footprint -> that building's canvas
// (offset to its local frame); everything else is street/yard ground (solid below the walk surface).
bool settlementOccupied(const std::vector<Realized>& bs, int groundTop, int x, int y, int z) {
    for (const auto& b : bs) {
        const int lx = x - b.footprint.x * 9, lz = z - b.footprint.z * 9;
        if (lx >= 0 && lx < b.footprint.w * 9 && lz >= 0 && lz < b.footprint.d * 9)
            return b.shell.canvas.occupiedMicro(lx, y, lz);
    }
    return y < groundTop;   // open street/yard: solid ground below, walkable air above
}

// Build a 2-plot settlement (footprints 16x8) and realize both buildings (optionally sealing one).
std::vector<Realized> twoBuildingSettlement(int sealIndex /* -1 = none */) {
    auto layout = subdividePlots(52, 20, 2, 1, 4, 12);   // 2 plots side by side -> 16x8 footprints
    auto buildings = populatePlots(layout, 2, 8, "hall_house");
    std::vector<Realized> out;
    for (size_t i = 0; i < buildings.size(); ++i)
        out.push_back(realizeBuilding(buildings[i].footprint, (int)i == sealIndex));
    return out;
}
} // namespace

// A character on the street must be able to walk INTO every building (street -> yard -> door ->
// interior). This is the real "the settlement is navigable" proof.
TEST(SettlementTraversalTest, StreetReachesEveryBuildingInterior) {
    const auto bs = twoBuildingSettlement(/*sealIndex=*/-1);
    ASSERT_EQ(bs.size(), 2u);
    for (const auto& b : bs) ASSERT_TRUE(b.shell.ok) << b.shell.error;
    const int floorY = bs[0].shell.floorTopByStory[0];

    TraversalProbe probe([&](int x, int y, int z) { return settlementOccupied(bs, floorY, x, y, z); },
                         AgentBox{2, 16, 4});
    const glm::ivec3 start(26 * 9 + 4, floorY, 10 * 9 + 4);          // on the central street
    const glm::ivec3 bLo(0, floorY - 2, 0), bHi(52 * 9, floorY + 28, 20 * 9);

    for (size_t i = 0; i < bs.size(); ++i) {
        const Rect& fp = bs[i].footprint;
        const Rect& room0 = bs[i].program.stories[0].rooms[0].rect;   // first room interior
        const int gx = (fp.x + room0.x + room0.w / 2) * 9 + 4;
        const int gz = (fp.z + room0.z + room0.d / 2) * 9 + 4;
        EXPECT_TRUE(probe.reachable(start, glm::ivec3(gx - 2, floorY - 1, gz - 2),
                                    glm::ivec3(gx + 2, floorY + 1, gz + 2), bLo, bHi))
            << "the street can't reach building " << i << "'s interior (door not street-connected)";
    }
}

// TEETH: a SEALED building (exterior door stripped) is NOT reachable from the street — so the positive
// test depends on the carved door, not the probe phasing through walls.
TEST(SettlementTraversalTest, SealedBuildingIsUnreachable) {
    const auto bs = twoBuildingSettlement(/*sealIndex=*/0);          // building 0 has no exterior door
    ASSERT_EQ(bs.size(), 2u);
    for (const auto& b : bs) ASSERT_TRUE(b.shell.ok) << b.shell.error;
    const int floorY = bs[0].shell.floorTopByStory[0];

    TraversalProbe probe([&](int x, int y, int z) { return settlementOccupied(bs, floorY, x, y, z); },
                         AgentBox{2, 16, 4});
    const glm::ivec3 start(26 * 9 + 4, floorY, 10 * 9 + 4);
    const glm::ivec3 bLo(0, floorY - 2, 0), bHi(52 * 9, floorY + 28, 20 * 9);

    const Rect& fp = bs[0].footprint;
    const Rect& room0 = bs[0].program.stories[0].rooms[0].rect;
    const int gx = (fp.x + room0.x + room0.w / 2) * 9 + 4;
    const int gz = (fp.z + room0.z + room0.d / 2) * 9 + 4;
    EXPECT_FALSE(probe.reachable(start, glm::ivec3(gx - 2, floorY - 1, gz - 2),
                                 glm::ivec3(gx + 2, floorY + 1, gz + 2), bLo, bHi))
        << "reached a SEALED building's interior — the street->door proof has no teeth";
}
