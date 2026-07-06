#include <gtest/gtest.h>

#include "core/RoomLayout.h"
#include "core/RoomProgram.h"
#include "core/BuildingProgram.h"

using namespace Phyxel::Core;

// ============================================================================
// finish_forge — OPENINGS IN AUTO-FILLED LAYOUTS (FinishDetailPlan findings 1+2)
//
// Measured on the live settlement (2026-07-06): every auto-filled building has
// exactly ONE exterior door, always on the gable end (room 0 sits at length-pos
// 0 and the door placer checks x==0 first), and ZERO windows (no layout
// generator emits kind=="window"; room_program.json has none). Historically a
// longhouse/croft entrance is a CROSS-PASSAGE doorway on the LONG elevation,
// and rooms have (small, shuttered) window openings.
//
// RED-BEFORE-GREEN: written against the windowless gable-door autofill. Sizes /
// counts / sills are pinned loosely here (presence + wall placement); exact
// grounded dimensions live with the implementation (TrimGrounding/DimensionReference).
// ============================================================================

namespace {

RoomProgramRegistry& registry() {
    static RoomProgramRegistry reg;
    static bool loaded = reg.loadFromFile("resources/room_program.json");
    (void)loaded;
    return reg;
}

// Auto-fill a program the same way build_settlement does.
BuildingProgram autofill(const std::string& typology, int W, int D) {
    BuildingProgram p;
    p.name = "gen"; p.style = "timber_cottage"; p.typology = typology;
    p.footprintW = W; p.footprintD = D; p.substructure = "slab";
    ProgStory s; s.height = 3;
    p.stories.push_back(s);
    autofillRoomLayout(p, 1u, registry().get(typology));
    return p;
}

const ProgPortal* exteriorDoor(const ProgStory& st) {
    for (const auto& p : st.portals)
        if (p.kind == "door" && (p.a == "exterior" || p.b == "exterior")) return &p;
    return nullptr;
}

int windowCount(const ProgStory& st) {
    int n = 0;
    for (const auto& p : st.portals)
        if (p.kind == "window" && (p.a == "exterior" || p.b == "exterior")) ++n;
    return n;
}

} // namespace

// A 16x6 longhouse's entrance is a cross-passage doorway on the LONG elevation
// (front or back wall), NOT on the gable end. RED today: the autofill puts the
// only door on the x=0 gable.
TEST(OpeningsLayoutTest, LonghouseEntranceIsOnTheLongElevation) {
    ASSERT_NE(registry().get("longhouse"), nullptr) << "longhouse missing from room_program.json";
    auto p = autofill("longhouse", 16, 6);
    ASSERT_FALSE(p.stories.empty());
    const ProgPortal* e = exteriorDoor(p.stories[0]);
    ASSERT_NE(e, nullptr) << "no exterior door at all";
    // Length runs along X (16 >= 6): the long walls are z==0 and z==6.
    EXPECT_TRUE(e->pz == 0 || e->pz == 6)
        << "entrance sits on a gable end (px=" << e->px << ", pz=" << e->pz
        << ") — a longhouse doorway belongs on the long elevation (cross-passage)";
}

// Auto-filled ground stories carry window openings. RED today: zero windows in
// every auto-filled typology (settlement measurement: 4 windowless buildings).
TEST(OpeningsLayoutTest, AutofilledLonghouseHasWindows) {
    ASSERT_NE(registry().get("longhouse"), nullptr);
    auto p = autofill("longhouse", 16, 6);
    ASSERT_FALSE(p.stories.empty());
    EXPECT_GE(windowCount(p.stories[0]), 1)
        << "auto-filled longhouse has no window openings anywhere";
}

TEST(OpeningsLayoutTest, AutofilledCroftHasWindows) {
    ASSERT_NE(registry().get("croft"), nullptr) << "croft missing from room_program.json";
    auto p = autofill("croft", 8, 6);
    ASSERT_FALSE(p.stories.empty());
    EXPECT_GE(windowCount(p.stories[0]), 1)
        << "auto-filled croft has no window openings anywhere";
}

// Multi-story typologies (tavern: taproom below, chambers above) get windows on
// the UPPER story too — and the autofill's "exterior portals are ground-story
// only" rule must exempt windows (it exists to stop upper-story exterior DOORS).
TEST(OpeningsLayoutTest, TavernUpperChambersHaveWindows) {
    ASSERT_NE(registry().get("tavern"), nullptr) << "tavern missing from room_program.json";
    auto p = autofill("tavern", 12, 7);
    ASSERT_GE(p.stories.size(), 2u) << "tavern should auto-grow to 2 stories";
    EXPECT_GE(windowCount(p.stories[1]), 1)
        << "upper guest chambers are windowless";
    // The guard this rule exists for still holds: no exterior DOOR above ground.
    EXPECT_EQ(exteriorDoor(p.stories[1]), nullptr)
        << "an exterior door leaked onto an upper story";
}

// Windows never collide with the entrance: no window portal shares the door's
// wall cell. (GREEN trivially today — zero windows — and pinned once they exist.)
TEST(OpeningsLayoutTest, WindowsDoNotOverlapTheEntrance) {
    auto p = autofill("longhouse", 16, 6);
    ASSERT_FALSE(p.stories.empty());
    const ProgPortal* e = exteriorDoor(p.stories[0]);
    ASSERT_NE(e, nullptr);
    for (const auto& w : p.stories[0].portals) {
        if (w.kind != "window") continue;
        EXPECT_FALSE(w.px == e->px && w.pz == e->pz)
            << "window at (" << w.px << "," << w.pz << ") overlaps the entrance";
    }
}

// ---------------------------------------------------------------------------
// Geometry link (solution-auditor gap, 2026-07-06): the portal-level tests
// above cannot see an axis-convention mismatch between the layout generator
// and the realizer's carve pass. This test runs the REAL pipeline — autofill
// -> realizeShell — and asserts every generated exterior opening became an
// actual void through the wall band on the correct elevation.
// ---------------------------------------------------------------------------
#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"

namespace {

StyleProfile timberStyle() {
    StyleProfileRegistry reg;
    reg.loadFromJson(nlohmann::json::parse(R"({
        "timber_cottage": {
            "roof_style": "gable", "foundation": "slab",
            "thickness": { "exterior_wall": 0.222, "interior_wall": 0.111,
                           "foundation_wall": 0.444, "floor": 0.333, "ceiling": 0.111 },
            "materials": { "structure": "Wood", "floor": "Wood", "roof": "Thatch", "foundation": "Stone" },
            "roof": { "pitch_deg": 50.0 }
        }
    })"));
    return *reg.get("timber_cottage");
}

bool anyAir(const MicroCanvas& c, int x0, int x1, int y0, int y1, int z0, int z1) {
    for (int x = x0; x < x1; ++x)
        for (int y = y0; y < y1; ++y)
            for (int z = z0; z < z1; ++z)
                if (!c.occupiedMicro(x, y, z)) return true;
    return false;
}

} // namespace

TEST(OpeningsLayoutTest, AutofilledOpeningsAreCarvedThroughTheRealWall) {
    ASSERT_NE(registry().get("longhouse"), nullptr);
    auto p = autofill("longhouse", 16, 6);
    auto r = StructureRealizer::realizeShell(p, timberStyle());
    ASSERT_TRUE(r.ok) << r.error;
    const auto& c = r.canvas;
    const int wb = r.floorTopMicro;
    const int extT = StructureRealizer::thicknessMicro(0.222);

    int doors = 0, windows = 0;
    for (const auto& portal : p.stories[0].portals) {
        if (portal.a != "exterior" && portal.b != "exterior") continue;
        // Front/back long walls (z = 0 / z = 6 in cube coords). All exterior
        // openings on this typology sit on them by construction.
        ASSERT_TRUE(portal.pz == 0 || portal.pz == 6)
            << portal.kind << " not on a long wall (px=" << portal.px << ", pz=" << portal.pz << ")";
        const int zlo = (portal.pz == 0) ? 0 : 6 * 9 - extT;
        const int zhi = (portal.pz == 0) ? extT : 6 * 9;
        const int xlo = portal.px * 9;
        if (portal.kind == "door") {
            ++doors;
            // The doorway's centre strip must be a real void through the wall band.
            EXPECT_TRUE(anyAir(c, xlo + 3, xlo + 6, wb + 2, wb + 15, zlo, zhi))
                << "door portal at (" << portal.px << "," << portal.pz
                << ") was never carved through the wall";
        } else if (portal.kind == "window") {
            ++windows;
            // The window opening (on its 1-cube sill) must be a void in the band.
            EXPECT_TRUE(anyAir(c, xlo + 2, xlo + 7, wb + 10, wb + 16, zlo, zhi))
                << "window portal at (" << portal.px << "," << portal.pz
                << ") was never carved through the wall";
        }
    }
    EXPECT_EQ(doors, 2) << "expected the opposed cross-passage door pair";
    EXPECT_GE(windows, 1);
}

// Street-facing (settlement wiring, 2026-07-06): a "front" hint on the program
// names the street-side wall; the entrance (and therefore the windows, which
// follow the entrance wall) must sit on it. RED today: the hint is parsed but
// ignored — the door always lands on the z0 long wall.
TEST(OpeningsLayoutTest, FrontHintFlipsTheEntranceToTheStreetWall) {
    ASSERT_NE(registry().get("longhouse"), nullptr);
    BuildingProgram p;
    p.name = "gen"; p.style = "timber_cottage"; p.typology = "longhouse";
    p.footprintW = 16; p.footprintD = 6; p.substructure = "slab";
    p.front = "z1";                       // the street is on the +z side
    ProgStory s; s.height = 3;
    p.stories.push_back(s);
    autofillRoomLayout(p, 1u, registry().get("longhouse"));

    const ProgPortal* e = exteriorDoor(p.stories[0]);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->pz, 6) << "front hint z1 ignored: primary door not on the street wall";
    for (const auto& w : p.stories[0].portals)
        if (w.kind == "window" && (w.a == "exterior" || w.b == "exterior"))
            EXPECT_EQ(w.pz, 6) << "window at (" << w.px << "," << w.pz
                               << ") not on the street-facing front";
}
