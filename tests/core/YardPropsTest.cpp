#include <gtest/gtest.h>

#include "core/SettlementLayout.h"

using namespace Phyxel::Core;

// ============================================================================
// planYardProps — the rear toft stops being bare grass (place_yard_props #29 /
// place_garden #25 minimum slice). L1/L2 invariants: props EXIST when the toft
// has room, sit INSIDE the plot inset 1 cube (clear of the fence line), OUTSIDE
// the building footprint, on the REAR side (behind the building, away from the
// street), and never overlap each other. Deterministic in (plot, seed).
// RED baseline: the empty stub places nothing.
// ============================================================================

namespace {
// A hand-built village plot: 12 frontage x 18 deep, street to the SOUTH ('S'),
// building 8x6 at setback 2 — rear toft is z 8..18 (10 cubes deep, plenty).
AssignedPlot southPlot() {
    AssignedPlot ap;
    ap.plot.rect = {0, 0, 12, 18};
    ap.typology = "croft";
    ap.streetSide = 'S';
    ap.setback = 2;
    ap.footprint = {2, 2, 8, 6};
    return ap;
}
// The same parcel rotated: street to the WEST ('W'), long axis along X.
AssignedPlot westPlot() {
    AssignedPlot ap;
    ap.plot.rect = {0, 0, 18, 12};
    ap.typology = "croft";
    ap.streetSide = 'W';
    ap.setback = 2;
    ap.footprint = {2, 2, 6, 8};
    return ap;
}
bool overlaps(int ax, int az, int aw, int ad, const Rect& r) {
    return ax < r.x1() && r.x < ax + aw && az < r.z1() && r.z < az + ad;
}
} // namespace

// THE presence invariant (RED on the stub): a roomy rear toft gets furnished — at least a
// woodpile and a garden bed.
TEST(YardPropsTest, RoomyToftGetsWoodpileAndGarden) {
    const auto props = planYardProps(southPlot(), 7);
    ASSERT_GE(props.size(), 2u) << "rear toft left bare";
    bool wood = false, garden = false;
    for (const auto& p : props) {
        if (p.type == "woodpile") wood = true;
        if (p.type == "garden_bed") garden = true;
    }
    EXPECT_TRUE(wood) << "no woodpile";
    EXPECT_TRUE(garden) << "no garden bed";
}

// Placement invariants: inside the plot inset 1 (fence clearance), outside the building,
// on the REAR side of the building (away from the street), non-overlapping.
TEST(YardPropsTest, PropsRespectFenceBuildingAndRearSide) {
    const AssignedPlot ap = southPlot();
    const auto props = planYardProps(ap, 7);
    ASSERT_FALSE(props.empty());
    for (size_t i = 0; i < props.size(); ++i) {
        const auto& p = props[i];
        EXPECT_GE(p.cx, ap.plot.rect.x + 1) << p.type << " on the fence line";
        EXPECT_GE(p.cz, ap.plot.rect.z + 1) << p.type;
        EXPECT_LE(p.cx + p.w, ap.plot.rect.x1() - 1) << p.type;
        EXPECT_LE(p.cz + p.d, ap.plot.rect.z1() - 1) << p.type;
        EXPECT_FALSE(overlaps(p.cx, p.cz, p.w, p.d, ap.footprint))
            << p.type << " overlaps the building";
        // street at -z ('S') -> rear = beyond the building's far z edge
        EXPECT_GE(p.cz, ap.footprint.z1()) << p.type << " is not in the REAR toft";
        for (size_t j = i + 1; j < props.size(); ++j) {
            const auto& q = props[j];
            EXPECT_FALSE(p.cx < q.cx + q.w && q.cx < p.cx + p.w &&
                         p.cz < q.cz + q.d && q.cz < p.cz + p.d)
                << p.type << " overlaps " << q.type;
        }
    }
}

// The rotated parcel ('W' street): the rear toft is beyond the building's +x edge.
TEST(YardPropsTest, RotatedParcelPutsPropsBeyondTheRearWall) {
    const AssignedPlot ap = westPlot();
    const auto props = planYardProps(ap, 7);
    ASSERT_FALSE(props.empty());
    for (const auto& p : props) {
        EXPECT_GE(p.cx, ap.footprint.x1()) << p.type << " is not in the REAR toft (street 'W')";
        EXPECT_FALSE(overlaps(p.cx, p.cz, p.w, p.d, ap.footprint));
    }
}

// Honest degradation: a parcel with NO rear room (building flush to the rear fence) places
// nothing rather than clipping props into the fence or building.
TEST(YardPropsTest, NoRearRoomPlacesNothing) {
    AssignedPlot ap = southPlot();
    ap.plot.rect = {0, 0, 12, 9};      // plot barely deeper than setback+building: rear toft ~0
    const auto props = planYardProps(ap, 7);
    EXPECT_TRUE(props.empty());
}

// Deterministic in seed.
TEST(YardPropsTest, DeterministicInSeed) {
    const auto a = planYardProps(southPlot(), 42);
    const auto b = planYardProps(southPlot(), 42);
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i].type, b[i].type);
        EXPECT_EQ(a[i].cx, b[i].cx);
        EXPECT_EQ(a[i].cz, b[i].cz);
        EXPECT_EQ(a[i].rotDeg, b[i].rotDeg);
    }
}
