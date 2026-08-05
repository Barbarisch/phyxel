// Far-tree impostors (world-look A1 rethink, 2026-08-02) — "in the real world a tree can be
// seen from a kilometer away". The chunk-squash representation was rejected by the user
// ("weird floating voxels"); this is its replacement: the far-terrain worker asks the
// DETERMINISTIC flora plan which trees exist on each tile and emits one impostor instance per
// tree. These tests pin the data side (the plan → instances contract); the card rendering is
// verified visually at runtime.
//
// RED before the feature: FarTileMesh::trees exists but nothing fills it.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>

#include "core/WorldGenerator.h"
#include "graphics/FarTerrainMesher.h"
#include "graphics/FarTerrainTypes.h"

using namespace Phyxel;
using namespace Phyxel::Graphics;

namespace {

namespace fs = std::filesystem;

struct TempDir {
    fs::path dir;
    TempDir() { dir = fs::temp_directory_path() / "phyxel_far_tree_test";
                fs::create_directories(dir); }
    TempDir(const TempDir&) = delete;
    ~TempDir() { std::error_code ec; fs::remove_all(dir, ec); }
};

/// One all-climate forest biome with a dense tree pool — guarantees every tile has trees,
/// independent of what resources/biomes.json currently ships.
fs::path writeForestBiome(const fs::path& dir) {
    fs::path p = dir / "_biomes.json";
    std::ofstream f(p);
    f << R"JSON({
  "biomes": [
    {
      "name": "TestForest", "surface": "Grass", "subsurface": "Dirt", "deep": "Stone",
      "temp": [0.0, 1.0], "moisture": [0.0, 1.0], "heightScale": 0.3, "heightOffset": 0.0,
      "flora": { "density": 0.9, "spacing": 5,
                 "items": [
                   { "template": "forge_oak_m", "weight": 3 },
                   { "template": "forge_spruce_l", "weight": 2 },
                   { "template": "forge_bush_s", "weight": 2 }
                 ] }
    }
  ]
})JSON";
    return p;
}

std::unique_ptr<WorldGenerator> forestGenerator(const fs::path& biomes, uint32_t seed = 99) {
    auto gen = std::make_unique<WorldGenerator>(WorldGenerator::GenerationType::Perlin, seed);
    EXPECT_TRUE(gen->loadBiomes(biomes.string()));
    return gen;
}

FarMaterialResolver fakeResolver() {
    return [](const std::string&, int) -> uint16_t { return 0; };
}

} // namespace

// THE POINT: a far tile over forest carries tree impostor instances from the flora plan.
TEST(FarTreeImpostorTest, ForestTileCarriesTreeInstances) {
    TempDir td;
    auto biomes = writeForestBiome(td.dir);
    FarTerrainMesher mesher(forestGenerator(biomes), fakeResolver());

    FarTileKey key{1, 3, 3};             // an arbitrary ring-1 tile away from origin
    FarTileMesh mesh = mesher.buildTile(key, /*step=*/2);

    EXPECT_GT(mesh.trees.size(), 10u)
        << "a 128u forest tile at density 0.9 / spacing 5 should carry dozens of trees — "
           "the far world renders empty without them";
}

// Instances must be sane: tile-local coords inside the tile, positive sizes, valid class.
TEST(FarTreeImpostorTest, InstancesAreTileLocalAndSane) {
    TempDir td;
    auto biomes = writeForestBiome(td.dir);
    FarTerrainMesher mesher(forestGenerator(biomes), fakeResolver());

    FarTileKey key{1, -2, 5};
    FarTileMesh mesh = mesher.buildTile(key, 2);
    ASSERT_FALSE(mesh.trees.empty());
    for (const auto& t : mesh.trees) {
        EXPECT_GE(t.localX, 0.0f);
        EXPECT_LT(t.localX, float(mesh.tileSize));
        EXPECT_GE(t.localZ, 0.0f);
        EXPECT_LT(t.localZ, float(mesh.tileSize));
        EXPECT_GT(t.height, 1.0f);
        EXPECT_LT(t.height, 40.0f);
        EXPECT_GT(t.canopyR, 0.2f);
        EXPECT_LE(t.packed & 0x3u, 3u);
        // Anchored AT or BELOW the tile's quantized surface — floating trees are exactly the
        // defect this representation replaces.
        // (worldY vs mesh bounds: must sit within the tile's vertical range, not above it.)
        EXPECT_LE(t.worldY, mesh.maxY + 0.001f)
            << "tree base floats above the far-tile surface";
    }
}

// Same seed, same tile → identical instances (the plan is pure; two workers must agree).
TEST(FarTreeImpostorTest, DeterministicAcrossMesherInstances) {
    TempDir td;
    auto biomes = writeForestBiome(td.dir);
    FarTerrainMesher a(forestGenerator(biomes), fakeResolver());
    FarTerrainMesher b(forestGenerator(biomes), fakeResolver());

    FarTileKey key{2, 1, -1};
    FarTileMesh ma = a.buildTile(key, 4);
    FarTileMesh mb = b.buildTile(key, 4);

    ASSERT_EQ(ma.trees.size(), mb.trees.size());
    for (size_t i = 0; i < ma.trees.size(); ++i) {
        EXPECT_EQ(ma.trees[i].localX, mb.trees[i].localX);
        EXPECT_EQ(ma.trees[i].worldY, mb.trees[i].worldY);
        EXPECT_EQ(ma.trees[i].packed, mb.trees[i].packed);
    }
}

// Bushes/ferns/shrubs are NOT impostors — at 500u+ they are sub-pixel; emitting them would
// triple instance counts for nothing. The pool above deliberately mixes forge_bush_s in.
TEST(FarTreeImpostorTest, UndergrowthIsFilteredOut) {
    TempDir td;
    auto biomes = writeForestBiome(td.dir);
    FarTerrainMesher mesher(forestGenerator(biomes), fakeResolver());

    FarTileMesh mesh = mesher.buildTile(FarTileKey{1, 0, 4}, 2);
    ASSERT_FALSE(mesh.trees.empty());
    // The pool is 3:2:2 oak:spruce:bush — if bushes leaked in as impostors, the shortest
    // instances would be bush-scale (~1-2u). All impostors must be tree-scale.
    for (const auto& t : mesh.trees) {
        EXPECT_GE(t.height, 4.0f) << "an undergrowth template leaked into the impostor set";
    }
}

// Coarser rings subsample: the same world area must carry FEWER impostors at step 8 than the
// sum of its step-2 tiles — far bands stay bounded by thinning, not by vanishing.
TEST(FarTreeImpostorTest, CoarserRingsSubsampleNotVanish) {
    TempDir td;
    auto biomes = writeForestBiome(td.dir);
    FarTerrainMesher mesher(forestGenerator(biomes), fakeResolver());

    // One step-8 tile covers 512u; the same area is 4x4 step-2 tiles (128u each).
    FarTileMesh coarse = mesher.buildTile(FarTileKey{3, 1, 1}, 8);
    size_t fineTotal = 0;
    for (int tz = 0; tz < 4; ++tz)
        for (int tx = 0; tx < 4; ++tx) {
            // step-2 tile grid: 512u tile (1,1) spans world 512..1024 → fine tiles 4..7.
            FarTileMesh fine = mesher.buildTile(FarTileKey{1, 4 + tx, 4 + tz}, 2);
            fineTotal += fine.trees.size();
        }
    EXPECT_GT(coarse.trees.size(), 0u) << "far ring lost its trees entirely";
    EXPECT_LT(coarse.trees.size(), fineTotal)
        << "far ring is NOT subsampled — instance counts will explode with distance";
}

// Structure exclusion zones (user repro 2026-08-02: LOD trees faded in THROUGH the village
// buildings on zoom-out). planFlora is the pristine generator plan; a settlement build EDITS
// chunks (persisted), so near fields have no trees there — the far tier must drop planned
// trees inside placed-structure footprints or it renders phantoms.
TEST(FarTreeImpostorTest, ExclusionZonesDropTreesInsideStructureFootprints) {
    TempDir td;
    auto biomes = writeForestBiome(td.dir);
    FarTerrainMesher mesher(forestGenerator(biomes), fakeResolver());

    // Baseline: the tile carries trees, some inside the rect we are about to exclude.
    const FarTileKey key{1, 3, 3};
    FarTileMesh before = mesher.buildTile(key, 2);
    ASSERT_FALSE(before.trees.empty());
    const glm::ivec2 o = before.originXZ;
    // Exclude the middle half of the tile (world coords).
    const float ts = float(before.tileSize);
    const glm::vec4 rect(o.x + ts * 0.25f, o.y + ts * 0.25f,
                         o.x + ts * 0.75f, o.y + ts * 0.75f);
    // Recover the plan's integer column from localX (= worldX - o.x + 0.5) so the counts
    // here use the SAME coordinates planTrees filters on (no half-voxel boundary flips).
    auto inRect = [&](const FarTreeInstance& t) {
        const float wx = float(o.x) + t.localX - 0.5f, wz = float(o.y) + t.localZ - 0.5f;
        return wx >= rect.x && wx <= rect.z && wz >= rect.y && wz <= rect.w;
    };
    size_t insideBefore = 0;
    for (const auto& t : before.trees) insideBefore += inRect(t) ? 1 : 0;
    ASSERT_GT(insideBefore, 0u) << "test rect covers no planned trees — pick a denser tile";

    mesher.setTreeExclusions({rect});
    FarTileMesh after = mesher.buildTile(key, 2);

    size_t insideAfter = 0;
    for (const auto& t : after.trees) insideAfter += inRect(t) ? 1 : 0;
    EXPECT_EQ(insideAfter, 0u) << "planned trees survived inside a structure footprint — "
                                  "these render as phantom trees through buildings";
    // Trees OUTSIDE the rect are untouched — exclusion must not thin the rest of the tile.
    EXPECT_EQ(after.trees.size(), before.trees.size() - insideBefore);
}
