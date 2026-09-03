#include <gtest/gtest.h>

#include <queue>
#include <set>

#include "core/RoomProgram.h"
#include "core/SettlementLayout.h"
#include "core/SettlementProgram.h"

using namespace Phyxel::Core;

// ============================================================================
// Market-square dressing (CityForgePlan M1, place_public_spaces #43): the city
// square gets a civic STATUE at the centre (the market-cross spot), the tier
// WELL relocated to a corner pad, and trestle STALLS flush against the
// through-street edges of the pads. Town keeps its centre well and gains
// stalls. RED baseline: planSquareDressing returns {} — every invariant below
// fails until the planner exists.
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
    const SettlementTierPreset* town = nullptr;
    bool ok = false;
    Fixture() {
        if (loadShipped(sreg) && loadRooms(rreg)) {
            city = sreg.get("medieval", "city");
            town = sreg.get("medieval", "town");
            ok = city && town;
        }
    }
};

Rect propRect(const SquareProp& p) { return Rect{p.cx, p.cz, p.w, p.d}; }
bool overlaps(const Rect& a, const Rect& b) {
    return a.x < b.x1() && b.x < a.x1() && a.z < b.z1() && b.z < a.z1();
}
bool inside(const Rect& inner, const Rect& outer) {
    return inner.x >= outer.x && inner.z >= outer.z && inner.x1() <= outer.x1() &&
           inner.z1() <= outer.z1();
}
Rect grow(const Rect& r, int by) { return Rect{r.x - by, r.z - by, r.w + 2 * by, r.d + 2 * by}; }

/// The square's through-street bands: streets (not the square itself) clipped to the square.
std::vector<Rect> throughBands(const MainStreetLayout& msl) {
    std::vector<Rect> bands;
    const Rect& sq = msl.marketSquare;
    for (const auto& s : msl.base.streets) {
        if (s.x == sq.x && s.z == sq.z && s.w == sq.w && s.d == sq.d) continue;
        const int x0 = std::max(s.x, sq.x), x1 = std::min(s.x1(), sq.x1());
        const int z0 = std::max(s.z, sq.z), z1 = std::min(s.z1(), sq.z1());
        if (x0 < x1 && z0 < z1) bands.push_back(Rect{x0, z0, x1 - x0, z1 - z0});
    }
    return bands;
}
}  // namespace

TEST(MarketDressingTest, CitySquareGetsStatueStallsAndRelocatedWell) {
    Fixture f;
    ASSERT_TRUE(f.ok) << "shipped settlement/room programs must load";
    ASSERT_TRUE(f.city->pub.statue) << "city preset declares a statue (data)";
    ASSERT_GT(f.city->pub.stalls, 0) << "city preset declares stalls (data)";

    const auto msl = planCityLayout(*f.city, 160, 160, f.rreg, 7);
    ASSERT_TRUE(msl.ok && msl.hasSquare);

    const auto dress = planSquareDressing(msl, f.city->pub, 7);
    ASSERT_TRUE(dress.ok) << "a square exists — the planner must dress it";

    int statues = 0, stalls = 0, wells = 0;
    for (const auto& p : dress.props) {
        if (p.type == "statue_hero") ++statues;
        else if (p.type == "market_stall") ++stalls;
        else if (p.type == "well") ++wells;
        EXPECT_TRUE(inside(propRect(p), msl.marketSquare))
            << p.type << " at (" << p.cx << "," << p.cz << ") escapes the square";
    }
    EXPECT_EQ(statues, 1);
    EXPECT_EQ(wells, 1) << "the tier well must survive the dressing";
    EXPECT_GE(stalls, 2) << "a city market should read as a market";
    EXPECT_LE(stalls, f.city->pub.stalls);

    // Statue at the square centre (the market-cross spot), within a cube of true centre.
    const auto& sq = msl.marketSquare;
    for (const auto& p : dress.props)
        if (p.type == "statue_hero") {
            EXPECT_LE(std::abs((p.cx + p.w / 2) - (sq.x + sq.w / 2)), 1);
            EXPECT_LE(std::abs((p.cz + p.d / 2) - (sq.z + sq.d / 2)), 1);
        }

    // Statue in the carriageway is legal ONLY with >= 2 cubes clear on each side of every
    // through band it overlaps; stalls and the well must stay OUT of the through bands.
    const auto bands = throughBands(msl);
    ASSERT_FALSE(bands.empty());
    for (const auto& p : dress.props) {
        const Rect pr = propRect(p);
        for (const auto& b : bands) {
            if (!overlaps(pr, b)) continue;
            EXPECT_EQ(p.type, "statue_hero")
                << p.type << " blocks a through street at (" << p.cx << "," << p.cz << ")";
            if (p.type == "statue_hero") {
                const bool alongX = b.w >= b.d;   // band runs along X -> lateral axis is Z
                const int lo = alongX ? pr.z - b.z : pr.x - b.x;
                const int hi = alongX ? b.z1() - pr.z1() : b.x1() - pr.x1();
                EXPECT_GE(lo, 2) << "statue pinches the street";
                EXPECT_GE(hi, 2) << "statue pinches the street";
            }
        }
    }

    // No prop-pair contact: >= 1 cube clearance between any two props.
    for (size_t i = 0; i < dress.props.size(); ++i)
        for (size_t j = i + 1; j < dress.props.size(); ++j)
            EXPECT_FALSE(overlaps(grow(propRect(dress.props[i]), 1), propRect(dress.props[j])))
                << dress.props[i].type << "#" << i << " crowds " << dress.props[j].type << "#" << j;

    // Perimeter clearance: nothing flush against the square boundary (doors front the square).
    for (const auto& p : dress.props)
        EXPECT_TRUE(inside(propRect(p), grow(sq, -1)))
            << p.type << " is flush against the square perimeter";
}

TEST(MarketDressingTest, TownKeepsCentreWellAndGainsStalls) {
    Fixture f;
    ASSERT_TRUE(f.ok);
    ASSERT_FALSE(f.town->pub.statue) << "town preset has no statue (data)";
    ASSERT_GT(f.town->pub.stalls, 0);

    const auto msl = planMainStreetLayout(*f.town, 120, 80, f.rreg, 11);
    ASSERT_TRUE(msl.ok && msl.hasSquare);

    const auto dress = planSquareDressing(msl, f.town->pub, 11);
    ASSERT_TRUE(dress.ok);

    // The well keeps the LEGACY centre anchor (bit-compatible with the pre-dressing town).
    const auto& sq = msl.marketSquare;
    bool wellAtCentre = false;
    int stalls = 0;
    for (const auto& p : dress.props) {
        if (p.type == "well" && p.cx == sq.x + sq.w / 2 && p.cz == sq.z + sq.d / 2)
            wellAtCentre = true;
        if (p.type == "market_stall") ++stalls;
        EXPECT_NE(p.type, "statue_hero");
    }
    EXPECT_TRUE(wellAtCentre) << "town well must keep its legacy centre anchor";
    EXPECT_GE(stalls, 1);
}

TEST(MarketDressingTest, DeterministicInSeedAndHonestWithoutSquare) {
    Fixture f;
    ASSERT_TRUE(f.ok);
    const auto msl = planCityLayout(*f.city, 160, 160, f.rreg, 7);
    ASSERT_TRUE(msl.ok);

    const auto a = planSquareDressing(msl, f.city->pub, 7);
    const auto b = planSquareDressing(msl, f.city->pub, 7);
    ASSERT_EQ(a.props.size(), b.props.size());
    for (size_t i = 0; i < a.props.size(); ++i) {
        EXPECT_EQ(a.props[i].type, b.props[i].type);
        EXPECT_EQ(a.props[i].cx, b.props[i].cx);
        EXPECT_EQ(a.props[i].cz, b.props[i].cz);
        EXPECT_EQ(a.props[i].rotDeg, b.props[i].rotDeg);
    }

    MainStreetLayout noSquare = msl;
    noSquare.hasSquare = false;
    const auto none = planSquareDressing(noSquare, f.city->pub, 7);
    EXPECT_FALSE(none.ok);
    EXPECT_TRUE(none.props.empty());
}

// L3: the dressed square must stay WALKABLE — from every through-street mouth the flood
// (4-neigh over square+streets minus prop cells) reaches every stall FRONT cell and the well.
TEST(MarketDressingTest, DressedSquareStaysWalkable) {
    Fixture f;
    ASSERT_TRUE(f.ok);
    const auto msl = planCityLayout(*f.city, 160, 160, f.rreg, 7);
    ASSERT_TRUE(msl.ok && msl.hasSquare);
    const auto dress = planSquareDressing(msl, f.city->pub, 7);
    ASSERT_TRUE(dress.ok);

    // Walkable set: all street cells (square included) minus dressed prop cells.
    std::set<std::pair<int, int>> walk;
    for (const auto& s : msl.base.streets)
        for (int x = s.x; x < s.x1(); ++x)
            for (int z = s.z; z < s.z1(); ++z) walk.insert({x, z});
    for (const auto& p : dress.props)
        for (int x = p.cx; x < p.cx + p.w; ++x)
            for (int z = p.cz; z < p.cz + p.d; ++z) walk.erase({x, z});

    // Flood from a main-street end.
    const Rect& ms = msl.mainStreet;
    std::pair<int, int> start{ms.x, ms.z + ms.d / 2};
    ASSERT_TRUE(walk.count(start));
    std::set<std::pair<int, int>> seen{start};
    std::queue<std::pair<int, int>> q;
    q.push(start);
    while (!q.empty()) {
        auto [x, z] = q.front();
        q.pop();
        for (auto [dx, dz] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
            std::pair<int, int> n{x + dx, z + dz};
            if (walk.count(n) && seen.insert(n).second) q.push(n);
        }
    }
    // Every prop must be REACHABLE: some cell of its 1-cube surround is flooded.
    for (const auto& p : dress.props) {
        bool reachable = false;
        const Rect g = grow(propRect(p), 1);
        for (int x = g.x; x < g.x1() && !reachable; ++x)
            for (int z = g.z; z < g.z1() && !reachable; ++z)
                if (seen.count({x, z})) reachable = true;
        EXPECT_TRUE(reachable) << p.type << " at (" << p.cx << "," << p.cz
                               << ") is walled off from the street network";
    }
}
