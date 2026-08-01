#include <gtest/gtest.h>

#include "core/HydrologyMap.h"
#include "core/WorldGenerator.h"
#include "core/WorldRecipe.h"

#include <algorithm>
#include <climits>

// L2 validation for P1 increment 3: the continentalness → base-elevation spline is data-driven via
// the world recipe (art-direction), and it round-trips through world.db JSON persistence. Drives the
// real WorldGenerator. RED before the recipe-spline wiring: an override would have no effect.

namespace Phyxel {
namespace {

int minSurfaceY(WorldGenerator& g, int x0, int z0, int span, int step) {
    int m = INT_MAX;
    for (int z = z0; z <= z0 + span; z += step)
        for (int x = x0; x <= x0 + span; x += step)
            m = std::min(m, g.sampleSurface(x, z).surfaceY);
    return m;
}

TEST(TerrainRecipeTest, HeightSplineOverrideReshapesTerrain) {
    WorldGenerator g(WorldGenerator::GenerationType::Perlin, 123u);
    // Baseline: the default spline dips to the ocean/shelf floor, so low ground exists well below Y=50.
    const int baseMin = minSurfaceY(g, -400, -400, 800, 8);
    ASSERT_LT(baseMin, 50) << "baseline terrain has no low ground to move (region too small?)";

    // Apply a recipe whose spline pins the continental base to Y=220 for ALL continentalness. Every
    // column's floor should jump to ~220 (base 220 + relief>=0 + small biome offset). Without the
    // recipe-spline wiring this override does nothing and baseMin stays < 50 (RED).
    WorldRecipe r;
    r.climateFrequency = g.getTerrainParams().climateFrequency;  // keep biome size unchanged
    r.heightSpline = {{0.0f, 220.0f}, {1.0f, 220.0f}};
    g.applyRecipe(r);

    const int overMin = minSurfaceY(g, -400, -400, 800, 8);
    EXPECT_GT(overMin, 200) << "height-spline override did not raise the terrain floor";
}

TEST(TerrainRecipeTest, SeaLevelOverrideReachesTheHydrologyBake) {
    // THE §2e bug one level down: the recipe/game.json can say where the sea is, but the hydrology
    // bake hardcodes kSeaLevelY — so a world whose terrain never reaches Y=16 gives Priority-Flood
    // no outlet and every basin fills to its spill (perched hillside lakes). RED before the plumb:
    // the bake ignores recipe.seaLevelY and reports seaLevel()==kSeaLevelY regardless.
    WorldGenerator::clearHydroBakeCache();
    WorldGenerator g(WorldGenerator::GenerationType::Mountains, 5501u);
    ASSERT_NE(g.hydrology(), nullptr) << "Mountains must bake hydrology";
    EXPECT_FLOAT_EQ(g.hydrology()->seaLevel(), Core::kSeaLevelY) << "default bake uses the engine default";

    WorldRecipe r;
    r.climateFrequency = g.getTerrainParams().climateFrequency;  // keep biome size unchanged
    r.seaLevelY = 120.0f;
    g.applyRecipe(r);
    ASSERT_NE(g.hydrology(), nullptr);
    EXPECT_FLOAT_EQ(g.hydrology()->seaLevel(), 120.0f) << "recipe seaLevelY must reach the bake";

    // Same seed/type, different sea level → must NOT share a memoized bake (seaLevel is part of
    // the bake identity). Without the cache-key change this returns the 120 bake for both.
    WorldGenerator g2(WorldGenerator::GenerationType::Mountains, 5501u);
    WorldRecipe r2 = r;
    r2.seaLevelY = 40.0f;
    g2.applyRecipe(r2);
    ASSERT_NE(g2.hydrology(), nullptr);
    EXPECT_FLOAT_EQ(g2.hydrology()->seaLevel(), 40.0f) << "hydro bake cache must key on seaLevel";
    EXPECT_FLOAT_EQ(g.hydrology()->seaLevel(), 120.0f) << "first generator's bake must be unaffected";
}

TEST(TerrainRecipeTest, SeaLevelRoundTripsThroughRecipeJson) {
    WorldRecipe r;
    r.seaLevelY = 54.0f;
    WorldRecipe reloaded = WorldRecipe::fromJson(r.toJson());
    EXPECT_FLOAT_EQ(reloaded.seaLevelY, 54.0f) << "seaLevelY lost in JSON round-trip";
    // A legacy recipe (persisted before the field existed) must default to the engine constant
    // the old bake actually used, keeping legacy worlds byte-identical.
    WorldRecipe legacy = WorldRecipe::fromJson("{\"version\":1,\"seed\":7,\"type\":\"Perlin\"}");
    EXPECT_FLOAT_EQ(legacy.seaLevelY, Core::kSeaLevelY) << "legacy recipes must keep the old default";
}

TEST(TerrainRecipeTest, HeightSplineRoundTripsThroughRecipeJson) {
    WorldGenerator g(WorldGenerator::GenerationType::Mountains, 77u);
    WorldRecipe custom;
    custom.climateFrequency = g.getTerrainParams().climateFrequency;
    custom.heightSpline = {{0.0f, -30.0f}, {0.5f, 40.0f}, {1.0f, 260.0f}};  // a 3-point coast→plateau curve
    g.applyRecipe(custom);

    // Persist → reload (world.db path) and apply to a fresh generator with the same seed.
    WorldRecipe reloaded = WorldRecipe::fromJson(g.makeRecipe().toJson());
    ASSERT_EQ(reloaded.heightSpline.size(), 3u) << "spline lost in JSON round-trip";
    WorldGenerator g2(WorldGenerator::GenerationType::Mountains, 77u);
    g2.applyRecipe(reloaded);

    // Both generators must produce identical terrain — the persisted spline reproduces the shape.
    for (int i = 0; i < 200; ++i) {
        int x = i * 41 - 800, z = i * 29 - 500;
        EXPECT_EQ(g.sampleSurface(x, z).surfaceY, g2.sampleSurface(x, z).surfaceY)
            << "mismatch at (" << x << "," << z << ")";
    }
}

}  // namespace
}  // namespace Phyxel
