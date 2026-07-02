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

// A piece FORCED onto a door wall must skip rather than block the doorway. Doors on ALL FOUR walls
// (centred) make every wall-centre cell a doorway threshold — so whichever wall a centred piece tries,
// the ONLY thing that can reject it is the blocked-cell guard (not the door-wall preference, since
// every wall is a door wall). With the guard, both pieces are skipped; delete `blocked.count(c)` from
// fits() and they'd land ON the door cells, so this test has real teeth on that guard.
TEST(FurniturePlacerTest, PieceForcedOntoDoorWallSkipsNotBlocks) {
    const auto s = story(R"json({
        "height":3,
        "rooms":[{"id":"r","rect":[0,0,4,4],"purpose":"store"}],
        "portals":[
            {"between":["exterior","r"],"pos":[2,0],"width":1,"height":2,"kind":"door"},
            {"between":["exterior","r"],"pos":[2,4],"width":1,"height":2,"kind":"door"},
            {"between":["exterior","r"],"pos":[0,2],"width":1,"height":2,"kind":"door"},
            {"between":["exterior","r"],"pos":[4,2],"width":1,"height":2,"kind":"door"}
        ]
    })json");
    const std::map<std::string, Footprint> fp = {{"barrel", {1, 1}}, {"chest", {1, 1}}};
    const auto aware = FurniturePlacer::furnish(s, glm::ivec3(0, 0, 0), 10, fp);
    // The invariant is NOT "place nothing" — packing now relocates pieces to the free CORNERS. The
    // invariant is that NO piece sits on a doorway threshold (door cell or the cell just inside it).
    const std::pair<int, int> doorThresholds[] = {
        {2, 0}, {1, 0}, {2, 3}, {1, 3}, {0, 2}, {0, 1}, {3, 2}, {3, 1}};
    for (const auto& p : aware) {
        const std::pair<int, int> cell{p.worldPos.x, p.worldPos.z};
        bool onThreshold = false;
        for (const auto& d : doorThresholds) if (d == cell) onThreshold = true;
        EXPECT_FALSE(onThreshold) << "furniture on a doorway threshold at ("
                                  << cell.first << "," << cell.second << ") — blocks the door";
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

// ============================================================================
// MICRO-PRECISE placement (the wall-clip / floor-sink fix). Furniture is positioned on the micro
// grid (1 cube = 9 micro): inset off the thin perimeter wall band, sitting on the EXACT walkable
// surface — never inside the wall or floor. These pin the geometry of FurniturePlacer::microWorldPos.
// ============================================================================

// A piece backing the MIN-X wall must inset +extT micro (clearing the wall band) and sit on the
// exact surface micro-Y — NOT the old cube origin (inset 0 -> clipped) nor the truncated cube Y.
TEST(FurniturePlacerTest, MicroPlacementInsetsOffMinXWall) {
    FurniturePlacement p;
    p.worldPos = glm::ivec3(12, 99, 12);   // cube cell; y here is the (stale) truncated cube Y
    p.backDir  = glm::ivec3(-1, 0, 0);     // backs onto the -x (min-x) wall
    const glm::ivec3 m = FurniturePlacer::microWorldPos(p, /*extTMicro=*/3, /*surfaceMicroY=*/150);
    EXPECT_EQ(m.x, 12 * 9 + 3) << "must inset +3 micro off the -x wall band [108,111)";
    EXPECT_EQ(m.z, 12 * 9)     << "no z wall -> no z inset";
    EXPECT_EQ(m.y, 150)        << "must sit on the exact walkable-surface micro, not a cube-truncated Y";
    // RED baseline the fix replaces: the old cube path placed at worldPos*9 (inset 0 -> INSIDE the
    // wall band) and at floorY*9 (truncated -> sunk into the floor).
    EXPECT_NE(m.x, p.worldPos.x * 9) << "no-inset placement clipped the wall (the original bug)";
}

// MAX-X wall: inset the OTHER way (-extT) so the piece's far face meets the wall's interior face.
TEST(FurniturePlacerTest, MicroPlacementInsetsOffMaxXWall) {
    FurniturePlacement p;
    p.worldPos = glm::ivec3(19, 99, 12);
    p.backDir  = glm::ivec3(1, 0, 0);      // backs onto the +x (max-x) wall
    const glm::ivec3 m = FurniturePlacer::microWorldPos(p, 3, 150);
    EXPECT_EQ(m.x, 19 * 9 - 3) << "must inset -3 micro so the +x face clears the +x wall band";
    EXPECT_EQ(m.y, 150);
}

// A CENTRE/interior piece (backDir 0) gets no inset, but still sits on the exact surface (no sink).
TEST(FurniturePlacerTest, MicroPlacementCentrePieceNoInsetButOnSurface) {
    FurniturePlacement p;
    p.worldPos = glm::ivec3(15, 99, 14);
    p.backDir  = glm::ivec3(0, 0, 0);
    const glm::ivec3 m = FurniturePlacer::microWorldPos(p, 3, 150);
    EXPECT_EQ(m.x, 15 * 9) << "centre piece: no inset";
    EXPECT_EQ(m.z, 14 * 9);
    EXPECT_EQ(m.y, 150)    << "still on the exact surface (the floor-sink fix applies to all pieces)";
}

// placedCubeSpan is the single source of truth for the cubes a micro-placed fixture actually occupies
// (reservation == registration == render). It must reproduce the observed MICRO-SPILL: the house_3
// fireplace template is 2x1 CUBES (micro extents 17 x 8) but, inset 3 micro off two walls at a corner,
// renders across 3x2 world cubes (x -10..-8, z 38..39) — the exact Bricks seen in the world.
TEST(FurniturePlacerTest, PlacedCubeSpanReproducesMicroSpill) {
    // corner hearth: backDir on BOTH axes, extT=3 -> +1 cube each axis (the spill).
    CubeSpan s = placedCubeSpan(/*microW=*/17, /*microD=*/8, /*rot=*/0,
                                glm::ivec3(1, 0, 1), /*extT=*/3, /*baseX=*/-9, /*baseZ=*/39);
    EXPECT_EQ(s.minX, -10); EXPECT_EQ(s.maxX, -8) << "fireplace spills to 3 cubes in x";
    EXPECT_EQ(s.minZ, 38);  EXPECT_EQ(s.maxZ, 39) << "fireplace spills to 2 cubes in z";
    EXPECT_EQ(s.width(), 3); EXPECT_EQ(s.depth(), 2);

    // NO inset (backDir 0) -> no spill: a 2x1-cube template stays 2x1.
    CubeSpan flush = placedCubeSpan(17, 8, 0, glm::ivec3(0, 0, 0), 3, 5, 5);
    EXPECT_EQ(flush.width(), 2) << "cube-aligned (no inset) template does not spill in x";
    EXPECT_EQ(flush.depth(), 1) << "cube-aligned template does not spill in z";

    // rotation 90 swaps the micro extents (a bed's long axis turns).
    CubeSpan rot = placedCubeSpan(17, 8, 90, glm::ivec3(0, 0, 0), 3, 0, 0);
    EXPECT_EQ(rot.width(), 1) << "rot90: x takes the SHORT micro extent";
    EXPECT_EQ(rot.depth(), 2) << "rot90: z takes the LONG micro extent";
}
