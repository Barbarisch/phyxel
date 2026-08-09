// A window is a LIGHT, not a doorway-sized void.
//
// The generated tavern's upper-storey windows read as 1 m square holes punched in
// the wall. The portal grid is per-CUBE, so one cube is the smallest window the
// PLAN can express — but the realizer already frames the carved cube back in at
// micro resolution (jambs, lintel, sill), so the visible aperture is a free
// parameter. It was set to door proportions: jamb 1 micro, lintel 2, leaving a
// 7x7 micro (0.78 x 0.78 m) near-square opening in a chamber wall.
//
// room_program.json says so itself, in the provenance for windows.size:
//   "1x1 cube = the smallest carvable opening on the cube portal grid, KNOWN
//    OVERSIZED vs the qualitative 'small shuttered opening' record"
//
// This measures the aperture the realizer actually paints, from the recorded
// reveal (the realizer's contract is that the reveal IS what was painted), and
// holds it to something a medieval chamber would own: a narrow shuttered light,
// taller than it is wide.

#include <gtest/gtest.h>

#include <string>

#include "core/BuildingProgram.h"
#include "core/RoomLayout.h"
#include "core/RoomProgram.h"
#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"

using namespace Phyxel;
using namespace Phyxel::Core;

namespace {

bool loadCanon(RoomProgramRegistry& reg) {
    for (const char* p : {"resources/room_program.json", "../resources/room_program.json",
                          "../../resources/room_program.json"})
        if (reg.loadFromFile(p)) return true;
    return false;
}

bool loadStyles(StyleProfileRegistry& reg) {
    for (const char* p : {"resources/structure_styles.json", "../resources/structure_styles.json",
                          "../../resources/structure_styles.json"})
        if (reg.loadFromFile(p)) return true;
    return false;
}

struct Aperture { int run = 0, height = 0; bool ok = false; };

// The clear span the frame leaves open: the carved cube minus its two jambs
// (across) and minus the lintel (up). `run` is along the wall, `height` is up,
// both in micro (9 micro = 1 cube = 1 m).
Aperture apertureOf(const OpeningCut& cut) {
    Aperture a;
    const TrimBox* clear = nullptr;
    const TrimBox* lintel = nullptr;
    int jambRun = 0, jambs = 0;
    for (const auto& t : cut.reveal) {
        if (t.role == "clear" && !clear) clear = &t;
        else if (t.role == "lintel") lintel = &t;
        else if (t.role == "jamb") { jambRun += cut.alongZ ? t.d : t.w; ++jambs; }
    }
    if (!clear || !lintel || jambs < 2) return a;
    a.run = (cut.alongZ ? clear->d : clear->w) - jambRun;
    a.height = lintel->y - clear->y;
    a.ok = true;
    return a;
}

}  // namespace

TEST(WindowAperture, ChamberWindowsAreNarrowLightsNotSquareHoles) {
    RoomProgramRegistry reg;
    ASSERT_TRUE(loadCanon(reg));
    StyleProfileRegistry styles;
    ASSERT_TRUE(loadStyles(styles));
    const RoomProgram* tavern = reg.get("tavern");
    ASSERT_NE(tavern, nullptr);

    nlohmann::json j;
    j["name"] = "tavern"; j["style"] = "timber_cottage";
    j["footprint"] = nlohmann::json::array({7, 20});
    j["substructure"] = "crawlspace"; j["typology"] = "tavern";
    j["stories"] = nlohmann::json::array({nlohmann::json{{"height", 3}}});
    BuildingProgram p = BuildingProgram::fromJson(j);
    ASSERT_TRUE(autofillRoomLayout(p, 99u, tavern));
    const StyleProfile* sp = styles.get(p.style);
    ASSERT_NE(sp, nullptr);
    auto shell = StructureRealizer::realizeShell(p, *sp);
    ASSERT_TRUE(shell.ok) << shell.error;

    int windows = 0;
    for (const auto& cut : shell.plan.openings) {
        if (cut.kind != "window") continue;
        const Aperture a = apertureOf(cut);
        ASSERT_TRUE(a.ok) << "window at (" << cut.x << "," << cut.z << ") has no framed reveal";
        ++windows;
        const std::string where = "window at (" + std::to_string(cut.x) + "," +
                                  std::to_string(cut.z) + ") aperture " +
                                  std::to_string(a.run) + "x" + std::to_string(a.height) +
                                  " micro";

        EXPECT_GT(a.run, 0) << where << " — framed shut";
        EXPECT_GT(a.height, 0) << where << " — framed shut";
        // <= 6 micro (0.67 m) across. 7x7 was the door-proportioned default that
        // read as a square hole punched in the chamber wall.
        EXPECT_LE(a.run, 6) << where << " — too wide for a shuttered chamber light";
        // A medieval light is TALLER than it is wide; a square opening is what
        // made these read as doorways.
        EXPECT_GT(a.height, a.run) << where << " — square opening, not a light";
    }
    EXPECT_GE(windows, 4) << "the tavern generated no windows to measure";
}

// Doors must NOT be narrowed by the same change — a 0.78 m clear opening is the
// grounded figure for a real door (real clear openings 0.76-0.81 m) and shrinking
// it would break the character box's passage.
TEST(WindowAperture, DoorsKeepTheirGroundedClearWidth) {
    RoomProgramRegistry reg;
    ASSERT_TRUE(loadCanon(reg));
    StyleProfileRegistry styles;
    ASSERT_TRUE(loadStyles(styles));
    const RoomProgram* tavern = reg.get("tavern");
    ASSERT_NE(tavern, nullptr);

    nlohmann::json j;
    j["name"] = "tavern"; j["style"] = "timber_cottage";
    j["footprint"] = nlohmann::json::array({7, 20});
    j["substructure"] = "crawlspace"; j["typology"] = "tavern";
    j["stories"] = nlohmann::json::array({nlohmann::json{{"height", 3}}});
    BuildingProgram p = BuildingProgram::fromJson(j);
    ASSERT_TRUE(autofillRoomLayout(p, 99u, tavern));
    const StyleProfile* sp = styles.get(p.style);
    ASSERT_NE(sp, nullptr);
    auto shell = StructureRealizer::realizeShell(p, *sp);
    ASSERT_TRUE(shell.ok) << shell.error;

    int framedDoors = 0;
    for (const auto& cut : shell.plan.openings) {
        if (cut.kind != "door") continue;
        const Aperture a = apertureOf(cut);
        if (!a.ok) continue;              // interior doors carve without a frame
        ++framedDoors;
        EXPECT_GE(a.run, 7) << "exterior door narrowed to " << a.run
                            << " micro — a character can no longer pass";
    }
    EXPECT_GE(framedDoors, 1) << "no framed exterior door to check";
}
