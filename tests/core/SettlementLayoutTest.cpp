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
// >= streetWidth GEOMETRIC GAP — buildings can't abut with no road between them. (L2: gap is wide
// enough on paper; walking it with a TraversalProbe is the deferred L3 slice, not asserted here.)
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

// The emitted street[] corridors (the artifact the L3 walkability slice will probe) are well-formed:
// one band per grid line, each >= streetWidth wide, spanning the full perpendicular extent, and NOT
// overlapping any plot (streets live in the gaps, not under buildings).
TEST(SettlementLayoutTest, StreetsCoverTheGapsAndDontOverlapPlots) {
    const int W = 40, D = 40, cols = 2, rows = 2, sw = 4;
    auto s = subdividePlots(W, D, cols, rows, sw, 6);
    ASSERT_FALSE(s.plots.empty());
    EXPECT_EQ(s.streets.size(), static_cast<size_t>((cols + 1) + (rows + 1)));
    for (const auto& st : s.streets) {
        const bool vertical = (st.d == D);     // vertical band spans full depth
        const bool horizontal = (st.w == W);   // horizontal band spans full width
        EXPECT_TRUE(vertical || horizontal) << "a street doesn't span the footprint";
        EXPECT_GE(std::min(st.w, st.d), sw) << "a street is narrower than streetWidth";
    }
    for (const auto& st : s.streets)
        for (const auto& p : s.plots)
            EXPECT_FALSE(overlaps(st, p.rect)) << "a street runs under a plot";
}

// Too-dense request (plots would be below minPlot) returns EMPTY so the caller can reduce density,
// rather than emitting unbuildable sliver plots.
TEST(SettlementLayoutTest, TooDenseReturnsEmpty) {
    // 20×20, 5×5 plots, street 4 -> plotW = (20 - 6*4)/5 < 0 -> empty
    auto s = subdividePlots(20, 20, 5, 5, 4, 6);
    EXPECT_TRUE(s.plots.empty());
}

// populate_plots: each building is INSET from its plot by the yard setback (red on the no-inset stub)
// — a building can't fill the plot edge-to-edge with no yard for a path/garden between it and the road.
TEST(SettlementLayoutTest, BuildingInsetFromPlotByYard) {
    const int setback = 2;
    auto s = subdividePlots(60, 60, 2, 2, 4, 8);
    auto bs = populatePlots(s, setback, 6, "hall_house");
    ASSERT_EQ(bs.size(), 4u);
    for (const auto& b : bs) {
        const Rect& plot = s.plots[b.plotIndex].rect;
        EXPECT_GE(b.footprint.x, plot.x + setback)   << "no yard on the -x side";
        EXPECT_LE(b.footprint.x1(), plot.x1() - setback) << "no yard on the +x side";
        EXPECT_GE(b.footprint.z, plot.z + setback)   << "no yard on the -z side";
        EXPECT_LE(b.footprint.z1(), plot.z1() - setback) << "no yard on the +z side";
    }
}

// Buildings fit their plots, don't overlap, carry the typology, and tiny plots are skipped.
TEST(SettlementLayoutTest, BuildingsFitDontOverlapAndCarryTypology) {
    auto s = subdividePlots(60, 60, 2, 2, 4, 8);
    auto bs = populatePlots(s, 2, 6, "hall_house");
    ASSERT_FALSE(bs.empty());
    for (const auto& b : bs) {
        const Rect& plot = s.plots[b.plotIndex].rect;
        EXPECT_GE(b.footprint.x, plot.x); EXPECT_LE(b.footprint.x1(), plot.x1());
        EXPECT_GE(b.footprint.z, plot.z); EXPECT_LE(b.footprint.z1(), plot.z1());
        EXPECT_EQ(b.typology, "hall_house");
    }
    for (size_t i = 0; i < bs.size(); ++i)
        for (size_t j = i + 1; j < bs.size(); ++j)
            EXPECT_FALSE(overlaps(bs[i].footprint, bs[j].footprint)) << "buildings overlap";
}

// A plot too small for a building + yard is skipped (no sliver buildings).
TEST(SettlementLayoutTest, TinyPlotSkipped) {
    auto s = subdividePlots(40, 40, 2, 2, 4, 6);   // plots ~14x14
    auto bs = populatePlots(s, 5, 8, "hall_house"); // 14 - 2*5 = 4 < minBuilding 8 -> all skipped
    EXPECT_TRUE(bs.empty());
}

// STRESS (Phase 0): the invariants must hold AT SCALE, not just N=4. A 6x6 grid = 36 plots / 36
// buildings — assert EVERY plot tiles without overlap, every building fits + is inset, no pair
// overlaps. (The stress-test discipline: push N to the extreme, assert the invariant at every step.)
TEST(SettlementLayoutTest, ScaleSixBySixAllInvariantsHold) {
    const int cols = 6, rows = 6, sw = 4, setback = 2, minPlot = 8, minBuilding = 6;
    // size the footprint so 6x6 plots of >= minPlot fit: W = cols*plotW + (cols+1)*sw, plotW ~12
    const int W = cols * 12 + (cols + 1) * sw, D = rows * 12 + (rows + 1) * sw;
    auto s = subdividePlots(W, D, cols, rows, sw, minPlot);
    ASSERT_EQ(s.plots.size(), static_cast<size_t>(cols * rows)) << "lost plots at scale";
    auto bs = populatePlots(s, setback, minBuilding, "hall_house");
    ASSERT_EQ(bs.size(), static_cast<size_t>(cols * rows)) << "lost buildings at scale";
    // every building fits + is inset by the yard on ALL FOUR sides
    for (const auto& b : bs) {
        const Rect& plot = s.plots[b.plotIndex].rect;
        EXPECT_GE(b.footprint.x, plot.x + setback);
        EXPECT_LE(b.footprint.x1(), plot.x1() - setback);
        EXPECT_GE(b.footprint.z, plot.z + setback);
        EXPECT_LE(b.footprint.z1(), plot.z1() - setback);
        EXPECT_GE(b.footprint.w, minBuilding); EXPECT_GE(b.footprint.d, minBuilding);
        EXPECT_LE(b.footprint.x1(), W); EXPECT_LE(b.footprint.z1(), D);
    }
    // NO pair of the 36 buildings overlaps (entailed by inset+non-overlapping plots, but cheap)
    for (size_t i = 0; i < bs.size(); ++i)
        for (size_t j = i + 1; j < bs.size(); ++j)
            ASSERT_FALSE(overlaps(bs[i].footprint, bs[j].footprint))
                << "buildings " << i << "," << j << " overlap at scale";
    // THE scale-specific invariant: every adjacent plot pair still has a >= streetWidth street AT
    // SCALE — a per-column stride off-by-one would narrow streets only at larger grids (auditor caught
    // that the no-overlap check alone passes such a bug, since abutting != overlapping).
    for (size_t i = 0; i < s.plots.size(); ++i)
        for (size_t j = i + 1; j < s.plots.size(); ++j)
            if (s.plots[i].row == s.plots[j].row || s.plots[i].col == s.plots[j].col)
                EXPECT_GE(separation(s.plots[i].rect, s.plots[j].rect), sw)
                    << "adjacent plots " << i << "," << j << " have no street at scale";
}

// setback=0 is allowed (urban row-house: building flush to the plot edge, NO yard). The scope-honest
// boundary: "no yard" must NOT mean "in the street" — a flush building still can't overlap a street
// corridor (the plot starts a streetWidth in). (Auditor flagged the "guaranteed yard" overclaim.)
TEST(SettlementLayoutTest, ZeroSetbackFlushButClearOfStreet) {
    auto s = subdividePlots(40, 40, 2, 2, 4, 6);
    auto bs = populatePlots(s, 0, 6, "hall_house");
    ASSERT_FALSE(bs.empty());
    for (const auto& b : bs)
        for (const auto& st : s.streets)
            EXPECT_FALSE(overlaps(b.footprint, st)) << "a flush (setback 0) building sits in a street";
}

// Composed at a world origin, the buildings' world footprints don't overlap — the settlement-
// composition invariant the runtime hamlet relies on (translation preserves the layout's non-overlap).
// NB: this asserts the LAYOUT math; that the realizer stays WITHIN each footprint at runtime is
// runtime-observed (the placed bboxes), not asserted here — an integration test is the open gap.
TEST(SettlementLayoutTest, ComposedWorldFootprintsDontOverlap) {
    const int ox = 10, oz = 10;
    auto s = subdividePlots(52, 36, 2, 2, 4, 6);
    auto bs = populatePlots(s, 2, 6, "hall_house");
    ASSERT_EQ(bs.size(), 4u);
    std::vector<Rect> world;
    for (const auto& b : bs) { Rect w = b.footprint; w.x += ox; w.z += oz; world.push_back(w); }
    for (size_t i = 0; i < world.size(); ++i)
        for (size_t j = i + 1; j < world.size(); ++j)
            EXPECT_FALSE(overlaps(world[i], world[j])) << "composed buildings " << i << "," << j << " overlap";
}
