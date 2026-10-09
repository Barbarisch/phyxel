#include <gtest/gtest.h>

#include "core/water/WaterSpanEdit.h"

// WaterCore Phase D slice D2 (docs/WaterCore.md 16.3): edits never create water. The pure column
// rules: a placed solid displaces (debited, never re-minted); a removed floor voxel lets the run
// fall one voxel (mass exact); any other dig changes nothing; a run clipped into a non-resident
// vertical chunk holds the edit.

namespace Phyxel::Core::Water {
namespace {

WorldSpan span(int x, int z, float lo, float hi) { WorldSpan s; s.x = x; s.z = z; s.bottomY = lo; s.topY = hi; return s; }

TEST(WaterSpanEditTest, PlacedSolidDisplacesNeverCreates) {
    std::vector<WorldSpan> spans{span(0, 0, 10.0f, 12.4f), span(1, 0, 10.0f, 12.4f)};
    // a cube into the middle of column 0: the run splits, 1 m^3 is displaced
    SpanEditResult r = spanSolidPlaced(spans, 0, 11, 0);
    EXPECT_TRUE(r.changed);
    EXPECT_NEAR(r.displaced, 1.0, 1e-6);
    ASSERT_EQ(spans.size(), 3u);
    EXPECT_FLOAT_EQ(spans[0].bottomY, 10.0f); EXPECT_FLOAT_EQ(spans[0].topY, 11.0f);
    EXPECT_FLOAT_EQ(spans[1].bottomY, 12.0f); EXPECT_FLOAT_EQ(spans[1].topY, 12.4f);
    EXPECT_EQ(spans[2].x, 1) << "the other column is untouched";
    double total = 0.0; for (const auto& s : spans) total += s.topY - s.bottomY;
    EXPECT_NEAR(total, 2 * 2.4 - 1.0, 1e-6) << "nothing created: total = before - displaced";
    // a cube into the partial top cell of column 1 displaces exactly the 0.4 it held
    r = spanSolidPlaced(spans, 1, 12, 0);
    EXPECT_NEAR(r.displaced, 0.4, 1e-5);
    // a cube above the water (air) changes nothing
    r = spanSolidPlaced(spans, 1, 20, 0);
    EXPECT_FALSE(r.changed); EXPECT_EQ(r.displaced, 0.0);
}

TEST(WaterSpanEditTest, DugCellBelowRunShiftsItDown) {
    std::vector<WorldSpan> spans{span(0, 0, 10.0f, 12.4f)};
    const SpanEditResult r = spanSolidRemoved(spans, 0, 9, 0);   // the floor voxel under the run
    EXPECT_TRUE(r.changed);
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_FLOAT_EQ(spans[0].bottomY, 9.0f); EXPECT_FLOAT_EQ(spans[0].topY, 11.4f) << "mass exact: the water fell one voxel";
    EXPECT_EQ(r.displaced, 0.0);
}

TEST(WaterSpanEditTest, PitAwayFromWaterStaysDry) {
    std::vector<WorldSpan> spans{span(0, 0, 10.0f, 12.4f)};
    const auto before = spans;
    EXPECT_FALSE(spanSolidRemoved(spans, 0, 5, 0).changed) << "a pit two voxels under the floor";
    EXPECT_FALSE(spanSolidRemoved(spans, 3, 9, 0).changed) << "a pit in a dry column beside the pool";
    EXPECT_FALSE(spanSolidRemoved(spans, 0, 14, 0).changed) << "air above the water";
    ASSERT_EQ(spans.size(), before.size());
    EXPECT_FLOAT_EQ(spans[0].bottomY, before[0].bottomY); EXPECT_FLOAT_EQ(spans[0].topY, before[0].topY);
}

TEST(WaterSpanEditTest, HeldWhenARunCrossesIntoANonResidentChunk) {
    std::vector<WorldSpan> spans{span(0, 0, 28.0f, 40.0f)};   // crosses y = 32
    EXPECT_TRUE(spanEditHeld(spans, 0, 0, [](int cy) { return cy == 0; })) << "chunk y1 is absent: a shift-down of the lower clip alone would mint water";
    EXPECT_FALSE(spanEditHeld(spans, 0, 0, [](int) { return true; }));
    EXPECT_FALSE(spanEditHeld(spans, 5, 0, [](int) { return false; })) << "a column with no runs is never held";
}

}  // namespace
}  // namespace Phyxel::Core::Water
