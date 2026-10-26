#include <gtest/gtest.h>

#include "core/CivPlan.h"
#include "core/CorridorGraph.h"
#include "core/HydrologyMap.h"
#include "core/WorldForgeParams.h"
#include "core/WorldForgePlan.h"
#include "core/WorldGenerator.h"
#include "core/WorldRecipe.h"
#include "core/WorldSheet.h"

#include <algorithm>
#include <cmath>
#include <climits>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace Phyxel;

// ============================================================================
// RouteGraphTest — M4a (docs/WorldForgeV2Plan.md M4, D25 / D27 / D28): the continent corridor
// graph over the civ plan — every live site reaches its seat, corridors never cross sea or a
// basin, class by hierarchy, the reuse discount merges parallel runs into trunks, the box plan's
// roads follow the corridors, and beyond the box the corridor is stamped at macro fidelity.
// Red on the M3b code: the generator bakes no corridor graph.
// ============================================================================
namespace {

WorldRecipe civRecipe(uint32_t seed, float reuse = 1.0f / 3.0f) {
    WorldRecipe r;
    r.version = 2;
    r.type = "Perlin";
    r.seed = seed;
    r.worldforge.enabled = true;
    r.worldforge.civ = true;
    r.worldforge.routeReuseFraction = reuse;
    r.worldforge.siteCount = 8;
    r.worldforge.regionRadius = 2048;
    r.worldforge.minSpacing = 400;
    return r;
}

int expectedClass(const CivPlan::Site& a, const CivPlan::Site& b) {
    const int t = std::max(a.tier, b.tier);
    if (t <= CivPlan::City) return CorridorGraph::Highway;
    if (t <= CivPlan::Village) return CorridorGraph::Road;
    return CorridorGraph::Track;
}

void printGraph(const char* tag, const CorridorGraph& cg) {
    std::printf("[ M4a ] %s: %zu routes, %.1f km, shared cells %d, junction cells %d, unroutable %d, bake %.1f ms\n", tag,
                cg.routes().size(), cg.totalLengthU() / 1000.0f, cg.sharedCells(), cg.junctionCells(), cg.unroutable(), cg.bakeMs());
}

int distinctCells(const CorridorGraph& cg) {
    std::set<int> cells;
    for (const auto& r : cg.routes()) for (int c : r.cells) cells.insert(c);
    return static_cast<int>(cells.size());
}

}  // namespace

TEST(RouteGraphTest, ContinentCorridorsReachEverySite) {
    WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u);
    g.applyRecipe(civRecipe(20260816u));
    const CivPlan* civ = g.civPlan();
    const CorridorGraph* cg = g.corridors();
    ASSERT_TRUE(civ);
    ASSERT_TRUE(cg) << "no corridor graph on a v2 world with worldforge.civ on";
    printGraph("baseline", *cg);
    for (const auto& s : cg->refusals()) std::printf("    %s\n", s.c_str());
    std::set<int> seats, routed;
    for (const auto& p : civ->provinces()) seats.insert(p.seat);
    for (const auto& r : cg->routes()) { routed.insert(r.a); routed.insert(r.b); }
    int liveUnrouted = 0;
    for (const auto& s : civ->sites())
        if (s.state != CivPlan::Abandoned && !routed.count(s.id)) ++liveUnrouted;
    // An unroutable demand is legitimate only when the site's LAND is not connected to its
    // seat's (an island, or land walled off by basins): flood the land cells and compare labels.
    const MacroSheet* M = g.macroSheet().get();
    const int n = M->cells();
    const float sea = g.getTerrainParams().seaLevelY;
    std::vector<int> comp(static_cast<size_t>(n) * n, -1);
    int comps = 0;
    for (int start = 0; start < n * n; ++start) {
        const MacroCell& c0 = M->cell(start % n, start / n);
        if (comp[static_cast<size_t>(start)] >= 0 || c0.baseHeight <= sea || c0.filled > c0.baseHeight + 1e-3f) continue;
        std::vector<int> stack{start}; comp[static_cast<size_t>(start)] = comps;
        while (!stack.empty()) {
            const int c = stack.back(); stack.pop_back();
            const int ci = c % n, cj = c / n;
            for (int dj = -1; dj <= 1; ++dj) for (int di = -1; di <= 1; ++di) {
                const int ni = ci + di, nj = cj + dj;
                if ((!di && !dj) || ni < 0 || nj < 0 || ni >= n || nj >= n) continue;
                const int nc = nj * n + ni;
                const MacroCell& mc = M->cell(ni, nj);
                if (comp[static_cast<size_t>(nc)] >= 0 || mc.baseHeight <= sea || mc.filled > mc.baseHeight + 1e-3f) continue;
                comp[static_cast<size_t>(nc)] = comps; stack.push_back(nc);
            }
        }
        ++comps;
    }
    int isolated = 0;
    for (const auto& p : civ->provinces())
        for (int m : p.members)
            if (m != p.seat && civ->sites()[static_cast<size_t>(m)].state != CivPlan::Abandoned &&
                comp[static_cast<size_t>(civ->sites()[static_cast<size_t>(m)].cell)] != comp[static_cast<size_t>(civ->sites()[static_cast<size_t>(p.seat)].cell)]) ++isolated;
    std::printf("[ M4a ] %zu seats, %zu sites on a route, %d live sites on none, %d land components, %d members not land-connected to their seat\n",
                seats.size(), routed.size(), liveUnrouted, comps, isolated);
    EXPECT_EQ(cg->unroutable(), isolated) << "unroutable demands must be exactly the members on land not connected to their seat";
    EXPECT_EQ(liveUnrouted, isolated) << "a land-connected live site has no corridor";
    EXPECT_GE(cg->routes().size(), civ->sites().size() - seats.size()) << "fewer routes than member sites";
    EXPECT_LT(cg->bakeMs(), 20000.0f) << "corridor bake gate (this build config)";
}

TEST(RouteGraphTest, NoCorridorThroughSeaOrBasin) {
    WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u);
    g.applyRecipe(civRecipe(20260816u));
    const CorridorGraph* cg = g.corridors();
    ASSERT_TRUE(cg);
    const MacroSheet* M = g.macroSheet().get();
    const int n = M->cells();
    const float sea = g.getTerrainParams().seaLevelY;
    int wet = 0, cells = 0;
    for (const auto& r : cg->routes())
        for (int c : r.cells) {
            ++cells;
            const MacroCell& mc = M->cell(c % n, c / n);
            if (mc.baseHeight <= sea || mc.filled > mc.baseHeight + 1e-3f) ++wet;
        }
    std::printf("[ M4a ] %d corridor cells, %d in sea or a basin\n", cells, wet);
    EXPECT_EQ(wet, 0);
}

TEST(RouteGraphTest, ClassFollowsTheHierarchy) {
    WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u);
    g.applyRecipe(civRecipe(20260816u));
    const CivPlan* civ = g.civPlan();
    const CorridorGraph* cg = g.corridors();
    ASSERT_TRUE(civ && cg);
    int wrong = 0, highways = 0, roads = 0, tracks = 0;
    for (const auto& r : cg->routes()) {
        if (r.cls != expectedClass(civ->sites()[static_cast<size_t>(r.a)], civ->sites()[static_cast<size_t>(r.b)])) ++wrong;
        if (r.cls == CorridorGraph::Highway) ++highways; else if (r.cls == CorridorGraph::Road) ++roads; else ++tracks;
    }
    std::printf("[ M4a ] classes: %d highway, %d road, %d track; %d against the hierarchy\n", highways, roads, tracks, wrong);
    EXPECT_EQ(wrong, 0);
    EXPECT_GT(highways, 0) << "two cities, no highway";
    EXPECT_GT(tracks, roads) << "the ladder is a pyramid: more tracks than roads";
}

// D27: with the reuse discount later routes merge into earlier trunks — the network has fewer
// distinct cells and more shared cells than the same demand routed without it.
TEST(RouteGraphTest, ReuseDiscountMergesRoutesIntoTrunks) {
    WorldGenerator a(WorldGenerator::GenerationType::Perlin, 20260816u), b(WorldGenerator::GenerationType::Perlin, 20260816u);
    a.applyRecipe(civRecipe(20260816u, 1.0f / 3.0f));
    b.applyRecipe(civRecipe(20260816u, 1.0f));
    ASSERT_TRUE(a.corridors() && b.corridors());
    printGraph("reuse 1/3", *a.corridors());
    printGraph("reuse 1 (none)", *b.corridors());
    const int da = distinctCells(*a.corridors()), db = distinctCells(*b.corridors());
    std::printf("[ M4a ] distinct network cells: %d with reuse, %d without\n", da, db);
    EXPECT_LT(da, db) << "the reuse discount did not shrink the network";
    EXPECT_GT(a.corridors()->sharedCells(), b.corridors()->sharedCells());
    EXPECT_EQ(a.corridors()->routes().size(), b.corridors()->routes().size());
    // the effect that matters (measured 295 vs 636 = 0.46): merged junctions, not a shorter network
    EXPECT_LE(a.corridors()->junctionCells(), b.corridors()->junctionCells() * 7 / 10)
        << "the reuse discount did not merge junctions (with " << a.corridors()->junctionCells() << ", without " << b.corridors()->junctionCells() << ")";
}

TEST(RouteGraphTest, DeterministicAcrossGenerators) {
    WorldGenerator a(WorldGenerator::GenerationType::Perlin, 20260816u), b(WorldGenerator::GenerationType::Perlin, 20260816u);
    a.applyRecipe(civRecipe(20260816u)); b.applyRecipe(civRecipe(20260816u));
    ASSERT_TRUE(a.corridors() && b.corridors());
    ASSERT_EQ(a.corridors()->routes().size(), b.corridors()->routes().size());
    for (size_t i = 0; i < a.corridors()->routes().size(); ++i) {
        EXPECT_EQ(a.corridors()->routes()[i].cells, b.corridors()->routes()[i].cells);
        EXPECT_EQ(a.corridors()->routes()[i].cls, b.corridors()->routes()[i].cls);
    }
}

// The box plan's roads follow the continent corridors: the share of centreline vertices inside
// the corridor band is higher with the band discount than without it, and most of the network is
// inside the band. Prediction written first: ≥ 50 % inside the band with the discount.
TEST(RouteGraphTest, BoxRoadsFollowTheCorridors) {
    auto inBand = [](WorldGenerator& g, int& verts) {
        const WorldForgePlan* box = g.worldForge();
        const CorridorGraph* cg = g.corridors();
        if (!box || !cg) return -1.0f;
        int inside = 0; verts = 0;
        for (const auto& road : box->roads())
            for (const auto& v : road.centerline) {
                ++verts;
                int cls = 0;
                if (cg->distanceToCorridorU(v.x, v.y, &cls) <= 256.0f) ++inside;
            }
        return verts ? static_cast<float>(inside) / verts : 0.0f;
    };
    WorldGenerator a(WorldGenerator::GenerationType::Perlin, 20260816u), b(WorldGenerator::GenerationType::Perlin, 20260816u);
    a.applyRecipe(civRecipe(20260816u, 1.0f / 3.0f));
    b.applyRecipe(civRecipe(20260816u, 1.0f));
    int va = 0, vb = 0;
    const float fa = inBand(a, va), fb = inBand(b, vb);
    std::printf("[ M4a ] box road vertices inside the corridor band: %.1f %% of %d with the discount, %.1f %% of %d without\n", fa * 100.0f, va, fb * 100.0f, vb);
    ASSERT_GE(fa, 0.0f); ASSERT_GE(fb, 0.0f);
    EXPECT_GE(fa, 0.5f) << "the box roads do not follow the corridors";
    EXPECT_GT(fa, fb) << "the band discount changed nothing";
}

// D28: beyond the hydrology box the corridor is stamped at macro fidelity — a column on a
// corridor polyline outside the box carries the class and material, a column 100 u off it does
// not, and no column INSIDE the box is stamped by the corridor (the box plan owns it there).
TEST(RouteGraphTest, CorridorsAreStampedBeyondTheBox) {
    WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u);
    g.applyRecipe(civRecipe(20260816u));
    const CorridorGraph* cg = g.corridors();
    ASSERT_TRUE(cg);
    const HydrologyMap* h = g.hydrology();
    ASSERT_TRUE(h);
    const float bx0 = h->originX(), bz0 = h->originZ(), bx1 = bx0 + h->cellsX() * h->cellSize(), bz1 = bz0 + h->cellsZ() * h->cellSize();
    auto inBox = [&](const glm::vec2& p) { return p.x >= bx0 && p.x < bx1 && p.y >= bz0 && p.y < bz1; };
    int tested = 0, stamped = 0, offStamped = 0, wetSkipped = 0;
    for (const auto& r : cg->routes()) {
        if (tested >= 40) break;
        for (size_t k = 1; k + 1 < r.polyline.size(); k += 7) {
            const glm::vec2 p = r.polyline[k];
            if (inBox(p)) continue;   // outside the box only (no exclusion band: the edge is the seam under test)
            const auto col = g.sampleSurface(static_cast<int>(std::lround(p.x)), static_cast<int>(std::lround(p.y)));
            ++tested;
            if (col.surfaceY < 16) { ++wetSkipped; continue; }
            if (col.roadClass == r.cls && col.surfaceMat == WorldForgePlan::roadMaterial(r.cls)) ++stamped;
            const glm::vec2 d = glm::normalize(r.polyline[k + 1] - r.polyline[k - 1]);
            const glm::vec2 off = p + glm::vec2(-d.y, d.x) * 100.0f;
            const auto colOff = g.sampleSurface(static_cast<int>(std::lround(off.x)), static_cast<int>(std::lround(off.y)));
            if (colOff.roadClass > 0) ++offStamped;
            if (tested >= 40) break;
        }
    }
    std::printf("[ M4a ] D28 stamp: %d corridor columns outside the box, %d stamped with their class, %d below sea (skipped), %d off-corridor columns stamped\n",
                tested, stamped, wetSkipped, offStamped);
    EXPECT_GT(tested, 20);
    EXPECT_EQ(stamped, tested - wetSkipped) << "a corridor column outside the box carries no road";
    EXPECT_EQ(offStamped, 0);
}

// Stress: the x10-density continent (≈ 1 470 live sites) — every demand routed or refused with a
// reason, no corridor cell in water, the cost measured.
TEST(RouteGraphTest, StressTenTimesTheSites) {
    WorldRecipe r = civRecipe(20260816u);
    r.worldforge.civDensityScale = 10.0f;
    WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u);
    g.applyRecipe(r);
    const CivPlan* civ = g.civPlan();
    const CorridorGraph* cg = g.corridors();
    ASSERT_TRUE(civ && cg);
    printGraph("x10 density", *cg);
    const MacroSheet* M = g.macroSheet().get();
    const int n = M->cells();
    int wet = 0;
    for (const auto& rt : cg->routes())
        for (int c : rt.cells) {
            const MacroCell& mc = M->cell(c % n, c / n);
            if (mc.baseHeight <= g.getTerrainParams().seaLevelY || mc.filled > mc.baseHeight + 1e-3f) ++wet;
        }
    std::set<int> seats;
    for (const auto& p : civ->provinces()) seats.insert(p.seat);
    EXPECT_EQ(wet, 0);
    EXPECT_GE(cg->routes().size() + static_cast<size_t>(cg->unroutable()), civ->sites().size() - seats.size());
    EXPECT_EQ(static_cast<int>(cg->refusals().size()), cg->unroutable()) << "every unroutable demand names itself";
    EXPECT_LT(cg->bakeMs(), 120000.0f);
}

// Audit 2026-10-22: the box plan's roads end at the box edge inset by the pin margin while the
// macro stamp began only at the full box edge — a 512 u ring with neither (at every exit). Walk
// every exit road's end outward along its corridor to 512 u past the box edge, sampling every
// 16 u: each column on the corridor (within the class half-width) must carry a road, from the
// fine road's last vertex to the macro stamp, with no gap.
TEST(RouteGraphTest, FineRoadMeetsTheMacroStampAtTheBoxEdge) {
    WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u);
    g.applyRecipe(civRecipe(20260816u));
    const CorridorGraph* cg = g.corridors();
    const WorldForgePlan* box = g.worldForge();
    const HydrologyMap* h = g.hydrology();
    ASSERT_TRUE(cg && box && h);
    const float bx0 = h->originX(), bz0 = h->originZ(), bx1 = bx0 + h->cellsX() * h->cellSize(), bz1 = bz0 + h->cellsZ() * h->cellSize();
    auto inBox = [&](const glm::vec2& p) { return p.x >= bx0 && p.x < bx1 && p.y >= bz0 && p.y < bz1; };
    int exits = 0, samples = 0, gaps = 0, exitsWithGap = 0;
    int gapInset = 0, gapRing = 0, gapOutside = 0, wetSkipped = 0;   // where the gaps are: inside the 512 u inset box / the ring / outside
    auto inInset = [&](const glm::vec2& p) { return p.x >= bx0 + 512.0f && p.x < bx1 - 512.0f && p.y >= bz0 + 512.0f && p.y < bz1 - 512.0f; };
    for (const auto& road : box->roads()) {
        if (road.a >= 0 && road.b >= 0) continue;
        if (road.centerline.size() < 2) continue;
        for (int end = 0; end < 2; ++end) {
            if ((end == 0 && road.a >= 0) || (end == 1 && road.b >= 0)) continue;
            const glm::vec2 e = end == 0 ? road.centerline.front() : road.centerline.back();
            int cls = 0, route = -1;
            const float d0 = cg->distanceToCorridorU(e.x, e.y, &cls, &route);
            if (route < 0 || d0 > 256.0f) continue;
            const auto& pl = cg->routes()[static_cast<size_t>(route)].polyline;
            // nearest polyline vertex to the road end, then walk outward (toward the box edge and beyond)
            size_t k0 = 0; float bestD = 1e30f;
            for (size_t k = 0; k < pl.size(); ++k) { const float d = glm::length(pl[k] - e); if (d < bestD) { bestD = d; k0 = k; } }
            ++exits;
            int gapHere = 0;
            // walk BOTH ways from the road end along the corridor, 1024 u each way, and judge only
            // the ring and the outside: inside the inset box the fine road is free to leave the
            // macro polyline (the band discount is a bias, 96 % of vertices inside it, not a wall)
            for (int dir = -1; dir <= 1; dir += 2) {
                float walked = 0.0f;
                for (int k = static_cast<int>(k0); k + dir >= 0 && k + dir < static_cast<int>(pl.size()) && walked < 1024.0f; k += dir) {
                    const glm::vec2 a = pl[static_cast<size_t>(k)], b = pl[static_cast<size_t>(k + dir)];
                    const float len = glm::length(b - a);
                    for (float t = 0.0f; t < len && walked + t < 1024.0f; t += 16.0f) {
                        const glm::vec2 p = a + (b - a) * (t / len);
                        if (inInset(p)) continue;
                        const auto col = g.sampleSurface(static_cast<int>(std::lround(p.x)), static_cast<int>(std::lround(p.y)));
                        if (col.surfaceY < 16 || col.riverOrder > 0 || col.bridgeDeckY != INT_MIN) continue;
                        // a column under standing water (a meso lake the 512 u macro corridor cannot
                        // see) is never stamped - a causeway must not dam a lake; the diagnostics
                        // found the 12 residual "gaps" to be ONE such pair of columns hit by six exits
                        if (g.waterLevelAt(p.x, p.y) > static_cast<float>(col.surfaceY)) { ++wetSkipped; continue; }
                        ++samples;
                        bool road = col.roadClass > 0;
                        if (!road && inBox(p)) { const auto rh = box->roadAt(p.x, p.y); road = rh.cls > 0 && rh.dist <= 256.0f; }
                        if (!road) {
                            ++gaps; ++gapHere; if (inBox(p)) ++gapRing; else ++gapOutside;
                            int gcls = 0, groute = -1;
                            const float gd = cg->distanceToCorridorU(p.x, p.y, &gcls, &groute);
                            const auto rh = box->roadAt(p.x, p.y);
                            if (gaps <= 12)
                                std::printf("    gap at (%.0f,%.0f): inBox %d, corridor d %.1f cls %d route %d (road end at %.0f,%.0f, %.0f u away), roadAt cls %d dist %.1f, column class %d mat %s\n",
                                            p.x, p.y, inBox(p) ? 1 : 0, gd, gcls, groute, e.x, e.y, glm::length(p - e), rh.cls, rh.dist, col.roadClass, col.surfaceMat.c_str());
                        }
                    }
                    walked += len;
                }
            }
            if (gapHere) ++exitsWithGap;
        }
    }
    std::printf("[ M4a ] box-edge seam: %d exits walked, %d dry columns sampled, %d without a road (%d exits with a gap): %d inside the inset box, %d in the 512 u ring, %d outside; %d columns under standing water skipped\n",
                exits, samples, gaps, exitsWithGap, gapInset, gapRing, gapOutside, wetSkipped);   // gapInset stays 0 by construction
    EXPECT_GT(exits, 5);
    EXPECT_EQ(gaps, 0) << "a corridor column between the fine road's end and the macro stamp carries no road";
}
