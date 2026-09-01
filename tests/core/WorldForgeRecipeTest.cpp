#include <gtest/gtest.h>

#include "core/WorldForgePlan.h"
#include "core/WorldGenerator.h"
#include "core/WorldRecipe.h"

using namespace Phyxel;

// ============================================================================
// WorldForge ↔ WorldRecipe wiring (docs/WorldForge.md M0): params persistence,
// legacy recipes untouched, and the recipe-seed authority fix (the DB recipe is
// the source of truth for a world — docs/WorldModel.md — but applyRecipe used
// to ignore recipe.seed, so editing game.json silently re-seeded the world and
// would drift any plan keyed on the seed). Red-before-green: RecipeSeedAuthority
// ran RED before applyRecipe adopted the stored seed.
// ============================================================================

// THE prerequisite fix: a stored recipe's seed WINS over the constructor seed.
TEST(WorldForgeRecipeTest, RecipeSeedAuthority) {
    WorldGenerator gen(WorldGenerator::GenerationType::Perlin, 111);
    WorldRecipe r = gen.makeRecipe();
    ASSERT_EQ(r.seed, 111u);
    r.seed = 222;
    gen.applyRecipe(r);
    EXPECT_EQ(gen.getSeed(), 222u)
        << "applyRecipe must adopt the stored recipe's seed (DB is the source of truth)";
}

// Seed 0 in a recipe means "unowned" (pre-fix recipes and synthesized defaults): keep the
// constructor seed rather than collapsing every legacy world onto seed 0.
TEST(WorldForgeRecipeTest, RecipeSeedZeroKeepsConstructorSeed) {
    WorldGenerator gen(WorldGenerator::GenerationType::Perlin, 111);
    WorldRecipe r = gen.makeRecipe();
    r.seed = 0;
    gen.applyRecipe(r);
    EXPECT_EQ(gen.getSeed(), 111u);
}

// worldforge params round-trip through the recipe JSON.
TEST(WorldForgeRecipeTest, RecipeWorldForgeRoundTrip) {
    WorldRecipe r;
    r.seed = 42;
    r.worldforge.enabled = true;
    r.worldforge.siteCount = 4;
    r.worldforge.regionRadius = 1024.0f;
    r.worldforge.minSpacing = 350.0f;
    r.worldforge.maxSpacing = 1300.0f;
    r.worldforge.sitePins = {{100, -200}};
    const WorldRecipe back = WorldRecipe::fromJson(r.toJson());
    EXPECT_TRUE(back.worldforge == r.worldforge);
    EXPECT_EQ(back.seed, 42u);
}

// A legacy recipe (no worldforge key) parses to DISABLED, and a disabled recipe writes NO
// worldforge key — stored recipe JSON for legacy worlds is byte-identical to before.
TEST(WorldForgeRecipeTest, LegacyRecipeDisabledAndKeyless) {
    const WorldRecipe legacy = WorldRecipe::fromJson("{\"version\":1,\"seed\":7}");
    EXPECT_FALSE(legacy.worldforge.enabled);
    WorldRecipe r;
    r.seed = 7;
    EXPECT_EQ(r.toJson().find("worldforge"), std::string::npos)
        << "disabled worldforge must not appear in the recipe JSON";
}

// A generator with worldforge disabled (every legacy world) bakes NO plan; enabling it via
// the recipe bakes one, deterministically across independent generators.
TEST(WorldForgeRecipeTest, GeneratorBakesPlanOnlyWhenEnabled) {
    WorldGenerator::clearHydroBakeCache();
    WorldGenerator plain(WorldGenerator::GenerationType::Perlin, 777001);
    EXPECT_EQ(plain.worldForge(), nullptr);

    WorldRecipe r = plain.makeRecipe();
    r.worldforge.enabled = true;
    r.worldforge.siteCount = 3;
    r.worldforge.regionRadius = 1536.0f;
    WorldGenerator a(WorldGenerator::GenerationType::Perlin, 777001);
    a.applyRecipe(r);
    ASSERT_NE(a.worldForge(), nullptr);
    EXPECT_TRUE(a.worldForge()->params().enabled);

    WorldGenerator b(WorldGenerator::GenerationType::Perlin, 777001);
    b.applyRecipe(r);
    ASSERT_NE(b.worldForge(), nullptr);
    EXPECT_EQ(a.worldForge()->planHash(), b.worldForge()->planHash())
        << "same seed + params must bake an identical plan";
}
