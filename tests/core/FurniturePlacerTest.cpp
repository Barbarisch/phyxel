#include <gtest/gtest.h>

#include "core/FurniturePlacer.h"
#include "core/BuildingProgram.h"

using namespace Phyxel::Core;

namespace {
ProgStory story(const char* s) { return ProgStory::fromJson(nlohmann::json::parse(s)); }
const FurniturePlacement* find(const std::vector<FurniturePlacement>& v, const std::string& t) {
    for (const auto& f : v) if (f.type == t) return &f;
    return nullptr;
}
} // namespace

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
