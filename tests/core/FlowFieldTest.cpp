#include <gtest/gtest.h>

#include "core/FlowField.h"

#include <cmath>
#include <cstdio>

// L2 validation for P2.3a flow accumulation (docs/TerrainGenerationV2.md P2.3). A valley must FUNNEL
// upstream drainage into its mouth (large accum) while ridges carry almost none — the signal rivers
// are later thresholded from. Drives the real FlowField on synthetic terrain.

namespace Phyxel {
namespace {

TEST(FlowFieldTest, ValleyFunnelsAccumulationToItsMouth) {
    // A strong V-valley along z=100 (2.0/cell toward the axis) with a gentle downhill toward x=0
    // (0.1/cell). Steepest-descent flow drops every column into the valley, then runs down it to the
    // (x=0, z=100) mouth — so the mouth carries ~the whole region while ridge corners carry ~1.
    auto height = [](float x, float z) { return std::fabs(z - 100.0f) * 2.0f + x * 0.1f; };
    const int cx = 20, cz = 20;
    FlowField f(height, 0.0f, 0.0f, cx, cz, 10.0f, -1000.0f);  // seaLevel far below → border outlets
    const int total = cx * cz;

    std::printf("[flow] maxAccum=%d/%d mouth=%d ridge=%d\n",
                f.maxAccum(), total, f.accumAt(5.0f, 100.0f), f.accumAt(185.0f, 5.0f));

    EXPECT_GT(f.maxAccum(), total * 3 / 4) << "the valley mouth should gather most of the region";
    EXPECT_GT(f.accumAt(5.0f, 100.0f), total / 2) << "mouth column should carry a big catchment";
    EXPECT_LT(f.accumAt(185.0f, 5.0f), 10) << "a ridge corner should carry almost no upstream";
    EXPECT_EQ(f.accumAt(1e6f, 0.0f), 0) << "outside the region → 0";
}

TEST(FlowFieldTest, EveryInteriorCellHasAtLeastOneUpstream) {
    auto height = [](float x, float z) { return std::sin(x * 0.02f) * 30.0f + std::cos(z * 0.017f) * 25.0f + 60.0f; };
    FlowField f(height, 0.0f, 0.0f, 18, 18, 10.0f, -1000.0f);
    for (int j = 0; j < 18; ++j)
        for (int i = 0; i < 18; ++i)
            EXPECT_GE(f.accumAt(i * 10.0f, j * 10.0f), 1) << "cell (" << i << "," << j << ") accum < 1";
}

TEST(FlowFieldTest, DrainageGraphIsAcyclic_AllCellsReleased) {
    // Guard: the accumulation topo-pass must release EVERY cell — a cycle in the steepest-descent/
    // flat-fallback graph would leave cells unreleased and silently under-count upstream area.
    auto height = [](float x, float z) { return std::fabs(z - 100.0f) * 2.0f + x * 0.1f; };
    FlowField f(height, 0.0f, 0.0f, 20, 20, 10.0f, -1000.0f);
    EXPECT_TRUE(f.drainageComplete()) << "a cycle in the drainage graph left cells unreleased";
}

TEST(FlowFieldTest, Deterministic) {
    auto height = [](float x, float z) { return std::fabs(z - 90.0f) * 1.5f + x * 0.2f + std::sin(x * 0.03f) * 5.0f; };
    FlowField a(height, -50.0f, -50.0f, 22, 22, 11.0f, -1000.0f);
    FlowField b(height, -50.0f, -50.0f, 22, 22, 11.0f, -1000.0f);
    for (float z = -40.0f; z < 180.0f; z += 33.0f)
        for (float x = -40.0f; x < 180.0f; x += 29.0f)
            EXPECT_EQ(a.accumAt(x, z), b.accumAt(x, z));
}

}  // namespace
}  // namespace Phyxel
