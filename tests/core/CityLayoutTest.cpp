#include <gtest/gtest.h>

#include <cmath>
#include <queue>
#include <set>
#include <unordered_map>

#include "core/RoomProgram.h"
#include "core/SettlementLayout.h"
#include "core/SettlementProgram.h"
#include "core/StreetPaver.h"
#include "core/TraversalProbe.h"

using namespace Phyxel::Core;

// ============================================================================
// planCityLayout — the semi-organic city quarter (site_settlement #38 growth
// axes + zone_districts #41): "slightly organized, a little chaotic".
// Organized: crossroads AXES (main + cross street) meeting at the market
// square; every plot fronts a street; one connected paved network. Chaotic
// (bounded): secondary streets at seeded-JITTERED block intervals, varied
// burgage frontages/setbacks, trades clustered inside the core ring (district
// weights are DATA). RED baseline: the stub delegates to the village planner —
// one spine, no cross axis, no blocks, no district ring.
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
    const SettlementTierPreset* city = nullptr;
    bool ok = false;
    Fixture() {
        if (loadShipped(sreg) && loadRooms(rreg)) {
            city = sreg.get("medieval", "city");
            ok = city != nullptr;
        }
    }
};
constexpr int W = 180, D = 110;
bool overlaps(const Rect& a, const Rect& b) {
    return a.x < b.x1() && b.x < a.x1() && a.z < b.z1() && b.z < a.z1();
}
bool touches(const Rect& a, const Rect& b) {
    if (overlaps(a, b)) return true;
    const bool xOverlap = a.x < b.x1() && b.x < a.x1();
    const bool zOverlap = a.z < b.z1() && b.z < a.z1();
    if (xOverlap && (a.z1() == b.z || b.z1() == a.z)) return true;
    if (zOverlap && (a.x1() == b.x || b.x1() == a.x)) return true;
    return false;
}
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
const std::set<std::string> SHOPS = {"tavern", "general_store", "bakery", "apothecary", "butcher"};
} // namespace

// Crossroads axes (RED on the stub): besides the main street, a full CROSS street of main width
// runs perpendicular through the settlement, and the market square sits over their crossing.
TEST(CityLayoutTest, CrossroadsAxesMeetAtTheMarketSquare) {
    Fixture f;
    if (!f.ok) GTEST_SKIP() << "canon files not reachable from CWD";
    const auto l = planCityLayout(*f.city, W, D, f.rreg, 3);
    ASSERT_TRUE(l.ok);
    ASSERT_TRUE(l.hasSquare);
    const bool alongX = l.mainStreet.w >= l.mainStreet.d;
    const Rect* cross = nullptr;
    for (const auto& s : l.base.streets) {
        const bool perp = alongX ? (s.d > s.w) : (s.w > s.d);
        const int width = alongX ? s.w : s.d;
        const int span = alongX ? s.d : s.w;
        if (perp && width == f.city->street.mainWidth && span >= D - 2) { cross = &s; break; }
    }
    ASSERT_NE(cross, nullptr) << "no full cross street of main width — a city needs its crossroads";
    EXPECT_TRUE(overlaps(l.marketSquare, l.mainStreet)) << "square off the main street";
    EXPECT_TRUE(overlaps(l.marketSquare, *cross)) << "square off the cross street";
}

// Bounded chaos (RED on the stub): secondary streets slice the quarter into blocks whose
// spacing stays inside the tier's jitter band — irregular but never degenerate.
TEST(CityLayoutTest, SecondaryStreetsSliceJitteredBlocks) {
    Fixture f;
    if (!f.ok) GTEST_SKIP() << "canon files not reachable from CWD";
    const auto l = planCityLayout(*f.city, W, D, f.rreg, 3);
    ASSERT_TRUE(l.ok);
    const bool alongX = l.mainStreet.w >= l.mainStreet.d;
    // collect the u-positions of all perpendicular streets (secondaries + the cross axis)
    std::vector<std::pair<int, int>> bands;   // (u0, u1)
    for (const auto& s : l.base.streets) {
        const bool perp = alongX ? (s.d > s.w) : (s.w > s.d);
        if (!perp) continue;
        bands.push_back(alongX ? std::make_pair(s.x, s.x1()) : std::make_pair(s.z, s.z1()));
    }
    ASSERT_GE(bands.size(), 3u) << "no secondary streets — the quarter is one undivided strip";
    std::sort(bands.begin(), bands.end());
    for (size_t i = 1; i < bands.size(); ++i) {
        const int gap = bands[i].first - bands[i - 1].second;
        EXPECT_GE(gap, f.city->blocksMin - 2) << "blocks " << i << " degenerate (" << gap << ")";
        EXPECT_LE(gap, f.city->blocksMax + f.city->blocksMin)
            << "block " << i << " oversized (" << gap << ")";
    }
}

// Every plot fronts a street (the >=95% chaos bound — by construction it should be 100%),
// nothing overlaps, and the whole street network is one connected component.
TEST(CityLayoutTest, PlotsFrontStreetsNoOverlapsConnectedNetwork) {
    Fixture f;
    if (!f.ok) GTEST_SKIP() << "canon files not reachable from CWD";
    const auto l = planCityLayout(*f.city, W, D, f.rreg, 3);
    ASSERT_TRUE(l.ok);
    ASSERT_GE(l.assigned.size(), 12u);
    int fronting = 0;
    for (size_t i = 0; i < l.assigned.size(); ++i) {
        const Rect& p = l.assigned[i].plot.rect;
        bool fronts = false;
        for (const auto& s : l.base.streets) {
            EXPECT_FALSE(overlaps(p, s)) << "plot " << i << " overlaps a street";
            if (touches(p, s)) fronts = true;
        }
        if (fronts) ++fronting;
        for (size_t j = i + 1; j < l.assigned.size(); ++j)
            EXPECT_FALSE(overlaps(p, l.assigned[j].plot.rect))
                << "plots " << i << "," << j << " overlap";
        EXPECT_FALSE(overlaps(l.assigned[i].footprint, l.marketSquare));
    }
    EXPECT_GE(fronting * 100, static_cast<int>(l.assigned.size()) * 95)
        << "plots not fronting any street: " << (l.assigned.size() - fronting);
    EXPECT_TRUE(networkConnected(l.base.streets));
}

// Anti-uniformity: plot areas vary (the burgage mix, not a stamped grid).
TEST(CityLayoutTest, PlotAreasAreNotUniform) {
    Fixture f;
    if (!f.ok) GTEST_SKIP() << "canon files not reachable from CWD";
    const auto l = planCityLayout(*f.city, W, D, f.rreg, 3);
    ASSERT_TRUE(l.ok);
    std::set<int> areas;
    for (const auto& ap : l.assigned) areas.insert(ap.plot.rect.w * ap.plot.rect.d);
    EXPECT_GT(areas.size(), 2u) << "plot areas uniform — grid behavior";
}

// Districts are data (RED on the stub): inside the core ring around the square the draw uses the
// shop-heavy core weights, so trades cluster on the market; the fringe stays dwelling-heavy.
TEST(CityLayoutTest, TradesClusterInsideTheCoreRing) {
    Fixture f;
    if (!f.ok) GTEST_SKIP() << "canon files not reachable from CWD";
    ASSERT_GT(f.city->coreRing, 0);
    ASSERT_FALSE(f.city->coreTypologyWeights.empty());
    const auto l = planCityLayout(*f.city, W, D, f.rreg, 3);
    ASSERT_TRUE(l.ok);
    ASSERT_TRUE(l.hasSquare);
    const int scx = l.marketSquare.x + l.marketSquare.w / 2;
    const int scz = l.marketSquare.z + l.marketSquare.d / 2;
    int coreN = 0, coreShops = 0, fringeN = 0, fringeShops = 0;
    for (const auto& ap : l.assigned) {
        // classify by the FRONTAGE midpoint (market adjacency is about the street frontage,
        // and it is the point the allocator's ring draw uses)
        int px, pz;
        const Rect& p = ap.plot.rect;
        switch (ap.streetSide) {
            case 'S': px = p.x + p.w / 2; pz = p.z; break;
            case 'N': px = p.x + p.w / 2; pz = p.z1(); break;
            case 'W': px = p.x; pz = p.z + p.d / 2; break;
            default:  px = p.x1(); pz = p.z + p.d / 2; break;
        }
        const int cheb = std::max(std::abs(px - scx), std::abs(pz - scz));
        const bool shop = SHOPS.count(ap.typology) > 0;
        if (cheb <= f.city->coreRing) { ++coreN; coreShops += shop; }
        else                          { ++fringeN; fringeShops += shop; }
    }
    ASSERT_GT(coreN, 0) << "no plots in the core ring at all";
    ASSERT_GT(fringeN, 0);
    EXPECT_GE(coreShops * 2, coreN)
        << "core ring is not trade-heavy (" << coreShops << "/" << coreN << ")";
    EXPECT_LE(fringeShops * 10, fringeN * 3)
        << "fringe is shop-heavy (" << fringeShops << "/" << fringeN << ") — ring weights unused";
}

// L3: over the paved network (flat ground), a probe walks from the square centre to the main
// street's far end AND into a cross-street row — the crossroads city is walkable end to end.
TEST(CityLayoutTest, ProbeWalksSquareToStreetEndAndCrossRow) {
    Fixture f;
    if (!f.ok) GTEST_SKIP() << "canon files not reachable from CWD";
    const auto l = planCityLayout(*f.city, W, D, f.rreg, 3);
    ASSERT_TRUE(l.ok);
    ASSERT_TRUE(l.hasSquare);
    auto ground = [](int, int) { return (16 + 1) * 9; };
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
    const int sy = (16 + 1) * 9 + 1;
    const glm::ivec3 start{(l.marketSquare.x + l.marketSquare.w / 2) * 9, sy,
                           (l.marketSquare.z + l.marketSquare.d / 2) * 9};
    const glm::ivec3 lo{0, sy - 18, 0}, hi{W * 9, sy + 18, D * 9};
    const bool alongX = l.mainStreet.w >= l.mainStreet.d;
    const glm::ivec3 end = alongX
        ? glm::ivec3{(l.mainStreet.x + 2) * 9, sy, (l.mainStreet.z + l.mainStreet.d / 2) * 9}
        : glm::ivec3{(l.mainStreet.x + l.mainStreet.w / 2) * 9, sy, (l.mainStreet.z + 2) * 9};
    EXPECT_TRUE(probe.reachable(start, end - glm::ivec3(5, 9, 5), end + glm::ivec3(5, 9, 5), lo, hi))
        << "main street end unreachable from the square";
    // a plot fronting the CROSS axis ('W' or 'E' when the main street runs along X): its street
    // edge must be walkable from the square
    for (const auto& ap : l.assigned) {
        const bool crossRow = alongX ? (ap.streetSide == 'W' || ap.streetSide == 'E')
                                     : (ap.streetSide == 'S' || ap.streetSide == 'N');
        if (!crossRow) continue;
        const Rect& p = ap.plot.rect;
        glm::ivec3 door;
        switch (ap.streetSide) {
            case 'W': door = {p.x * 9 - 5, sy, (p.z + p.d / 2) * 9}; break;
            case 'E': door = {p.x1() * 9 + 4, sy, (p.z + p.d / 2) * 9}; break;
            case 'S': door = {(p.x + p.w / 2) * 9, sy, p.z * 9 - 5}; break;
            default:  door = {(p.x + p.w / 2) * 9, sy, p.z1() * 9 + 4}; break;
        }
        EXPECT_TRUE(probe.reachable(start, door - glm::ivec3(5, 9, 5), door + glm::ivec3(5, 9, 5),
                                    lo, hi))
            << "cross-row frontage unreachable from the square";
        break;
    }
}

TEST(CityLayoutTest, DeterministicInSeed) {
    Fixture f;
    if (!f.ok) GTEST_SKIP() << "canon files not reachable from CWD";
    const auto a = planCityLayout(*f.city, W, D, f.rreg, 11);
    const auto b = planCityLayout(*f.city, W, D, f.rreg, 11);
    const auto c = planCityLayout(*f.city, W, D, f.rreg, 12);
    ASSERT_EQ(a.assigned.size(), b.assigned.size());
    EXPECT_EQ(a.base.streets.size(), b.base.streets.size());
    for (size_t i = 0; i < a.assigned.size(); ++i) {
        EXPECT_EQ(a.assigned[i].typology, b.assigned[i].typology);
        EXPECT_EQ(a.assigned[i].plot.rect.x, b.assigned[i].plot.rect.x);
        EXPECT_EQ(a.assigned[i].plot.rect.z, b.assigned[i].plot.rect.z);
    }
    bool differs = a.assigned.size() != c.assigned.size();
    for (size_t i = 0; !differs && i < a.assigned.size() && i < c.assigned.size(); ++i)
        differs = a.assigned[i].plot.rect.x != c.assigned[i].plot.rect.x ||
                  a.assigned[i].typology != c.assigned[i].typology;
    EXPECT_TRUE(differs) << "different seed produced an identical city";
}
