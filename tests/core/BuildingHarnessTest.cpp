#include <gtest/gtest.h>

#include <iostream>
#include <string>
#include <vector>

#include "core/BuildingProgram.h"
#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"
#include "core/TraversalProbe.h"

// ============================================================================
// Building Validation Harness — runs the building pipeline across a CORPUS of varied
// programs and reports a PASS/FAIL matrix per validation layer. This is the quality-
// tracking / sign-off instrument: "N of M cases pass; here are the failures and why."
// It is the reusable strict-validation infrastructure — later placers add cases + layers.
//
// Layers checked (v1, shell + circulation):
//   build    — realizeShell succeeds, non-empty.
//   floors   — L2: every room on every story has a floor to stand on (no fall-through).
//   reach    — L3: a character-box (TraversalProbe) climbs from the ground floor to the
//              TOP floor (every floor reachable). The hard one; where multi-story bugs live.
// (Furniture per-story / KI-2 is a handler concern — a future harness layer, noted in the ledger.)
// ============================================================================

using namespace Phyxel::Core;

namespace {

StyleProfile harnessStyle() {
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

// A uniform-height tower: `stories` floors of height 3 over a w×d footprint, an exterior door
// on story 0, and (optionally) a stair on each floor to the next at well [1,2,2,6].
BuildingProgram tower(int stories, int w, int d, const std::string& form,
                      const std::string& substructure, bool withStairs) {
    nlohmann::json j;
    j["name"] = "case"; j["style"] = "timber_cottage";
    j["footprint"] = nlohmann::json::array({w, d});
    j["substructure"] = substructure; j["roof_style"] = "gable";
    j["stories"] = nlohmann::json::array();
    for (int s = 0; s < stories; ++s) {
        nlohmann::json room;
        room["id"] = "r" + std::to_string(s);
        room["rect"] = nlohmann::json::array({0, 0, w, d});
        room["purpose"] = "living";
        nlohmann::json story;
        story["height"] = 3;
        story["rooms"] = nlohmann::json::array({room});
        story["portals"] = nlohmann::json::array();
        story["stairs"] = nlohmann::json::array();
        if (s == 0)
            story["portals"].push_back({{"between", {"exterior", "r0"}},
                                        {"pos", {0, d / 2}}, {"width", 1}, {"height", 2}, {"kind", "door"}});
        if (withStairs && s < stories - 1) {
            nlohmann::json st;
            st["from_story"] = s; st["to_story"] = s + 1;
            st["rect"] = nlohmann::json::array({1, 2, 2, 6}); st["form"] = form;
            story["stairs"].push_back(st);
        }
        j["stories"].push_back(story);
    }
    return BuildingProgram::fromJson(j);
}

// L2: every room on every story has a floor cell at its centre (somewhere to stand).
bool floorsContinuous(const StructureRealizer::ShellResult& sh, const BuildingProgram& p) {
    for (size_t s = 0; s < p.stories.size() && s < sh.floorTopByStory.size(); ++s) {
        const int slabTop = sh.floorTopByStory[s] - 1;   // top micro of the floor slab
        for (const auto& room : p.stories[s].rooms) {
            const int cx = (room.rect.x + room.rect.w / 2) * 9 + 4;
            const int cz = (room.rect.z + room.rect.d / 2) * 9 + 4;
            if (!sh.canvas.occupiedMicro(cx, slabTop, cz)) return false;
        }
    }
    return true;
}

// L3: a character-box climbs from the ground floor to the TOP floor. Bounded to the (shared)
// stair well + a 1-cube margin. Multi-story with no stair => upper floors sealed => false.
bool topReachable(const StructureRealizer::ShellResult& sh, const BuildingProgram& p) {
    const int n = static_cast<int>(p.stories.size());
    if (n <= 1) return true;                          // single story: trivially "all floors"
    Rect well; bool haveWell = false;
    for (const auto& st : p.stories)
        for (const auto& s : st.stairs) { well = s.rect; haveWell = true; break; }
    if (!haveWell) return false;                      // no stairs in a multi-story building
    if ((int)sh.floorTopByStory.size() < n) return false;

    const int floor0 = sh.floorTopByStory[0];
    const int topY   = sh.floorTopByStory[n - 1];
    const int wx0 = well.x * 9, wz0 = well.z * 9, wxm = well.w * 9, wzm = well.d * 9;

    TraversalProbe probe([&](int x, int y, int z) { return sh.canvas.occupiedMicro(x, y, z); },
                         AgentBox{2, 16, 4});
    // Start inside LANE A (where a switchback's first flight runs), not the well centre (the lane
    // seam), and just south of the well so the agent walks in and climbs — matches the proven
    // AgentCanClimbSwitchbackToTopFloor start.
    const glm::ivec3 start(wx0 + wxm / 4, floor0, std::max(0, wz0 - 4));
    const glm::ivec3 goalLo(wx0 - 9, topY - 1, wz0 - 9), goalHi(wx0 + wxm + 9, topY + 2, wz0 + wzm + 9);
    const glm::ivec3 bLo(std::max(0, wx0 - 9), 0, std::max(0, wz0 - 9));
    const glm::ivec3 bHi(wx0 + wxm + 9, topY + 30, wz0 + wzm + 9);
    return probe.reachable(start, goalLo, goalHi, bLo, bHi);
}

struct CaseResult {
    std::string name;
    bool build = false, floors = false, reach = false;
    bool pass() const { return build && floors && reach; }
};

CaseResult runCase(const std::string& name, const BuildingProgram& p, const StyleProfile& style) {
    CaseResult r; r.name = name;
    auto sh = StructureRealizer::realizeShell(p, style);
    glm::ivec3 lo, hi;
    r.build = sh.ok && sh.canvas.microBounds(lo, hi);
    if (!r.build) return r;
    r.floors = floorsContinuous(sh, p);
    r.reach  = topReachable(sh, p);
    return r;
}

}  // namespace

// The corpus run + matrix report. Asserts the harness has TEETH (a stairless multi-story is
// flagged unreachable) and that the canonical exemplar passes; everything else is reported so
// the real pass-rate is visible (the sign-off number).
TEST(BuildingHarness, Corpus) {
    const StyleProfile style = harnessStyle();
    struct Spec { std::string name; BuildingProgram p; };
    std::vector<Spec> corpus = {
        {"1-story 7x9",                 tower(1, 7, 9, "switchback", "crawlspace", true)},
        {"1-story 5x6 small",           tower(1, 5, 6, "switchback", "crawlspace", true)},
        {"1-story 12x14 large",         tower(1, 12, 14, "switchback", "crawlspace", true)},
        {"2-story switchback",          tower(2, 7, 9, "switchback", "crawlspace", true)},
        {"3-story switchback (exemplar)",tower(3, 7, 9, "switchback", "crawlspace", true)},
        {"5-story switchback",          tower(5, 7, 9, "switchback", "crawlspace", true)},
        {"10-story switchback",         tower(10, 7, 9, "switchback", "crawlspace", true)},
        {"3-story switchback slab",     tower(3, 7, 9, "switchback", "slab", true)},
        {"3-story switchback basement", tower(3, 7, 9, "switchback", "basement", true)},
        {"2-story straight",            tower(2, 7, 9, "straight", "crawlspace", true)},
        {"3-story straight",            tower(3, 7, 9, "straight", "crawlspace", true)},
        {"BAD: 3-story NO stairs",      tower(3, 7, 9, "switchback", "crawlspace", false)},
    };

    std::cout << "\n=== BUILDING VALIDATION HARNESS ===\n";
    std::cout << "case                              build  floors  reach(L3)  OVERALL\n";
    int passed = 0;
    CaseResult bad, exemplar;
    for (const auto& c : corpus) {
        CaseResult r = runCase(c.name, c.p, style);
        auto yn = [](bool b) { return b ? " ok " : "FAIL"; };
        std::cout << "  " << c.name;
        for (int i = (int)c.name.size(); i < 32; ++i) std::cout << ' ';
        std::cout << "  " << yn(r.build) << "    " << yn(r.floors) << "     " << yn(r.reach)
                  << "      " << (r.pass() ? "PASS" : "----") << "\n";
        if (r.pass()) ++passed;
        if (c.name.rfind("BAD", 0) == 0) bad = r;
        if (c.name.rfind("3-story switchback (exemplar)", 0) == 0) exemplar = r;
    }
    std::cout << "--- " << passed << " / " << corpus.size() << " cases pass ---\n\n";

    // Teeth: a multi-story building with no stairs MUST be flagged unreachable.
    EXPECT_FALSE(bad.reach) << "harness has no teeth: a stairless 3-story read as reachable";
    // Regression guard: the canonical switchback exemplar must fully pass.
    EXPECT_TRUE(exemplar.pass()) << "the 3-story switchback exemplar regressed";
}
