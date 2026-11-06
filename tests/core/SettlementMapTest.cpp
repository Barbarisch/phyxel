#include <gtest/gtest.h>

#include "core/RoomProgram.h"
#include "core/SettlementMapPlanner.h"
#include "core/SettlementProgram.h"
#include "core/WorldForgePlan.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace Phyxel;
using namespace Phyxel::Core;

// ============================================================================
// WorldForge v2 M5a (docs/WorldForgeV2Plan.md §M5; the design check of 2026-11-05, READY): the
// settlement MAP's L2 invariants on the headless planner's real output, red first on the
// hamlet as built (planSettlementMapAsBuilt - the control), green on the interest-map growth.
// Rigs are in-process heightfields (the RouteFieldTest PondRig shape): one variable each, the
// prediction in docs/evidence/worldforge_v2/m5a_red_green.txt before the run, the control
// stated. The rig differs from the shipped defaults in its single synthetic field, a pinned
// site and a fixed seed; nothing here bounds a city's bake time (measured apart, reported).
// ============================================================================

namespace {

struct Rigs {
    SettlementProgramRegistry program;
    RoomProgramRegistry rooms;
    bool programOk = false, roomsOk = false;
    Rigs() {
        programOk = program.loadFromFile("resources/settlement_program.json");
        roomsOk = rooms.loadFromFile("resources/room_program.json");
    }
};
Rigs& rigs() { static Rigs r; return r; }

// a 5 % hillside rising along +X with a creek two cubes wide along the south edge (z 2..3)
int hillHeight(int x, int z) { (void)z; return 30 + static_cast<int>(std::floor(0.05f * static_cast<float>(x))); }
bool hillWater(int x, int z) { (void)x; return z >= 2 && z <= 3; }
// the flat control
int flatHeight(int, int) { return 30; }
// a 4-cube bench across the site at x 18..21 from z 8 northward (a steep band with a way round its south end)
int bandHeight(int x, int z) { return 30 + ((x >= 18 && x < 22 && z >= 8) ? 4 : 0); }

SettlementMapInput inputFor(const char* tier, int W, int D, std::function<int(int, int)> h, std::function<bool(int, int)> w, unsigned seed = 7) {
    SettlementMapInput in;
    in.tier = rigs().program.get("medieval", tier);
    in.rooms = rigs().roomsOk ? &rigs().rooms : nullptr;
    in.seed = seed; in.W = W; in.D = D;
    in.heightAt = std::move(h); in.waterAt = std::move(w);
    in.entries = {{0, D / 2, 1}};   // one road arriving at the west edge
    return in;
}
SettlementMapInput hamletHill(unsigned seed = 7) { return inputFor("hamlet", 40, 32, hillHeight, hillWater, seed); }

// the invariants, counted (each test asserts the ones it is about and prints the rest)
struct Violations { int lotNoWay = 0, lotDoorOffWay = 0, overlaps = 0, wayThroughFootprint = 0, lotOutside = 0, lotRelief = 0, badWay = 0, wayInWater = 0; };
Violations measure(const SettlementMap& m, const std::function<int(int, int)>& heightAt, int reliefCap, const std::function<bool(int, int)>& waterAt = {}) {
    Violations v;
    for (const auto& l : m.lots) {
        if (l.way < 0 || l.way >= static_cast<int>(m.ways.size())) { ++v.lotNoWay; }
        else {
            // the door-approach cell within 1.5 u of the way's POLYLINE (point to segment - chain 63 measured
            // node distance, which read every street-tier lot as off its two-node spine)
            const MapWay& w = m.ways[static_cast<size_t>(l.way)];
            const glm::ivec2 d = lotDoorApproach(l);
            float best = 1e9f;
            for (size_t k = 0; k + 1 < w.nodes.size(); ++k) {
                const MapNode* a = m.node(w.nodes[k]); const MapNode* b = m.node(w.nodes[k + 1]);
                if (!a || !b) continue;
                const glm::vec2 pa(static_cast<float>(a->x), static_cast<float>(a->z)), pb(static_cast<float>(b->x), static_cast<float>(b->z)), pd(static_cast<float>(d.x), static_cast<float>(d.y));
                const glm::vec2 ab = pb - pa;
                const float t = glm::dot(ab, ab) > 0.0f ? std::clamp(glm::dot(pd - pa, ab) / glm::dot(ab, ab), 0.0f, 1.0f) : 0.0f;
                best = std::min(best, glm::length(pd - (pa + ab * t)));
            }
            // the growth's way STARTS at the door-approach cell (exact: audit E2 - the polyline tolerance is for a
            // street-tier spine, whose rows abut the street's edge while the polyline is its centre line)
            const MapNode* first = w.nodes.empty() ? nullptr : m.node(w.nodes.front());
            const bool startsAtDoor = first && first->x == d.x && first->z == d.y;
            if (m.morphology == "cluster") { if (!startsAtDoor) ++v.lotDoorOffWay; }
            else if (!startsAtDoor && best > 1.5f + 0.5f * w.width) ++v.lotDoorOffWay;
        }
        if (l.rect.x < 0 || l.rect.z < 0 || l.rect.x1() > m.W || l.rect.z1() > m.D) ++v.lotOutside;
        int lo = INT_MAX, hi = INT_MIN;
        for (int z = std::max(0, l.rect.z); z < std::min(m.D, l.rect.z1()); ++z)
            for (int x = std::max(0, l.rect.x); x < std::min(m.W, l.rect.x1()); ++x) { const int y = heightAt(x, z); lo = std::min(lo, y); hi = std::max(hi, y); }
        if (lo <= hi && hi - lo > reliefCap) ++v.lotRelief;
    }
    for (size_t i = 0; i < m.lots.size(); ++i)
        for (size_t j = i + 1; j < m.lots.size(); ++j) {
            const Rect& a = m.lots[i].rect; const Rect& b = m.lots[j].rect;
            if (a.x < b.x1() && b.x < a.x1() && a.z < b.z1() && b.z < a.z1()) ++v.overlaps;
        }
    for (const auto& w : m.ways) {
        if (w.nodes.size() < 2 || w.width <= 0.0f) { ++v.badWay; continue; }
        for (size_t k = 0; k + 1 < w.nodes.size(); ++k) {
            const MapNode* a = m.node(w.nodes[k]); const MapNode* b = m.node(w.nodes[k + 1]);
            if (!a || !b) { ++v.badWay; continue; }
            const float len = std::hypot(static_cast<float>(b->x - a->x), static_cast<float>(b->z - a->z));
            const int steps = std::max(1, static_cast<int>(std::ceil(len * 2.0f)));
            bool through = false;
            for (int s = 0; s <= steps && !through; ++s) {
                const float t = static_cast<float>(s) / static_cast<float>(steps);
                const float x = static_cast<float>(a->x) + t * static_cast<float>(b->x - a->x), z = static_cast<float>(a->z) + t * static_cast<float>(b->z - a->z);
                for (const auto& l : m.lots) {
                    const Rect& f = l.footprint;
                    if (x >= static_cast<float>(f.x) && x < static_cast<float>(f.x1()) && z >= static_cast<float>(f.z) && z < static_cast<float>(f.z1())) { through = true; break; }
                }
            }
            if (through) ++v.wayThroughFootprint;
            if (waterAt) {   // a way never crosses water (M0): every cell of the segment, dry
                const int cells = std::max(1, std::max(std::abs(b->x - a->x), std::abs(b->z - a->z)));
                for (int c = 0; c <= cells; ++c) {
                    const int x = a->x + (b->x - a->x) * c / cells, z = a->z + (b->z - a->z) * c / cells;
                    if (waterAt(x, z)) { ++v.wayInWater; break; }
                }
            }
        }
    }
    return v;
}

void printMap(const char* tag, const SettlementMap& m, const Violations& v) {
    std::printf("[ M5a ] %s: %s/%s %zu lots, %zu ways, %zu nodes, entry %d, bake %.2f ms%s | no way %d, door off its way %d, overlaps %d, way through a footprint %d, outside %d, relief over cap %d, bad ways %d\n",
                tag, m.tier.c_str(), m.morphology.c_str(), m.lots.size(), m.ways.size(), m.nodes.size(), m.entryNode, m.bakeMs,
                m.ok() ? "" : (" REFUSED: " + m.refusal).c_str(), v.lotNoWay, v.lotDoorOffWay, v.overlaps, v.wayThroughFootprint, v.lotOutside, v.lotRelief, v.badWay);
    if (v.wayInWater) std::printf("[ M5a ] %s: %d way segments cross water\n", tag, v.wayInWater);
}

// the steepest cube step along a way's cells (the map's own grade) and along a straight door -> entry line
float worstStepAlongWays(const SettlementMap& m, const std::function<int(int, int)>& h) {
    float worst = 0.0f;
    for (const auto& w : m.ways)
        for (size_t k = 0; k + 1 < w.nodes.size(); ++k) {
            const MapNode* a = m.node(w.nodes[k]); const MapNode* b = m.node(w.nodes[k + 1]);
            if (!a || !b) continue;
            const int steps = std::max(1, std::max(std::abs(b->x - a->x), std::abs(b->z - a->z)));
            int prev = h(a->x, a->z);
            for (int s = 1; s <= steps; ++s) {
                const int x = a->x + (b->x - a->x) * s / steps, z = a->z + (b->z - a->z) * s / steps;
                const int y = h(std::clamp(x, 0, m.W - 1), std::clamp(z, 0, m.D - 1));
                worst = std::max(worst, static_cast<float>(std::abs(y - prev))); prev = y;
            }
        }
    return worst;
}
float worstStepAlongStraightLines(const SettlementMap& m, const std::function<int(int, int)>& h, glm::ivec2 entry) {
    float worst = 0.0f;
    for (const auto& l : m.lots) {
        const glm::ivec2 d = lotDoorApproach(l);
        const int steps = std::max(1, std::max(std::abs(entry.x - d.x), std::abs(entry.y - d.y)));
        int prev = h(std::clamp(d.x, 0, m.W - 1), std::clamp(d.y, 0, m.D - 1));
        for (int s = 1; s <= steps; ++s) {
            const int x = d.x + (entry.x - d.x) * s / steps, z = d.y + (entry.y - d.y) * s / steps;
            const int y = h(std::clamp(x, 0, m.W - 1), std::clamp(z, 0, m.D - 1));
            worst = std::max(worst, static_cast<float>(std::abs(y - prev))); prev = y;
        }
    }
    return worst;
}

}  // namespace

// Every lot fronts a way, and its door-approach cell lies on that way's first node. RED on the
// hamlet as built (the control): its plots are picked on flatness alone and the paths are an MST
// of the realized doors planned after the build - the map carries no way at all.
TEST(SettlementMapTest, EveryLotTouchesAWay) {
    ASSERT_TRUE(rigs().programOk) << "resources/settlement_program.json";
    const SettlementMapInput in = hamletHill();
    ASSERT_NE(in.tier, nullptr);
    const SettlementMap control = planSettlementMapAsBuilt(in);
    const Violations vc = measure(control, in.heightAt, in.maxRelief);
    printMap("CONTROL (as built)", control, vc);
    EXPECT_GT(vc.lotNoWay + vc.lotDoorOffWay, 0) << "the as-built hamlet has every lot on a way - the measure sees nothing";
    const SettlementMap m = planSettlementMap(in);
    const Violations v = measure(m, in.heightAt, in.maxRelief, in.waterAt);
    printMap("planner", m, v);
    ASSERT_TRUE(m.ok()) << m.refusal;
    EXPECT_GE(m.lots.size(), static_cast<size_t>(in.tier->buildingsMin)) << "fewer lots than the tier's minimum";
    EXPECT_EQ(v.wayInWater, 0) << "a way crosses the creek";
    std::printf("[ M5a ] hamlet hill: target %d, seated %zu (seated under the target is silent above the minimum - reported)\n", m.targetLots, m.lots.size());
    EXPECT_EQ(v.lotNoWay, 0) << "a lot fronts no way";
    EXPECT_EQ(v.lotDoorOffWay, 0) << "a lot's door-approach cell is not on its way";
    EXPECT_GE(m.entryNode, 0) << "no entry node: the network does not reach the arriving road";
}

// The structural invariants on every shipped tier, on the hill and on the flat control.
TEST(SettlementMapTest, MapInvariantsHoldOnEveryTier) {
    ASSERT_TRUE(rigs().programOk);
    struct Site { const char* tier; int W, D; };
    for (const Site s : {Site{"hamlet", 40, 32}, Site{"village", 80, 48}, Site{"town", 140, 60}, Site{"city", 160, 120}})
        for (const bool hill : {true, false}) {
            const SettlementMapInput in = inputFor(s.tier, s.W, s.D, hill ? std::function<int(int, int)>(hillHeight) : std::function<int(int, int)>(flatHeight),
                                                   hill ? std::function<bool(int, int)>(hillWater) : std::function<bool(int, int)>());
            ASSERT_NE(in.tier, nullptr) << s.tier;
            const SettlementMap m = planSettlementMap(in);
            const Violations v = measure(m, in.heightAt, in.maxRelief, in.waterAt);
            printMap(hill ? "hill" : "flat", m, v);
            // the water invariant is WaysStayOutOfWater's (red by measurement on the street tiers, chain 70)
            ASSERT_TRUE(m.ok()) << s.tier << ": " << m.refusal;
            EXPECT_GT(m.lots.size(), 0u) << s.tier;
            EXPECT_EQ(v.overlaps, 0) << s.tier << ": two lots overlap";
            EXPECT_EQ(v.wayThroughFootprint, 0) << s.tier << ": a way crosses a building footprint";
            EXPECT_EQ(v.lotOutside, 0) << s.tier << ": a lot outside the site";
            EXPECT_EQ(v.lotRelief, 0) << s.tier << ": a lot over the relief cap";
            EXPECT_EQ(v.badWay, 0) << s.tier << ": a way with fewer than two nodes or no width";
            EXPECT_LE(static_cast<int>(m.lots.size()), in.lotCap);
            // every tier (D41): a lot fronts a way and its door-approach cell lies on it (chain 64 read the
            // city's cross-street rows as off the main street they were all assigned to)
            EXPECT_EQ(v.lotNoWay, 0) << s.tier << ": a lot fronts no way";
            EXPECT_EQ(v.lotDoorOffWay, 0) << s.tier << ": a lot's door is off its way";
        }
}

// STATUS 2026-11-05 (chain 70, m5a_red_green.txt): RED BY MEASUREMENT on the street tiers, left for the owner / M5b. A way
// never crosses standing or channel water (M0: water is unbuildable; the growth's cost grid blocks it). The hamlet's grown ways
// cross none; the village / town / city maps are the main-street scenario AS BUILT (chooseStreetAxis + planMainStreetLayout /
// planCityLayout) whose spine and lanes run straight across the hill rig's creek (town 2 segments, city 3) - the layout drops
// PLOTS on water but not its streets. The pin as written stands so the finding stays visible; M5b grows the street tiers.
// RULED 2026-11-05 (owner): stays red, as written, until M5b grows the street tiers from the map.
TEST(SettlementMapTest, WaysStayOutOfWater) {
    ASSERT_TRUE(rigs().programOk);
    struct Site { const char* tier; int W, D; };
    for (const Site s : {Site{"hamlet", 40, 32}, Site{"village", 80, 48}, Site{"town", 140, 60}, Site{"city", 160, 120}}) {
        const SettlementMapInput in = inputFor(s.tier, s.W, s.D, hillHeight, hillWater);
        ASSERT_NE(in.tier, nullptr) << s.tier;
        const SettlementMap m = planSettlementMap(in);
        ASSERT_TRUE(m.ok()) << s.tier << ": " << m.refusal;
        const Violations v = measure(m, in.heightAt, in.maxRelief, in.waterAt);
        std::printf("[ M5a ] water: %s %zu ways, %d segments cross the creek\n", s.tier, m.ways.size(), v.wayInWater);
        EXPECT_EQ(v.wayInWater, 0) << s.tier << ": a way crosses water";
    }
}

// Determinism: the same inputs plan to the byte-identical record.
TEST(SettlementMapTest, MapIsIdenticalFromPlanTwice) {
    ASSERT_TRUE(rigs().programOk);
    for (const char* tier : {"hamlet", "town"}) {
        const SettlementMapInput in = inputFor(tier, tier[0] == 'h' ? 40 : 140, tier[0] == 'h' ? 32 : 60, hillHeight, hillWater);
        ASSERT_NE(in.tier, nullptr);
        SettlementMap a = planSettlementMap(in), b = planSettlementMap(in);
        a.bakeMs = b.bakeMs = 0.0f;   // the one diagnostic that may differ
        EXPECT_EQ(a.toJson().dump(), b.toJson().dump()) << tier;
        const SettlementMap c = SettlementMap::fromJson(a.toJson());
        EXPECT_EQ(c.toJson().dump(), a.toJson().dump()) << tier << ": the JSON round trip changed the record";
    }
}

// ORDER independence of the planner given its samplers (audit E3: this is not chunk independence - the
// same pure field is read through a direct sampler and through a tiled cache whose tiles are first touched
// in another order; it can only fail if the planner's result depended on the samplers' call history).
// The chunk-independence claim proper is the realizer's sampler (the generator's natural ground, a pure
// function of world position) and the L4 probe of the built hamlet's way cells while resident.
TEST(SettlementMapTest, MapIsOrderIndependentOfItsSamplers) {
    ASSERT_TRUE(rigs().programOk);
    struct Tiled {
        std::map<std::pair<int, int>, std::vector<int>> tiles;   // 8 x 8 tiles, filled on first touch
        int touches = 0;
        int at(int x, int z) {
            const auto key = std::make_pair(x >> 3, z >> 3);
            auto it = tiles.find(key);
            if (it == tiles.end()) {
                std::vector<int> t(64);
                for (int j = 0; j < 8; ++j) for (int i = 0; i < 8; ++i) t[static_cast<size_t>(j) * 8 + i] = hillHeight((key.first << 3) + i, (key.second << 3) + j);
                it = tiles.emplace(key, std::move(t)).first; ++touches;
            }
            return it->second[static_cast<size_t>(z & 7) * 8 + (x & 7)];
        }
    };
    auto tiled = std::make_shared<Tiled>();
    // touch the tiles in reverse order first, so the cache's history differs from the planner's scan
    for (int z = 31; z >= 0; z -= 8) for (int x = 39; x >= 0; x -= 8) tiled->at(x, z);
    SettlementMapInput a = hamletHill(), b = hamletHill();
    b.heightAt = [tiled](int x, int z) { return tiled->at(x, z); };
    SettlementMap ma = planSettlementMap(a), mb = planSettlementMap(b);
    ma.bakeMs = mb.bakeMs = 0.0f;
    std::printf("[ M5a ] two samplers: %zu lots / %zu ways each; the tiled cache touched %d tiles\n", ma.lots.size(), mb.ways.size(), tiled->touches);
    EXPECT_EQ(ma.toJson().dump(), mb.toJson().dump());
}

// Terrain following, the M5a form of the plan's CityAxesFollowTerrain (the street tiers' axes are
// M5b's): the hamlet's ways route round a 4-cube bench instead of climbing it. The CONTROL is the
// straight door -> entry line (what an MST of the doors walks): it crosses the bench. RED on the
// hamlet as built, which has no ways to measure.
TEST(SettlementMapTest, WaysAvoidTheSteepBand) {
    ASSERT_TRUE(rigs().programOk);
    SettlementMapInput in = inputFor("hamlet", 40, 32, bandHeight, std::function<bool(int, int)>());
    ASSERT_NE(in.tier, nullptr);
    in.entries = {{0, 16, 1}};
    const SettlementMap m = planSettlementMap(in);
    const Violations v = measure(m, in.heightAt, in.maxRelief);
    printMap("band", m, v);
    ASSERT_TRUE(m.ok()) << m.refusal;
    int east = 0;
    for (const auto& l : m.lots) if (l.rect.x >= 22) ++east;
    const float worstWay = worstStepAlongWays(m, in.heightAt), worstLine = worstStepAlongStraightLines(m, in.heightAt, {0, 16});
    std::printf("[ M5a ] band: %zu lots (%d east of the bench), %zu ways; steepest cube step along the ways %.0f, along the straight door -> entry lines %.0f (the control)\n",
                m.lots.size(), east, m.ways.size(), worstWay, worstLine);
    ASSERT_GT(m.ways.size(), 0u) << "no ways to measure";
    EXPECT_GT(east, 0) << "no lot east of the bench: the rig measures nothing (its one variable did not move)";
    EXPECT_GE(worstLine, 4.0f) << "the straight lines do not cross the bench: the control sees nothing";
    EXPECT_LE(worstWay, 1.0f) << "a way climbs the bench";
    // the PLANNER control (audit E2): with the slope aversion at 0 the same growth connects the same lots straight over the
    // bench - the discriminating assert must be able to fail on a planner output, not only on a drawn line
    struct Restore { ~Restore() { setPathSlopeAversionForTesting(-1.0f); } } restore;
    setPathSlopeAversionForTesting(0.0f);
    const SettlementMap flat = planSettlementMap(in);
    const float worstFlat = worstStepAlongWays(flat, in.heightAt);
    std::printf("[ M5a ] band CONTROL (slope aversion 0): %zu lots, %zu ways, steepest cube step along the ways %.0f\n", flat.lots.size(), flat.ways.size(), worstFlat);
    EXPECT_GE(worstFlat, 4.0f) << "the slope-blind planner routes round the bench too: the measure does not discriminate";
}

// L3 on the map before any voxel: every lot's door-approach cell reaches the entry on the
// rasterized stand-in (footprints solid, ground flat). The CONTROL seals the entry behind a
// footprint wall and must read blocked.
TEST(SettlementMapTest, DoorsReachTheEntryOnTheMapStandIn) {
    ASSERT_TRUE(rigs().programOk);
    const SettlementMapInput in = hamletHill();
    ASSERT_NE(in.tier, nullptr);
    SettlementMap m = planSettlementMap(in);
    ASSERT_TRUE(m.ok()) << m.refusal;
    AgentBox box;
    const WalkabilityReport rep = mapWalkabilityStandIn(m, box);
    std::printf("[ M5a ] stand-in: %d walkable, %d blocked of %zu lots (entry node %d)\n", rep.walkable, rep.blocked, m.lots.size(), m.entryNode);
    EXPECT_EQ(static_cast<size_t>(rep.walkable), m.lots.size()) << rep.summary();
    EXPECT_EQ(rep.blocked, 0) << rep.summary();
    // the control: a wall of footprint across the whole walkable bound (the stand-in's bound is one cube
    // wider than the site - chain 64's wall spanned only the site and the probe walked round its ends)
    SettlementMap sealed = m;
    MapLot wall; wall.id = 999; wall.rect = {1, -2, 1, m.D + 4}; wall.footprint = wall.rect; wall.typology = "wall"; wall.orientation = 1; wall.way = 0;
    sealed.lots.push_back(wall);
    const WalkabilityReport sealedRep = mapWalkabilityStandIn(sealed, box);
    std::printf("[ M5a ] stand-in CONTROL (sealed): %d walkable, %d blocked\n", sealedRep.walkable, sealedRep.blocked);
    EXPECT_GT(sealedRep.blocked, 0) << "the sealed control walks: the stand-in measures nothing";
}

// The bake cost, reported: the hamlet, and the growth pushed to the M5 stress cap on a large
// flat site (the Release figure per city is the L4 run's; this is Debug, a bound, not the gate).
TEST(SettlementMapTest, BakeTimeIsReportedAndTheGrowthReachesManyLots) {
    ASSERT_TRUE(rigs().programOk);
    const SettlementMapInput in = hamletHill();
    const SettlementMap m = planSettlementMap(in);
    std::printf("[ M5a ] hamlet bake %.2f ms (%zu lots)\n", m.bakeMs, m.lots.size());
    SettlementTierPreset big = *in.tier;
    big.buildingsMin = big.buildingsMax = 400;
    SettlementMapInput stress = inputFor("hamlet", 200, 160, flatHeight, std::function<bool(int, int)>());
    stress.tier = &big;
    const SettlementMap s = planSettlementMap(stress);
    std::printf("[ M5a ] stress (200 x 160 flat, target 400): %zu lots, %zu ways in %.1f ms%s\n", s.lots.size(), s.ways.size(), s.bakeMs, s.ok() ? "" : (" (" + s.refusal + ")").c_str());
    EXPECT_GE(s.lots.size(), 100u) << "the growth seats too few lots on an open site";
    EXPECT_LT(s.bakeMs, 20000.0f) << "the growth is too slow even for Debug";
}
