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
