#include <gtest/gtest.h>

#include "core/water/WaterBodyTable.h"

// WaterCore Phase D slice D1 (docs/WaterCore.md 16.2): Tier A records. Round trip through the
// world_meta string, per-column crediting (a volume straddling a bake body and open ground is
// split exactly), and rule-3 displacement.

namespace Phyxel::Core::Water {
namespace {

using CM = WaterBodyTable::ColumnMass;

TEST(WaterBodyTableTest, RoundTripsThroughMeta) {
    WaterBodyTable t;
    WaterBodyRecord g; g.id = 3; g.cls = "lake"; g.level = 148.9f; g.mass = 1234.5; g.bboxMin = {10, 20}; g.bboxMax = {40, 60};
    t.importGeneration({g});
    t.credit({CM{100, 100, 2.5, 17.5f}, CM{101, 100, 2.5, 17.5f}}, {}, [](int, int) { return -1; });
    ASSERT_EQ(t.size(), 2u);
    WaterBodyTable u;
    ASSERT_TRUE(u.load(t.serialize()));
    ASSERT_EQ(u.size(), 2u);
    const WaterBodyRecord* lake = u.find(3);
    ASSERT_NE(lake, nullptr);
    EXPECT_EQ(lake->origin, "generation"); EXPECT_FLOAT_EQ(lake->level, 148.9f); EXPECT_NEAR(lake->mass, 1234.5, 1e-9);
    EXPECT_EQ(lake->bboxMin, glm::ivec2(10, 20)); EXPECT_EQ(lake->bboxMax, glm::ivec2(40, 60));
    const WaterBodyRecord* pond = u.find(-1);
    ASSERT_NE(pond, nullptr);
    EXPECT_EQ(pond->origin, "av"); EXPECT_NEAR(pond->mass, 5.0, 1e-9); EXPECT_FLOAT_EQ(pond->level, 17.5f);
    EXPECT_NEAR(u.avMass(), 5.0, 1e-9);
    EXPECT_FALSE(WaterBodyTable().load("not json"));
}

TEST(WaterBodyTableTest, CreditsPerColumnAcrossABodyBoundary) {
    // columns x < 5 belong to bake body 7; x >= 5 to nobody. A volume over x 3..6 was seeded with
    // 1.0 per column and wrote 1.5 (x 3,4) and 0.5 (x 5,6): the lake gains +1.0, the new pond holds
    // exactly its written mass (1.0), never the lake's share.
    WaterBodyTable t;
    WaterBodyRecord g; g.id = 7; g.cls = "lake"; g.mass = 100.0; g.bboxMin = {0, 0}; g.bboxMax = {4, 0};
    t.importGeneration({g});
    auto bake = [](int x, int) { return x < 5 ? 7 : -1; };
    std::vector<CM> written{CM{3, 0, 1.5, 10.5f}, CM{4, 0, 1.5, 10.5f}, CM{5, 0, 0.5, 9.5f}, CM{6, 0, 0.5, 9.5f}};
    std::vector<CM> seeded{CM{3, 0, 1.0}, CM{4, 0, 1.0}, CM{5, 0, 1.0}, CM{6, 0, 1.0}};
    const int credited = t.credit(written, seeded, bake);
    EXPECT_EQ(credited, 7) << "most columns went to the lake";
    EXPECT_NEAR(t.find(7)->mass, 101.0, 1e-9);
    const WaterBodyRecord* pond = t.find(-1);
    ASSERT_NE(pond, nullptr);
    EXPECT_NEAR(pond->mass, 1.0, 1e-9) << "a new av pond takes the full written mass of its columns";
    EXPECT_EQ(pond->bboxMin, glm::ivec2(5, 0)); EXPECT_EQ(pond->bboxMax, glm::ivec2(6, 0));
    // the pond sleeps again later with 0.7 per column (it drained): its mass is replaced for those columns
    t.credit({CM{5, 0, 0.7, 9.7f}, CM{6, 0, 0.7, 9.7f}}, {CM{5, 0, 0.5}, CM{6, 0, 0.5}}, bake);
    EXPECT_NEAR(t.find(-1)->mass, 1.4, 1e-9);
    EXPECT_EQ(t.size(), 2u) << "no second pond record for the same columns";
}

TEST(WaterBodyTableTest, DisplacementDebitsTheOwnerAndCountsOrphans) {
    WaterBodyTable t;
    t.credit({CM{0, 0, 2.0, 12.0f}, CM{1, 0, 2.0, 12.0f}}, {}, [](int, int) { return -1; });
    t.displace(1, 0, 1.0, [](int, int) { return -1; });
    EXPECT_NEAR(t.find(-1)->mass, 3.0, 1e-9);
    EXPECT_NEAR(t.find(-1)->displaced, 1.0, 1e-9);
    t.displace(50, 50, 0.25, [](int, int) { return -1; });   // no record owns this column
    EXPECT_NEAR(t.orphanDisplaced(), 0.25, 1e-9);
    EXPECT_NEAR(t.displacedTotal(), 1.25, 1e-9);
}

}  // namespace
}  // namespace Phyxel::Core::Water
