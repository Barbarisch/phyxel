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

TEST(FlowFieldTest, StrahlerTwoEqualTributariesMakeNextOrder) {
    // Y-confluence: A(0)→C(2), B(1)→C(2), C(2)→D(3), D(3)=sink. Two order-1 streams meet → order 2.
    std::vector<int> downstream = {2, 2, 3, 3};
    std::vector<int> accum = {1, 1, 3, 4};
    auto order = FlowField::computeStrahler(downstream, accum, /*threshold=*/0);
    EXPECT_EQ(order[0], 1);  // headwater A
    EXPECT_EQ(order[1], 1);  // headwater B
    EXPECT_EQ(order[2], 2);  // confluence: two equal order-1 → order 2
    EXPECT_EQ(order[3], 2);  // trunk keeps order 2
}

TEST(FlowFieldTest, StrahlerSmallTributaryDoesNotBumpTrunk) {
    // A,B→C (order 2); a lone source E joins C at F. E is order 1, so F stays order 2 (not 3).
    // idx: 0=A→2, 1=B→2, 2=C→5, 3=E→5, 4=isolated(self), 5=F=sink.
    std::vector<int> downstream = {2, 2, 5, 5, 4, 5};
    std::vector<int> accum = {1, 1, 3, 1, 1, 5};
    auto order = FlowField::computeStrahler(downstream, accum, /*threshold=*/0);
    EXPECT_EQ(order[2], 2) << "the A+B confluence is order 2";
    EXPECT_EQ(order[3], 1) << "lone tributary E is order 1";
    EXPECT_EQ(order[5], 2) << "order-1 tributary must NOT bump the order-2 trunk to 3";
}

TEST(FlowFieldTest, StrahlerThresholdDemotesSubThresholdTributaries) {
    // Same Y as test 1 but threshold=2: A,B (accum 1) are below threshold → order 0, and C becomes a
    // headwater (order 1) because it has no RIVER tributaries.
    std::vector<int> downstream = {2, 2, 3, 3};
    std::vector<int> accum = {1, 1, 3, 4};
    auto order = FlowField::computeStrahler(downstream, accum, /*threshold=*/2);
    EXPECT_EQ(order[0], 0) << "sub-threshold cell is not a river";
    EXPECT_EQ(order[1], 0);
    EXPECT_EQ(order[2], 1) << "river cell with no river tributaries is a headwater";
    EXPECT_EQ(order[3], 1);
}

TEST(FlowFieldTest, OrderIsARiverOnTheValleyFloorNotTheRidge) {
    auto height = [](float x, float z) { return std::fabs(z - 100.0f) * 2.0f + x * 0.1f; };
    FlowField f(height, 0.0f, 0.0f, 20, 20, 10.0f, -1000.0f, /*riverThreshold=*/50);
    EXPECT_GE(f.orderAt(5.0f, 100.0f), 1) << "the high-accumulation valley mouth should be a river";
    EXPECT_EQ(f.orderAt(185.0f, 5.0f), 0) << "a low-accumulation ridge is not a river";
    EXPECT_GE(f.maxOrder(), 1);
}

TEST(FlowFieldTest, ChannelGeometryTablesAreGrounded) {
    // Grounded width (2,3,5,8,14,22 → half below) and depth (0,0,1,1,1,2) by Strahler order.
    EXPECT_FLOAT_EQ(FlowField::channelHalfWidth(1), 1.0f);
    EXPECT_FLOAT_EQ(FlowField::channelHalfWidth(3), 2.5f);
    EXPECT_FLOAT_EQ(FlowField::channelHalfWidth(6), 11.0f);
    EXPECT_FLOAT_EQ(FlowField::channelHalfWidth(9), 11.0f) << "clamps above order 6";
    EXPECT_FLOAT_EQ(FlowField::channelDepth(1), 0.0f) << "order-1 is sub-voxel → no carve";
    EXPECT_FLOAT_EQ(FlowField::channelDepth(2), 0.0f) << "order-2 is sub-voxel → no carve";
    EXPECT_FLOAT_EQ(FlowField::channelDepth(3), 1.0f);
    EXPECT_FLOAT_EQ(FlowField::channelDepth(6), 2.0f);
    EXPECT_FLOAT_EQ(FlowField::channelDepth(0), 0.0f) << "non-river → no carve";
}

TEST(FlowFieldTest, SegmentChannelGeometryAndOrderGate) {
    // Segment from (0,0) to (100,0), order 3 → half-width 2.5, depth 1.0, parabolic.
    // On the centreline: full depth. Halfway out: 1*(1-0.5^2)=0.75. Beyond half-width: no carve.
    auto onLine = FlowField::segmentChannel(50.0f, 0.0f, 0.0f, 0.0f, 100.0f, 0.0f, 3);
    EXPECT_TRUE(onLine.hit);
    EXPECT_EQ(onLine.order, 3);
    EXPECT_NEAR(onLine.depth, 1.0f, 1e-4f) << "full depth at the centreline";

    auto halfway = FlowField::segmentChannel(50.0f, 1.25f, 0.0f, 0.0f, 100.0f, 0.0f, 3);  // 1.25 = halfW/2
    EXPECT_TRUE(halfway.hit);
    EXPECT_NEAR(halfway.depth, 0.75f, 1e-3f) << "parabolic: 1*(1-0.5^2)";

    auto beyond = FlowField::segmentChannel(50.0f, 3.0f, 0.0f, 0.0f, 100.0f, 0.0f, 3);  // 3 > halfW 2.5
    EXPECT_FALSE(beyond.hit) << "outside half-width → no carve";

    // Order gate: orders 1-2 are sub-voxel → no carve even on the centreline; order 6 carves deeper.
    EXPECT_FALSE(FlowField::segmentChannel(50.0f, 0.0f, 0.0f, 0.0f, 100.0f, 0.0f, 2).hit) << "order 2 sub-voxel";
    EXPECT_FALSE(FlowField::segmentChannel(50.0f, 0.0f, 0.0f, 0.0f, 100.0f, 0.0f, 1).hit) << "order 1 sub-voxel";
    auto big = FlowField::segmentChannel(50.0f, 0.0f, 0.0f, 0.0f, 100.0f, 0.0f, 6);
    EXPECT_TRUE(big.hit);
    EXPECT_NEAR(big.depth, 2.0f, 1e-4f) << "order 6 depth";
}

// A hand-encoded drainage tree (heights = a lookup grid so steepest-descent follows the tree): two
// order-1 pairs form two order-2 tributaries that meet at c3 → order 3. cellSize 10, threshold 0.
//   outlet(0,3)=0  c3(1,3)=10 ;  c1(1,2)=20 fed by (2,2),(1,1) ;  c2(1,4)=20 fed by (2,4),(1,5).
static float treeHeight(float x, float z) {
    static const float H[7][7] = {
        {50, 50, 50, 50, 50, 50, 50},
        {50, 50, 50, 50, 50, 50, 50},
        {50, 20, 50, 50, 50, 50, 50},   // c1 at (i=1,j=2)
        { 0, 10, 50, 50, 50, 50, 50},   // outlet (0,3), c3 (1,3)
        {50, 20, 50, 50, 50, 50, 50},   // c2 at (1,4)
        {50, 50, 50, 50, 50, 50, 50},
        {50, 50, 50, 50, 50, 50, 50},
    };
    int i = static_cast<int>(std::lround(x / 10.0f)), j = static_cast<int>(std::lround(z / 10.0f));
    i = i < 0 ? 0 : (i > 6 ? 6 : i);
    j = j < 0 ? 0 : (j > 6 ? 6 : j);
    return H[j][i];
}

TEST(FlowFieldTest, ChannelAtCarvesAnOrder3RiverAtItsCentreline) {
    FlowField f(treeHeight, 0.0f, 0.0f, 7, 7, 10.0f, -1000.0f, /*riverThreshold=*/0);
    ASSERT_GE(f.maxOrder(), 3) << "the hand-built tree must reach order 3 (two order-2 tributaries meet)";
    ASSERT_EQ(f.orderAt(15.0f, 35.0f), 3) << "c3 (cell 1,3) is the order-3 confluence";

    // On the c3 cell centre (= a segment endpoint): carves, order 3, ~full depth (channelDepth(3)=1).
    auto on = f.channelAt(15.0f, 35.0f);
    EXPECT_TRUE(on.hit) << "channelAt must carve on the order-3 river";
    EXPECT_EQ(on.order, 3);
    EXPECT_NEAR(on.depth, 1.0f, 0.2f);
    // Well off the network (a high plateau corner) → no order-≥3 channel → no carve.
    auto off = f.channelAt(55.0f, 5.0f);
    EXPECT_FALSE(off.hit) << "channelAt must not carve off the river network";
}

TEST(FlowFieldTest, ChannelAtGatesOrder1And2RiversAtCellCentres) {
    // The single valley tops out at order 1-2; sampling at cell CENTRES (where the segments actually
    // run) it must carve NOTHING (orders 1-2 sub-voxel). Sampling centres makes this sensitive to the
    // gate — a broken order gate would produce hits here (proven by the auditor's mutation).
    auto height = [](float x, float z) { return std::fabs(z - 100.0f) * 2.0f + x * 0.1f; };
    FlowField f(height, 0.0f, 0.0f, 20, 20, 10.0f, -1000.0f, /*riverThreshold=*/20);
    ASSERT_GE(f.maxOrder(), 1) << "there must be river cells to gate (else the test is vacuous)";
    ASSERT_LE(f.maxOrder(), 2) << "this fixture is meant to top out below the carving order (3)";
    for (int j = 0; j < 20; ++j)
        for (int i = 0; i < 20; ++i)
            EXPECT_FALSE(f.channelAt((i + 0.5f) * 10.0f, (j + 0.5f) * 10.0f).hit)
                << "order-<=2 river must carve nothing at cell centre (" << i << "," << j << ")";
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
