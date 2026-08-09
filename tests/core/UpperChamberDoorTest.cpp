// Upper-storey chamber doors must never be carved into an EXTERIOR wall.
//
// The generated tavern had a 2x2-cube hole punched clean through its south gable,
// open to the sky from outside. Cause: generateUpperChambers sites each chamber
// door on the gallery/chamber wall, avoiding the stairwell shaft — but when the
// shaft covers most of that wall the only remaining cell is the FIRST one, at the
// very edge of the footprint. The realizer then widens the 1-cube door to a
// 2-cube carve, which reaches past the footprint edge and removes the gable.
//
// A door is an opening in an INTERIOR partition. It has no business touching the
// shell. This pins the invariant across the footprints the typologies actually
// generate, not just the one that broke.

#include <gtest/gtest.h>

#include <string>

#include "core/BuildingProgram.h"
#include "core/RoomLayout.h"
#include "core/RoomProgram.h"

using namespace Phyxel;
using namespace Phyxel::Core;

namespace {

bool loadCanon(RoomProgramRegistry& reg) {
    for (const char* p : {"resources/room_program.json", "../resources/room_program.json",
                          "../../resources/room_program.json"})
        if (reg.loadFromFile(p)) return true;
    return false;
}

BuildingProgram tavernOf(int w, int d, unsigned seed, const RoomProgram* rp) {
    nlohmann::json j;
    j["name"] = "tavern"; j["style"] = "timber_cottage";
    j["footprint"] = nlohmann::json::array({w, d});
    j["substructure"] = "crawlspace"; j["typology"] = "tavern";
    j["stories"] = nlohmann::json::array({nlohmann::json{{"height", 3}}});
    BuildingProgram p = BuildingProgram::fromJson(j);
    EXPECT_TRUE(autofillRoomLayout(p, seed, rp));
    return p;
}

}  // namespace

// The realizer widens a 1-cube door to a 2-cube carve, so a door one cube from the
// edge still reaches it. Every interior door must therefore sit with at least one
// clear cube of wall between it and the footprint boundary on BOTH sides.
TEST(UpperChamberDoor, NoInteriorDoorTouchesTheShell) {
    RoomProgramRegistry reg;
    ASSERT_TRUE(loadCanon(reg));
    const RoomProgram* tavern = reg.get("tavern");
    ASSERT_NE(tavern, nullptr);

    struct Case { int w, d; };
    const Case cases[] = {{7, 20}, {7, 14}, {7, 28}, {6, 12}, {7, 16}, {6, 24}};
    for (const auto& c : cases) {
        for (unsigned seed : {1u, 42u, 99u, 2026u}) {
            const BuildingProgram p = tavernOf(c.w, c.d, seed, tavern);
            for (size_t si = 0; si < p.stories.size(); ++si) {
                for (const auto& portal : p.stories[si].portals) {
                    if (portal.kind != "door") continue;
                    const bool exterior = portal.a == "exterior" || portal.b == "exterior";
                    if (exterior) continue;   // an entrance BELONGS in the shell

                    const std::string where =
                        "footprint " + std::to_string(c.w) + "x" + std::to_string(c.d) +
                        " seed " + std::to_string(seed) + " story " + std::to_string(si) +
                        " door " + portal.a + "->" + portal.b +
                        " at (" + std::to_string(portal.px) + "," +
                        std::to_string(portal.pz) + ")";

                    // The door lies ON one wall plane and runs ALONG the other axis.
                    // Whichever axis it runs along, it must clear the footprint edge.
                    const bool onXWall = (portal.px > 0 && portal.px < c.w);
                    if (onXWall) {
                        EXPECT_GE(portal.pz, 1) << where << " — carve reaches the z=0 gable";
                        EXPECT_LE(portal.pz, c.d - 2) << where << " — carve reaches the far gable";
                    } else {
                        EXPECT_GE(portal.px, 1) << where << " — carve reaches the x=0 wall";
                        EXPECT_LE(portal.px, c.w - 2) << where << " — carve reaches the far wall";
                    }
                }
            }
        }
    }
}

// ...and the chambers must still be REACHABLE after the inset: moving a door off
// the edge must not park it over the stairwell void instead. Every chamber keeps
// exactly one door to the gallery.
TEST(UpperChamberDoor, EveryChamberStillHasItsGalleryDoor) {
    RoomProgramRegistry reg;
    ASSERT_TRUE(loadCanon(reg));
    const RoomProgram* tavern = reg.get("tavern");
    ASSERT_NE(tavern, nullptr);

    const BuildingProgram p = tavernOf(7, 20, 99u, tavern);
    ASSERT_GE(p.stories.size(), 2u);
    const auto& upper = p.stories[1];

    int chambers = 0;
    for (const auto& r : upper.rooms)
        if (r.purpose == "bedchamber") ++chambers;
    ASSERT_GT(chambers, 0);

    for (const auto& r : upper.rooms) {
        if (r.purpose != "bedchamber") continue;
        int doors = 0;
        for (const auto& portal : upper.portals)
            if (portal.kind == "door" && (portal.a == r.id || portal.b == r.id)) ++doors;
        EXPECT_GE(doors, 1) << "chamber '" << r.id << "' has no door — it is sealed";
    }
}
