#include <gtest/gtest.h>

#include <map>
#include <set>
#include <tuple>

#include "core/FurniturePlacer.h"
#include "core/BuildingProgram.h"

using namespace Phyxel::Core;

namespace {
ProgStory story(const char* s) { return ProgStory::fromJson(nlohmann::json::parse(s)); }
std::set<std::tuple<int, int, int>> posSet(const std::vector<FurniturePlacement>& v) {
    std::set<std::tuple<int, int, int>> s;
    for (const auto& f : v) s.insert({f.worldPos.x, f.worldPos.y, f.worldPos.z});
    return s;
}
int sharedPositions(const std::vector<FurniturePlacement>& a, const std::vector<FurniturePlacement>& b) {
    auto sa = posSet(a); int n = 0;
    for (const auto& f : b) if (sa.count({f.worldPos.x, f.worldPos.y, f.worldPos.z})) ++n;
    return n;
}
const FurniturePlacement* find(const std::vector<FurniturePlacement>& v, const std::string& t) {
    for (const auto& f : v) if (f.type == t) return &f;
    return nullptr;
}
} // namespace

// KI-2: multi-story furniture must NOT stack. The origin of the bug (user's words): "every floor is
// exactly the same and therefore one floor blocks the one below it." The handler iterated the
// stories of ONE building and furnished each with the SAME constant floorY, so a tower of identical
// floors landed every story's furniture at the same world positions. This test reproduces that exact
// call shape — TWO distinct stories of one program — not the same story object furnished twice.
TEST(FurniturePlacerTest, PerStoryFloorYStopsCrossStoryStacking) {
    // A 2-story building whose floors are identical (the worst case the user described).
    const auto program = BuildingProgram::fromJson(nlohmann::json::parse(R"json({
        "name":"twin","footprintW":7,"footprintD":9,
        "stories":[
            {"height":3,"rooms":[{"id":"r","rect":[0,0,7,9],"purpose":"living"}]},
            {"height":3,"rooms":[{"id":"r","rect":[0,0,7,9],"purpose":"living"}]}
        ]
    })json"));
    ASSERT_EQ(program.stories.size(), 2u);
    const glm::ivec3 origin{0, 0, 0};

    // BUG repro (teeth): the handler's OLD behavior — furnish each story at one constant floorY.
    const auto bugLo = FurniturePlacer::furnish(program.stories[0], origin, 18);
    const auto bugHi = FurniturePlacer::furnish(program.stories[1], origin, 18);
    ASSERT_GT(bugLo.size(), 0u) << "no fixtures placed — can't exercise the bug";
    ASSERT_GT(bugHi.size(), 0u);
    EXPECT_GT(sharedPositions(bugLo, bugHi), 0)
        << "two stories at the SAME floorY did NOT collide — the check would have no teeth";

    // FIX: the handler's NEW behavior — each story gets its own walkable floor Y.
    const auto fixLo = FurniturePlacer::furnish(program.stories[0], origin, 18);
    const auto fixHi = FurniturePlacer::furnish(program.stories[1], origin, 21);
    EXPECT_EQ(sharedPositions(fixLo, fixHi), 0)
        << "per-story floorY still stacks furniture across stories (KI-2 not fixed)";
}

// FOOTPRINT-AWARE: two room-filling pieces must NOT both be placed (the second can't fit without
// overlapping). The with-vs-without contrast is the falsifiable measurement: dimension-blind (1×1)
// places BOTH (silent overlap); footprint-aware places only the one that fits.
TEST(FurniturePlacerTest, OversizedSecondPieceSkippedNotOverlapped) {
    // a service room (recipe: barrel + chest) sized 4×4, no doors
    const auto s = story(R"json({"height":3,"rooms":[{"id":"r","rect":[0,0,4,4],"purpose":"service"}]})json");
    const glm::ivec3 origin{0, 0, 0};
    // each piece fills the whole 4×4 room
    const std::map<std::string, Footprint> fp = {{"barrel", {4, 4}}, {"chest", {4, 4}}};

    const auto blind = FurniturePlacer::furnish(s, origin, 10);          // 1×1 -> both placed
    EXPECT_EQ(blind.size(), 2u) << "dimension-blind should place both (the overlap bug)";

    const auto aware = FurniturePlacer::furnish(s, origin, 10, fp);      // footprint-aware
    EXPECT_EQ(aware.size(), 1u) << "footprint-aware must skip the second room-filling piece";
}

// A piece deeper than the room is skipped entirely (can't be forced into an out-of-bounds spot).
TEST(FurniturePlacerTest, PieceTooBigForRoomIsSkipped) {
    const auto s = story(R"json({"height":3,"rooms":[{"id":"r","rect":[0,0,4,4],"purpose":"bedchamber"}]})json");
    // bed depth 9 >> room depth 4; chest is fine (1×1)
    const std::map<std::string, Footprint> fp = {{"bed", {1, 9}}, {"chest", {1, 1}}};
    const auto aware = FurniturePlacer::furnish(s, glm::ivec3(0,0,0), 10, fp);
    for (const auto& f : aware) EXPECT_NE(f.type, "bed") << "an oversized bed must not be placed";
    EXPECT_TRUE(find(aware, "chest") != nullptr) << "the chest still fits";
}

// A deep piece must not cover a doorway threshold — it relocates or is skipped, never blocks the door.
TEST(FurniturePlacerTest, DeepPieceDoesNotBlockDoorway) {
    // 4×4 room, a door on the south wall at x=2; a single deep barrel that would span to the door.
    const auto s = story(R"json({
        "height":3,
        "rooms":[{"id":"r","rect":[0,0,4,4],"purpose":"store"}],
        "portals":[{"between":["exterior","r"],"pos":[2,0],"width":1,"height":2,"kind":"door"}]
    })json");
    const std::map<std::string, Footprint> fp = {{"barrel", {1, 4}}, {"chest", {1, 1}}};
    const auto aware = FurniturePlacer::furnish(s, glm::ivec3(0,0,0), 10, fp);
    // the doorway threshold cell (2,0)/(1,0) must be free of every placed piece's anchor span.
    for (const auto& f : aware) {
        EXPECT_FALSE(f.worldPos.x == 2 && f.worldPos.z == 0) << "a piece sits on the doorway";
        EXPECT_FALSE(f.worldPos.x == 1 && f.worldPos.z == 0) << "a piece sits on the doorway";
    }
}

// The convention I kept getting wrong by hand: a piece against the MIN-X wall faces
// +x (into the room) => rotation 270, NOT 90/"east".
TEST(FurniturePlacerTest, FacingIntoRoomMatchesEngineConvention) {
    EXPECT_EQ(FurniturePlacer::facingIntoRoom(+1, 0), 270);  // min-x wall -> front +x
    EXPECT_EQ(FurniturePlacer::facingIntoRoom(-1, 0), 90);   // max-x wall -> front -x
    EXPECT_EQ(FurniturePlacer::facingIntoRoom(0, +1), 0);    // min-z wall -> front +z
    EXPECT_EQ(FurniturePlacer::facingIntoRoom(0, -1), 180);  // max-z wall -> front -z
}

// THE bug the user caught: the fireplace must face INTO the room, computed from its
// wall — not hand-set wrong. Force it onto the min-x wall and check it faces +x.
TEST(FurniturePlacerTest, FireplaceFacesIntoTheRoom) {
    auto s = story(R"json({
        "height": 3,
        "rooms": [{ "id": "hall", "rect": [0,0,5,5], "purpose": "living" }],
        "portals": [
            { "between": ["exterior","hall"], "pos": [5,2], "width": 1, "height": 2, "kind": "window" },
            { "between": ["exterior","hall"], "pos": [2,0], "width": 1, "height": 2, "kind": "door" },
            { "between": ["exterior","hall"], "pos": [2,5], "width": 1, "height": 2, "kind": "window" }
        ]
    })json");
    auto fx = FurniturePlacer::furnish(s, glm::ivec3(100, 17, 200), 18);
    const FurniturePlacement* fire = find(fx, "fireplace");
    ASSERT_NE(fire, nullptr);
    EXPECT_EQ(fire->worldPos.x, 100);   // origin.x + min-x wall (rx=0) — only free wall
    EXPECT_EQ(fire->rotation, 270);     // faces +x INTO the room (not at the wall)
    EXPECT_EQ(fire->worldPos.y, 18);    // on the floor, not floating
}

TEST(FurniturePlacerTest, BedroomGetsBedOnAFreeWallOnTheFloor) {
    auto s = story(R"json({
        "height": 3,
        "rooms": [{ "id": "bc", "rect": [0,0,4,5], "purpose": "bedchamber" }],
        "portals": [{ "between": ["hall","bc"], "pos": [0,2], "width": 1, "height": 2, "kind": "door" }]
    })json");
    auto fx = FurniturePlacer::furnish(s, glm::ivec3(0, 0, 0), 10);
    const FurniturePlacement* bed = find(fx, "bed");
    ASSERT_NE(bed, nullptr);
    EXPECT_EQ(bed->worldPos.y, 10);                       // on the floor
    EXPECT_GE(bed->worldPos.x, 0); EXPECT_LT(bed->worldPos.x, 4);   // inside the room
    EXPECT_GE(bed->worldPos.z, 0); EXPECT_LT(bed->worldPos.z, 5);
    EXPECT_NE(bed->worldPos.x, 0);                        // NOT on the min-x (door) wall
    EXPECT_TRUE(find(fx, "chest") != nullptr);            // bedroom also gets a chest
}

TEST(FurniturePlacerTest, KitchenAndTinyRooms) {
    auto s = story(R"json({
        "height": 3,
        "rooms": [
            { "id": "k", "rect": [0,0,4,4], "purpose": "kitchen" },
            { "id": "closet", "rect": [4,0,1,1], "purpose": "store" }
        ],
        "portals": []
    })json");
    auto fx = FurniturePlacer::furnish(s, glm::ivec3(0, 0, 0), 5);
    EXPECT_TRUE(find(fx, "counter") != nullptr);          // kitchen gets a counter
    for (const auto& f : fx) EXPECT_NE(f.room, "closet");  // 1x1 room too small -> skipped
}
