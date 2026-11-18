#include <climits>
#include <cstdio>
#include <functional>
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

// O35 (c) (pm_chain404.txt): a prop stands on LEVEL ground. The live town's well was planned across one level step of
// a stepped market with a second step beside it (top 1421 micro, the ground beside 1422). The planner had no heights.
namespace {
struct LevelCount { int props = 0, straddle = 0, sunk = 0; };
// straddle = a prop whose footprint cells are not at one height; sunk = a prop with a ring cell HIGHER than its base
LevelCount countLevel(const SquareDressing& d, const std::function<int(int, int)>& ground) {
    LevelCount c;
    for (const auto& p : d.props) {
        ++c.props;
        const int h = ground(p.cx, p.cz);
        bool str = false, snk = false;
        for (int z = p.cz - 1; z <= p.cz + p.d; ++z)
            for (int x = p.cx - 1; x <= p.cx + p.w; ++x) {
                const bool in = x >= p.cx && x < p.cx + p.w && z >= p.cz && z < p.cz + p.d;
                if (in && ground(x, z) != h) str = true;
                if (!in && ground(x, z) > h) snk = true;
            }
        if (str) ++c.straddle; else if (snk) ++c.sunk;
    }
    return c;
}
// a map-like square: side x side at (20, 20), one carriageway 3 wide through its middle along X
MainStreetLayout squareRig(int side) {
    MainStreetLayout m;
    m.hasSquare = true; m.ok = true;
    m.marketSquare = Rect{20, 20, side, side};
    m.squareBandsGiven = true;
    m.squareBands.push_back(Rect{20, 20 + side / 2 - 1, side, 3});
    return m;
}
int fdiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
// a ground in cubes: each cell at one height, tolerance 0
SquareGround cubes(const std::function<int(int, int)>& h) {
    SquareGround g; g.tolerance = 0;
    g.at = [h](int x, int z) { const int v = h(x, z); return std::make_pair(v, v); };
    return g;
}
}  // namespace

TEST(MarketDressingTest, APropStandsOnLevelGround) {
    Fixture f;
    ASSERT_TRUE(f.ok);
    PublicSpec pub = f.town->pub;
    ASSERT_TRUE(pub.well); ASSERT_GT(pub.stalls, 0);
    const auto msl = squareRig(12);
    const auto flat = [](int, int) { return 50; };
    const auto terraceZ4 = [](int, int z) { return 50 + fdiv(z, 4); };       // level terraces 4 deep, one cube apart
    const auto stairZ1 = [](int, int z) { return 50 + z; };                  // one cube a cell: no level 2 x 2 anywhere
    const auto nl = "\n";

    // CONTROL (the plan as shipped: no ground): on the terraces it stands props across a step or under a rise
    const auto blind = planSquareDressing(msl, pub, 11);
    const LevelCount cb = countLevel(blind, terraceZ4);
    std::printf("[ O35c ] no ground given, measured on terraces 4 deep: %d props, %d across a step, %d under a rise%s", cb.props, cb.straddle, cb.sunk, nl);
    EXPECT_GT(cb.straddle + cb.sunk, 0) << "the control does not show the defect: the rig measures nothing";

    // (1) a level square: the ground changes nothing
    const auto onFlat = planSquareDressing(msl, pub, 11, cubes(flat));
    ASSERT_EQ(onFlat.props.size(), blind.props.size());
    for (size_t i = 0; i < blind.props.size(); ++i) {
        EXPECT_EQ(onFlat.props[i].type, blind.props[i].type);
        EXPECT_EQ(onFlat.props[i].cx, blind.props[i].cx); EXPECT_EQ(onFlat.props[i].cz, blind.props[i].cz);
        EXPECT_EQ(onFlat.props[i].rotDeg, blind.props[i].rotDeg);
    }
    // (2) terraces: every prop level, the well kept
    const auto onT = planSquareDressing(msl, pub, 11, cubes(terraceZ4));
    const LevelCount ct = countLevel(onT, terraceZ4);
    int wells = 0, stalls = 0;
    for (const auto& p : onT.props) { if (p.type == "well") ++wells; if (p.type == "market_stall") ++stalls; }
    std::printf("[ O35c ] terraces 4 deep with the ground: %d props (%d well, %d stalls), %d across a step, %d under a rise, %zu dropped%s", ct.props, wells, stalls, ct.straddle, ct.sunk, onT.dropped.size(), nl);
    EXPECT_EQ(ct.straddle, 0); EXPECT_EQ(ct.sunk, 0);
    EXPECT_EQ(wells, 1) << "a level 2 x 2 with no rise beside it exists on terraces 4 deep: the well must find it";
    // (3) a stair of one cube a cell: nothing can stand level - said, not placed
    const auto onS = planSquareDressing(msl, pub, 11, cubes(stairZ1));
    const LevelCount cs = countLevel(onS, stairZ1);
    std::printf("[ O35c ] a stair of one cube a cell: %d props placed, %zu dropped%s", cs.props, onS.dropped.size(), nl);
    EXPECT_EQ(cs.props, 0) << "a prop was stood on a stair";
    EXPECT_EQ(static_cast<int>(onS.dropped.size()), 1 + pub.stalls) << "the well and every stall must be SAID to be dropped";

    // SCALE: square sides x terrace depths x both axes x seeds, town and city (statue) asks: 0 violations at every one
    int plans = 0, placed = 0, dropped = 0, bad = 0, noWell = 0;
    for (const SettlementTierPreset* t : {f.town, f.city})
        for (int side : {8, 12, 16, 24})
            for (int depth : {3, 4, 6, 9})
                for (int axis = 0; axis < 2; ++axis)
                    for (unsigned seed : {1u, 7u, 23u}) {
                        const auto m = squareRig(side);
                        const std::function<int(int, int)> g = [depth, axis](int x, int z) { return 50 + fdiv(axis ? x : z, depth); };
                        const auto d = planSquareDressing(m, t->pub, seed, cubes(g));
                        const LevelCount c = countLevel(d, g);
                        ++plans; placed += c.props; dropped += static_cast<int>(d.dropped.size()); bad += c.straddle + c.sunk;
                        bool w = false;
                        for (const auto& p : d.props) {
                            if (p.type == "well") w = true;
                            EXPECT_TRUE(inside(propRect(p), m.marketSquare));
                            for (const auto& b : m.squareBands) if (p.type != "statue_hero") EXPECT_FALSE(overlaps(propRect(p), b)) << p.type << " in the carriageway";
                            for (const auto& q : d.props) if (&p != &q) EXPECT_FALSE(overlaps(propRect(p), propRect(q)));
                        }
                        if (t->pub.well && !w) ++noWell;
                        EXPECT_EQ(c.straddle + c.sunk, 0) << t->name << " side " << side << " depth " << depth << " axis " << axis << " seed " << seed;
                    }
    std::printf("[ O35c ] scale: %d plans, %d props placed, %d dropped, %d not level, %d plans without the well asked for%s", plans, placed, dropped, bad, noWell, nl);
    EXPECT_EQ(bad, 0);
}

// O35 (c): the realizer's ground - MICRO, a cell's lowest and highest column, tolerance = the step-up (4). The plaza as
// the paver lays it: terraces `depth` cells deep, 9 micro apart, the last cell of each a ramp up to the next.
TEST(MarketDressingTest, APropStandsOnLevelGroundInMicro) {
    Fixture f;
    ASSERT_TRUE(f.ok);
    const auto nl = "\n";
    int plans = 0, placed = 0, dropped = 0, bad = 0, wells = 0, blindBad = 0, blindProps = 0;
    const auto notLevel = [](const SquareProp& p, const SquareGround& g) {
        int lo = INT_MAX, hi = INT_MIN, ring = INT_MIN;
        for (int z = p.cz - 1; z <= p.cz + p.d; ++z)
            for (int x = p.cx - 1; x <= p.cx + p.w; ++x) {
                const auto c = g.at(x, z);
                if (x >= p.cx && x < p.cx + p.w && z >= p.cz && z < p.cz + p.d) { lo = std::min(lo, c.first); hi = std::max(hi, c.second); }
                else ring = std::max(ring, c.second);
            }
        return !(hi - lo <= 4 && ring <= hi + 2);
    };
    for (const SettlementTierPreset* t : {f.town, f.city})
        for (int side : {8, 12, 16, 24})
            for (int depth : {3, 4, 6, 9})
                for (int axis = 0; axis < 2; ++axis)
                    for (unsigned seed : {1u, 7u, 23u}) {
                        const auto m = squareRig(side);
                        SquareGround g; g.tolerance = 4;
                        g.at = [depth, axis](int x, int z) {
                            const int u = axis ? x : z, t0 = fdiv(u, depth), r = u - t0 * depth, base = 1400 + 9 * t0;
                            return r == depth - 1 ? std::make_pair(base, base + 8) : std::make_pair(base, base);   // the ramp cell
                        };
                        const auto d = planSquareDressing(m, t->pub, seed, g);
                        for (const auto& p : planSquareDressing(m, t->pub, seed).props) { ++blindProps; if (notLevel(p, g)) ++blindBad; }   // the CONTROL: no ground
                        ++plans; placed += static_cast<int>(d.props.size()); dropped += static_cast<int>(d.dropped.size());
                        for (const auto& p : d.props) {
                            if (p.type == "well") ++wells;
                            int lo = INT_MAX, hi = INT_MIN, ring = INT_MIN;
                            for (int z = p.cz - 1; z <= p.cz + p.d; ++z)
                                for (int x = p.cx - 1; x <= p.cx + p.w; ++x) {
                                    const auto c = g.at(x, z);
                                    if (x >= p.cx && x < p.cx + p.w && z >= p.cz && z < p.cz + p.d) { lo = std::min(lo, c.first); hi = std::max(hi, c.second); }
                                    else ring = std::max(ring, c.second);
                                }
                            const bool ok = hi - lo <= 4 && ring <= hi + 2;
                            if (!ok) ++bad;
                            EXPECT_TRUE(ok) << p.type << " at " << p.cx << "," << p.cz << ": footprint " << lo << ".." << hi << ", ring up to " << ring
                                            << " (" << t->name << " side " << side << " depth " << depth << " axis " << axis << " seed " << seed << ")";
                        }
                    }
    std::printf("[ O35c ] micro scale: %d plans, %d props placed (%d wells), %d dropped, %d not level%s", plans, placed, wells, dropped, bad, nl);
    EXPECT_EQ(bad, 0);
    EXPECT_GT(placed, 0) << "nothing stood anywhere: the arm measures nothing";
    std::printf("[ O35c ] micro scale, the control (planned without the ground): %d props, %d not level%s", blindProps, blindBad, nl);
    EXPECT_GT(blindBad, 0) << "the control shows no defect: the arm measures nothing";
}
