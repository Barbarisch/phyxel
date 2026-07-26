// Fauna planning — deterministic biome-driven herd-anchor scatter (red-before-green).
//
// planFauna mirrors planFlora: a per-cell local-maximum (Poisson-disk) test scatters
// biome-appropriate animal anchors, as a PURE function of (cell, seed) so a streamed chunk
// and a whole-region pass agree (no double-spawns). This pins the contract the runtime
// FaunaSpawner depends on: placements come only from the biome's fauna pool, are spacing-
// separated, weighted, byte-identical across generators of the same seed, and EMPTY for a
// biome with no fauna block.

#include <gtest/gtest.h>
#include "core/WorldGenerator.h"
#include <glm/glm.hpp>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <string>

namespace Phyxel {
namespace {

namespace fs = std::filesystem;

struct TempDir {
    fs::path dir;
    TempDir() { dir = fs::temp_directory_path() / "phyxel_fauna_plan_test";
                fs::create_directories(dir); }
    TempDir(const TempDir&) = delete;
    ~TempDir() { std::error_code ec; fs::remove_all(dir, ec); }
};

// One whole-climate-range biome carrying a fauna pool (deer weight 3, fox weight 1).
fs::path writeFaunaBiome(const fs::path& dir) {
    fs::path p = dir / "_biomes.json";
    std::ofstream f(p);
    f << R"JSON({
  "biomes": [
    {
      "name": "Meadow", "surface": "Grass", "subsurface": "Dirt", "deep": "Stone",
      "temp": [0.0, 1.0], "moisture": [0.0, 1.0], "heightScale": 0.0, "heightOffset": 0.0,
      "flora": { "density": 0.5, "spacing": 6, "items": [{"template": "bush", "weight": 1}] },
      "fauna": { "density": 0.85, "spacing": 32,
                 "items": [{"animFile": "deer.anim", "weight": 3},
                           {"animFile": "fox.anim",  "weight": 1}] }
    }
  ]
})JSON";
    return p;
}

// A biome with flora but NO fauna block — planFauna must return nothing for it.
fs::path writeNoFaunaBiome(const fs::path& dir) {
    fs::path p = dir / "_nofauna.json";
    std::ofstream f(p);
    f << R"JSON({
  "biomes": [
    {
      "name": "Barren", "surface": "Grass", "subsurface": "Dirt", "deep": "Stone",
      "temp": [0.0, 1.0], "moisture": [0.0, 1.0], "heightScale": 0.0, "heightOffset": 0.0,
      "flora": { "density": 0.5, "spacing": 6, "items": [{"template": "bush", "weight": 1}] }
    }
  ]
})JSON";
    return p;
}

TEST(FaunaPlanTest, ScattersPoolWeightedAndSpacingSeparated) {
    TempDir td;
    fs::path biomes = writeFaunaBiome(td.dir);

    WorldGenerator gen(WorldGenerator::GenerationType::Flat, 7);
    ASSERT_TRUE(gen.loadBiomes(biomes.string()));

    const int N = 512;
    auto herds = gen.planFauna(0, 0, N - 1, N - 1, 0);
    ASSERT_GT(herds.size(), 0u) << "no fauna scattered";

    // Every placement must come from the biome's pool.
    int deer = 0, fox = 0;
    for (const auto& h : herds) {
        ASSERT_TRUE(h.animFile == "deer.anim" || h.animFile == "fox.anim")
            << "off-pool animFile: " << h.animFile;
        (h.animFile == "deer.anim") ? ++deer : ++fox;
    }
    // Weighted pick: deer (weight 3) clearly out-numbers fox (weight 1).
    EXPECT_GT(deer, fox) << "weighted pool ignored (deer " << deer << " vs fox " << fox << ")";

    // Spacing: the local-maximum test guarantees anchors are separated. No two herds may sit
    // closer than ~half the configured spacing (allowing for per-cell jitter).
    const float minSep = 32.0f * 0.4f;
    for (size_t i = 0; i < herds.size(); ++i)
        for (size_t j = i + 1; j < herds.size(); ++j) {
            float dx = float(herds[i].worldX - herds[j].worldX);
            float dz = float(herds[i].worldZ - herds[j].worldZ);
            EXPECT_GE(std::sqrt(dx * dx + dz * dz), minSep)
                << "herds " << i << " and " << j << " too close";
        }
}

TEST(FaunaPlanTest, DeterministicAcrossGeneratorsOfSameSeed) {
    TempDir td;
    fs::path biomes = writeFaunaBiome(td.dir);
    const int N = 384;

    WorldGenerator a(WorldGenerator::GenerationType::Flat, 12345);
    WorldGenerator b(WorldGenerator::GenerationType::Flat, 12345);
    ASSERT_TRUE(a.loadBiomes(biomes.string()));
    ASSERT_TRUE(b.loadBiomes(biomes.string()));

    auto ha = a.planFauna(0, 0, N - 1, N - 1, 0);
    auto hb = b.planFauna(0, 0, N - 1, N - 1, 0);

    ASSERT_EQ(ha.size(), hb.size());
    for (size_t i = 0; i < ha.size(); ++i) {
        EXPECT_EQ(ha[i].animFile, hb[i].animFile);
        EXPECT_EQ(ha[i].worldX, hb[i].worldX);
        EXPECT_EQ(ha[i].worldZ, hb[i].worldZ);
    }

    // A sub-window agrees with the same cells of the full plan (streamed chunk == region pass).
    auto sub = a.planFauna(100, 100, 250, 250, 0);
    for (const auto& s : sub) {
        bool found = false;
        for (const auto& h : ha)
            if (h.worldX == s.worldX && h.worldZ == s.worldZ && h.animFile == s.animFile) { found = true; break; }
        EXPECT_TRUE(found) << "sub-window herd (" << s.worldX << "," << s.worldZ
                           << ") absent from full-region plan — placement not order-independent";
    }
}

TEST(FaunaPlanTest, BiomeWithoutFaunaBlockScattersNothing) {
    TempDir td;
    fs::path biomes = writeNoFaunaBiome(td.dir);

    WorldGenerator gen(WorldGenerator::GenerationType::Flat, 7);
    ASSERT_TRUE(gen.loadBiomes(biomes.string()));

    auto herds = gen.planFauna(0, 0, 511, 511, 0);
    EXPECT_EQ(herds.size(), 0u) << "fauna scattered for a biome with no fauna pool";
}

}  // namespace
}  // namespace Phyxel
