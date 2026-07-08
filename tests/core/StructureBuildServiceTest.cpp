#include <gtest/gtest.h>

#include "core/StructureBuildService.h"

using namespace Phyxel::Core;
using json = nlohmann::json;

// The v1 composite generators are gone; plain house/tavern requests must map onto
// v2 BuildingPrograms (typology + style + footprint + story ARRAY) so no existing
// caller (API script, game.json) silently loses its buildings.

TEST(StructureBuildServiceTest, AliasHouseSmallFootprintIsCroft) {
    json v2 = StructureBuildService::aliasLegacyParams({
        {"type", "house"}, {"position", {{"x", 10}, {"y", 16}, {"z", 20}}},
        {"width", 7}, {"depth", 6}});
    ASSERT_FALSE(v2.is_null());
    EXPECT_EQ(v2["schema"], "v2");
    EXPECT_EQ(v2["typology"], "croft");
    EXPECT_EQ(v2["style"], "timber_cottage");
    EXPECT_EQ(v2["footprint"][0], 7);
    EXPECT_EQ(v2["footprint"][1], 6);
    ASSERT_TRUE(v2["stories"].is_array());       // v1 int -> v2 story objects
    EXPECT_EQ(v2["stories"].size(), 1u);
    EXPECT_EQ(v2["position"]["x"], 10);
}

TEST(StructureBuildServiceTest, AliasHouseLargeFootprintIsHallHouse) {
    json v2 = StructureBuildService::aliasLegacyParams({
        {"type", "house"}, {"width", 10}, {"depth", 8}});
    EXPECT_EQ(v2["typology"], "hall_house");
}

TEST(StructureBuildServiceTest, AliasTavernKeepsTypologyAndStoriesCount) {
    json v2 = StructureBuildService::aliasLegacyParams({
        {"type", "tavern"}, {"width", 10}, {"depth", 8}, {"stories", 2}});
    EXPECT_EQ(v2["typology"], "tavern");
    EXPECT_EQ(v2["function"], "tavern");
    ASSERT_TRUE(v2["stories"].is_array());
    EXPECT_EQ(v2["stories"].size(), 2u);
}

TEST(StructureBuildServiceTest, AliasRespectsExplicitTypologyAndStyle) {
    json v2 = StructureBuildService::aliasLegacyParams({
        {"type", "house"}, {"width", 7}, {"depth", 6},
        {"typology", "longhouse"}, {"style", "stone_manor"}});
    EXPECT_EQ(v2["typology"], "longhouse");
    EXPECT_EQ(v2["style"], "stone_manor");
}

TEST(StructureBuildServiceTest, AliasUnknownTypeIsNull) {
    EXPECT_TRUE(StructureBuildService::aliasLegacyParams({{"type", "tower"}}).is_null());
    EXPECT_TRUE(StructureBuildService::aliasLegacyParams({{"type", "wall"}}).is_null());
}
