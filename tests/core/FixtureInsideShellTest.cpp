#include <gtest/gtest.h>

#include "core/RoomLayout.h"
#include "core/BuildingProgram.h"
#include "core/FurniturePlacer.h"

using namespace Phyxel::Core;

// ============================================================================
// KI-5g — furniture generates OUTSIDE walls (USER observation; live evidence: a
// wardrobe on grass at ~(58,17,14), two cells WEST of its building's x=60 wall,
// L-plan winged solar room). This L2 check pins the first invariant of the
// walkability-by-construction validator: every placement the placer emits must
// lie INSIDE the union of the story's room rects. If this test is GREEN while
// the live defect exists, the fault is downstream in the consumer's spawn math
// (microWorldPos / mounts / insets) — that is itself diagnostic progress.
// ============================================================================

namespace {
bool inAnyRoom(const ProgStory& st, int lx, int lz) {
    for (const auto& r : st.rooms)
        if (lx >= r.rect.x && lx < r.rect.x1() && lz >= r.rect.z && lz < r.rect.z1())
            return true;
    return false;
}
} // namespace

// The ACTUAL live escape (root-caused from the fixture registry): the L-tavern's
// UPPER story was laid out on the full rect while the ground was an L, so upstairs
// chambers — and their wardrobes — hovered over the empty notch at (64,y21,13).
// A winged ground floor must cap the building at one story until winged upper
// layouts exist.
TEST(FixtureInsideShellTest, WingedGroundFloorTruncatesUpperStories) {
    BuildingProgram p;
    p.name = "lplan_2story"; p.style = "stone_keep";
    p.footprintW = 7; p.footprintD = 16;
    p.substructure = "slab"; p.footprintShape = "L";
    ProgStory s0; s0.height = 3; p.stories.push_back(s0);
    ProgStory s1; s1.height = 3; p.stories.push_back(s1);   // would overhang the notch
    ASSERT_TRUE(autofillRoomLayout(p, 7u, nullptr));
    ASSERT_GE(p.stories[0].rooms.size(), 3u) << "winged ground did not apply";
    EXPECT_EQ(p.stories.size(), 1u)
        << "winged ground floor kept an upper story - its rooms/furniture would "
           "hover over the L-notch (KI-5g live case)";
}

TEST(FixtureInsideShellTest, WingedLayoutPlacementsStayInsideRooms) {
    // Reproduce the live defective building's shape: 7x16 L, winged rooms
    // (hall/service/solar), typology-less so the winged path applies post-reorder.
    BuildingProgram p;
    p.name = "lplan_probe"; p.style = "stone_keep";
    p.footprintW = 7; p.footprintD = 16;
    p.substructure = "slab"; p.footprintShape = "L";
    ProgStory st0; st0.height = 3; p.stories.push_back(st0);
    ASSERT_TRUE(autofillRoomLayout(p, 7u, nullptr));
    const ProgStory& st = p.stories[0];
    ASSERT_GE(st.rooms.size(), 3u);

    // Real-ish footprints incl. the offender (wardrobe 1x2) and multi-cell pieces.
    std::map<std::string, Footprint> fps;
    fps["bed"] = {2, 3};
    fps["wardrobe"] = {1, 2};
    fps["rug"] = {2, 3};
    fps["table"] = {2, 2};
    fps["chest"] = {1, 1};

    const glm::ivec3 origin(60, 0, 1);      // the live building's origin
    // extTMicro = 9: stone_keep's clamped cube-thick wall (the live style).
    auto placements = FurniturePlacer::furnish(st, origin, 17, fps, nullptr, 9, "middling");
    ASSERT_FALSE(placements.empty());

    for (const auto& pl : placements) {
        const int lx = pl.worldPos.x - origin.x;
        const int lz = pl.worldPos.z - origin.z;
        EXPECT_TRUE(lx >= 0 && lx < p.footprintW && lz >= 0 && lz < p.footprintD)
            << pl.type << " (" << pl.room << ") anchor OUTSIDE the footprint at local ("
            << lx << "," << lz << ") world (" << pl.worldPos.x << "," << pl.worldPos.z << ")";
        EXPECT_TRUE(inAnyRoom(st, lx, lz))
            << pl.type << " (" << pl.room << ") anchor outside every room at local ("
            << lx << "," << lz << ")";
    }
}
