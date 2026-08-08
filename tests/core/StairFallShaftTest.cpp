#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "core/BuildingProgram.h"
#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"
#include "core/TraversalProbe.h"

// ============================================================================
// M2 stair correctness — RED tests first (StructureForge milestone M2).
//
// The defects these pin (all live at time of writing):
//  1. StructureRealizer's place_stairs pass never checks StairPlan::ok
//     (StructureRealizer.cpp stairs pass): a failed STRAIGHT plan still cuts
//     the full stairwell hole with ZERO treads under it — a fall shaft; a
//     failed SWITCHBACK plan silently builds nothing (upper story sealed, no
//     diagnostics) — and BOTH still push a StairRecord into the AssemblyPlan,
//     so downstream consumers (furniture reservation, featureAt, validators)
//     believe a stair exists.
//  2. No balustrade/guard anywhere: the stairwell opening in the upper floor
//     is an unguarded >2 m drop on every side, including over the descending
//     lane (placer spec 12 F4).
//
// Contract after M2:
//  - A multi-story program whose authored stair cannot be planned walkably
//    REPAIRS once (alternate form in the same well) or the shell realization
//    FAILS (shell.ok == false) — never a silent no-stair, never a hole with
//    no flight.
//  - Guard invariant at the realized canvas: every micro column on the
//    stairwell perimeter at an upper story's floor level either has a small
//    drop (<= the character step-up, e.g. the emergence tread) or a guard
//    solid within the guard band above the floor.
// ============================================================================

using namespace Phyxel::Core;

namespace {

StyleProfile stairStyle() {
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

// Two stories over w×d, one room per story, exterior door on story 0, and ONE
// authored stair (rect + form) from story 0 to 1.
BuildingProgram twoStory(int w, int d, const Rect& well, const std::string& form) {
    BuildingProgram p;
    p.name = "stair_case"; p.style = "timber_cottage";
    p.footprintW = w; p.footprintD = d; p.substructure = "crawlspace";
    for (int s = 0; s < 2; ++s) {
        ProgStory st; st.height = 3;
        ProgRoom r; r.id = s ? "upper" : "hall"; r.rect = {0, 0, w, d};
        r.purpose = s ? "bedchamber" : "living";
        st.rooms.push_back(r);
        p.stories.push_back(st);
    }
    ProgPortal door; door.a = "exterior"; door.b = "hall"; door.kind = "door";
    door.px = 0; door.pz = d / 2; door.width = 1; door.height = 2;
    p.stories[0].portals.push_back(door);
    ProgStair sr; sr.fromStory = 0; sr.toStory = 1; sr.rect = well; sr.form = form;
    p.stories[0].stairs.push_back(sr);
    return p;
}

// A character-box can climb from the lower floor into the upper story through
// the well (same probe geometry the BuildingHarness uses).
bool topReachable(const StructureRealizer::ShellResult& sh, const Rect& well) {
    if (sh.floorTopByStory.size() < 2) return false;
    const int floor0 = sh.floorTopByStory[0];
    const int topY   = sh.floorTopByStory[1];
    const int wx0 = well.x * 9, wz0 = well.z * 9, wxm = well.w * 9, wzm = well.d * 9;
    TraversalProbe probe([&](int x, int y, int z) { return sh.canvas.occupiedMicro(x, y, z); },
                         AgentBox{2, 16, 4});
    const glm::ivec3 start(wx0 + wxm / 4, floor0, std::max(0, wz0 - 4));
    const glm::ivec3 goalLo(wx0 - 9, topY - 1, wz0 - 9), goalHi(wx0 + wxm + 9, topY + 2, wz0 + wzm + 9);
    const glm::ivec3 bLo(std::max(0, wx0 - 9), 0, std::max(0, wz0 - 9));
    const glm::ivec3 bHi(wx0 + wxm + 9, topY + 30, wz0 + wzm + 9);
    return probe.reachable(start, goalLo, goalHi, bLo, bHi);
}

}  // namespace

// A 1x1 well cannot hold a walkable straight flight for a full story rise
// (fitFlight: run 9 micro / min tread 2 => max 4 treads; rise 30 micro needs 8).
// Today the realizer cuts the hole anyway and builds no treads — a FALL SHAFT —
// and reports ok. After M2 the shell must refuse (no walkable stair exists and
// the same well cannot repair: switchback needs width >= 2).
TEST(StairFallShaft, FailedStraightPlanMustNotRealizeSilentlyOk) {
    BuildingProgram p = twoStory(7, 9, /*well*/ {2, 3, 1, 1}, "straight");
    auto sh = StructureRealizer::realizeShell(p, stairStyle());
    EXPECT_FALSE(sh.ok)
        << "a 2-story shell whose only stair cannot be planned realized 'ok' — "
           "this is the silent fall-shaft/no-stair defect (StairPlan::ok never checked)";
}

// A width-1 well cannot hold a switchback (two lanes), but a STRAIGHT flight
// fits the same 1x2 well (run 18 micro, rise 30: 9 treads, riser 4 <= step-up).
// Today: silent no-stair (empty solids, degenerate hole), upper story sealed,
// yet a StairRecord is still pushed. After M2: the realizer retries the
// alternate form in the same well and the agent can climb it.
TEST(StairFallShaft, UnfitSwitchbackRepairsToStraightInSameWell) {
    const Rect well{2, 3, 1, 2};
    BuildingProgram p = twoStory(7, 9, well, "switchback");
    auto sh = StructureRealizer::realizeShell(p, stairStyle());
    ASSERT_TRUE(sh.ok) << sh.error;
    EXPECT_TRUE(topReachable(sh, well))
        << "switchback didn't fit the 1x2 well and nothing repaired it — upper story "
           "is sealed while the plan records a stair";
}

// Guard invariant (placer 12 F4): on the upper floor, every micro column of the
// stairwell perimeter must either present a small drop (<= 4 micro step-up —
// e.g. the emergence tread at floor level) or carry a guard solid within 8
// micro (~0.89 m; IRC R312 36 in guard, medieval variant tracked in
// GroundingGaps.md) above the floor surface. Today there are no guards at all.
TEST(StairFallShaft, StairwellPerimeterIsGuardedOnUpperFloor) {
    const Rect well{2, 3, 2, 4};
    BuildingProgram p = twoStory(7, 9, well, "switchback");
    auto sh = StructureRealizer::realizeShell(p, stairStyle());
    ASSERT_TRUE(sh.ok) << sh.error;
    ASSERT_GE(sh.floorTopByStory.size(), 2u);
    const int botMicro = sh.floorTopByStory[0];
    const int floor1   = sh.floorTopByStory[1];
    const int WM = well.w * 9, DM = well.d * 9;
    const int kStepUp = 4, kGuardBand = 8;

    int unguarded = 0;
    std::string firstBad;
    auto checkColumn = [&](int mx, int mz) {
        const int cx = well.x * 9 + mx, cz = well.z * 9 + mz;
        // highest solid below the upper floor surface in this well column
        int topSolid = botMicro - 1;
        for (int y = floor1 - 1; y >= botMicro; --y)
            if (sh.canvas.occupiedMicro(cx, y, cz)) { topSolid = y; break; }
        const int drop = floor1 - 1 - topSolid;
        if (drop <= kStepUp) return;                    // emergence/tread — safe
        for (int y = floor1; y < floor1 + kGuardBand; ++y)
            if (sh.canvas.occupiedMicro(cx, y, cz)) return;   // guarded
        ++unguarded;
        if (firstBad.empty())
            firstBad = "local(" + std::to_string(mx) + "," + std::to_string(mz) +
                       ") drop=" + std::to_string(drop);
    };
    for (int mx = 0; mx < WM; ++mx) { checkColumn(mx, 0); checkColumn(mx, DM - 1); }
    for (int mz = 1; mz < DM - 1; ++mz) { checkColumn(0, mz); checkColumn(WM - 1, mz); }

    EXPECT_EQ(unguarded, 0)
        << unguarded << " unguarded stairwell-perimeter columns with a > step-up drop "
           "on the upper floor (first: " << firstBad << ") — the shaft has no balustrade";
}
