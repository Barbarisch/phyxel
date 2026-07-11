#include <gtest/gtest.h>

#include "core/FurnitureCatalog.h"
#include "core/FurniturePlacer.h"

using namespace Phyxel::Core;

// ============================================================================
// Furniture quality B — MOUNTING + DATA RECIPES.
//
// Mounting: wall_lantern/tool_rack were catalogued "awaiting wall placement"
// and sat on the FLOOR; the chandelier's ceiling hang was unbuilt. mountFor +
// mountedMicroY give every fixture an honest base Y: sconce at the grounded
// 60 in mounting height, chandelier hung below the ceiling with head
// clearance, everything else on the floor.
//
// Data recipes: recipeFor was a hardcoded 10-branch map; furnishing recipes
// move to resources/furnishing_recipes.json with wealth-tier filtering
// (humble/middling/high from room_program wealth_tier).
//
// RED baseline: the stubs mount everything on the floor and load no recipes.
// ============================================================================

namespace {
bool loadShippedRecipes() {
    for (const char* p : {"resources/furnishing_recipes.json",
                          "../resources/furnishing_recipes.json",
                          "../../resources/furnishing_recipes.json",
                          "../../../resources/furnishing_recipes.json"})
        if (FurniturePlacer::loadRecipesFromFile(p)) return true;
    return false;
}
std::vector<std::string> typesFor(const std::string& purpose, const std::string& tier) {
    // furnish a synthetic 8x6 room of `purpose` and collect the piece types the recipe emits
    ProgStory st;
    ProgRoom room;
    room.id = "r0"; room.purpose = purpose; room.rect = {0, 0, 8, 6};
    st.rooms.push_back(room);
    ProgPortal door; door.a = "exterior"; door.b = "r0"; door.kind = "door";
    door.px = 3; door.pz = 0; door.width = 1; door.height = 2;
    st.portals.push_back(door);
    std::vector<UnplacedFixture> un;
    auto placements = FurniturePlacer::furnish(st, {0, 0, 0}, 17, {}, &un, 2, tier);
    std::vector<std::string> types;
    for (const auto& p : placements) types.push_back(p.type);
    for (const auto& u : un) types.push_back(u.type);   // count unfit pieces as "in recipe" too
    return types;
}
bool hasType(const std::vector<std::string>& v, const std::string& t) {
    for (const auto& x : v)
        if (x == t) return true;
    return false;
}
} // namespace

// Mount kinds are placer data (RED: stub says Floor for everything).
TEST(FurnitureMountTest, MountKindsPerType) {
    EXPECT_EQ(FurniturePlacer::mountFor("wall_lantern"), FurniturePlacer::Mount::Wall);
    EXPECT_EQ(FurniturePlacer::mountFor("tool_rack"), FurniturePlacer::Mount::Wall);
    EXPECT_EQ(FurniturePlacer::mountFor("chandelier"), FurniturePlacer::Mount::Ceiling);
    EXPECT_EQ(FurniturePlacer::mountFor("bed"), FurniturePlacer::Mount::Floor);
    EXPECT_EQ(FurniturePlacer::mountFor("table"), FurniturePlacer::Mount::Floor);
}

// The sconce mounts at the grounded 60 in (14 micro) wall height (RED: floor-placed today).
TEST(FurnitureMountTest, WallSconceMountsAtGroundedHeight) {
    const int surface = 153;                       // a floor top face
    EXPECT_EQ(FurniturePlacer::mountedMicroY("wall_lantern", surface, surface + 27, 6),
              surface + 14);
    EXPECT_EQ(FurniturePlacer::mountedMicroY("tool_rack", surface, surface + 27, 8),
              surface + 9);
}

// The chandelier hangs below the ceiling (1-micro drop), never breaching head clearance.
TEST(FurnitureMountTest, ChandelierHangsFromCeilingWithClearance) {
    const int surface = 153, ceiling = surface + 27;    // 3-cube story
    // template 8 micro tall -> base at ceiling - 8 - 1 = surface + 18 (exactly clearance)
    EXPECT_EQ(FurniturePlacer::mountedMicroY("chandelier", surface, ceiling, 8), ceiling - 9);
    // an oversized fixture in a low room is LIFTED to keep >= 18 micro clearance
    EXPECT_GE(FurniturePlacer::mountedMicroY("chandelier", surface, surface + 20, 12),
              surface + 18);
    // floor furniture ignores the ceiling
    EXPECT_EQ(FurniturePlacer::mountedMicroY("bed", surface, ceiling, 5), surface);
}

// Data recipes load and tier-filter: a humble chamber has no wardrobe/rug; middling does
// (RED: no data file, no tier filtering — the hardcoded map gives everyone everything).
TEST(RecipeDataTest, WealthTierFiltersChamberPieces) {
    ASSERT_TRUE(loadShippedRecipes()) << "furnishing_recipes.json missing/unloadable";
    const auto humble = typesFor("bedchamber", "humble");
    const auto middling = typesFor("bedchamber", "middling");
    EXPECT_TRUE(hasType(humble, "bed"));
    EXPECT_TRUE(hasType(humble, "stool"));
    EXPECT_FALSE(hasType(humble, "wardrobe")) << "a croft bedchamber owns a chest, not a press";
    EXPECT_FALSE(hasType(humble, "rug"));
    EXPECT_TRUE(hasType(middling, "wardrobe"));
    EXPECT_TRUE(hasType(middling, "rug"));
    FurniturePlacer::clearRecipes();
}

// Every type any tier of any data recipe can emit resolves in the catalog (no silent drops).
TEST(RecipeDataTest, EveryRecipeTypeResolvesInTheCatalog) {
    ASSERT_TRUE(loadShippedRecipes());
    for (const auto& purpose : FurniturePlacer::knownPurposes())
        for (const char* tier : {"humble", "middling", "high"})
            for (const auto& t : typesFor(purpose, tier))
                EXPECT_FALSE(FurnitureCatalog::templateFor(t).empty())
                    << "recipe type '" << t << "' (purpose " << purpose << ", tier " << tier
                    << ") has no template";
    FurniturePlacer::clearRecipes();
}

// Without a tier (legacy callers), the full recipe emits — no accidental starvation.
TEST(RecipeDataTest, EmptyTierEmitsEverything) {
    ASSERT_TRUE(loadShippedRecipes());
    const auto all = typesFor("bedchamber", "");
    EXPECT_TRUE(hasType(all, "wardrobe"));
    EXPECT_TRUE(hasType(all, "rug"));
    FurniturePlacer::clearRecipes();
}
