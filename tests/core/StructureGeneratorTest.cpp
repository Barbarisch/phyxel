#include <gtest/gtest.h>
#include "core/StructureGenerator.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <set>
#include <unordered_set>

using namespace Phyxel::Core;
using json = nlohmann::json;

// ============================================================================
// Helper: count unique positions (detect no duplicates)
// ============================================================================
namespace {
struct IVec3Hash {
    size_t operator()(const glm::ivec3& v) const {
        return std::hash<int>()(v.x) ^ (std::hash<int>()(v.y) << 10) ^ (std::hash<int>()(v.z) << 20);
    }
};

size_t uniqueCount(const std::vector<VoxelPlacement>& voxels) {
    std::unordered_set<glm::ivec3, IVec3Hash> positions;
    for (const auto& v : voxels) positions.insert(v.position);
    return positions.size();
}

bool allMaterial(const std::vector<VoxelPlacement>& voxels, const std::string& mat) {
    return std::all_of(voxels.begin(), voxels.end(),
                       [&mat](const VoxelPlacement& v) { return v.material == mat; });
}
} // anonymous namespace

// ============================================================================
// Facing helpers
// ============================================================================

TEST(StructureGeneratorTest, FacingFromString) {
    EXPECT_EQ(StructureGenerator::facingFromString("north"), Facing::North);
    EXPECT_EQ(StructureGenerator::facingFromString("East"), Facing::East);
    EXPECT_EQ(StructureGenerator::facingFromString("south"), Facing::South);
    EXPECT_EQ(StructureGenerator::facingFromString("West"), Facing::West);
    EXPECT_EQ(StructureGenerator::facingFromString("unknown"), Facing::South); // default
}

TEST(StructureGeneratorTest, FacingToString) {
    EXPECT_EQ(StructureGenerator::facingToString(Facing::North), "north");
    EXPECT_EQ(StructureGenerator::facingToString(Facing::East), "east");
    EXPECT_EQ(StructureGenerator::facingToString(Facing::South), "south");
    EXPECT_EQ(StructureGenerator::facingToString(Facing::West), "west");
}

TEST(StructureGeneratorTest, RotateLocalNorthIdentity) {
    glm::ivec3 pos(2, 3, 4);
    auto result = StructureGenerator::rotateLocal(pos, Facing::North, 8, 10);
    EXPECT_EQ(result, pos);
}

TEST(StructureGeneratorTest, RotateLocalSouth180) {
    // South: (x,y,z) -> (w-1-x, y, d-1-z)
    glm::ivec3 pos(0, 0, 0);
    auto result = StructureGenerator::rotateLocal(pos, Facing::South, 4, 6);
    EXPECT_EQ(result, glm::ivec3(3, 0, 5));
}

TEST(StructureGeneratorTest, RotateLocalEast90) {
    // East: (x,y,z) -> (d-1-z, y, x)
    glm::ivec3 pos(1, 0, 2);
    auto result = StructureGenerator::rotateLocal(pos, Facing::East, 4, 6);
    EXPECT_EQ(result, glm::ivec3(3, 0, 1)); // (6-1-2, 0, 1) = (3, 0, 1)
}

// ============================================================================
// Primitives: Box
// ============================================================================

TEST(StructureGeneratorTest, GenerateBoxSolid) {
    auto result = StructureGenerator::generateBox({0, 0, 0}, 3, 3, 3, "Stone", false);
    EXPECT_EQ(result.voxels.size(), 27u); // 3*3*3
    EXPECT_TRUE(allMaterial(result.voxels, "Stone"));
}

TEST(StructureGeneratorTest, GenerateBoxHollow) {
    auto result = StructureGenerator::generateBox({0, 0, 0}, 5, 5, 5, "Stone", true);
    // Total 125, interior (3*3*3=27) removed
    EXPECT_EQ(result.voxels.size(), 125u - 27u);
    EXPECT_TRUE(allMaterial(result.voxels, "Stone"));
}

TEST(StructureGeneratorTest, GenerateBoxPosition) {
    auto result = StructureGenerator::generateBox({10, 20, 30}, 2, 2, 2, "Wood");
    EXPECT_EQ(result.voxels.size(), 8u);
    // Check minimum position
    auto minX = std::min_element(result.voxels.begin(), result.voxels.end(),
                                  [](const VoxelPlacement& a, const VoxelPlacement& b) { return a.position.x < b.position.x; });
    EXPECT_EQ(minX->position.x, 10);
}

// ============================================================================
// Primitives: Walls
// ============================================================================

TEST(StructureGeneratorTest, GenerateWalls) {
    auto result = StructureGenerator::generateWalls({0, 0, 0}, 6, 3, 6, "Stone");
    // 6*3*6 = 108 total, interior (4*3*4 = 48) removed
    EXPECT_EQ(result.voxels.size(), 108u - 48u);
}

// ============================================================================
// Primitives: Floor
// ============================================================================

TEST(StructureGeneratorTest, GenerateFloor) {
    auto result = StructureGenerator::generateFloor({0, 0, 0}, 5, 5, "Wood");
    EXPECT_EQ(result.voxels.size(), 25u); // 5*5
    // All at y=0
    for (const auto& v : result.voxels) {
        EXPECT_EQ(v.position.y, 0);
    }
}

// ============================================================================
// Primitives: Room
// ============================================================================

TEST(StructureGeneratorTest, GenerateRoom) {
    MaterialPalette mat;
    mat.wall = "Stone";
    mat.floor = "Wood";
    mat.roof = "Wood";
    auto result = StructureGenerator::generateRoom({0, 0, 0}, 6, 4, 6, mat);
    // Floor: 36, Walls: (6*3*6 - 4*3*4) = 60, Ceiling: 36
    // Total = 36 + 60 + 36 = 132
    EXPECT_EQ(result.voxels.size(), 132u);
}

// ============================================================================
// Primitives: Openings
// ============================================================================

TEST(StructureGeneratorTest, GenerateDoorOpening) {
    auto positions = StructureGenerator::generateDoorOpening({5, 0, 0}, 2, 3);
    EXPECT_EQ(positions.size(), 6u); // 2*3
}

TEST(StructureGeneratorTest, GenerateWindowOpening) {
    auto positions = StructureGenerator::generateWindowOpening({5, 2, 0}, 2, 2);
    EXPECT_EQ(positions.size(), 4u); // 2*2
}

// ============================================================================
// Primitives: Staircase
// ============================================================================

TEST(StructureGeneratorTest, GenerateStaircase) {
    auto result = StructureGenerator::generateStaircase({0, 0, 0}, Facing::North, 5, 2, "Stone");
    EXPECT_EQ(result.voxels.size(), 10u); // 5 steps * 2 wide
    EXPECT_TRUE(allMaterial(result.voxels, "Stone"));
}

// ============================================================================
// Furniture
// ============================================================================

TEST(StructureGeneratorTest, GenerateTable) {
    auto result = StructureGenerator::generateTable({0, 0, 0}, Facing::North, "Wood");
    // 4 legs + 6 top = 10
    EXPECT_EQ(result.voxels.size(), 10u);
    EXPECT_TRUE(allMaterial(result.voxels, "Wood"));
}

TEST(StructureGeneratorTest, GenerateChair) {
    auto result = StructureGenerator::generateChair({0, 0, 0}, Facing::North, "Wood");
    // leg + seat + back = 3
    EXPECT_EQ(result.voxels.size(), 3u);
}

TEST(StructureGeneratorTest, GenerateCounter) {
    auto result = StructureGenerator::generateCounter({0, 0, 0}, Facing::North, 6, "Wood");
    EXPECT_EQ(result.voxels.size(), 12u); // 6 * 2 (base + top)
}

TEST(StructureGeneratorTest, GenerateBed) {
    auto result = StructureGenerator::generateBed({0, 0, 0}, Facing::North, "Wood");
    // 2*3 base + 2 headboard = 8
    EXPECT_EQ(result.voxels.size(), 8u);
}

// ============================================================================
// Composites: House
// ============================================================================

TEST(StructureGeneratorTest, GenerateHouseBasic) {
    MaterialPalette mat;
    auto result = StructureGenerator::generateHouse({0, 0, 0}, 8, 10, 5, mat, Facing::South, 2, false,
                                                     DetailLevel::Rough);
    // Should have floor + walls + roof minus door + windows
    EXPECT_GT(result.voxels.size(), 0u);
    EXPECT_EQ(uniqueCount(result.voxels), result.voxels.size()); // no duplicates
    // Should have a Home location marker
    EXPECT_EQ(result.locations.size(), 1u);
    EXPECT_EQ(result.locations[0].type, LocationType::Home);
}

TEST(StructureGeneratorTest, GenerateHouseFurnished) {
    MaterialPalette mat;
    auto unfurnished = StructureGenerator::generateHouse({0, 0, 0}, 8, 10, 5, mat, Facing::South, 0, false);
    auto furnished = StructureGenerator::generateHouse({0, 0, 0}, 8, 10, 5, mat, Facing::South, 0, true);
    EXPECT_GT(furnished.voxels.size(), unfurnished.voxels.size());
}

TEST(StructureGeneratorTest, GenerateHouseFacing) {
    MaterialPalette mat;
    auto south = StructureGenerator::generateHouse({0, 0, 0}, 8, 10, 5, mat, Facing::South, 0, false);
    auto east  = StructureGenerator::generateHouse({0, 0, 0}, 8, 10, 5, mat, Facing::East, 0, false);
    // Different facing should produce same voxel count but different positions
    EXPECT_EQ(south.voxels.size(), east.voxels.size());
}

// ============================================================================
// Composites: Tavern
// ============================================================================

TEST(StructureGeneratorTest, GenerateTavernBasic) {
    MaterialPalette mat;
    auto result = StructureGenerator::generateTavern({0, 0, 0}, 14, 18, 2, mat, Facing::South, true);
    EXPECT_GT(result.voxels.size(), 500u); // large structure
    EXPECT_EQ(result.locations.size(), 1u);
    EXPECT_EQ(result.locations[0].type, LocationType::Tavern);
}

TEST(StructureGeneratorTest, GenerateTavernMultiStory) {
    MaterialPalette mat;
    auto single = StructureGenerator::generateTavern({0, 0, 0}, 14, 18, 1, mat, Facing::South, false);
    auto multi  = StructureGenerator::generateTavern({0, 0, 0}, 14, 18, 2, mat, Facing::South, false);
    EXPECT_GT(multi.voxels.size(), single.voxels.size());
}

// ============================================================================
// Composites: Tower
// ============================================================================

TEST(StructureGeneratorTest, GenerateTower) {
    auto result = StructureGenerator::generateTower({0, 0, 0}, 4, 10, "Stone", Facing::South);
    EXPECT_GT(result.voxels.size(), 100u);
    EXPECT_EQ(result.locations.size(), 1u);
    EXPECT_EQ(result.locations[0].type, LocationType::GuardPost);
}

// ============================================================================
// Composites: Wall Segment
// ============================================================================

TEST(StructureGeneratorTest, GenerateWallSegment) {
    auto result = StructureGenerator::generateWallSegment({0, 0, 0}, {10, 0, 0}, 3, "Stone", 1);
    EXPECT_EQ(result.voxels.size(), 33u); // 11 positions * 3 height
}

// ============================================================================
// JSON-driven generation
// ============================================================================

TEST(StructureGeneratorTest, GenerateFromJsonBox) {
    json def = {
        {"type", "box"},
        {"position", {{"x", 0}, {"y", 0}, {"z", 0}}},
        {"width", 4}, {"height", 3}, {"depth", 4},
        {"material", "Stone"}, {"hollow", false}
    };
    auto result = StructureGenerator::generateFromJson(def);
    EXPECT_EQ(result.voxels.size(), 48u); // 4*3*4
}

TEST(StructureGeneratorTest, GenerateFromJsonHouse) {
    json def = {
        {"type", "house"},
        {"position", {{"x", 10}, {"y", 20}, {"z", 30}}},
        {"width", 8}, {"depth", 10}, {"height", 5},
        {"materials", {{"wall", "Stone"}, {"floor", "Wood"}, {"roof", "Wood"}}},
        {"facing", "south"}, {"windows", 2}, {"furnished", false}
    };
    auto result = StructureGenerator::generateFromJson(def);
    EXPECT_GT(result.voxels.size(), 0u);
    EXPECT_EQ(result.locations.size(), 1u);
}

TEST(StructureGeneratorTest, GenerateFromJsonTavern) {
    json def = {
        {"type", "tavern"},
        {"position", {{"x", 0}, {"y", 0}, {"z", 0}}},
        {"width", 14}, {"depth", 18}, {"stories", 2},
        {"facing", "south"}
    };
    auto result = StructureGenerator::generateFromJson(def);
    EXPECT_GT(result.voxels.size(), 500u);
}

TEST(StructureGeneratorTest, GenerateFromJsonUnknownType) {
    json def = {{"type", "spaceship"}, {"position", {{"x", 0}, {"y", 0}, {"z", 0}}}};
    auto result = StructureGenerator::generateFromJson(def);
    EXPECT_EQ(result.voxels.size(), 0u);
}

TEST(StructureGeneratorTest, GenerateFromJsonFurniture) {
    json def = {{"type", "table"}, {"position", {{"x", 5}, {"y", 1}, {"z", 5}}}, {"facing", "north"}};
    auto result = StructureGenerator::generateFromJson(def);
    EXPECT_EQ(result.voxels.size(), 10u);
}

// ============================================================================
// MaterialPalette
// ============================================================================

TEST(StructureGeneratorTest, MaterialPaletteFromJson) {
    json j = {{"wall", "Metal"}, {"floor", "Ice"}, {"roof", "Cork"}};
    auto mat = MaterialPalette::fromJson(j);
    EXPECT_EQ(mat.wall, "Metal");
    EXPECT_EQ(mat.floor, "Ice");
    EXPECT_EQ(mat.roof, "Cork");
    EXPECT_EQ(mat.stairs, "Stone"); // default
    EXPECT_EQ(mat.furniture, "Wood"); // default
}

TEST(StructureGeneratorTest, MaterialPaletteFromJsonDefaults) {
    json j = json::object();
    auto mat = MaterialPalette::fromJson(j);
    EXPECT_EQ(mat.wall, "Stone");
    EXPECT_EQ(mat.floor, "Wood");
}

// ============================================================================
// getStructureTypes
// ============================================================================

TEST(StructureGeneratorTest, GetStructureTypes) {
    auto types = StructureGenerator::getStructureTypes();
    EXPECT_TRUE(types.is_array());
    EXPECT_GE(types.size(), 10u); // At least 10 types

    // Check that house and tavern are listed
    bool hasHouse = false, hasTavern = false;
    for (const auto& t : types) {
        if (t["type"] == "house") hasHouse = true;
        if (t["type"] == "tavern") hasTavern = true;
    }
    EXPECT_TRUE(hasHouse);
    EXPECT_TRUE(hasTavern);
}

// ============================================================================
// Placement (without ChunkManager — null check)
// ============================================================================

TEST(StructureGeneratorTest, PlaceWithNullChunkManager) {
    auto structure = StructureGenerator::generateBox({0, 0, 0}, 3, 3, 3, "Stone");
    auto result = StructureGenerator::place(nullptr, structure);
    EXPECT_EQ(result.placed, 0);
    EXPECT_EQ(result.failed, 27);
}

TEST(StructureGeneratorTest, PlaceExceedsVoxelLimit) {
    // Generate a huge structure
    StructureResult structure;
    for (int i = 0; i < 100001; ++i) {
        structure.voxels.push_back({{i, 0, 0}, "Stone"});
    }
    auto result = StructureGenerator::place(nullptr, structure);
    EXPECT_EQ(result.placed, 0);
    EXPECT_EQ(result.failed, 100001);
}

// ============================================================================
// VoxelLevel and Subcube Staircase
// ============================================================================

TEST(StructureGeneratorTest, SubcubeStaircaseHasSubcubeVoxels) {
    auto result = StructureGenerator::generateSubcubeStaircase({0, 0, 0}, Facing::North,
                                                                3, 2, "Stone");
    EXPECT_GT(result.voxels.size(), 0u);

    // All voxels should be at subcube level
    for (const auto& v : result.voxels) {
        EXPECT_EQ(v.level, VoxelLevel::Subcube);
    }
}

TEST(StructureGeneratorTest, SubcubeStaircaseMaterial) {
    auto result = StructureGenerator::generateSubcubeStaircase({0, 0, 0}, Facing::North,
                                                                2, 1, "Wood");
    for (const auto& v : result.voxels) {
        EXPECT_EQ(v.material, "Wood");
    }
}

TEST(StructureGeneratorTest, SubcubeStaircaseMoreVoxelsThanCubeStaircase) {
    auto cubeResult = StructureGenerator::generateStaircase({0, 0, 0}, Facing::North,
                                                             5, 2, "Stone");
    auto subcubeResult = StructureGenerator::generateSubcubeStaircase({0, 0, 0}, Facing::North,
                                                                       5, 2, "Stone");

    // Subcube staircase has 3 steps per block height, so more voxels
    EXPECT_GT(subcubeResult.voxels.size(), cubeResult.voxels.size());
}

TEST(StructureGeneratorTest, SubcubeStaircaseSubcubePosRange) {
    auto result = StructureGenerator::generateSubcubeStaircase({0, 0, 0}, Facing::North,
                                                                2, 1, "Stone");
    for (const auto& v : result.voxels) {
        EXPECT_GE(v.subcubePos.x, 0);
        EXPECT_LE(v.subcubePos.x, 2);
        EXPECT_GE(v.subcubePos.y, 0);
        EXPECT_LE(v.subcubePos.y, 2);
        EXPECT_GE(v.subcubePos.z, 0);
        EXPECT_LE(v.subcubePos.z, 2);
    }
}

TEST(StructureGeneratorTest, SubcubeStaircaseHeight1Has3Steps) {
    // 1 block height = 3 subcube steps
    auto result = StructureGenerator::generateSubcubeStaircase({0, 0, 0}, Facing::North,
                                                                1, 1, "Stone");
    // Should have at least 3 step positions (3 subcube Y levels at 0,1,2)
    // Count distinct subcubePos.y values
    std::set<int> yLevels;
    for (const auto& v : result.voxels) {
        yLevels.insert(v.subcubePos.y);
    }
    EXPECT_EQ(yLevels.size(), 3u); // Y=0, Y=1, Y=2
}

TEST(StructureGeneratorTest, CubeStaircaseDefaultLevel) {
    auto result = StructureGenerator::generateStaircase({0, 0, 0}, Facing::North,
                                                         3, 2, "Stone");
    // Cube-level staircase should have Cube level for all voxels
    for (const auto& v : result.voxels) {
        EXPECT_EQ(v.level, VoxelLevel::Cube);
    }
}

TEST(StructureGeneratorTest, SubcubeStaircaseFromJson) {
    json def = {
        {"type", "subcube_staircase"},
        {"position", {{"x", 10}, {"y", 5}, {"z", 10}}},
        {"facing", "east"},
        {"height", 3},
        {"width", 2},
        {"material", "Metal"}
    };
    auto result = StructureGenerator::generateFromJson(def);
    EXPECT_GT(result.voxels.size(), 0u);

    for (const auto& v : result.voxels) {
        EXPECT_EQ(v.level, VoxelLevel::Subcube);
        EXPECT_EQ(v.material, "Metal");
    }
}

TEST(StructureGeneratorTest, GetStructureTypesIncludesSubcubeStaircase) {
    auto types = StructureGenerator::getStructureTypes();
    bool found = false;
    for (const auto& t : types) {
        if (t["type"] == "subcube_staircase") found = true;
    }
    EXPECT_TRUE(found);
}

TEST(StructureGeneratorTest, VoxelPlacementDefaultLevel) {
    VoxelPlacement vp;
    EXPECT_EQ(vp.level, VoxelLevel::Cube);
    EXPECT_EQ(vp.subcubePos, glm::ivec3(0));
    EXPECT_EQ(vp.microcubePos, glm::ivec3(0));
}
