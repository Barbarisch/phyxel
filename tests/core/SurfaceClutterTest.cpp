#include <gtest/gtest.h>

#include <set>

#include "core/FurniturePlacer.h"

using namespace Phyxel::Core;

// ============================================================================
// Surface clutter (mugs + bottles ON tables/shelves) — the surface-placement path furnish() lacks
// (furnish only floor-places). placeSurfaceClutter must put each item on a DISTINCT cell INSIDE the
// surface footprint, at the surface-top Y (not the floor), never overflow the surface, deterministic.
// ============================================================================

TEST(SurfaceClutterTest, ClutterStaysOnSurfaceTopAndDoesNotOverlap) {
    const Rect surface{10, 20, 3, 2};          // world cells x[10,13) z[20,22) -> 6 cells
    const int topY = 15;
    const auto out = FurniturePlacer::placeSurfaceClutter(
        "taproom", surface, topY, {"mug", "mug", "bottle"}, 42u);

    ASSERT_EQ(out.size(), 3u) << "all 3 clutter items fit the 6-cell surface";
    std::set<std::pair<int, int>> cells;
    for (const auto& p : out) {
        EXPECT_GE(p.worldPos.x, surface.x);
        EXPECT_LT(p.worldPos.x, surface.x1())  << "clutter off the surface in X";
        EXPECT_GE(p.worldPos.z, surface.z);
        EXPECT_LT(p.worldPos.z, surface.z1())  << "clutter off the surface in Z";
        EXPECT_EQ(p.worldPos.y, topY)          << "clutter not on the surface top (floating/sunk)";
        cells.insert({p.worldPos.x, p.worldPos.z});
    }
    EXPECT_EQ(cells.size(), out.size()) << "two clutter items share a cell (overlap)";
}

TEST(SurfaceClutterTest, DeterministicBySeed) {
    const Rect surface{0, 0, 4, 3};
    const auto a = FurniturePlacer::placeSurfaceClutter("r", surface, 9, {"mug", "bottle", "mug"}, 7u);
    const auto b = FurniturePlacer::placeSurfaceClutter("r", surface, 9, {"mug", "bottle", "mug"}, 7u);
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i].type, b[i].type);
        EXPECT_EQ(a[i].worldPos, b[i].worldPos) << "same seed produced a different scatter";
    }
}

// TEETH: more items than the surface has cells -> only as many as fit, still no overlap (no overflow).
TEST(SurfaceClutterTest, NeverOverflowsTheSurface) {
    const Rect tiny{5, 5, 1, 1};               // a single-cell surface
    const auto out = FurniturePlacer::placeSurfaceClutter(
        "r", tiny, 3, {"mug", "mug", "mug", "mug", "mug"}, 1u);
    ASSERT_EQ(out.size(), 1u) << "overflowed a 1-cell surface with 5 items";
    EXPECT_EQ(out[0].worldPos, glm::ivec3(5, 3, 5));
}

// A degenerate surface (no cells) or no items -> nothing placed (no crash).
TEST(SurfaceClutterTest, EmptyInputsPlaceNothing) {
    EXPECT_TRUE(FurniturePlacer::placeSurfaceClutter("r", Rect{0, 0, 0, 0}, 0, {"mug"}, 1u).empty());
    EXPECT_TRUE(FurniturePlacer::placeSurfaceClutter("r", Rect{0, 0, 3, 3}, 0, {}, 1u).empty());
}
