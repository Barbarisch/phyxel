#include <gtest/gtest.h>

#include "core/ChunkManager.h"
#include "core/StructureBuildService.h"

using namespace Phyxel;
using Core::StructureBuildService;

// ============================================================================
// VEGETATION GATE — a structure must never generate THROUGH a tree (user report
// 2026-08-07: a house built overlapping an existing tree, trunk fused into the
// wall). Default behavior clears the WHOLE tree from the lot (flood from the
// footprint); {"keep_vegetation": true} refuses the build instead.
// This suite pins the REFUSAL side headlessly (it returns before placement,
// same pattern as StructureGroundingTest); the default clearing path runs the
// full place() pipeline and is proven at L4 (build over a live tree, assert
// vegetation_cleared_cells > 0 and no Log/Leaf left in the walls).
// RED before the gate existed: this build sailed through realize/place with the
// tree still standing inside the footprint.
// ============================================================================

namespace {
nlohmann::json croftParams(int x, int z) {
    return {{"schema", "v2"}, {"type", "house"}, {"typology", "croft"},
            {"style", "timber_cottage"},
            {"position", {{"x", x}, {"y", 16}, {"z", z}}},
            {"footprint", nlohmann::json::array({8, 6})},
            {"substructure", "slab"},
            {"stories", nlohmann::json::array({nlohmann::json{{"height", 3}}})}};
}
} // namespace

TEST(VegetationGateTest, KeepVegetationRefusesBuildOverATree) {
    ChunkManager cm;   // headless (StructureGroundingTest pattern)
    cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
    auto owned = std::make_unique<Phyxel::Chunk>(glm::ivec3(96, 0, 192));
    owned->initializeForLoading();
    // 12x10 stone platform at y=15: world (100..111) x (200..209).
    for (int x = 100; x < 112; ++x)
        for (int z = 200; z < 210; ++z)
            owned->addCube(glm::ivec3(x - 96, 15, z - 192), "Stone");
    // A tree ON the platform: 4-cube Log trunk + a 3x3 Leaf canopy layer.
    for (int y = 16; y <= 19; ++y)
        owned->addCube(glm::ivec3(106 - 96, y, 204 - 192), "Log");
    int canopyCells = 0;
    for (int x = 105; x <= 107; ++x)
        for (int z = 203; z <= 205; ++z) {
            owned->addCube(glm::ivec3(x - 96, 20, z - 192), "Leaf");
            ++canopyCells;
        }
    cm.chunkMap[glm::ivec3(3, 0, 6)] = owned.get();
    cm.chunks.push_back(std::move(owned));
    ASSERT_TRUE(cm.hasVoxelAt(glm::ivec3(106, 16, 204))) << "test trunk not visible";

    StructureBuildService::Deps deps;
    deps.chunkManager = &cm;

    // Footprint (104..111) x (202..207) swallows trunk + canopy -> refused, with
    // an honest tree-cell count, and the tree is left standing (nothing placed).
    auto params = croftParams(104, 202);
    params["keep_vegetation"] = true;
    auto res = StructureBuildService::buildV2(params, deps);
    EXPECT_FALSE(res.value("success", false));
    EXPECT_NE(res.value("error", std::string()).find("vegetation"), std::string::npos)
        << res.dump();
    EXPECT_EQ(res.value("vegetation_cells", 0), 4 + canopyCells) << res.dump();
    EXPECT_TRUE(cm.hasVoxelAt(glm::ivec3(106, 18, 204))) << "refused build removed the tree";
    EXPECT_TRUE(cm.hasVoxelAt(glm::ivec3(105, 20, 203))) << "refused build removed canopy";

    // Same build with the footprint clear of the tree -> the gate does not fire.
    // (Positioned fully on the platform but past the canopy: x 100..107 is out of
    // reach only if we shift z; use z 206.. beyond canopy z<=205 minus footprint
    // depth. The 8x6 croft at (104,202) was the overlap case; a build elsewhere
    // on this small platform would still need place() — so we only pin that the
    // ERROR names vegetation, not grounding, proving the gate (not the grounding
    // refusal) rejected the overlap case above.)
    EXPECT_EQ(res.value("error", std::string()).find("ungrounded"), std::string::npos)
        << "grounding, not the vegetation gate, refused the build: " << res.dump();
}
