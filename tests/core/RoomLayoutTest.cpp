#include <gtest/gtest.h>

#include <queue>

#include "core/RoomLayout.h"
#include "core/RoomProgram.h"
#include "core/BuildingProgram.h"
#include "core/BuildingProgramValidator.h"

using namespace Phyxel::Core;

namespace {
bool overlaps(const Rect& a, const Rect& b) {
    return a.x < b.x1() && b.x < a.x1() && a.z < b.z1() && b.z < a.z1();
}
// The grounded hall_house typology: service + hall(×2 bays) + solar (room_program.json).
RoomProgram hallHouse() {
    RoomProgram rp;
    rp.name = "hall_house"; rp.bays = 4; rp.bayLength = 4; rp.widthMin = 6; rp.widthMax = 8;
    rp.rooms = {{"service", "service", 1.0}, {"hall", "hall", 2.0}, {"solar", "solar", 1.0}};
    return rp;
}
bool hasPurpose(const std::vector<ProgRoom>& rooms, const std::string& purpose) {
    for (const auto& r : rooms) if (r.purpose == purpose) return true;
    return false;
}
bool hasCode(const ValidationReport& r, const std::string& code) {
    for (const auto& i : r.issues()) if (i.code == code) return true;
    return false;
}
// Build a 1-story program carrying a generated layout, for the validator gate.
BuildingProgram programFrom(const RoomLayout& rl, int W, int D) {
    BuildingProgram p;
    p.name = "gen"; p.style = "timber_cottage"; p.footprintW = W; p.footprintD = D;
    p.substructure = "crawlspace";
    ProgStory s; s.height = 3; s.rooms = rl.rooms; s.portals = rl.portals;
    p.stories.push_back(s);
    return p;
}
}  // namespace

TEST(RoomLayoutTest, TilesFootprintNoGapNoOverlap) {
    auto rl = generateRoomLayout(7, 9, 4, 123);
    int area = 0;
    for (const auto& r : rl.rooms) area += r.rect.w * r.rect.d;
    EXPECT_EQ(area, 7 * 9) << "rooms don't tile the footprint (gap or overlap area)";
    for (size_t i = 0; i < rl.rooms.size(); ++i)
        for (size_t j = i + 1; j < rl.rooms.size(); ++j)
            EXPECT_FALSE(overlaps(rl.rooms[i].rect, rl.rooms[j].rect))
                << "rooms " << i << " and " << j << " overlap";
}

TEST(RoomLayoutTest, EveryRoomMeetsMinSize) {
    auto rl = generateRoomLayout(7, 9, 5, 99);
    for (const auto& r : rl.rooms) { EXPECT_GE(r.rect.w, 2); EXPECT_GE(r.rect.d, 2); }
}

// THE base-pipeline fix: a typology-driven house gets the typology's PURPOSED rooms — a service end,
// a hall, a solar (bedroom) — not N identical "living" rooms. This is what makes it a real house.
// (Red on the "living" stub: a hall_house yields no service/hall/solar; green once purposes flow.)
TEST(RoomLayoutTest, TypologyLayoutAssignsRealPurposes) {
    auto rl = generateRoomLayoutFromProgram(16, 7, hallHouse());
    ASSERT_EQ(rl.rooms.size(), 3u) << "hall_house has 3 rooms (service/hall/solar)";
    EXPECT_TRUE(hasPurpose(rl.rooms, "service")) << "no service (kitchen-end) room — house isn't real";
    EXPECT_TRUE(hasPurpose(rl.rooms, "hall"))    << "no hall (living) room";
    EXPECT_TRUE(hasPurpose(rl.rooms, "solar"))   << "no solar (bedroom) room";
    EXPECT_FALSE(hasPurpose(rl.rooms, "living")) << "rooms still stamped generic 'living'";
}

// The grounded shape: rooms tile the footprint, and the hall (2 bays) is the largest slice.
TEST(RoomLayoutTest, TypologyLayoutTilesAndSizesByBays) {
    const int W = 16, D = 7;
    auto rl = generateRoomLayoutFromProgram(W, D, hallHouse());
    ASSERT_EQ(rl.rooms.size(), 3u);
    int area = 0;
    for (const auto& r : rl.rooms) area += r.rect.w * r.rect.d;
    EXPECT_EQ(area, W * D) << "typology rooms don't tile the footprint";
    for (size_t i = 0; i < rl.rooms.size(); ++i)
        for (size_t j = i + 1; j < rl.rooms.size(); ++j)
            EXPECT_FALSE(overlaps(rl.rooms[i].rect, rl.rooms[j].rect));
    // hall has 2 bays vs 1 for service/solar -> its length slice must be the largest.
    int serviceW = 0, hallW = 0, solarW = 0;
    for (const auto& r : rl.rooms) {
        if (r.purpose == "service") serviceW = r.rect.w;
        if (r.purpose == "hall")    hallW = r.rect.w;
        if (r.purpose == "solar")   solarW = r.rect.w;
    }
    EXPECT_GT(hallW, serviceW) << "the 2-bay hall should be longer than the 1-bay service";
    EXPECT_GT(hallW, solarW)   << "the 2-bay hall should be longer than the 1-bay solar";
}

// The generated typology layout is navigable + valid (no overlap, every room reachable, sizes ok).
TEST(RoomLayoutTest, TypologyLayoutPassesValidatorGates) {
    const int W = 16, D = 7;
    auto rl = generateRoomLayoutFromProgram(W, D, hallHouse());
    auto report = BuildingProgramValidator::validate(programFrom(rl, W, D));
    EXPECT_FALSE(hasCode(report, "room_overlap"));
    EXPECT_FALSE(hasCode(report, "room_unreachable")) << "a typology room is sealed off";
}

// autofill wires the typology into the ground story: an empty hall_house story comes back with
// service/hall/solar, not generic BSP "living" rooms.
TEST(RoomLayoutTest, AutofillUsesTypologyOnGroundStory) {
    BuildingProgram p;
    p.name = "h"; p.footprintW = 16; p.footprintD = 7; p.typology = "hall_house";
    ProgStory s; s.height = 3;            // no authored rooms
    p.stories.push_back(s);
    const RoomProgram rp = hallHouse();
    autofillRoomLayout(p, 1u, &rp);
    ASSERT_FALSE(p.stories[0].rooms.empty());
    EXPECT_TRUE(hasPurpose(p.stories[0].rooms, "service"));
    EXPECT_TRUE(hasPurpose(p.stories[0].rooms, "solar"));
    EXPECT_FALSE(hasPurpose(p.stories[0].rooms, "living")) << "autofill ignored the typology";
}

TEST(RoomLayoutTest, AdjacencyIsConnected) {
    auto rl = generateRoomLayout(10, 12, 6, 7);
    const int n = (int)rl.rooms.size();
    std::vector<std::vector<int>> adj(n);
    auto sharedOk = [](const Rect& a, const Rect& b) {
        if ((a.x1() == b.x || b.x1() == a.x) && std::min(a.z1(), b.z1()) > std::max(a.z, b.z)) return true;
        if ((a.z1() == b.z || b.z1() == a.z) && std::min(a.x1(), b.x1()) > std::max(a.x, b.x)) return true;
        return false;
    };
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            if (sharedOk(rl.rooms[i].rect, rl.rooms[j].rect)) { adj[i].push_back(j); adj[j].push_back(i); }
    std::vector<bool> vis(n, false); std::queue<int> q; q.push(0); vis[0] = true; int seen = 1;
    while (!q.empty()) { int u = q.front(); q.pop(); for (int v : adj[u]) if (!vis[v]) { vis[v] = true; ++seen; q.push(v); } }
    EXPECT_EQ(seen, n) << "the room partition is not a connected adjacency graph";
}

TEST(RoomLayoutTest, DeterministicBySeed) {
    auto a = generateRoomLayout(10, 12, 5, 777);
    auto b = generateRoomLayout(10, 12, 5, 777);
    ASSERT_EQ(a.rooms.size(), b.rooms.size());
    for (size_t i = 0; i < a.rooms.size(); ++i) {
        EXPECT_EQ(a.rooms[i].rect.x, b.rooms[i].rect.x);
        EXPECT_EQ(a.rooms[i].rect.z, b.rooms[i].rect.z);
        EXPECT_EQ(a.rooms[i].rect.w, b.rooms[i].rect.w);
        EXPECT_EQ(a.rooms[i].rect.d, b.rooms[i].rect.d);
    }
}

// The generated program must clear the validator's L2 gates: no overlap, every room reachable
// (topology), an entrance present.
TEST(RoomLayoutTest, GeneratedLayoutPassesValidatorGates) {
    auto rl = generateRoomLayout(7, 9, 4, 123);
    auto r = BuildingProgramValidator::validate(programFrom(rl, 7, 9));
    EXPECT_FALSE(hasCode(r, "room_overlap")) << r.summary();
    EXPECT_FALSE(hasCode(r, "room_unreachable")) << r.summary();
    EXPECT_FALSE(hasCode(r, "no_entrance")) << r.summary();
}

// autofillRoomLayout (the build-handler seam): a story with no authored rooms gets a tiling layout
// + an entrance, and the result clears the validator gates.
TEST(RoomLayoutTest, AutofillFillsEmptyStory) {
    BuildingProgram p;
    p.name = "x"; p.style = "timber_cottage"; p.footprintW = 7; p.footprintD = 9; p.substructure = "crawlspace";
    p.stories.push_back(ProgStory{});       // one story, height default, NO rooms
    autofillRoomLayout(p, 42);
    ASSERT_FALSE(p.stories[0].rooms.empty()) << "autofill left the story empty";
    int area = 0;
    for (const auto& rm : p.stories[0].rooms) area += rm.rect.w * rm.rect.d;
    EXPECT_EQ(area, 7 * 9) << "autofilled rooms don't tile the footprint";
    bool ext = false;
    for (const auto& pt : p.stories[0].portals) if (pt.a == "exterior" || pt.b == "exterior") ext = true;
    EXPECT_TRUE(ext) << "no exterior entrance after autofill";
    auto r = BuildingProgramValidator::validate(p);
    EXPECT_FALSE(hasCode(r, "room_overlap")) << r.summary();
    EXPECT_FALSE(hasCode(r, "room_unreachable")) << r.summary();
}

// autofill must NOT clobber a hand-authored layout.
TEST(RoomLayoutTest, AutofillRespectsAuthoredRooms) {
    BuildingProgram p;
    p.name = "x"; p.style = "timber_cottage"; p.footprintW = 7; p.footprintD = 9;
    ProgStory s; s.height = 3;
    ProgRoom hall; hall.id = "hall"; hall.rect = {0, 0, 7, 9}; hall.purpose = "living";
    s.rooms.push_back(hall);
    p.stories.push_back(s);
    autofillRoomLayout(p, 42);
    ASSERT_EQ(p.stories[0].rooms.size(), 1u) << "autofill clobbered authored rooms";
    EXPECT_EQ(p.stories[0].rooms[0].id, "hall");
}
