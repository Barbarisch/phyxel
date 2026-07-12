#include <gtest/gtest.h>

#include <queue>
#include <unordered_map>

#include "core/RoomProgram.h"
#include "core/SettlementLayout.h"
#include "core/SettlementProgram.h"
#include "core/StreetPaver.h"
#include "core/TraversalProbe.h"

using namespace Phyxel::Core;

// ============================================================================
// Market town tier (place_public_spaces #43 / zone_parcel back lanes) — the
// town morphology: the main street WIDENS into a market square at mid-length
// (the common English market form — a widened street, not a detached plaza),
// building-free and paved; burgage rows get BACK LANES behind them, joined to
// the main street by end connectors so the street network is one connected
// component. L3: a probe walks the paved network from the street end into the
// square and into a back lane. RED baseline: the village-only main-street
// planner emits no square and no lanes (streets == {spine}).
// ============================================================================

namespace {
bool loadShipped(SettlementProgramRegistry& reg) {
    for (const char* p : {"resources/settlement_program.json", "../resources/settlement_program.json",
                          "../../resources/settlement_program.json",
                          "../../../resources/settlement_program.json"})
        if (reg.loadFromFile(p)) return true;
    return false;
}
bool loadRooms(RoomProgramRegistry& reg) {
    for (const char* p : {"resources/room_program.json", "../resources/room_program.json",
                          "../../resources/room_program.json", "../../../resources/room_program.json"})
        if (reg.loadFromFile(p)) return true;
    return false;
}
struct Fixture {
    SettlementProgramRegistry sreg;
    RoomProgramRegistry rreg;
    const SettlementTierPreset* town = nullptr;
    bool ok = false;
    Fixture() {
        if (loadShipped(sreg) && loadRooms(rreg)) {
            town = sreg.get("medieval", "town");
            ok = town != nullptr;
        }
    }
};
bool overlaps(const Rect& a, const Rect& b) {
    return a.x < b.x1() && b.x < a.x1() && a.z < b.z1() && b.z < a.z1();
}
// Two street rects belong to one walkable network if they overlap OR share an edge run.
bool touches(const Rect& a, const Rect& b) {
    if (overlaps(a, b)) return true;
    const bool xOverlap = a.x < b.x1() && b.x < a.x1();
    const bool zOverlap = a.z < b.z1() && b.z < a.z1();
    if (xOverlap && (a.z1() == b.z || b.z1() == a.z)) return true;
    if (zOverlap && (a.x1() == b.x || b.x1() == a.x)) return true;
    return false;
}
// All street rects reachable from streets[0] via touch-adjacency?
bool networkConnected(const std::vector<Rect>& streets) {
    if (streets.empty()) return false;
    std::vector<bool> seen(streets.size(), false);
    std::queue<size_t> q;
    q.push(0); seen[0] = true;
    size_t n = 1;
    while (!q.empty()) {
        size_t i = q.front(); q.pop();
        for (size_t j = 0; j < streets.size(); ++j)
            if (!seen[j] && touches(streets[i], streets[j])) { seen[j] = true; q.push(j); ++n; }
    }
    return n == streets.size();
}
} // namespace

// THE town invariant (RED on the village-only planner): a market square exists, straddles the
// main street at mid-length, and is paved street-network area (in base.streets).
TEST(MarketTownLayoutTest, SquareStraddlesTheMainStreetAtMidLength) {
    Fixture f;
    if (!f.ok) GTEST_SKIP() << "canon files not reachable from CWD";
    const auto l = planMainStreetLayout(*f.town, 120, 56, f.rreg, 3);
    ASSERT_TRUE(l.ok);
    ASSERT_TRUE(l.hasSquare) << "town tier produced no market square";
    EXPECT_TRUE(overlaps(l.marketSquare, l.mainStreet))
        << "the square must WIDEN the main street (overlap it), not float detached";
    // centred near mid-length of the long axis
    const bool alongX = l.mainStreet.w >= l.mainStreet.d;
    const int mid = alongX ? 60 : 28;
    const int sqMid = alongX ? l.marketSquare.x + l.marketSquare.w / 2
                             : l.marketSquare.z + l.marketSquare.d / 2;
    EXPECT_NEAR(sqMid, mid, 6) << "square not at the street's mid-length";
    bool inStreets = false;
    for (const auto& s : l.base.streets)
        if (s.x == l.marketSquare.x && s.z == l.marketSquare.z &&
            s.w == l.marketSquare.w && s.d == l.marketSquare.d) inStreets = true;
    EXPECT_TRUE(inStreets) << "square must be street-network area (paved as a plaza)";
}

// The square is building-free: no plot (and no footprint) overlaps it.
TEST(MarketTownLayoutTest, SquareIsBuildingFree) {
    Fixture f;
    if (!f.ok) GTEST_SKIP() << "canon files not reachable from CWD";
    const auto l = planMainStreetLayout(*f.town, 120, 56, f.rreg, 3);
    ASSERT_TRUE(l.ok);
    ASSERT_TRUE(l.hasSquare);
    for (const auto& ap : l.assigned) {
        EXPECT_FALSE(overlaps(ap.plot.rect, l.marketSquare))
            << "plot at (" << ap.plot.rect.x << "," << ap.plot.rect.z << ") is in the square";
        EXPECT_FALSE(overlaps(ap.footprint, l.marketSquare));
    }
}

// Back lanes: the town's street network has lanes behind the plot rows, and the WHOLE network
// (spine + square + lanes + connectors) is ONE connected component.
TEST(MarketTownLayoutTest, BackLanesFormOneConnectedNetwork) {
    Fixture f;
    if (!f.ok) GTEST_SKIP() << "canon files not reachable from CWD";
    const auto l = planMainStreetLayout(*f.town, 120, 56, f.rreg, 3);
    ASSERT_TRUE(l.ok);
    EXPECT_GE(l.base.streets.size(), 5u)
        << "town needs spine + square + back lanes + connectors, got " << l.base.streets.size();
    EXPECT_TRUE(networkConnected(l.base.streets))
        << "street network is fragmented — a lane no character can reach from the street";
    // at least one lane on the far side of each plot row (a rect beyond some plot's rear edge)
    int rearLanes = 0;
    for (const auto& s : l.base.streets) {
        if (s.x == l.mainStreet.x && s.z == l.mainStreet.z) continue;
        for (const auto& ap : l.assigned) {
            const Rect& p = ap.plot.rect;
            if ((ap.streetSide == 'S' && s.z >= p.z1()) ||
                (ap.streetSide == 'N' && s.z1() <= p.z) ||
                (ap.streetSide == 'W' && s.x >= p.x1()) ||
                (ap.streetSide == 'E' && s.x1() <= p.x)) { ++rearLanes; break; }
        }
    }
    EXPECT_GE(rearLanes, 2) << "no back lanes behind the plot rows";
}

// L3: over the PAVED town network (flat terrain), a probe walks from the main street's end into
// the market square, and from the square into a back lane — the network is walkable, not just
// connected on paper.
TEST(MarketTownLayoutTest, ProbeWalksStreetIntoSquareAndBackLane) {
    Fixture f;
    if (!f.ok) GTEST_SKIP() << "canon files not reachable from CWD";
    const auto l = planMainStreetLayout(*f.town, 120, 56, f.rreg, 3);
    ASSERT_TRUE(l.ok);
    ASSERT_TRUE(l.hasSquare);
    auto ground = [](int, int) { return (16 + 1) * 9; };            // flat plain, top face micro
    const auto plan = planStreetPaving(l.base.streets, {0, 0}, {}, {}, ground, AgentBox{},
                                       "Cobblestone");
    ASSERT_TRUE(plan.ok);
    std::unordered_map<long long, int> cols;
    for (const auto& c : plan.columns)
        cols[(static_cast<long long>(c.x) << 32) ^ (c.z & 0xffffffffLL)] = c.surface;
    auto occupied = [&](int x, int y, int z) {
        auto it = cols.find((static_cast<long long>(x) << 32) ^ (z & 0xffffffffLL));
        if (it != cols.end()) return y <= it->second;
        return y < ground(x, z);
    };
    TraversalProbe probe(occupied, AgentBox{});
    const int surf = (16 + 1) * 9 + 1;                              // walk top over paving
    // start: main street end; goals: square centre, then a rear-lane cell
    const bool alongX = l.mainStreet.w >= l.mainStreet.d;
    const int sy = surf;
    glm::ivec3 start{(l.mainStreet.x + 1) * 9 + 4, sy,
                     (l.mainStreet.z + l.mainStreet.d / 2) * 9 + 4};
    if (!alongX) start = {(l.mainStreet.x + l.mainStreet.w / 2) * 9 + 4, sy,
                          (l.mainStreet.z + 1) * 9 + 4};
    const glm::ivec3 sq{(l.marketSquare.x + l.marketSquare.w / 2) * 9, sy,
                        (l.marketSquare.z + l.marketSquare.d / 2) * 9};
    const glm::ivec3 lo{0, sy - 18, 0}, hi{120 * 9, sy + 18, 56 * 9};
    EXPECT_TRUE(probe.reachable(start, sq - glm::ivec3(4, 9, 4), sq + glm::ivec3(4, 9, 4), lo, hi))
        << "square unreachable from the main street over the paving";
    // find a back-lane rect (not the spine, not the square) and walk into it
    for (const auto& s : l.base.streets) {
        if ((s.x == l.mainStreet.x && s.z == l.mainStreet.z && s.w == l.mainStreet.w) ||
            (s.x == l.marketSquare.x && s.z == l.marketSquare.z && s.w == l.marketSquare.w))
            continue;
        const glm::ivec3 lane{(s.x + s.w / 2) * 9, sy, (s.z + s.d / 2) * 9};
        EXPECT_TRUE(probe.reachable(start, lane - glm::ivec3(4, 9, 4), lane + glm::ivec3(4, 9, 4),
                                    lo, hi))
            << "lane at (" << s.x << "," << s.z << ") unreachable over the paving";
        break;
    }
}

TEST(MarketTownLayoutTest, DeterministicInSeed) {
    Fixture f;
    if (!f.ok) GTEST_SKIP() << "canon files not reachable from CWD";
    const auto a = planMainStreetLayout(*f.town, 120, 56, f.rreg, 9);
    const auto b = planMainStreetLayout(*f.town, 120, 56, f.rreg, 9);
    ASSERT_EQ(a.assigned.size(), b.assigned.size());
    EXPECT_EQ(a.hasSquare, b.hasSquare);
    EXPECT_EQ(a.base.streets.size(), b.base.streets.size());
    for (size_t i = 0; i < a.assigned.size(); ++i) {
        EXPECT_EQ(a.assigned[i].typology, b.assigned[i].typology);
        EXPECT_EQ(a.assigned[i].plot.rect.x, b.assigned[i].plot.rect.x);
        EXPECT_EQ(a.assigned[i].plot.rect.z, b.assigned[i].plot.rect.z);
    }
}
