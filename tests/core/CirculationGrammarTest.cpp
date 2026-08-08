#include <gtest/gtest.h>

#include <string>

#include "core/BuildingProgram.h"
#include "core/BuildingProgramValidator.h"
#include "core/RoomLayout.h"
#include "core/RoomProgram.h"

// ============================================================================
// M6 circulation grammar — "everything walkable AND it makes sense".
//
// Connectivity alone is not livability. A plan can be fully connected and still
// unlivable: chambers chained door-to-door, so reaching the far bedroom means
// walking through someone else's. That is exactly the defect the user called
// out ("we dont want ... bedrooms that ... end up needing you to walk through
// another bedroom to reach").
//
// RED before M6: the generated tavern's upper floor is a row of guest chambers
// linked in a LINE (generateUpperChambers -> generateRoomLayoutFromProgram's
// door chain), so chamber_2 is reached only through chamber_1. Nothing detected
// it, because the only structural check was "is every room linked".
// ============================================================================

using namespace Phyxel::Core;

namespace {

bool hasIssue(const ValidationReport& r, const std::string& code) {
    for (const auto& i : r.issues()) if (i.code == code) return true;
    return false;
}

ProgPortal door(const std::string& a, const std::string& b, int px, int pz) {
    ProgPortal p; p.a = a; p.b = b; p.px = px; p.pz = pz;
    p.width = 1; p.height = 2; p.kind = "door";
    return p;
}

const RoomProgram* tavernProgram(RoomProgramRegistry& reg) {
    for (const char* p : {"resources/room_program.json", "../resources/room_program.json",
                          "../../resources/room_program.json",
                          "../../../resources/room_program.json"})
        if (reg.loadFromFile(p)) return reg.get("tavern");
    return nullptr;
}

}  // namespace

// The classification the grammar rests on.
TEST(CirculationGrammar, PurposesClassifyIntoAccessClasses) {
    EXPECT_EQ(accessClassFor("bedchamber"), AccessClass::Private);
    EXPECT_EQ(accessClassFor("chamber"),    AccessClass::Private);
    EXPECT_EQ(accessClassFor("solar"),      AccessClass::Private);   // medieval private upper room
    EXPECT_EQ(accessClassFor("landing"),    AccessClass::Circulation);
    EXPECT_EQ(accessClassFor("gallery"),    AccessClass::Circulation);
    EXPECT_EQ(accessClassFor("screens passage"), AccessClass::Circulation);
    EXPECT_EQ(accessClassFor("kitchen"),    AccessClass::Service);
    EXPECT_EQ(accessClassFor("byre"),       AccessClass::Service);
    EXPECT_EQ(accessClassFor("hall"),       AccessClass::Public);
    EXPECT_EQ(accessClassFor("taproom"),    AccessClass::Public);
    // Unknown purposes must read PUBLIC — the permissive default, so an
    // unclassified room never fabricates a violation.
    EXPECT_EQ(accessClassFor("whatchamacallit_room"), AccessClass::Public);
}

// THE TEETH, hand-built so the defect is unambiguous: three chambers in a line
// off a taproom. chamber_c is reachable ONLY through chamber_b.
TEST(CirculationGrammar, ChainedBedroomsAreAViolation) {
    BuildingProgram p;
    p.name = "chained"; p.style = "timber_cottage";
    p.footprintW = 12; p.footprintD = 6; p.substructure = "slab";
    ProgStory st; st.height = 3;
    ProgRoom tap;  tap.id  = "taproom";   tap.purpose  = "taproom";    tap.rect = {0, 0, 3, 6};
    ProgRoom a;    a.id    = "chamber_a"; a.purpose    = "bedchamber"; a.rect   = {3, 0, 3, 6};
    ProgRoom b;    b.id    = "chamber_b"; b.purpose    = "bedchamber"; b.rect   = {6, 0, 3, 6};
    ProgRoom c;    c.id    = "chamber_c"; c.purpose    = "bedchamber"; c.rect   = {9, 0, 3, 6};
    st.rooms = {tap, a, b, c};
    st.portals.push_back(door("exterior", "taproom", 0, 3));
    st.portals.push_back(door("taproom", "chamber_a", 3, 3));
    st.portals.push_back(door("chamber_a", "chamber_b", 6, 3));   // through a bedroom
    st.portals.push_back(door("chamber_b", "chamber_c", 9, 3));   // and another
    p.stories.push_back(st);

    auto r = BuildingProgramValidator::validate(p, {}, nullptr);
    EXPECT_TRUE(hasIssue(r, "private_room_through_private"))
        << "a chain of bedrooms went undetected — the grammar has no teeth: " << r.summary();
    EXPECT_TRUE(hasIssue(r, "no_circulation"))
        << "3 bedrooms with no landing/passage should warn: " << r.summary();
    // chamber_a is fine (entered from the taproom); b and c are not.
    int violations = 0;
    for (const auto& i : r.issues())
        if (i.code == "private_room_through_private") ++violations;
    EXPECT_EQ(violations, 2) << "expected chamber_b and chamber_c to be flagged, not chamber_a";
}

// The same plan with a LANDING serving each chamber is legal — this is the shape
// the generator must produce (the galleried-inn arrangement).
TEST(CirculationGrammar, ChambersOffALandingAreLegal) {
    BuildingProgram p;
    p.name = "landing"; p.style = "timber_cottage";
    p.footprintW = 12; p.footprintD = 6; p.substructure = "slab";
    ProgStory st; st.height = 3;
    ProgRoom land; land.id = "landing";   land.purpose = "landing";    land.rect = {0, 0, 3, 6};
    ProgRoom a;    a.id    = "chamber_a"; a.purpose    = "bedchamber"; a.rect    = {3, 0, 3, 6};
    ProgRoom b;    b.id    = "chamber_b"; b.purpose    = "bedchamber"; b.rect    = {6, 0, 3, 6};
    ProgRoom c;    c.id    = "chamber_c"; c.purpose    = "bedchamber"; c.rect    = {9, 0, 3, 6};
    st.rooms = {land, a, b, c};
    st.portals.push_back(door("exterior", "landing", 0, 3));
    st.portals.push_back(door("landing", "chamber_a", 3, 1));
    st.portals.push_back(door("landing", "chamber_b", 3, 3));   // every chamber off
    st.portals.push_back(door("landing", "chamber_c", 3, 5));   // the SAME circulation
    p.stories.push_back(st);

    auto r = BuildingProgramValidator::validate(p, {}, nullptr);
    EXPECT_FALSE(hasIssue(r, "private_room_through_private"))
        << "chambers served by a landing were wrongly flagged: " << r.summary();
    EXPECT_FALSE(hasIssue(r, "no_circulation")) << r.summary();
}

// A private room reached through a SERVICE or PUBLIC room is fine — only
// private-through-private is the violation (you may walk through the hall).
TEST(CirculationGrammar, ReachingABedroomThroughAHallIsFine) {
    BuildingProgram p;
    p.name = "throughhall"; p.style = "timber_cottage";
    p.footprintW = 9; p.footprintD = 6; p.substructure = "slab";
    ProgStory st; st.height = 3;
    ProgRoom h; h.id = "hall";    h.purpose = "hall";       h.rect = {0, 0, 3, 6};
    ProgRoom k; k.id = "kitchen"; k.purpose = "kitchen";    k.rect = {3, 0, 3, 6};
    ProgRoom s; s.id = "solar";   s.purpose = "solar";      s.rect = {6, 0, 3, 6};
    st.rooms = {h, k, s};
    st.portals.push_back(door("exterior", "hall", 0, 3));
    st.portals.push_back(door("hall", "kitchen", 3, 3));
    st.portals.push_back(door("kitchen", "solar", 6, 3));   // through SERVICE — allowed
    p.stories.push_back(st);

    auto r = BuildingProgramValidator::validate(p, {}, nullptr);
    EXPECT_FALSE(hasIssue(r, "private_room_through_private"))
        << "walking through a kitchen to a solar is not a bedroom-through-bedroom: "
        << r.summary();
}

// THE REAL GENERATED CASE — the reason this milestone exists. The shipped tavern
// typology grows an upper floor of guest chambers; before M6 they were chained
// in a line. This test measures the ACTUAL generator output.
TEST(CirculationGrammar, GeneratedTavernUpperFloorServesChambersFromCirculation) {
    RoomProgramRegistry reg;
    const RoomProgram* tavern = tavernProgram(reg);
    ASSERT_NE(tavern, nullptr) << "tavern typology not loadable";

    nlohmann::json j;
    j["name"] = "tavern"; j["style"] = "timber_cottage";
    j["footprint"] = nlohmann::json::array({7, 14});
    j["substructure"] = "crawlspace"; j["typology"] = "tavern";
    j["stories"] = nlohmann::json::array({nlohmann::json{{"height", 3}}});
    BuildingProgram p = BuildingProgram::fromJson(j);
    ASSERT_TRUE(autofillRoomLayout(p, 4242u, tavern));
    ASSERT_GE(p.stories.size(), 2u) << "tavern should grow an upper story";

    // Precondition: the upper floor really does hold several private rooms.
    int priv = 0;
    for (const auto& rm : p.stories[1].rooms)
        if (accessClassFor(rm.purpose) == AccessClass::Private) ++priv;
    ASSERT_GT(priv, 1) << "fixture drifted: upper floor no longer has multiple chambers";

    auto r = BuildingProgramValidator::validate(p, {}, tavern);
    EXPECT_FALSE(hasIssue(r, "private_room_through_private"))
        << "the GENERATED tavern still chains its guest chambers — a guest must walk "
           "through another guest's room:\n" << r.summary();
}
