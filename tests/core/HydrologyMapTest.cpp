#include <gtest/gtest.h>

#include "core/HydrologyMap.h"

#include <cmath>
#include <cstdint>

// L2/L3 validation for the P2.2 hydrology data layer (docs/TerrainGenerationV2.md P2.2): the ocean
// fills sub-sea cells to sea level, inland lakes are flat + contained, draining slopes produce no
// spurious lakes, and the bake is deterministic. Drives the real HydrologyMap on synthetic heights.

namespace Phyxel {
namespace {

TEST(HydrologyMapTest, OceanFillsSubSeaColumnsToSeaLevel) {
    // A plane below sea on the low-x side, above sea (draining to the sea) on the high side. sea=16.
    auto height = [](float x, float) { return x * 0.5f - 50.0f; };  // x=20→-40, x=180→+40
    HydrologyMap m(height, 0.0f, 0.0f, 20, 20, 10.0f, 16.0f);

    EXPECT_TRUE(m.hasWater(20.0f, 50.0f)) << "sub-sea column should be ocean";
    EXPECT_FLOAT_EQ(m.waterLevelAt(20.0f, 50.0f), 16.0f) << "ocean surface should be exactly sea level";
    EXPECT_FALSE(m.hasWater(180.0f, 50.0f)) << "above-sea slope drains to the sea → dry";
    EXPECT_FLOAT_EQ(m.waterLevelAt(1e6f, 0.0f), HydrologyMap::NO_WATER) << "outside region → no water";
}

TEST(HydrologyMapTest, InlandLakeIsFlatAndContained) {
    // A 100 plateau with a central pit of 40; sea far below (0) so there is NO ocean — only the lake.
    auto height = [](float x, float z) {
        bool inPit = (x >= 60.0f && x < 140.0f && z >= 60.0f && z < 140.0f);
        return inPit ? 40.0f : 100.0f;
    };
    HydrologyMap m(height, 0.0f, 0.0f, 20, 20, 10.0f, 0.0f);

    float a = m.waterLevelAt(80.0f, 80.0f), b = m.waterLevelAt(120.0f, 120.0f);
    EXPECT_TRUE(m.hasWater(80.0f, 80.0f)) << "pit should hold a lake";
    EXPECT_FLOAT_EQ(a, b) << "lake surface must be flat across the basin";
    EXPECT_GT(a, 40.0f) << "lake surface must sit above the basin floor";
    EXPECT_FALSE(m.hasWater(20.0f, 20.0f)) << "the surrounding plateau must be dry (contained)";
}

TEST(HydrologyMapTest, InlandBasinBehindSubSeaOutletDrainsToSeaNotBorder) {
    // THE test that actually exercises HydrologyMap's SEA-OUTLET integration (the earlier cases don't
    // — none of them place a sub-sea cell inside the terrain, so they pass even with a border-only
    // flood). Mirrors PriorityFloodTest.SeaOutletDrainsSubSeaCellsInsteadOfBrimming as a height field:
    // a 50 border, a 10 ring, and a -20 sub-sea drain at the centre; sea = 0.
    //   • sea-outlet flood: the 10 ring DRAINS to the sub-sea centre → stays dry (filled == base).
    //   • border-only flood (the bug): the ring is enclosed by the 50 border → brims to a spurious
    //     lake at 50. So EXPECT_FALSE(hasWater(ring)) FAILS if HydrologyMap drops the sea outlet.
    auto height = [](float x, float z) {
        int i = static_cast<int>(std::lround(x / 10.0f));
        int j = static_cast<int>(std::lround(z / 10.0f));
        if (i <= 0 || i >= 4 || j <= 0 || j >= 4) return 50.0f;   // high border
        if (i == 2 && j == 2) return -20.0f;                      // sub-sea drain (ocean outlet)
        return 10.0f;                                             // above-sea ring
    };
    HydrologyMap m(height, 0.0f, 0.0f, 5, 5, 10.0f, 0.0f);
    EXPECT_FLOAT_EQ(m.waterLevelAt(20.0f, 20.0f), 0.0f) << "sub-sea centre is ocean at sea level";
    // The ring drains to the ocean → dry. This is red under a border-only flood (ring brims to 50).
    EXPECT_FALSE(m.hasWater(10.0f, 20.0f)) << "ring behind a sub-sea outlet must drain, not brim to border";
    EXPECT_FALSE(m.hasWater(30.0f, 20.0f)) << "spurious border-brim lake on the ring";
    EXPECT_FALSE(m.hasWater(20.0f, 10.0f)) << "spurious border-brim lake on the ring";
}

TEST(HydrologyMapTest, DrainingSlopeHasNoSpuriousLakes) {
    // A monotonic slope, entirely above sea → every column drains off the edge, no depressions.
    auto height = [](float x, float) { return x * 0.3f + 30.0f; };
    HydrologyMap m(height, 0.0f, 0.0f, 16, 16, 10.0f, 0.0f);
    long wet = 0;
    for (int j = 0; j < 16; ++j)
        for (int i = 0; i < 16; ++i)
            if (m.hasWater(i * 10.0f + 1.0f, j * 10.0f + 1.0f)) ++wet;
    EXPECT_EQ(wet, 0) << "a draining slope above sea must not grow lakes";
}

TEST(HydrologyMapTest, Deterministic) {
    auto height = [](float x, float z) { return std::sin(x * 0.01f) * 20.0f + std::cos(z * 0.013f) * 15.0f + 30.0f; };
    HydrologyMap a(height, -100.0f, -100.0f, 24, 24, 12.0f, 5.0f);
    HydrologyMap b(height, -100.0f, -100.0f, 24, 24, 12.0f, 5.0f);
    for (float z = -90.0f; z < 180.0f; z += 37.0f)
        for (float x = -90.0f; x < 180.0f; x += 41.0f)
            EXPECT_FLOAT_EQ(a.waterLevelAt(x, z), b.waterLevelAt(x, z));
}

}  // namespace
}  // namespace Phyxel
