#include <gtest/gtest.h>

#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"
#include "core/BuildingProgram.h"

using namespace Phyxel::Core;

// ============================================================================
// finish_forge P2 place_trim, increment A — CORNER QUOINS
// (docs/structure-generation/FinishDetailPlan.md §P2, docs/structure-generation/TrimGrounding.md)
//
// Styles with flags.quoins (stone_manor, stone_keep) get alternating dressed-
// stone corner blocks proud of both facade planes — "long and short work".
// GROUNDED: reclaimed stone quoins 450x300x145 mm (Britannia Stone, TrimGrounding)
// -> long leg 4 micro, short leg 3 micro, proud face + course height 1 micro
// (the 145 mm dimension at the grid floor). The 4:3 alternation emerges from the
// grounded block itself (long-and-short work is attested; a numeric ratio is
// NEEDS-RESEARCH, so no extra number is invented).
//
// RED-BEFORE-GREEN: written against the trim-less realizer — corners are flat
// wall planes with NOTHING proud of the facade (the P2 relief diagnosis).
// ============================================================================

namespace {

const char* kStoneWing = R"({
    "name": "wing", "style": "quoin_test", "footprint": [9, 6],
    "substructure": "slab", "roof_style": "gable",
    "stories": [{
        "height": 3,
        "rooms": [ { "id": "hall", "rect": [0,0,9,6], "purpose": "living" } ],
        "portals": [
            { "between": ["exterior","hall"], "pos": [4,0], "width": 1, "height": 2, "kind": "door" }
        ]
    }]
})";

StyleProfile quoinStyle() {
    StyleProfileRegistry reg;
    reg.loadFromJson(nlohmann::json::parse(R"({
        "quoin_test": {
            "roof_style": "gable", "foundation": "slab",
            "thickness": { "exterior_wall": 0.667, "interior_wall": 0.333,
                           "foundation_wall": 0.667, "floor": 0.333, "ceiling": 0.333 },
            "materials": { "structure": "StoneBricks", "cladding": "Stone", "trim": "Stone",
                           "floor": "Wood", "roof": "ClayTile", "foundation": "Stone" },
            "flags": { "quoins": true },
            "roof": { "pitch_deg": 40.0 }
        }
    })"));
    return *reg.get("quoin_test");
}

StructureRealizer::ShellResult build() {
    return StructureRealizer::realizeShell(
        BuildingProgram::fromJson(nlohmann::json::parse(kStoneWing)),
        quoinStyle());
}

// Count consecutive occupied PROUD cells (just outside the facade plane) running
// away from the corner along +x at z = -1 (outside the z0 facade), at micro row y.
int proudRunAlongX(const MicroCanvas& c, int y) {
    int n = 0;
    while (c.occupiedMicro(n, y, -1)) ++n;
    return n;
}
// Same along +z at x = -1 (outside the x0 facade).
int proudRunAlongZ(const MicroCanvas& c, int y) {
    int n = 0;
    while (c.occupiedMicro(-1, y, n)) ++n;
    return n;
}

} // namespace

// The SW corner carries quoin cells PROUD of both facade planes for the full
// wall height. RED today: nothing exists outside the wall planes at corners.
TEST(TrimForgeTest, StoneCornersCarryProudQuoins) {
    auto r = build();
    ASSERT_TRUE(r.ok) << r.error;
    const auto& c = r.canvas;
    const int wb = r.floorTopMicro;

    bool anyProudX = false, anyProudZ = false;
    for (int y = wb; y < wb + 27; ++y) {           // the 3-cube story wall band
        if (proudRunAlongX(c, y) > 0) anyProudX = true;
        if (proudRunAlongZ(c, y) > 0) anyProudZ = true;
    }
    EXPECT_TRUE(anyProudX) << "no quoin cells proud of the z0 facade at the SW corner";
    EXPECT_TRUE(anyProudZ) << "no quoin cells proud of the x0 facade at the SW corner";
}

// Long-and-short work: successive 1-micro courses alternate the leg orientation —
// a course with the LONG (4-micro) run along x has the SHORT (3-micro) run along
// z, and the next course swaps. RED today (all runs are 0).
TEST(TrimForgeTest, QuoinCoursesAlternateLongAndShort) {
    auto r = build();
    ASSERT_TRUE(r.ok) << r.error;
    const auto& c = r.canvas;
    const int wb = r.floorTopMicro;

    int alternations = 0;
    for (int course = 0; course + 1 < 12; ++course) {
        const int xa = proudRunAlongX(c, wb + course),     za = proudRunAlongZ(c, wb + course);
        const int xb = proudRunAlongX(c, wb + course + 1), zb = proudRunAlongZ(c, wb + course + 1);
        // A valid pair: one course long-x/short-z, the neighbour short-x/long-z.
        if (xa == 4 && za == 3 && xb == 3 && zb == 4) ++alternations;
        if (xa == 3 && za == 4 && xb == 4 && zb == 3) ++alternations;
    }
    EXPECT_GE(alternations, 4)
        << "corner shows no long-and-short alternation (runs are uniform or absent)";
}

// A style WITHOUT the quoins flag stays a clean flat corner (no relief invented
// where the style doesn't call for it). GREEN before and after.
TEST(TrimForgeTest, NoQuoinsWithoutTheStyleFlag) {
    StyleProfileRegistry reg;
    reg.loadFromJson(nlohmann::json::parse(R"({
        "plain_test": {
            "roof_style": "gable", "foundation": "slab",
            "thickness": { "exterior_wall": 0.667, "interior_wall": 0.333,
                           "foundation_wall": 0.667, "floor": 0.333, "ceiling": 0.333 },
            "materials": { "structure": "StoneBricks", "cladding": "Stone", "trim": "Stone",
                           "floor": "Wood", "roof": "ClayTile", "foundation": "Stone" },
            "flags": { "quoins": false },
            "roof": { "pitch_deg": 40.0 }
        }
    })"));
    auto r = StructureRealizer::realizeShell(
        BuildingProgram::fromJson(nlohmann::json::parse(kStoneWing)), *reg.get("plain_test"));
    ASSERT_TRUE(r.ok) << r.error;
    const auto& c = r.canvas;
    const int wb = r.floorTopMicro;
    for (int y = wb; y < wb + 27; ++y) {
        EXPECT_EQ(proudRunAlongX(c, y), 0) << "unexpected proud corner cells without the quoins flag";
        EXPECT_EQ(proudRunAlongZ(c, y), 0);
    }
}

// ---------------------------------------------------------------------------
// Auditor gaps (2026-07-06): quoins vs corner-adjacent openings, and the
// narrow-footprint stress axis.
// ---------------------------------------------------------------------------

// A window one cube from the corner: BOTH proud mechanisms (quoin shell, window
// sill/jambs) must coexist — the window stays framed with a clear void and the
// corner still carries its quoins. Guards the pass-ordering overwrite risk
// (pass 4.5 paints after the carve pass).
TEST(TrimForgeTest, QuoinsCoexistWithACornerAdjacentWindow) {
    const char* prog = R"({
        "name": "wing", "style": "quoin_test", "footprint": [9, 6],
        "substructure": "slab", "roof_style": "gable",
        "stories": [{
            "height": 3,
            "rooms": [ { "id": "hall", "rect": [0,0,9,6], "purpose": "living" } ],
            "portals": [
                { "between": ["exterior","hall"], "pos": [4,0], "width": 1, "height": 2, "kind": "door" },
                { "between": ["exterior","hall"], "pos": [1,0], "width": 1, "height": 1, "kind": "window" }
            ]
        }]
    })";
    auto r = StructureRealizer::realizeShell(
        BuildingProgram::fromJson(nlohmann::json::parse(prog)), quoinStyle());
    ASSERT_TRUE(r.ok) << r.error;
    const auto& c = r.canvas;
    const int wb = r.floorTopMicro;

    // Quoins still present at the SW corner.
    bool anyProud = false;
    for (int y = wb; y < wb + 27; ++y) if (proudRunAlongX(c, y) > 0) anyProud = true;
    EXPECT_TRUE(anyProud) << "corner quoins vanished when a window sits one cube away";

    // The window (cube x=1, front wall z0) keeps a real void through the wall band
    // at opening height — the quoin pass must not have filled it.
    bool voidExists = false;
    for (int x = 9 + 2; x < 9 + 7 && !voidExists; ++x)
        for (int y = wb + 10; y < wb + 16 && !voidExists; ++y)
            for (int z = 0; z < 6 && !voidExists; ++z)
                if (!c.occupiedMicro(x, y, z)) voidExists = true;
    EXPECT_TRUE(voidExists) << "window void was overwritten (quoin/window interaction)";

    // And its sill still protrudes proud of the facade at the window base.
    bool sillProud = false;
    for (int x = 9; x < 18 && !sillProud; ++x)
        for (int y = wb + 7; y < wb + 10 && !sillProud; ++y)
            if (c.occupiedMicro(x, y, -1)) sillProud = true;
    EXPECT_TRUE(sillProud) << "window sill lost its proud ledge next to the quoined corner";
}

// Narrow-footprint stress: on a minimal 3x3 building the 4-micro quoin legs from
// opposite corners of one 27-micro facade must not meet or wrap; runs stay at
// their grounded lengths and the wall centre stays quoin-free.
TEST(TrimForgeTest, QuoinLegsDoNotCollideOnANarrowFootprint) {
    const char* prog = R"({
        "name": "hut", "style": "quoin_test", "footprint": [3, 3],
        "substructure": "slab", "roof_style": "gable",
        "stories": [{
            "height": 3,
            "rooms": [ { "id": "hall", "rect": [0,0,3,3], "purpose": "living" } ],
            "portals": [
                { "between": ["exterior","hall"], "pos": [1,0], "width": 1, "height": 2, "kind": "door" }
            ]
        }]
    })";
    auto r = StructureRealizer::realizeShell(
        BuildingProgram::fromJson(nlohmann::json::parse(prog)), quoinStyle());
    ASSERT_TRUE(r.ok) << r.error;
    const auto& c = r.canvas;
    const int wb = r.floorTopMicro;

    for (int y = wb; y < wb + 27; ++y) {
        const int run = proudRunAlongX(c, y);
        EXPECT_LE(run, 4) << "quoin leg overran its grounded length on a narrow facade";
        // Facade is 27 micro wide; legs from both corners (<=4 each) leave the middle clear.
        EXPECT_FALSE(c.occupiedMicro(13, y, -1))
            << "quoin cells met in the middle of a 3-cube facade at y=" << y;
    }
}
