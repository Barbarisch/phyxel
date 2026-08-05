// Megaflora Increment B — flora LAYERS (red-before-green).
//
// A biome historically carried ONE flora config: a single density + spacing + weighted item pool.
// That can't express "sparse giants over a dense understory" — the giants and the understory need
// DIFFERENT spacings in the same biome (giants ~24-32 cubes apart so their canopies don't overlap,
// understory ~4-6 apart to carpet the floor). This is the enchanted-forest requirement.
//
// The fix: an optional "floraLayers" array on a biome, each layer with its own spacing/density/
// items, each running its own independent local-maxima placement. This test loads a biome with a
// dense understory layer (layer 0, the legacy "flora") PLUS a sparse giant layer ("floraLayers"),
// and asserts planFlora returns BOTH pools with the giants strictly sparser than the understory.
// RED before the feature exists: "floraLayers" is ignored, so the giant template never appears.

#include <gtest/gtest.h>
#include "core/WorldGenerator.h"
#include "core/WorldRecipe.h"
#include <glm/glm.hpp>
#include <fstream>
#include <filesystem>
#include <set>
#include <string>

namespace Phyxel {
namespace {

namespace fs = std::filesystem;

struct TempDir {
    fs::path dir;
    TempDir() { dir = fs::temp_directory_path() / "phyxel_flora_layers_test";
                fs::create_directories(dir); }
    TempDir(const TempDir&) = delete;
    ~TempDir() { std::error_code ec; fs::remove_all(dir, ec); }
};

// One biome, whole climate range, with a dense understory (legacy "flora") and a sparse giant layer.
fs::path writeLayeredBiome(const fs::path& dir) {
    fs::path p = dir / "_biomes.json";
    std::ofstream f(p);
    f << R"JSON({
  "biomes": [
    {
      "name": "Enchanted", "surface": "Grass", "subsurface": "Dirt", "deep": "Stone",
      "temp": [0.0, 1.0], "moisture": [0.0, 1.0], "heightScale": 0.0, "heightOffset": 0.0,
      "flora": { "density": 0.9, "spacing": 4,
                 "items": [{"template": "understory_fern", "weight": 1}] },
      "floraLayers": [
        { "density": 0.7, "spacing": 24,
          "items": [{"template": "giant_elder_oak", "weight": 1}] }
      ]
    }
  ]
})JSON";
    return p;
}

TEST(FloraLayersTest, TwoLayersProduceSparseGiantsOverDenseUnderstory) {
    TempDir td;
    fs::path biomes = writeLayeredBiome(td.dir);

    WorldGenerator gen(WorldGenerator::GenerationType::Flat, 7);
    ASSERT_TRUE(gen.loadBiomes(biomes.string()));

    // A large region so both layers get many candidate cells.
    const int N = 256;
    auto placements = gen.planFlora(0, 0, N - 1, N - 1, 0);
    ASSERT_GT(placements.size(), 0u);

    int understory = 0, giants = 0;
    for (const auto& p : placements) {
        if (p.templateName == "understory_fern") ++understory;
        else if (p.templateName == "giant_elder_oak") ++giants;
    }

    // Both pools must appear — the giant layer being present at all is the feature under test.
    EXPECT_GT(understory, 0) << "understory (layer 0) missing";
    EXPECT_GT(giants, 0) << "giant layer ('floraLayers') produced no placements — layers not honored";

    // And the spacings must actually differ: dense understory (spacing 4) must far out-number the
    // sparse giants (spacing 24). Same-spacing would prove the layer config was ignored.
    EXPECT_GT(understory, giants * 3)
        << "understory (" << understory << ") not dense relative to giants (" << giants
        << ") — layer spacings not independent";
}

// World-look C2: every pre-floraLayers world.db recipe (and every recipe synthesized before a
// biome gained its undergrowth band) carries an EMPTY extraLayers for each biome. applyRecipe
// unconditionally cleared b.extraFloraLayers before copying that empty list in — silently
// stripping the biomes.json undergrowth band from every existing world on load. Layer 0 already
// had the guard (`if (!bt.flora.empty())`); this pins the same semantics for the extra bands:
// an ITEM-LESS tune means "no opinion, keep the library", never "delete".
TEST(FloraLayersTest, RecipeWithoutLayersKeepsTheBiomesJsonLayers) {
    TempDir td;
    fs::path biomes = writeLayeredBiome(td.dir);

    WorldGenerator gen(WorldGenerator::GenerationType::Flat, 7);
    ASSERT_TRUE(gen.loadBiomes(biomes.string()));

    // A stored recipe that predates floraLayers: tunes the biome but carries no bands.
    WorldRecipe recipe;
    WorldRecipe::BiomeTune bt;
    bt.name = "Enchanted";
    bt.heightScale = 0.0f;
    bt.floraDensity = 0.9f;
    bt.floraSpacing = 4;
    recipe.biomes.push_back(bt);
    gen.applyRecipe(recipe);

    const int N = 256;
    auto placements = gen.planFlora(0, 0, N - 1, N - 1, 0);
    int giants = 0;
    for (const auto& p : placements)
        if (p.templateName == "giant_elder_oak") ++giants;
    EXPECT_GT(giants, 0)
        << "an item-less recipe clobbered the biomes.json floraLayers — every world whose "
           "recipe predates the band loses its undergrowth/giants on load";
}

}  // namespace
}  // namespace Phyxel
