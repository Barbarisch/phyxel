#include <gtest/gtest.h>

#include "core/PriorityFlood.h"

#include <cstdio>
#include <vector>

// L2 validation for the Priority-Flood hydrology primitive (docs/TerrainGenerationV2.md §2b, P2.1).
// Asserts the depression-fill invariants on real output: filled >= elevation, flat lake surfaces at
// the rim spill, monotonic drainage to the border, and no over/under-filling.

namespace Phyxel {
namespace {

TEST(PriorityFloodTest, FlatFieldIsUnchanged) {
    std::vector<float> e(5 * 5, 7.0f);
    auto f = PriorityFlood::fill(e, 5, 5);
    for (size_t i = 0; i < e.size(); ++i) EXPECT_FLOAT_EQ(f[i], 7.0f);
}

TEST(PriorityFloodTest, MonotonicSlopeDrainsWithoutFilling) {
    // elevation = x (rises to the right); everything drains to the low x=0 edge → no depressions.
    const int w = 8, h = 5;
    std::vector<float> e(w * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) e[y * w + x] = static_cast<float>(x);
    auto f = PriorityFlood::fill(e, w, h);
    for (size_t i = 0; i < e.size(); ++i) EXPECT_FLOAT_EQ(f[i], e[i]) << "slope should not fill";
}

TEST(PriorityFloodTest, PitFillsExactlyToItsRim) {
    // 5×5: a 9-ring encloses a pit=1; the top border has a gap of 4. The pit must fill to the RIM
    // (9), not to the outer border (20) and not stay at 1. Only the center should change.
    const int w = 5, h = 5;
    std::vector<float> e = {
        20, 20,  4, 20, 20,
        20,  9,  9,  9, 20,
        20,  9,  1,  9, 20,
        20,  9,  9,  9, 20,
        20, 20, 20, 20, 20,
    };
    auto f = PriorityFlood::fill(e, w, h);
    auto at = [&](int x, int y) { return f[y * w + x]; };
    EXPECT_FLOAT_EQ(at(2, 2), 9.0f) << "pit filled to rim (9), not border (20) or unfilled (1)";
    EXPECT_FLOAT_EQ(at(2, 0), 4.0f) << "border gap must be untouched";
    EXPECT_FLOAT_EQ(at(1, 1), 9.0f) << "rim stays at rim height";
    // Nothing else moved: every non-center cell equals its input.
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (!(x == 2 && y == 2)) EXPECT_FLOAT_EQ(f[y * w + x], e[y * w + x]) << "moved at (" << x << "," << y << ")";

    auto depth = PriorityFlood::waterDepth(e, w, h);
    EXPECT_FLOAT_EQ(depth[2 * w + 2], 8.0f) << "lake depth = rim - floor = 9 - 1";
    for (size_t i = 0; i < depth.size(); ++i)
        if (i != static_cast<size_t>(2 * w + 2)) EXPECT_FLOAT_EQ(depth[i], 0.0f) << "spurious water at " << i;
}

TEST(PriorityFloodTest, LakeSurfaceIsFlat) {
    // A wide basin (many low cells) must fill to a SINGLE flat level, not a bumpy one.
    const int w = 6, h = 6;
    std::vector<float> e(w * h, 30.0f);            // high border/plateau
    for (int y = 1; y < h - 1; ++y)
        for (int x = 1; x < w - 1; ++x) e[y * w + x] = static_cast<float>((x + y) % 3);  // bumpy floor 0..2
    auto f = PriorityFlood::fill(e, w, h);
    float lvl = f[1 * w + 1];
    for (int y = 1; y < h - 1; ++y)
        for (int x = 1; x < w - 1; ++x)
            EXPECT_FLOAT_EQ(f[y * w + x], lvl) << "lake surface not flat at (" << x << "," << y << ")";
    EXPECT_FLOAT_EQ(lvl, 30.0f) << "fully-enclosed basin fills to its rim (the 30 plateau)";
}

TEST(PriorityFloodTest, SeaOutletDrainsSubSeaCellsInsteadOfBrimming) {
    // 5×5: a 50 plateau + 10 ring enclose a sub-sea pit of -20. sea = 0.
    // Border-only fill: the whole interior is enclosed → brims to the rim (50).
    // Sea-outlet fill: the pit is <= sea, so it's an OUTLET (ocean drains out) → stays -20, and the
    // 10-ring drains down to it → stays 10. This is the difference the sea outlet makes.
    const int w = 5, h = 5;
    std::vector<float> e = {
        50, 50,  50, 50, 50,
        50, 10,  10, 10, 50,
        50, 10, -20, 10, 50,
        50, 10,  10, 10, 50,
        50, 50,  50, 50, 50,
    };
    auto fb = PriorityFlood::fill(e, w, h);           // border outlets only
    auto fs = PriorityFlood::fill(e, w, h, 0.0f);     // + sea outlet at 0
    EXPECT_FLOAT_EQ(fb[2 * w + 2], 50.0f) << "border-only should brim the enclosed pit to the rim";
    EXPECT_FLOAT_EQ(fs[2 * w + 2], -20.0f) << "sea outlet leaves the sub-sea pit draining (unfilled)";
    EXPECT_FLOAT_EQ(fb[2 * w + 1], 50.0f) << "border-only brims the ring too";
    EXPECT_FLOAT_EQ(fs[2 * w + 1], 10.0f) << "ring drains to the sub-sea outlet, not brimmed";
}

TEST(PriorityFloodTest, FilledNeverBelowElevationAndDrains) {
    // Random-ish rugged field: (1) filled >= elevation everywhere; (2) every non-border cell has a
    // neighbor at <= its filled level (a non-increasing path to the border always exists).
    const int w = 12, h = 10;
    std::vector<float> e(w * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            e[y * w + x] = static_cast<float>(((x * 7 + y * 13 + x * y) % 17));  // deterministic bumps
    auto f = PriorityFlood::fill(e, w, h);
    for (size_t i = 0; i < e.size(); ++i) ASSERT_GE(f[i], e[i]) << "filled below elevation at " << i;

    const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    for (int y = 1; y < h - 1; ++y)
        for (int x = 1; x < w - 1; ++x) {
            float cur = f[y * w + x];
            bool hasLowerOrEqual = false;
            for (int d = 0; d < 4; ++d) {
                int nx = x + dx[d], ny = y + dy[d];
                if (f[ny * w + nx] <= cur + 1e-4f) { hasLowerOrEqual = true; break; }
            }
            EXPECT_TRUE(hasLowerOrEqual) << "no drainage path from (" << x << "," << y << ")";
        }
}

TEST(PriorityFloodTest, FillWithFlowGivesValidDownstream) {
    // Every cell's downstream must (a) drain to a filled level <= its own (never uphill), and
    // (b) reach an outlet (a self-loop) within n steps (no cycles). Outlets point to themselves.
    const int w = 12, h = 10;
    std::vector<float> e(w * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) e[y * w + x] = static_cast<float>(((x * 7 + y * 13 + x * y) % 17));
    auto fr = PriorityFlood::fillWithFlow(e, w, h, -1000.0f);  // no ocean → border outlets
    ASSERT_EQ(static_cast<int>(fr.downstream.size()), w * h);
    for (int c = 0; c < w * h; ++c) {
        int d = fr.downstream[c];
        EXPECT_GE(fr.filled[c] + 1e-4f, fr.filled[d]) << "downstream is uphill at " << c;
        // Walk to an outlet (self-loop) — must terminate (no cycles).
        int steps = 0, cur = c;
        while (fr.downstream[cur] != cur && steps <= w * h) { cur = fr.downstream[cur]; ++steps; }
        EXPECT_LE(steps, w * h) << "downstream cycle starting at " << c;
    }
}

}  // namespace
}  // namespace Phyxel
