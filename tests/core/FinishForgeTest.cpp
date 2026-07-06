#include <gtest/gtest.h>

#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"
#include "core/BuildingProgram.h"

using namespace Phyxel::Core;

// ============================================================================
// finish_forge P1 — FRAMED OPENINGS (docs/structure-generation/FinishDetailPlan.md)
//
// The realizer carves door/window/arch openings as RAW full-cube holes (the P
// "gaps only" state of the cut_openings placer). These tests pin the P1 frame
// invariant on the REAL canvas: every exterior door/window gets JAMBS inside the
// opening's side bands, a LINTEL band across its head, and windows get a sill
// ledge protruding proud of the facade plane — while the clear passage stays
// clear (framing must never make a door impassable).
//
// RED-BEFORE-GREEN: written against the raw-hole realizer, where every frame
// assertion fails (the whole opening box is air) and only the passage assertion
// holds. Occupancy-based (material-agnostic) so style changes don't break them.
// ============================================================================

namespace {

// Cottage from StructureRealizerTest + one west-wall WINDOW on the hall.
const char* kCottageWithWindow = R"({
    "name": "cottage", "style": "timber_cottage", "footprint": [7, 9],
    "substructure": "crawlspace", "roof_style": "gable",
    "stories": [{
        "height": 3,
        "rooms": [
            { "id": "hall",    "rect": [0,0,4,9], "purpose": "living" },
            { "id": "kitchen", "rect": [4,0,3,9], "purpose": "kitchen" }
        ],
        "portals": [
            { "between": ["exterior","hall"], "pos": [0,3], "width": 1, "height": 2, "kind": "door" },
            { "between": ["exterior","hall"], "pos": [0,6], "width": 1, "height": 1, "kind": "window" },
            { "between": ["hall","kitchen"],  "pos": [4,2], "width": 1, "height": 2, "kind": "arch" }
        ]
    }]
})";

StyleProfile timberCottageStyle() {
    StyleProfileRegistry reg;
    reg.loadFromJson(nlohmann::json::parse(R"({
        "timber_cottage": {
            "roof_style": "gable", "foundation": "crawlspace",
            "thickness": { "exterior_wall": 0.333, "interior_wall": 0.222,
                           "foundation_wall": 0.667, "floor": 0.333, "ceiling": 0.222 },
            "materials": { "structure": "Wood", "floor": "Wood", "roof": "Wood", "foundation": "Stone" },
            "roof": { "pitch": 0.8 }
        }
    })"));
    return *reg.get("timber_cottage");
}

StructureRealizer::ShellResult build() {
    return StructureRealizer::realizeShell(
        BuildingProgram::fromJson(nlohmann::json::parse(kCottageWithWindow)),
        timberCottageStyle());
}

// True if ANY micro cell in the half-open box is occupied.
bool anyOccupied(const MicroCanvas& c, int x0, int x1, int y0, int y1, int z0, int z1) {
    for (int x = x0; x < x1; ++x)
        for (int y = y0; y < y1; ++y)
            for (int z = z0; z < z1; ++z)
                if (c.occupiedMicro(x, y, z)) return true;
    return false;
}

// True if EVERY micro cell in the half-open box is air.
bool allClear(const MicroCanvas& c, int x0, int x1, int y0, int y1, int z0, int z1) {
    for (int x = x0; x < x1; ++x)
        for (int y = y0; y < y1; ++y)
            for (int z = z0; z < z1; ++z)
                if (c.occupiedMicro(x, y, z)) return false;
    return true;
}

} // namespace

// Exterior DOOR at cube (0,3), west wall (x=0), 1 cube wide, 2 cubes tall.
// Opening box in micro: x [0,9) (through-axis), z [27,36), y [wBase, wBase+18).
// Wall band occupies x [0,3) (timber_cottage exterior 0.333 m -> 3 micro).
TEST(FinishForgeTest, ExteriorDoorIsFramedNotRawHole) {
    auto r = build();
    ASSERT_TRUE(r.ok) << r.error;
    const auto& c = r.canvas;
    const int wb = r.floorTopMicro;

    // JAMBS: solid cells inside the opening's side bands (within the wall band depth),
    // spanning door height. Raw hole = all air here -> RED today.
    EXPECT_TRUE(anyOccupied(c, 0, 3, wb + 2, wb + 16, 27, 29))
        << "south jamb missing: door opening's -z side band is raw air";
    EXPECT_TRUE(anyOccupied(c, 0, 3, wb + 2, wb + 16, 34, 36))
        << "north jamb missing: door opening's +z side band is raw air";

    // LINTEL: solid band across the head of the clear span, inside the opening box.
    EXPECT_TRUE(anyOccupied(c, 0, 3, wb + 16, wb + 18, 30, 33))
        << "lintel missing: door head band is raw air";

    // CLEAR PASSAGE (guards over-framing; GREEN before and after): the centre strip
    // of the doorway stays walk-through air for its full depth.
    EXPECT_TRUE(allClear(c, 0, 9, wb + 2, wb + 15, 30, 33))
        << "door centre strip must stay clear after framing";
}

// Exterior WINDOW at cube (0,6), 1 cube, sits on the realizer's 1-cube sill offset.
// Opening box in micro: x [0,9), z [54,63), y [wBase+9, wBase+18).
TEST(FinishForgeTest, ExteriorWindowIsFramedWithProtrudingSill) {
    auto r = build();
    ASSERT_TRUE(r.ok) << r.error;
    const auto& c = r.canvas;
    const int wb = r.floorTopMicro;

    // JAMBS + LINTEL inside the window box (same invariant as doors).
    EXPECT_TRUE(anyOccupied(c, 0, 3, wb + 10, wb + 16, 54, 56))
        << "window south jamb missing";
    EXPECT_TRUE(anyOccupied(c, 0, 3, wb + 10, wb + 16, 61, 63))
        << "window north jamb missing";
    EXPECT_TRUE(anyOccupied(c, 0, 3, wb + 16, wb + 18, 57, 60))
        << "window lintel missing";

    // SILL: a ledge protruding PROUD of the facade plane (west facade = x 0; proud
    // cells sit at x = -1) along the window base. This is what breaks the flat
    // full-cube facade read. RED today: nothing exists outside the wall plane.
    EXPECT_TRUE(anyOccupied(c, -1, 0, wb + 7, wb + 10, 54, 63))
        << "window sill missing: nothing protrudes proud of the facade plane";
}
