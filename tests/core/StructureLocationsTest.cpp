#include <gtest/gtest.h>

#include <fstream>

#include "core/RoomProgram.h"
#include "core/RoomLayout.h"
#include "core/BuildingProgram.h"
#include "core/StructureRealizer.h"
#include "core/StructureBuildService.h"
#include "core/StyleProfile.h"
#include "core/LocationRegistry.h"

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
