#include <gtest/gtest.h>

#include <queue>

#include "core/RoomLayout.h"
#include "core/BuildingProgram.h"
#include "core/BuildingProgramValidator.h"

using namespace Phyxel::Core;

namespace {
bool overlaps(const Rect& a, const Rect& b) {
    return a.x < b.x1() && b.x < a.x1() && a.z < b.z1() && b.z < a.z1();
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
