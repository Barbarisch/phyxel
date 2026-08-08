#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/BuildingProgram.h"
#include "core/RealizedStructureValidator.h"
#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"

// ============================================================================
// M7 doorway clearance (checklist G5/K6) — the REALIZED check.
//
// RED before M7: furniture avoided doorways at PLAN time only — a 2x2 block of
// CUBE cells around each portal point. But a piece renders at MICRO precision
// and spills past its reserved footprint (the wall-inset micro-spill that
// placedCubeSpan exists to describe), and nothing ever re-checked the placed
// result against the carved opening. A piece that "fits" beside a door could
// still stand in it, and the build shipped a room you could not walk into.
//
// The passage geometry is taken from the opening's recorded `clear` reveal
// boxes — what the realizer actually carved — so the check needs no assumption
// about which way the wall faces.
// ============================================================================

using namespace Phyxel::Core;

namespace {

StyleProfile doorStyle() {
    StyleProfileRegistry reg;
    reg.loadFromJson(nlohmann::json::parse(R"({
        "timber_cottage": {
            "roof_style": "gable", "foundation": "slab",
            "thickness": { "exterior_wall": 0.333, "interior_wall": 0.222,
                           "foundation_wall": 0.667, "floor": 0.333, "ceiling": 0.222 },
            "materials": { "structure": "Wood", "floor": "Wood", "roof": "Wood", "foundation": "Stone" },
            "roof": { "pitch": 0.8 }
        }
    })"));
    return *reg.get("timber_cottage");
}

// Two rooms with an interior door between them + an exterior entrance.
BuildingProgram twoRoomWithDoor() {
    BuildingProgram p;
    p.name = "doors"; p.style = "timber_cottage";
    p.footprintW = 9; p.footprintD = 7; p.substructure = "slab";
    ProgStory st; st.height = 3;
    ProgRoom a; a.id = "hall";    a.purpose = "hall";    a.rect = {0, 0, 4, 7};
    ProgRoom b; b.id = "kitchen"; b.purpose = "kitchen"; b.rect = {4, 0, 5, 7};
    st.rooms = {a, b};
    auto door = [](const std::string& x, const std::string& y, int px, int pz) {
        ProgPortal d; d.a = x; d.b = y; d.px = px; d.pz = pz;
        d.width = 1; d.height = 2; d.kind = "door"; return d;
    };
    st.portals.push_back(door("exterior", "hall", 0, 3));
    st.portals.push_back(door("hall", "kitchen", 4, 3));   // interior door at x=4, z=3
    p.stories.push_back(st);
    return p;
}

// A fixture box spanning cubes [x0,x1] x [z0,z1] at floor height, in world MICRO.
RealizedStructureValidator::PlacedBox box(const std::string& type, const std::string& room,
                                          int x0, int z0, int x1, int z1, int floorCubeY) {
    RealizedStructureValidator::PlacedBox b;
    b.type = type; b.room = room; b.objectId = type + "_1";
    b.lo = glm::ivec3(x0 * 9, floorCubeY * 9, z0 * 9);
    b.hi = glm::ivec3((x1 + 1) * 9, (floorCubeY + 2) * 9, (z1 + 1) * 9);
    return b;
}

}  // namespace

TEST(DoorwayClearance, EmptyRoomsPass) {
    auto p = twoRoomWithDoor();
    auto sh = StructureRealizer::realizeShell(p, doorStyle());
    ASSERT_TRUE(sh.ok) << sh.error;
    auto rep = RealizedStructureValidator::checkDoorwayClearance(sh.plan, {0, 0, 0}, {});
    EXPECT_TRUE(rep.ok()) << rep.summary();
}

// THE TEETH: a chest standing in the interior doorway must be caught.
TEST(DoorwayClearance, AFixtureStandingInTheDoorwayIsCaught) {
    auto p = twoRoomWithDoor();
    auto sh = StructureRealizer::realizeShell(p, doorStyle());
    ASSERT_TRUE(sh.ok) << sh.error;
    ASSERT_FALSE(sh.plan.openings.empty()) << "no openings recorded — nothing to test against";
    const int floorY = sh.floorTopByStory[0] / 9;

    // The interior door is at cube x=4, z=3. A chest occupying that cell blocks it.
    auto rep = RealizedStructureValidator::checkDoorwayClearance(
        sh.plan, {0, 0, 0}, {box("chest", "kitchen", 4, 3, 4, 3, floorY)});
    EXPECT_FALSE(rep.ok())
        << "a chest standing squarely in the doorway went undetected — the check has no teeth";
}

// A piece parked directly IN FRONT of the door blocks it just as effectively —
// the approach is part of the passage.
TEST(DoorwayClearance, AFixtureBlockingTheApproachIsCaught) {
    auto p = twoRoomWithDoor();
    auto sh = StructureRealizer::realizeShell(p, doorStyle());
    ASSERT_TRUE(sh.ok) << sh.error;
    const int floorY = sh.floorTopByStory[0] / 9;
    auto rep = RealizedStructureValidator::checkDoorwayClearance(
        sh.plan, {0, 0, 0}, {box("table", "kitchen", 5, 3, 5, 3, floorY)});
    EXPECT_FALSE(rep.ok()) << "a piece parked in front of the door was not flagged";
}

// NO FALSE POSITIVES: furniture against the same wall but clear of the opening
// must pass, or the repair would strip legitimately-placed pieces.
TEST(DoorwayClearance, FurnitureClearOfTheOpeningPasses) {
    auto p = twoRoomWithDoor();
    auto sh = StructureRealizer::realizeShell(p, doorStyle());
    ASSERT_TRUE(sh.ok) << sh.error;
    const int floorY = sh.floorTopByStory[0] / 9;
    // Same wall (x=5) but at z=0 and z=6 — well clear of the z=3 doorway.
    auto rep = RealizedStructureValidator::checkDoorwayClearance(
        sh.plan, {0, 0, 0},
        {box("bed", "kitchen", 5, 0, 6, 1, floorY),
         box("chest", "kitchen", 5, 6, 5, 6, floorY)});
    EXPECT_TRUE(rep.ok())
        << "furniture clear of the doorway was wrongly flagged — the repair would evict it: "
        << rep.summary();
}

// The world ORIGIN must be honoured: the same fixture, same relative position,
// in a building placed far from the origin, must still be caught.
TEST(DoorwayClearance, WorksAwayFromTheWorldOrigin) {
    auto p = twoRoomWithDoor();
    auto sh = StructureRealizer::realizeShell(p, doorStyle());
    ASSERT_TRUE(sh.ok) << sh.error;
    const glm::ivec3 origin(100, 16, 200);
    const int floorY = origin.y + sh.floorTopByStory[0] / 9;
    auto rep = RealizedStructureValidator::checkDoorwayClearance(
        sh.plan, origin,
        {box("chest", "kitchen", origin.x + 4, origin.z + 3,
             origin.x + 4, origin.z + 3, floorY)});
    EXPECT_FALSE(rep.ok())
        << "the doorway check ignores the structure origin — it would never fire on a "
           "real placed building";
}

// A WINDOW is not a passage: a chest under a window is furniture, not a defect.
TEST(DoorwayClearance, WindowsAreNotPassages) {
    BuildingProgram p;
    p.name = "win"; p.style = "timber_cottage";
    p.footprintW = 8; p.footprintD = 7; p.substructure = "slab";
    ProgStory st; st.height = 3;
    ProgRoom r; r.id = "hall"; r.purpose = "hall"; r.rect = {0, 0, 8, 7};
    st.rooms.push_back(r);
    ProgPortal d; d.a = "exterior"; d.b = "hall"; d.px = 0; d.pz = 1;
    d.width = 1; d.height = 2; d.kind = "door";
    ProgPortal w; w.a = "exterior"; w.b = "hall"; w.px = 0; w.pz = 5;
    w.width = 2; w.height = 1; w.kind = "window"; w.infill = "shuttered";
    st.portals = {d, w};
    p.stories.push_back(st);

    auto sh = StructureRealizer::realizeShell(p, doorStyle());
    ASSERT_TRUE(sh.ok) << sh.error;
    const int floorY = sh.floorTopByStory[0] / 9;
    auto rep = RealizedStructureValidator::checkDoorwayClearance(
        sh.plan, {0, 0, 0}, {box("chest", "hall", 0, 5, 1, 5, floorY)});
    EXPECT_TRUE(rep.ok()) << "a chest under a WINDOW was flagged as blocking a passage";
}
