#include <gtest/gtest.h>

#include "core/HydrologyMap.h"
#include "core/WaterBodyIndex.h"

#include <cmath>

// Tangible-water Phase A: connected-component water BODY identity over the hydrology bake.
// Written RED against a stub WaterBodyIndex that labels nothing — every test below fails until
// the labeling/classification is implemented.
//
// Fixture geography (12×12 cells, cellSize 10, sea level 0, default terrain 50):
//  - OCEAN: the x=0 column at height −5 floods to sea level 0 (touches the bake boundary).
//  - LAKE: 3×4 basin at height 10, cells x∈[4,6] z∈[4,7], with an outlet channel at height 25
//    running east to the boundary → fills flat to 25; 12 cells ≥ kInfiniteMinCells.
//  - POND: 2-cell basin at height 5, cells (2,9)-(3,9), outlet channel at height 15 → level 15.
//  - The outlet channels themselves sit AT their pass height → dry (level == terrain).

namespace Phyxel {
namespace {

float fixtureHeight(float x, float z) {
    const int cx = static_cast<int>(std::floor(x / 10.0f));
    const int cz = static_cast<int>(std::floor(z / 10.0f));
    if (cx == 0) return -5.0f;                                    // ocean strip (boundary column)
    if (cx >= 4 && cx <= 6 && cz >= 4 && cz <= 7) return 10.0f;   // lake basin
    if (cz == 5 && cx >= 7 && cx <= 11) return 25.0f;             // lake outlet channel (dry)
    if ((cx == 2 || cx == 3) && cz == 9) return 5.0f;             // pond basin
    if (cz == 9 && cx >= 4 && cx <= 11) return 15.0f;             // pond outlet channel (dry)
    return 50.0f;
}

struct BakedFixture {
    HydrologyMap hydro;
    WaterBodyIndex bodies;
    BakedFixture()
        : hydro(fixtureHeight, 0.0f, 0.0f, 12, 12, 10.0f, /*seaLevel=*/0.0f),
          bodies(hydro, fixtureHeight) {}
};

glm::vec2 cellCenter(int cx, int cz) { return {(cx + 0.5f) * 10.0f, (cz + 0.5f) * 10.0f}; }

}  // namespace

TEST(WaterBodyIndexTest, LabelsDistinctBasinsAsDistinctBodies) {
    BakedFixture f;
    ASSERT_GE(f.bodies.bodies().size(), 3u) << "expected at least ocean + lake + pond";

    const auto oc = cellCenter(0, 6), lk = cellCenter(5, 5), pd = cellCenter(2, 9);
    const int32_t oceanId = f.bodies.bodyIdAt(oc.x, oc.y);
    const int32_t lakeId  = f.bodies.bodyIdAt(lk.x, lk.y);
    const int32_t pondId  = f.bodies.bodyIdAt(pd.x, pd.y);
    ASSERT_GE(oceanId, 0) << "ocean cell unlabeled";
    ASSERT_GE(lakeId, 0)  << "lake cell unlabeled";
    ASSERT_GE(pondId, 0)  << "pond cell unlabeled";
    EXPECT_NE(oceanId, lakeId);
    EXPECT_NE(lakeId, pondId);
    EXPECT_NE(oceanId, pondId);

    // Dry columns label -1: the high plateau and the (level == terrain) outlet channels.
    EXPECT_EQ(f.bodies.bodyIdAt(cellCenter(9, 2).x, cellCenter(9, 2).y), -1) << "plateau must be dry";
    EXPECT_EQ(f.bodies.bodyIdAt(cellCenter(8, 5).x, cellCenter(8, 5).y), -1)
        << "outlet channel at its pass height holds no water";
    EXPECT_EQ(f.bodies.bodyIdAt(-50.0f, -50.0f), -1) << "outside the baked region";
}

TEST(WaterBodyIndexTest, ClassifiesOceanLakePondByBoundaryAndArea) {
    BakedFixture f;
    const auto* ocean = f.bodies.bodyAt(cellCenter(0, 6).x, cellCenter(0, 6).y);
    const auto* lake  = f.bodies.bodyAt(cellCenter(5, 5).x, cellCenter(5, 5).y);
    const auto* pond  = f.bodies.bodyAt(cellCenter(2, 9).x, cellCenter(2, 9).y);
    ASSERT_NE(ocean, nullptr);
    ASSERT_NE(lake, nullptr);
    ASSERT_NE(pond, nullptr);

    EXPECT_EQ(ocean->cls, WaterBodyIndex::Class::Ocean)
        << "boundary-touching body at sea level must be OCEAN";
    EXPECT_NEAR(ocean->level, 0.0f, 1e-3f);

    EXPECT_EQ(lake->cls, WaterBodyIndex::Class::Lake) << "12 cells >= 4 must be an (infinite) LAKE";
    EXPECT_EQ(lake->areaCells, 12);
    EXPECT_NEAR(lake->level, 25.0f, 1e-3f) << "lake fills flat to its outlet pass height";

    EXPECT_EQ(pond->cls, WaterBodyIndex::Class::Pond) << "2 cells < 4 must be a (finite) POND";
    EXPECT_EQ(pond->areaCells, 2);
    EXPECT_NEAR(pond->level, 15.0f, 1e-3f);
}

TEST(WaterBodyIndexTest, VolumeEstimateMatchesAnalyticBasin) {
    BakedFixture f;
    const auto* lake = f.bodies.bodyAt(cellCenter(5, 5).x, cellCenter(5, 5).y);
    ASSERT_NE(lake, nullptr);
    // 12 cells × (level 25 − terrain 10) × cellSize² (100) = 18000.
    EXPECT_NEAR(lake->volumeEst, 18000.0f, 1.0f);
    // bbox covers exactly the basin cells.
    EXPECT_EQ(lake->bboxMin, glm::ivec2(4, 4));
    EXPECT_EQ(lake->bboxMax, glm::ivec2(6, 7));
}

TEST(WaterBodyIndexTest, SameLevelBasinsAcrossDryGroundStayDistinct) {
    // Two 1-cell basins at identical levels, separated by dry ground: CC must keep them apart
    // (labeling is by connectivity, not by bucketing levels).
    auto height = [](float x, float z) -> float {
        const int cx = static_cast<int>(std::floor(x / 10.0f));
        const int cz = static_cast<int>(std::floor(z / 10.0f));
        if (cx == 0) return -5.0f;                       // boundary ocean outlet
        if (cx == 3 && cz == 3) return 5.0f;             // basin A
        if (cx == 3 && cz == 7) return 5.0f;             // basin B (same depth)
        if (cz == 3 && cx >= 1 && cx <= 2) return 20.0f; // A's outlet channel
        if (cz == 7 && cx >= 1 && cx <= 2) return 20.0f; // B's outlet channel (same pass height)
        return 50.0f;
    };
    HydrologyMap hydro(height, 0.0f, 0.0f, 10, 10, 10.0f, 0.0f);
    WaterBodyIndex bodies(hydro, height);

    const int32_t a = bodies.bodyIdAt(35.0f, 35.0f);
    const int32_t b = bodies.bodyIdAt(35.0f, 75.0f);
    ASSERT_GE(a, 0);
    ASSERT_GE(b, 0);
    EXPECT_NE(a, b) << "same-level but disconnected basins merged — labeling bucketed by level";
    EXPECT_NEAR(bodies.body(a)->level, bodies.body(b)->level, 1e-4f) << "fixture: levels should match";
}

TEST(WaterBodyIndexTest, DeterministicAcrossRebuilds) {
    BakedFixture f1, f2;
    ASSERT_EQ(f1.bodies.bodies().size(), f2.bodies.bodies().size());
    for (size_t i = 0; i < f1.bodies.bodies().size(); ++i) {
        const auto& a = f1.bodies.bodies()[i];
        const auto& b = f2.bodies.bodies()[i];
        EXPECT_EQ(a.id, b.id);
        EXPECT_EQ(a.cls, b.cls);
        EXPECT_EQ(a.areaCells, b.areaCells);
        EXPECT_FLOAT_EQ(a.level, b.level);
    }
}

}  // namespace Phyxel
