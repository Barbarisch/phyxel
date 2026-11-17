#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <fstream>

#include "core/RoomProgram.h"
#include "core/SettlementLayout.h"      // planSquareDressing (M5e-1)
#include "core/SettlementMapPlanner.h"
#include "core/SettlementProgram.h"
#include "core/StreetPaver.h"           // planWayPaving with plazas (M5e-1)
#include "core/TownWall.h"
#include "core/WorldForgePlan.h"

#include <algorithm>
#include <chrono>
#include <climits>
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
// M5b-1: two 4-cube benches, one open at the south end (x 18..21, z >= 8) and one open at the north end
// (x 44..47, z < D - 8): no straight east-west row at any z clears both, a routed way snakes round them
std::function<int(int, int)> twoBenchesHeight(int D) {
    return [D](int x, int z) { return 30 + (((x >= 18 && x < 22 && z >= 8) || (x >= 44 && x < 48 && z < D - 8)) ? 4 : 0); };
}
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
            // chain 91: a croft whose door cell lies ON an earlier path fronts that path (no one-node way of its own)
            if (m.morphology == "cluster") { if (!startsAtDoor && best > 0.5f) ++v.lotDoorOffWay; }
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

// A way never crosses standing or channel water (M0: water is unbuildable; the growth's cost grid blocks it). RED BY
// MEASUREMENT from chain 70 (2026-11-05) to chain 88 on the street tiers - the main-street scenario AS BUILT ran its spine
// and lanes straight across the hill rig's creek (town 2 segments, city 3); the owner ruled it stayed red as written until
// M5b grew the street tiers from the map. GREEN since chain 89 (2026-11-06, M5b-1): every tier's ways are routed on the
// cost grid; the as-built layouts remain as the CONTROL of CityAxesFollowTerrain.
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
    // O15 (register): the contract's scale is a 200-lot city under 2 s on RELEASE. Two figures are PRINTED for the
    // Release run to read (no time bound here - Debug is not the gate): the growth on a site large enough for 200
    // lots (280 x 200 flat, the same target 400), and the city tier as programmed on its 160 x 120 rig.
    SettlementMapInput stress2 = inputFor("hamlet", 280, 200, flatHeight, std::function<bool(int, int)>());
    stress2.tier = &big;
    const SettlementMap s2 = planSettlementMap(stress2);
    std::printf("[ O15 ] stress (280 x 200 flat, target 400): %zu lots, %zu ways in %.1f ms%s\n", s2.lots.size(), s2.ways.size(), s2.bakeMs, s2.ok() ? "" : (" (" + s2.refusal + ")").c_str());
    EXPECT_GE(s2.lots.size(), 200u) << "the 280 x 200 site does not reach the contract's 200 lots";
    const SettlementMapInput cityIn = inputFor("city", 160, 120, flatHeight, std::function<bool(int, int)>());
    const SettlementMap c = planSettlementMap(cityIn);
    std::printf("[ O15 ] city as programmed (160 x 120 flat): %zu lots, %zu ways in %.1f ms%s\n", c.lots.size(), c.ways.size(), c.bakeMs, c.ok() ? "" : (" (" + c.refusal + ")").c_str());
}

// ============================================================================
// M5b-1 (the M5b design check of 2026-11-06; predictions in pm_chain88.txt)
// ============================================================================

// CityAxesFollowTerrain, the street tiers' form of WaysAvoidTheSteepBand: on the two-bench rig the
// PRIMARY way of the village, town and city routes round the benches (steepest cube step <= 1) while the
// axis chooser's spine as built (planSettlementMapAsBuilt, the CONTROL) runs straight across one (>= 4).
// RED by measurement today: the planner's street-tier map IS the as-built spine.
TEST(SettlementMapTest, CityAxesFollowTerrain) {
    ASSERT_TRUE(rigs().programOk);
    struct Site { const char* tier; int W, D; };
    for (const Site s : {Site{"village", 80, 48}, Site{"town", 140, 60}, Site{"city", 160, 120}}) {
        SettlementMapInput in = inputFor(s.tier, s.W, s.D, twoBenchesHeight(s.D), std::function<bool(int, int)>());
        ASSERT_NE(in.tier, nullptr) << s.tier;
        const SettlementMap control = planSettlementMapAsBuilt(in);
        const SettlementMap m = planSettlementMap(in);
        ASSERT_TRUE(control.ok()) << s.tier << " control: " << control.refusal;
        ASSERT_TRUE(m.ok()) << s.tier << ": " << m.refusal;
        auto primaryWorst = [&](const SettlementMap& map) {
            SettlementMap only = map; only.ways.clear();
            for (const auto& w : map.ways) if (w.highway == "primary") only.ways.push_back(w);
            return std::make_pair(worstStepAlongWays(only, in.heightAt), only.ways.size());
        };
        const auto [wc, nc] = primaryWorst(control);
        const auto [wp, np] = primaryWorst(m);
        std::printf("[ M5b ] axes %s: CONTROL (as built) %zu primary ways, steepest step %.0f; planner %zu primary ways, steepest step %.0f (%zu lots, %zu ways)\n",
                    s.tier, nc, wc, np, wp, m.lots.size(), m.ways.size());
        ASSERT_GT(nc, 0u) << s.tier << ": the control has no primary way";
        // chain 88: the village's axis chooser cleared both benches with a north-south spine (its own relief
        // scoring finds the flat column) - the discriminating controls are the town's and the city's, both at 4
        if (s.tier != std::string("village")) EXPECT_GE(wc, 4.0f) << s.tier << ": the as-built spine clears both benches - the rig measures nothing";
        ASSERT_GT(np, 0u) << s.tier << ": the planner has no primary way";
        EXPECT_LE(wp, 1.0f) << s.tier << ": the primary way climbs a bench";
    }
}

// SiteRectsSeatTheBand (red test (e)): on the FLAT open field the literal site rects of the box plan
// (WorldForgePlan::tierPreset) seat the program's band for every seed - the hamlet's 40 x 32 (REASONED)
// is predicted to FAIL on the seeds whose target is 6 (the L4 sites seated 5 / 3 / 2). The search below
// is the derivation's measurement: the smallest rect at the preset's aspect that seats every seed's
// target; the derived preset (D43 (b)) is pinned against it. The street tiers' as-built rows carry no
// target (the growth sets one in M5b-1): reported, and asserted at the tier's minimum only.
TEST(SettlementMapTest, SiteRectsSeatTheBand) {
    ASSERT_TRUE(rigs().programOk);
    // chain 89: seeds 1..N reached only the band's lower targets (town 16..21 of 15-30, city 21..24 of 20-48) - the
    // seed band - 1 draws the band's MAXIMUM (min + seed % band), so every tier's top is in the set
    struct Site { const char* tier; std::vector<unsigned> seeds; };
    for (const Site s : {Site{"hamlet", {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20}}, Site{"village", {1, 2, 3, 4, 8}}, Site{"town", {1, 2, 3, 15}}, Site{"city", {1, 2, 28}}}) {
        const WorldForgePlan::TierPreset preset = WorldForgePlan::tierPreset(s.tier == std::string("city") ? "town" : s.tier);
        const int W0 = s.tier == std::string("city") ? 160 : preset.width, D0 = s.tier == std::string("city") ? 120 : preset.depth;
        const SettlementTierPreset* tier = rigs().program.get("medieval", s.tier);
        ASSERT_NE(tier, nullptr);
        const int nSeeds = static_cast<int>(s.seeds.size());
        auto seatsAll = [&](int W, int D, int& short_, int& minSeated) {
            short_ = 0; minSeated = INT_MAX;
            for (const unsigned seed : s.seeds) {
                const SettlementMapInput in = inputFor(s.tier, W, D, flatHeight, std::function<bool(int, int)>(), seed);
                const SettlementMap m = planSettlementMap(in);
                const int seated = static_cast<int>(m.lots.size());
                minSeated = std::min(minSeated, seated);
                const int want = m.targetLots > 0 ? m.targetLots : tier->buildingsMin;
                if (!m.ok() || seated < want) ++short_;
            }
            return short_ == 0;
        };
        int shortAtPreset = 0, minAtPreset = 0;
        const bool presetOk = seatsAll(W0, D0, shortAtPreset, minAtPreset);
        int W = W0, D = D0, sh = 0, mn = 0; bool found = presetOk;
        while (!found && W < 2 * W0) {   // grow at the preset's aspect, 4 cubes a step
            W += 4; D = std::max(4, (W * D0 / W0 + 3) / 4 * 4);
            found = seatsAll(W, D, sh, mn);
        }
        std::printf("[ M5b ] site rect %s: preset %d x %d seats the target in %d of %d seeds (min seated %d, band %d-%d); smallest rect at that aspect seating every seed: %s\n",
                    s.tier, W0, D0, nSeeds - shortAtPreset, nSeeds, minAtPreset, tier->buildingsMin, tier->buildingsMax,
                    found ? (std::to_string(W) + " x " + std::to_string(D)).c_str() : "none under 2x");
        // D43 (b), ruled 2026-11-06: the civ-seeded box plans take the DERIVED presets - at least the measured
        // smallest rect, and seating every seed's target themselves (chain 88: the literal 40 x 32 seated it on
        // 10 of 20, the smallest 48 x 40; the derived 56 x 44 carries a 1.25 area margin for the dropouts)
        ASSERT_TRUE(found) << s.tier << ": no rect under 2x the preset seats every seed";
        const WorldForgePlan::TierPreset derived = WorldForgePlan::tierPresetDerived(s.tier);
        int shortAtDerived = 0, minAtDerived = 0;
        const bool derivedOk = seatsAll(derived.width, derived.depth, shortAtDerived, minAtDerived);
        std::printf("[ M5b ] site rect %s: DERIVED %d x %d seats the target in %d of %d seeds (min seated %d)\n", s.tier, derived.width, derived.depth, nSeeds - shortAtDerived, nSeeds, minAtDerived);
        EXPECT_GE(derived.width * derived.depth, W * D) << s.tier << ": the derived preset is smaller than the measured smallest rect " << W << " x " << D;
        EXPECT_TRUE(derivedOk) << s.tier << ": the derived preset " << derived.width << " x " << derived.depth << " does not seat the band's target on " << shortAtDerived << " of " << nSeeds << " flat seeds";
    }
}

// The derived site rects on the HILL rig (the 5 % hillside with the creek): every tier seats at least its
// minimum and its ways stay dry - the flat measurement above plus the slope and water dropouts.
TEST(SettlementMapTest, DerivedSiteRectsSeatTheMinimumOnTheHill) {
    ASSERT_TRUE(rigs().programOk);
    for (const char* tierName : {"hamlet", "village", "town", "city"}) {
        const WorldForgePlan::TierPreset derived = WorldForgePlan::tierPresetDerived(tierName);
        for (unsigned seed = 1; seed <= 3; ++seed) {
            const SettlementMapInput in = inputFor(tierName, derived.width, derived.depth, hillHeight, hillWater, seed);
            ASSERT_NE(in.tier, nullptr) << tierName;
            const SettlementMap m = planSettlementMap(in);
            const Violations v = measure(m, in.heightAt, in.maxRelief, in.waterAt);
            std::printf("[ M5b ] hill %s %d x %d seed %u: %zu lots of target %d (min %d), %zu ways, %d wet segments, bake %.1f ms%s\n", tierName, derived.width, derived.depth, seed,
                        m.lots.size(), m.targetLots, in.tier->buildingsMin, m.ways.size(), v.wayInWater, m.bakeMs, m.ok() ? "" : (" REFUSED: " + m.refusal).c_str());
            printMap("hill (derived rect)", m, v);
            ASSERT_TRUE(m.ok()) << tierName << ": " << m.refusal;
            EXPECT_GE(static_cast<int>(m.lots.size()), in.tier->buildingsMin) << tierName;
            EXPECT_EQ(v.wayInWater, 0) << tierName << ": a way crosses the creek";
            EXPECT_EQ(v.overlaps + v.wayThroughFootprint + v.lotOutside + v.lotNoWay + v.lotDoorOffWay, 0) << tierName << ": an invariant broke on the hill";
        }
    }
}

// M5b-1a increment B (2026-11-13; predictions pm_chain149.txt). The lot's DOOR is map data: the wall cell the forge
// will cut it through and its outward normal, predicted by the forge's own deterministic autofill; and the lot pins
// its STYLE (D49). Measured on the serialized record (what the ledger stores and the API exports): every lot of
// every tier carries a door ON ITS FOOTPRINT'S PERIMETER whose normal is a unit axis pointing OUT of the footprint,
// and a style of the palette. RED as built (chain 149): no lot carries either. Whether the door is on the lot's
// FRONT side is printed, not asserted (the prediction falls back to any exterior door).
TEST(SettlementMapTest, EveryLotCarriesItsDoorAndStyle) {
    ASSERT_TRUE(rigs().programOk);
    ASSERT_TRUE(rigs().roomsOk) << "the room program did not load - no door can be predicted";
    const std::vector<std::string> palette = {"timber_cottage", "stone_manor", "stone_keep"};
    struct Site { const char* tier; int W, D; };
    for (const Site s : {Site{"hamlet", 40, 32}, Site{"village", 80, 48}, Site{"town", 140, 60}, Site{"city", 160, 120}})
        for (const bool hill : {true, false}) {
            SettlementMapInput in = inputFor(s.tier, s.W, s.D, hill ? std::function<int(int, int)>(hillHeight) : std::function<int(int, int)>(flatHeight),
                                             hill ? std::function<bool(int, int)>(hillWater) : std::function<bool(int, int)>());
            in.styles = palette; in.varietySeed = 1;
            in.originX = 1000; in.originZ = -2000;   // a world corner off the origin: the door's seed is the WORLD position
            const SettlementMap m = planSettlementMap(in);
            ASSERT_TRUE(m.ok()) << s.tier << ": " << m.refusal;
            const nlohmann::json j = m.toJson();
            const nlohmann::json lots = j.value("lots", nlohmann::json::array());
            ASSERT_GT(lots.size(), 0u);
            int noDoor = 0, offPerimeter = 0, badNormal = 0, noStyle = 0, offFront = 0;
            std::set<std::string> stylesSeen;
            for (const auto& l : lots) {
                const auto f = l["footprint"];
                const int fx = f[0].get<int>(), fz = f[1].get<int>(), fw = f[2].get<int>(), fd = f[3].get<int>();
                const std::string st = l.value("style", std::string());
                if (std::find(palette.begin(), palette.end(), st) == palette.end()) ++noStyle; else stylesSeen.insert(st);
                if (!l.contains("door") || !l.contains("door_normal")) { ++noDoor; continue; }
                const int dx = l["door"][0].get<int>(), dz = l["door"][1].get<int>();
                const int nx = l["door_normal"][0].get<int>(), nz = l["door_normal"][1].get<int>();
                const bool inside = dx >= fx && dx < fx + fw && dz >= fz && dz < fz + fd;
                const bool edge = dx == fx || dx == fx + fw - 1 || dz == fz || dz == fz + fd - 1;
                if (!inside || !edge) ++offPerimeter;
                const bool unit = (std::abs(nx) + std::abs(nz)) == 1;
                const int ox = dx + nx, oz = dz + nz;
                const bool outward = !(ox >= fx && ox < fx + fw && oz >= fz && oz < fz + fd);
                if (!unit || !outward) ++badNormal;
                static const int kN[4][2] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};   // orientation 0 = +Z, 1 = +X, 2 = -Z, 3 = -X
                const int o = ((l.value("orientation", 0) % 4) + 4) % 4;
                if (nx != kN[o][0] || nz != kN[o][1]) ++offFront;
            }
            std::printf("[ map door ] %s %s: %zu lots - %d without a door, %d off the perimeter, %d with a bad normal, %d not on the front side "
                        "(printed, not asserted), %d without a palette style (%zu styles seen)\n", s.tier, hill ? "hill" : "flat", lots.size(),
                        noDoor, offPerimeter, badNormal, offFront, noStyle, stylesSeen.size());
            EXPECT_EQ(noDoor, 0) << s.tier << ": " << noDoor << " of " << lots.size() << " lots carry no door";
            EXPECT_EQ(offPerimeter, 0) << s.tier << ": a door's wall cell is not on its footprint's perimeter";
            EXPECT_EQ(badNormal, 0) << s.tier << ": a door's normal is not a unit axis pointing out of the footprint";
            EXPECT_EQ(noStyle, 0) << s.tier << ": " << noStyle << " of " << lots.size() << " lots carry no style of the palette";
            // the record round-trips
            const SettlementMap back = SettlementMap::fromJson(j);
            EXPECT_EQ(back.toJson().value("lots", nlohmann::json::array()), lots) << s.tier << ": the lots do not round-trip through JSON";
        }
}

// M5b-1a increment C (2026-11-13; predictions pm_chain154.txt). A lot whose door meets a paved way is BUILT AT THE
// WAY'S LEVEL: the map states the way's standing level at the door, the pad cube and the threshold. On the record:
// threshold == pad_y * 9 + the style's offset, and threshold - way_level in [-1, 7] (padCubeForStreet's contract: the
// way never stands more than one micro above the threshold - the lintel - and the descent is the doorsteps').
// RED as built (chain 154): no lot carries a seat. The count of lots without one is printed, not asserted.
TEST(SettlementMapTest, EveryLotAtAPavedWayCarriesItsPad) {
    ASSERT_TRUE(rigs().programOk);
    ASSERT_TRUE(rigs().roomsOk);
    const std::map<std::string, int> offsets = {{"timber_cottage", 12}, {"stone_manor", 12}, {"stone_keep", 14}};   // chain 148's pin
    struct Site { const char* tier; int W, D; };
    for (const Site s : {Site{"hamlet", 40, 32}, Site{"village", 80, 48}, Site{"town", 140, 60}, Site{"city", 160, 120}})
        for (const bool hill : {true, false}) {
            SettlementMapInput in = inputFor(s.tier, s.W, s.D, hill ? std::function<int(int, int)>(hillHeight) : std::function<int(int, int)>(flatHeight),
                                             hill ? std::function<bool(int, int)>(hillWater) : std::function<bool(int, int)>());
            in.styles = {"timber_cottage", "stone_manor", "stone_keep"}; in.varietySeed = 1;
            in.thresholdAbovePad = offsets;
            in.originX = 1000; in.originZ = -2000;
            const SettlementMap m = planSettlementMap(in);
            ASSERT_TRUE(m.ok()) << s.tier << ": " << m.refusal;
            const nlohmann::json lots = m.toJson().value("lots", nlohmann::json::array());
            int seated = 0, badThreshold = 0, outOfBand = 0, lo = INT_MAX, hi = INT_MIN;
            for (const auto& l : lots) {
                if (!l.contains("pad_y")) continue;
                ++seated;
                const std::string st = l.value("style", std::string());
                const int off = offsets.count(st) ? offsets.at(st) : 12;
                const int T = l.value("threshold", INT_MIN), W = l.value("way_level", INT_MIN), pad = l.value("pad_y", INT_MIN);
                if (T != pad * 9 + off) ++badThreshold;
                if (T - W < -1 || T - W > 7) ++outOfBand;
                lo = std::min(lo, T - W); hi = std::max(hi, T - W);
            }
            std::printf("[ map seat ] %s %s: %zu lots, %d carry a seat (%zu do not - printed, not asserted); threshold - way level in [%d, %d]; "
                        "%d with a threshold that is not pad * 9 + the style's offset, %d outside [-1, 7]; bake %.1f ms\n", s.tier, hill ? "hill" : "flat",
                        lots.size(), seated, lots.size() - static_cast<size_t>(seated), seated ? lo : 0, seated ? hi : 0, badThreshold, outOfBand, m.bakeMs);
            EXPECT_GT(seated, 0) << s.tier << (hill ? " hill" : " flat") << ": no lot carries a seat (way level, pad, threshold)";
            EXPECT_EQ(badThreshold, 0) << s.tier;
            EXPECT_EQ(outOfBand, 0) << s.tier << ": a lot's threshold is not within [-1, 7] of its way";
        }
}

// M5b-1a increment D (predictions pm_chain179.txt). Every lot that carries a seat and has a paved way within 6 cells
// of its door carries its APPROACH: door + normal * k with the standing level of each cell. On the record: the cells
// are the door's, in order; from the threshold no cell stands above the previous level and none more than a step (4)
// below it, up to the last (the way's cell, whose riser is printed, not asserted: a way more than two steps under a
// door is left as found by planDoorstepLevels and reported).
TEST(SettlementMapTest, EveryLotCarriesItsDoorPadAndApproach) {
    ASSERT_TRUE(rigs().programOk);
    ASSERT_TRUE(rigs().roomsOk);
    struct Site { const char* tier; int W, D; };
    for (const Site s : {Site{"hamlet", 40, 32}, Site{"village", 80, 48}, Site{"town", 140, 60}, Site{"city", 160, 120}})
        for (const bool hill : {true, false}) {
            SettlementMapInput in = inputFor(s.tier, s.W, s.D, hill ? std::function<int(int, int)>(hillHeight) : std::function<int(int, int)>(flatHeight),
                                             hill ? std::function<bool(int, int)>(hillWater) : std::function<bool(int, int)>());
            in.styles = {"timber_cottage", "stone_manor", "stone_keep"}; in.varietySeed = 1;
            in.thresholdAbovePad = {{"timber_cottage", 12}, {"stone_manor", 12}, {"stone_keep", 14}};
            in.originX = 1000; in.originZ = -2000;
            const SettlementMap m = planSettlementMap(in);
            ASSERT_TRUE(m.ok()) << s.tier << ": " << m.refusal;
            int seated = 0, with = 0, wrongCells = 0, badRiser = 0, wayRiserOut = 0, cells = 0, longest = 0;
            for (const auto& l : m.lots) {
                if (!l.hasSeat) continue;
                ++seated;
                if (!l.hasApproach) continue;
                ++with;
                longest = std::max(longest, static_cast<int>(l.approach.size()));
                int level = l.threshold;
                for (size_t i = 0; i < l.approach.size(); ++i) {
                    const auto& a = l.approach[i];
                    ++cells;
                    if (a.x != l.doorX + l.doorNX * static_cast<int>(i + 1) || a.z != l.doorZ + l.doorNZ * static_cast<int>(i + 1)) ++wrongCells;
                    const int riser = level - a.level;   // > 0 = a step down, outward
                    if (i + 1 < l.approach.size()) { if (riser < 0 || riser > 4) ++badRiser; }
                    else if (riser < -1 || riser > 4) ++wayRiserOut;
                    level = a.level;
                }
            }
            std::printf("[ map approach ] %s %s: %zu lots, %d seated, %d carry an approach (%d cells, the longest %d); %d cells not on the door's line, "
                        "%d unpaved risers outside [0, 4], %d risers onto the way outside [-1, 4] (printed)\n", s.tier, hill ? "hill" : "flat", m.lots.size(),
                        seated, with, cells, longest, wrongCells, badRiser, wayRiserOut);
            EXPECT_GT(with, 0) << s.tier << (hill ? " hill" : " flat") << ": no seated lot carries an approach";
            EXPECT_EQ(wrongCells, 0) << s.tier;
            EXPECT_EQ(badRiser, 0) << s.tier << ": an approach cell stands above the previous level or more than a step below it";
        }
}

// M5b-1a increment E (sequencing amendment 3): the SLOPED headless rig. The four shapes only the 10-17 minute live-site
// arms caught, counted on the MAP (micro levels; a lot's ground = the natural standing level, (height + 1) * 9):
//   cut      - the way stands a cube or more BELOW the lot's ground at its door wall cell
//   fill     - the way stands a cube or more ABOVE it
//   climb    - the natural ground along the lot's frontage (the row of cells just outside its door wall) ranges over
//              2 cubes or more (the plan's ">= 1.5 cubes", in whole cubes; the GROUND, not the way's paving - the map
//              does not carry the paving's surface along a frontage)
//   setback0 - the first cell outside the door is the way (an approach of one cell)
struct SlopedShapes { int lots = 0, seated = 0, with = 0, cut = 0, fill = 0, climb = 0, setback0 = 0, badRiser = 0, wayRiserOut = 0, wrongThreshold = 0, noDoor = 0; };
SlopedShapes slopedShapes(const SettlementMap& m, const SettlementMapInput& in) {
    SlopedShapes r;
    r.lots = static_cast<int>(m.lots.size());
    for (const auto& l : m.lots) {
        if (!l.hasDoor) { ++r.noDoor; continue; }
        if (!l.hasSeat) continue;
        ++r.seated;
        const int doorGround = (in.heightAt(l.doorX, l.doorZ) + 1) * 9;
        if (l.wayLevel <= doorGround - 9) ++r.cut;
        if (l.wayLevel >= doorGround + 9) ++r.fill;
        int lo = INT_MAX, hi = INT_MIN;
        if (l.doorNZ != 0) {
            const int z = l.doorNZ > 0 ? l.footprint.z1() : l.footprint.z - 1;
            for (int x = l.footprint.x; x < l.footprint.x1(); ++x) { const int h = in.heightAt(x, z); lo = std::min(lo, h); hi = std::max(hi, h); }
        } else {
            const int x = l.doorNX > 0 ? l.footprint.x1() : l.footprint.x - 1;
            for (int z = l.footprint.z; z < l.footprint.z1(); ++z) { const int h = in.heightAt(x, z); lo = std::min(lo, h); hi = std::max(hi, h); }
        }
        if (hi - lo >= 2) ++r.climb;
        const auto oi = in.thresholdAbovePad.find(l.style);
        if (l.threshold != l.padY * 9 + (oi != in.thresholdAbovePad.end() ? oi->second : 12)) ++r.wrongThreshold;
        if (!l.hasApproach) continue;
        ++r.with;
        if (l.approach.size() == 1) ++r.setback0;
        int level = l.threshold;
        for (size_t i = 0; i < l.approach.size(); ++i) {
            const int riser = level - l.approach[i].level;
            if (i + 1 < l.approach.size()) { if (riser < 0 || riser > 4) ++r.badRiser; }
            else if (riser < -1 || riser > 4) ++r.wayRiserOut;
            level = l.approach[i].level;
        }
    }
    return r;
}
// The rig's field (cubes; base 30): a 12 % rise along +X with a 2-cube roll across it and a 1.5-cube ripple along it.
// Picked by chain 188's scan of five fields x two tiers x three seeds (m3c_chains/pm_chain179.txt): the only field on
// which every tier / seed tried shows a cut AND a fill; a plain plane (15 % along X, 20 % along Z, 10 % + 10 %) showed
// fills and no cut on any of 18 maps.
int slopeRolling(int x, int z) { return 30 + static_cast<int>(std::floor(0.12f * static_cast<float>(x) + 2.0f * std::sin(static_cast<float>(z) / 6.0f) + 1.5f * std::sin(static_cast<float>(x) / 5.0f))); }

// A PIN, not red-before-green (stated): the rig is what it claims - each of the four shapes occurs on it and none on
// the flat control - and on it every seated lot carries its threshold (pad * 9 + the style's offset) and an approach
// whose unpaved risers stay in [0, 4]. MAP level only: nothing is realized here (the realizer on a synthetic sloped
// field is NOT built - the live-site arms remain the only place the four shapes meet voxels).
TEST(SettlementMapTest, TheSlopedRigCarriesTheFourShapes) {
    ASSERT_TRUE(rigs().programOk);
    ASSERT_TRUE(rigs().roomsOk);
    struct Site { const char* tier; int W, D; unsigned seed; };
    for (const Site st : {Site{"town", 140, 60, 23u}, Site{"village", 80, 48, 7u}})
        for (const bool sloped : {true, false}) {
            SettlementMapInput in = inputFor(st.tier, st.W, st.D, sloped ? std::function<int(int, int)>(slopeRolling) : std::function<int(int, int)>(flatHeight), {}, st.seed);
            in.styles = {"timber_cottage", "stone_manor", "stone_keep"}; in.varietySeed = 1;
            in.thresholdAbovePad = {{"timber_cottage", 12}, {"stone_manor", 12}, {"stone_keep", 14}};
            in.originX = 1000; in.originZ = -2000;
            const SettlementMap m = planSettlementMap(in);
            ASSERT_TRUE(m.ok()) << st.tier << ": " << m.refusal;
            const SlopedShapes r = slopedShapes(m, in);
            std::printf("[ sloped rig ] %s %s seed %u: %d lots (%d dropped, %d without a door), %d seated, %d with an approach | cut %d fill %d climb %d setback0 %d | "
                        "unpaved risers out %d, way risers out %d (printed), wrong thresholds %d, bake %.1f ms\n", st.tier, sloped ? "sloped" : "flat", st.seed, r.lots,
                        m.droppedLots, r.noDoor, r.seated, r.with, r.cut, r.fill, r.climb, r.setback0, r.badRiser, r.wayRiserOut, r.wrongThreshold, m.bakeMs);
            EXPECT_GT(r.seated, 0) << st.tier;
            EXPECT_EQ(r.with, r.seated) << st.tier << ": a seated lot carries no approach";
            EXPECT_EQ(r.badRiser, 0) << st.tier;
            EXPECT_EQ(r.wrongThreshold, 0) << st.tier;
            if (sloped) {
                EXPECT_GT(r.cut, 0) << st.tier << ": no way a cube or more below a lot's ground at its door";
                EXPECT_GT(r.fill, 0) << st.tier << ": no way a cube or more above one";
                EXPECT_GT(r.climb, 0) << st.tier << ": no frontage whose ground ranges over 2 cubes";
                EXPECT_GT(r.setback0, 0) << st.tier << ": no lot whose door opens straight onto the way";
            } else {   // the control: the measures read nothing where there is no relief
                EXPECT_EQ(r.cut + r.fill + r.climb, 0) << st.tier << ": a shape counted on the flat field";
            }
        }
}

// ============================================================================
// M5b-2 (the design re-check of 2026-11-14; D44 a; D50-D52 PROPOSED): the PARCEL as a cell mask inside the lot's
// rect. Headless, map level only - nothing here places a voxel. Rigs: the flat field and the 5 % hill (controls:
// mask == rect), two 20 % planes (the slope along X, along Z), increment E's rolling field. The predictions are in
// docs/evidence/worldforge_v2/m3c_chains/pm_chain195.txt before each run.
// ============================================================================
namespace {
int plane20x(int x, int z) { (void)z; return 30 + static_cast<int>(std::floor(0.20f * static_cast<float>(x))); }
int plane20z(int x, int z) { (void)x; return 30 + static_cast<int>(std::floor(0.20f * static_cast<float>(z))); }

// the front block: the frontage edge through the footprint's back row, the rect's full width (D50)
bool inFrontBlock(const MapLot& l, int x, int z) {
    const Rect& r = l.rect; const Rect& f = l.footprint;
    if (x < r.x || x >= r.x1() || z < r.z || z >= r.z1()) return false;
    switch (l.orientation) {
        case 0: return z >= f.z;
        case 2: return z < f.z1();
        case 1: return x >= f.x;
        default: return x < f.x1();
    }
}
int backYardDepth(const MapLot& l) {
    const Rect& r = l.rect; const Rect& f = l.footprint;
    switch (l.orientation) { case 0: return f.z - r.z; case 2: return r.z1() - f.z1(); case 1: return f.x - r.x; default: return r.x1() - f.x1(); }
}

struct ParcelStats {
    int lots = 0, written = 0, equalRect = 0, lostCells = 0, lostAlongX = 0, lostAlongZ = 0, lotsAlongX = 0, lotsAlongZ = 0, yardAlongX = 0, yardAlongZ = 0;
    int outside = 0, frontMissing = 0, disconnected = 0, holes = 0, overlapCells = 0, wetCells = 0, centrelineCells = 0, maxRects = 0;
    int reliefRects = 0, reliefParcels = 0;
};
ParcelStats parcelStats(const SettlementMap& m, const SettlementMapInput& in) {
    ParcelStats st;
    std::map<std::pair<int, int>, int> owner;
    for (const auto& l : m.lots) {
        ++st.lots;
        if (!l.parcel.empty()) ++st.written;
        st.maxRects = std::max(st.maxRects, static_cast<int>(l.parcel.size()));
        const Rect& r = l.rect;
        const bool depthAlongX = l.orientation == 1 || l.orientation == 3;
        (depthAlongX ? st.lotsAlongX : st.lotsAlongZ) += 1;
        (depthAlongX ? st.yardAlongX : st.yardAlongZ) += backYardDepth(l) * (depthAlongX ? r.d : r.w);
        for (const auto& pr : l.parcel)
            if (pr.x < r.x || pr.z < r.z || pr.x1() > r.x1() || pr.z1() > r.z1() || pr.w <= 0 || pr.d <= 0) ++st.outside;
        int lost = 0, cells = 0, lo = INT_MAX, hi = INT_MIN, plo = INT_MAX, phi = INT_MIN;
        std::vector<glm::ivec2> mine;
        for (int z = r.z; z < r.z1(); ++z)
            for (int x = r.x; x < r.x1(); ++x) {
                const int h = in.heightAt(x, z);
                lo = std::min(lo, h); hi = std::max(hi, h);
                if (!l.inParcel(x, z)) { ++lost; if (inFrontBlock(l, x, z)) ++st.frontMissing; continue; }
                ++cells; plo = std::min(plo, h); phi = std::max(phi, h);
                mine.push_back({x, z});
                if (!owner.emplace(std::make_pair(x, z), l.id).second) ++st.overlapCells;
                if (in.waterAt && in.waterAt(x, z)) ++st.wetCells;
            }
        st.reliefRects += hi - lo;
        if (cells > 0) st.reliefParcels += phi - plo;
        st.lostCells += lost;
        (depthAlongX ? st.lostAlongX : st.lostAlongZ) += lost;
        if (lost == 0) ++st.equalRect;
        // D51: a depth profile - along the depth axis a column, once out of the parcel behind the front block, stays out
        const int n = depthAlongX ? r.d : r.w, depth = depthAlongX ? r.w : r.d;
        for (int i = 0; i < n; ++i) {
            bool out = false;
            for (int k = 0; k < depth; ++k) {
                int x, z;
                switch (l.orientation) {
                    case 0: x = r.x + i; z = r.z1() - 1 - k; break;
                    case 2: x = r.x + i; z = r.z + k; break;
                    case 1: x = r.x1() - 1 - k; z = r.z + i; break;
                    default: x = r.x + k; z = r.z + i; break;
                }
                const bool inside = l.inParcel(x, z);
                if (!inside) out = true; else if (out) ++st.holes;
            }
        }
        // 4-connected
        if (!mine.empty()) {
            std::set<std::pair<int, int>> todo;
            for (const auto& c : mine) todo.insert({c.x, c.y});
            std::vector<std::pair<int, int>> stack{{mine[0].x, mine[0].y}};
            todo.erase(stack[0]);
            while (!stack.empty()) {
                const auto c = stack.back(); stack.pop_back();
                for (const auto& d : {std::pair<int, int>{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
                    const auto it = todo.find({c.first + d.first, c.second + d.second});
                    if (it != todo.end()) { stack.push_back(*it); todo.erase(it); }
                }
            }
            if (!todo.empty()) ++st.disconnected;
        }
    }
    // a way's centre line inside a parcel (printed: the growth lets a way that was there first stay inside a later rect)
    for (const auto& w : m.ways)
        for (size_t k = 0; k + 1 < w.nodes.size(); ++k) {
            const MapNode* a = m.node(w.nodes[k]); const MapNode* b = m.node(w.nodes[k + 1]);
            if (!a || !b) continue;
            const int cells = std::max(1, std::max(std::abs(b->x - a->x), std::abs(b->z - a->z)));
            for (int c = 0; c <= cells; ++c)
                if (owner.count({a->x + (b->x - a->x) * c / cells, a->z + (b->z - a->z) * c / cells})) ++st.centrelineCells;
        }
    return st;
}
void printParcels(const char* rig, const char* tier, const ParcelStats& p) {
    std::printf("[ M5b-2 ] %-10s %-8s %2d lots, %2d with a parcel written, %2d equal to the rect | lost %d cells (depth along X: %d of %d yard cells over %d lots; along Z: %d of %d over %d) | "
                "outside %d, front block missing %d, disconnected %d, holes %d, overlap %d, wet %d, way centre-line cells inside %d (printed), max rects %d | relief sum rects %d parcels %d\n",
                rig, tier, p.lots, p.written, p.equalRect, p.lostCells, p.lostAlongX, p.yardAlongX, p.lotsAlongX, p.lostAlongZ, p.yardAlongZ, p.lotsAlongZ,
                p.outside, p.frontMissing, p.disconnected, p.holes, p.overlapCells, p.wetCells, p.centrelineCells, p.maxRects, p.reliefRects, p.reliefParcels);
}
struct ParcelSite { const char* tier; int W, D; };
const ParcelSite kParcelSites[] = {{"hamlet", 40, 32}, {"village", 80, 48}, {"town", 140, 60}, {"city", 160, 120}};
}  // namespace

// (h) THE CONTROL: on the flat field and on the 5 % hill every parcel is its rect, exactly (D52: 19 steps at 5 % cost
// 0.22 of a cell, under the half cell of slack) - and it is WRITTEN (the conquest ran).
TEST(SettlementMapTest, ParcelsEqualTheToftOnTheFlat) {
    ASSERT_TRUE(rigs().programOk);
    for (const ParcelSite s : kParcelSites)
        for (const bool hill : {false, true}) {
            const SettlementMapInput in = inputFor(s.tier, s.W, s.D, hill ? std::function<int(int, int)>(hillHeight) : std::function<int(int, int)>(flatHeight),
                                                   hill ? std::function<bool(int, int)>(hillWater) : std::function<bool(int, int)>());
            const SettlementMap m = planSettlementMap(in);
            ASSERT_TRUE(m.ok()) << s.tier << ": " << m.refusal;
            const ParcelStats p = parcelStats(m, in);
            printParcels(hill ? "hill 5 %" : "flat", s.tier, p);
            EXPECT_GT(p.lots, 0) << s.tier;
            EXPECT_EQ(p.written, p.lots) << s.tier << ": a lot carries no parcel";
            EXPECT_EQ(p.equalRect, p.lots) << s.tier << ": a parcel differs from its rect on " << (hill ? "the 5 % hill" : "the flat");
        }
}

// (i) On a 20 % plane a back yard that runs ALONG the slope is shortened and one that runs ACROSS it is not; the
// planner control (lambda_s = 0) gives the rect on the same field - the measure discriminates on a planner output.
TEST(SettlementMapTest, ParcelsShrinkWithTheSlope) {
    ASSERT_TRUE(rigs().programOk);
    int lostAlong = 0, lostAcross = 0, lostControl = 0, lotsAlong = 0, reliefRects = 0, reliefParcels = 0;
    for (const ParcelSite s : {kParcelSites[0], kParcelSites[1], kParcelSites[2]})
        for (const bool alongX : {true, false})
            for (const float lambda : {4.7f, 0.0f}) {
                SettlementMapInput in = inputFor(s.tier, s.W, s.D, alongX ? std::function<int(int, int)>(plane20x) : std::function<int(int, int)>(plane20z), {});
                in.parcelSlopeWeight = lambda;
                const SettlementMap m = planSettlementMap(in);
                ASSERT_TRUE(m.ok()) << s.tier << ": " << m.refusal;
                const ParcelStats p = parcelStats(m, in);
                printParcels(lambda > 0.0f ? (alongX ? "20 % X" : "20 % Z") : (alongX ? "20 % X l=0" : "20 % Z l=0"), s.tier, p);
                EXPECT_EQ(p.written, p.lots) << s.tier;
                if (lambda > 0.0f) {
                    lostAlong += alongX ? p.lostAlongX : p.lostAlongZ; lostAcross += alongX ? p.lostAlongZ : p.lostAlongX;
                    lotsAlong += alongX ? p.lotsAlongX : p.lotsAlongZ;
                    reliefRects += p.reliefRects; reliefParcels += p.reliefParcels;
                } else lostControl += p.lostCells;
            }
    std::printf("[ M5b-2 ] 20 %% planes: lost along the slope %d cells over %d lots, across it %d, with lambda_s = 0 %d; relief summed over the rects %d, over the parcels %d\n",
                lostAlong, lotsAlong, lostAcross, lostControl, reliefRects, reliefParcels);
    EXPECT_GT(lotsAlong, 0) << "the rig seats no lot whose depth runs along the slope";
    EXPECT_GT(lostAlong, 0) << "no back yard along a 20 % slope lost a cell";
    EXPECT_EQ(lostAcross, 0) << "a back yard across the slope lost cells";
    EXPECT_EQ(lostControl, 0) << "the lambda_s = 0 control differs from the rect";
    // the audit's F2: the relief sums are PRINTED only - a parcel is a subset of its rect, so "<" asked nothing the lost
    // cells do not already say (157 against 155 on chain 199)
}

// (j) + (l): on every rig a parcel is inside its rect, holds its front block (footprint + setback strip: D50), is
// 4-connected, a depth profile with no hole (D51), shares no cell with another and holds no water cell.
TEST(SettlementMapTest, EveryParcelHoldsItsFootprintAndIsDisjointAndDry) {
    ASSERT_TRUE(rigs().programOk);
    struct Field { const char* name; std::function<int(int, int)> h; std::function<bool(int, int)> w; };
    const Field fields[] = {{"hill 5 %", hillHeight, hillWater}, {"20 % X", plane20x, {}}, {"20 % Z", plane20z, {}}, {"rolling", slopeRolling, {}}};
    int total = 0;
    for (const auto& f : fields)
        for (const ParcelSite s : kParcelSites) {
            const SettlementMapInput in = inputFor(s.tier, s.W, s.D, f.h, f.w);
            const SettlementMap m = planSettlementMap(in);
            if (!m.ok()) { std::printf("[ M5b-2 ] %-10s %-8s REFUSED: %s\n", f.name, s.tier, m.refusal.c_str()); continue; }
            const ParcelStats p = parcelStats(m, in);
            printParcels(f.name, s.tier, p);
            total += p.lots;
            EXPECT_EQ(p.written, p.lots) << f.name << " " << s.tier;
            EXPECT_EQ(p.outside, 0) << f.name << " " << s.tier << ": a parcel rect outside the lot's rect";
            EXPECT_EQ(p.frontMissing, 0) << f.name << " " << s.tier << ": a front-block cell outside the parcel";
            EXPECT_EQ(p.disconnected, 0) << f.name << " " << s.tier;
            EXPECT_EQ(p.holes, 0) << f.name << " " << s.tier << ": a parcel cell behind a gap in its column";
            EXPECT_EQ(p.maxRects, 1) << f.name << " " << s.tier << ": a parcel that is not one rect (D51 as revised: the fence's four planes)";
            // the audit's F3 / F4: the same properties from sources the conquest does not compute - the FOOTPRINT rect,
            // the DOOR's wall cell and normal (the forge's autofill), the water sampler over the RECT
            int footOutside = 0, doorSideMoved = 0, approachOutside = 0, wetRect = 0, doors = 0;
            for (const auto& l : m.lots) {
                const Rect pr = l.parcelRect();
                int out = 0;
                for (int z = l.footprint.z; z < l.footprint.z1(); ++z) for (int x = l.footprint.x; x < l.footprint.x1(); ++x)
                    if (x < pr.x || x >= pr.x1() || z < pr.z || z >= pr.z1()) ++out;
                footOutside += out;
                if (out > 0)   // chain 206: 39 cells on the city, on every field
                    std::printf("[ M5b-2 ]     lot %d %s orientation %d setback %d: toft [%d,%d,%d,%d] footprint [%d,%d,%d,%d] parcel [%d,%d,%d,%d] - %d footprint cells outside the parcel\n",
                                l.id, l.typology.c_str(), l.orientation, l.setback, l.rect.x, l.rect.z, l.rect.w, l.rect.d, l.footprint.x, l.footprint.z, l.footprint.w, l.footprint.d,
                                pr.x, pr.z, pr.w, pr.d, out);
                for (int z = l.rect.z; z < l.rect.z1(); ++z) for (int x = l.rect.x; x < l.rect.x1(); ++x) if (in.waterAt && in.waterAt(x, z)) ++wetRect;
                if (!l.hasDoor) continue;
                ++doors;
                // the parcel's edge on the side the door faces is the toft's edge (the gate and the approach are there)
                const bool same = l.doorNX > 0 ? pr.x1() == l.rect.x1() : l.doorNX < 0 ? pr.x == l.rect.x : l.doorNZ > 0 ? pr.z1() == l.rect.z1() : pr.z == l.rect.z;
                if (!same) ++doorSideMoved;
                // every cell from the door wall outward to the toft's edge is in the parcel
                for (int k = 1; k <= 8; ++k) {
                    const int x = l.doorX + l.doorNX * k, z = l.doorZ + l.doorNZ * k;
                    if (x < l.rect.x || x >= l.rect.x1() || z < l.rect.z || z >= l.rect.z1()) break;
                    if (!l.inParcel(x, z)) ++approachOutside;
                }
            }
            std::printf("[ M5b-2 ]   %-10s %-8s from the footprint / door / water sampler: footprint cells outside the parcel %d, door-side edges moved %d of %d doors, "
                        "approach cells outside %d, wet cells in a toft rect %d\n", f.name, s.tier, footOutside, doorSideMoved, doors, approachOutside, wetRect);
            EXPECT_EQ(footOutside, 0) << f.name << " " << s.tier;
            EXPECT_EQ(doorSideMoved, 0) << f.name << " " << s.tier << ": the parcel gave up its frontage";
            EXPECT_EQ(approachOutside, 0) << f.name << " " << s.tier;
            EXPECT_EQ(wetRect, 0) << f.name << " " << s.tier << ": the growth seated a toft on water (the conquest's water gate would then shorten at lambda 0)";
            EXPECT_EQ(p.overlapCells, 0) << f.name << " " << s.tier;
            EXPECT_EQ(p.wetCells, 0) << f.name << " " << s.tier;
        }
    EXPECT_GT(total, 0);
}

// (n) the record: `parcel` round-trips, and a record without it (written before M5b-2) reads back as the rect.
TEST(SettlementMapTest, ParcelRoundTripsAndAnOldRecordReadsAsTheRect) {
    ASSERT_TRUE(rigs().programOk);
    const SettlementMapInput in = inputFor("village", 80, 48, plane20z, {});   // chain 196: the X plane seats one lot along X
    const SettlementMap m = planSettlementMap(in);
    ASSERT_TRUE(m.ok()) << m.refusal;
    const nlohmann::json j = m.toJson();
    int written = 0, shorter = 0;
    for (const auto& l : j["lots"]) if (l.contains("parcel")) ++written;
    for (const auto& l : m.lots) if (l.parcelArea() < l.rect.w * l.rect.d) ++shorter;
    const SettlementMap back = SettlementMap::fromJson(j);
    EXPECT_EQ(back.toJson().dump(), j.dump());
    nlohmann::json old = j;
    for (auto& l : old["lots"]) l.erase("parcel");
    const SettlementMap asOld = SettlementMap::fromJson(old);
    int notRect = 0;
    for (const auto& l : asOld.lots) if (!l.parcel.empty() || l.parcelArea() != l.rect.w * l.rect.d || !l.inParcel(l.rect.x, l.rect.z) || l.inParcel(l.rect.x1(), l.rect.z)) ++notRect;
    std::printf("[ M5b-2 ] record: %zu lots, %d with `parcel` in the JSON, %d shorter than the rect; read without it: %d not the rect\n", m.lots.size(), written, shorter, notRect);
    EXPECT_EQ(written, static_cast<int>(m.lots.size()));
    EXPECT_GT(shorter, 0) << "the rig's round trip carries no parcel that differs from its rect";
    EXPECT_EQ(notRect, 0);
}

// The parcels are a pure function of the height FIELD, not of the sampler's history (the M5a pin's shape on the 20 %
// plane, where the parcels differ from their rects).
TEST(SettlementMapTest, ParcelsAreOrderIndependentOfTheSampler) {
    ASSERT_TRUE(rigs().programOk);
    auto memo = std::make_shared<std::map<std::pair<int, int>, int>>();
    for (int z = 47; z >= 0; --z) for (int x = 79; x >= 0; --x) (*memo)[{x, z}] = plane20z(x, z);   // filled in reverse first
    SettlementMapInput a = inputFor("village", 80, 48, plane20z, {}), b = a;
    b.heightAt = [memo](int x, int z) { const auto it = memo->find({x, z}); return it != memo->end() ? it->second : ((*memo)[{x, z}] = plane20z(x, z)); };
    SettlementMap ma = planSettlementMap(a), mb = planSettlementMap(b);
    ma.bakeMs = mb.bakeMs = 0.0f;
    int shorter = 0;
    for (const auto& l : ma.lots) if (l.parcelArea() < l.rect.w * l.rect.d) ++shorter;
    std::printf("[ M5b-2 ] two samplers on the 20 %% plane: %zu lots each, %d parcels shorter than their rect\n", ma.lots.size(), shorter);
    EXPECT_GT(shorter, 0);
    EXPECT_EQ(ma.toJson().dump(), mb.toJson().dump());
}

// The realizer's five parcel consumers read layout.plots / msl.assigned: the projection hands them the PARCEL (one
// rect, D51 as revised), and the map lot keeps the nominal toft in `rect`.
TEST(SettlementMapTest, TheProjectionHandsTheParcelToTheRealizer) {
    ASSERT_TRUE(rigs().programOk);
    const SettlementMapInput in = inputFor("village", 80, 48, plane20z, {});
    const SettlementMap m = planSettlementMap(in);
    ASSERT_TRUE(m.ok()) << m.refusal;
    const MapProjection p = projectMap(m);
    ASSERT_EQ(p.layout.plots.size(), m.lots.size());
    ASSERT_EQ(p.msl.assigned.size(), m.lots.size());
    int shorter = 0, plotNotParcel = 0, assignedNotParcel = 0, frontMoved = 0;
    for (size_t i = 0; i < m.lots.size(); ++i) {
        const MapLot& l = m.lots[i];
        if (l.parcel.size() != 1) { ++plotNotParcel; continue; }
        const Rect& pr = l.parcel[0];
        if (pr.w * pr.d < l.rect.w * l.rect.d) ++shorter;
        const auto same = [&](const Rect& r) { return r.x == pr.x && r.z == pr.z && r.w == pr.w && r.d == pr.d; };
        if (!same(p.layout.plots[i].rect)) ++plotNotParcel;
        if (!same(p.msl.assigned[i].plot.rect)) ++assignedNotParcel;
        // the frontage edge (where the gate and the approach are) never moves
        bool front = true;
        switch (l.orientation) { case 0: front = pr.z1() == l.rect.z1(); break; case 2: front = pr.z == l.rect.z; break; case 1: front = pr.x1() == l.rect.x1(); break; default: front = pr.x == l.rect.x; break; }
        if (!front) ++frontMoved;
    }
    std::printf("[ M5b-2 ] projection: %zu lots, %d parcels shorter than the rect; plots that are not the parcel %d, assigned plots %d, frontage edges moved %d\n",
                m.lots.size(), shorter, plotNotParcel, assignedNotParcel, frontMoved);
    EXPECT_GT(shorter, 0);
    EXPECT_EQ(plotNotParcel, 0);
    EXPECT_EQ(assignedNotParcel, 0);
    EXPECT_EQ(frontMoved, 0);
}

// The audit's claim 5: the terrace is ONE rule (planParcelTerrace) called by the planner and by the realizer's unit -
// its properties measured on its output, the parcel against the toft on the same ground (the rule reads WHICH rect it
// is given), and the planner's stated terrace level against the rule on the natural ground for the first lot.
TEST(SettlementMapTest, TheTerraceIsOneRuleOverTheParcel) {
    ASSERT_TRUE(rigs().programOk);
    const std::function<int(int, int)> ground = plane20z;
    const Rect parcel{10, 10, 8, 6}, toft{10, 10, 8, 8};
    const ParcelTerrace t = planParcelTerrace(parcel, 4, ground), tt = planParcelTerrace(toft, 4, ground);
    ASSERT_TRUE(t.ok); ASSERT_TRUE(tt.ok);
    std::vector<int> tops;
    for (int x = parcel.x; x < parcel.x1(); ++x) for (int z = parcel.z; z < parcel.z1(); ++z) tops.push_back(ground(x, z));
    std::sort(tops.begin(), tops.end());
    int inNotGrade = 0, skirtOffRamp = 0, skirtPastNatural = 0, changedInSkirt = 0;
    for (const auto& c : t.cells) {
        const int dx = std::max({parcel.x - c.x, c.x - (parcel.x1() - 1), 0}), dz = std::max({parcel.z - c.z, c.z - (parcel.z1() - 1), 0}), dist = std::max(dx, dz);
        const int nat = ground(c.x, c.z);
        if (dist == 0) { if (c.target != t.grade) ++inNotGrade; continue; }
        if (std::abs(c.target - t.grade) > dist) ++skirtOffRamp;
        if (c.target < std::min(nat, t.grade) || c.target > std::max(nat, t.grade)) ++skirtPastNatural;
        if (c.target != nat) ++changedInSkirt;
    }
    std::map<std::pair<int, int>, int> byCell;
    for (const auto& c : tt.cells) byCell[{c.x, c.z}] = c.target;
    int differFromToft = 0, lostRowAtToftGrade = 0, lostRowAtParcelGrade = 0;
    for (const auto& c : t.cells) {
        const auto it = byCell.find({c.x, c.z});
        if (it != byCell.end() && it->second != c.target) ++differFromToft;
        if (c.z >= parcel.z1() && c.z < toft.z1() && c.x >= toft.x && c.x < toft.x1()) {   // the two rows the parcel gives up
            if (it != byCell.end() && it->second == tt.grade) ++lostRowAtToftGrade;
            if (c.target == t.grade) ++lostRowAtParcelGrade;
        }
    }
    std::printf("[ M5b-2 ] terrace rule on the 20 %% Z plane: parcel 8 x 6 grade %d (median here %d), toft 8 x 8 grade %d; %zu cells (expected %d); parcel cells off grade %d, "
                "skirt cells off the ramp %d, past the natural ground %d, changed in the skirt %d; targets that differ from the toft's %d; the two given-up rows: %d of 16 at "
                "the toft's grade under the toft, %d at the parcel's grade under the parcel\n", t.grade, tops[tops.size() / 2], tt.grade, t.cells.size(), (8 + 8) * (6 + 8),
                inNotGrade, skirtOffRamp, skirtPastNatural, changedInSkirt, differFromToft, lostRowAtToftGrade, lostRowAtParcelGrade);
    EXPECT_EQ(t.grade, tops[tops.size() / 2]);
    EXPECT_EQ(t.cells.size(), static_cast<size_t>((8 + 8) * (6 + 8)));
    EXPECT_EQ(inNotGrade, 0);
    EXPECT_EQ(skirtOffRamp, 0);
    EXPECT_EQ(skirtPastNatural, 0);
    EXPECT_GT(differFromToft, 0) << "the rule gives the parcel and the toft the same targets on a slope";
    EXPECT_EQ(lostRowAtToftGrade, 16) << "under the toft the two back rows are graded flat";
    EXPECT_LT(lostRowAtParcelGrade, 16) << "under the parcel the two rows it gives up are still graded flat";
    EXPECT_FALSE(planParcelTerrace(Rect{0, 0, 1, 5}, 4, ground).ok);
    EXPECT_FALSE(planParcelTerrace(parcel, 4, {}).ok);

    // the planner: the first lot's stated terrace level is the rule on the natural ground over ITS PARCEL
    for (const float lambda : {4.7f, 0.0f}) {
        SettlementMapInput in = inputFor("village", 80, 48, plane20z, {});
        in.terraceLots = true; in.parcelSlopeWeight = lambda;
        const SettlementMap m = planSettlementMap(in);
        ASSERT_TRUE(m.ok()) << m.refusal;
        ASSERT_FALSE(m.lots.empty());
        int stated = 0, equalRule = 0, shorter = 0, shorterEqualToftRule = 0;
        for (const auto& l : m.lots) {
            if (!l.hasTerrace) continue;
            ++stated;
            const bool isShorter = l.parcelArea() < l.rect.w * l.rect.d;
            if (isShorter) ++shorter;
            if (l.terraceY == planParcelTerrace(l.parcelRect(), 4, ground).grade) ++equalRule;
            if (isShorter && l.terraceY == planParcelTerrace(l.rect, 4, ground).grade) ++shorterEqualToftRule;
        }
        const MapLot& l0 = m.lots[0];
        std::printf("[ M5b-2 ] planner terraces, lambda_s %.1f: %d of %zu lots state a terrace; %d equal the rule over their parcel on the NATURAL ground (later lots read earlier "
                    "terraces' skirts - printed); %d parcels shorter, %d of them at the grade the rule gives their TOFT (printed); lot 0 states %d, the rule %d\n",
                    lambda, stated, m.lots.size(), equalRule, shorter, shorterEqualToftRule, l0.terraceY, planParcelTerrace(l0.parcelRect(), 4, ground).grade);
        EXPECT_EQ(stated, static_cast<int>(m.lots.size()));
        EXPECT_TRUE(l0.hasTerrace);
        EXPECT_EQ(l0.terraceY, planParcelTerrace(l0.parcelRect(), 4, ground).grade) << "the first lot's terrace is not the rule over its parcel";
    }
}

// ---- M5c-2 (D56-D58 PROPOSED): the CROFT - one rect behind a peasant toft's parcel, sized by the free ground ------------
namespace {
struct CroftStats {
    int crofts = 0, eligible = 0, cells = 0, onIneligible = 0, twoOnOneLot = 0, thin = 0, notAnchored = 0, widerThanParcel = 0;
    int outside = 0, inAParcel = 0, inAnotherToft = 0, wet = 0, onAWay = 0, overlapping = 0, inAMarket = 0, givenUpRowsOwned = 0;
};
// independent of the planner: the back edge from rect / footprint / orientation, the way's centreline sampled between nodes
CroftStats croftStats(const SettlementMap& m, const SettlementMapInput& in) {
    CroftStats c;
    for (const auto& l : m.lots) if (in.tier->hasCroft(l.typology)) ++c.eligible;
    std::set<int> seenLots;
    std::set<std::pair<int, int>> taken, wayCells;
    for (const auto& w : m.ways) {
        const int r = static_cast<int>(std::floor(w.width / 2.0f));
        for (size_t i = 0; i + 1 < w.nodes.size(); ++i) {
            const MapNode* a = m.node(w.nodes[i]); const MapNode* b = m.node(w.nodes[i + 1]);
            if (!a || !b) continue;
            const int n = std::max({std::abs(b->x - a->x), std::abs(b->z - a->z), 1});
            for (int t = 0; t <= n; ++t) {
                const int cx = a->x + static_cast<int>(std::lround(static_cast<float>(b->x - a->x) * t / n)), cz = a->z + static_cast<int>(std::lround(static_cast<float>(b->z - a->z) * t / n));
                for (int dx = -r; dx <= r; ++dx) for (int dz = -r; dz <= r; ++dz) wayCells.insert({cx + dx, cz + dz});
            }
        }
    }
    for (const auto& a : m.areas) {
        if (a.landuse != "croft") continue;
        ++c.crofts; c.cells += a.rect.w * a.rect.d;
        const MapLot* l = nullptr;
        for (const auto& ml : m.lots) if (ml.id == a.lot) l = &ml;
        if (!l || !in.tier->hasCroft(l->typology)) { ++c.onIneligible; continue; }
        if (!seenLots.insert(a.lot).second) ++c.twoOnOneLot;
        if (a.rect.w < 2 || a.rect.d < 2) ++c.thin;
        const Rect p = l->parcelRect();
        bool anchored = false, within = false;
        switch (((l->orientation % 4) + 4) % 4) {
            case 0: anchored = a.rect.z1() == p.z; within = a.rect.x >= p.x && a.rect.x1() <= p.x1(); break;   // the door faces +Z: the back is -Z
            case 2: anchored = a.rect.z == p.z1(); within = a.rect.x >= p.x && a.rect.x1() <= p.x1(); break;
            case 1: anchored = a.rect.x1() == p.x; within = a.rect.z >= p.z && a.rect.z1() <= p.z1(); break;
            default: anchored = a.rect.x == p.x1(); within = a.rect.z >= p.z && a.rect.z1() <= p.z1(); break;
        }
        if (!anchored) ++c.notAnchored;
        if (!within) ++c.widerThanParcel;
        for (int x = a.rect.x; x < a.rect.x1(); ++x) for (int z = a.rect.z; z < a.rect.z1(); ++z) {
            if (x < 0 || z < 0 || x >= m.W || z >= m.D) { ++c.outside; continue; }
            for (const auto& ml : m.lots) {
                const bool inRect = x >= ml.rect.x && x < ml.rect.x1() && z >= ml.rect.z && z < ml.rect.z1();
                if (ml.inParcel(x, z)) ++c.inAParcel;
                else if (inRect && ml.id != l->id) ++c.inAnotherToft;
                else if (inRect) ++c.givenUpRowsOwned;   // its own toft's rows the parcel gave up (O22 c)
            }
            if (in.waterAt && in.waterAt(x, z)) ++c.wet;
            if (wayCells.count({x, z})) ++c.onAWay;
            if (!taken.insert({x, z}).second) ++c.overlapping;
            for (const auto& o : m.areas) if (o.landuse == "market" && x >= o.rect.x && x < o.rect.x1() && z >= o.rect.z && z < o.rect.z1()) ++c.inAMarket;
        }
    }
    return c;
}
void printCrofts(const char* what, const SettlementMap& m, const CroftStats& c) {
    std::printf("[ M5c-2 ] %s: %zu lots, %d eligible, %d crofts, %d cells (%d of them toft rows the parcel gave up) | on an ineligible lot %d, two on one lot %d, thinner than 2 %d, "
                "not on the parcel's back edge %d, wider than the parcel %d | outside %d, in a parcel %d, in another toft %d, wet %d, on a way %d, overlapping %d, in a market %d%s",
                what, m.lots.size(), c.eligible, c.crofts, c.cells, c.givenUpRowsOwned, c.onIneligible, c.twoOnOneLot, c.thin, c.notAnchored, c.widerThanParcel, c.outside, c.inAParcel,
                c.inAnotherToft, c.wet, c.onAWay, c.overlapping, c.inAMarket, "\n");
    for (const auto& a : m.areas) if (a.landuse == "croft") std::printf("[ M5c-2 ]   croft of lot %d: %d,%d %dx%d%s", a.lot, a.rect.x, a.rect.z, a.rect.w, a.rect.d, "\n");
}
void expectCroftsSound(const char* what, const CroftStats& c) {
    EXPECT_EQ(c.onIneligible, 0) << what; EXPECT_EQ(c.twoOnOneLot, 0) << what; EXPECT_EQ(c.thin, 0) << what;
    EXPECT_EQ(c.notAnchored, 0) << what; EXPECT_EQ(c.widerThanParcel, 0) << what; EXPECT_EQ(c.outside, 0) << what;
    EXPECT_EQ(c.inAParcel, 0) << what; EXPECT_EQ(c.inAnotherToft, 0) << what; EXPECT_EQ(c.wet, 0) << what;
    EXPECT_EQ(c.onAWay, 0) << what; EXPECT_EQ(c.overlapping, 0) << what; EXPECT_EQ(c.inAMarket, 0) << what;
}
}  // namespace

// (a) a tier whose program names croft typologies plans crofts; the control (crofts = false) plans none; twice the same
TEST(SettlementMapTest, PeasantToftsCarryACroft) {
    ASSERT_TRUE(rigs().programOk);
    for (const ParcelSite s : {kParcelSites[0], kParcelSites[1]})
        for (const bool slope : {false, true}) {
            SettlementMapInput in = inputFor(s.tier, s.W, s.D, slope ? std::function<int(int, int)>(plane20z) : std::function<int(int, int)>(flatHeight), {});
            const SettlementMap m = planSettlementMap(in);
            ASSERT_TRUE(m.ok()) << s.tier << ": " << m.refusal;
            const CroftStats c = croftStats(m, in);
            const std::string what = std::string(s.tier) + (slope ? " on the 20 % Z plane" : " flat");
            printCrofts(what.c_str(), m, c);
            EXPECT_GT(c.eligible, 0) << what;
            EXPECT_GT(c.crofts, 0) << what << ": no croft behind any peasant toft";
            expectCroftsSound(what.c_str(), c);
            SettlementMap again = planSettlementMap(in), a0 = m;
            again.bakeMs = 0.0f; a0.bakeMs = 0.0f;
            EXPECT_EQ(again.toJson().dump(), a0.toJson().dump()) << what << ": the map differs between two plans";
            EXPECT_EQ(SettlementMap::fromJson(m.toJson()).toJson().dump(), m.toJson().dump()) << what << ": the JSON round trip changed the record";
            in.crofts = false;
            const SettlementMap off = planSettlementMap(in);
            EXPECT_EQ(croftStats(off, in).crofts, 0) << what << ": the control plans a croft";
            EXPECT_EQ(off.lots.size(), m.lots.size()) << what << ": the crofts changed the lots";
        }
}

// (b) every croft is sound on every tier and rig - and on the hill with a creek the count is not vacuous
TEST(SettlementMapTest, CroftsAreDisjointAndInsideTheSite) {
    ASSERT_TRUE(rigs().programOk);
    int total = 0;
    for (const ParcelSite s : kParcelSites)
        for (int rig = 0; rig < 3; ++rig) {
            const SettlementMapInput in = rig == 0 ? inputFor(s.tier, s.W, s.D, hillHeight, hillWater)
                                        : rig == 1 ? inputFor(s.tier, s.W, s.D, plane20x, {}) : inputFor(s.tier, s.W, s.D, slopeRolling, {});
            const SettlementMap m = planSettlementMap(in);
            if (!m.ok()) { std::printf("[ M5c-2 ] %s rig %d refused: %s%s", s.tier, rig, m.refusal.c_str(), "\n"); continue; }
            const CroftStats c = croftStats(m, in);
            const std::string what = std::string(s.tier) + (rig == 0 ? " hill + creek" : rig == 1 ? " 20 % X plane" : " rolling");
            printCrofts(what.c_str(), m, c);
            expectCroftsSound(what.c_str(), c);
            total += c.crofts;
        }
    EXPECT_GT(total, 0) << "no croft on any rig: every check above is vacuous";
}

// (d) a PIN, green before the feature (stated): town and city carry no croft - their program names no croft typology
TEST(SettlementMapTest, ATownCarriesNoCroft) {
    ASSERT_TRUE(rigs().programOk);
    for (const ParcelSite s : {kParcelSites[2], kParcelSites[3]}) {
        const SettlementMapInput in = inputFor(s.tier, s.W, s.D, plane20z, {});
        const SettlementMap m = planSettlementMap(in);
        ASSERT_TRUE(m.ok()) << s.tier << ": " << m.refusal;
        EXPECT_TRUE(in.tier->croftTypologies.empty()) << s.tier;
        EXPECT_EQ(croftStats(m, in).crofts, 0) << s.tier;
    }
}

// M5d design check (pm_chain233.txt PM5d.0): a DIAGNOSTIC, no assertions - the wall as built today (planTownWall on
// the site rect, with the streets the map's projection hands the realizer) measured against the city's lots.
TEST(SettlementMapTest, DISABLED_DiagCityWallEnvelope) {
    ASSERT_TRUE(rigs().programOk);
    struct Rig { const char* name; int W, D; std::function<int(int, int)> h; std::function<bool(int, int)> w; };
    const Rig rigsList[] = {{"flat 160x120", 160, 120, flatHeight, {}}, {"flat 160x160", 160, 160, flatHeight, {}},
                            {"hill + creek 160x120", 160, 120, hillHeight, hillWater}, {"hill + creek 160x160", 160, 160, hillHeight, hillWater},
                            {"rolling 160x160", 160, 160, slopeRolling, {}}};
    for (const Rig& r : rigsList) {
        const SettlementMapInput in = inputFor("city", r.W, r.D, r.h, r.w);
        const SettlementMap m = planSettlementMap(in);
        if (!m.ok()) { std::printf("[ M5d ] %s: map refused: %s%s", r.name, m.refusal.c_str(), "\n"); continue; }
        const MapProjection pr = projectMap(m);
        std::vector<Rect> fps; int bx0 = INT_MAX, bz0 = INT_MAX, bx1 = INT_MIN, bz1 = INT_MIN; long lotCells = 0;
        for (const auto& l : m.lots) {
            fps.push_back(l.footprint); lotCells += static_cast<long>(l.rect.w) * l.rect.d;
            bx0 = std::min(bx0, l.rect.x); bz0 = std::min(bz0, l.rect.z); bx1 = std::max(bx1, l.rect.x1()); bz1 = std::max(bz1, l.rect.z1());
        }
        const TownWallPlan wp = planTownWall(Rect{0, 0, r.W, r.D}, pr.layout.streets, fps, in.tier->walls);
        std::printf("[ M5d ] %s: %zu lots, %zu ways, %zu projected street rects | lots bbox %d,%d..%d,%d (%d x %d) | gaps to the site edge W %d E %d S %d N %d | lot rect cells %ld of %d site cells (%.1f %%)%s",
                    r.name, m.lots.size(), m.ways.size(), pr.layout.streets.size(), bx0, bz0, bx1, bz1, bx1 - bx0, bz1 - bz0, bx0, r.W - bx1, bz0, r.D - bz1,
                    lotCells, r.W * r.D, 100.0 * static_cast<double>(lotCells) / (r.W * r.D), "\n");
        if (!wp.ok) { std::printf("[ M5d ]   wall as built: REFUSED: %s%s", wp.refusal.c_str(), "\n"); continue; }
        const long inner = static_cast<long>(wp.innerBound.w) * wp.innerBound.d;
        long bandCells = 0, bandWet = 0; int hLo = INT_MAX, hHi = INT_MIN;
        for (const auto& run : wp.runs)
            for (int x = run.band.x; x < run.band.x1(); ++x) for (int z = run.band.z; z < run.band.z1(); ++z) {
                ++bandCells;
                if (r.w && r.w(x, z)) ++bandWet;
                const int h = r.h(x, z); hLo = std::min(hLo, h); hHi = std::max(hHi, h);
            }
        std::printf("[ M5d ]   wall as built: ok, %zu runs, %zu gates, %zu towers | inner bound %d,%d %d x %d = %ld cells; the lots' bbox is %.1f %% of it, the lots' rects %.1f %% | band %ld cells, %ld in water, ground along it %d..%d%s",
                    wp.runs.size(), wp.gates.size(), wp.towers.size(), wp.innerBound.x, wp.innerBound.z, wp.innerBound.w, wp.innerBound.d, inner,
                    100.0 * static_cast<double>(bx1 - bx0) * (bz1 - bz0) / inner, 100.0 * static_cast<double>(lotCells) / inner, bandCells, bandWet, hLo, hHi, "\n");
        for (const auto& g : wp.gates) std::printf("[ M5d ]     gate side %c opening %d,%d %d x %d street width %d%s", g.side, g.opening.x, g.opening.z, g.opening.w, g.opening.d, g.streetWidth, "\n");
    }
}

// ============================================================================
// M5d-1 (the M5d design check of 2026-11-14, D59-D61 PROPOSED; predictions in pm_chain233.txt): the wall as map data
// ============================================================================
namespace {
struct WallStats {
    bool present = false;
    int cells = 0, corners = 0, gates = 0, gaps = 0;
    int broken = 0, repeated = 0;                      // ring: a step that is not one cell along an axis; a cell twice
    int lotCellsOutside = 0, marketCellsOutside = 0;   // not enclosed
    int inLotMargin = 0, inMarket = 0;                 // a ring cell inside a lot rect grown by the margin / in the market
    int wetNotInGap = 0, dryInGap = 0, wet = 0;
    int crossings = 0, ungated = 0, gateOffRing = 0, gateNarrow = 0;
    int approachCells = 0, onApproach = 0, gateInGap = 0;
    int outerInLot = 0;                   // the audit of M5d-2: a cell of the band's OUTER course (8-adjacent to the ring, not inside) in a lot's rect
    int roadCells = 0, roadUngated = 0;   // M5d-1b: ring cells on the WORLD road; of them, with no gate within half its width   // the audit of M5d-1, condition 4: ring vs the lots' door approaches; a gate in a water gap
    long enclosed = 0;
};
// independent of the planner: inside = what a flood from outside the ring's bounding box cannot reach
WallStats wallStats(const SettlementMap& m, const SettlementMapInput& in) {
    WallStats w; w.present = m.wall.present;
    if (!m.wall.present) return w;
    const auto& ring = m.wall.ring;
    w.cells = static_cast<int>(ring.size()); w.corners = static_cast<int>(m.wall.corners().size());
    w.gates = static_cast<int>(m.wall.gates.size()); w.gaps = static_cast<int>(m.wall.waterGaps.size());
    std::set<std::pair<int, int>> onRing;
    int x0 = 0, z0 = 0, x1 = m.W, z1 = m.D;
    for (size_t i = 0; i < ring.size(); ++i) {
        const auto& a = ring[i]; const auto& b = ring[(i + 1) % ring.size()];
        if (std::abs(a.x - b.x) + std::abs(a.z - b.z) != 1) ++w.broken;
        if (!onRing.insert({a.x, a.z}).second) ++w.repeated;
        x0 = std::min(x0, a.x); z0 = std::min(z0, a.z); x1 = std::max(x1, a.x + 1); z1 = std::max(z1, a.z + 1);
    }
    x0 -= 2; z0 -= 2; x1 += 2; z1 += 2;
    const int GW = x1 - x0, GD = z1 - z0;
    std::vector<uint8_t> out(static_cast<size_t>(GW) * GD, 0);
    std::vector<std::pair<int, int>> stack{{x0, z0}};
    out[0] = 1;
    while (!stack.empty()) {
        const auto [x, z] = stack.back(); stack.pop_back();
        const int nx[4] = {x + 1, x - 1, x, x}, nz[4] = {z, z, z + 1, z - 1};
        for (int k = 0; k < 4; ++k) {
            if (nx[k] < x0 || nz[k] < z0 || nx[k] >= x1 || nz[k] >= z1) continue;
            const size_t i = static_cast<size_t>(nz[k] - z0) * GW + (nx[k] - x0);
            if (out[i] || onRing.count({nx[k], nz[k]})) continue;
            out[i] = 1; stack.push_back({nx[k], nz[k]});
        }
    }
    const auto inside = [&](int x, int z) {
        if (x < x0 || z < z0 || x >= x1 || z >= z1) return false;
        return !out[static_cast<size_t>(z - z0) * GW + (x - x0)] && !onRing.count({x, z});
    };
    for (int z = z0; z < z1; ++z) for (int x = x0; x < x1; ++x) if (inside(x, z)) ++w.enclosed;
    for (const auto& l : m.lots)
        for (int x = l.rect.x; x < l.rect.x1(); ++x) for (int z = l.rect.z; z < l.rect.z1(); ++z) if (!inside(x, z)) ++w.lotCellsOutside;
    for (const auto& a : m.areas) if (a.landuse == "market")
        for (int x = a.rect.x; x < a.rect.x1(); ++x) for (int z = a.rect.z; z < a.rect.z1(); ++z) if (!inside(x, z)) ++w.marketCellsOutside;
    const int mg = in.tier->walls.marginCubes;
    std::set<std::pair<int, int>> inGap;
    for (const auto& g : m.wall.waterGaps) {
        size_t at = ring.size();
        for (size_t i = 0; i < ring.size(); ++i) if (ring[i].x == g.x && ring[i].z == g.z) { at = i; break; }
        if (at == ring.size()) { ++w.dryInGap; continue; }
        for (int k = 0; k < g.cells; ++k) inGap.insert({ring[(at + k) % ring.size()].x, ring[(at + k) % ring.size()].z});
    }
    for (const auto& c : ring) {
        for (const auto& l : m.lots)
            if (c.x >= l.rect.x - mg && c.x < l.rect.x1() + mg && c.z >= l.rect.z - mg && c.z < l.rect.z1() + mg) { ++w.inLotMargin; break; }
        for (const auto& a : m.areas) if (a.landuse == "market" && c.x >= a.rect.x && c.x < a.rect.x1() && c.z >= a.rect.z && c.z < a.rect.z1()) ++w.inMarket;
        const bool wetCell = in.waterAt && in.waterAt(c.x, c.z);
        if (wetCell) ++w.wet;
        if (wetCell && !inGap.count({c.x, c.z})) ++w.wetNotInGap;
        if (!wetCell && inGap.count({c.x, c.z})) ++w.dryInGap;
    }
    // every way: its centreline between nodes, and from an end node on the site edge straight out to the ring
    for (const auto& way : m.ways) {
        std::vector<std::pair<int, int>> line;
        for (size_t i = 0; i + 1 < way.nodes.size(); ++i) {
            const MapNode* a = m.node(way.nodes[i]); const MapNode* b = m.node(way.nodes[i + 1]);
            if (!a || !b) continue;
            const int n = std::max({std::abs(b->x - a->x), std::abs(b->z - a->z), 1});
            for (int t = 0; t <= n; ++t)
                line.push_back({a->x + static_cast<int>(std::lround(static_cast<float>(b->x - a->x) * t / n)), a->z + static_cast<int>(std::lround(static_cast<float>(b->z - a->z) * t / n))});
        }
        for (const int end : {way.nodes.front(), way.nodes.back()}) {
            const MapNode* n = m.node(end);
            if (!n) continue;
            int dx = 0, dz = 0;
            if (n->x <= 0) dx = -1; else if (n->x >= m.W - 1) dx = 1; else if (n->z <= 0) dz = -1; else if (n->z >= m.D - 1) dz = 1; else continue;
            for (int k = 1; k <= 64; ++k) {
                line.push_back({n->x + dx * k, n->z + dz * k});
                if (onRing.count(line.back())) break;
            }
        }
        std::set<std::pair<int, int>> seen;
        for (const auto& c : line) {
            if (!onRing.count(c) || !seen.insert(c).second) continue;
            ++w.crossings;
            bool gated = false;
            for (const auto& g : m.wall.gates)
                if (g.way == way.id && std::abs(g.x - c.first) + std::abs(g.z - c.second) <= std::max(1, g.width / 2)) gated = true;
            if (!gated) ++w.ungated;
        }
    }
    {
        std::set<std::pair<int, int>> outer;
        for (const auto& c : ring)
            for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx)
                if (!onRing.count({c.x + dx, c.z + dz}) && !inside(c.x + dx, c.z + dz)) outer.insert({c.x + dx, c.z + dz});
        for (const auto& o : outer)
            for (const auto& l : m.lots) if (o.first >= l.rect.x && o.first < l.rect.x1() && o.second >= l.rect.z && o.second < l.rect.z1()) { ++w.outerInLot; break; }
    }
    if (in.roadAt)
        for (const auto& c : ring) {
            if (in.roadAt(c.x, c.z) <= 0) continue;
            ++w.roadCells;
            bool gated = false;
            for (const auto& g : m.wall.gates) if (std::abs(g.x - c.x) + std::abs(g.z - c.z) <= std::max(1, g.width / 2)) gated = true;
            if (!gated) ++w.roadUngated;
        }
    for (const auto& l : m.lots)
        if (l.hasApproach)
            for (const auto& a : l.approach) { ++w.approachCells; if (onRing.count({a.x, a.z})) ++w.onApproach; }
    for (const auto& g : m.wall.gates) {
        if (inGap.count({g.x, g.z})) ++w.gateInGap;
        if (!onRing.count({g.x, g.z})) ++w.gateOffRing;
        for (const auto& way : m.ways) if (way.id == g.way && g.width < static_cast<int>(std::ceil(way.width))) ++w.gateNarrow;
    }
    return w;
}
void printWall(const char* what, const SettlementMap& m, const WallStats& w, long rectangle) {
    std::printf("[ M5d-1 ] %s: %zu lots, %zu ways | wall %s%s%s | %d ring cells, %d corners, %d gates, %d water gaps (%d wet cells) | enclosed %ld cells = %.1f %% of the rectangle's %ld | "
                "ring broken %d, repeated %d | lot cells outside %d, market cells outside %d | ring in a lot's margin %d, in the market %d | wet not in a gap %d, dry in a gap %d | "
                "crossings %d, ungated %d, gate off the ring %d, gate narrower than its way %d | on a door approach %d of %d approach cells, gate in a water gap %d | on the world road %d, ungated %d | bake %.1f ms, the wall's %.1f ms%s",
                what, m.lots.size(), m.ways.size(), w.present ? "present" : "ABSENT", m.wall.refusal.empty() ? "" : " refusal: ", m.wall.refusal.c_str(), w.cells, w.corners, w.gates, w.gaps, w.wet,
                w.enclosed, rectangle > 0 ? 100.0 * static_cast<double>(w.enclosed) / static_cast<double>(rectangle) : 0.0, rectangle, w.broken, w.repeated, w.lotCellsOutside, w.marketCellsOutside,
                w.inLotMargin, w.inMarket, w.wetNotInGap, w.dryInGap, w.crossings, w.ungated, w.gateOffRing, w.gateNarrow, w.onApproach, w.approachCells, w.gateInGap, w.roadCells, w.roadUngated, m.bakeMs, m.wall.bakeMs, "\n");
    for (const auto& g : m.wall.gates) std::printf("[ M5d-1 ]   gate at %d,%d way %d width %d%s", g.x, g.z, g.way, g.width, "\n");
    for (const auto& g : m.wall.waterGaps) std::printf("[ M5d-1 ]   water gap from %d,%d over %d cells%s", g.x, g.z, g.cells, "\n");
}
void expectWallSound(const char* what, const WallStats& w) {
    EXPECT_GE(w.cells, 8) << what; EXPECT_EQ(w.broken, 0) << what; EXPECT_EQ(w.repeated, 0) << what;
    EXPECT_EQ(w.lotCellsOutside, 0) << what; EXPECT_EQ(w.marketCellsOutside, 0) << what;
    EXPECT_EQ(w.inLotMargin, 0) << what; EXPECT_EQ(w.inMarket, 0) << what;
    EXPECT_EQ(w.wetNotInGap, 0) << what; EXPECT_EQ(w.dryInGap, 0) << what;
    EXPECT_EQ(w.ungated, 0) << what; EXPECT_EQ(w.gateOffRing, 0) << what; EXPECT_EQ(w.gateNarrow, 0) << what;
    EXPECT_EQ(w.onApproach, 0) << what << ": the ring stands on a lot's door approach";
    EXPECT_EQ(w.gateInGap, 0) << what << ": a gate recorded inside a water gap";
    EXPECT_EQ(w.outerInLot, 0) << what << ": the band's outer course stands in a lot's rect";
    EXPECT_EQ(w.roadUngated, 0) << what << ": the ring stands on the world road with no gate there - the arriving road is walled in";
    EXPECT_GT(w.crossings, 0) << what << ": no way crosses the wall - the town is sealed or the wall is not round it";
}
struct WallRig { const char* name; int W, D; std::function<int(int, int)> h; std::function<bool(int, int)> w; };
std::vector<WallRig> wallRigs() {
    return {{"flat 160x120", 160, 120, flatHeight, {}}, {"flat 160x160", 160, 160, flatHeight, {}},
            {"hill + creek 160x120", 160, 120, hillHeight, hillWater}, {"hill + creek 160x160", 160, 160, hillHeight, hillWater},
            {"rolling 160x160", 160, 160, slopeRolling, {}},
            // chain 236 (added after chain 235 showed no ring meets the old creek): water THROUGH the lots
            {"flat, creek at z 40", 160, 120, flatHeight, [](int, int z) { return z >= 40 && z <= 41; }},
            {"flat, pond 60..75 x 20..30", 160, 120, flatHeight, [](int x, int z) { return x >= 60 && x <= 75 && z >= 20 && z <= 30; }}};
}
}  // namespace

// (a) the contract's named red: the city's map carries a wall, it is sound, and it is not the site rectangle
TEST(SettlementMapTest, WallIsNotTheSiteRectangle) {
    ASSERT_TRUE(rigs().programOk);
    int wetOverAllRigs = 0, gapsOverAllRigs = 0;
    for (const WallRig& r : wallRigs()) {
        const SettlementMapInput in = inputFor("city", r.W, r.D, r.h, r.w);
        ASSERT_TRUE(in.tier->walls.enabled) << "the city's program asks for no wall: every check below is vacuous";
        const SettlementMap m = planSettlementMap(in);
        if (!m.ok()) { std::printf("[ M5d-1 ] %s: the map is REFUSED: %s%s", r.name, m.refusal.c_str(), "\n"); continue; }
        const WallStats w = wallStats(m, in);
        const int mg = in.tier->walls.marginCubes;
        const long rectangle = static_cast<long>(r.W + 2 * mg) * (r.D + 2 * mg);
        printWall(r.name, m, w, rectangle);
        EXPECT_TRUE(w.present) << r.name << ": the city's map carries no wall";
        if (!w.present) continue;
        expectWallSound(r.name, w);
        if (std::string(r.name) == "hill + creek 160x160")
            EXPECT_LT(w.enclosed, rectangle) << r.name << ": the wall encloses as much as the site rectangle plus its margin (the lots' bounding box was 47.9 % of it, chain 233)";
        wetOverAllRigs += w.wet; gapsOverAllRigs += w.gaps;
    }
    // chain 236: was a per-rig guard on every rig with water (loosened after chain 235, stated in pm_chain233.txt)
    EXPECT_GT(wetOverAllRigs, 0) << "no rig's ring meets water: the water rule is not exercised";
    EXPECT_GT(gapsOverAllRigs, 0) << "no water gap recorded on any rig";
}

// (b) planned twice the same; the JSON round trip keeps the record
TEST(SettlementMapTest, TheWallIsTheSamePlannedTwice) {
    ASSERT_TRUE(rigs().programOk);
    for (const WallRig& r : wallRigs()) {
        const SettlementMapInput in = inputFor("city", r.W, r.D, r.h, r.w);
        SettlementMap a = planSettlementMap(in), b = planSettlementMap(in);
        ASSERT_TRUE(a.ok()) << r.name;
        EXPECT_TRUE(a.wall.present) << r.name << ": the city's map carries no wall";
        if (!a.wall.present) continue;
        a.bakeMs = 0.0f; b.bakeMs = 0.0f;
        EXPECT_EQ(a.toJson().dump(), b.toJson().dump()) << r.name << ": the map differs between two plans";
        const SettlementMap back = SettlementMap::fromJson(a.toJson());
        EXPECT_EQ(back.toJson().dump(), a.toJson().dump()) << r.name << ": the JSON round trip changed the record";
        EXPECT_EQ(back.wall.ring.size(), a.wall.ring.size()) << r.name << ": the ring read back has another length";
        // the site's world offset must not move the route
        SettlementMapInput moved = in; moved.originX += 4096; moved.originZ -= 777;
        SettlementMap c = planSettlementMap(moved);
        ASSERT_EQ(c.wall.ring.size(), a.wall.ring.size()) << r.name << ": the world offset changed the ring";
        for (size_t i = 0; i < a.wall.ring.size(); ++i)
            if (a.wall.ring[i].x != c.wall.ring[i].x || a.wall.ring[i].z != c.wall.ring[i].z) { ADD_FAILURE() << r.name << ": the world offset moved ring cell " << i; break; }
    }
}

// (c) a PIN, green before the feature (stated): a tier without `walls`, and the city with its walls switched off, carry no wall
TEST(SettlementMapTest, OnlyAWalledTierCarriesAWall) {
    ASSERT_TRUE(rigs().programOk);
    for (const char* tier : {"hamlet", "village", "town"}) {
        const SettlementMapInput in = inputFor(tier, 140, 60, flatHeight, {});
        ASSERT_FALSE(in.tier->walls.enabled) << tier;
        const SettlementMap m = planSettlementMap(in);
        ASSERT_TRUE(m.ok()) << tier << ": " << m.refusal;
        EXPECT_FALSE(m.wall.present) << tier;
        EXPECT_FALSE(m.toJson().contains("barriers")) << tier << ": an unwalled tier's record changed";
    }
    SettlementMapInput in = inputFor("city", 160, 160, hillHeight, hillWater);
    SettlementTierPreset off = *in.tier; off.walls.enabled = false; in.tier = &off;
    const SettlementMap m = planSettlementMap(in);
    ASSERT_TRUE(m.ok()) << m.refusal;
    EXPECT_FALSE(m.wall.present) << "the city with walls.enabled false plans a wall";
    EXPECT_FALSE(m.toJson().contains("barriers"));
}

// ============================================================================
// M5d-3 (the design check of 2026-11-15, D64 as refined + D66 PROPOSED; predictions in pm_chain286.txt): the wall's
// towers as map data. Everything below is measured from the RECORD, independently of the planner.
// ============================================================================
namespace {
struct TowerStats {
    int entries = 0, built = 0, skipped = 0, noReason = 0;
    int wrongSize = 0, cornerOffRing = 0, cornerOutsideDrum = 0;
    int onLot = 0, onArea = 0, onWay = 0, onGate = 0, onRoad = 0, onWater = 0, touching = 0;   // cells of built drums on what a drum may not stand on; pairs of drums that meet
    int doorOffRim = 0, outNotBesideDoor = 0, outNotInside = 0;
    int hullCorners = 0, hullUnserved = 0;   // the audit of M5d-3 (condition 1): the hull of the lots' and the market's corners, computed HERE; a hull corner with no tower entry within its push
};
TowerStats towerStats(const SettlementMap& m, const SettlementMapInput& in) {
    TowerStats t;
    const auto& ring = m.wall.ring;
    const size_t n = ring.size();
    if (n == 0) { t.entries = static_cast<int>(m.wall.towers.size()); return t; }
    std::map<std::pair<int, int>, size_t> ringAt;
    int x0 = 0, z0 = 0, x1 = m.W, z1 = m.D;
    for (size_t i = 0; i < n; ++i) {
        ringAt[{ring[i].x, ring[i].z}] = i;
        x0 = std::min(x0, ring[i].x); z0 = std::min(z0, ring[i].z); x1 = std::max(x1, ring[i].x + 1); z1 = std::max(z1, ring[i].z + 1);
    }
    x0 -= 2; z0 -= 2; x1 += 2; z1 += 2;
    const int GW = x1 - x0, GD = z1 - z0;
    std::vector<uint8_t> out(static_cast<size_t>(GW) * GD, 0);
    std::vector<std::pair<int, int>> stack{{x0, z0}};
    out[0] = 1;
    while (!stack.empty()) {
        const auto [x, z] = stack.back(); stack.pop_back();
        const int nx[4] = {x + 1, x - 1, x, x}, nz[4] = {z, z, z + 1, z - 1};
        for (int k = 0; k < 4; ++k) {
            if (nx[k] < x0 || nz[k] < z0 || nx[k] >= x1 || nz[k] >= z1) continue;
            const size_t i = static_cast<size_t>(nz[k] - z0) * GW + (nx[k] - x0);
            if (out[i] || ringAt.count({nx[k], nz[k]})) continue;
            out[i] = 1; stack.push_back({nx[k], nz[k]});
        }
    }
    const auto inside = [&](int x, int z) {
        if (x < x0 || z < z0 || x >= x1 || z >= z1) return false;
        return !out[static_cast<size_t>(z - z0) * GW + (x - x0)] && !ringAt.count({x, z});
    };
    std::set<std::pair<int, int>> wayBand, gateCells;
    for (const auto& way : m.ways) {
        const int r = static_cast<int>(std::ceil(way.width / 2.0f));
        for (size_t i = 0; i + 1 < way.nodes.size(); ++i) {
            const MapNode* a = m.node(way.nodes[i]); const MapNode* b = m.node(way.nodes[i + 1]);
            if (!a || !b) continue;
            const int steps = std::max({std::abs(b->x - a->x), std::abs(b->z - a->z), 1});
            for (int s = 0; s <= steps; ++s) {
                const int cx = a->x + static_cast<int>(std::lround(static_cast<float>(b->x - a->x) * s / steps)), cz = a->z + static_cast<int>(std::lround(static_cast<float>(b->z - a->z) * s / steps));
                for (int dz = -r; dz <= r; ++dz) for (int dx = -r; dx <= r; ++dx) wayBand.insert({cx + dx, cz + dz});
            }
        }
    }
    for (const auto& g : m.wall.gates) {
        const auto it = ringAt.find({g.x, g.z});
        if (it == ringAt.end()) continue;
        const size_t half = static_cast<size_t>(std::max(1, g.width / 2));
        for (size_t d = 0; d <= half && d < n; ++d) {
            gateCells.insert({ring[(it->second + d) % n].x, ring[(it->second + d) % n].z});
            gateCells.insert({ring[(it->second + n - d) % n].x, ring[(it->second + n - d) % n].z});
        }
    }
    {   // "one entry per routed corner", tied to something: the convex hull of every lot rect's and the market's corner
        // cells (the test's own monotone chain); each hull corner must have an entry whose corner lies within the
        // push the planner may use (ceil(margin * sqrt 2) + 2, up to 8 further, + 1 for rounding), and there may be
        // no more entries than hull corners
        using P = std::pair<long, long>;
        std::vector<P> pts;
        const auto add = [&pts](const Rect& r) { if (r.w > 0 && r.d > 0) { pts.push_back({r.x, r.z}); pts.push_back({r.x1() - 1, r.z}); pts.push_back({r.x, r.z1() - 1}); pts.push_back({r.x1() - 1, r.z1() - 1}); } };
        for (const auto& l : m.lots) add(l.rect);
        for (const auto& a : m.areas) if (a.landuse == "market") add(a.rect);
        std::sort(pts.begin(), pts.end()); pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
        const auto cross = [](const P& o, const P& a, const P& b) { return (a.first - o.first) * (b.second - o.second) - (a.second - o.second) * (b.first - o.first); };
        std::vector<P> hull;
        for (int pass = 0; pass < 2; ++pass) {
            const size_t base = hull.size();
            for (size_t k = 0; k < pts.size(); ++k) {
                const P& q = pass == 0 ? pts[k] : pts[pts.size() - 1 - k];
                while (hull.size() >= base + 2 && cross(hull[hull.size() - 2], hull.back(), q) <= 0) hull.pop_back();
                hull.push_back(q);
            }
            hull.pop_back();
        }
        t.hullCorners = static_cast<int>(hull.size());
        const double reach = std::ceil(static_cast<double>(in.tier->walls.marginCubes) * 1.41421356) + 2.0 + 8.0 + 1.0;
        for (const auto& h : hull) {
            bool served = false;
            for (const auto& tw : m.wall.towers) {
                const double dx = static_cast<double>(tw.cornerX - h.first), dz = static_cast<double>(tw.cornerZ - h.second);
                if (std::sqrt(dx * dx + dz * dz) <= reach) served = true;
            }
            if (!served) ++t.hullUnserved;
        }
    }
    std::vector<std::set<std::pair<int, int>>> bodies;
    for (const auto& tw : m.wall.towers) {
        ++t.entries;
        if (!tw.built) { ++t.skipped; if (tw.skipped.empty()) ++t.noReason; continue; }
        ++t.built;
        if (tw.size != in.tier->walls.towerSize) ++t.wrongSize;
        if (!ringAt.count({tw.cornerX, tw.cornerZ})) ++t.cornerOffRing;
        const Rect bb = tw.bbox();
        std::set<std::pair<int, int>> body;
        for (const auto& f : towerFootprintCells(bb, in.tier->walls.towerShape)) body.insert({bb.x + f.x, bb.z + f.y});
        if (!body.count({tw.cornerX, tw.cornerZ})) ++t.cornerOutsideDrum;
        for (const auto& c : body) {
            for (const auto& l : m.lots) if (c.first >= l.rect.x && c.first < l.rect.x1() && c.second >= l.rect.z && c.second < l.rect.z1()) { ++t.onLot; break; }
            for (const auto& a : m.areas) if (c.first >= a.rect.x && c.first < a.rect.x1() && c.second >= a.rect.z && c.second < a.rect.z1()) { ++t.onArea; break; }
            if (wayBand.count(c)) ++t.onWay;
            if (gateCells.count(c)) ++t.onGate;
            if (in.roadAt && in.roadAt(c.first, c.second) > 0) ++t.onRoad;
            if (in.waterAt && in.waterAt(c.first, c.second)) ++t.onWater;
        }
        for (const auto& other : bodies) {
            bool meets = false;
            for (const auto& c : body) {
                for (int dz = -1; dz <= 1 && !meets; ++dz) for (int dx = -1; dx <= 1 && !meets; ++dx) if (other.count({c.first + dx, c.second + dz})) meets = true;   // the audit: corners too
                if (meets) break;
            }
            if (meets) ++t.touching;
        }
        const std::pair<int, int> door{tw.doorX, tw.doorZ}, outc{tw.outX, tw.outZ};
        const bool rim = body.count(door) && (!body.count({door.first + 1, door.second}) || !body.count({door.first - 1, door.second}) || !body.count({door.first, door.second + 1}) || !body.count({door.first, door.second - 1}));
        if (!rim) ++t.doorOffRim;
        if (std::abs(outc.first - door.first) + std::abs(outc.second - door.second) != 1) ++t.outNotBesideDoor;
        if (body.count(outc) || !inside(outc.first, outc.second)) ++t.outNotInside;
        bodies.push_back(std::move(body));
    }
    return t;
}
void printTowers(const char* what, const SettlementMap& m, const TowerStats& t) {
    std::printf("[ M5d-3 ] %s: %d tower entries for %d hull corners (%d unserved), %zu ring turns - %d built, %d not built (%d with no reason) | wrong size %d, corner off the ring %d, corner outside its drum %d | "
                "drum cells on a lot %d, an area %d, a way %d, a gate %d, the road %d, water %d; drums meeting %d | door off the rim %d, out cell not beside the door %d, out cell not inside the ring %d%s",
                what, t.entries, t.hullCorners, t.hullUnserved, m.wall.corners().size(), t.built, t.skipped, t.noReason, t.wrongSize, t.cornerOffRing, t.cornerOutsideDrum, t.onLot, t.onArea, t.onWay, t.onGate, t.onRoad, t.onWater, t.touching,
                t.doorOffRim, t.outNotBesideDoor, t.outNotInside, "\n");
    for (const auto& tw : m.wall.towers) {
        if (tw.built) std::printf("[ M5d-3 ]   tower at %d,%d (corner %d,%d), size %d, door %c at %d,%d, entered from %d,%d%s", tw.x, tw.z, tw.cornerX, tw.cornerZ, tw.size, tw.doorSide, tw.doorX, tw.doorZ, tw.outX, tw.outZ, "\n");
        else std::printf("[ M5d-3 ]   corner %d,%d: NOT BUILT - %s%s", tw.cornerX, tw.cornerZ, tw.skipped.c_str(), "\n");
    }
}
void expectTowersSound(const char* what, const TowerStats& t) {
    EXPECT_EQ(t.noReason, 0) << what << ": a tower is not built and no reason is recorded";
    EXPECT_EQ(t.hullUnserved, 0) << what << ": a corner of the lots' hull has no tower entry within the wall's push of it";
    EXPECT_LE(t.entries, t.hullCorners) << what << ": more tower entries than the lots' hull has corners";
    EXPECT_EQ(t.wrongSize, 0) << what; EXPECT_EQ(t.cornerOffRing, 0) << what;
    EXPECT_EQ(t.cornerOutsideDrum, 0) << what << ": a drum does not hold its corner - the wall would not meet it";
    EXPECT_EQ(t.onLot, 0) << what; EXPECT_EQ(t.onArea, 0) << what; EXPECT_EQ(t.onWay, 0) << what; EXPECT_EQ(t.onGate, 0) << what;
    EXPECT_EQ(t.onRoad, 0) << what; EXPECT_EQ(t.onWater, 0) << what; EXPECT_EQ(t.touching, 0) << what;
    EXPECT_EQ(t.doorOffRim, 0) << what; EXPECT_EQ(t.outNotBesideDoor, 0) << what;
    EXPECT_EQ(t.outNotInside, 0) << what << ": a tower's door does not open inside the ring";
}
}  // namespace

// (a) every walled rig's record carries one tower entry per routed corner; a built tower stands clear and opens inside
TEST(SettlementMapTest, TheWallCarriesATowerAtEachRoutedCorner) {
    ASSERT_TRUE(rigs().programOk);
    int walls = 0, builtAll = 0, skippedAll = 0;
    for (const WallRig& r : wallRigs()) {
        const SettlementMapInput in = inputFor("city", r.W, r.D, r.h, r.w);
        ASSERT_TRUE(in.tier->walls.towers) << "the city's program asks for no towers: every check below is vacuous";
        const SettlementMap m = planSettlementMap(in);
        if (!m.ok() || !m.wall.present) { std::printf("[ M5d-3 ] %s: no wall (%s%s)%s", r.name, m.refusal.c_str(), m.wall.refusal.c_str(), "\n"); continue; }
        ++walls;
        const TowerStats t = towerStats(m, in);
        printTowers(r.name, m, t);
        EXPECT_GE(t.entries, 3) << r.name << ": fewer tower entries than a ring has corners to be routed through";
        expectTowersSound(r.name, t);
        builtAll += t.built; skippedAll += t.skipped;
        const SettlementMap back = SettlementMap::fromJson(m.toJson());
        EXPECT_EQ(back.wall.towers.size(), m.wall.towers.size()) << r.name << ": the round trip lost tower entries";
        const TowerStats tb = towerStats(back, in);
        EXPECT_EQ(tb.built, t.built) << r.name << ": the round trip changed which towers are built";
        expectTowersSound((std::string(r.name) + " (read back)").c_str(), tb);
    }
    std::printf("[ M5d-3 ] over %d walls: %d towers built, %d not built%s", walls, builtAll, skippedAll, "\n");
    EXPECT_GT(walls, 0);
    EXPECT_GT(builtAll, 0) << "no tower is built on any rig: every check on a built tower is vacuous";
}

// (b) degenerate inputs: a drum too small to work is not built and says why; towers switched off record none
TEST(SettlementMapTest, ATowerTooSmallToWorkIsNotBuiltAndSaysWhy) {
    ASSERT_TRUE(rigs().programOk);
    SettlementMapInput in = inputFor("city", 160, 120, flatHeight, {});
    SettlementTierPreset small = *in.tier; small.walls.towerSize = 4; in.tier = &small;
    const SettlementMap m = planSettlementMap(in);
    ASSERT_TRUE(m.ok()) << m.refusal;
    ASSERT_TRUE(m.wall.present) << m.wall.refusal;
    const TowerStats t = towerStats(m, in);
    printTowers("flat 160x120, tower_size 4", m, t);
    EXPECT_GE(t.entries, 3);
    EXPECT_EQ(t.built, 0) << "a 4-cube drum cannot hold a wall, a stair and a room (TowerForge refuses it) and was recorded built";
    EXPECT_EQ(t.noReason, 0);
    int saysTooSmall = 0;
    for (const auto& tw : m.wall.towers) if (tw.skipped.find("too small") != std::string::npos) ++saysTooSmall;
    EXPECT_GT(saysTooSmall, 0) << "no entry carries TowerForge's own refusal";

    SettlementMapInput in2 = inputFor("city", 160, 120, flatHeight, {});
    SettlementTierPreset off = *in2.tier; off.walls.towers = false; in2.tier = &off;
    const SettlementMap m2 = planSettlementMap(in2);
    ASSERT_TRUE(m2.ok()) << m2.refusal;
    ASSERT_TRUE(m2.wall.present);
    EXPECT_TRUE(m2.wall.towers.empty()) << "walls.towers is false and the record carries tower entries";
    EXPECT_FALSE(m2.toJson()["barriers"][0].contains("towers"));

    // a built tower whose record lost its door reads back NOT BUILT, with a reason (never built with a door at 0,0)
    const SettlementMap full = planSettlementMap(inputFor("city", 160, 120, flatHeight, {}));
    nlohmann::json j = full.toJson();
    int stripped = 0;
    if (j["barriers"][0].contains("towers"))
        for (auto& tj : j["barriers"][0]["towers"]) if (tj.value("built", false)) { tj.erase("door"); ++stripped; }
    EXPECT_GT(stripped, 0) << "no built tower to strip: the read-back rule is not exercised";
    const SettlementMap back = SettlementMap::fromJson(j);
    for (const auto& tw : back.wall.towers) {
        EXPECT_FALSE(tw.built) << "a tower with no door in its record read back as built";
        EXPECT_FALSE(tw.skipped.empty());
    }
    // the audit of M5d-3 (defect 5): a built tower whose record does not hold together reads back NOT BUILT
    const std::vector<std::pair<const char*, std::function<void(nlohmann::json&)>>> breaks = {
        {"the out cell not beside the door", [](nlohmann::json& tj) { tj["out"] = {tj["out"][0].get<int>() + 3, tj["out"][1].get<int>()}; }},
        {"the door outside the drum's square", [](nlohmann::json& tj) { tj["door"] = {tj["x"].get<int>() + 40, tj["z"].get<int>()}; tj["out"] = {tj["x"].get<int>() + 41, tj["z"].get<int>()}; tj["door_side"] = "E"; }},
        {"the corner outside the drum's square", [](nlohmann::json& tj) { tj["corner"] = {tj["x"].get<int>() + 40, tj["z"].get<int>()}; }},
        {"a side that is no side", [](nlohmann::json& tj) { tj["door_side"] = "Q"; }}};
    for (const auto& [whatBroke, breakIt] : breaks) {
        nlohmann::json jb = full.toJson();
        int broke = 0;
        for (auto& tj : jb["barriers"][0]["towers"]) if (tj.value("built", false)) { breakIt(tj); ++broke; }
        EXPECT_GT(broke, 0) << whatBroke;
        int stillBuilt = 0;
        for (const auto& tw : SettlementMap::fromJson(jb).wall.towers) if (tw.built) ++stillBuilt;
        EXPECT_EQ(stillBuilt, 0) << whatBroke << ": read back as built";
    }
    int goodBuilt = 0;   // the control: the untouched record keeps its towers
    for (const auto& tw : SettlementMap::fromJson(full.toJson()).wall.towers) if (tw.built) ++goodBuilt;
    EXPECT_GT(goodBuilt, 0);
}

// (d) STRESS, written after the first green (pm_chain233.txt PM5d.6): seeds and a walled town
TEST(SettlementMapTest, TheWallHoldsAcrossSeedsAndTiers) {
    ASSERT_TRUE(rigs().programOk);
    const auto all = wallRigs();
    int plans = 0, mapsRefused = 0, wallsAbsent = 0, towerEntries = 0, towersBuilt = 0;
    const auto one = [&](const std::string& what, const SettlementMapInput& in) {
        const SettlementMap m = planSettlementMap(in);
        ++plans;
        if (!m.ok()) { ++mapsRefused; std::printf("[ M5d-1 ] %s: the map is REFUSED: %s%s", what.c_str(), m.refusal.c_str(), "\n"); return; }
        const WallStats w = wallStats(m, in);
        const int mg = in.tier->walls.marginCubes;
        printWall(what.c_str(), m, w, static_cast<long>(in.W + 2 * mg) * (in.D + 2 * mg));
        EXPECT_TRUE(w.present) << what << ": no wall: " << m.wall.refusal;
        if (!w.present) { ++wallsAbsent; return; }
        expectWallSound(what.c_str(), w);
        const TowerStats ts = towerStats(m, in);   // M5d-3
        expectTowersSound(what.c_str(), ts);
        towerEntries += ts.entries; towersBuilt += ts.built;
    };
    for (const size_t ri : {size_t{3}, size_t{4}, size_t{5}})
        for (unsigned seed = 1; seed <= 10; ++seed) {
            const WallRig& r = all[ri];
            one(std::string(r.name) + " seed " + std::to_string(seed), inputFor("city", r.W, r.D, r.h, r.w, seed));
        }
    const SettlementTierPreset* city = rigs().program.get("medieval", "city");
    ASSERT_NE(city, nullptr);
    for (unsigned seed = 1; seed <= 5; ++seed) {
        SettlementMapInput in = inputFor("town", 140, 76, flatHeight, {}, seed);
        SettlementTierPreset walled = *in.tier; walled.walls = city->walls; in.tier = &walled;
        one("walled town 140x76 seed " + std::to_string(seed), in);
    }
    std::printf("[ M5d-1 ] stress: %d plans, %d maps refused, %d walls absent%s", plans, mapsRefused, wallsAbsent, "\n");
    EXPECT_EQ(plans, 35);
    EXPECT_LT(mapsRefused, plans) << "every map refused: nothing was measured";
    std::printf("[ M5d-3 ] stress: %d tower entries, %d built%s", towerEntries, towersBuilt, "\n");
    EXPECT_GT(towersBuilt, 0) << "no tower built over the stress plans: the tower checks measured nothing";
}

// (e) the audit of M5d-1 (bug A): a wall record whose nodes cannot be a ring must read back REFUSED - never
// present with an empty ring (a consumer would stamp nothing and say nothing)
TEST(SettlementMapTest, AWallRecordWithBadNodesIsRefusedNotEmpty) {
    ASSERT_TRUE(rigs().programOk);
    const SettlementMapInput in = inputFor("city", 160, 120, flatHeight, {});
    const SettlementMap m = planSettlementMap(in);
    ASSERT_TRUE(m.ok()) << m.refusal;
    ASSERT_TRUE(m.wall.present);
    const nlohmann::json good = m.toJson();
    const std::vector<std::pair<const char*, nlohmann::json>> bad = {
        {"a diagonal leg", nlohmann::json::parse("[[0,0],[3,4],[0,4]]")},
        {"two nodes", nlohmann::json::parse("[[0,0],[0,9]]")},
        {"one node", nlohmann::json::parse("[[5,5]]")},
        {"no nodes", nlohmann::json::array()}};
    for (const auto& [what, nodes] : bad) {
        nlohmann::json j = good;
        j["barriers"][0]["nodes"] = nodes;
        const SettlementMap back = SettlementMap::fromJson(j);
        std::printf("[ M5d-1 ] bad record (%s): present %d, ring %zu cells, refusal '%s'%s", what, back.wall.present ? 1 : 0, back.wall.ring.size(), back.wall.refusal.c_str(), "\n");
        EXPECT_FALSE(back.wall.present) << what << ": read back as present with " << back.wall.ring.size() << " ring cells";
        EXPECT_FALSE(back.wall.refusal.empty()) << what << ": no refusal says why";
    }
    const SettlementMap ok = SettlementMap::fromJson(good);   // the control: the good record reads back present
    EXPECT_TRUE(ok.wall.present);
    EXPECT_EQ(ok.wall.ring.size(), m.wall.ring.size());
}

// (f) M5d-1b (chain 253, live: the city sealed its arriving road): the world road that runs on out of the site gets a gate
TEST(SettlementMapTest, TheWallGatesTheArrivingRoad) {
    ASSERT_TRUE(rigs().programOk);
    SettlementMapInput in = inputFor("city", 140, 60, flatHeight, {});
    in.entries = {{126, 38, 2}};                                                       // the road's END, inside the site (as the live request states it)
    in.roadAt = [](int x, int z) { return (x >= 126 && z >= 36 && z <= 40) ? 2 : 0; };   // and the road itself, running on east out of the site
    const SettlementMap m = planSettlementMap(in);
    ASSERT_TRUE(m.ok()) << m.refusal;
    const WallStats w = wallStats(m, in);
    printWall("flat 140x60, the road leaves east from 126,38", m, w, static_cast<long>(140 + 4) * (60 + 4));
    ASSERT_TRUE(w.present) << m.wall.refusal;
    EXPECT_GT(w.roadCells, 0) << "the ring never meets the road: nothing below is measured";
    expectWallSound("flat 140x60, the road leaves east", w);
    int roadGates = 0;
    for (const auto& g : m.wall.gates) if (g.way < 0) ++roadGates;
    EXPECT_EQ(roadGates, 1) << "one road crosses the ring once";
    // the control: the same site without the road sampler plans no road gate
    const auto strip = in.roadAt;
    in.roadAt = nullptr;
    const SettlementMap off = planSettlementMap(in);
    for (const auto& g : off.wall.gates) EXPECT_GE(g.way, 0) << "a road gate without a road";
    // the audit of M5d-1b: the control must still STAND on the strip (else "no road gate" says nothing), and with the
    // strip handed to the measurement only, those cells must read ungated - the rule is what gates them
    SettlementMapInput measured = in; measured.roadAt = strip;
    const WallStats c = wallStats(off, measured);
    std::printf("[ M5d-1 ] the control (no road sampler): %d ring cells on the strip, %d of them ungated%s", c.roadCells, c.roadUngated, "\n");
    EXPECT_GT(c.roadCells, 0) << "the control's ring does not cross the strip";
    EXPECT_GT(c.roadUngated, 0) << "the control's ring is gated on the strip without the rule";
}

// (g) the audit of M5d-1b, condition 1: a road that runs ALONG the ring past one of the settlement's own gates - every
// ring cell on it is inside some gate's opening, not just the stretch that own gate covers
TEST(SettlementMapTest, TheWallGatesARoadThatRunsAlongIt) {
    ASSERT_TRUE(rigs().programOk);
    SettlementMapInput in = inputFor("city", 140, 60, flatHeight, {});
    in.roadAt = [](int x, int z) { return (x >= 50 && x <= 100 && z <= -3) ? 2 : 0; };   // everything south of the lots between x 50 and 100: no way round it
    const SettlementMap m = planSettlementMap(in);
    ASSERT_TRUE(m.ok()) << m.refusal;
    const WallStats w = wallStats(m, in);
    printWall("flat 140x60, a road along the south wall", m, w, static_cast<long>(140 + 4) * (60 + 4));
    ASSERT_TRUE(w.present) << m.wall.refusal;
    EXPECT_GT(w.roadCells, 14) << "the ring does not run along the road for longer than two gate widths: the rig does not make the case";
    expectWallSound("flat 140x60, a road along the south wall", w);
}

// ---- M5e-1: the market as a place on the map ---------------------------------------------------------------------
namespace {
struct MarketStats {
    int markets = 0, wrongSize = 0, outside = 0, wet = 0, primaryCells = 0, relief = 0, cellsInALot = 0;
    int props = 0, stalls = 0, wells = 0, statues = 0, propCellsOnAWay = 0, propCellsOnFullBand = 0, propsOutside = 0, propsOverlapping = 0;
    Rect rect{0, 0, 0, 0};
};
// independent of the planner: a way's band is its centreline sampled between nodes, floor(width / 2) cells each side
std::set<std::pair<int, int>> bandCells(const SettlementMap& m, bool primaryOnly, int widthCap = 0) {
    std::set<std::pair<int, int>> out;
    for (const auto& w : m.ways) {
        if (primaryOnly && w.highway != "primary") continue;
        const int r = static_cast<int>(std::floor((widthCap > 0 ? std::min(w.width, static_cast<float>(widthCap)) : w.width) / 2.0f));
        for (size_t i = 0; i + 1 < w.nodes.size(); ++i) {
            const MapNode* a = m.node(w.nodes[i]); const MapNode* b = m.node(w.nodes[i + 1]);
            if (!a || !b) continue;
            const int n = std::max({std::abs(b->x - a->x), std::abs(b->z - a->z), 1});
            for (int t = 0; t <= n; ++t) {
                const int cx = a->x + static_cast<int>(std::lround(static_cast<float>(b->x - a->x) * t / n)), cz = a->z + static_cast<int>(std::lround(static_cast<float>(b->z - a->z) * t / n));
                for (int dx = -r; dx <= r; ++dx) for (int dz = -r; dz <= r; ++dz) out.insert({cx + dx, cz + dz});
            }
        }
    }
    return out;
}
MarketStats marketStats(const SettlementMap& m, const SettlementMapInput& in) {
    MarketStats k;
    // D67 PROPOSED (changed BEFORE chain 303, stated in pm_chain299.txt): what the dressing keeps clear is each way's
    // THROUGH LANE - the tier's lane width, read from the PROGRAM here, not from the map. The full band stays counted.
    const auto primary = bandCells(m, true), ways = bandCells(m, false, in.tier->street.laneWidth), fullWays = bandCells(m, false);
    for (const auto& a : m.areas) {
        if (a.landuse != "market") continue;
        ++k.markets; k.rect = a.rect;
        if (a.rect.w != in.tier->pub.marketW || a.rect.d != in.tier->pub.marketD) ++k.wrongSize;
        int lo = INT_MAX, hi = INT_MIN;
        for (int z = a.rect.z; z < a.rect.z1(); ++z) for (int x = a.rect.x; x < a.rect.x1(); ++x) {
            if (x < 0 || z < 0 || x >= m.W || z >= m.D) { ++k.outside; continue; }
            if (in.waterAt && in.waterAt(x, z)) ++k.wet;
            if (primary.count({x, z})) ++k.primaryCells;
            const int h = in.heightAt(x, z); lo = std::min(lo, h); hi = std::max(hi, h);
            for (const auto& l : m.lots) { const Rect pr = l.parcelRect(); if (x >= pr.x && x < pr.x1() && z >= pr.z && z < pr.z1()) { ++k.cellsInALot; break; } }
        }
        if (hi >= lo) k.relief = hi - lo;
    }
    if (k.markets != 1) return k;
    const MapProjection p = projectMap(m);
    const SquareDressing d = planSquareDressing(p.msl, in.tier->pub, in.seed);
    std::set<std::pair<int, int>> used;
    for (const auto& pr : d.props) {
        ++k.props;
        const bool statue = pr.type.rfind("statue", 0) == 0;
        if (statue) ++k.statues; else if (pr.type == "well") ++k.wells; else ++k.stalls;
        bool out = false, over = false;
        for (int z = pr.cz; z < pr.cz + pr.d; ++z) for (int x = pr.cx; x < pr.cx + pr.w; ++x) {
            if (x < k.rect.x || x >= k.rect.x1() || z < k.rect.z || z >= k.rect.z1()) out = true;
            if (!used.insert({x, z}).second) over = true;
            if (!statue && ways.count({x, z})) ++k.propCellsOnAWay;   // the statue stands at the market cross by design
            if (!statue && fullWays.count({x, z})) ++k.propCellsOnFullBand;
        }
        k.propsOutside += out; k.propsOverlapping += over;
    }
    return k;
}
void printMarket(const std::string& what, const MarketStats& k) {
    std::printf("[ M5e-1 ] %s: %d market(s) %d x %d at (%d, %d): wrong size %d, cells outside %d, wet %d, in a lot %d, on the primary way %d, relief %d | "
                "dressing %d props (%d stalls, %d wells, %d statues): cells on a through lane %d (in a way's full width %d), props outside %d, overlapping %d\n",
                what.c_str(), k.markets, k.rect.w, k.rect.d, k.rect.x, k.rect.z, k.wrongSize, k.outside, k.wet, k.cellsInALot, k.primaryCells, k.relief,
                k.props, k.stalls, k.wells, k.statues, k.propCellsOnAWay, k.propCellsOnFullBand, k.propsOutside, k.propsOverlapping);
}
struct MarketRig { std::string name; const char* tier; int W, D; std::function<int(int, int)> h; std::function<bool(int, int)> w; };
std::vector<MarketRig> marketRigs() {
    std::vector<MarketRig> out{{"town flat 140x76", "town", 140, 76, flatHeight, {}},
                               {"town hill + creek 140x76", "town", 140, 76, hillHeight, hillWater},
                               {"town rolling 140x76", "town", 140, 76, slopeRolling, {}},
                               {"town flat, pond 60..80 x 30..46", "town", 140, 76, flatHeight, [](int x, int z) { return x >= 60 && x <= 80 && z >= 30 && z <= 46; }}};
    for (const WallRig& r : wallRigs()) out.push_back({std::string("city ") + r.name, "city", r.W, r.D, r.h, r.w});
    return out;
}
}  // namespace

// (a) a tier whose program asks for a market square carries exactly ONE market area: the program's size, inside the
// site, dry, off every parcel, with the primary way through it. A tier that asks for none carries none (the control).
TEST(SettlementMapTest, ATierWithAMarketCarriesOneOnItsPrimaryWay) {
    ASSERT_TRUE(rigs().programOk);
    int plans = 0, refused = 0, measured = 0, unplaced = 0;
    for (const MarketRig& r : marketRigs())
        for (unsigned seed = 1; seed <= 5; ++seed) {
            const SettlementMapInput in = inputFor(r.tier, r.W, r.D, r.h, r.w, seed);
            ASSERT_GT(in.tier->pub.marketW, 0) << r.tier << ": the program asks for no market: every check below is vacuous";
            const SettlementMap m = planSettlementMap(in);
            ++plans;
            const std::string what = r.name + " seed " + std::to_string(seed);
            if (!m.ok()) { ++refused; std::printf("[ M5e-1 ] %s: the map is REFUSED: %s%s", what.c_str(), m.refusal.c_str(), "\n"); continue; }
            const MarketStats k = marketStats(m, in);
            printMarket(what, k);
            ++measured;
            // amended BEFORE chain 300 (stated in pm_chain299.txt): a market that has no dry place is NOT placed and the
            // map says why - that is the design check's wording; a silent absence stays red
            // the audit of M5e-1 (2): "no market + a note" summed over the rigs was a loophole - one rig could lose its
            // market in silence. Every one of THESE rigs has a dry place: a market is required on each plan. The
            // not-placed branch has its own rig below.
            if (k.markets == 0) { ++unplaced; ADD_FAILURE() << what << ": no market on a rig that has a dry place for one (" << m.marketNote << ")"; continue; }
            if (!m.marketNote.empty()) std::printf("[ M5e-1 ] %s: note: %s%s", what.c_str(), m.marketNote.c_str(), "\n");
            EXPECT_EQ(k.markets, 1) << what << ": the program asks for a market square and the map carries " << k.markets;
            if (k.markets != 1) continue;
            EXPECT_EQ(k.wrongSize, 0) << what << ": the market is not the program's size";
            EXPECT_EQ(k.outside, 0) << what << ": market cells outside the site";
            EXPECT_EQ(k.wet, 0) << what << ": water cells inside the market";
            EXPECT_EQ(k.cellsInALot, 0) << what << ": market cells inside a parcel";
            EXPECT_GT(k.primaryCells, 0) << what << ": the primary way does not pass through the market";
        }
    EXPECT_LT(unplaced, measured) << "no market placed on any plan: nothing was measured";
    std::printf("[ M5e-1 ] markets: %d plans, %d refused, %d measured, %d not placed%s", plans, refused, measured, unplaced, "\n");
    EXPECT_GT(measured, 0) << "every map refused: nothing was measured";
    {   // the not-placed branch (never seen on the 55 plans): water flanks the main street on both sides, six cells off
        // its row, with dry gaps narrower than the market - every 12 x 12 rect on the street holds water
        // RIG CHANGE 2026-11-17 (pm_chain364.txt, chains 378-382): the site was 140 wide. Since every way keeps a
        // clearance from water each 6-wide gap holds ONE lane and the lot behind it plugs it - 7 gaps a side, 14 lots,
        // one under the town's minimum, refused. 180 wide gives 9 gaps a side. Rows, gaps and assertions unchanged.
        const int D = 76, kFlankW = 180;
        const auto flanked = [D](int x, int z) { return (z == D / 2 - 6 || z == D / 2 + 6) && (x % 20) >= 6; };
        for (unsigned seed = 1; seed <= 3; ++seed) {
            const SettlementMapInput in = inputFor("town", kFlankW, D, flatHeight, flanked, seed);
            const SettlementMap m = planSettlementMap(in);
            const std::string what = "town flat, water flanking the street, seed " + std::to_string(seed);
            if (!m.ok()) { ADD_FAILURE() << what << ": the map is refused (" << m.refusal << "): the rig does not make the case"; continue; }
            const MarketStats k = marketStats(m, in);
            printMarket(what, k);
            std::printf("[ M5e-1 ] %s: note: %s%s", what.c_str(), m.marketNote.c_str(), "\n");
            EXPECT_EQ(k.markets, 0) << what << ": a market was placed (wet " << k.wet << "): the rig does not make the case, or the rule let water in";
            EXPECT_EQ(k.wet, 0) << what;
            EXPECT_NE(m.marketNote.find("no dry place"), std::string::npos) << what << ": no market and the map does not say why";
        }
    }
    for (const char* tier : {"hamlet", "village"}) {   // the control: no market asked, none carried
        const SettlementMapInput in = inputFor(tier, 96, 64, flatHeight, {});
        ASSERT_EQ(in.tier->pub.marketW, 0) << tier << " now asks for a market: it is no longer the control";
        const SettlementMap m = planSettlementMap(in);
        ASSERT_TRUE(m.ok()) << tier << ": " << m.refusal;
        EXPECT_EQ(marketStats(m, in).markets, 0) << tier << ": a market on a tier that asks for none";
    }
}

// (b) the market's dressing (the stalls and the well; the statue stands at the cross by design) is planned from the
// map's projection and must keep off every way's band, stay inside the market and not overlap itself
TEST(SettlementMapTest, TheMarketsDressingKeepsOffTheWays) {
    ASSERT_TRUE(rigs().programOk);
    int measured = 0, props = 0, stalls = 0, asked = 0, wells = 0;
    for (const MarketRig& r : marketRigs())
        for (unsigned seed = 1; seed <= 5; ++seed) {
            const SettlementMapInput in = inputFor(r.tier, r.W, r.D, r.h, r.w, seed);
            const SettlementMap m = planSettlementMap(in);
            if (!m.ok()) continue;
            const MarketStats k = marketStats(m, in);
            if (k.markets != 1) continue;
            const std::string what = r.name + " seed " + std::to_string(seed);
            ++measured; props += k.props; stalls += k.stalls; asked += in.tier->pub.stalls;
            EXPECT_EQ(k.propCellsOnAWay, 0) << what << ": a stall or the well is planned on a way's through lane (" << k.propCellsOnAWay << " cells)";
            EXPECT_EQ(k.propsOutside, 0) << what << ": dressing outside the market";
            EXPECT_EQ(k.propsOverlapping, 0) << what << ": dressing props overlap";
            // added BEFORE chain 301 (pm_chain299.txt PM.5): chain 300 showed a town whose square took the well and planned none
            if (in.tier->pub.well) EXPECT_EQ(k.wells, 1) << what << ": the program asks for a well and the square's dressing plans " << k.wells;
            wells += k.wells;
        }
    std::printf("[ M5e-1 ] wells: %d over %d markets%s", wells, measured, "\n");
    std::printf("[ M5e-1 ] dressing: %d markets measured, %d props, %d stalls of %d asked%s", measured, props, stalls, asked, "\n");
    EXPECT_GT(measured, 0) << "no market measured";
    EXPECT_GT(stalls, 0) << "no stall planned on any market: the way check measured nothing";
}

// (c) the audit of M5e-1 (7): the plaza is paved WITHOUT moving a way's column (the map states the ways' levels and the
// doors are seated on them), every plaza column is paved, and no step inside the plaza or onto a way exceeds the walker's
namespace {
int groundMicroFrom(const std::function<int(int, int)>& cubeTop, int mx, int mz) { return cubeTop(mx / 9, mz / 9) * 9 + 8; }   // positive coordinates only
}
TEST(SettlementMapTest, ThePlazaIsPavedWithoutMovingTheWays) {
    struct G { const char* name; std::function<int(int, int)> h; };
    const std::vector<G> grounds{{"flat", flatHeight}, {"hill 5 %", hillHeight}, {"rolling", slopeRolling}, {"a 4-cube bench", bandHeight}};
    const AgentBox box;
    const std::vector<WayRibbon> ways{{{{10, 20}, {40, 20}}, 7}, {{{25, 5}, {25, 35}}, 3}};
    const glm::ivec4 plaza(19, 14, 12, 12);   // x, z, w, d in cubes: both ways cross it
    int measured = 0;
    for (const G& g : grounds) {
        const std::function<int(int, int)> gm = [&g](int mx, int mz) { return groundMicroFrom(g.h, mx, mz); };
        const PavingPlan a = planWayPaving(ways, gm, box, "Cobblestone");
        const PavingPlan b = planWayPaving(ways, gm, box, "Cobblestone", {plaza});
        std::map<std::pair<int, int>, int> A, B;
        for (const auto& c : a.columns) A[{c.x, c.z}] = c.surface;
        for (const auto& c : b.columns) B[{c.x, c.z}] = c.surface;
        int moved = 0, lost = 0, missing = 0, steps = 0, worst = 0, plazaOnly = 0, fill = 0, cut = 0;
        for (const auto& [k, S] : A) { const auto it = B.find(k); if (it == B.end()) ++lost; else if (it->second != S) ++moved; }
        const auto inPlaza = [&](int x, int z) { return x >= plaza.x * 9 && x < (plaza.x + plaza.z) * 9 && z >= plaza.y * 9 && z < (plaza.y + plaza.w) * 9; };
        for (int z = plaza.y * 9; z < (plaza.y + plaza.w) * 9; ++z) for (int x = plaza.x * 9; x < (plaza.x + plaza.z) * 9; ++x) {
            const auto it = B.find({x, z});
            if (it == B.end()) { ++missing; continue; }
            if (!A.count({x, z})) { ++plazaOnly; const int t = gm(x, z); if (it->second > t) ++fill; else if (it->second < t) ++cut; }
            for (const auto& d : {std::pair<int, int>{1, 0}, std::pair<int, int>{0, 1}, std::pair<int, int>{-1, 0}, std::pair<int, int>{0, -1}}) {
                const auto jt = B.find({x + d.first, z + d.second});
                if (jt == B.end()) continue;
                if (inPlaza(x + d.first, z + d.second) && (d.first < 0 || d.second < 0)) continue;   // each inner pair once
                const int st = std::abs(jt->second - it->second);
                worst = std::max(worst, st);
                if (st > box.maxStepUpMicro) ++steps;
            }
        }
        std::printf("[ M5e-1 ] plaza on %s: %zu way columns, %d moved, %d lost; plaza %d columns of %d (%d missing; %d fill, %d cut); steps over the walker's %d micro: %d (worst %d)%s",
                    g.name, A.size(), moved, lost, plazaOnly, plaza.z * plaza.w * 81, missing, fill, cut, box.maxStepUpMicro, steps, worst, "\n");
        if (A.empty()) { std::printf("[ M5e-1 ] plaza on %s: the ways pave nothing here - not measured%s", g.name, "\n"); continue; }
        ++measured;
        EXPECT_EQ(moved, 0) << g.name << ": the plaza moved a way's column";
        EXPECT_EQ(lost, 0) << g.name;
        EXPECT_EQ(missing, 0) << g.name << ": plaza columns not paved";
        EXPECT_GT(plazaOnly, 0) << g.name << ": the plaza added nothing - the rig does not make the case";
        EXPECT_EQ(steps, 0) << g.name << ": a step inside the plaza or onto a way that the walker cannot take";
    }
    EXPECT_GE(measured, 2);
}

// ---- M5f-1 (D9): the era hook, falsified with a FIXTURE era ---------------------------------------------------------
// The fixture is not a real era and grounds nothing: it exists to differ from the medieval data in every way the
// planner reads - a typology name the medieval canon lacks, a footprint that is not the 8 x 6 stand-in, three
// storeys, the gable to the street - and one typology that SHARES a medieval name with other numbers.
namespace {
struct FixtureEra {
    SettlementProgramRegistry program;
    RoomProgramRegistry rooms;
    bool ok = false;
    std::string why;
    explicit FixtureEra(const nlohmann::json& weights) {
        std::ifstream f("resources/settlement_program.json");
        if (!f.is_open()) { why = "settlement_program.json not found"; return; }
        nlohmann::json j; f >> j;
        nlohmann::json tier = j["eras"]["medieval"]["tiers"]["hamlet"];
        tier["typology_weights"] = weights;
        tier["crofts"] = nlohmann::json::object();
        tier["plot"]["depth_min"] = 18; tier["plot"]["depth_max"] = 20;
        nlohmann::json era;
        era["eras"]["fixture"]["tiers"]["hamlet"] = tier;
        if (!program.loadFromJson(era)) { why = "the fixture era did not load"; return; }
        if (!rooms.loadFromFile("resources/room_program.json")) { why = "room_program.json did not load"; return; }
        const nlohmann::json room = nlohmann::json::array({nlohmann::json{{"id", "hall"}, {"purpose", "hall"}, {"bays", 1.0}}});
        const nlohmann::json pack = {{"period", "fixture"}, {"programs", {
            {"blockhouse", {{"description", "TEST FIXTURE"}, {"source", "TEST FIXTURE - not a grounded program"}, {"width_min", 7.0}, {"width_max", 7.0},
                            {"bay_length", 3.0}, {"bays", 5}, {"stories", 3}, {"entrance", "gable"}, {"rooms", room}}},
            {"croft", {{"description", "TEST FIXTURE"}, {"source", "TEST FIXTURE - not a grounded program"}, {"width_min", 5.0}, {"width_max", 5.0},
                       {"bay_length", 3.0}, {"bays", 2}, {"stories", 2}, {"entrance", "long_wall"}, {"rooms", room}}}}}};
        const std::string tmp = "test_fixture_era_pack.json";
        { std::ofstream out(tmp); out << pack.dump(2); }
        const bool loaded = rooms.loadPeriodPack(tmp, "fixture");
        std::remove(tmp.c_str());
        if (!loaded) { why = "the fixture room pack did not load"; return; }
        ok = true;
    }
    SettlementMapInput input(unsigned seed) const {
        SettlementMapInput in;
        in.tier = program.get("fixture", "hamlet");
        in.rooms = &rooms; in.era = "fixture";
        in.seed = seed; in.W = 80; in.D = 64;
        in.heightAt = flatHeight; in.waterAt = {};
        in.entries = {{0, 32, 1}};
        return in;
    }
};
}  // namespace

// pm_chain363.txt: M5's contracted stress sites, headless - a 15-degree slope, a site bisected by an order-3 river, a
// site on a coast (the 400-lot cap is BakeTimeIsReportedAndTheGrowthReachesManyLots). The invariants are asserted on
// every plan; whether a plan SUCCEEDS is printed, not asserted - this is the first time any of them is run.
TEST(SettlementMapTest, TheContractsStressSites) {
    ASSERT_TRUE(rigs().programOk);
    RoomProgramRegistry romanRooms;
    ASSERT_TRUE(romanRooms.loadFromFile("resources/room_program.json"));
    ASSERT_TRUE(romanRooms.loadPeriodPack("resources/room_programs/roman.json", "roman"));
    struct Case { const char* name; std::function<int(int, int)> h; std::function<std::function<bool(int, int)>(int, int)> water; };
    // the band a way paves: every cell within round(width) / 2 of its centre line (the planner's own radius), in water
    auto bandCellsInWater = [](const SettlementMap& m, const std::function<bool(int, int)>& waterAt, int W, int D) {
        if (!waterAt) return 0;
        std::set<std::pair<int, int>> wet;
        for (const auto& w : m.ways) {
            const int r = std::max(0, static_cast<int>(std::lround(w.width)) / 2);
            for (size_t k = 0; k + 1 < w.nodes.size(); ++k) {
                const MapNode* a = m.node(w.nodes[k]); const MapNode* b = m.node(w.nodes[k + 1]);
                if (!a || !b) continue;
                const int steps = std::max(std::abs(b->x - a->x), std::abs(b->z - a->z));
                for (int st = 0; st <= steps; ++st) {
                    const int cx = a->x + (steps ? (b->x - a->x) * st / steps : 0), cz = a->z + (steps ? (b->z - a->z) * st / steps : 0);
                    for (int dz = -r; dz <= r; ++dz) for (int dx = -r; dx <= r; ++dx) {
                        if (dx * dx + dz * dz > r * r) continue;
                        const int x = cx + dx, z = cz + dz;
                        if (x < 0 || z < 0 || x >= W || z >= D) continue;
                        if (waterAt(x, z)) wet.insert({x, z});
                    }
                }
            }
        }
        return static_cast<int>(wet.size());
    };
    const std::vector<Case> cases = {
        {"15 degrees", [](int x, int) { return 30 + static_cast<int>(std::floor(0.2679f * static_cast<float>(x))); },
                       [](int, int) { return std::function<bool(int, int)>(); }},
        {"order-3 river across the street", flatHeight,
                       [](int W, int) { return std::function<bool(int, int)>([W](int x, int) { return x >= W / 2 - 2 && x <= W / 2 + 2; }); }},
        {"coast (far 40 % water)", flatHeight,
                       [](int W, int) { return std::function<bool(int, int)>([W](int x, int) { return x * 10 >= W * 6; }); }},
        // PL.2 (the audit): a ragged shore - water over the far 60 % except a dry spit 3 wide along the street's line
        {"coast with a dry spit", flatHeight,
                       [](int W, int D) { return std::function<bool(int, int)>([W, D](int x, int z) { return x * 10 >= W * 4 && !(z >= D / 2 - 1 && z <= D / 2 + 1); }); }},
        // chain 370: the river close to the entry - a near bank that holds a street and few lots
        {"order-3 river near the entry", flatHeight,
                       [](int, int) { return std::function<bool(int, int)>([](int x, int) { return x >= 24 && x <= 28; }); }},
    };
    struct Site { const char* era; const char* tier; };
    int plans = 0, refused = 0, noReason = 0, bad = 0;
    int noDryRoute = 0, lotsAcrossTheRiver = 0, lotsInWater = 0, nodesInWater = 0;   // D69 (b), pm_chain364.txt
    int bandInWater = 0, otherReason = 0;   // PL.2 (the audit)
    for (const Case& c : cases)
        for (const Site st : {Site{"medieval", "hamlet"}, Site{"medieval", "village"}, Site{"medieval", "town"}, Site{"medieval", "city"}, Site{"roman", "hamlet"}}) {
            const WorldForgePlan::TierPreset rect = WorldForgePlan::tierPresetDerived(st.tier, st.era);
            for (unsigned seed = 1; seed <= 3; ++seed) {
                SettlementMapInput in = inputFor(st.tier, rect.width, rect.depth, c.h, c.water(rect.width, rect.depth), seed);
                if (std::string(st.era) == "roman") { in.tier = rigs().program.get("roman", "hamlet"); in.rooms = &romanRooms; in.era = "roman"; }
                ASSERT_NE(in.tier, nullptr) << st.era << "/" << st.tier;
                const SettlementMap m = planSettlementMap(in);
                const Violations v = measure(m, in.heightAt, in.maxRelief, in.waterAt);
                ++plans;
                if (!m.ok()) { ++refused; if (m.refusal.empty()) ++noReason; if (m.refusal.find("no dry route") != std::string::npos) ++noDryRoute;
                               if (m.refusal.find("tier's minimum") == std::string::npos && m.refusal.find("too shallow") == std::string::npos) ++otherReason; }
                const int bandWet = bandCellsInWater(m, in.waterAt, rect.width, rect.depth);
                bandInWater += bandWet;
                if (bandWet) std::printf("[ M5 stress ]   ^ next line: %d band cells over water%s", bandWet, "\n");
                if (in.waterAt)
                    for (const auto& l : m.lots) {
                        bool wet = false;
                        for (int z = l.footprint.z; z < l.footprint.z1() && !wet; ++z) for (int x = l.footprint.x; x < l.footprint.x1() && !wet; ++x) wet = in.waterAt(x, z);
                        if (wet) ++lotsInWater;
                        const bool nearRiver = std::string(c.name).find("near the entry") != std::string::npos;
                        if (std::string(c.name).find("river") != std::string::npos && l.footprint.x > (nearRiver ? 28 : rect.width / 2 + 2)) ++lotsAcrossTheRiver;
                    }
                if (in.waterAt)   // a way NODE in water (chain 367's defect was a node, not a segment)
                    for (const auto& nd : m.nodes) if (in.waterAt(nd.x, nd.z)) ++nodesInWater;
                const int wrong = v.overlaps + v.lotOutside + v.wayInWater + v.lotNoWay;
                bad += wrong;
                int bankA = 0, bankB = 0;   // lots on the entry side / the far side of x = W / 2 (the river case reads it)
                for (const auto& l : m.lots) { if (l.footprint.x + l.footprint.w / 2 < rect.width / 2) ++bankA; else ++bankB; }
                std::printf("[ M5 stress ] %-32s %s/%-7s %3d x %-3d seed %u: %2zu lots of target %2d (min %d; %d entry side, %d far side), %zu ways; overlaps %d outside %d wet %d no-way %d%s%s",
                            c.name, st.era, st.tier, rect.width, rect.depth, seed, m.lots.size(), m.targetLots, in.tier->buildingsMin, bankA, bankB, m.ways.size(),
                            v.overlaps, v.lotOutside, v.wayInWater, v.lotNoWay, m.ok() ? "" : (" REFUSED: " + m.refusal).c_str(), "\n");
            }
        }
    std::printf("[ M5 stress ] %d plans, %d refused (%d without a reason), %d invariant violations%s", plans, refused, noReason, bad, "\n");
    std::printf("[ M5 stress ] D69 (b): %d refused for want of a dry route, %d lots across the river, %d footprints in water%s", noDryRoute, lotsAcrossTheRiver, lotsInWater, "\n");
    EXPECT_EQ(noDryRoute, 0) << "plans refused for want of a dry route: D69 (b) settles the dry ground the road reaches";
    EXPECT_EQ(lotsAcrossTheRiver, 0) << "lots beyond a river the street cannot cross (no bridge is built)";
    EXPECT_EQ(lotsInWater, 0) << "footprints standing in water";
    std::printf("[ M5 stress ] way nodes in water: %d%s", nodesInWater, "\n");
    EXPECT_EQ(nodesInWater, 0) << "way nodes in water";
    // PL.2: the road arriving at the SOUTH edge of a coast (water over the far 40 % in z)
    for (const Site st : {Site{"medieval", "village"}, Site{"roman", "hamlet"}}) {
        const WorldForgePlan::TierPreset rect = WorldForgePlan::tierPresetDerived(st.tier, st.era);
        const int D = rect.depth;
        SettlementMapInput in = inputFor(st.tier, rect.width, rect.depth, flatHeight, std::function<bool(int, int)>([D](int, int z) { return z * 10 >= D * 6; }), 2);
        in.entries = {{rect.width / 2, 0, 1}};
        if (std::string(st.era) == "roman") { in.tier = rigs().program.get("roman", "hamlet"); in.rooms = &romanRooms; in.era = "roman"; }
        const SettlementMap m = planSettlementMap(in);
        const Violations v = measure(m, in.heightAt, in.maxRelief, in.waterAt);
        const int bandWet = bandCellsInWater(m, in.waterAt, rect.width, rect.depth);
        bandInWater += bandWet;
        if (!m.ok() && m.refusal.find("no dry route") != std::string::npos) ++noDryRoute;
        std::printf("[ M5 stress ] coast, road from the SOUTH edge   %s/%-7s %3d x %-3d seed 2: %2zu lots of target %2d, %zu ways; wet segments %d, band cells over water %d%s%s", st.era, st.tier,
                    rect.width, rect.depth, m.lots.size(), m.targetLots, m.ways.size(), v.wayInWater, bandWet, m.ok() ? "" : (" REFUSED: " + m.refusal).c_str(), "\n");
        EXPECT_EQ(v.wayInWater, 0);
    }
    {   // the "too shallow" refusal, where it occurs: water from 3 cubes in - no clear cell beyond the entry's column
        SettlementMapInput in = inputFor("village", 104, 64, flatHeight, std::function<bool(int, int)>([](int x, int) { return x >= 3; }), 1);
        const SettlementMap m = planSettlementMap(in);
        std::printf("[ M5 stress ] water from x = 3 (village): %s%s", m.ok() ? "PLANNED" : m.refusal.c_str(), "\n");
        EXPECT_FALSE(m.ok());
        EXPECT_NE(m.refusal.find("too shallow"), std::string::npos) << m.refusal;
    }
    std::printf("[ M5 stress ] PL.2: %d band cells over water in all, %d refusals with a reason that is neither the minimum nor too-shallow, %d no-dry-route%s", bandInWater, otherReason, noDryRoute, "\n");
    EXPECT_EQ(bandInWater, 0) << "cells of a way's paved band over water";
    EXPECT_EQ(otherReason, 0) << "refusals whose reason is neither the tier's minimum nor a too-shallow dry side";
    EXPECT_EQ(noDryRoute, 0) << "a south-edge coast refused for want of a dry route";
    EXPECT_EQ(noReason, 0) << "refusals without a reason";
    EXPECT_EQ(bad, 0) << "overlaps, lots outside the site, ways in water or lots without a way on a stress site";
}

// M5f-2b (pm_chain348.txt): the roman hamlet's site rectangle, derived as D43 (b) derived the medieval ones. Depth 88 =
// two plots of the retrieved 40 + the 6-wide road, rounded up (the inputs GROUNDED, the sum DESIGN); the width is
// MEASURED here: the smallest, from 40 in steps of 4, at which every flat seed 1..20 seats its target. The derived rect
// (WorldForgePlan::tierPresetDerived("hamlet", "roman")) must be at least that and seat them all.
TEST(SettlementMapTest, RomanSiteRectSeatsTheBand) {
    SettlementProgramRegistry program; RoomProgramRegistry rooms;
    ASSERT_TRUE(program.loadFromFile("resources/settlement_program.json"));
    ASSERT_TRUE(rooms.loadFromFile("resources/room_program.json"));
    const auto* tier = program.get("roman", "hamlet");
    ASSERT_NE(tier, nullptr);
    ASSERT_TRUE(rooms.loadPeriodPack("resources/room_programs/roman.json", "roman"));
    auto seatsAll = [&](int W, int D, int& short_, int& minSeated) {
        short_ = 0; minSeated = INT_MAX;
        for (unsigned seed = 1; seed <= 20; ++seed) {
            SettlementMapInput in;
            in.tier = tier; in.rooms = &rooms; in.era = "roman"; in.seed = seed; in.W = W; in.D = D;
            in.heightAt = flatHeight; in.waterAt = {};
            in.entries = {{0, D / 2, 1}};
            const SettlementMap m = planSettlementMap(in);
            const int seated = static_cast<int>(m.lots.size());
            minSeated = std::min(minSeated, seated);
            const int want = m.targetLots > 0 ? m.targetLots : tier->buildingsMin;
            if (!m.ok() || seated < want) ++short_;
        }
        return short_ == 0;
    };
    const int D0 = 88;
    int W = 40, sh = 0, mn = 0; bool found = false;
    for (; W <= 200; W += 4) if (seatsAll(W, D0, sh, mn)) { found = true; break; }
    std::printf("[ M5f-2b ] roman hamlet site: at depth %d the smallest width seating every target on 20 flat seeds: %s (band %d-%d)%s", D0,
                found ? std::to_string(W).c_str() : "none up to 200", tier->buildingsMin, tier->buildingsMax, "\n");
    ASSERT_TRUE(found);
    const WorldForgePlan::TierPreset derived = WorldForgePlan::tierPresetDerived("hamlet", "roman");
    int shortAtDerived = 0, minAtDerived = 0;
    const bool derivedOk = seatsAll(derived.width, derived.depth, shortAtDerived, minAtDerived);
    std::printf("[ M5f-2b ] roman hamlet site: DERIVED %d x %d seats the target on %d of 20 seeds (min seated %d)%s", derived.width, derived.depth, 20 - shortAtDerived, minAtDerived, "\n");
    EXPECT_GE(derived.depth, D0) << "the derived rect is shallower than two retrieved plots and the road";
    EXPECT_GE(derived.width, W) << "the derived rect is narrower than the measured smallest width " << W;
    EXPECT_TRUE(derivedOk) << "the derived rect " << derived.width << " x " << derived.depth << " does not seat the target on " << shortAtDerived << " of 20 flat seeds";
    // PL.9 (i): the medieval table through the new function, every tier, both spellings - the numbers as they stood
    // before M5f-2b (no pinned plan hash runs the civ path, so this and the live record lines are what hold it)
    struct Was { const char* tier; int w, d; };
    for (const Was t : {Was{"hamlet", 56, 44}, Was{"outpost", 56, 44}, Was{"village", 104, 64}, Was{"town", 140, 60}, Was{"city", 160, 160}}) {
        const WorldForgePlan::TierPreset med = WorldForgePlan::tierPresetDerived(t.tier, "medieval"), dflt = WorldForgePlan::tierPresetDerived(t.tier);
        EXPECT_EQ(med.width, t.w) << t.tier; EXPECT_EQ(med.depth, t.d) << t.tier;
        EXPECT_EQ(dflt.width, t.w) << t.tier; EXPECT_EQ(dflt.depth, t.d) << t.tier;
    }
    // PL.9 (ii): the roman era has no larger tier - a roman world's village / town / city sites keep the medieval
    // rects, and the program holds no roman tier of that name, so a build of one refuses ("unknown era/tier")
    for (const Was t : {Was{"village", 104, 64}, Was{"town", 140, 60}, Was{"city", 160, 160}}) {
        const WorldForgePlan::TierPreset r = WorldForgePlan::tierPresetDerived(t.tier, "roman");
        EXPECT_EQ(r.width, t.w) << t.tier; EXPECT_EQ(r.depth, t.d) << t.tier;
        EXPECT_EQ(program.get("roman", t.tier), nullptr) << "a roman " << t.tier << " tier exists - give it a rect of its own";
    }
    // PL.9 (iii): the hill rig (the 5 % hillside with the creek), the stress the design check promised
    for (unsigned seed = 1; seed <= 3; ++seed) {
        SettlementMapInput in;
        in.tier = tier; in.rooms = &rooms; in.era = "roman"; in.seed = seed; in.W = derived.width; in.D = derived.depth;
        in.heightAt = hillHeight; in.waterAt = hillWater;
        in.entries = {{0, derived.depth / 2, 1}};
        const SettlementMap m = planSettlementMap(in);
        const Violations v = measure(m, in.heightAt, in.maxRelief, in.waterAt);
        std::printf("[ M5f-2b ] hill roman %d x %d seed %u: %zu lots of target %d (min %d), %zu ways, %d wet segments%s%s", derived.width, derived.depth, seed, m.lots.size(),
                    m.targetLots, tier->buildingsMin, m.ways.size(), v.wayInWater, m.ok() ? "" : (" REFUSED: " + m.refusal).c_str(), "\n");
        EXPECT_TRUE(m.ok()) << "hill seed " << seed << ": " << m.refusal;
        EXPECT_GE(static_cast<int>(m.lots.size()), tier->buildingsMin) << "hill seed " << seed;
        EXPECT_EQ(v.wayInWater, 0) << "hill seed " << seed << ": a way crosses the creek";
    }
}

// M5f-2a (pm_chain332.txt): the SHIPPED second era. A Roman roadside settlement plans strip buildings from the shipped
// files alone (settlement_program.json eras.roman, room_programs/roman.json): the narrow side on the street, 8 x 18,
// one storey. The site is the village's 104 x 64 - a hamlet's 56 x 44 does not hold two rows of 18-long houses (O33).
TEST(SettlementMapTest, ARomanRoadsideSettlementPlansStripBuildingsGableOn) {
    SettlementProgramRegistry program; RoomProgramRegistry rooms;
    ASSERT_TRUE(program.loadFromFile("resources/settlement_program.json"));
    ASSERT_TRUE(rooms.loadFromFile("resources/room_program.json"));
    const auto* tier = program.get("roman", "hamlet");
    ASSERT_NE(tier, nullptr) << "the shipped settlement program has no era roman with a hamlet tier";
    ASSERT_TRUE(rooms.loadPeriodPack("resources/room_programs/roman.json", "roman")) << "the shipped roman room-program pack did not load";
    int plans = 0, lots = 0, wrongType = 0, wrongStories = 0, notGableOn = 0, noDoor = 0, doorNotOnShortSide = 0, fewest = 1 << 30;
    int onPrimary = 0, widestGap = 0, bothSides = 0;   // PL.5: the row (pm_chain332.txt)
    int overlapping = 0, controlNarrowest = 1 << 30;   // PL.7: the audits' conditions
    for (unsigned seed = 1; seed <= 8; ++seed) {
        SettlementMapInput in;
        in.tier = tier; in.rooms = &rooms; in.era = "roman";
        const WorldForgePlan::TierPreset romanRect = WorldForgePlan::tierPresetDerived("hamlet", "roman");   // M5f-2b: the era's own site
        in.seed = seed; in.W = romanRect.width; in.D = romanRect.depth;
        in.heightAt = flatHeight; in.waterAt = {};
        in.entries = {{0, romanRect.depth / 2, 1}};
        const SettlementMap m = planSettlementMap(in);
        if (!m.ok()) { std::printf("[ M5f-2a ] roman seed %u: REFUSED: %s%s", seed, m.refusal.c_str(), "\n"); continue; }
        ++plans;
        if (seed == 1) {   // the layout as data, one seed (chain 336 diagnostic: where the ways and the doors are)
            for (const auto& w : m.ways) {
                std::string pts;
                for (int nid : w.nodes) for (const auto& nd : m.nodes) if (nd.id == nid) pts += " (" + std::to_string(nd.x) + "," + std::to_string(nd.z) + ")";
                std::printf("[ M5f-2a ]   way %d %s width %.0f:%s%s", w.id, w.highway.c_str(), w.width, pts.c_str(), "\n");
            }
            for (const auto& l : m.lots)
                std::printf("[ M5f-2a ]   lot %d footprint [%d,%d,%d,%d] orientation %d door (%d,%d) way %d%s", l.id, l.footprint.x, l.footprint.z, l.footprint.w,
                            l.footprint.d, l.orientation, l.doorX, l.doorZ, l.way, "\n");
        }
        fewest = std::min(fewest, static_cast<int>(m.lots.size()));
        for (const auto& l : m.lots) {
            ++lots;
            if (l.typology != "strip_building") ++wrongType;
            if (l.stories != 1) ++wrongStories;
            // the street runs across the door's facing: orientation 0 / 2 faces +-Z, so the footprint's w lies along it
            const bool facesZ = (l.orientation == 0 || l.orientation == 2);
            const int along = facesZ ? l.footprint.w : l.footprint.d, back = facesZ ? l.footprint.d : l.footprint.w;
            if (along != 8 || back != 18) ++notGableOn;
            // "fronts the through road": the door stands within 9 cubes of the primary way's centre line and faces it.
            // 9 was set AFTER seeing chain 336 (the doors stood 8 from the centre line) - it pins what the planner
            // does, it is not derived. A lot's own `way` is its approach, so it is not the test. The road is ASSUMED
            // to run along x (this test's entry).
            if (l.hasDoor) {
                double best = 1e9;
                for (const auto& w : m.ways) {
                    if (w.highway != "primary") continue;
                    for (size_t k = 0; k + 1 < w.nodes.size(); ++k) {
                        const MapNode *a = nullptr, *b = nullptr;
                        for (const auto& nd : m.nodes) { if (nd.id == w.nodes[k]) a = &nd; if (nd.id == w.nodes[k + 1]) b = &nd; }
                        if (!a || !b) continue;
                        const double vx = b->x - a->x, vz = b->z - a->z, len2 = vx * vx + vz * vz;
                        double t = len2 > 0 ? ((l.doorX - a->x) * vx + (l.doorZ - a->z) * vz) / len2 : 0.0;
                        t = std::max(0.0, std::min(1.0, t));
                        best = std::min(best, std::hypot(l.doorX - (a->x + t * vx), l.doorZ - (a->z + t * vz)));
                    }
                }
                // the road runs along x here (the entry is on the x = 0 edge): a house facing it has orientation 0 or 2
                if (best <= 9.0 && facesZ) ++onPrimary;
            }
            if (!l.hasDoor) { ++noDoor; continue; }
            // the door's wall cell lies on the footprint edge that faces the street (a short side)
            const bool onFront = l.orientation == 0 ? l.doorZ == l.footprint.z + l.footprint.d - 1
                               : l.orientation == 2 ? l.doorZ == l.footprint.z
                               : l.orientation == 1 ? l.doorX == l.footprint.x + l.footprint.w - 1
                                                    : l.doorX == l.footprint.x;
            if (!onFront) ++doorNotOnShortSide;
        }
        std::printf("[ M5f-2a ] roman seed %u: %zu lots, %zu ways; first lot %s %d x %d orientation %d, %d storey(s)%s", seed, m.lots.size(), m.ways.size(),
                    m.lots.empty() ? "-" : m.lots[0].typology.c_str(), m.lots.empty() ? 0 : m.lots[0].footprint.w, m.lots.empty() ? 0 : m.lots[0].footprint.d,
                    m.lots.empty() ? -1 : m.lots[0].orientation, m.lots.empty() ? 0 : m.lots[0].stories, "\n");
        // the rows: on each side of the road (by the door's facing) the footprints in x order, and the gaps between neighbours
        int sides = 0, seedGap = 0;
        for (const int o : {0, 2}) {
            std::vector<std::pair<int, int>> spans;
            for (const auto& l : m.lots) if (l.orientation == o) spans.push_back({l.footprint.x, l.footprint.x + l.footprint.w});
            if (spans.empty()) continue;
            ++sides;
            std::sort(spans.begin(), spans.end());
            for (size_t k = 0; k + 1 < spans.size(); ++k) seedGap = std::max(seedGap, spans[k + 1].first - spans[k].second);
        }
        if (sides == 2) ++bothSides;
        widestGap = std::max(widestGap, seedGap);
        // PL.7: no two footprints overlap - all pairs, whatever their orientation (a negative gap passed the row check)
        for (size_t a = 0; a < m.lots.size(); ++a)
            for (size_t b = a + 1; b < m.lots.size(); ++b) {
                const Rect &fa = m.lots[a].footprint, &fb = m.lots[b].footprint;
                if (fa.x < fb.x1() && fb.x < fa.x1() && fa.z < fb.z1() && fb.z < fa.z1()) ++overlapping;
            }
        // PL.7, the CONTROL: the same tier with the sociability measure put back to the centre must NOT make rows
        // (chain 338: gaps of 7) - the row assertion below is one that fails without the edge measure
        {
            SettlementTierPreset centre = *tier;
            centre.bells.socialFromEdge = false;
            SettlementMapInput cin = in; cin.tier = &centre;
            const SettlementMap cm = planSettlementMap(cin);
            int cGap = 0;
            for (const int o : {0, 2}) {
                std::vector<std::pair<int, int>> spans;
                for (const auto& l : cm.lots) if (l.orientation == o) spans.push_back({l.footprint.x, l.footprint.x + l.footprint.w});
                std::sort(spans.begin(), spans.end());
                for (size_t k = 0; k + 1 < spans.size(); ++k) cGap = std::max(cGap, spans[k + 1].first - spans[k].second);
            }
            controlNarrowest = std::min(controlNarrowest, cGap);
            std::printf("[ M5f-2a ]   seed %u control (centre measure): %zu lots, widest gap %d%s", seed, cm.lots.size(), cGap, "\n");
        }
        std::printf("[ M5f-2a ]   seed %u rows: %d side(s) of the road in use, widest gap between neighbours %d%s", seed, sides, seedGap, "\n");
    }
    std::printf("[ M5f-2a ] roman roadside settlements: %d plans of 8, %d lots (fewest %d); wrong typology %d, not one storey %d, not 8 along x 18 back %d, no door %d, door not on the street's short side %d%s",
                plans, lots, plans ? fewest : 0, wrongType, wrongStories, notGableOn, noDoor, doorNotOnShortSide, "\n");
    // PL.10 / PL.11 (pm_chain348.txt): the same plans with the ONE entry at the site's corner (0, 4), where the live
    // roman site's entry was (chain 350: 7 houses in the row, 4 strays). The street must still carry every house.
    int cornerSideOn = 0, cornerGap = 0, cornerRefused = 0, cornerOffStreet = 0, cornerExtraWays = 0, cornerUnderMin = 0;
    for (unsigned seed = 1; seed <= 8; ++seed) {
        const WorldForgePlan::TierPreset rr = WorldForgePlan::tierPresetDerived("hamlet", "roman");
        SettlementMapInput in;
        in.tier = tier; in.rooms = &rooms; in.era = "roman"; in.seed = seed; in.W = rr.width; in.D = rr.depth;
        in.heightAt = flatHeight; in.waterAt = {};
        in.entries = {{0, 4, 1}};
        const SettlementMap m = planSettlementMap(in);
        int acrossX = 0, sideOn = 0;
        for (const auto& l : m.lots) { if (l.orientation == 0 || l.orientation == 2) ++acrossX; else ++sideOn; }
        cornerSideOn += sideOn;   // printed: houses on the street's edge leg face +-X (the street has a corner)
        if (static_cast<int>(m.lots.size()) < tier->buildingsMin) ++cornerUnderMin;   // PL.12 (a): not vacuous
        int north = 0, south = 0;   // PL.12 (c), printed: the two sides of the middle run (houses facing +-Z)
        for (const auto& l : m.lots) { if (l.orientation == 0) ++north; else if (l.orientation == 2) ++south; }
        std::printf("[ M5f-2b ]   corner seed %u: %d on the edge leg, %d and %d on the two sides of the middle run%s", seed, sideOn, north, south, "\n");
        if (!m.ok()) ++cornerRefused;
        cornerExtraWays += static_cast<int>(m.ways.size()) - 1;   // a way beyond the street = a lane of a lot's own
        // every door within 9 cubes of the street's line, whichever leg
        for (const auto& l : m.lots) {
            double best = 1e9;
            for (const auto& w : m.ways) {
                if (w.highway != "primary") continue;
                for (size_t k = 0; k + 1 < w.nodes.size(); ++k) {
                    const MapNode *a = nullptr, *b = nullptr;
                    for (const auto& nd : m.nodes) { if (nd.id == w.nodes[k]) a = &nd; if (nd.id == w.nodes[k + 1]) b = &nd; }
                    if (!a || !b) continue;
                    const double vx = b->x - a->x, vz = b->z - a->z, len2 = vx * vx + vz * vz;
                    double t = len2 > 0 ? ((l.doorX - a->x) * vx + (l.doorZ - a->z) * vz) / len2 : 0.0;
                    t = std::max(0.0, std::min(1.0, t));
                    best = std::min(best, std::hypot(l.doorX - (a->x + t * vx), l.doorZ - (a->z + t * vz)));
                }
            }
            if (!l.hasDoor || best > 9.0) ++cornerOffStreet;
        }
        // neighbours facing the same way: along x for +-Z, along z for +-X
        for (int o = 0; o < 4; ++o) {
            std::vector<std::pair<int, int>> spans;
            for (const auto& l : m.lots) if (l.orientation == o) spans.push_back((o == 0 || o == 2) ? std::make_pair(l.footprint.x, l.footprint.x + l.footprint.w)
                                                                                                    : std::make_pair(l.footprint.z, l.footprint.z + l.footprint.d));
            std::sort(spans.begin(), spans.end());
            for (size_t k = 0; k + 1 < spans.size(); ++k) cornerGap = std::max(cornerGap, spans[k + 1].first - spans[k].second);
        }
        std::string prim;
        for (const auto& w : m.ways) if (w.highway == "primary") for (int nid : w.nodes) for (const auto& nd : m.nodes) if (nd.id == nid) prim += " (" + std::to_string(nd.x) + "," + std::to_string(nd.z) + ")";
        std::printf("[ M5f-2b ] corner entry (0,4) seed %u: %zu lots (%d facing across x, %d side-on), %zu ways; primary:%s%s%s", seed, m.lots.size(), acrossX, sideOn,
                    m.ways.size(), prim.c_str(), m.ok() ? "" : (" REFUSED: " + m.refusal).c_str(), "\n");
    }
    std::printf("[ M5f-2a ] the row: %d of %d lots face the through road within 9 cubes of its centre line; widest gap between neighbours %d; both sides in use on %d of %d plans (printed, not asserted)%s",
                onPrimary, lots, widestGap, bothSides, plans, "\n");
    EXPECT_EQ(cornerRefused, 0) << "corner-entry plans refused";
    // chain 357 -> 358 (stated in pm_chain348.txt before the run): "every house fronts the street", not "every house
    // faces the same axis" - the street has a corner, and houses line both of its legs
    std::printf("[ M5f-2b ] corner entry, 8 seeds: %d lots on the edge leg (facing +-X), %d ways beyond the street, %d doors off the street, widest gap %d%s",
                cornerSideOn, cornerExtraWays, cornerOffStreet, cornerGap, "\n");
    EXPECT_EQ(cornerUnderMin, 0) << "corner entry: plans under the tier's minimum of lots";
    EXPECT_EQ(cornerExtraWays, 0) << "corner entry: a lot on a lane of its own, off the street";
    // PL.12 (b): water on the very cell the bent street is aimed at (4, 44) - the street must not run through it
    {
        const WorldForgePlan::TierPreset rr = WorldForgePlan::tierPresetDerived("hamlet", "roman");
        SettlementMapInput in;
        in.tier = tier; in.rooms = &rooms; in.era = "roman"; in.seed = 5; in.W = rr.width; in.D = rr.depth;
        in.heightAt = flatHeight;
        in.waterAt = [](int x, int z) { return x >= 3 && x <= 5 && z >= 43 && z <= 45; };
        in.entries = {{0, 4, 1}};
        const SettlementMap m = planSettlementMap(in);
        const Violations v = measure(m, in.heightAt, in.maxRelief, in.waterAt);
        std::printf("[ M5f-2b ] wet mid-cell (3..5, 43..45), corner entry, seed 5: %zu lots, %zu ways, %d wet street segments%s%s", m.lots.size(), m.ways.size(), v.wayInWater,
                    m.ok() ? "" : (" REFUSED: " + m.refusal).c_str(), "\n");
        EXPECT_EQ(v.wayInWater, 0) << "the street runs through the water on the cell it was aimed at";
        EXPECT_TRUE(m.ok()) << m.refusal;
    }
    EXPECT_EQ(cornerOffStreet, 0) << "corner entry: doors further than 9 cubes from the street's line";
    EXPECT_LE(cornerGap, 2) << "corner entry: a gap wider than 2 between neighbours facing the same way";
    EXPECT_EQ(onPrimary, lots) << "lots that do not front the through road";
    // 2 = two 1-cube setbacks on touching parcels; like the 9 above it was fixed after seeing the output
    EXPECT_LE(widestGap, 2) << "a gap between neighbouring houses on one side of the road wider than 2 cubes - not a row";
    EXPECT_EQ(overlapping, 0) << "pairs of footprints that overlap";
    EXPECT_GT(controlNarrowest, 2) << "the centre measure made a row on some seed: the control no longer shows the row check failing";
    EXPECT_EQ(plans, 8) << "seeds the planner refused";
    EXPECT_GE(plans ? fewest : 0, 5) << "a plan under the tier's minimum of 5";
    EXPECT_EQ(wrongType, 0);
    EXPECT_EQ(wrongStories, 0);
    EXPECT_EQ(notGableOn, 0) << "lots whose narrow side is not on the street";
    EXPECT_EQ(noDoor, 0);
    EXPECT_EQ(doorNotOnShortSide, 0);
}

// (a) the registry: an era's typology is found by ITS OWN NAME under its era, and a name two eras share resolves to the
// era asked for - never to the other one, never to nothing
TEST(SettlementMapTest, ASecondErasTypologyIsFoundByItsOwnName) {
    const FixtureEra fx(nlohmann::json{{"blockhouse", 1}});
    ASSERT_TRUE(fx.ok) << fx.why;
    const RoomProgram* b = fx.rooms.get("blockhouse", "fixture");
    EXPECT_NE(b, nullptr) << "the fixture era's blockhouse is not found by its name under its era";
    if (b) { EXPECT_EQ(b->period, "fixture"); EXPECT_EQ(b->stories, 3); }
    const RoomProgram* fc = fx.rooms.get("croft", "fixture");
    EXPECT_NE(fc, nullptr) << "the fixture era's croft is not found (the medieval one shadows it)";
    if (fc) { EXPECT_EQ(fc->period, "fixture"); EXPECT_EQ(fc->stories, 2) << "the croft handed back is not the fixture's"; }
    const RoomProgram* mc = fx.rooms.get("croft", "medieval");   // the controls
    ASSERT_NE(mc, nullptr);
    EXPECT_EQ(mc->period, "medieval");
    EXPECT_EQ(fx.rooms.get("blockhouse", "medieval"), nullptr) << "a medieval blockhouse appeared";
    EXPECT_EQ(fx.rooms.get("blockhouse", "nonesuch"), nullptr);
}

// (b) the planner: a hamlet of the fixture era is planned from the FIXTURE's typology - its footprint, its storeys
TEST(SettlementMapTest, ASecondEraPlansItsOwnBuildings) {
    const FixtureEra fx(nlohmann::json{{"blockhouse", 1}});
    ASSERT_TRUE(fx.ok) << fx.why;
    int plans = 0, lots = 0, wrongType = 0, standIn = 0, wrongFoot = 0, wrongStories = 0;
    for (unsigned seed = 1; seed <= 5; ++seed) {
        const SettlementMapInput in = fx.input(seed);
        ASSERT_NE(in.tier, nullptr) << "the fixture era has no hamlet tier";
        const SettlementMap m = planSettlementMap(in);
        if (!m.ok()) { std::printf("[ M5f-1 ] fixture hamlet seed %u: REFUSED: %s%s", seed, m.refusal.c_str(), "\n"); continue; }
        ++plans;
        for (const auto& l : m.lots) {
            ++lots;
            if (l.typology != "blockhouse") ++wrongType;
            const int a = std::min(l.footprint.w, l.footprint.d), b = std::max(l.footprint.w, l.footprint.d);
            if (a == 6 && b == 8) ++standIn;
            if (a != 7 || b != 15) ++wrongFoot;
            if (l.stories != 3) ++wrongStories;
        }
        std::printf("[ M5f-1 ] fixture hamlet seed %u: era %s, %zu lots; first lot %s %d x %d, %d storeys%s", seed, m.era.c_str(), m.lots.size(),
                    m.lots.empty() ? "-" : m.lots[0].typology.c_str(), m.lots.empty() ? 0 : m.lots[0].footprint.w, m.lots.empty() ? 0 : m.lots[0].footprint.d,
                    m.lots.empty() ? 0 : m.lots[0].stories, "\n");
    }
    std::printf("[ M5f-1 ] fixture hamlets: %d plans, %d lots; wrong typology %d, the 8 x 6 stand-in %d, not 7 x 15 %d, not 3 storeys %d%s", plans, lots, wrongType, standIn, wrongFoot, wrongStories, "\n");
    EXPECT_GT(lots, 0) << "no fixture lot planned: nothing was measured";
    EXPECT_EQ(wrongType, 0);
    EXPECT_EQ(standIn, 0) << "lots planned as the 8 x 6 stand-in: the era's typology was not looked up";
    EXPECT_EQ(wrongFoot, 0) << "lots whose footprint is not the fixture's 7 x 15";
    EXPECT_EQ(wrongStories, 0) << "lots whose storeys are not the fixture's 3";
    // the audit of M5f-1 (finding 1): the name BOTH eras hold, through the planner - the fixture's croft (5 x 6, two
    // storeys), not the medieval one
    const FixtureEra fc(nlohmann::json{{"croft", 1}});
    ASSERT_TRUE(fc.ok) << fc.why;
    int cLots = 0, cWrong = 0;
    for (unsigned seed = 1; seed <= 5; ++seed) {
        const SettlementMap m = planSettlementMap(fc.input(seed));
        if (!m.ok()) { std::printf("[ M5f-1 ] fixture croft hamlet seed %u: REFUSED: %s%s", seed, m.refusal.c_str(), "\n"); continue; }
        for (const auto& l : m.lots) {
            ++cLots;
            const int a = std::min(l.footprint.w, l.footprint.d), b = std::max(l.footprint.w, l.footprint.d);
            if (l.typology != "croft" || a != 5 || b != 6 || l.stories != 2) {
                if (++cWrong <= 3) std::printf("[ M5f-1 ]   croft lot %s %d x %d, %d storeys%s", l.typology.c_str(), l.footprint.w, l.footprint.d, l.stories, "\n");
            }
        }
    }
    std::printf("[ M5f-1 ] fixture hamlets of the shared name croft: %d lots, %d not the fixture's 5 x 6 two-storey croft%s", cLots, cWrong, "\n");
    EXPECT_GT(cLots, 0) << "no fixture croft lot planned: nothing was measured";
    EXPECT_EQ(cWrong, 0) << "lots of the shared name that are not the fixture era's croft";
}

// (c) a typology the era's pack does not hold is REFUSED by name - never planned as a stand-in
TEST(SettlementMapTest, ATypologyTheEraLacksIsRefusedNotStoodIn) {
    const FixtureEra fx(nlohmann::json{{"nonesuch", 1}});
    ASSERT_TRUE(fx.ok) << fx.why;
    const SettlementMap m = planSettlementMap(fx.input(7));
    std::printf("[ M5f-1 ] a typology the era lacks: ok %d, %zu lots, refusal '%s'%s", m.ok() ? 1 : 0, m.lots.size(), m.refusal.c_str(), "\n");
    EXPECT_FALSE(m.ok()) << "the planner planned " << m.lots.size() << " lots of a typology that exists in no pack";
    EXPECT_NE(m.refusal.find("nonesuch"), std::string::npos) << "the refusal does not name the typology";
    EXPECT_NE(m.refusal.find("fixture"), std::string::npos) << "the refusal does not name the era";
    // the same name under the MEDIEVAL era is medieval's business: croft exists there, so a fixture tier asking for
    // "tavern" (medieval only) must be refused too - no borrowing across eras
    const FixtureEra fy(nlohmann::json{{"tavern", 1}});
    ASSERT_TRUE(fy.ok) << fy.why;
    const SettlementMap m2 = planSettlementMap(fy.input(7));
    std::printf("[ M5f-1 ] a medieval-only typology asked by the fixture era: ok %d, %zu lots, refusal '%s'%s", m2.ok() ? 1 : 0, m2.lots.size(), m2.refusal.c_str(), "\n");
    EXPECT_FALSE(m2.ok()) << "the fixture era borrowed the medieval tavern";
}
