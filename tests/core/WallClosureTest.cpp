// The shell must be CLOSED: no gap in an exterior wall that isn't a recorded opening.
//
// Every hole found in the generated tavern so far was found by a human looking at
// a screenshot. There are detectors for the floor slab (FloorIntegrityTest) and for
// opening size (WindowApertureTest), but nothing has ever asserted the simplest
// property of a building: that you cannot see through its walls.
//
// Scope is deliberately the storey interior — from each storey's walkable surface
// up to its ceiling — so the gable/roof profile above the eaves can't produce false
// positives. Within that band the wall is continuous by definition, or it is a hole.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/AssemblyPlan.h"
#include "core/BuildingProgram.h"
#include "core/FurniturePlacer.h"
#include "core/MicroCanvas.h"
#include "core/RoomLayout.h"
#include "core/RoomProgram.h"
#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"

using namespace Phyxel;
using namespace Phyxel::Core;

namespace {

bool loadCanon(RoomProgramRegistry& r) {
    for (const char* p : {"resources/room_program.json", "../resources/room_program.json",
                          "../../resources/room_program.json"})
        if (r.loadFromFile(p)) return true;
    return false;
}
bool loadStyles(StyleProfileRegistry& r) {
    for (const char* p : {"resources/structure_styles.json", "../resources/structure_styles.json",
                          "../../resources/structure_styles.json"})
        if (r.loadFromFile(p)) return true;
    return false;
}

struct Gap { int gx, gy, gz; };

bool insideAnyOpening(const AssemblyPlan& plan, int gx, int gy, int gz) {
    for (const auto& cut : plan.openings)
        for (const auto& t : cut.reveal) {
            if (t.role != "clear") continue;
            if (gx >= t.x && gx < t.x + t.w && gy >= t.y && gy < t.y + t.h &&
                gz >= t.z && gz < t.z + t.d)
                return true;
        }
    return false;
}

// Air cells in the four exterior wall bands, within each storey's interior height.
std::vector<Gap> wallGaps(const StructureRealizer::ShellResult& shell,
                          const BuildingProgram& p) {
    std::vector<Gap> gaps;
    const int W = p.footprintW, D = p.footprintD;
    const int extT = std::max(1, FurniturePlacer::planExteriorThicknessMicro(shell.plan));
    // Faces are the FOOTPRINT edges, not the canvas extent. The canvas reaches a cube
    // beyond the footprint because of the ROOF OVERHANG, so sampling its bounds aims
    // this detector at the eaves and reports the whole envelope as open (measured:
    // 17496 false gaps). The wall band straddles the footprint edge; local micro 0 /
    // W*9-1 are its faces.
    const int faceMinX = 0,         faceMaxX = W * 9 - 1;
    const int faceMinZ = 0,         faceMaxZ = D * 9 - 1;

    // ONE continuous band from the ground floor to the top storey's ceiling — not a
    // stack of per-storey islands. Checking each storey's interior separately leaves
    // the JOIST ZONE between them unchecked (storey 0's ceiling up to storey 1's
    // floor top), and that is exactly the height the recessed bays straddle: a
    // per-storey scan reported the walls closed while you could see the interior
    // through them.
    {
        if (shell.floorTopByStory.empty()) return gaps;
        const size_t top = p.stories.size() - 1;
        const int y0 = shell.floorTopByStory[0];
        const int y1 = shell.floorTopByStory[std::min(top, shell.floorTopByStory.size() - 1)] +
                       p.stories[top].height * 9;

        // A perimeter sample is SOLID if any cell through the band depth is solid.
        // Check the OUTER SKIN, not "any cell through the depth". A band whose outer
        // cell is missing but whose inner cell survives passes an any-cell test while
        // reading from outside as a recessed bay you can see the interior through —
        // which is precisely the defect an any-cell version of this test declared
        // clean. What you can see is what counts.
        auto bandSolid = [&](int gy, int along, int plane) {
            for (int t = 0; t < 1; ++t) {
                int gx, gz;
                switch (plane) {
                    case 0: gx = faceMinX + t;      gz = along; break;   // -X
                    case 1: gx = faceMaxX - t;      gz = along; break;   // +X
                    case 2: gx = along;             gz = faceMinZ + t; break;   // -Z
                    default: gx = along;            gz = faceMaxZ - t;  break;  // +Z
                }
                if (!shell.canvas.materialAt(gx, gy, gz).empty()) return true;
                if (insideAnyOpening(shell.plan, gx, gy, gz)) return true;   // a door IS a gap
            }
            return false;
        };

        for (int plane = 0; plane < 4; ++plane) {
            // Walk the face's own span, and skip the outermost cube at each end so a
            // corner (where two bands meet and the profile steps) can't read as a gap.
            const int a0 = (plane < 2) ? faceMinZ + 9 : faceMinX + 9;
            const int a1 = (plane < 2) ? faceMaxZ - 9 : faceMaxX - 9;
            for (int gy = y0; gy < y1; ++gy)
                for (int along = a0; along <= a1; ++along)
                    if (!bandSolid(gy, along, plane)) {
                        int gx, gz;
                        switch (plane) {
                            case 0: gx = faceMinX; gz = along; break;
                            case 1: gx = faceMaxX; gz = along; break;
                            case 2: gx = along;    gz = faceMinZ; break;
                            default: gx = along;   gz = faceMaxZ; break;
                        }
                        gaps.push_back({gx, gy, gz});
                    }
        }
    }
    return gaps;
}

std::string describe(const std::vector<Gap>& g) {
    if (g.empty()) return "none";
    int mnx = 1 << 30, mxx = -(1 << 30), mny = 1 << 30, mxy = -(1 << 30),
        mnz = 1 << 30, mxz = -(1 << 30);
    for (const auto& c : g) {
        mnx = std::min(mnx, c.gx); mxx = std::max(mxx, c.gx);
        mny = std::min(mny, c.gy); mxy = std::max(mxy, c.gy);
        mnz = std::min(mnz, c.gz); mxz = std::max(mxz, c.gz);
    }
    return std::to_string(g.size()) + " air cells; micro x[" + std::to_string(mnx) + ".." +
           std::to_string(mxx) + "] y[" + std::to_string(mny) + ".." + std::to_string(mxy) +
           "] z[" + std::to_string(mnz) + ".." + std::to_string(mxz) + "]  == cubes x[" +
           std::to_string(mnx / 9) + ".." + std::to_string(mxx / 9) + "] y[" +
           std::to_string(mny / 9) + ".." + std::to_string(mxy / 9) + "] z[" +
           std::to_string(mnz / 9) + ".." + std::to_string(mxz / 9) + "]";
}

}  // namespace

TEST(WallClosure, YouCannotSeeThroughTheTavernWalls) {
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

    const auto gaps = wallGaps(shell, p);
    EXPECT_TRUE(gaps.empty()) << "exterior wall is open where no opening was recorded: "
                              << describe(gaps);
}
