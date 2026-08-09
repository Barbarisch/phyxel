// DIAGNOSTIC (not an assertion): print the east elevation of the realized shell at
// micro resolution, as the depth of the outermost solid cell.
//
// Every shell-side detector says the tavern is closed, and the recessed bays are
// plainly visible in a render anyway. Rather than guess at a fifth mechanism, draw
// the surface and look at it: for each (y, z) on the +X side, find the largest x
// carrying material and print how far in from the footprint edge it sits.
//
//   '.'  = flush with the footprint edge      '1'..'8' = recessed that many micro
//   ' '  = nothing solid anywhere on that row (open)

#include <gtest/gtest.h>

#include <iostream>
#include <string>

#include "core/BuildingProgram.h"
#include "core/MicroCanvas.h"
#include "core/RoomLayout.h"
#include "core/RoomProgram.h"
#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"

using namespace Phyxel;
using namespace Phyxel::Core;

TEST(EastFaceProbe, DrawTheElevation) {
    RoomProgramRegistry reg;
    bool okc = false;
    for (const char* p : {"resources/room_program.json", "../resources/room_program.json"})
        if (reg.loadFromFile(p)) { okc = true; break; }
    ASSERT_TRUE(okc);
    StyleProfileRegistry styles;
    bool oks = false;
    for (const char* p : {"resources/structure_styles.json", "../resources/structure_styles.json"})
        if (styles.loadFromFile(p)) { oks = true; break; }
    ASSERT_TRUE(oks);
    const RoomProgram* tavern = reg.get("tavern");
    ASSERT_NE(tavern, nullptr);

    nlohmann::json j;
    j["name"] = "tavern"; j["style"] = "timber_cottage";
    j["footprint"] = nlohmann::json::array({7, 20});
    j["substructure"] = "crawlspace"; j["typology"] = "tavern";
    j["stories"] = nlohmann::json::array({nlohmann::json{{"height", 3}}});
    BuildingProgram p = BuildingProgram::fromJson(j);
    ASSERT_TRUE(autofillRoomLayout(p, 99u, tavern));
    auto shell = StructureRealizer::realizeShell(p, *styles.get(p.style));
    ASSERT_TRUE(shell.ok) << shell.error;

    glm::ivec3 lo(0), hi(0);
    ASSERT_TRUE(shell.canvas.microBounds(lo, hi));
    std::cout << "canvas micro bounds: x[" << lo.x << ".." << hi.x << "] y[" << lo.y << ".."
              << hi.y << "] z[" << lo.z << ".." << hi.z << "]\n";
    std::cout << "footprint edge x = " << (p.footprintW * 9 - 1)
              << ";  storey surfaces:";
    for (int f : shell.floorTopByStory) std::cout << " " << f;
    std::cout << "\n\nEAST ELEVATION (rows = y desc, cols = z every 3 micro)\n";

    const int edge = p.footprintW * 9 - 1;
    for (int gy = hi.y; gy >= lo.y; gy -= 2) {
        char row[128];
        int n = 0;
        for (int gz = lo.z; gz <= hi.z && n < 120; gz += 3) {
            int found = -1;
            for (int gx = hi.x; gx >= lo.x; --gx)
                if (!shell.canvas.materialAt(gx, gy, gz).empty()) { found = gx; break; }
            if (found < 0)            row[n++] = ' ';
            else if (found >= edge)   row[n++] = '.';
            else {
                const int d = edge - found;
                row[n++] = (d <= 8) ? char('0' + d) : '#';
            }
        }
        row[n] = 0;
        printf("y=%3d |%s|\n", gy, row);
    }
    SUCCEED();
}
