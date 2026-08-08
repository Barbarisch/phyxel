#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "core/BuildingProgram.h"
#include "core/FurniturePlacer.h"

// ============================================================================
// M4 furnishing passes — heavy before light.
//
// A room is furnished the way a room is really built out: the heavy, built-in,
// often VENTED fixtures land first and claim their wall (a hearth carries a flue
// through the roof — it cannot be shuffled aside for a stool), then the movable
// furniture packs into what is left, then lighting.
//
// RED before M4: pieces placed in recipe-DECLARATION order, so a recipe that
// happened to list a table above a fireplace let the table take the hearth's
// spot. The fix is an ordering, so the falsifiable test is an ordering test:
// declare the light piece FIRST and prove the heavy one still places first.
// ============================================================================

using namespace Phyxel::Core;

namespace {

// A room big enough for several pieces, with a door so door-wall avoidance is exercised.
ProgStory oneRoom(const std::string& purpose, int w = 8, int d = 7) {
    ProgStory st;
    ProgRoom r; r.id = "r0"; r.purpose = purpose; r.rect = {0, 0, w, d};
    st.rooms.push_back(r);
    ProgPortal door; door.a = "exterior"; door.b = "r0"; door.kind = "door";
    door.px = 3; door.pz = 0; door.width = 1; door.height = 2;
    st.portals.push_back(door);
    return st;
}

std::vector<std::string> placedTypesFor(const std::string& purpose, int w = 8, int d = 7) {
    std::vector<UnplacedFixture> un;
    auto placements = FurniturePlacer::furnish(oneRoom(purpose, w, d), {0, 0, 0}, 17, {}, &un, 2, "");
    std::vector<std::string> types;
    for (const auto& p : placements) types.push_back(p.type);
    return types;
}

int firstIndexOf(const std::vector<std::string>& v, const std::string& t) {
    auto it = std::find(v.begin(), v.end(), t);
    return it == v.end() ? -1 : static_cast<int>(it - v.begin());
}

}  // namespace

// The type-data contract the ordering rests on.
TEST(FurnishPassOrder, VentedAndBuiltInFixturesRankHeavy) {
    for (const char* t : {"fireplace", "forge_hearth", "oven_bread"}) {
        EXPECT_EQ(FurniturePlacer::passRank(t), 0) << t << " must be a HEAVY pass fixture";
        EXPECT_TRUE(FurniturePlacer::isVentedFixture(t)) << t << " burns — it needs a flue";
    }
    for (const char* t : {"tavern_bar", "back_bar", "counter"})
        EXPECT_EQ(FurniturePlacer::passRank(t), 0) << t << " is built-in millwork (heavy)";
    for (const char* t : {"bed", "table", "stool", "chest", "bench"})
        EXPECT_EQ(FurniturePlacer::passRank(t), 1) << t << " is movable furniture (light)";
    for (const char* t : {"candle_stand", "wall_lantern", "chandelier"})
        EXPECT_EQ(FurniturePlacer::passRank(t), 2) << t << " belongs to the lighting pass";
    // Not vented: a bar does not burn, and must not demand a chimney.
    for (const char* t : {"tavern_bar", "counter", "bed", "chandelier"})
        EXPECT_FALSE(FurniturePlacer::isVentedFixture(t)) << t << " must not request a flue";
}

// The behavioural claim: with NO data recipes loaded, the hardcoded "hall" recipe
// declares fireplace, table, bench, chair — and "kitchen" declares counter,
// fireplace, stool. In both, every heavy piece must be placed before every light
// piece, regardless of declaration order.
TEST(FurnishPassOrder, HeavyFixturesArePlacedBeforeLightFurniture) {
    FurniturePlacer::clearRecipes();   // exercise the hardcoded fallback path too
    for (const char* purpose : {"hall", "kitchen", "taproom", "forge", "bakehouse"}) {
        const auto types = placedTypesFor(purpose);
        ASSERT_FALSE(types.empty()) << purpose << ": nothing placed";
        int lastHeavy = -1, firstLight = static_cast<int>(types.size());
        for (size_t i = 0; i < types.size(); ++i) {
            const int rank = FurniturePlacer::passRank(types[i]);
            if (rank == 0) lastHeavy = std::max(lastHeavy, static_cast<int>(i));
            else firstLight = std::min(firstLight, static_cast<int>(i));
        }
        if (lastHeavy >= 0 && firstLight < static_cast<int>(types.size()))
            EXPECT_LT(lastHeavy, firstLight)
                << purpose << ": a light piece was placed before a heavy one — pass order broken";
    }
}

// The specific regression: the hardcoded kitchen recipe declares `counter` (heavy)
// then `fireplace` (heavy) then `stool` — but the HALL recipe declares fireplace
// first and table second. Build a purpose whose LIGHT piece is declared first and
// prove the heavy one still wins the wall. "service" (barrel, chest — both light)
// plus taproom gives coverage; here we assert directly on the tavern, whose recipe
// interleaves heavy bar/back_bar with light stools and tables.
TEST(FurnishPassOrder, TaproomBarClaimsItsSpotBeforeSeating) {
    FurniturePlacer::clearRecipes();
    const auto types = placedTypesFor("taproom", 10, 9);
    const int bar   = firstIndexOf(types, "tavern_bar");
    const int stool = firstIndexOf(types, "stool");
    const int table = firstIndexOf(types, "tavern_table");
    if (bar >= 0 && stool >= 0) EXPECT_LT(bar, stool) << "seating placed before the bar";
    if (bar >= 0 && table >= 0) EXPECT_LT(bar, table) << "table placed before the bar";

    // THE TEETH — this is the exact inversion M4 introduces. The hardcoded taproom
    // recipe DECLARES: tavern_bar, back_bar, bar_stool, tavern_table, bench, stool,
    // fireplace, candle_stand — i.e. the fireplace (HEAVY) is declared 7th, AFTER four
    // light seating/table pieces. Before M4 placement followed declaration order, so
    // the hearth got whatever wall was left over. Assert the placed order now INVERTS
    // the declared one; if the sort were removed this fails.
    const int fire  = firstIndexOf(types, "fireplace");
    const int bench = firstIndexOf(types, "bench");
    ASSERT_GE(fire, 0) << "the taproom fireplace did not place — test is not measuring the case";
    if (bench >= 0) EXPECT_LT(fire, bench)
        << "fireplace placed AFTER the bench, i.e. still in recipe-declaration order";
    if (stool >= 0) EXPECT_LT(fire, stool)
        << "fireplace placed AFTER a stool, i.e. still in recipe-declaration order";
    if (table >= 0) EXPECT_LT(fire, table)
        << "fireplace placed AFTER the table, i.e. still in recipe-declaration order";
}

// Determinism is not sacrificed by the reordering: the sort is STABLE, so the same
// room furnishes identically every time (the ForgePattern contract).
TEST(FurnishPassOrder, OrderingIsStableAndDeterministic) {
    FurniturePlacer::clearRecipes();
    const auto a = placedTypesFor("taproom");
    const auto b = placedTypesFor("taproom");
    EXPECT_EQ(a, b) << "furnishing became nondeterministic";
}
