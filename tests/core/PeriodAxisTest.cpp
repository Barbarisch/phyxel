#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>

#include "core/BuildingProgram.h"
#include "core/BuildingProgramValidator.h"
#include "core/ChunkManager.h"
#include "core/RoomProgram.h"
#include "core/RoomLayout.h"
#include "core/StructureBuildService.h"

// ============================================================================
// M8 PERIOD AXIS — era is a first-class parameter of the room-program model.
//
// The user's decision was "period axis, medieval first": ship medieval-accurate
// content now (garderobe and solar, not bathroom and closet), and design the
// schema so later eras arrive as DATA PACKS with no code change. The rule that
// makes it honest is the same one the asset gate enforces: the engine never
// SUBSTITUTES. Asking for a Victorian tavern and quietly receiving a medieval
// one would be exactly the fabrication this pipeline exists to prevent.
// ============================================================================

using namespace Phyxel;
using namespace Phyxel::Core;

namespace {

bool loadCanon(RoomProgramRegistry& reg) {
    for (const char* p : {"resources/room_program.json", "../resources/room_program.json",
                          "../../resources/room_program.json",
                          "../../../resources/room_program.json"})
        if (reg.loadFromFile(p)) return true;
    return false;
}

bool hasIssue(const ValidationReport& r, const std::string& code) {
    for (const auto& i : r.issues()) if (i.code == code) return true;
    return false;
}

}  // namespace

// Every shipped program is medieval, and says so — the default must not be a
// silent blank that later code has to guess about.
TEST(PeriodAxis, ShippedProgramsAreMedievalByDefault) {
    RoomProgramRegistry reg;
    ASSERT_TRUE(loadCanon(reg));
    ASSERT_GT(reg.size(), 0u);
    for (const auto& name : reg.programs()) {
        const RoomProgram* p = reg.get(name);
        ASSERT_NE(p, nullptr);
        EXPECT_EQ(p->period, "medieval") << name << " has no period — the axis has a hole in it";
    }
    const auto periods = reg.periods();
    ASSERT_EQ(periods.size(), 1u);
    EXPECT_EQ(periods[0], "medieval");
}

// Period-aware lookup NEVER crosses eras.
TEST(PeriodAxis, LookupNeverSubstitutesAnotherEra) {
    RoomProgramRegistry reg;
    ASSERT_TRUE(loadCanon(reg));
    EXPECT_NE(reg.get("tavern", "medieval"), nullptr);
    EXPECT_EQ(reg.get("tavern", "victorian"), nullptr)
        << "asking for a victorian tavern returned the MEDIEVAL one — silent substitution";
    EXPECT_NE(reg.get("tavern", ""), nullptr) << "empty period should mean 'any'";
}

// An era arrives as DATA, with no code change: load a pack, and both eras coexist.
TEST(PeriodAxis, AnEraLoadsAsADataPackAndCoexists) {
    RoomProgramRegistry reg;
    ASSERT_TRUE(loadCanon(reg));
    const size_t medievalCount = reg.size();

    // A minimal, deliberately NON-medieval program: a corridor-planned house, the
    // thing a medieval croft never has. (Written inline so the test does not depend
    // on an era pack existing yet — the point is the MECHANISM.)
    const nlohmann::json pack = {
        {"period", "georgian"},
        {"programs", {
            {"corridor_house", {
                {"description", "corridor-planned town house"},
                {"source", "TEST FIXTURE — not a grounded program"},
                {"width_min", 5.0}, {"width_max", 8.0},
                {"bay_length", 3.0}, {"bays", 3},
                {"rooms", nlohmann::json::array({
                    nlohmann::json{{"id", "passage"}, {"purpose", "passage"}, {"bays", 1.0}},
                    nlohmann::json{{"id", "parlour"}, {"purpose", "hall"}, {"bays", 1.0}},
                    nlohmann::json{{"id", "chamber"}, {"purpose", "bedchamber"}, {"bays", 1.0},
                                   {"required", false}}
                })}
            }}
        }}
    };
    // Round-trip through a file, the way the forge loads a pack.
    const std::string tmp = "test_georgian_pack.json";
    { std::ofstream out(tmp); out << pack.dump(2); }
    ASSERT_TRUE(reg.loadPeriodPack(tmp, "georgian"));
    std::remove(tmp.c_str());

    EXPECT_GT(reg.size(), medievalCount) << "the pack did not merge into the registry";
    const RoomProgram* g = reg.get("georgian:corridor_house");
    ASSERT_NE(g, nullptr) << "era-qualified key not found";
    EXPECT_EQ(g->period, "georgian");
    // Both eras now coexist, and the medieval content is untouched.
    EXPECT_NE(reg.get("tavern", "medieval"), nullptr);
    const auto periods = reg.periods();
    EXPECT_EQ(periods.size(), 2u);
    // The optional room parsed as optional; the others defaulted to required.
    int optional = 0, required = 0;
    for (const auto& rs : g->rooms) (rs.required ? required : optional)++;
    EXPECT_EQ(optional, 1);
    EXPECT_EQ(required, 2) << "rooms without an explicit flag must default to REQUIRED";
}

// REQUIRED ROOMS: a tavern without a taproom is not a tavern.
TEST(PeriodAxis, AMissingRequiredRoomIsAnError) {
    RoomProgramRegistry reg;
    ASSERT_TRUE(loadCanon(reg));
    const RoomProgram* tavern = reg.get("tavern");
    ASSERT_NE(tavern, nullptr);

    // A plan with rooms, but none of them the taproom the typology requires.
    BuildingProgram p;
    p.name = "not_really_a_tavern"; p.style = "timber_cottage";
    p.footprintW = 6; p.footprintD = 12; p.substructure = "slab";
    ProgStory st; st.height = 3;
    ProgRoom a; a.id = "store"; a.purpose = "service"; a.rect = {0, 0, 6, 6};
    ProgRoom b; b.id = "store2"; b.purpose = "service"; b.rect = {0, 6, 6, 6};
    st.rooms = {a, b};
    ProgPortal d; d.a = "exterior"; d.b = "store"; d.px = 0; d.pz = 3;
    d.width = 1; d.height = 2; d.kind = "door";
    ProgPortal i; i.a = "store"; i.b = "store2"; i.px = 3; i.pz = 6;
    i.width = 1; i.height = 2; i.kind = "door";
    st.portals = {d, i};
    p.stories.push_back(st);

    auto r = BuildingProgramValidator::validate(p, {}, tavern);
    EXPECT_TRUE(hasIssue(r, "required_room_missing"))
        << "a 'tavern' with no taproom passed the typology gate: " << r.summary();
}

// ...and the real generated tavern satisfies its own required rooms.
TEST(PeriodAxis, TheGeneratedTavernSatisfiesItsRequiredRooms) {
    RoomProgramRegistry reg;
    ASSERT_TRUE(loadCanon(reg));
    const RoomProgram* tavern = reg.get("tavern");
    ASSERT_NE(tavern, nullptr);

    nlohmann::json j;
    j["name"] = "tavern"; j["style"] = "timber_cottage";
    j["footprint"] = nlohmann::json::array({7, 14});
    j["substructure"] = "crawlspace"; j["typology"] = "tavern";
    j["stories"] = nlohmann::json::array({nlohmann::json{{"height", 3}}});
    BuildingProgram p = BuildingProgram::fromJson(j);
    ASSERT_TRUE(autofillRoomLayout(p, 99u, tavern));

    auto r = BuildingProgramValidator::validate(p, {}, tavern);
    EXPECT_FALSE(hasIssue(r, "required_room_missing"))
        << "the GENERATED tavern is missing a room its own typology requires: " << r.summary();
}

// The build path REFUSES an unknown period instead of quietly building medieval.
TEST(PeriodAxis, AnUnknownPeriodRefusesRatherThanSubstituting) {
    ChunkManager cm;
    cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
    StructureBuildService::Deps deps;
    deps.chunkManager = &cm;

    nlohmann::json params = {
        {"schema", "v2"}, {"type", "tavern"}, {"typology", "tavern"},
        {"style", "timber_cottage"}, {"period", "victorian"},
        {"position", {{"x", 500}, {"y", 16}, {"z", 500}}},
        {"footprint", nlohmann::json::array({6, 12})},
        {"substructure", "crawlspace"},
        {"stories", nlohmann::json::array({nlohmann::json{{"height", 3}}})}};

    auto res = StructureBuildService::buildV2(params, deps);
    ASSERT_TRUE(res.contains("error")) << res.dump();
    EXPECT_EQ(res.value("refused_at", std::string()), "intake")
        << "an unknown period did not refuse at intake: " << res.dump();
    EXPECT_EQ(res.value("requested_period", std::string()), "victorian");
    ASSERT_TRUE(res.contains("known_periods")) << "the refusal must say what IS available";
}
