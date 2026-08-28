#include <gtest/gtest.h>

#include <iostream>
#include <set>

#include "core/BuildingProgram.h"
#include "core/RoomLayout.h"
#include "core/RoomProgram.h"

using namespace Phyxel::Core;

// ============================================================================
// Tower house (CityForgePlan M8) — the keep form this engine can structure.
// A tower is STACKED SINGLE ROOMS over a store, which is a different plan from
// the inn's gallery-and-chambers, and its floors differ in purpose (store ->
// hall -> chamber). Both of those are typology data (`upper_plan`,
// `upper_purposes`); this pins that they produce a tower rather than an inn,
// and that every floor is served by a stair.
// ============================================================================

namespace {

bool loadCanon(RoomProgramRegistry& reg) {
    for (const char* p : {"resources/room_program.json", "../resources/room_program.json",
                          "../../resources/room_program.json",
                          "../../../resources/room_program.json"})
        if (reg.loadFromFile(p)) return true;
    return false;
}

BuildingProgram towerProgram(const RoomProgram& rp, int W, int D) {
    BuildingProgram p;
    p.footprintW = W;
    p.footprintD = D;
    p.typology = "tower_house";
    ProgStory s;
    s.height = 3;
    p.stories.push_back(s);          // autofill grows to the typology's story count
    autofillRoomLayout(p, 11u, &rp);
    return p;
}

}  // namespace

TEST(TowerHouseTest, StacksSingleRoomsWithADifferentPurposePerFloor) {
    RoomProgramRegistry reg;
    ASSERT_TRUE(loadCanon(reg));
    const RoomProgram* rp = reg.get("tower_house");
    ASSERT_NE(rp, nullptr) << "tower_house typology must exist";
    EXPECT_EQ(rp->upperPlan, "single") << "a tower stacks single rooms, it is not an inn";
    ASSERT_GE(rp->upperPurposeByStory.size(), 2u) << "a tower's floors differ: hall, then chamber";

    const auto p = towerProgram(*rp, 6, 8);
    ASSERT_EQ(p.stories.size(), static_cast<size_t>(rp->stories));

    // Diagnostic dump — geometry bugs in this pipeline are solved by looking at the
    // actual rects, not by permuting the data (a lesson this typology re-taught).
    for (size_t s = 0; s < p.stories.size(); ++s) {
        std::cout << "  story " << s << ": ";
        for (const auto& r : p.stories[s].rooms)
            std::cout << r.id << "(" << r.purpose << ")["
                      << r.rect.x << "," << r.rect.z << " " << r.rect.w << "x" << r.rect.d << "] ";
        std::cout << "\n    portals:";
        for (const auto& pt : p.stories[s].portals)
            std::cout << " {" << pt.kind << " " << pt.a << "->" << pt.b
                      << " at(" << pt.px << "," << pt.pz << ") w" << pt.width << "}";
        std::cout << "\n    stairs:";
        for (const auto& st : p.stories[s].stairs)
            std::cout << " {" << st.form << " " << st.fromStory << "->" << st.toStory
                      << " [" << st.rect.x << "," << st.rect.z << " "
                      << st.rect.w << "x" << st.rect.d << "]}";
        std::cout << "\n";
    }

    // ONE room per upper floor — the defining tower plan (an inn would give a landing
    // plus chambers here).
    for (size_t s = 1; s < p.stories.size(); ++s)
        EXPECT_EQ(p.stories[s].rooms.size(), 1u)
            << "story " << s << " is not a single stacked room";

    // Purposes differ by floor: the hall sits under the private chamber.
    EXPECT_EQ(p.stories[1].rooms[0].purpose, rp->upperPurposeByStory[0]);
    EXPECT_EQ(p.stories[2].rooms[0].purpose, rp->upperPurposeByStory[1]);
}

TEST(TowerHouseTest, EveryFloorIsServedByAStair) {
    RoomProgramRegistry reg;
    ASSERT_TRUE(loadCanon(reg));
    const RoomProgram* rp = reg.get("tower_house");
    ASSERT_NE(rp, nullptr);
    const auto p = towerProgram(*rp, 6, 8);

    // A stair for every consecutive pair of floors — a tower whose top room has no way
    // up is the failure this typology kept hitting.
    for (size_t s = 0; s + 1 < p.stories.size(); ++s) {
        bool linked = false;
        for (const auto& st : p.stories[s].stairs)
            if (st.fromStory == static_cast<int>(s) && st.toStory == static_cast<int>(s) + 1)
                linked = true;
        EXPECT_TRUE(linked) << "no stair from story " << s << " to " << (s + 1);
    }
}

// THE BUG THIS TYPOLOGY EXPOSED: the stair well hugs one wall for most of its length, so a
// SHORT building's centred entrance opened straight into the shaft — the building could not
// be entered and every upper floor reported "unreachable" while the stair was built fine.
// (tower_house 6x8: door at (0,4), well at [0,1 2x6].) A long building escapes by luck.
TEST(TowerHouseTest, TheEntranceNeverOpensIntoTheStairShaft) {
    RoomProgramRegistry reg;
    ASSERT_TRUE(loadCanon(reg));
    const RoomProgram* rp = reg.get("tower_house");
    ASSERT_NE(rp, nullptr);

    // Sweep the footprints a tower can take — the collision is size-dependent, so one
    // footprint proves nothing.
    for (int W = 5; W <= 8; ++W)
        for (int D = 6; D <= 12; ++D) {
            const auto p = towerProgram(*rp, W, D);
            if (p.stories.empty() || p.stories[0].stairs.empty()) continue;
            const Rect well = p.stories[0].stairs[0].rect;
            for (const auto& pt : p.stories[0].portals) {
                if (pt.kind != "door" || (pt.a != "exterior" && pt.b != "exterior")) continue;
                int cx = pt.px, cz = pt.pz;
                if (pt.px == 0) cx = 0; else if (pt.px == W) cx = W - 1;
                if (pt.pz == 0) cz = 0; else if (pt.pz == D) cz = D - 1;
                const bool inShaft = cx >= well.x && cx < well.x1() &&
                                     cz >= well.z && cz < well.z1();
                EXPECT_FALSE(inShaft)
                    << W << "x" << D << ": the entrance at (" << pt.px << "," << pt.pz
                    << ") opens into the stair shaft [" << well.x << "," << well.z << " "
                    << well.w << "x" << well.d << "] — the building cannot be entered";
            }
        }
}

// The stair well must not swallow the room it lands in: the upper floor is ONE room, so
// the shaft has to leave a walkable remainder beside it, and the floor's door/opening
// cannot sit inside the shaft.
TEST(TowerHouseTest, TheShaftLeavesTheUpperFloorHabitable) {
    RoomProgramRegistry reg;
    ASSERT_TRUE(loadCanon(reg));
    const RoomProgram* rp = reg.get("tower_house");
    ASSERT_NE(rp, nullptr);
    const auto p = towerProgram(*rp, 6, 8);
    ASSERT_GE(p.stories.size(), 2u);
    ASSERT_FALSE(p.stories[0].stairs.empty());

    const Rect well = p.stories[0].stairs[0].rect;
    const Rect room = p.stories[1].rooms[0].rect;
    const int wellArea = well.w * well.d, roomArea = room.w * room.d;
    EXPECT_GT(roomArea, wellArea * 2)
        << "the stair shaft (" << well.w << "x" << well.d << ") eats too much of a "
        << room.w << "x" << room.d << " floor to leave a usable room";
}
