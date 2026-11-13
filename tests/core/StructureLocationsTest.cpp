#include <gtest/gtest.h>

#include <fstream>

#include "core/RoomProgram.h"
#include "core/RoomLayout.h"
#include "core/BuildingProgram.h"
#include "core/StructureRealizer.h"
#include "core/StructureBuildService.h"
#include "core/StyleProfile.h"
#include "core/LocationRegistry.h"
#include "core/ChunkManager.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

#include <set>

using namespace Phyxel::Core;

// ============================================================================
// STRUCTURE LOCATIONS (playable-town increment 2) — every v2 building derives a
// schedule-target LocationMarker so settlements register Home/Work/Tavern
// locations that NPC schedules (locationId-driven) can resolve.
// RED (measured live 2026-07-21, pre-change): a full seed-3 village build left
// /api/locations EMPTY (Count:0) — schedules had nothing to target.
// These tests pin the derivation contract: typed from typology, anchored at an
// OUTDOOR cell by the ground-story exterior door (the 2.5D NavGrid cannot see
// interiors — building columns read as roof), centre fallback when doorless.
// ============================================================================

namespace {
bool loadShippedCanon(RoomProgramRegistry& reg) {
    for (const char* p : {"resources/room_program.json", "../resources/room_program.json",
                          "../../resources/room_program.json", "../../../resources/room_program.json"}) {
        std::ifstream f(p);
        if (f.good()) return reg.loadFromFile(p);
    }
    return false;
}

StyleProfile testStyle() {
    StyleProfileRegistry reg;
    reg.loadFromJson(nlohmann::json::parse(R"({
        "timber_cottage": { "roof_style":"gable", "foundation":"crawlspace",
            "thickness": { "exterior_wall":0.333, "interior_wall":0.222, "foundation_wall":0.667,
                           "floor":0.333, "ceiling":0.222 },
            "materials": { "structure":"Wood", "floor":"Wood", "roof":"Wood", "foundation":"Stone" },
            "roof": { "pitch":0.8 } } })"));
    return *reg.get("timber_cottage");
}

// Autofilled typology program on a footprint that fits it (mirrors TavernTypologyTest).
BuildingProgram typologyProgram(const std::string& typ, int w, int d, const RoomProgram* rp) {
    BuildingProgram p;
    p.name = typ; p.style = "timber_cottage"; p.footprintW = w; p.footprintD = d;
    p.substructure = "slab"; p.typology = typ;
    ProgStory s; s.height = 3; p.stories.push_back(s);
    autofillRoomLayout(p, 7u, rp);
    return p;
}
} // namespace

// The tavern's marker: exactly one, typed Tavern, id carries the typology, and the
// anchor sits OUTSIDE the footprint (an outdoor, street-reachable cell) within reach
// of a perimeter door opening.
TEST(StructureLocationsTest, TavernEmitsTavernMarkerOutsideDoor) {
    RoomProgramRegistry reg;
    if (!loadShippedCanon(reg)) GTEST_SKIP() << "room_program.json not reachable from CWD";
    const RoomProgram* rp = reg.get("tavern");
    ASSERT_NE(rp, nullptr);

    BuildingProgram p = typologyProgram("tavern", 16, 7, rp);
    auto shell = StructureRealizer::realizeShell(p, testStyle());
    ASSERT_TRUE(shell.ok) << shell.error;

    const glm::ivec3 origin(100, 20, 50);
    auto locs = StructureRealizer::deriveLocations(p, "tavern", shell.plan, origin,
                                                   shell.floorTopMicro);
    ASSERT_EQ(locs.size(), 1u);
    const auto& m = locs[0];
    EXPECT_EQ(m.type, LocationType::Tavern);
    EXPECT_EQ(m.id.rfind("tavern_", 0), 0u) << "id was: " << m.id;

    // Local-frame anchor must be OUTSIDE the footprint (outdoor cell)...
    const glm::ivec3 local = glm::ivec3(m.position) - origin;
    const bool outside = local.x < 0 || local.x >= p.footprintW ||
                         local.z < 0 || local.z >= p.footprintD;
    EXPECT_TRUE(outside) << "anchor inside footprint at local (" << local.x << ","
                         << local.z << ")";

    // ...and adjacent to a real perimeter door opening in the plan (within 3 cubes).
    bool nearDoor = false;
    for (const auto& o : shell.plan.openings) {
        if (o.kind != "door") continue;
        if (o.x != 0 && o.x != p.footprintW && o.z != 0 && o.z != p.footprintD) continue;
        const int dx = std::abs(local.x - o.x), dz = std::abs(local.z - o.z);
        if (dx <= 3 && dz <= 3 + o.w) nearDoor = true;
    }
    EXPECT_TRUE(nearDoor) << "anchor not adjacent to any perimeter door";
}

// Typology -> LocationType mapping: dwellings Home, trades Work, tavern Tavern,
// unknown Custom. Driven through the real API (same plan, varied typology).
TEST(StructureLocationsTest, TypologyMapsToLocationType) {
    RoomProgramRegistry reg;
    if (!loadShippedCanon(reg)) GTEST_SKIP() << "room_program.json not reachable from CWD";
    BuildingProgram p = typologyProgram("hall_house", 16, 8, reg.get("hall_house"));
    auto shell = StructureRealizer::realizeShell(p, testStyle());
    ASSERT_TRUE(shell.ok) << shell.error;

    auto typeOf = [&](const std::string& typ) {
        return StructureRealizer::deriveLocations(p, typ, shell.plan, glm::ivec3(0, 16, 0),
                                                  shell.floorTopMicro)[0].type;
    };
    EXPECT_EQ(typeOf("hall_house"), LocationType::Home);
    EXPECT_EQ(typeOf("croft"), LocationType::Home);
    EXPECT_EQ(typeOf("longhouse"), LocationType::Home);
    EXPECT_EQ(typeOf("blacksmith"), LocationType::Work);
    EXPECT_EQ(typeOf("bakery"), LocationType::Work);
    EXPECT_EQ(typeOf("general_store"), LocationType::Work);
    EXPECT_EQ(typeOf("tavern"), LocationType::Tavern);
    EXPECT_EQ(typeOf("gazebo_of_mystery"), LocationType::Custom);
}

// Dwellings have OPPOSED cross-passage doors on both long elevations; the marker must
// anchor at the STREET-facing one (program.front), not whichever door enumerates first.
// RED (measured live pre-fix): 6/14 village anchors unreachable — back-door anchors
// faced away from the street (z=9, z=-1 rows).
TEST(StructureLocationsTest, PrefersFrontWallDoor) {
    RoomProgramRegistry reg;
    if (!loadShippedCanon(reg)) GTEST_SKIP() << "room_program.json not reachable from CWD";
    const RoomProgram* rp = reg.get("hall_house");
    ASSERT_NE(rp, nullptr);

    auto anchorFor = [&](const std::string& front) {
        BuildingProgram p = typologyProgram("hall_house", 16, 8, rp);
        p.front = front;
        auto shell = StructureRealizer::realizeShell(p, testStyle());
        EXPECT_TRUE(shell.ok) << shell.error;
        auto locs = StructureRealizer::deriveLocations(p, "hall_house", shell.plan,
                                                       glm::ivec3(0, 16, 0), shell.floorTopMicro);
        EXPECT_EQ(locs.size(), 1u);
        return glm::ivec3(locs[0].position);
    };
    // 16x8 long axis runs X -> the opposed cross-passage doors sit on the z=0 / z=D
    // walls. The anchor must flip sides with `front`.
    const glm::ivec3 a0 = anchorFor("z0"), a1 = anchorFor("z1");
    EXPECT_LT(a0.z, 0)  << "front=z0 anchor not on the z<0 side";
    EXPECT_GE(a1.z, 8)  << "front=z1 anchor not on the z>=D side";
}

// A doorless shell (no rooms -> no portals -> no openings) still gets a marker:
// centre fallback at floor level, INSIDE the footprint.
TEST(StructureLocationsTest, DoorlessShellFallsBackToCentre) {
    BuildingProgram p;
    p.name = "shed"; p.style = "timber_cottage";
    p.footprintW = 6; p.footprintD = 5; p.substructure = "slab";
    ProgStory s; s.height = 3;
    ProgRoom r; r.id = "r0"; r.rect = {0, 0, 6, 5}; r.purpose = "generic";
    s.rooms.push_back(r);                                // one room, NO portals -> no doors
    p.stories.push_back(s);
    auto shell = StructureRealizer::realizeShell(p, testStyle());
    ASSERT_TRUE(shell.ok) << shell.error;

    bool anyDoor = false;
    for (const auto& o : shell.plan.openings) anyDoor |= (o.kind == "door");
    ASSERT_FALSE(anyDoor) << "premise broken: doorless shell has a door";

    const glm::ivec3 origin(-40, 16, 200);
    auto locs = StructureRealizer::deriveLocations(p, "", shell.plan, origin,
                                                   shell.floorTopMicro);
    ASSERT_EQ(locs.size(), 1u);
    const glm::ivec3 local = glm::ivec3(locs[0].position) - origin;
    EXPECT_GE(local.x, 0); EXPECT_LT(local.x, p.footprintW);
    EXPECT_GE(local.z, 0); EXPECT_LT(local.z, p.footprintD);
    EXPECT_EQ(locs[0].type, LocationType::Custom);
}

// Anchor snapping (measured live: the tavern anchor landed in a dead column under
// the eave — no standable surface — and 12 residents got no_route). snapToStandable
// must move a dead anchor to the nearest standable cell and leave good anchors alone.
TEST(StructureLocationsTest, SnapToStandableFixesDeadAnchor) {
    // Synthetic world: flat floor at y=16 (stand at y=17) EXCEPT column (10,20) which
    // is capped solid at y17-19 (an "eave" — no gap to stand in). Neighbour (9,20) open.
    auto solidAt = [](const glm::ivec3& p) {
        if (p.y <= 16) return true;                              // ground
        if (p.x == 10 && p.z == 20 && p.y <= 19) return true;    // dead column cap
        return false;
    };

    // Dead anchor at (10,17,20): no standable slot in its column -> snaps to a ring-1
    // neighbour at the same standing level.
    const glm::ivec3 snapped = Phyxel::Core::StructureBuildService::snapToStandable(
        solidAt, glm::ivec3(10, 17, 20));
    EXPECT_TRUE(std::abs(snapped.x - 10) <= 1 && std::abs(snapped.z - 20) <= 1);
    EXPECT_FALSE(snapped.x == 10 && snapped.z == 20) << "stayed in the dead column";
    EXPECT_EQ(snapped.y, 17);

    // A good anchor is untouched.
    const glm::ivec3 good = Phyxel::Core::StructureBuildService::snapToStandable(
        solidAt, glm::ivec3(30, 17, 30));
    EXPECT_EQ(good, glm::ivec3(30, 17, 30));

    // Nothing standable anywhere in range -> honest no-op (unchanged).
    auto allSolid = [](const glm::ivec3&) { return true; };
    EXPECT_EQ(Phyxel::Core::StructureBuildService::snapToStandable(allSolid,
                                                                   glm::ivec3(0, 17, 0)),
              glm::ivec3(0, 17, 0));

    // The avoid predicate (e.g. "inside the building footprint") excludes otherwise
    // standable candidates — the snap must route around it.
    auto avoidHighX = [](const glm::ivec3& p) { return p.x >= 10; };
    const glm::ivec3 s2 = Phyxel::Core::StructureBuildService::snapToStandable(
        solidAt, glm::ivec3(10, 17, 20), 5, avoidHighX);
    EXPECT_LT(s2.x, 10) << "snapped into the avoided region";
    EXPECT_EQ(s2.y, 17);
}

// M5b-1 (chain 93, the street tiers' first live gate): a way graded one cube ABOVE the pad put the marker's
// column one cube higher than the anchor; the snap's same-level-first search walked FIVE cubes sideways to the
// first same-level cell - round the corner of the house - and the gate, the spurs and the doorsteps all inferred
// the door's wall from that side (two village crofts: the route's goal became the inside of a windowless east
// wall where a bench stood; blocked). The ground a cube up or down in the SAME column is the open ground in
// front of the door and must win over any lateral step; the eave case (dy = +3, the test above) stays lateral.
TEST(StructureLocationsTest, SnapToStandableKeepsTheMarkerInItsColumnAcrossAOneCubeStep) {
    // flat ground at y = 16 (stand at 17); a one-cube-higher apron (y = 17 solid, stand at 18) over the 9 x 9
    // around (10, 20) - the graded way in front of the door; same-level open ground first at ring 5
    auto solidAt = [](const glm::ivec3& p) {
        if (p.y <= 16) return true;
        if (p.y == 17 && std::abs(p.x - 10) <= 4 && std::abs(p.z - 20) <= 4) return true;
        return false;
    };
    const glm::ivec3 snapped = Phyxel::Core::StructureBuildService::snapToStandable(solidAt, glm::ivec3(10, 17, 20));
    EXPECT_EQ(snapped, glm::ivec3(10, 18, 20))
        << "the marker left its column for (" << snapped.x << ", " << snapped.y << ", " << snapped.z
        << ") instead of standing on the apron one cube up";
    // one cube DOWN likewise (the way cut below the pad)
    auto cutAt = [](const glm::ivec3& p) {
        if (std::abs(p.x - 10) <= 4 && std::abs(p.z - 20) <= 4) return p.y <= 15;
        return p.y <= 16;
    };
    const glm::ivec3 down = Phyxel::Core::StructureBuildService::snapToStandable(cutAt, glm::ivec3(10, 17, 20));
    EXPECT_EQ(down, glm::ivec3(10, 16, 20)) << "the marker left its column instead of stepping down one cube";
}

// M5b-1 chain 98: the way FILLED two cubes above the pad in front of a croft's door; the marker's own column was
// standable only two cubes up, beyond the in-column step, and the search turned the corner to a same-level cell
// five cubes east - in the neighbour's toft. A front-door marker's meaning points along its door's outward normal:
// a displaced anchor must move OUT along that normal (up or down a cube or two) before any lateral step.
TEST(StructureLocationsTest, SnapToStandableFollowsTheDoorNormalBeforeTurningACorner) {
    // ground at y = 16 (stand 17) everywhere; the way's fill: y <= 18 solid (stand 19) over z >= 20 for |x - 10| <= 4;
    // the anchor (10, 17, 20) sits inside the fill; the door is at z < 20, its outward normal +z
    auto solidAt = [](const glm::ivec3& p) {
        if (p.y <= 16) return true;
        if (p.z >= 20 && std::abs(p.x - 10) <= 4 && p.y <= 18) return true;
        return false;
    };
    const glm::ivec3 snapped = Phyxel::Core::StructureBuildService::snapToStandable(solidAt, glm::ivec3(10, 17, 20), 5, {}, glm::ivec2(0, 1));
    EXPECT_EQ(snapped.x, 10) << "the marker turned a corner to (" << snapped.x << ", " << snapped.y << ", " << snapped.z << ")";
    EXPECT_GE(snapped.z, 20) << "the marker moved back toward / into the building";
    EXPECT_EQ(snapped.y, 19) << "the marker should stand on the fill in front of the door";
}

// M5b-1: the marker RECORDS the door it was anchored at - the wall cube and the outward normal - so the
// settlement's door-finding is the forge's door, not an inference from where the (snapped) marker sits.
TEST(StructureLocationsTest, DerivedLocationRecordsTheDoorItAnchored) {
    RoomProgramRegistry reg;
    ASSERT_TRUE(reg.loadFromFile("resources/room_program.json"));
    const RoomProgram* croft = reg.get("croft");
    ASSERT_NE(croft, nullptr);
    for (const char* front : {"z1", "z0"}) {
        BuildingProgram p;
        p.name = "croft"; p.typology = "croft"; p.footprintW = 8; p.footprintD = 6; p.substructure = "slab";
        p.front = front;
        ProgStory st; st.height = 3; p.stories.push_back(st);
        ASSERT_TRUE(autofillRoomLayout(p, 7u, croft));
        StyleProfile style;
        auto shell = StructureRealizer::realizeShell(p, style);
        ASSERT_TRUE(shell.ok) << shell.error;
        const glm::ivec3 origin(100, 16, 200);
        auto locs = StructureRealizer::deriveLocations(p, "croft", shell.plan, origin, shell.floorTopMicro);
        ASSERT_EQ(locs.size(), 1u);
        EXPECT_TRUE(locs[0].hasDoor) << front << ": the croft's marker records no door";
        const int nz = (std::string(front) == "z1") ? +1 : -1;
        EXPECT_EQ(locs[0].doorNormal, glm::ivec2(0, nz)) << front;
        EXPECT_EQ(locs[0].doorWall.y, nz > 0 ? origin.z + 5 : origin.z) << front << ": the door's wall cube is not on the front wall";
        EXPECT_GE(locs[0].doorWall.x, origin.x); EXPECT_LT(locs[0].doorWall.x, origin.x + 8);
        // the marker sits two cells beyond the door's wall cube along the normal
        const glm::ivec3 m(static_cast<int>(std::floor(locs[0].position.x)), 0, static_cast<int>(std::floor(locs[0].position.z)));
        EXPECT_EQ(m.x, locs[0].doorWall.x) << front;
        EXPECT_EQ(m.z, locs[0].doorWall.y + 2 * nz) << front;
    }
}

// Determinism + origin linearity: same inputs -> identical marker; shifting the
// origin shifts the marker by exactly the shift (ids differ since they encode
// world coords — two crofts must not collide in the registry).
TEST(StructureLocationsTest, DeterministicAndOriginRelative) {
    RoomProgramRegistry reg;
    if (!loadShippedCanon(reg)) GTEST_SKIP() << "room_program.json not reachable from CWD";
    BuildingProgram p = typologyProgram("croft", 8, 6, reg.get("croft"));
    auto shell = StructureRealizer::realizeShell(p, testStyle());
    ASSERT_TRUE(shell.ok) << shell.error;

    const glm::ivec3 o1(0, 16, 0), o2(64, 16, -32);
    auto a  = StructureRealizer::deriveLocations(p, "croft", shell.plan, o1, shell.floorTopMicro);
    auto a2 = StructureRealizer::deriveLocations(p, "croft", shell.plan, o1, shell.floorTopMicro);
    auto b  = StructureRealizer::deriveLocations(p, "croft", shell.plan, o2, shell.floorTopMicro);
    ASSERT_EQ(a.size(), 1u); ASSERT_EQ(b.size(), 1u);
    EXPECT_EQ(a[0].id, a2[0].id);
    EXPECT_EQ(a[0].position.x, a2[0].position.x);
    EXPECT_EQ(glm::ivec3(b[0].position) - glm::ivec3(a[0].position), o2 - o1);
    EXPECT_NE(a[0].id, b[0].id);
}

// ── M5a-1 (audits 16-17): the locations a BUILD reports, measured through buildV2 ──────────────────
// The settlement gate reads every building's front door from the build response's "locations"; chain 74 of the
// M5a-1 leg measured a headless build (no LocationRegistry) reporting none, so the gate passed on 0 routes. The
// response now reports the list with or without a registry and says how many a registry took.
namespace {
// A flat pad: 2 x 2 x 2 resident chunks, the lower row solid to y 15 (a building at y 16 and its roof stay inside
// resident chunks - the M0 placement rule refuses a target chunk that is not resident).
struct PadWorld {
    Phyxel::ChunkManager cm;
    PadWorld() {
        cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
        for (int cx = 0; cx < 2; ++cx)
            for (int cy = 0; cy < 2; ++cy)
                for (int cz = 0; cz < 2; ++cz) {
                    auto owned = std::make_unique<Phyxel::Chunk>(glm::ivec3(cx, cy, cz) * 32);
                    owned->initializeForLoading();
                    if (cy == 0)
                        for (int x = 0; x < 32; ++x)
                            for (int z = 0; z < 32; ++z)
                                for (int y = 0; y < 16; ++y) owned->addCube(glm::ivec3(x, y, z));
                    cm.chunkMap[glm::ivec3(cx, cy, cz)] = owned.get();
                    cm.chunks.push_back(std::move(owned));
                }
    }
};
// chain 85: every typology refused "no_stories" - a direct v2 build names its stories (the settlement realizer
// derives them from the program); one story of 3 cubes per program story, as StructureGroundingTest's croft
nlohmann::json padParams(const std::string& typology, int w, int d, int stories = 1) {
    nlohmann::json st = nlohmann::json::array();
    for (int i = 0; i < std::max(1, stories); ++i) st.push_back(nlohmann::json{{"height", 3}});
    return {{"schema", "v2"}, {"type", "house"}, {"typology", typology},
            {"position", {{"x", 2}, {"y", 16}, {"z", 2}}},
            {"footprint", nlohmann::json::array({w, d})},
            {"substructure", "slab"}, {"stories", st}};
}
}  // namespace

// Without a registry the list is REPORTED (the gate's input) and none is registered; with one, the same list and
// one registered. The red form of the first half is chain 74 (m5a_red_green.txt): the list was empty without a
// registry and the gate measured nothing; the count field is new (red by construction).
TEST(StructureLocationsTest, BuildReportsItsLocationsWithOrWithoutARegistry) {
    {
        PadWorld w;
        StructureBuildService::Deps deps;
        deps.chunkManager = &w.cm;
        const auto resp = StructureBuildService::buildV2(padParams("croft", 8, 6), deps);
        ASSERT_TRUE(resp.value("success", false)) << resp.dump();
        ASSERT_TRUE(resp.contains("locations")) << resp.dump();
        EXPECT_EQ(resp["locations"].size(), 1u) << "the front door is reported with no registry: " << resp["locations"].dump();
        EXPECT_EQ(resp.value("locations_registered", -1), 0) << "nothing can be registered without a registry";
    }
    {
        PadWorld w;
        LocationRegistry reg;
        StructureBuildService::Deps deps;
        deps.chunkManager = &w.cm;
        deps.locations = &reg;
        const auto resp = StructureBuildService::buildV2(padParams("croft", 8, 6), deps);
        ASSERT_TRUE(resp.value("success", false)) << resp.dump();
        EXPECT_EQ(resp["locations"].size(), 1u);
        EXPECT_EQ(resp.value("locations_registered", -1), 1);
        EXPECT_EQ(reg.getAllLocations().size(), 1u) << "the registry took the one location";
    }
}

// Every shipped typology, built headless at its natural footprint (bays x bay length along, the program's width
// across - the planner's and the realizer's derivation), reports exactly ONE location: the gate's "doorless"
// verdict (doors < built) can therefore not fire on a typology - measured, not inferred from deriveLocations.
TEST(StructureLocationsTest, EveryShippedTypologyReportsOneLocation) {
    RoomProgramRegistry reg;
    ASSERT_TRUE(loadShippedCanon(reg));
    int built = 0;
    for (const auto& name : reg.programs()) {
        const RoomProgram* rp = reg.get(name);
        ASSERT_NE(rp, nullptr) << name;
        // M5b-1: the pad builds every typology at ITS natural footprint through the one derivation the planner
        // and the realizer use (naturalFootprintOf keeps the forge's proportion - chain 86's manor_hall finding,
        // closed by RoomProgramTest.EveryTypologysNaturalFootprintPassesTheForge); no rig footprint any more
        const NaturalFootprint nf = naturalFootprintOf(*rp);
        const int natLong = nf.longU, natShort = nf.shortU;
        PadWorld w;
        StructureBuildService::Deps deps;
        deps.chunkManager = &w.cm;
        const auto resp = StructureBuildService::buildV2(padParams(name, natLong, natShort, rp->stories), deps);
        if (!resp.value("success", false)) {
            ADD_FAILURE() << name << " (" << natLong << " x " << natShort << ") did not build on the flat pad: "
                          << resp.value("error", std::string("?")) << " - a finding about the pad or the typology, not the gate";
            continue;
        }
        ++built;
        const auto locs = resp.value("locations", nlohmann::json::array());
        EXPECT_EQ(locs.size(), 1u) << name << " reported " << locs.size() << " locations: " << locs.dump();
        EXPECT_GE(resp.value("floor_top_micro", -1), 0) << name << " reported no floor top";
        std::printf("[ locations ] %-14s %2d x %2d: %zu location(s) (%s), floor top micro %d\n", name.c_str(), natLong, natShort,
                    locs.size(), locs.empty() ? "-" : locs[0].value("type", std::string("?")).c_str(), resp.value("floor_top_micro", -1));
    }
    EXPECT_EQ(built, static_cast<int>(reg.programs().size())) << "every shipped typology must build on the pad for this pin to cover it";
}
