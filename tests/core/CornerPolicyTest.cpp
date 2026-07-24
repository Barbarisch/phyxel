#include <gtest/gtest.h>

#include "core/CornerPolicy.h"
#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"
#include "core/BuildingProgram.h"

using namespace Phyxel::Core;

// ============================================================================
// Claims Ledger increment 4 (docs/structure-generation/ClaimsLedger.md): the
// KI-5a corner-margin rule moves out of addTypologyWindows into CornerPolicy —
// one definition, queried by the program-time window placer and pinned against
// the realize-time CornerZone claims. Refactor locks: (1) the policy band is
// numerically identical to the legacy inline rule across the whole input space;
// (2) every realized CornerZone lies INSIDE the zone the policy excludes from
// window placement (stages cannot drift); (3) the 260-case window census golden
// stays byte-identical (WindowCornerTest, unchanged).
// ============================================================================

namespace {

StyleProfile quoinStyle() {
    StyleProfileRegistry reg;
    reg.loadFromJson(nlohmann::json::parse(R"({
        "stone_quoins": {
            "roof_style": "gable", "foundation": "slab",
            "thickness": { "exterior_wall": 0.333, "interior_wall": 0.222,
                           "foundation_wall": 0.667, "floor": 0.333, "ceiling": 0.222 },
            "materials": { "structure": "StoneBricks", "floor": "Wood", "roof": "Wood",
                           "foundation": "Stone", "trim": "Sandstone" },
            "flags": { "quoins": true },
            "roof": { "pitch": 0.8 }
        }
    })"));
    return *reg.get("stone_quoins");
}

BuildingProgram quoinBuilding(int W, int D) {
    BuildingProgram p;
    p.name = "policy_quoins"; p.style = "stone_quoins";
    p.footprintW = W; p.footprintD = D; p.substructure = "slab";
    ProgStory st; st.height = 3;
    ProgRoom r; r.id = "hall"; r.rect = {0, 0, W, D}; r.purpose = "living";
    st.rooms.push_back(r);
    ProgPortal door; door.a = "exterior"; door.b = "hall";
    door.px = W / 2; door.pz = 0; door.width = 1; door.height = 2; door.kind = "door";
    st.portals.push_back(door);
    p.stories.push_back(st);
    return p;
}

} // namespace

// The policy band is the legacy inline rule, verbatim, across the whole input
// space the window placer can produce (edge spans within [0, axisMax], every
// combination of corner-touching and mid-wall ends).
TEST(CornerPolicyTest, WindowSafeBandMatchesLegacyRule) {
    int cases = 0;
    for (int axisMax = 3; axisMax <= 24; ++axisMax)
        for (int lo = 0; lo < axisMax; ++lo)
            for (int hi = lo + 1; hi <= axisMax; ++hi) {
                // Legacy inline rule (RoomLayout.cpp pre-increment-4, verbatim):
                const int legacyLo = (lo == 0) ? 1 : lo;
                const int legacyHi = (hi == axisMax) ? axisMax - 1 : hi;
                int sLo = 0, sHi = 0;
                CornerPolicy::windowSafeBand(lo, hi, axisMax, sLo, sHi);
                ASSERT_EQ(sLo, legacyLo) << "lo=" << lo << " hi=" << hi << " max=" << axisMax;
                ASSERT_EQ(sHi, legacyHi) << "lo=" << lo << " hi=" << hi << " max=" << axisMax;
                ++cases;
            }
    EXPECT_GT(cases, 2000) << "sweep degenerated";
}

// The bridging invariant: every CornerZone the realizer records (the quoin
// claims, increment 2) lies INSIDE the zone this policy excludes from window
// placement. If someone widens quoins beyond the margin, or shrinks the margin
// below the quoin zone, the stages have drifted and this fails.
TEST(CornerPolicyTest, RealizedCornerZonesLieInsidePolicyMargin) {
    for (const auto& dims : {std::pair<int,int>{8, 6}, std::pair<int,int>{15, 9}}) {
        const int W = dims.first, D = dims.second;
        auto r = StructureRealizer::realizeShell(quoinBuilding(W, D), quoinStyle());
        ASSERT_TRUE(r.ok) << r.error;
        ASSERT_EQ(r.plan.corners.size(), 4u) << W << "x" << D;

        int sLoX = 0, sHiX = 0, sLoZ = 0, sHiZ = 0;
        CornerPolicy::windowSafeBand(0, W, W, sLoX, sHiX);   // full x-axis edge
        CornerPolicy::windowSafeBand(0, D, D, sLoZ, sHiZ);   // full z-axis edge
        for (const auto& q : r.plan.corners) {
            EXPECT_TRUE(q.x < sLoX || q.x >= sHiX)
                << W << "x" << D << ": corner cube x=" << q.x
                << " is INSIDE the window-safe band [" << sLoX << "," << sHiX << ")";
            EXPECT_TRUE(q.z < sLoZ || q.z >= sHiZ)
                << W << "x" << D << ": corner cube z=" << q.z
                << " is INSIDE the window-safe band [" << sLoZ << "," << sHiZ << ")";
        }
    }
}
