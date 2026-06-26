#include <gtest/gtest.h>

#include "core/SettlementLayout.h"
#include "core/BuildingProgram.h"

using namespace Phyxel::Core;

// ============================================================================
// subdivide_plots (#40) — the FIRST settlement-composition slice. The leap from
// one building to a town: a settlement footprint tiles into non-overlapping plots
// separated by WALKABLE street corridors. L2 invariants: no overlap, fit, min
// size, and a real street gap between adjacent plots (the new thing — buildings
// must not abut with no street between them).
// ============================================================================

namespace {
bool overlaps(const Rect& a, const Rect& b) {
    return a.x < b.x1() && b.x < a.x1() && a.z < b.z1() && b.z < a.z1();
}
// gap between two non-overlapping rects along whichever axis they're separated on (min of x/z gaps).
int separation(const Rect& a, const Rect& b) {
    int dx = std::max(0, std::max(a.x, b.x) - std::min(a.x1(), b.x1()));
    int dz = std::max(0, std::max(a.z, b.z) - std::min(a.z1(), b.z1()));
    return std::max(dx, dz);   // adjacent plots share one axis-band; the street is the gap on the other
}
} // namespace

// THE new invariant (red on the missing-street-offset stub): adjacent plots must be separated by a
// street >= streetWidth — buildings can't abut with no road between them.
TEST(SettlementLayoutTest, AdjacentPlotsSeparatedByStreet) {
    const int sw = 4;
    auto s = subdividePlots(40, 40, 2, 2, sw, 6);
    ASSERT_EQ(s.plots.size(), 4u);
    for (size_t i = 0; i < s.plots.size(); ++i)
        for (size_t j = i + 1; j < s.plots.size(); ++j) {
            EXPECT_FALSE(overlaps(s.plots[i].rect, s.plots[j].rect)) << "plots " << i << "," << j << " overlap";
            // plots sharing a row or column are adjacent -> must have a street between them
            const bool sameRow = s.plots[i].row == s.plots[j].row;
            const bool sameCol = s.plots[i].col == s.plots[j].col;
            if (sameRow || sameCol)
                EXPECT_GE(separation(s.plots[i].rect, s.plots[j].rect), sw)
                    << "adjacent plots " << i << "," << j << " have no street between them";
        }
}

// Every plot meets the minimum building size.
TEST(SettlementLayoutTest, EveryPlotMeetsMinSize) {
    auto s = subdividePlots(50, 50, 3, 3, 4, 6);
    ASSERT_FALSE(s.plots.empty());
    for (const auto& p : s.plots) { EXPECT_GE(p.rect.w, 6); EXPECT_GE(p.rect.d, 6); }
}

// All plots fit inside the settlement footprint (no plot pokes out past W×D).
TEST(SettlementLayoutTest, PlotsFitTheFootprint) {
    const int W = 40, D = 30;
    auto s = subdividePlots(W, D, 2, 2, 4, 6);
    ASSERT_FALSE(s.plots.empty());
    for (const auto& p : s.plots) {
        EXPECT_GE(p.rect.x, 0); EXPECT_LE(p.rect.x1(), W);
        EXPECT_GE(p.rect.z, 0); EXPECT_LE(p.rect.z1(), D);
    }
}

// Too-dense request (plots would be below minPlot) returns EMPTY so the caller can reduce density,
// rather than emitting unbuildable sliver plots.
TEST(SettlementLayoutTest, TooDenseReturnsEmpty) {
    // 20×20, 5×5 plots, street 4 -> plotW = (20 - 6*4)/5 < 0 -> empty
    auto s = subdividePlots(20, 20, 5, 5, 4, 6);
    EXPECT_TRUE(s.plots.empty());
}
