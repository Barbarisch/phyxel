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

TEST(StructureDetailTest, PitchedRoofHasSubcubeStepping) {
    auto result = StructureGenerator::generatePitchedRoof({0, 0, 0}, Facing::North, 8, 10, "Wood");
    ASSERT_FALSE(result.voxels.empty());
    EXPECT_GT(countByLevel(result.voxels, VoxelLevel::Subcube), 0u);
}

// The roof must be one CONTINUOUS slope: every micro column of the footprint is covered
// in plan view (no see-through gaps), and the surface rises exactly 1 subcube per z-row
// toward the ridge. The original implementation emitted only the middle z-third of each
// row (a floating 1-subcube strip with air on both sides) — this test fails on it.
TEST(StructureDetailTest, PitchedRoofIsContinuousSolidWedge) {
    const int W = 8, D = 10;
    auto result = StructureGenerator::generatePitchedRoof({0, 0, 0}, Facing::North, W, D, "Wood");

    // covered[microX][microZ] -> highest covered micro-Y (or -1)
    std::vector<std::vector<int>> top(W * 3, std::vector<int>(D * 3, -1));
    for (const auto& v : result.voxels) {
        const int cx = v.position.x, cy = v.position.y, cz = v.position.z;
        if (v.level == VoxelLevel::Cube) {
            for (int sx = 0; sx < 3; ++sx)
                for (int sz = 0; sz < 3; ++sz)
                    for (int sy = 0; sy < 3; ++sy)
                        top[cx * 3 + sx][cz * 3 + sz] =
                            std::max(top[cx * 3 + sx][cz * 3 + sz], cy * 3 + sy);
        } else if (v.level == VoxelLevel::Subcube) {
            top[cx * 3 + v.subcubePos.x][cz * 3 + v.subcubePos.z] =
                std::max(top[cx * 3 + v.subcubePos.x][cz * 3 + v.subcubePos.z],
                         cy * 3 + v.subcubePos.y);
        }
    }

    for (int mx = 0; mx < W * 3; ++mx) {
        for (int mz = 0; mz < D * 3; ++mz) {
            // 1) full plan-view coverage — a roof with holes leaks sky
            ASSERT_GE(top[mx][mz], 0) << "uncovered micro column (" << mx << "," << mz << ")";
            // 2) the surface follows the gable profile: rise = distance from the nearer eave
            const int z = mz / 3;
            const int expected = std::min(z, D - 1 - z);
            EXPECT_EQ(top[mx][mz], expected)
                << "surface height off-profile at micro (" << mx << "," << mz << ")";
        }
    }
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
    // The wedge is solid: full-cube core + subcube-stepped surface (see
    // PitchedRoofIsContinuousSolidWedge for the continuity invariant).
    EXPECT_GT(countByLevel(result.voxels, VoxelLevel::Subcube), 0u);
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
