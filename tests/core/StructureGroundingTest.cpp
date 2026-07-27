#include <gtest/gtest.h>

#include "core/ChunkManager.h"
#include "core/StructureBuildService.h"

using namespace Phyxel;
using Core::StructureBuildService;

// ============================================================================
// GROUNDING GATE — a structure must have terrain under EVERY footprint column;
// building on air is refused by default (override: allow_ungrounded).
// RED (measured live 2026-07-21, pre-change): build_structure at (200,16,200)
// on a world whose terrain ends at x=31 returned success + a fully furnished
// croft floating in the void (a whole village east of the generated chunk hung
// in the air the same way).
// These tests pin the REFUSAL side, which returns before any placement; the
// grounded-success and allow_ungrounded override paths run the full place()
// pipeline (not headless-testable today — no test exercises place() without an
// engine) and are proven at L4: the standard village build is the success case,
// and the live refusal probe at (200,200) is the gate case.
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

TEST(StructureGroundingTest, RefusesAirBuildsByDefault) {
    ChunkManager cm;   // headless (WaterManagerTest pattern: VK_NULL_HANDLE init + manual chunk)
    cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
    // A 12x10 stone platform at y=15: world (100..111) x (200..209) — inside chunk (3,0,6).
    auto owned = std::make_unique<Phyxel::Chunk>(glm::ivec3(96, 0, 192));
    owned->initializeForLoading();
    for (int x = 100; x < 112; ++x)
        for (int z = 200; z < 210; ++z)
            owned->addCube(glm::ivec3(x - 96, 15, z - 192));
    cm.chunkMap[glm::ivec3(3, 0, 6)] = owned.get();
    cm.chunks.push_back(std::move(owned));
    ASSERT_TRUE(cm.hasVoxelAt(glm::ivec3(100, 15, 200))) << "test platform not visible";

    StructureBuildService::Deps deps;
    deps.chunkManager = &cm;

    // Fully in the void -> refused with an honest count, nothing placed.
    auto bad = StructureBuildService::buildV2(croftParams(500, 500), deps);
    EXPECT_FALSE(bad.value("success", false));
    EXPECT_NE(bad.value("error", std::string()).find("ungrounded"), std::string::npos)
        << bad.dump();
    EXPECT_EQ(bad.value("ungrounded_columns", 0), 8 * 6);
    EXPECT_FALSE(cm.hasVoxelAt(glm::ivec3(500, 16, 500))) << "refused build placed voxels";

    // Hanging off the platform edge -> refused, and the count is EXACTLY the
    // overhanging columns (x=112..113 -> 2 columns x 6 deep) — which also proves
    // the 46 on-platform columns were correctly recognized as grounded.
    auto edge = StructureBuildService::buildV2(croftParams(106, 200), deps);
    EXPECT_FALSE(edge.value("success", false));
    EXPECT_EQ(edge.value("ungrounded_columns", 0), 2 * 6) << edge.dump();
}
