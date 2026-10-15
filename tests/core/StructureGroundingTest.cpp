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

// ============================================================================
// M0 (docs/WorldForgeV2Plan.md §5 M0, "forge gates run read-only before any mutation").
// F5 audit finding: the forge's stage order is footprint (REMOVES overlapping structures
// and placed trees) -> realize -> validate_realized -> place (grounding + vegetation gates),
// so EVERY refusal after `footprint` leaves the removals behind: a neighbour deleted, a
// tree felled, and then "refused". RED: an ungrounded lot that overlaps a registered
// neighbouring structure refuses (correct) AND the neighbour is gone (the residue).
// ============================================================================
#include "core/ObjectTemplateManager.h"
#include "core/PlacedObjectManager.h"

TEST(StructureGroundingTest, ARefusalLeavesNoRemovalsBehind) {
    ChunkManager cm;
    cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
    auto owned = std::make_unique<Phyxel::Chunk>(glm::ivec3(96, 0, 192));
    owned->initializeForLoading();
    for (int x = 100; x < 112; ++x)
        for (int z = 200; z < 210; ++z)
            owned->addCube(glm::ivec3(x - 96, 15, z - 192));
    cm.chunkMap[glm::ivec3(3, 0, 6)] = owned.get();
    cm.chunks.push_back(std::move(owned));

    ObjectTemplateManager templates(nullptr);
    Core::PlacedObjectManager placed(&cm, &templates, nullptr);
    // A neighbour whose XZ bbox overlaps the lot we will try to build on (500,500) — in the
    // void, so the build MUST refuse as ungrounded. Today the overlap removal runs first.
    const std::string neighbour = placed.registerStructure(
        "croft", glm::ivec3(498, 16, 498), 0, glm::ivec3(498, 16, 498), glm::ivec3(505, 20, 505));
    ASSERT_FALSE(neighbour.empty());
    ASSERT_EQ(placed.list().size(), 1u);

    StructureBuildService::Deps deps;
    deps.chunkManager = &cm;
    deps.placedObjects = &placed;
    const auto bad = StructureBuildService::buildV2(croftParams(500, 500), deps);
    ASSERT_TRUE(bad.contains("error")) << bad.dump();
    EXPECT_NE(bad.value("error", std::string()).find("ungrounded"), std::string::npos) << bad.dump();
    // THE invariant: a refused build changes nothing in the world or the registry.
    EXPECT_EQ(placed.list().size(), 1u)
        << "the refusal removed the overlapping neighbour BEFORE the grounding gate ran (refused_at="
        << bad.value("refused_at", std::string("?")) << ")";
    // And the gate that refused must have run before any mutating stage.
    EXPECT_NE(bad.value("refused_at", std::string()), "place")
        << "grounding must be checked before 'footprint' mutates, not inside 'place'";
}

// ============================================================================
// M0 (docs/WorldForgeV2Plan.md §5 M0, "placement into un-generated terrain refuses").
// F5-corrected mechanism: StructureGenerator::place calls ensureChunkAt, which creates an
// EMPTY chunk for a non-resident, unsaved target. In a STREAMING world the streamer then
// treats that chunk as existing and never generates its terrain - a hill with a building
// stuck in a void (the "structures on thin air" family). The grounding gate protects the
// footprint's own chunk; this is about the chunk ABOVE it that a wall or roof reaches.
// RED: today the build succeeds and an empty chunk (3-coord y=1) exists afterwards.
// ============================================================================
#include "core/WorldGenerator.h"

TEST(StructureGroundingTest, PlacementIntoUngeneratedTerrainIsRefused) {
    ChunkManager cm;
    cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
    cm.configureStreamingGeneration(true, WorldGenerator::GenerationType::Perlin, 20260816);
    WorldGenerator* gen = cm.getStreamingGenerator();
    ASSERT_NE(gen, nullptr);
    // Find a lot whose GENERATED surface lies inside chunk y=1 (world y 32..63) across the
    // whole 8x6 footprint: the chunk a croft's roof reaches from a pad at y=28 is terrain there.
    glm::ivec2 hill(-1, -1);
    for (int x = 64; x < 4096 && hill.x < 0; x += 32)
        for (int z = 64; z < 4096; z += 32) {
            bool all = true;
            for (int fx = 0; fx < 8 && all; ++fx)
                for (int fz = 0; fz < 6 && all; ++fz) {
                    const int sy = gen->sampleSurface(x + fx, z + fz).surfaceY;
                    all = (sy >= 36 && sy <= 62);
                }
            if (all) { hill = {x, z}; break; }
        }
    ASSERT_GE(hill.x, 0) << "rig precondition: no 8x6 lot with a generated surface inside chunk y=1";
    // A RESIDENT chunk at y=0 under the lot holding a flat pad at y=28 (so the walls/roof
    // cross y=32 into chunk y=1, which is NOT resident and NOT saved).
    const glm::ivec3 cc = Phyxel::Utils::CoordinateUtils::worldToChunkCoord(glm::ivec3(hill.x, 0, hill.y));
    auto owned = std::make_unique<Phyxel::Chunk>(cc * 32);
    owned->initializeForLoading();
    for (int x = 0; x < 32; ++x)
        for (int z = 0; z < 32; ++z)
            for (int y = 0; y <= 28; ++y) owned->addCube(glm::ivec3(x, y, z));
    cm.chunkMap[cc] = owned.get();
    cm.chunks.push_back(std::move(owned));
    ASSERT_TRUE(cm.hasVoxelAt(glm::ivec3(hill.x, 28, hill.y)));
    const glm::ivec3 above(cc.x, 1, cc.z);
    ASSERT_EQ(cm.chunkMap.count(above), 0u) << "rig: the upper chunk must start non-resident";

    nlohmann::json params = croftParams(hill.x, hill.y);
    params["position"]["y"] = 29;
    StructureBuildService::Deps deps;
    deps.chunkManager = &cm;
    const auto res = StructureBuildService::buildV2(params, deps);
    EXPECT_TRUE(res.contains("error"))
        << "the build was placed into an un-generated chunk: " << res.dump().substr(0, 300);
    if (res.contains("error"))
        EXPECT_NE(res.value("error", std::string()).find("un-generated"), std::string::npos) << res.dump();
    EXPECT_EQ(cm.chunkMap.count(above), 0u)
        << "an EMPTY chunk was materialized over un-generated terrain (the streamer will never fill it)";

    // CONTROL: the same lot in a NON-streaming world. The upper chunk is air by definition
    // there (nothing would ever be generated), so the build must still succeed.
    ChunkManager dry;
    dry.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
    auto owned2 = std::make_unique<Phyxel::Chunk>(cc * 32);
    owned2->initializeForLoading();
    for (int x = 0; x < 32; ++x)
        for (int z = 0; z < 32; ++z)
            for (int y = 0; y <= 28; ++y) owned2->addCube(glm::ivec3(x, y, z));
    dry.chunkMap[cc] = owned2.get();
    dry.chunks.push_back(std::move(owned2));
    StructureBuildService::Deps dryDeps;
    dryDeps.chunkManager = &dry;
    const auto ok = StructureBuildService::buildV2(params, dryDeps);
    EXPECT_FALSE(ok.contains("error")) << "control: a bounded world's air chunk is a legitimate target: " << ok.dump().substr(0, 300);
}
