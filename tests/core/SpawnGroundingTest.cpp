#include <gtest/gtest.h>
#include "core/SpawnGrounding.h"

#include <set>
#include <tuple>

using namespace Phyxel::Core;

namespace {
// Build a solidity predicate from an explicit set of solid cells.
std::function<bool(int, int, int)> solidFrom(std::set<std::tuple<int, int, int>> cells) {
    return [cells = std::move(cells)](int x, int y, int z) {
        return cells.count({x, y, z}) > 0;
    };
}
}

// An airborne spawn (empty cell) is left untouched — intentional fall/air spawns
// must not be force-grounded.
TEST(SpawnGroundingTest, AirborneSpawnUnchanged) {
    auto isSolid = solidFrom({{5, 16, 5}, {5, 15, 5}});  // ground below, air above
    EXPECT_EQ(groundSpawnYIfInsideSolid(isSolid, 5, 40, 5), 40);
    EXPECT_EQ(groundSpawnYIfInsideSolid(isSolid, 5, 17, 5), 17);  // resting just above surface
}

// A spawn INSIDE solid is lifted to the first air cell above the solid column.
TEST(SpawnGroundingTest, InsideSolidGroundsToSurface) {
    // Solid column at (10,z=10) filling y=0..16; surface air starts at y=17.
    std::set<std::tuple<int, int, int>> cells;
    for (int y = 0; y <= 16; ++y) cells.insert({10, y, 10});
    auto isSolid = solidFrom(cells);

    EXPECT_EQ(groundSpawnYIfInsideSolid(isSolid, 10, 16, 10), 17) << "buried at the top";
    EXPECT_EQ(groundSpawnYIfInsideSolid(isSolid, 10, 8, 10), 17)  << "buried deep";
    EXPECT_EQ(groundSpawnYIfInsideSolid(isSolid, 10, 0, 10), 17)  << "buried at the floor";
}

// A spawn inside solid with a pocket of air above stops at the FIRST air cell,
// not the very top (the first standable surface encountered climbing up).
TEST(SpawnGroundingTest, StopsAtFirstAirPocket) {
    // Solid at y=0..4, air at 5, solid again 6..8. Buried at y=2 -> climbs to y=5.
    std::set<std::tuple<int, int, int>> cells;
    for (int y = 0; y <= 4; ++y) cells.insert({1, y, 1});
    for (int y = 6; y <= 8; ++y) cells.insert({1, y, 1});
    auto isSolid = solidFrom(cells);
    EXPECT_EQ(groundSpawnYIfInsideSolid(isSolid, 1, 2, 1), 5);
}

// A column solid all the way to the search cap returns the cap (best effort) —
// never loops forever, always returns a finite Y.
TEST(SpawnGroundingTest, SolidToCapReturnsCap) {
    auto isSolid = [](int, int, int) { return true; };  // solid everywhere
    EXPECT_EQ(groundSpawnYIfInsideSolid(isSolid, 0, 10, 0, /*searchUp=*/8), 18);
}

// Negative coordinates behave the same (world coords can be negative).
TEST(SpawnGroundingTest, NegativeCoords) {
    std::set<std::tuple<int, int, int>> cells;
    for (int y = -10; y <= -3; ++y) cells.insert({-7, y, -2});
    auto isSolid = solidFrom(cells);
    EXPECT_EQ(groundSpawnYIfInsideSolid(isSolid, -7, -5, -2), -2);  // climbs above y=-3
}

// A null predicate is tolerated (returns the requested y).
TEST(SpawnGroundingTest, NullPredicateSafe) {
    std::function<bool(int, int, int)> none;
    EXPECT_EQ(groundSpawnYIfInsideSolid(none, 3, 22, 3), 22);
}
