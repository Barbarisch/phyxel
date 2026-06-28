#include <gtest/gtest.h>

#include <map>
#include <string>

#include "core/BuildingProgram.h"
#include "core/FurniturePlacer.h"

using namespace Phyxel::Core;

// ============================================================================
// Furniture placement: PACK along walls (multiple pieces per wall) + HONEST reporting. The runtime
// pass found the taproom silently lost bar_stool/bench/candle_stand — the old placer capped at ONE
// piece per wall and dropped the overflow without counting it ("0 skipped" lied). This proves the fix:
// the 7-item taproom recipe now fully furnishes a normal taproom, and a piece that genuinely can't
// fit is REPORTED in `unplaced`, never silently dropped.
// ============================================================================

namespace {
// the real taproom recipe's footprints (cubes), from the asset metrics.
std::map<std::string, Footprint> taproomFootprints() {
    return {
        {"tavern_bar",   {3, 1}}, {"back_bar", {3, 1}}, {"bar_stool", {1, 1}},
        {"tavern_table", {2, 1}}, {"bench",    {2, 1}}, {"fireplace", {2, 1}},
        {"candle_stand", {1, 1}},
    };
}
ProgStory taproom(int w, int d) {
    ProgStory s; s.height = 3;
    ProgRoom r; r.id = "taproom"; r.purpose = "taproom"; r.rect = {0, 0, w, d};
    s.rooms.push_back(r);
    // one exterior door on the west wall (realistic — every room has an entrance somewhere)
    ProgPortal e; e.a = "exterior"; e.b = "taproom"; e.px = 0; e.pz = d / 2; e.width = 1; e.height = 2;
    s.portals.push_back(e);
    return s;
}
int countType(const std::vector<FurniturePlacement>& v, const std::string& t) {
    int n = 0; for (const auto& p : v) if (p.type == t) ++n; return n;
}
bool unplacedHas(const std::vector<UnplacedFixture>& v, const std::string& t) {
    for (const auto& u : v) if (u.type == t) return true; return false;
}
} // namespace

// A normal taproom (8x7) fully furnishes — including the pieces the old one-per-wall placer dropped
// (bar_stool, bench, candle_stand). Nothing is left unplaced.
TEST(PlacementReportTest, TaproomFullyFurnishedNoSilentDrop) {
    std::vector<UnplacedFixture> unplaced;
    const auto out = FurniturePlacer::furnish(taproom(8, 7), glm::ivec3(0), 16,
                                              taproomFootprints(), &unplaced);
    // The three the runtime pass found MISSING must now each be placed.
    EXPECT_EQ(countType(out, "bar_stool"), 1)    << "the bar still has no stool (silent drop)";
    EXPECT_EQ(countType(out, "bench"), 1)        << "no bench (silent drop)";
    EXPECT_EQ(countType(out, "candle_stand"), 1) << "no candle stand / lighting fixture (silent drop)";
    // and the rest of the recipe.
    EXPECT_EQ(countType(out, "tavern_bar"), 1);
    EXPECT_EQ(countType(out, "back_bar"), 1);
    EXPECT_EQ(countType(out, "tavern_table"), 1);
    EXPECT_EQ(countType(out, "fireplace"), 1);
    EXPECT_TRUE(unplaced.empty()) << "a normal taproom reported pieces it couldn't fit";
    EXPECT_EQ(out.size(), 7u) << "expected all 7 recipe pieces in the taproom";
}

// PACKING teeth: multiple pieces share a wall. The old cap was 4 wall pieces (one each) + 1 centre = 5
// max; the 7-item recipe needs >5 placed, which is only possible if pieces pack along walls.
TEST(PlacementReportTest, PacksMoreThanOnePerWall) {
    std::vector<UnplacedFixture> unplaced;
    const auto out = FurniturePlacer::furnish(taproom(8, 7), glm::ivec3(0), 16,
                                              taproomFootprints(), &unplaced);
    EXPECT_GT(out.size(), 5u) << "no more than one piece per wall — packing is not working";
}

// HONEST reporting teeth: a tiny room can't fit the wide pieces. They must appear in `unplaced`
// (reported), NOT vanish silently. (tavern_bar is 3 wide; a 2-wide room has no wall long enough.)
TEST(PlacementReportTest, UnfittablePiecesAreReportedNotDropped) {
    std::vector<UnplacedFixture> unplaced;
    const auto out = FurniturePlacer::furnish(taproom(2, 2), glm::ivec3(0), 16,
                                              taproomFootprints(), &unplaced);
    EXPECT_FALSE(unplaced.empty()) << "a tiny room dropped furniture silently (nothing reported)";
    EXPECT_TRUE(unplacedHas(unplaced, "tavern_bar")) << "the 3-wide bar can't fit a 2-wide room but wasn't reported";
    // EVERY recipe piece is accounted for: placed + unplaced == recipe size (nothing vanishes).
    EXPECT_EQ(out.size() + unplaced.size(), 7u) << "pieces vanished (placed + unplaced != recipe)";
}

// Without the out-param, furnish still works (back-compat) and simply doesn't report drops.
TEST(PlacementReportTest, NullUnplacedIsSafe) {
    const auto out = FurniturePlacer::furnish(taproom(8, 7), glm::ivec3(0), 16, taproomFootprints());
    EXPECT_EQ(out.size(), 7u);
}
