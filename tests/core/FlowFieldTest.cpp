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
    // Small-scale plan Phase 2a: orders 1-2 are CREEKS — sub-voxel WATER depths (fractions of a
    // voxel) that pin a fractional ribbon. They still cut no TERRAIN (the generator clamps the
    // carve to order ≥ 3) and no valley (nearestChannel skips them).
    EXPECT_FLOAT_EQ(FlowField::channelDepth(1), 0.33f) << "order-1 creek: sub-voxel water depth";
    EXPECT_FLOAT_EQ(FlowField::channelDepth(2), 0.66f) << "order-2 creek: sub-voxel water depth";
    EXPECT_FLOAT_EQ(FlowField::channelDepth(3), 1.0f);
    EXPECT_FLOAT_EQ(FlowField::channelDepth(6), 2.0f);
    EXPECT_FLOAT_EQ(FlowField::channelDepth(0), 0.0f) << "non-river → no channel";
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

    // Creeks (Phase 2a): orders 1-2 now HIT with fractional sub-voxel depth — the water runtime
    // pins their ribbon — while order 0 still reports nothing. Order 6 carves deeper as before.
    auto creek2 = FlowField::segmentChannel(50.0f, 0.0f, 0.0f, 0.0f, 100.0f, 0.0f, 2);
    EXPECT_TRUE(creek2.hit) << "order-2 creek must report a channel";
    EXPECT_NEAR(creek2.depth, 0.66f, 1e-4f) << "order-2 fractional depth at the centreline";
    auto creek1 = FlowField::segmentChannel(50.0f, 0.0f, 0.0f, 0.0f, 100.0f, 0.0f, 1);
    EXPECT_TRUE(creek1.hit) << "order-1 creek must report a channel";
    EXPECT_NEAR(creek1.depth, 0.33f, 1e-4f) << "order-1 fractional depth at the centreline";
    EXPECT_FALSE(FlowField::segmentChannel(50.0f, 0.0f, 0.0f, 0.0f, 100.0f, 0.0f, 0).hit) << "order 0 = no river";
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
    // A high plateau corner: at riverThreshold 0 every cell is technically an order-1 channel
    // (Phase 2a made those REPORT as fractional creeks rather than nothing), but it must never
    // read as a CARVING river — terrain consumers gate on order ≥ 3.
    auto off = f.channelAt(55.0f, 5.0f);
    EXPECT_LT(off.order, 3) << "plateau corner must not report a carving-order river";
    if (off.hit) EXPECT_LT(off.depth, 1.0f) << "an order-1/2 report must stay sub-voxel";

    // SEGMENT order ≠ CELL order — the distinction the water runtime's pin mapping depends on.
    // (15,31) lies INSIDE the order-3 confluence CELL (c3, cell 1,3 spans z 30..40), but the only
    // segment within range is the order-2 tributary c1→c3 running along x=15 — so channelAt must
    // report ORDER 2 with a fractional depth while orderAt says 3. Combining orderAt's cell order
    // with channelAt's depth full-pinned uncarved creek ground at every junction (a ~900-mass
    // sheet flood, measured live in CreekLab 2026-07-30): consumers must take BOTH facts from the
    // same channelAt hit.
    ASSERT_EQ(f.orderAt(15.0f, 31.0f), 3) << "fixture: (15,31) must sit in the order-3 cell";
    auto seg = f.channelAt(15.0f, 31.0f);
    EXPECT_TRUE(seg.hit);
    EXPECT_EQ(seg.order, 2) << "channelAt must report the HIT SEGMENT's order, not the cell's";
    EXPECT_LT(seg.depth, 1.0f) << "the order-2 segment's depth must stay sub-voxel";
}

// nearestChannel's minOrder parameter (water-as-terrain-stage P2): the default (3) keeps valley
// shaping blind to creeks, while minOrder=1 lets the creek-swale pass see orders 1-2. On the tree
// fixture, (15,25) sits ON the order-2 tributary c1→c3; the nearest order≥3 segment (c3→outlet
// along z=35) is 10 units away.
TEST(FlowFieldTest, NearestChannelMinOrderSeesCreeks) {
    FlowField f(treeHeight, 0.0f, 0.0f, 7, 7, 10.0f, -1000.0f, /*riverThreshold=*/0);
    ASSERT_GE(f.maxOrder(), 3);

    const auto big = f.nearestChannel(15.0f, 25.0f, 12.0f);         // default minOrder=3
    EXPECT_EQ(big.order, 3) << "default must keep reporting only order>=3";
    EXPECT_NEAR(big.dist, 10.0f, 0.5f) << "nearest order-3 segment is the c3->outlet run at z=35";

    const auto creek = f.nearestChannel(15.0f, 25.0f, 12.0f, 1);    // creek swale query
    EXPECT_GE(creek.order, 1);
    EXPECT_LE(creek.order, 2);
    EXPECT_NEAR(creek.dist, 0.0f, 0.5f) << "the point lies ON the order-2 tributary centreline";
}

TEST(FlowFieldTest, ChannelAtReportsFractionalCreeksAtOrder1And2) {
    // Phase 2a rewrite — this test used to pin the OPPOSITE (orders 1-2 report NO channel at all),
    // which was gate #2 of the four that kept creeks bone dry. The single valley tops out at order
    // 1-2; sampling at cell centres, channelAt must now report the creek with a FRACTIONAL
    // (sub-voxel) depth — never a full voxel — so the water runtime can pin a ribbon while the
    // terrain carve (clamped to order ≥ 3 in the generator) still leaves the ground untouched.
    auto height = [](float x, float z) { return std::fabs(z - 100.0f) * 2.0f + x * 0.1f; };
    FlowField f(height, 0.0f, 0.0f, 20, 20, 10.0f, -1000.0f, /*riverThreshold=*/20);
    ASSERT_GE(f.maxOrder(), 1) << "there must be river cells (else the test is vacuous)";
    ASSERT_LE(f.maxOrder(), 2) << "this fixture is meant to top out below the carving order (3)";
    int creekHits = 0;
    for (int j = 0; j < 20; ++j)
        for (int i = 0; i < 20; ++i) {
            const float wx = (i + 0.5f) * 10.0f, wz = (j + 0.5f) * 10.0f;
            const auto h = f.channelAt(wx, wz);
            if (f.orderAt(wx, wz) >= 1) {
                EXPECT_TRUE(h.hit) << "creek cell centre must report its channel (" << i << "," << j << ")";
                EXPECT_LT(h.depth, 1.0f) << "creek depth must stay sub-voxel";
                EXPECT_GT(h.depth, 0.0f);
                ++creekHits;
            }
        }
    EXPECT_GT(creekHits, 3) << "too few creek cells reported to be meaningful";
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
