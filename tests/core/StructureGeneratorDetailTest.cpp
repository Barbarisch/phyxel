#include <gtest/gtest.h>
#include "core/StructureGenerator.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <set>

using namespace Phyxel::Core;
using json = nlohmann::json;

// ============================================================================
// Helpers
// ============================================================================
namespace {

size_t countByLevel(const std::vector<VoxelPlacement>& voxels, VoxelLevel level) {
    return std::count_if(voxels.begin(), voxels.end(),
                         [level](const VoxelPlacement& v) { return v.level == level; });
}

} // anonymous namespace

// ============================================================================
// DetailLevel enum helpers
// ============================================================================

TEST(StructureDetailTest, DetailLevelFromString) {
    EXPECT_EQ(StructureGenerator::detailLevelFromString("rough"), DetailLevel::Rough);
    EXPECT_EQ(StructureGenerator::detailLevelFromString("Rough"), DetailLevel::Rough);
    EXPECT_EQ(StructureGenerator::detailLevelFromString("detailed"), DetailLevel::Detailed);
    EXPECT_EQ(StructureGenerator::detailLevelFromString("fine"), DetailLevel::Fine);
    EXPECT_EQ(StructureGenerator::detailLevelFromString("Fine"), DetailLevel::Fine);
    EXPECT_EQ(StructureGenerator::detailLevelFromString("unknown"), DetailLevel::Detailed); // default
}

TEST(StructureDetailTest, DetailLevelToString) {
    EXPECT_EQ(StructureGenerator::detailLevelToString(DetailLevel::Rough), "rough");
    EXPECT_EQ(StructureGenerator::detailLevelToString(DetailLevel::Detailed), "detailed");
    EXPECT_EQ(StructureGenerator::detailLevelToString(DetailLevel::Fine), "fine");
}

// ============================================================================
// Detail primitives produce subcube-level voxels
// ============================================================================

TEST(StructureDetailTest, WindowFrameAllSubcube) {
    auto result = StructureGenerator::generateWindowFrame({0, 0, 0}, Facing::North, 2, 2, "Wood");
    ASSERT_FALSE(result.voxels.empty());
    EXPECT_EQ(countByLevel(result.voxels, VoxelLevel::Subcube), result.voxels.size());
}

TEST(StructureDetailTest, DoorFrameAllSubcube) {
    auto result = StructureGenerator::generateDoorFrame({0, 0, 0}, Facing::North, 2, 3, "Wood");
    ASSERT_FALSE(result.voxels.empty());
    EXPECT_EQ(countByLevel(result.voxels, VoxelLevel::Subcube), result.voxels.size());
}

TEST(StructureDetailTest, RailingAllSubcube) {
    auto result = StructureGenerator::generateRailing({0, 0, 0}, Facing::North, 4, "Wood");
    ASSERT_FALSE(result.voxels.empty());
    EXPECT_EQ(countByLevel(result.voxels, VoxelLevel::Subcube), result.voxels.size());
}

TEST(StructureDetailTest, HalfWallAllSubcube) {
    auto result = StructureGenerator::generateHalfWall({0, 0, 0}, Facing::North, 4, "Stone");
    ASSERT_FALSE(result.voxels.empty());
    EXPECT_EQ(countByLevel(result.voxels, VoxelLevel::Subcube), result.voxels.size());
}

TEST(StructureDetailTest, PitchedRoofAllSubcube) {
    auto result = StructureGenerator::generatePitchedRoof({0, 0, 0}, Facing::North, 8, 10, "Wood");
    ASSERT_FALSE(result.voxels.empty());
    EXPECT_EQ(countByLevel(result.voxels, VoxelLevel::Subcube), result.voxels.size());
}

// ============================================================================
// Window/door frame sizing
// ============================================================================

TEST(StructureDetailTest, WindowFrameHasSillLintelAndJambs) {
    auto result = StructureGenerator::generateWindowFrame({0, 0, 0}, Facing::North, 2, 2, "Wood");
    // sill = 2, lintel = 2, left jamb = 2, right jamb = 2 => 8 subcubes
    EXPECT_EQ(result.voxels.size(), 8u);
}

TEST(StructureDetailTest, DoorFrameHasLintelAndJambs) {
    auto result = StructureGenerator::generateDoorFrame({0, 0, 0}, Facing::North, 2, 3, "Wood");
    // lintel = 2, left jamb = 3, right jamb = 3 => 8 subcubes
    EXPECT_EQ(result.voxels.size(), 8u);
}

// ============================================================================
// Rough vs Detailed: composites have different voxel counts
// ============================================================================

TEST(StructureDetailTest, HouseRoughHasNoSubcubes) {
    MaterialPalette mat;
    auto result = StructureGenerator::generateHouse({0, 0, 0}, 8, 10, 5, mat, Facing::South, 2, true,
                                                     DetailLevel::Rough);
    EXPECT_EQ(countByLevel(result.voxels, VoxelLevel::Subcube), 0u);
}

TEST(StructureDetailTest, HouseDetailedHasSubcubes) {
    MaterialPalette mat;
    auto result = StructureGenerator::generateHouse({0, 0, 0}, 8, 10, 5, mat, Facing::South, 2, true,
                                                     DetailLevel::Detailed);
    EXPECT_GT(countByLevel(result.voxels, VoxelLevel::Subcube), 0u);
}

TEST(StructureDetailTest, HouseDetailedHasMoreVoxelsThanRough) {
    MaterialPalette mat;
    auto rough    = StructureGenerator::generateHouse({0, 0, 0}, 8, 10, 5, mat, Facing::South, 2, true,
                                                       DetailLevel::Rough);
    auto detailed = StructureGenerator::generateHouse({0, 0, 0}, 8, 10, 5, mat, Facing::South, 2, true,
                                                       DetailLevel::Detailed);
    EXPECT_GT(detailed.voxels.size(), rough.voxels.size());
}

TEST(StructureDetailTest, TavernRoughHasNoSubcubeDetails) {
    MaterialPalette mat;
    // Note: tavern always has subcube stairs when stories > 1, so we use 1 story for this test
    auto result = StructureGenerator::generateTavern({0, 0, 0}, 14, 18, 1, mat, Facing::South, true,
                                                      DetailLevel::Rough);
    EXPECT_EQ(countByLevel(result.voxels, VoxelLevel::Subcube), 0u);
}

TEST(StructureDetailTest, TavernDetailedHasSubcubes) {
    MaterialPalette mat;
    auto result = StructureGenerator::generateTavern({0, 0, 0}, 14, 18, 1, mat, Facing::South, true,
                                                      DetailLevel::Detailed);
    EXPECT_GT(countByLevel(result.voxels, VoxelLevel::Subcube), 0u);
}

TEST(StructureDetailTest, TavernDetailedHasMoreVoxelsThanRough) {
    MaterialPalette mat;
    auto rough    = StructureGenerator::generateTavern({0, 0, 0}, 14, 18, 2, mat, Facing::South, true,
                                                        DetailLevel::Rough);
    auto detailed = StructureGenerator::generateTavern({0, 0, 0}, 14, 18, 2, mat, Facing::South, true,
                                                        DetailLevel::Detailed);
    EXPECT_GT(detailed.voxels.size(), rough.voxels.size());
}

TEST(StructureDetailTest, TowerRoughHasNoSubcubes) {
    auto result = StructureGenerator::generateTower({0, 0, 0}, 4, 12, "Stone", Facing::South,
                                                     DetailLevel::Rough);
    EXPECT_EQ(countByLevel(result.voxels, VoxelLevel::Subcube), 0u);
}

TEST(StructureDetailTest, TowerDetailedHasSubcubes) {
    auto result = StructureGenerator::generateTower({0, 0, 0}, 4, 12, "Stone", Facing::South,
                                                     DetailLevel::Detailed);
    EXPECT_GT(countByLevel(result.voxels, VoxelLevel::Subcube), 0u);
}

// ============================================================================
// JSON routing with detail_level
// ============================================================================

TEST(StructureDetailTest, JsonHouseDefaultIsDetailed) {
    json def = {{"type", "house"}, {"position", {{"x", 0}, {"y", 0}, {"z", 0}}},
                {"width", 8}, {"depth", 10}, {"height", 5}};
    auto result = StructureGenerator::generateFromJson(def);
    // Default detail_level = detailed, should have subcubes
    EXPECT_GT(countByLevel(result.voxels, VoxelLevel::Subcube), 0u);
}

TEST(StructureDetailTest, JsonHouseRoughNoSubcubes) {
    json def = {{"type", "house"}, {"position", {{"x", 0}, {"y", 0}, {"z", 0}}},
                {"width", 8}, {"depth", 10}, {"height", 5}, {"detail_level", "rough"}};
    auto result = StructureGenerator::generateFromJson(def);
    EXPECT_EQ(countByLevel(result.voxels, VoxelLevel::Subcube), 0u);
}

TEST(StructureDetailTest, JsonTavernDetailLevel) {
    json rough = {{"type", "tavern"}, {"position", {{"x", 0}, {"y", 0}, {"z", 0}}},
                  {"width", 14}, {"depth", 18}, {"stories", 2}, {"detail_level", "rough"}};
    json detailed = {{"type", "tavern"}, {"position", {{"x", 0}, {"y", 0}, {"z", 0}}},
                     {"width", 14}, {"depth", 18}, {"stories", 2}, {"detail_level", "detailed"}};
    auto rr = StructureGenerator::generateFromJson(rough);
    auto dr = StructureGenerator::generateFromJson(detailed);
    EXPECT_GT(dr.voxels.size(), rr.voxels.size());
}

TEST(StructureDetailTest, JsonTowerDetailLevel) {
    json rough = {{"type", "tower"}, {"position", {{"x", 0}, {"y", 0}, {"z", 0}}},
                  {"radius", 4}, {"height", 12}, {"detail_level", "rough"}};
    json detailed = {{"type", "tower"}, {"position", {{"x", 0}, {"y", 0}, {"z", 0}}},
                     {"radius", 4}, {"height", 12}, {"detail_level", "detailed"}};
    auto rr = StructureGenerator::generateFromJson(rough);
    auto dr = StructureGenerator::generateFromJson(detailed);
    EXPECT_GT(dr.voxels.size(), rr.voxels.size());
}

// ============================================================================
// New primitives routed via JSON
// ============================================================================

TEST(StructureDetailTest, JsonWindowFrame) {
    json def = {{"type", "window_frame"}, {"position", {{"x", 5}, {"y", 3}, {"z", 0}}},
                {"width", 2}, {"height", 2}, {"material", "Wood"}};
    auto result = StructureGenerator::generateFromJson(def);
    ASSERT_FALSE(result.voxels.empty());
    EXPECT_EQ(countByLevel(result.voxels, VoxelLevel::Subcube), result.voxels.size());
}

TEST(StructureDetailTest, JsonRailing) {
    json def = {{"type", "railing"}, {"position", {{"x", 0}, {"y", 5}, {"z", 0}}},
                {"length", 6}, {"material", "Wood"}};
    auto result = StructureGenerator::generateFromJson(def);
    ASSERT_FALSE(result.voxels.empty());
    EXPECT_EQ(countByLevel(result.voxels, VoxelLevel::Subcube), result.voxels.size());
}

TEST(StructureDetailTest, JsonPitchedRoof) {
    json def = {{"type", "pitched_roof"}, {"position", {{"x", 0}, {"y", 6}, {"z", 0}}},
                {"width", 8}, {"depth", 10}, {"material", "Wood"}};
    auto result = StructureGenerator::generateFromJson(def);
    ASSERT_FALSE(result.voxels.empty());
    EXPECT_EQ(countByLevel(result.voxels, VoxelLevel::Subcube), result.voxels.size());
}

// ============================================================================
// getStructureTypes includes detail_level for composites
// ============================================================================

TEST(StructureDetailTest, StructureTypesIncludeDetailLevel) {
    auto types = StructureGenerator::getStructureTypes();
    bool houseHasDetail = false, tavernHasDetail = false, towerHasDetail = false;
    for (const auto& t : types) {
        if (t["type"] == "house" && t["params"].contains("detail_level")) houseHasDetail = true;
        if (t["type"] == "tavern" && t["params"].contains("detail_level")) tavernHasDetail = true;
        if (t["type"] == "tower" && t["params"].contains("detail_level")) towerHasDetail = true;
    }
    EXPECT_TRUE(houseHasDetail);
    EXPECT_TRUE(tavernHasDetail);
    EXPECT_TRUE(towerHasDetail);
}

TEST(StructureDetailTest, StructureTypesIncludeNewPrimitives) {
    auto types = StructureGenerator::getStructureTypes();
    std::set<std::string> typeNames;
    for (const auto& t : types) typeNames.insert(t["type"].get<std::string>());
    EXPECT_TRUE(typeNames.count("window_frame"));
    EXPECT_TRUE(typeNames.count("door_frame"));
    EXPECT_TRUE(typeNames.count("railing"));
    EXPECT_TRUE(typeNames.count("half_wall"));
    EXPECT_TRUE(typeNames.count("pitched_roof"));
}
