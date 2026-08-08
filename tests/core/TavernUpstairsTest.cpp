#include <gtest/gtest.h>

#include <fstream>

#include "core/BuildingProgram.h"
#include "core/RoomLayout.h"
#include "core/RoomProgram.h"
#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"
#include "core/TraversalProbe.h"

using namespace Phyxel::Core;

// ============================================================================
// GENERATIVE MULTI-STORY — finishing the inn's UPSTAIRS. The tavern typology declares 2 stories +
// guest-chamber lodging; autofillRoomLayout must (a) grow a 1-story program to 2, (b) fill the upper
// floor with guest chambers, and (c) GENERATE THE CONNECTING STAIR (the circulation that was missing
// — before this, a multi-story program had disconnected, unreachable floors). The realizer/stair/
// traversal are harness-proven; this proves the GENERATOR produces a climbable multi-story building.
// ============================================================================

namespace {
StyleProfile innStyle() {
    StyleProfileRegistry reg;
    reg.loadFromJson(nlohmann::json::parse(R"({
        "timber_cottage": { "roof_style":"gable", "foundation":"crawlspace",
            "thickness": { "exterior_wall":0.333, "interior_wall":0.222, "foundation_wall":0.667,
                           "floor":0.333, "ceiling":0.222 },
            "materials": { "structure":"Wood", "floor":"Wood", "roof":"Wood", "foundation":"Stone" },
            "roof": { "pitch":0.8 } } })"));
    return *reg.get("timber_cottage");
}
bool loadCanon(RoomProgramRegistry& reg) {
    for (const char* p : {"resources/room_program.json", "../resources/room_program.json",
                          "../../resources/room_program.json", "../../../resources/room_program.json"}) {
        std::ifstream f(p);
        if (f.good()) return reg.loadFromFile(p);
    }
    return false;
}
// A 1-story EMPTY tavern program (height 3, 16x7), autofilled from the shipped tavern typology —
// the same core seam the build handler calls. The caller gives ONE story; the typology grows it.
BuildingProgram autofilledTavern(const RoomProgram* rp) {
    BuildingProgram p;
    p.name = "inn"; p.style = "timber_cottage"; p.footprintW = 16; p.footprintD = 7;
    p.substructure = "crawlspace"; p.function = "tavern"; p.typology = "tavern";
    ProgStory s; s.height = 3; p.stories.push_back(s);     // ONE story in, as the real caller gives
    autofillRoomLayout(p, 7u, rp);
    return p;
}

// Room-centre micro point at a given story's floor.
glm::ivec3 roomCentre(const StructureRealizer::ShellResult& sh, const ProgRoom& r, size_t story) {
    const int floorY = sh.floorTopByStory[story];
    return glm::ivec3((r.rect.x + r.rect.w / 2) * 9 + 4, floorY, (r.rect.z + r.rect.d / 2) * 9 + 4);
}
// L3: a character-box walks from one room to another ACROSS the whole building (both floors in
// bounds), so a cross-story walk must use the generated stair. Goal is the destination room centre.
bool walkRooms(const StructureRealizer::ShellResult& sh, const BuildingProgram& p,
               const glm::ivec3& start, const ProgRoom& to, size_t toStory) {
    const int W = p.footprintW * 9, D = p.footprintD * 9;
    const int topY = sh.floorTopByStory.back();
    const glm::ivec3 g = roomCentre(sh, to, toStory);
    TraversalProbe probe([&](int x, int y, int z) { return sh.canvas.occupiedMicro(x, y, z); },
                         AgentBox{2, 16, 4});
    const glm::ivec3 bLo(0, sh.floorTopByStory[0] - 2, 0), bHi(W, topY + 30, D);
    return probe.reachable(start, glm::ivec3(g.x - 2, g.y - 1, g.z - 2),
                           glm::ivec3(g.x + 2, g.y + 1, g.z + 2), bLo, bHi);
}
} // namespace

// Autofill grows the 1-story tavern to 2 stories, with guest chambers upstairs and a stair down.
TEST(TavernUpstairsTest, AutofillGeneratesUpstairsAndStair) {
    RoomProgramRegistry reg;
    if (!loadCanon(reg)) GTEST_SKIP() << "room_program.json not reachable";
    const RoomProgram* rp = reg.get("tavern");
    ASSERT_NE(rp, nullptr);
    ASSERT_EQ(rp->stories, 2) << "tavern typology must declare 2 stories";

    BuildingProgram p = autofilledTavern(rp);
    ASSERT_EQ(p.stories.size(), 2u) << "autofill did not grow the inn to 2 stories";

    // ground floor still has the taproom; upper floor is guest chambers.
    bool groundHasTaproom = false;
    for (const auto& r : p.stories[0].rooms) if (r.purpose == "taproom") groundHasTaproom = true;
    EXPECT_TRUE(groundHasTaproom) << "ground floor lost its taproom";
    ASSERT_FALSE(p.stories[1].rooms.empty()) << "upper floor has no rooms";
    // M6 (2026-08-08): the upper floor is a GALLERY serving chambers off it, so it
    // legitimately contains a circulation room as well as the guest chambers. The old
    // assertion ("every upstairs room is a bedchamber") encoded the pre-M6 plan, where
    // chambers were chained door-to-door and a guest walked through another guest's
    // room. Contract now: every upstairs room is a chamber OR circulation, and there
    // is at least one of each.
    int chambers = 0, circulation = 0;
    for (const auto& r : p.stories[1].rooms) {
        const AccessClass a = accessClassFor(r.purpose);
        if (r.purpose == "bedchamber") ++chambers;
        else if (a == AccessClass::Circulation) ++circulation;
        else ADD_FAILURE() << "upstairs room '" << r.id << "' is neither a guest chamber "
                              "nor circulation (purpose=" << r.purpose << ")";
    }
    EXPECT_GT(chambers, 0) << "upper floor has no guest chambers";
    EXPECT_GT(circulation, 0) << "upper floor has no landing/gallery to serve the chambers";

    // a connecting stair from story 0 -> 1 exists (the generated circulation).
    bool hasStair = false;
    for (const auto& st : p.stories[0].stairs)
        if (st.fromStory == 0 && st.toStory == 1) hasStair = true;
    EXPECT_TRUE(hasStair) << "no generated stair connecting the two floors";
}

// L3 — THE proof: a character-box walks from the ground taproom UP the generated stair and into an
// upstairs guest chamber. (Goal = chamber 1, a real-floor room; room 0 upstairs is the stair landing
// whose centre is the open shaft.)
TEST(TavernUpstairsTest, CharacterWalksFromTaproomUpIntoGuestChamber) {
    RoomProgramRegistry reg;
    if (!loadCanon(reg)) GTEST_SKIP() << "room_program.json not reachable";
    const RoomProgram* rp = reg.get("tavern");
    ASSERT_NE(rp, nullptr);

    BuildingProgram p = autofilledTavern(rp);
    ASSERT_GE(p.stories[1].rooms.size(), 2u) << "need a landing + >=1 chamber upstairs";
    auto sh = StructureRealizer::realizeShell(p, innStyle());
    ASSERT_TRUE(sh.ok) << sh.error;

    const glm::ivec3 startTaproom = roomCentre(sh, p.stories[0].rooms[0], 0);
    EXPECT_TRUE(walkRooms(sh, p, startTaproom, p.stories[1].rooms[1], 1))
        << "character could NOT walk from the taproom up into a guest chamber — upstairs unreachable";
}

// L3: once upstairs, the guest chambers interconnect (chamber -> chamber through carved doors).
TEST(TavernUpstairsTest, GuestChambersInterconnect) {
    RoomProgramRegistry reg;
    if (!loadCanon(reg)) GTEST_SKIP() << "room_program.json not reachable";
    const RoomProgram* rp = reg.get("tavern");
    ASSERT_NE(rp, nullptr);

    BuildingProgram p = autofilledTavern(rp);
    const auto& up = p.stories[1].rooms;
    if (up.size() < 3u) GTEST_SKIP() << "only one guest chamber — nothing to interconnect";
    auto sh = StructureRealizer::realizeShell(p, innStyle());
    ASSERT_TRUE(sh.ok) << sh.error;

    const glm::ivec3 startChamber = roomCentre(sh, up[1], 1);   // first real chamber (not the landing)
    for (size_t r = 2; r < up.size(); ++r)
        EXPECT_TRUE(walkRooms(sh, p, startChamber, up[r], 1))
            << "guest chamber " << r << " is not reachable from chamber 1";
}

// TEETH: remove the generated stair. The taproom->chamber walk must now FAIL — proving the positive
// depends on the generated stair, not a probe phasing through the floor slab.
TEST(TavernUpstairsTest, WithoutStairUpstairsIsUnreachable) {
    RoomProgramRegistry reg;
    if (!loadCanon(reg)) GTEST_SKIP() << "room_program.json not reachable";
    const RoomProgram* rp = reg.get("tavern");
    ASSERT_NE(rp, nullptr);

    BuildingProgram p = autofilledTavern(rp);
    for (auto& st : p.stories) st.stairs.clear();            // strip the generated circulation
    auto sh = StructureRealizer::realizeShell(p, innStyle());
    ASSERT_TRUE(sh.ok) << sh.error;

    const glm::ivec3 startTaproom = roomCentre(sh, p.stories[0].rooms[0], 0);
    EXPECT_FALSE(walkRooms(sh, p, startTaproom, p.stories[1].rooms[1], 1))
        << "character reached a guest chamber with NO stair — the climb proof has no teeth";
}
