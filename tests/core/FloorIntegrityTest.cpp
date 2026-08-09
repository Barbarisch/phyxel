// A floor you can fall through is not a floor.
//
// Every storey above the ground has a slab. The ONLY thing allowed to interrupt
// it is a recorded stair well — that hole is the point of a stair. Anything else
// is a defect, and it is one you cannot see from inside the room it belongs to:
// you see it from OUTSIDE, as a notch in the wall with the floor edge cut away
// behind it.
//
// Both shell holes found so far were caught by a human looking at a screenshot.
// That is the detector this file replaces.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "core/BuildingProgram.h"
#include "core/MicroCanvas.h"
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

struct Hole { int gx = 0, gz = 0; };

// Micro cells of an upper storey's floor slab that are AIR, excluding the cells a
// recorded stair well legitimately opens. Returns them in canvas-local micro.
std::vector<Hole> floorHoles(const StructureRealizer::ShellResult& shell,
                             const BuildingProgram& p, size_t storyIndex) {
    std::vector<Hole> holes;
    // The slab: the recorded "floor" record for this storey.
    const AssemblyPlan& plan = shell.plan;
    const FloorPatch* slab = nullptr;
    int seen = 0;
    for (const auto& f : plan.floors) {
        if (f.role != "floor") continue;
        if (seen == (int)storyIndex) { slab = &f; break; }
        ++seen;
    }
    if (!slab) return holes;

    // Sample the topmost SOLID layer of the slab. The realizer reports each
    // storey's walkable surface micro-Y directly (floorTopByStory) — derive from
    // that rather than re-deriving it from y*9+thickness, which is how this
    // sampler first aimed a layer above the slab and reported the whole floor
    // missing. A mis-aimed sampler that "finds" a 100% hole is worse than no
    // detector, so the caller asserts the layer is mostly solid first.
    const int gy = (storyIndex < shell.floorTopByStory.size()
                        ? shell.floorTopByStory[storyIndex]
                        : slab->y * MicroCanvas::MICRO_PER_CUBE) - 1;

    for (int cx = 0; cx < slab->w; ++cx)
        for (int cz = 0; cz < slab->d; ++cz)
            for (int mx = 0; mx < 9; ++mx)
                for (int mz = 0; mz < 9; ++mz) {
                    const int gx = (slab->x + cx) * 9 + mx;
                    const int gz = (slab->z + cz) * 9 + mz;
                    if (!shell.canvas.materialAt(gx, gy, gz).empty()) continue;   // solid
                    // A recorded stair well is allowed to open the slab.
                    bool inWell = false;
                    for (const auto& s : plan.stairs) {
                        if (s.toStory != (int)storyIndex && s.fromStory != (int)storyIndex) continue;
                        if (gx >= s.holeX && gx < s.holeX + s.holeW &&
                            gz >= s.holeZ && gz < s.holeZ + s.holeD) { inWell = true; break; }
                    }
                    if (!inWell) holes.push_back({gx, gz});
                }
    (void)p;
    return holes;
}

std::string summarize(const std::vector<Hole>& holes) {
    if (holes.empty()) return "none";
    int mnx = 1 << 30, mxx = -(1 << 30), mnz = 1 << 30, mxz = -(1 << 30);
    for (const auto& h : holes) {
        mnx = std::min(mnx, h.gx); mxx = std::max(mxx, h.gx);
        mnz = std::min(mnz, h.gz); mxz = std::max(mxz, h.gz);
    }
    return std::to_string(holes.size()) + " air cells, micro x[" + std::to_string(mnx) + ".." +
           std::to_string(mxx) + "] z[" + std::to_string(mnz) + ".." + std::to_string(mxz) +
           "] (cubes x[" + std::to_string(mnx / 9) + ".." + std::to_string(mxx / 9) + "] z[" +
           std::to_string(mnz / 9) + ".." + std::to_string(mxz / 9) + "])";
}

}  // namespace

TEST(FloorIntegrity, UpperStoreyFloorsHaveNoHolesButTheStairWell) {
    RoomProgramRegistry reg;
    ASSERT_TRUE(loadCanon(reg));
    StyleProfileRegistry styles;
    ASSERT_TRUE(loadStyles(styles));
    const RoomProgram* tavern = reg.get("tavern");
    ASSERT_NE(tavern, nullptr);

    struct Case { int w, d; };
    for (const auto& c : {Case{7, 20}, Case{7, 14}, Case{6, 12}}) {
        nlohmann::json j;
        j["name"] = "tavern"; j["style"] = "timber_cottage";
        j["footprint"] = nlohmann::json::array({c.w, c.d});
        j["substructure"] = "crawlspace"; j["typology"] = "tavern";
        j["stories"] = nlohmann::json::array({nlohmann::json{{"height", 3}}});
        BuildingProgram p = BuildingProgram::fromJson(j);
        ASSERT_TRUE(autofillRoomLayout(p, 99u, tavern));
        const StyleProfile* sp = styles.get(p.style);
        ASSERT_NE(sp, nullptr);
        auto shell = StructureRealizer::realizeShell(p, *sp);
        ASSERT_TRUE(shell.ok) << shell.error;

        for (size_t si = 1; si < p.stories.size(); ++si) {
            const auto holes = floorHoles(shell, p, si);
            // SANITY: a detector that reports most of the slab missing is aimed at
            // the wrong layer, not looking at a broken building. Say so, loudly,
            // instead of emitting a fake defect list.
            int slabCells = 0;
            for (const auto& f : shell.plan.floors)
                if (f.role == "floor") slabCells = std::max(slabCells, f.w * f.d * 81);
            ASSERT_LT((int)holes.size(), slabCells / 2)
                << "floor sampler is mis-aimed (" << holes.size() << " of " << slabCells
                << " cells read as air) — fix the sampler before trusting any hole it reports";

            EXPECT_TRUE(holes.empty())
                << "footprint " << c.w << "x" << c.d << " storey " << si
                << ": floor is punctured — " << summarize(holes);
        }
    }
}
