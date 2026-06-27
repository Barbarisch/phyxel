#include <gtest/gtest.h>

#include <set>

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
