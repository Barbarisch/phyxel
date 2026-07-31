#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <vector>

#include "core/FenceBuilder.h"
#include "core/DimensionCanon.h"

using namespace Phyxel::Core;

// ============================================================================
// Fence profile — a fence is a THIN, grounded, TYPED structure, not a 1 m cube wall. planFenceProfile,
// fed the canon dims (fence_picket/privacy/post_rail), must come out ~0.9 m tall and ~0.1 m THICK with
// posts at the grounded spacing — and the type must change the infill (picket gaps vs privacy solid).
// The thinness assertion is the one that fails the old 1-cube-Log placeholder.
// ============================================================================

namespace {
// the real canon values (cubes), mirrored hermetically so the test isn't CWD-dependent. These MUST
// match resources/object_dimensions.json (fence_picket 0.9/1.8/2, privacy 1.8/2.4, post_rail 1.2/2.7/2).
DimensionCanonRegistry canon() {
    DimensionCanonRegistry r;
    r.loadFromJson(nlohmann::json::parse(R"({
      "fence_picket":   {"category":"fence","height":0.9,"post_spacing":1.8,"rails":2,"tol":0.15},
      "fence_privacy":  {"category":"fence","height":1.8,"post_spacing":2.4,"tol":0.15},
      "fence_post_rail":{"category":"fence","height":1.2,"post_spacing":2.7,"rails":2,"tol":0.15}
    })"));
    return r;
}
int micro(double cubes) { return static_cast<int>(cubes * 9.0 + 0.5); }
bool cellAt(const FenceProfile& p, int u, int y) {
    for (const auto& c : p.cells) if (c.u == u && c.y == y) return true;
    return false;
}
int columnsAtHeight(const FenceProfile& p, int y, int runLen) {
    std::set<int> us; for (const auto& c : p.cells) if (c.y == y && c.u >= 0 && c.u < runLen) us.insert(c.u);
    return static_cast<int>(us.size());
}
// thickness MEASURED from the emitted cells (across-run extent), not the echoed thickMicro field.
int measuredThickness(const FenceProfile& p) {
    int maxW = -1; for (const auto& c : p.cells) maxW = std::max(maxW, c.w);
    return maxW + 1;
}
}  // namespace

// A picket fence is THIN and ~0.9 m tall with posts at ~1.8 m — measured against the canon.
TEST(FenceProfileTest, PicketIsThinAndGroundedToCanon) {
    const DimensionCanonRegistry reg = canon();
    const ArchetypeDims* pk = reg.get("fence_picket");
    ASSERT_NE(pk, nullptr);
    const int h = micro(pk->value("height")), sp = micro(pk->value("post_spacing"));
    const int runLen = 27;  // 3 m
    const FenceProfile p = planFenceProfile(runLen, h, sp, static_cast<int>(pk->value("rails")),
                                            FenceType::Picket);
    ASSERT_TRUE(p.ok);

    // THE point: a fence is THIN, not a 1 m (9-micro) cube wall.
    // Measured from the EMITTED cells (max c.w extent), NOT the echoed p.thickMicro field —
    // an impl that emits w=0..8 while leaving p.thickMicro=1 must still fail this.
    const int actualThick = measuredThickness(p);
    EXPECT_LE(actualThick, 2) << "fence is " << actualThick << " micro thick (measured) — a wall, not a fence";
    EXPECT_LT(actualThick, 9) << "fence is a full cube thick (measured)";
    EXPECT_EQ(p.thickMicro, actualThick) << "reported thickMicro disagrees with the emitted geometry";
    // grounded height (~0.9 m), not 1 m or 1.6 m
    EXPECT_EQ(p.heightMicro, h) << "picket height not grounded to the canon (0.9 m)";
    EXPECT_EQ(h, 8);  // sanity: 0.9 m -> 8 micro

    // posts at the grounded spacing: full-height columns at u=0 and u≈post_spacing
    EXPECT_TRUE(cellAt(p, 0, 0) && cellAt(p, 0, h - 1)) << "no post at the run start";
    EXPECT_TRUE(cellAt(p, sp, 0) && cellAt(p, sp, h - 1)) << "no post at one post_spacing";
}

// TEETH for the thinness assertion: if a profile is built thick (thickMicro=9, a cube wall), the
// measured-from-cells thickness MUST come out 9 and trip the ≤2 bar. This is the red case the thin
// assertion above is guarding against — proving that assertion can actually fail, not just echo "1".
TEST(FenceProfileTest, ThickProfileMeasuresThick_SoThinBarHasTeeth) {
    const FenceProfile wall = planFenceProfile(27, 8, 16, 2, FenceType::Picket, /*thickMicro=*/9);
    ASSERT_TRUE(wall.ok);
    const int actualThick = measuredThickness(wall);
    EXPECT_EQ(actualThick, 9) << "a 9-thick profile didn't emit cells out to w=8";
    EXPECT_GT(actualThick, 2) << "the thin bar can't distinguish a wall from a fence";
    EXPECT_EQ(wall.thickMicro, actualThick) << "reported thickMicro disagrees with emitted geometry";
}

// Type changes the infill: picket has GAPS (spaced slats), privacy is SOLID close boards.
TEST(FenceProfileTest, PicketHasGapsPrivacyIsSolid) {
    const int runLen = 27, mid = 4;
    const DimensionCanonRegistry reg = canon();
    const ArchetypeDims* pk = reg.get("fence_picket");
    const ArchetypeDims* pv = reg.get("fence_privacy");
    ASSERT_NE(pk, nullptr); ASSERT_NE(pv, nullptr);
    const FenceProfile picket  = planFenceProfile(runLen, micro(pk->value("height")), micro(pk->value("post_spacing")),
                                                  2, FenceType::Picket);
    const FenceProfile privacy = planFenceProfile(runLen, micro(pv->value("height")), micro(pv->value("post_spacing")),
                                                  0, FenceType::Privacy);
    ASSERT_TRUE(picket.ok && privacy.ok);

    EXPECT_LT(columnsAtHeight(picket, mid, runLen), runLen) << "picket has no gaps — it's a solid panel";
    EXPECT_EQ(columnsAtHeight(privacy, mid, runLen), runLen) << "privacy fence isn't solid (has gaps)";
}

// Post-and-rail is OPEN: between posts there are only rails, not full columns.
TEST(FenceProfileTest, PostRailIsOpenBetweenPosts) {
    const int runLen = 30;
    const DimensionCanonRegistry reg = canon();
    const ArchetypeDims* pr = reg.get("fence_post_rail");
    ASSERT_NE(pr, nullptr);
    const FenceProfile p = planFenceProfile(runLen, micro(pr->value("height")), micro(pr->value("post_spacing")),
                                            static_cast<int>(pr->value("rails")), FenceType::PostRail);
    ASSERT_TRUE(p.ok);
    // a u midway between posts (not a post column) has cells ONLY at rail heights, not a full column.
    const int sp = micro(pr->value("post_spacing")), midU = sp / 2;
    EXPECT_FALSE(cellAt(p, midU, 0)) << "post-rail has a full column between posts (not open)";
    int railsThere = 0; for (int y = 0; y < p.heightMicro; ++y) if (cellAt(p, midU, y)) ++railsThere;
    EXPECT_GE(railsThere, 1) << "no rail spanning between posts";
    EXPECT_LE(railsThere, 3) << "between posts should be only rails, not a filled column";
}

// Corner cleanliness (the deterministic ground-truth for the user's "fences don't meet to a clean
// corner — stop short / overshoot" report). A picket fence's density can't distinguish a post from a
// slat, so this is validated on the GENERATOR (fencePostPositions), not a world scan: posts must be
// EVENLY spaced with no two adjacent (a doubled corner), and endPosts=false must omit the end columns
// so two perpendicular runs meet at ONE shared corner post.
TEST(FenceProfileTest, PostPositionsEvenAndCornerShared) {
    // SCAN the property across every realistic parcel-edge length (2..40 cubes) and all three canon
    // post spacings — not one hand-picked size — so the anti-doubling + shared-corner invariants are
    // pinned everywhere fencePostPositions is actually invoked. (The old sweep+end-post algorithm
    // produced minGap=0 doubled posts at run lengths that are exact spacing multiples; even
    // distribution + the >=1-cube gap cap must never do so.)
    for (double spacingM : {1.8, 2.4, 2.7}) {
        const int spacing = micro(spacingM);
        for (int cubes = 2; cubes <= 40; ++cubes) {
            const int runLen = micro(static_cast<double>(cubes));
            const int span = runLen - 1;

            std::vector<int> owned = fencePostPositions(runLen, spacing, /*endPosts=*/true);
            ASSERT_GE(owned.size(), 2u) << "cubes=" << cubes << " spacing=" << spacingM;
            EXPECT_EQ(owned.front(), 0)    << "must post the START corner (cubes=" << cubes << ")";
            EXPECT_EQ(owned.back(),  span) << "must post the END corner (cubes=" << cubes << ")";
            int minGap = span, maxGap = 0;
            for (size_t i = 1; i < owned.size(); ++i) {
                const int g = owned[i] - owned[i - 1];
                minGap = std::min(minGap, g);
                maxGap = std::max(maxGap, g);
            }
            EXPECT_GT(minGap, 9)
                << "two posts within one cube — doubled/messy corner (cubes=" << cubes
                << " spacing=" << spacingM << ")";
            EXPECT_LE(maxGap, spacing + 1)
                << "gap wider than the post spacing — sagging run (cubes=" << cubes << ")";

            // The perpendicular run OMITS its end columns, so the shared corner carries ONE post.
            std::vector<int> shared = fencePostPositions(runLen, spacing, /*endPosts=*/false);
            for (int u : shared) {
                EXPECT_GT(u, 0)    << "endPosts=false posted the start corner (cubes=" << cubes << ")";
                EXPECT_LT(u, span) << "endPosts=false posted the end corner (cubes=" << cubes << ")";
            }
        }
    }
}

// ---------------------------------------------------------------------------
// fenceGateWindow — the gate cut, extracted so the stamper and the walkability
// validator share ONE definition instead of two copies that were identical only
// by luck. Extraction can silently change edge cases, so pin the contract:
// cube-ALIGNED centring (a naive micro formula drifts up to 4 micro off centre on
// odd spans -- previously auditor-caught), and honest refusal on a run too short
// to hold a gate rather than emitting a negative window.
// ---------------------------------------------------------------------------
TEST(FenceProfileTest, GateWindowIsCubeAlignedAndCentred) {
    int lo = -1, hi = -1;
    // A 9-cube run: runLenMicro = (9-1)*9 + 1 = 73. Gate 2 cubes -> centred at cube 3.
    ASSERT_TRUE(Phyxel::Core::fenceGateWindow(73, 2, lo, hi));
    EXPECT_EQ(lo % 9, 0) << "gate start is not cube-aligned (lo=" << lo << ")";
    EXPECT_EQ(hi - lo, 2 * 9) << "gate is not the requested 2 cubes wide";
    const int cubeSpan = (73 + 8) / 9;
    EXPECT_EQ(lo, ((cubeSpan - 2) / 2) * 9) << "gate is not centred in cubes";

    // Odd span: still cube-aligned, still exactly gateWidth wide.
    lo = hi = -1;
    ASSERT_TRUE(Phyxel::Core::fenceGateWindow(64, 2, lo, hi));
    EXPECT_EQ(lo % 9, 0);
    EXPECT_EQ(hi - lo, 18);
}

TEST(FenceProfileTest, GateWindowRefusesARunTooShortToHoldAGate) {
    int lo = -7, hi = -7;
    EXPECT_FALSE(Phyxel::Core::fenceGateWindow(10, 3, lo, hi))
        << "a 2-cube run reported room for a 3-cube gate";
    EXPECT_EQ(lo, -7) << "a refused gate must leave the caller's window untouched, not write a "
                         "negative span the stamper would read as 'skip from the start'";
    EXPECT_EQ(hi, -7);
    EXPECT_FALSE(Phyxel::Core::fenceGateWindow(0, 2, lo, hi));
    EXPECT_FALSE(Phyxel::Core::fenceGateWindow(73, 0, lo, hi));
}

// The smallest run the parcel stamper can actually produce (a 2-cube side) must
// still yield a gate -- otherwise extracting this helper would silently SEAL the
// smallest parcels and the walkability suite would be the only thing to notice.
TEST(FenceProfileTest, TheSmallestStampableRunStillGetsItsGate) {
    int lo = -1, hi = -1;
    ASSERT_TRUE(Phyxel::Core::fenceGateWindow(10, 2, lo, hi))   // 2-cube side: (2-1)*9+1 = 10
        << "the minimum parcel side lost its gate - every small plot would be sealed";
    EXPECT_EQ(lo, 0);
    EXPECT_EQ(hi, 18);
}
