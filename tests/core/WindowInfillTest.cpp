#include <gtest/gtest.h>

#include "core/BuildingProgram.h"
#include "core/RoomLayout.h"
#include "core/RoomProgram.h"
#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"

using namespace Phyxel::Core;

// ============================================================================
// finish_forge P3 — WINDOW INFILL (#10 / #15 slice). Open-air window holes read
// as ruins at street level. The GROUNDED period default is SHUTTERED (glazing
// unaffordable for ordinary households before 1558 — croft windows.size source);
// "glass" is realizable only where a typology cites it (no typology does yet —
// manor_hall has no grounded windows spec at all).
//
// Invariants: every autofilled window carries the grounded infill; the REALIZED
// reveal contains shutter geometry — a CLOSED plank leaf in the clear reveal, or
// an OPEN pair of panels folded back on the facade (deterministic per-opening
// hash mixes both); the glass mechanism paints a Glass pane when declared.
// RED baseline: the pre-infill realizer carves openings to pure air (leaf +
// panel volumes empty), proven red by stashing the realizer change.
// ============================================================================

namespace {

RoomProgramRegistry& registry() {
    static RoomProgramRegistry reg;
    static bool loaded = [] {
        for (const char* p : {"resources/room_program.json", "../resources/room_program.json",
                              "../../resources/room_program.json",
                              "../../../resources/room_program.json"})
            if (reg.loadFromFile(p)) return true;
        return false;
    }();
    (void)loaded;
    return reg;
}

BuildingProgram autofill(const std::string& typology, int W, int D) {
    BuildingProgram p;
    p.name = "gen"; p.style = "timber_cottage"; p.typology = typology;
    p.footprintW = W; p.footprintD = D; p.substructure = "slab";
    ProgStory s; s.height = 3;
    p.stories.push_back(s);
    autofillRoomLayout(p, 1u, registry().get(typology));
    return p;
}

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

bool anySolid(const MicroCanvas& c, int x0, int x1, int y0, int y1, int z0, int z1) {
    for (int x = x0; x < x1; ++x)
        for (int y = y0; y < y1; ++y)
            for (int z = z0; z < z1; ++z)
                if (c.occupiedMicro(x, y, z)) return true;
    return false;
}
bool anyMaterial(const MicroCanvas& c, const std::string& mat,
                 int x0, int x1, int y0, int y1, int z0, int z1) {
    for (int x = x0; x < x1; ++x)
        for (int y = y0; y < y1; ++y)
            for (int z = z0; z < z1; ++z)
                if (c.materialAt(x, y, z) == mat) return true;
    return false;
}

} // namespace

// Every autofilled window carries the grounded default infill (data threading:
// WindowSpec.infill -> ProgPortal.infill).
TEST(WindowInfillTest, AutofilledWindowsDeclareShutteredInfill) {
    ASSERT_NE(registry().get("longhouse"), nullptr);
    auto p = autofill("longhouse", 16, 6);
    ASSERT_FALSE(p.stories.empty());
    int windows = 0;
    for (const auto& po : p.stories[0].portals) {
        if (po.kind != "window") continue;
        ++windows;
        EXPECT_EQ(po.infill, "shuttered")
            << "window at (" << po.px << "," << po.pz << ") lost the grounded infill";
    }
    ASSERT_GE(windows, 1) << "fixture must generate windows";
}

// THE street-read invariant (RED on the pre-infill realizer: openings are pure air): every
// realized window shows shutter geometry — a closed leaf in the clear reveal OR open panels
// folded back on the facade flanking the opening.
TEST(WindowInfillTest, RealizedWindowRevealsContainShutterGeometry) {
    ASSERT_NE(registry().get("longhouse"), nullptr);
    auto p = autofill("longhouse", 16, 6);
    auto r = StructureRealizer::realizeShell(p, timberStyle());
    ASSERT_TRUE(r.ok) << r.error;
    const auto& c = r.canvas;
    const int wb = r.floorTopMicro;
    const int extT = StructureRealizer::thicknessMicro(0.222);
    const int kJamb = 1, kLintel = 2, sill = 1;

    int windows = 0, withShutters = 0;
    for (const auto& po : p.stories[0].portals) {
        if (po.kind != "window") continue;
        ASSERT_TRUE(po.pz == 0 || po.pz == 6) << "longhouse window off the long walls";
        ++windows;
        const int w = std::max(1, po.width);
        const int oyBase = wb + sill * 9;
        const int oyTop = oyBase + po.height * 9;
        const int jambTop = oyTop - kLintel;
        const int x0 = po.px * 9, x1 = (po.px + w) * 9;
        const int zWall0 = (po.pz == 0) ? 0 : 6 * 9 - extT;         // wall band depth
        const int zWall1 = (po.pz == 0) ? extT : 6 * 9;
        const int proudZ = (po.pz == 0) ? -1 : 6 * 9;               // 1 micro proud of the facade
        const int panelW = std::max(2, (x1 - x0) / 2);
        // closed leaf: any solid in the CLEAR reveal (inside jambs, above sill, below lintel)
        const bool leaf = anySolid(c, x0 + kJamb, x1 - kJamb, oyBase, jambTop, zWall0, zWall1);
        // open shutters: panels on the proud facade plane flanking the opening
        const bool panels = anySolid(c, x0 - panelW, x0, oyBase, jambTop, proudZ, proudZ + 1) &&
                            anySolid(c, x1, x1 + panelW, oyBase, jambTop, proudZ, proudZ + 1);
        if (leaf || panels) ++withShutters;
        EXPECT_TRUE(leaf || panels)
            << "window at (" << po.px << "," << po.pz << ") is an open-air hole — no leaf in the "
            << "reveal and no folded-back panels on the facade";
    }
    ASSERT_GE(windows, 1) << "fixture must generate windows";
    EXPECT_EQ(withShutters, windows);
}

// The glass mechanism: a window portal declaring infill="glass" gets a Glass pane in the clear
// reveal. (No shipped typology declares glass yet — manor_hall has no grounded windows spec —
// this proves the realizer path so grounded content can land as data.)
TEST(WindowInfillTest, GlassInfillPaintsAGlassPane) {
    BuildingProgram p;
    p.name = "glasstest"; p.style = "timber_cottage";
    p.footprintW = 10; p.footprintD = 6; p.substructure = "slab";
    ProgStory s; s.height = 3;
    ProgRoom room; room.id = "hall"; room.purpose = "hall"; room.rect = {0, 0, 10, 6};
    s.rooms.push_back(room);
    ProgPortal door; door.a = "exterior"; door.b = "hall"; door.kind = "door";
    door.px = 4; door.pz = 0; door.width = 1; door.height = 3;
    s.portals.push_back(door);
    ProgPortal win; win.a = "exterior"; win.b = "hall"; win.kind = "window";
    win.px = 7; win.pz = 0; win.width = 1; win.height = 1; win.infill = "glass";
    s.portals.push_back(win);
    p.stories.push_back(s);

    auto r = StructureRealizer::realizeShell(p, timberStyle());
    ASSERT_TRUE(r.ok) << r.error;
    const int extT = StructureRealizer::thicknessMicro(0.222);
    const int oyBase = r.floorTopMicro + 9;                          // 1-cube sill
    EXPECT_TRUE(anyMaterial(r.canvas, "Glass", 7 * 9, 8 * 9, oyBase, oyBase + 9, 0, extT))
        << "declared glass infill did not paint a Glass pane in the reveal";
}
