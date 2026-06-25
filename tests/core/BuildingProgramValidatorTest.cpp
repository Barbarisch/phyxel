#include <gtest/gtest.h>

#include "core/BuildingProgramValidator.h"
#include "core/BuildingProgram.h"

using namespace Phyxel::Core;

namespace {
// A clean single-story cottage: hall + kitchen, front door + interior arch.
const char* kGoodCottage = R"({
    "name": "cottage", "style": "timber_cottage", "footprint": [7, 9],
    "substructure": "crawlspace",
    "stories": [{
        "height": 3,
        "rooms": [
            { "id": "hall",    "rect": [0,0,4,9], "purpose": "living" },
            { "id": "kitchen", "rect": [4,0,3,9], "purpose": "kitchen" }
        ],
        "portals": [
            { "between": ["exterior","hall"], "pos": [0,3], "width": 1, "height": 2, "kind": "door" },
            { "between": ["hall","kitchen"],  "pos": [4,2], "width": 1, "height": 2, "kind": "arch" }
        ]
    }]
})";

// Three stories with STRAIGHT stair wells stacked at the same rect — even though each
// straight flight is walkable per-flight, the upper one's solid run fills the lower
// flight's headroom (the KI-4 column). Must trip stair_no_headroom.
const char* kStackedStraightStairs = R"({
    "name": "tower", "style": "timber_cottage", "footprint": [7, 9],
    "substructure": "crawlspace",
    "stories": [
        { "height": 3,
          "rooms": [{ "id": "r0", "rect": [0,0,7,9], "purpose": "living" }],
          "portals": [{ "between": ["exterior","r0"], "pos": [0,4], "width": 1, "height": 2, "kind": "door" }],
          "stairs": [{ "from_story": 0, "to_story": 1, "rect": [1,2,2,6], "form": "straight" }] },
        { "height": 3,
          "rooms": [{ "id": "r1", "rect": [0,0,7,9], "purpose": "living" }],
          "stairs": [{ "from_story": 1, "to_story": 2, "rect": [1,2,2,6], "form": "straight" }] },
        { "height": 3,
          "rooms": [{ "id": "r2", "rect": [0,0,7,9], "purpose": "living" }] }
    ]
})";

// The same stacked tower but with SWITCHBACK stairs (the default): folds to a compliant
// riser and interleaves lanes + a landing, so stacked wells keep their headroom -> walkable.
const char* kSwitchbackTower = R"({
    "name": "tower", "style": "timber_cottage", "footprint": [7, 9],
    "substructure": "crawlspace",
    "stories": [
        { "height": 3,
          "rooms": [{ "id": "r0", "rect": [0,0,7,9], "purpose": "living" }],
          "portals": [{ "between": ["exterior","r0"], "pos": [0,4], "width": 1, "height": 2, "kind": "door" }],
          "stairs": [{ "from_story": 0, "to_story": 1, "rect": [1,2,2,6], "form": "switchback" }] },
        { "height": 3,
          "rooms": [{ "id": "r1", "rect": [0,0,7,9], "purpose": "living" }],
          "stairs": [{ "from_story": 1, "to_story": 2, "rect": [1,2,2,6], "form": "switchback" }] },
        { "height": 3,
          "rooms": [{ "id": "r2", "rect": [0,0,7,9], "purpose": "living" }] }
    ]
})";

// A single straight flight with enough run for a compliant riser, no flight above — passes.
const char* kWalkableStraightStair = R"({
    "name": "duplex", "style": "timber_cottage", "footprint": [7, 9],
    "substructure": "crawlspace",
    "stories": [
        { "height": 3,
          "rooms": [{ "id": "r0", "rect": [0,0,7,9], "purpose": "living" }],
          "portals": [{ "between": ["exterior","r0"], "pos": [0,4], "width": 1, "height": 2, "kind": "door" }],
          "stairs": [{ "from_story": 0, "to_story": 1, "rect": [0,0,2,9], "form": "straight" }] },
        { "height": 3,
          "rooms": [{ "id": "r1", "rect": [0,0,7,9], "purpose": "living" }] }
    ]
})";

// A well far too small to hold a walkable flight (1x1) — no run for compliant risers.
const char* kShallowWellStair = R"({
    "name": "tiny", "style": "timber_cottage", "footprint": [7, 9],
    "substructure": "crawlspace",
    "stories": [
        { "height": 3,
          "rooms": [{ "id": "r0", "rect": [0,0,7,9], "purpose": "living" }],
          "portals": [{ "between": ["exterior","r0"], "pos": [0,4], "width": 1, "height": 2, "kind": "door" }],
          "stairs": [{ "from_story": 0, "to_story": 1, "rect": [3,3,1,1], "form": "straight" }] },
        { "height": 3,
          "rooms": [{ "id": "r1", "rect": [0,0,7,9], "purpose": "living" }] }
    ]
})";

// Stacked switchback stairs in stories TOO SHORT for head-room: floor-to-floor rise (~1.3 m) is
// below the character's standing height, so the upper flight sits within head-room of the lower
// emergence — a real, GEOMETRY-driven clearance failure (the old form==Straight gate misses it
// entirely because the form is switchback).
const char* kShortStoryTower = R"({
    "name": "squashed", "style": "timber_cottage", "footprint": [7, 9],
    "substructure": "crawlspace",
    "stories": [
        { "height": 1,
          "rooms": [{ "id": "r0", "rect": [0,0,7,9], "purpose": "living" }],
          "portals": [{ "between": ["exterior","r0"], "pos": [0,4], "width": 1, "height": 2, "kind": "door" }],
          "stairs": [{ "from_story": 0, "to_story": 1, "rect": [1,2,2,6], "form": "switchback" }] },
        { "height": 1,
          "rooms": [{ "id": "r1", "rect": [0,0,7,9], "purpose": "living" }],
          "stairs": [{ "from_story": 1, "to_story": 2, "rect": [1,2,2,6], "form": "switchback" }] },
        { "height": 1,
          "rooms": [{ "id": "r2", "rect": [0,0,7,9], "purpose": "living" }] }
    ]
})";

BuildingProgram parse(const char* s) {
    return BuildingProgram::fromJson(nlohmann::json::parse(s));
}
bool hasCode(const ValidationReport& r, const std::string& code) {
    for (const auto& i : r.issues()) if (i.code == code) return true;
    return false;
}
} // namespace

TEST(BuildingProgramValidatorTest, GoodCottagePasses) {
    auto r = BuildingProgramValidator::validate(parse(kGoodCottage));
    EXPECT_TRUE(r.ok()) << r.summary();
}

TEST(BuildingProgramValidatorTest, OverlappingRoomsFail) {
    auto p = parse(kGoodCottage);
    p.stories[0].rooms[1].rect = {2, 0, 3, 9};   // now overlaps hall (x 0..4)
    auto r = BuildingProgramValidator::validate(p);
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(hasCode(r, "room_overlap"));
}

TEST(BuildingProgramValidatorTest, UnreachableRoomFails) {
    auto p = parse(kGoodCottage);
    p.stories[0].portals.pop_back();             // drop the hall<->kitchen arch
    auto r = BuildingProgramValidator::validate(p);
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(hasCode(r, "room_unreachable"));
}

TEST(BuildingProgramValidatorTest, NoEntranceFails) {
    auto p = parse(kGoodCottage);
    p.stories[0].portals.erase(p.stories[0].portals.begin());   // drop the front door
    auto r = BuildingProgramValidator::validate(p);
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(hasCode(r, "no_entrance"));
}

TEST(BuildingProgramValidatorTest, LowCeilingFails) {
    auto p = parse(kGoodCottage);
    p.stories[0].height = 2;                      // 2 < 2.2 min
    auto r = BuildingProgramValidator::validate(p);
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(hasCode(r, "ceiling_too_low"));
}

TEST(BuildingProgramValidatorTest, ShortDoorFails) {
    auto p = parse(kGoodCottage);
    p.stories[0].portals[0].height = 1;           // 1 < 2.0 door clear min
    auto r = BuildingProgramValidator::validate(p);
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(hasCode(r, "door_too_short"));
}

TEST(BuildingProgramValidatorTest, ExteriorPortalOffPerimeterFails) {
    auto p = parse(kGoodCottage);
    p.stories[0].portals[0].px = 2;               // interior point, not on perimeter
    p.stories[0].portals[0].pz = 4;
    auto r = BuildingProgramValidator::validate(p);
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(hasCode(r, "exterior_portal_off_perimeter"));
}

// KI-4: the gate measures REAL vertical clearance on the planned geometry (not a form label).
// Thin treads give head-room regardless of form, so a stacked STRAIGHT tower with room-height
// stories is now clear — the old "straight stack is always blocked" premise is obsolete.
TEST(BuildingProgramValidatorTest, StackedStraightStairsAreClearWithThinTreads) {
    auto r = BuildingProgramValidator::validate(parse(kStackedStraightStairs));
    EXPECT_FALSE(hasCode(r, "stair_no_headroom")) << r.summary();
}

// The clearance gate fires on a GEOMETRY-driven failure the old form-label gate could not see:
// stacked switchback flights in stories too SHORT for head-room. RED on the old gate (switchback
// is exempt), GREEN once the gate measures real clearance on the StairPlan.
TEST(BuildingProgramValidatorTest, ShortStoriesLackStairHeadroom) {
    auto r = BuildingProgramValidator::validate(parse(kShortStoryTower));
    EXPECT_TRUE(hasCode(r, "stair_no_headroom")) << r.summary();
}

// The fix: the SAME stacked tower with switchback stairs is walkable — no gate failures.
TEST(BuildingProgramValidatorTest, SwitchbackTowerIsWalkable) {
    auto r = BuildingProgramValidator::validate(parse(kSwitchbackTower));
    EXPECT_FALSE(hasCode(r, "stair_riser_too_steep")) << r.summary();
    EXPECT_FALSE(hasCode(r, "stair_no_headroom")) << r.summary();
}

TEST(BuildingProgramValidatorTest, WalkableStraightStairPasses) {
    auto r = BuildingProgramValidator::validate(parse(kWalkableStraightStair));
    EXPECT_FALSE(hasCode(r, "stair_riser_too_steep")) << r.summary();
    EXPECT_FALSE(hasCode(r, "stair_no_headroom")) << r.summary();
}

// A well too small to hold a walkable flight must be flagged (can't make a compliant riser).
TEST(BuildingProgramValidatorTest, ShallowWellStairNotWalkable) {
    auto r = BuildingProgramValidator::validate(parse(kShallowWellStair));
    EXPECT_TRUE(hasCode(r, "stair_riser_too_steep")) << r.summary();
}

namespace {
RoomProgram makeTypology(const std::string& name, double wMin, double wMax,
                         double pMin, double pMax) {
    RoomProgram p;
    p.name = name; p.widthMin = wMin; p.widthMax = wMax;
    p.proportionMin = pMin; p.proportionMax = pMax;
    return p;
}
} // namespace

// Without a RoomProgram, behaviour is unchanged (the gate is opt-in).
TEST(BuildingProgramValidatorTest, RoomProgramGateIsOptIn) {
    auto r = BuildingProgramValidator::validate(parse(kGoodCottage));   // no roomProgram
    EXPECT_TRUE(r.ok()) << r.summary();
}

// The 7-wide cottage is TOO WIDE for a croft (cruck dwelling width <= 6 m).
TEST(BuildingProgramValidatorTest, TooWideForCroftFails) {
    RoomProgram croft = makeTypology("croft", 4, 6, 0, 3.0);
    auto r = BuildingProgramValidator::validate(parse(kGoodCottage), {}, &croft);
    EXPECT_TRUE(hasCode(r, "footprint_too_wide"));
    EXPECT_FALSE(r.ok());
}

// ...but it FITS a hall house (width 6-8), which the gate should accept.
TEST(BuildingProgramValidatorTest, FitsHallHouseTypology) {
    RoomProgram hall = makeTypology("hall_house", 6, 8, 0, 3.0);
    auto r = BuildingProgramValidator::validate(parse(kGoodCottage), {}, &hall);
    EXPECT_TRUE(r.ok()) << r.summary();
}

// A great-hall-style proportion bound rejects a too-square footprint.
TEST(BuildingProgramValidatorTest, ProportionGateRejectsTooSquare) {
    RoomProgram greatHall = makeTypology("manor_hall", 6, 12, 1.5, 3.0);
    // cottage is 7x9 -> ratio 1.28 < 1.5 min
    auto r = BuildingProgramValidator::validate(parse(kGoodCottage), {}, &greatHall);
    EXPECT_TRUE(hasCode(r, "footprint_too_square"));
}

TEST(BuildingProgramValidatorTest, UnusablyNarrowRoomFails) {
    auto p = parse(kGoodCottage);
    p.stories[0].rooms[1].rect = {4, 0, 1, 9};   // kitchen 1 wide -> < 2-cube usable min
    RoomProgram croft = makeTypology("croft", 4, 6, 0, 3.0);
    auto r = BuildingProgramValidator::validate(p, {}, &croft);
    EXPECT_TRUE(hasCode(r, "room_too_narrow"));
}

TEST(BuildingProgramValidatorTest, NonAdjacentInteriorPortalFails) {
    // Three rooms in a row; declare a portal between the two non-touching ends.
    auto p = parse(R"({
        "name": "row", "footprint": [9, 4],
        "stories": [{
            "height": 3,
            "rooms": [
                { "id": "a", "rect": [0,0,3,4] },
                { "id": "b", "rect": [3,0,3,4] },
                { "id": "c", "rect": [6,0,3,4] }
            ],
            "portals": [
                { "between": ["exterior","a"], "pos": [0,1], "width": 1, "height": 2, "kind": "door" },
                { "between": ["a","c"], "pos": [3,1], "width": 1, "height": 2, "kind": "arch" }
            ]
        }]
    })");
    auto r = BuildingProgramValidator::validate(p);
    EXPECT_TRUE(hasCode(r, "portal_rooms_not_adjacent"));   // a and c don't touch
}
